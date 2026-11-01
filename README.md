# Skiscanner

Firmware für den Hardware-Skiscanner der Skiservice-Software. Ein ESP32-S3 nimmt die QR-Codes vom USB-Barcodescanner (Netum NS-91) an und schickt sie per WLAN an die Skiservice-API. Je nach Modus wird damit ein Service-Ski fertig gestellt oder eine Bindung als geprüft markiert.

Der Scanner meldet sich alle 30 s beim Backend. Das Skiservice Center zeigt deshalb in der Hardware-Scanner-Ansicht, welche Scanner verbunden sind.

---

## Hardware

| Was              | Welcher Port am ESP32-S3     |
|------------------|------------------------------|
| Scanner (USB)    | **USB-Port** (nativer OTG)   |
| PC / Flashen     | **COM-Port** (UART-Chip)     |

> ⚠️ **Niemals beide Ports gleichzeitig am PC anschließen!**
> Der COM-Port liefert 5 V über den USB-OTG-Port zurück.

- Benötigter Adapter: **USB-C zu USB-A (OTG)** für den USB-Port des Dev Kits.
- Status-LED: die eingebaute RGB-LED des Boards (WS2812 an GPIO 48), siehe [Status-LED](#status-led).

---

## Einrichtung

**Am schnellsten mit dem Webflasher:** „Neuinstallation“ wählen und WLAN, Server-Adresse, Name und Modus gleich mit eintragen. Nach dem Flashen ist der Scanner sofort einsatzbereit (siehe [Webflasher](#webflasher)).

**Ohne vorher eingetragenes WLAN** startet der Scanner das Einrichtungs-WLAN (LED blinkt blau):

1. Mit dem WLAN **`Skiscanner-Config`** verbinden (Passwort `12345678`).
2. **`http://192.168.4.1`** öffnen.
3. WLAN-Name, Passwort, Server-Adresse, Name und Modus eintragen und speichern. Der Scanner startet neu und verbindet sich.

Findet der Scanner sein WLAN beim Start nicht innerhalb von etwa 10 s, öffnet er ebenfalls das Einrichtungs-WLAN.

Ist er verbunden, erreicht man die **Konfigurationsseite** unter `http://<IP des Scanners>`. Die IP steht im Router oder in der seriellen Ausgabe. Dort kann man:

- Server-Adresse, Name und Modus ändern
- mit „API testen“ prüfen, ob die API erreichbar ist
- den Status sehen: USB-Scanner, WLAN-Signal, API, Firmware-Version
- ein Update einspielen
- den Scanner zurücksetzen; alle Einstellungen werden gelöscht

**Server-Adresse:** Eingetragen wird nur die Adresse des Backends mit Port, z. B. `http://192.168.1.10:8000`. Die Pfade ergänzt die Firmware.

**Name:** Der Name muss eindeutig sein, z. B. `Werkstatt`. Im Center legt jedes Tablet über das Zahnrad fest, auf welchen Scanner es hört. Dann sieht es nur dessen Status und Scans.

---

## Modi

| Modus | trigger | Erkannte Etiketten | Wirkung |
|---|---|---|---|
| Fertigstellen | 1 | nur Service-Etiketten | Der Ski wird im Skiservice Center ausgewählt und nach dem Countdown fertig gestellt |
| Bindung prüfen | 2 | Service- und Verleih-Etiketten | Die Bindung wird auf geprüft gesetzt. Beim Service-Ski muss ein Bindungscheck beauftragt sein, beim Verleih-Material muss ein Ski dabei sein |
| Bearbeitung | 0 | – | noch ohne Funktion |

Welche Art von Etikett gescannt wurde, erkennt das Backend an der URL im QR-Code: `…/skiservice/anzeigen/…` für Service, `…/saisonverleih/anzeigen/…` für Verleih.

---

## Status-LED

**Grundregel:**

- **Dauerhaft grün** heißt, alles ist bereit zum Scannen.
- Gibt es ein Problem, **blinkt die LED alle 3 Sekunden zweimal**. Die Farbe sagt, was fehlt.
- Es wird immer nur das **wichtigste** Problem angezeigt, in der Reihenfolge der Tabelle. Erst wenn es behoben ist, kommt das nächste an die Reihe.

### Dauerstatus

| | Farbe | Muster | Bedeutung | Was tun |
|---|---|---|---|---|
| 1 | 🔴 Rot | 2× normal, alle 3 s | Kein USB-Scanner erkannt | Den Scanner am **USB-Port** (OTG) einstecken, nicht am COM-Port. Adapter und OTG-Jumper prüfen |
| 2 | 🔵 Blau | 2× langsam, alle 3 s | Einrichtungsmodus, das WLAN `Skiscanner-Config` ist offen | Verbinden und `http://192.168.4.1` öffnen, siehe [Einrichtung](#einrichtung). Kommt auch, wenn das eingetragene WLAN beim Start nicht erreichbar war |
| 3 | 🟢 Grün | 2× kurz, alle 3 s | WLAN-Verbindung verloren | Router, Reichweite und WLAN-Passwort prüfen. Der Scanner verbindet sich von selbst neu |
| 4 | 🟠 Orange | 2× normal, alle 3 s | Keine Server-Adresse eingetragen | Auf der Konfigurationsseite die Server-Adresse eintragen |
| 5 | 🟣 Lila | 2× normal, alle 3 s | API oder Datenbank nicht erreichbar | Prüfen, ob die API läuft und ob Server-Adresse und Port stimmen. „API testen“ auf der Konfigurationsseite zeigt den Fehler |
| – | 🟢 Grün | dauerhaft | Alles bereit | – |

Die API wird alle 30 s geprüft, nach einem Fehler alle 10 s und nach einem fehlgeschlagenen Scan sofort. Lila verschwindet also spätestens 10 s, nachdem die API wieder läuft.

### Rückmeldung nach einem Scan

Nach einem Scan blinkt die LED einmal, danach zeigt sie wieder den Dauerstatus.

| Farbe | Muster | Bedeutung |
|---|---|---|
| 🟢 Grün | 2× kurz, dann wieder dauerhaft grün | Scan übernommen |
| 🔴 Rot | 2× kurz | Scan abgelehnt, z. B. Verleih-Etikett im Modus Fertigstellen, kein Bindungscheck beauftragt, unbekannter Code. Oder die API antwortet nicht. Der Grund steht in der seriellen Ausgabe |
| 🔴 Rot | 2× normal | Kein WLAN, der Code wurde **nicht** gesendet |

> Grün kurz blinkend gibt es zweimal: Nach einem Scan blinkt es **einmal** und die LED wird danach wieder dauerhaft grün. Ohne WLAN wiederholt es sich **alle 3 s**, und die LED bleibt dazwischen aus.

### Blinkmuster

| Muster | an | aus |
|---|---|---|
| kurz | 0,2 s | 0,1 s |
| normal | 0,5 s | 0,25 s |
| langsam | 0,8 s | 0,4 s |

Farben und Zeiten stehen in `src/led.cpp`, die Reihenfolge der Prüfungen in `loop()` in `src/main.cpp`.

---

## Status & Überwachung

- **Am Scanner:** `http://<IP>/health` liefert den Zustand als JSON: Firmware, Laufzeit, USB-Scanner, Modus, Name, Server, WLAN mit Signal und API. Antwortet mit HTTP 200, wenn alles in Ordnung ist, sonst mit 503. Die Konfigurationsseite fragt das alle 5 s ab.
- **Im Center:** Bei jeder API-Prüfung meldet sich der Scanner beim Backend an (`POST /api/v1/scanner/skiscanner/melden`), mit Name, Modus, Firmware, IP und USB-Scanner-Status. Das Center zeigt den Scanner dann als
  - **verbunden**, mit Name und Modus,
  - **USB-Scanner fehlt** oder
  - **getrennt**, wenn er sich länger als 75 s nicht gemeldet hat.

  Beim Ein- und Ausstecken des USB-Scanners meldet er sich sofort.

---

## Update

Die Einstellungen liegen im NVS und bleiben bei einem Update erhalten.

- **Webflasher → „Update“:** per USB-Kabel am COM-Port, siehe unten.
- **Konfigurationsseite → Update:** per WLAN, ohne Kabel. `firmware.bin` aktualisiert die Firmware, `littlefs.bin` die Konfigurationsseiten. Eine Datei mit „littlefs“ im Namen wird automatisch als Dateisystem eingespielt.

Geräte mit der **alten Partitionstabelle** (Firmware vor der Umstellung auf NVS) brauchen **einmal** eine Neuinstallation über den Webflasher.

---

## Webflasher

Der Workflow veröffentlicht den Webflasher (`webflasher/index.html`) zusammen mit der aktuellen Firmware auf **GitHub Pages**. Dafür muss im Repo unter Settings → Pages → Source „GitHub Actions“ eingestellt sein. Er läuft in Chrome oder Edge, der ESP32-S3 hängt dabei am **COM-Port**.

- **Neuinstallation:**
  - Löscht den kompletten Flash und schreibt Bootloader, Partitionstabelle, Firmware und Konfigurationsseiten.
  - WLAN, Server-Adresse, Name und Modus lassen sich direkt eingeben. Sie landen in der Partition `wcfg` und werden beim ersten Start übernommen.
  - Nötig für neue Geräte und einmalig für Geräte mit der alten Partitionstabelle.
- **Update:** schreibt nur Firmware und Konfigurationsseiten, die Einstellungen bleiben erhalten.

Statt der Online-Version kann man eigene Dateien wählen: `scanner_vollstaendig.bin` bzw. `firmware.bin` + `littlefs.bin` aus dem Release.

Lokal testen:
1. Nach dem Build `python tools/release.py <ordner>` ausführen.
2. `webflasher/` und den Ordner als `firmware/` per `python -m http.server` ausliefern.

---

## Entwicklung

PlatformIO, Umgebung `esp32s3-n16r8`:

```bash
pio run                      # bauen
pio run --target upload      # Firmware flashen (COM-Port)
pio run --target uploadfs    # Konfigurationsseiten (data/*.html) flashen, nach Änderungen in data/ nötig
pio device monitor           # serielle Ausgabe, 115200 Baud
```

Die serielle Ausgabe zeigt u. a. `[USB]` (Scanner erkannt), `[SCAN]` (gescannter Code und Antwort der API) und `[HEALTH]` (API erreichbar oder nicht).

- **Release:**
  - Bei jedem Push auf `main`/`master` baut der Workflow die Firmware mit der Version `si-<datum>-<sha>`.
  - `tools/release.py` erzeugt daraus das vollständige Image und `manifest.json`.
  - Das Ergebnis wird als GitHub-Release und mit dem Webflasher auf GitHub Pages veröffentlicht.
- **Lokale Builds** heißen `dev-<git sha>` (`version.py`).

**Partitionstabelle** (`partitions.csv`, 16 MB):

| Partition | Offset | Größe | Inhalt |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | Einstellungen |
| otadata | 0xE000 | 8 KB | aktive App |
| app0 / app1 | 0x10000 / 0x650000 | je 6,25 MB | Firmware (OTA) |
| wcfg | 0xC90000 | 4 KB | Einrichtung aus dem Webflasher, wird nach dem Übernehmen gelöscht |
| spiffs | 0xC91000 | ca. 3,4 MB | LittleFS mit den Konfigurationsseiten |
| coredump | 0xFF0000 | 64 KB | Absturzdaten |

---

## Fehlersuche

| Problem | Lösung |
|---|---|
| LED blinkt rot, Scanner steckt | Scanner am USB-Port (OTG) statt am COM-Port? OTG-Jumper am Board geschlossen? |
| LED blinkt lila, die API läuft aber | Server-Adresse mit Port prüfen (z. B. `:8000`), Firewall des Servers, „API testen“ auf der Konfigurationsseite |
| Im Center „getrennt“ | LED am Scanner ansehen. Grün kurz = WLAN, lila = API. Sonst Strom und Name prüfen |
| Im Center nicht in der Liste | Der Scanner hat sich noch nie gemeldet: Server-Adresse prüfen. Firmware vor dieser Version erscheint erst nach dem ersten Scan |
| Scan blinkt rot | Modus prüfen: Fertigstellen nimmt nur Service-Etiketten. Bindung prüfen braucht einen beauftragten Bindungscheck bzw. Verleih-Material mit Ski. Details in der seriellen Ausgabe |
| Kein Serial Monitor | COM-Port nutzen, nicht den USB-Port |
| Falsche Zeichen (ü, ä, ö) | NS-91 auf deutsches Tastaturlayout umprogrammieren |
| Scan kommt ohne Enter | Per Programmierbarcode das Enter-Suffix am NS-91 aktivieren |
