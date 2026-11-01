#include "einstellungen.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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

AppConfig einstellungen_laden() {
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

void einstellungen_init() {
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
