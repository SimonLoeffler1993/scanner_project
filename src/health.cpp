#include "health.h"
#include "einstellungen.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "scanner.h"

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
    String body;
    serializeJson(meldung, body);

    HTTPClient http;
    http.setTimeout(TIMEOUT_MS);
    http.setConnectTimeout(TIMEOUT_MS);
    http.begin(api_url("/api/v1/scanner/skiscanner/melden"));
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    http.end();

    bool ok = code == 200;
    if (ok != api_ok || !schon_geprueft) {
        Serial.printf("[HEALTH] API %s (HTTP %d)\n", ok ? "erreichbar" : "NICHT erreichbar", code);
    }
    api_ok             = ok;
    letzter_http_code  = code;
    letzte_pruefung_ms = millis();
    schon_geprueft     = true;
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
    xTaskCreatePinnedToCore(health_task_fn, "health", 6144, nullptr, 1, &health_task, 0);
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
