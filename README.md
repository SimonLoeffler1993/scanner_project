# Netum NS-91 → ESP32-S3 | Inventur-Scanner

## Schritt 1: QR/Barcode im Serial Monitor anzeigen

---

## Hardware

| Was              | Welcher Port am ESP32-S3     |
|------------------|------------------------------|
| Scanner (USB)    | **USB-Port** (nativer OTG)   |
| PC / Flashen     | **COM-Port** (UART-Chip)     |

> ⚠️ **Niemals beide Ports gleichzeitig am PC anschließen!**
> Der COM-Port liefert 5V über den USB-OTG-Port zurück.

Benötigter Adapter: **USB-C zu USB-A (OTG)** für den USB-Port des Dev Kits.

---

## Flashen & Testen

```bash
# Projekt öffnen
cd scanner_project

# Flashen (COM-Port)
pio run --target upload

# Serial Monitor öffnen (COM-Port)
pio device monitor
```

Dann Scanner einstecken (USB-Port) → im Terminal erscheint:
```
[USB] Gerät verbunden!
      VID: 0x xxxx  PID: 0x xxxx
[USB] Tastatur / Scanner erkannt → bereit!
```

QR-Code oder Barcode scannen → Ausgabe:
```
--------------------------------------------
SCAN: https://example.com/artikel/12345
Länge: 34 Zeichen
--------------------------------------------
```

---

## Troubleshooting

| Problem                        | Lösung                                              |
|-------------------------------|-----------------------------------------------------|
| Gerät wird nicht erkannt       | Prüfen ob USB-OTG Jumper am Board geschlossen ist   |
| Kein Serial Monitor            | COM-Port nutzen, nicht USB-Port                     |
| Falsche Zeichen (ü, ä, ö)     | NS-91 auf deutsches Tastaturlayout umprogrammieren  |
| Scan kommt ohne Enter          | Per Programmierbarcode Enter-Suffix aktivieren       |

---

## Nächste Schritte (Schritt 2)

In `main.cpp` ist bereits ein Platzhalter für den HTTP POST:

```cpp
// TODO Schritt 2: HTTP POST an dein Backend
// sendToApi(g_barcode);
```

Dafür wird `WiFi.h` + `HTTPClient.h` ergänzt.
