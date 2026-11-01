# Setzt die Firmware-Version als Build-Flag FIRMWARE_VERSION.
# In der CI kommt sie aus der Umgebungsvariablen FIRMWARE_VERSION, lokal "dev-<git sha>".
import os
import subprocess

Import("env")  # noqa: F821 (von PlatformIO bereitgestellt)

version = os.environ.get("FIRMWARE_VERSION")
if not version:
    try:
        sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=env.subst("$PROJECT_DIR"), text=True).strip()  # noqa: F821
    except Exception:
        sha = "unbekannt"
    version = f"dev-{sha}"

print(f"Firmware-Version: {version}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])  # noqa: F821
