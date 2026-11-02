"""
Erzeugt aus dem PlatformIO-Build die Dateien für Release und Webflasher:
  scanner_vollstaendig.bin  komplettes Image ab 0x0 (Neuinstallation per esptool)
  bootloader.bin, partitions.bin, boot_app0.bin, firmware.bin, littlefs.bin
  manifest.json             Version, Dateien und Offsets für den Webflasher

Aufruf (nach "pio run" und "pio run -t buildfs"):
  python tools/release.py <ausgabeordner> [version]
Die Offsets kommen aus partitions.csv.
"""
import csv
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJEKT = Path(__file__).resolve().parent.parent
BUILD = PROJEKT / ".pio" / "build" / "esp32s3-n16r8"
PIO = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
BOOT_APP0 = PIO / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
ESPTOOL = PIO / "packages" / "tool-esptoolpy" / "esptool.py"


def partitionen() -> dict[str, tuple[int, int]]:
    """Name -> (Offset, Größe) aus partitions.csv"""
    ergebnis = {}
    with open(PROJEKT / "partitions.csv") as f:
        zeilen = (z for z in f if z.strip() and not z.lstrip().startswith("#"))
        for name, _typ, _subtyp, offset, groesse, *_ in csv.reader(zeilen):
            ergebnis[name.strip()] = (int(offset, 0), int(groesse, 0))
    return ergebnis


def main():
    ziel = Path(sys.argv[1])
    version = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("FIRMWARE_VERSION", "dev")
    ziel.mkdir(parents=True, exist_ok=True)
    teile = partitionen()

    dateien = {
        "bootloader.bin": (BUILD / "bootloader.bin", 0x0),
        "partitions.bin": (BUILD / "partitions.bin", 0x8000),
        "boot_app0.bin": (BOOT_APP0, teile["otadata"][0]),
        "firmware.bin": (BUILD / "firmware.bin", teile["app0"][0]),
        "littlefs.bin": (BUILD / "littlefs.bin", teile["spiffs"][0]),
    }
    for name, (quelle, _) in dateien.items():
        shutil.copyfile(quelle, ziel / name)

    # Komplettes Image, Flash-Modus und -Größe stehen bereits im Bootloader
    # esptool als Modul (CI: pip install esptool), sonst das aus den PlatformIO-Paketen
    try:
        import esptool  # noqa: F401
        befehl = [sys.executable, "-m", "esptool"]
    except ImportError:
        befehl = [sys.executable, str(ESPTOOL)]
    befehl += ["--chip", "esp32s3", "merge_bin", "-o", str(ziel / "scanner_vollstaendig.bin"),
               "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "16MB"]
    for name, (_, offset) in sorted(dateien.items(), key=lambda d: d[1][1]):
        befehl += [hex(offset), str(ziel / name)]
    subprocess.run(befehl, check=True)

    eintrag = lambda name: {"datei": name, "offset": dateien[name][1]}
    manifest = {
        "version": version,
        "chip": "ESP32-S3",
        # Neuinstallation: Flash wird komplett gelöscht, danach alle Teile geschrieben
        "neuinstallation": [eintrag(n) for n in ("bootloader.bin", "partitions.bin", "boot_app0.bin", "firmware.bin", "littlefs.bin")],
        # Update: NVS (Einstellungen) bleibt, boot_app0 sorgt dafür, dass nach einem OTA wieder app0 startet
        "update": [eintrag(n) for n in ("boot_app0.bin", "firmware.bin", "littlefs.bin")],
        "wcfg": {"offset": teile["wcfg"][0], "groesse": teile["wcfg"][1]},
    }
    (ziel / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
