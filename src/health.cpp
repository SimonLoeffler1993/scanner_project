#include "health.h"
#include "einstellungen.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "api_client.h"
#include "scanner.h"
#include "update.h"

static const uint32_t INTERVALL_OK_MS     = 30000;
static const uint32_t INTERVALL_FEHLER_MS = 10000;
static const uint16_t TIMEOUT_MS          = 3000;

static volatile bool     api_ok             = false;
static volatile int      letzter_http_code  = 0;
static volatile uint32_t letzte_pruefung_ms = 0;
static volatile bool     schon_geprueft     = false;
static TaskHandle_t      health_task        = nullptr;

// Meldet den Scanner beim Backend an (Anzeige "verbunden" im Center) und prüft dabei API und Datenbank
static void pruefen() {
    AppConfig cfg = einstellungen_laden();
    JsonDocument meldung;
    meldung["name"]        = cfg.name;
    meldung["modus"]       = cfg.modus;
    meldung["firmware"]    = FIRMWARE_VERSION;
    meldung["ip"]          = WiFi.localIP().toString();
    meldung["usb_scanner"] = scanner_connected;
    // Selbst-Update: das Center darf es anstoßen und zeigt den Stand an
    meldung["update_faehig"] = true;
    meldung["update_status"] = update_status();
    meldung["update_fehler"] = update_fehler();
    String body;
    serializeJson(meldung, body);

    ApiAnfrage api;
    api.begin("/api/v1/scanner/skiscanner/melden", TIMEOUT_MS);
    api.http.addHeader("Content-Type", "application/json");
    int code = api.http.POST(body);
    String grund = code < 0 ? api.fehler(code) : "";
    // "update": true = im Center wurde ein Firmware-Update angestoßen
    bool update_gewuenscht = false;
    if (code == 200) {
        JsonDocument antwort;
        if (!deserializeJson(antwort, api.http.getString())) {
            update_gewuenscht = antwort["update"] | false;
        }
    }
    api.http.end();

    bool ok = code == 200;
    if (ok != api_ok || !schon_geprueft) {
        Serial.printf("[HEALTH] API %s (HTTP %d) %s\n", ok ? "erreichbar" : "NICHT erreichbar", code,
                      code == 401 || code == 403 ? "Geräteschlüssel fehlt oder ist ungültig" : grund.c_str());
    }
    api_ok             = ok;
    letzter_http_code  = code;
    letzte_pruefung_ms = millis();
    schon_geprueft     = true;

    if (update_gewuenscht) update_anfordern();
}

// Läuft getrennt von loop(), damit ein langsamer Server weder Scans noch die LED blockiert
static void health_task_fn(void *) {
    for (;;) {
        if (WiFi.status() == WL_CONNECTED && server_konfiguriert()) {
            pruefen();
        } else {
            api_ok = false;
        }
        uint32_t warten = api_ok ? INTERVALL_OK_MS : INTERVALL_FEHLER_MS;
        // Wartet das Intervall ab oder bis health_jetzt_pruefen() weckt
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(warten));
    }
}

void health_init() {
    if (health_task) return;
    // Mehr Stack als für reines HTTP: der TLS-Handshake (mbedTLS) braucht zusätzlich einige KB
    xTaskCreatePinnedToCore(health_task_fn, "health", 10240, nullptr, 1, &health_task, 0);
}

void health_jetzt_pruefen() {
    if (health_task) xTaskNotifyGive(health_task);
}

bool health_api_ok() {
    return api_ok;
}

int health_http_code() {
    return letzter_http_code;
}

long health_vor_sekunden() {
    if (!schon_geprueft) return -1;
    return (long)((millis() - letzte_pruefung_ms) / 1000);
}
