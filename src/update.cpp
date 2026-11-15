#include "update.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include "api_client.h"
#include "health.h"
#include "zertifikate.h"

// Ordner mit manifest.json, firmware.bin und littlefs.bin (GitHub Pages, siehe tools/release.py).
// Fest eingebaut: die API stößt das Update nur an und kann keine andere Quelle vorgeben
#ifndef FIRMWARE_BASIS_URL
#define FIRMWARE_BASIS_URL "https://simonloeffler1993.github.io/scanner_project/firmware"
#endif

static const uint16_t TIMEOUT_MS = 15000;

enum Zustand { NICHTS, PRUEFE, LAEDT, AKTUELL, FEHLGESCHLAGEN };

static volatile Zustand zustand = NICHTS;
static char             fehler_text[160] = "";
static SemaphoreHandle_t fehler_sperre = xSemaphoreCreateMutex();

static void setze(Zustand neu, const String &fehler = "") {
    xSemaphoreTake(fehler_sperre, portMAX_DELAY);
    strlcpy(fehler_text, fehler.c_str(), sizeof(fehler_text));
    xSemaphoreGive(fehler_sperre);
    zustand = neu;
    Serial.printf("[UPDATE] %s %s\n", update_status(), fehler.c_str());
    // Gleich melden, damit das Center den Stand sofort zeigt
    health_jetzt_pruefen();
}

// GET auf eine Datei im Firmware-Ordner. Leerer Text = HTTP 200, sonst der Fehler
static String oeffnen(ApiAnfrage &anfrage, const char *datei) {
    String url = String(FIRMWARE_BASIS_URL) + "/" + datei;
    if (!anfrage.begin_url(url, TIMEOUT_MS, GITHUB_WURZELN)) return String(datei) + ": ungültige Adresse";
    int code = anfrage.http.GET();
    if (code == 200) return "";
    return String(datei) + ": " + (code < 0 ? anfrage.fehler(code) : "HTTP " + String(code));
}

// Schreibt genau groesse Bytes aus der Verbindung in den Flash. Bricht ab, wenn TIMEOUT_MS lang nichts kommt
// (Update.writeStream würde bei einer hängenden Verbindung sehr lange warten, so lange gingen keine Scans)
static bool uebertragen(WiFiClient &strom, size_t groesse) {
    static uint8_t puffer[4096];
    size_t   offen  = groesse;
    uint32_t zuletzt = millis();
    while (offen > 0) {
        size_t da = strom.available();
        if (da > 0) {
            int gelesen = strom.read(puffer, min(min(da, sizeof(puffer)), offen));
            if (gelesen <= 0) return false;
            if (Update.write(puffer, gelesen) != (size_t)gelesen) return false;
            offen  -= gelesen;
            zuletzt = millis();
        } else if (!strom.connected() || millis() - zuletzt > TIMEOUT_MS) {
            return false;
        } else {
            delay(10);
        }
    }
    return true;
}

// Lädt eine Datei und schreibt sie in den anderen App-Slot (U_FLASH) oder das Dateisystem (U_SPIFFS).
// Größe und MD5 kommen aus dem Manifest. Leerer Text = Erfolg
static String installieren(JsonVariantConst teil, int ziel) {
    const char *datei   = teil["datei"] | "";
    size_t      groesse = teil["groesse"] | 0;
    const char *md5     = teil["md5"] | "";
    if (!*datei || groesse == 0 || strlen(md5) != 32) return "Manifest ohne Angaben für das Selbst-Update";

    ApiAnfrage anfrage;
    String fehler = oeffnen(anfrage, datei);
    if (fehler.isEmpty() && anfrage.http.getSize() != (int)groesse) {
        // Passiert kurz nach einem Release, solange GitHub Pages Manifest und Datei noch unterschiedlich ausliefert
        fehler = String(datei) + ": Größe passt nicht zum Manifest";
    }
    if (fehler.isEmpty()) {
        if (!Update.begin(groesse, ziel)) {
            fehler = String(datei) + ": " + Update.errorString();
        } else {
            Update.setMD5(md5);
            if (!uebertragen(*anfrage.http.getStreamPtr(), groesse)) {
                fehler = String(datei) + ": " + (Update.hasError() ? Update.errorString() : "Verbindung abgebrochen");
                Update.abort();
            } else if (!Update.end()) {
                // Prüft Länge und MD5, bei U_FLASH wird erst hier auf den neuen Slot umgeschaltet
                fehler = String(datei) + ": " + Update.errorString();
            }
        }
    }
    anfrage.http.end();
    return fehler;
}

static String ausfuehren() {
    JsonDocument manifest;
    {
        ApiAnfrage anfrage;
        String fehler = oeffnen(anfrage, "manifest.json");
        if (fehler.isEmpty() && deserializeJson(manifest, anfrage.http.getString())) {
            fehler = "manifest.json: nicht lesbar";
        }
        anfrage.http.end();
        if (!fehler.isEmpty()) return fehler;
    }

    const char *version = manifest["version"] | "";
    if (!*version) return "manifest.json: keine Version";
    // Die Versionen (si-<Datum>-<sha>) lassen sich nicht ordnen: auf Pages liegt immer die neueste,
    // jede andere als die eigene wird installiert
    if (strcmp(version, FIRMWARE_VERSION) == 0) return "";

    Serial.printf("[UPDATE] %s -> %s\n", FIRMWARE_VERSION, version);
    setze(LAEDT);

    // Zuerst die Konfigurationsseiten: schlägt danach die Firmware fehl, läuft die alte mit neuen Seiten weiter
    LittleFS.end();
    String fehler = installieren(manifest["ota"]["littlefs"], U_SPIFFS);
    LittleFS.begin();
    if (!fehler.isEmpty()) return fehler;

    fehler = installieren(manifest["ota"]["firmware"], U_FLASH);
    if (!fehler.isEmpty()) return fehler;

    Serial.println("[UPDATE] installiert, Neustart");
    delay(1500);
    ESP.restart();
    return "";
}

static void update_task_fn(void *) {
    String fehler = ausfuehren();
    setze(fehler.isEmpty() ? AKTUELL : FEHLGESCHLAGEN, fehler);
    vTaskDelete(nullptr);
}

void update_anfordern() {
    if (update_laeuft()) return;
    setze(PRUEFE);
    // Eigener Task: der Download dauert bis zu einigen Minuten, Health-Check und LED laufen weiter.
    // 16 KB Stack wegen des TLS-Handshakes
    if (xTaskCreatePinnedToCore(update_task_fn, "update", 16 * 1024, nullptr, 1, nullptr, 0) != pdPASS) {
        setze(FEHLGESCHLAGEN, "Zu wenig Speicher für das Update");
    }
}

bool update_laeuft() {
    return zustand == PRUEFE || zustand == LAEDT;
}

const char *update_status() {
    switch (zustand) {
        case PRUEFE:         return "pruefe";
        case LAEDT:          return "laedt";
        case AKTUELL:        return "aktuell";
        case FEHLGESCHLAGEN: return "fehlgeschlagen";
        default:             return "";
    }
}

String update_fehler() {
    xSemaphoreTake(fehler_sperre, portMAX_DELAY);
    String text = fehler_text;
    xSemaphoreGive(fehler_sperre);
    return text;
}
