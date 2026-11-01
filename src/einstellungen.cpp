#include "einstellungen.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_partition.h>

static const char *NAMENSRAUM = "scanner";

// Server-Adresse vereinheitlichen: ohne "/" am Ende und ohne API-Pfad (alte Einstellungen enthielten die volle Scan-URL)
static String server_bereinigen(String url) {
    url.trim();
    int api = url.indexOf("/api/");
    if (api >= 0) url = url.substring(0, api);
    while (url.endsWith("/")) url.remove(url.length() - 1);
    return url;
}

// Zwischenspeicher, damit loop() nicht bei jedem Durchlauf das NVS liest
static AppConfig cache;
static bool cache_gueltig = false;
// Webserver, loop() und Health-Task lesen gleichzeitig
static SemaphoreHandle_t sperre = nullptr;

struct Sperre {
    Sperre()  { if (sperre) xSemaphoreTake(sperre, portMAX_DELAY); }
    ~Sperre() { if (sperre) xSemaphoreGive(sperre); }
};

AppConfig einstellungen_laden() {
    Sperre s;
    if (cache_gueltig) return cache;
    AppConfig cfg;
    Preferences prefs;
    prefs.begin(NAMENSRAUM, true);
    cfg.ssid      = prefs.getString("ssid", "");
    cfg.password  = prefs.getString("password", "");
    cfg.serverUrl = prefs.getString("server_url", "");
    cfg.name      = prefs.getString("name", "");
    cfg.modus     = prefs.getInt("modus", MODUS_FERTIG);
    prefs.end();
    cache = cfg;
    cache_gueltig = true;
    return cfg;
}

void einstellungen_speichern(const AppConfig &cfg) {
    Sperre s;
    Preferences prefs;
    prefs.begin(NAMENSRAUM, false);
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("password", cfg.password);
    prefs.putString("server_url", server_bereinigen(cfg.serverUrl));
    prefs.putString("name", cfg.name);
    prefs.putInt("modus", cfg.modus);
    prefs.end();
    cache_gueltig = false;
}

void einstellungen_loeschen() {
    Sperre s;
    Preferences prefs;
    prefs.begin(NAMENSRAUM, false);
    prefs.clear();
    prefs.end();
    cache_gueltig = false;
}

// Einmalige Übernahme der alten /config.json aus LittleFS (Firmware vor den NVS-Einstellungen)
static void alte_config_uebernehmen() {
    if (wlan_konfiguriert() || !LittleFS.exists("/config.json")) return;
    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError fehler = deserializeJson(doc, f);
    f.close();
    if (fehler) return;

    AppConfig cfg;
    cfg.ssid      = doc["ssid"]        | "";
    cfg.password  = doc["password"]    | "";
    cfg.serverUrl = doc["api_url"]     | "";
    cfg.name      = doc["api_name"]    | "";
    cfg.modus     = doc["api_trigger"] | (int)MODUS_FERTIG;
    einstellungen_speichern(cfg);
    LittleFS.remove("/config.json");
    Serial.println("[CONFIG] Alte /config.json ins NVS übernommen");
}

// Einrichtung aus dem Webflasher übernehmen: Partition "wcfg" enthält "WCFG" + JSON + \0.
// Danach wird sie gelöscht, damit ein Neustart später geänderte Einstellungen nicht überschreibt.
static void webflasher_einrichtung_uebernehmen() {
    const esp_partition_t *teil = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "wcfg");
    if (!teil) return;

    const size_t groesse = 4096;
    char *puffer = (char *)malloc(groesse + 1);
    if (!puffer) return;
    bool gelesen = esp_partition_read(teil, 0, puffer, groesse) == ESP_OK;
    puffer[groesse] = '\0';

    if (gelesen && memcmp(puffer, "WCFG", 4) == 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, puffer + 4)) {
            AppConfig cfg = einstellungen_laden();
            if (doc["ssid"].is<const char *>())       cfg.ssid      = doc["ssid"].as<String>();
            if (doc["password"].is<const char *>())   cfg.password  = doc["password"].as<String>();
            if (doc["server_url"].is<const char *>()) cfg.serverUrl = doc["server_url"].as<String>();
            if (doc["name"].is<const char *>())       cfg.name      = doc["name"].as<String>();
            if (doc["modus"].is<int>())               cfg.modus     = doc["modus"].as<int>();
            einstellungen_speichern(cfg);
            Serial.println("[CONFIG] Einrichtung aus dem Webflasher übernommen");
        } else {
            Serial.println("[CONFIG] Einrichtung aus dem Webflasher ungültig");
        }
        esp_partition_erase_range(teil, 0, teil->size);
    }
    free(puffer);
}

void einstellungen_init() {
    if (!sperre) sperre = xSemaphoreCreateMutex();
    webflasher_einrichtung_uebernehmen();
    alte_config_uebernehmen();
}

bool wlan_konfiguriert() {
    return einstellungen_laden().ssid.length() > 0;
}

bool server_konfiguriert() {
    return einstellungen_laden().serverUrl.length() > 0;
}

String api_url(const char *pfad) {
    return einstellungen_laden().serverUrl + pfad;
}
