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

## Modi

| Modus | trigger | Erkannte Etiketten | Wirkung |
|---|---|---|---|
| Fertigstellen | 1 | nur Service-Etiketten | Ski wird im Skiservice Center ausgewählt und nach dem Countdown fertig gestellt |
| Bindung prüfen | 2 | Service- und Verleih-Etiketten | Bindung wird auf geprüft gesetzt |
| Bearbeitungsstand | 0 | – | noch ohne Funktion |

Eingetragen wird nur die **Server-Adresse** des Backends (z. B. `http://192.168.1.10:8000`), die Pfade ergänzt die Firmware.

## Status-LED

| LED | Bedeutung |
|---|---|
| dauerhaft grün | alles bereit |
| blinkt rot | kein USB-Scanner |
| blinkt blau (langsam) | Einrichtungsmodus, WLAN `Skiscanner-Config` (Passwort `12345678`) |
| blinkt grün | keine WLAN-Verbindung |
| blinkt orange | keine Server-Adresse eingetragen |
| blinkt lila | API nicht erreichbar (Healthcheck alle 30 s, nach Fehler alle 10 s) |
| nach Scan 2× grün kurz | Scan übernommen |
| nach Scan 2× rot kurz | Scan abgelehnt (z. B. Verleih-Etikett im Modus Fertigstellen) oder API-Fehler |

Der Scanner liefert unter `http://<IP>/health` seinen Zustand als JSON (HTTP 200 wenn alles ok, sonst 503).

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
