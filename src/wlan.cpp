#include "wlan.h"
#include "scanner.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

static AsyncWebServer server(80);

// ---------------------------------------------------------------
// Config laden / speichern
// ---------------------------------------------------------------

struct AppConfig {
    String ssid;
    String password;
    String apiUrl;
    String apiName;
    int    apiTrigger = 0;
};

static bool loadConfig(AppConfig &cfg) {
    File f = LittleFS.open("/config.json", "r");
    if (!f) return false;

    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }

    cfg.ssid       = doc["ssid"]        | "";
    cfg.password   = doc["password"]    | "";
    cfg.apiUrl     = doc["api_url"]     | "";
    cfg.apiName    = doc["api_name"]    | "";
    cfg.apiTrigger = doc["api_trigger"] | 0;
    f.close();
    return cfg.ssid.length() > 0;
}

static void saveConfig(const AppConfig &cfg) {
    File f = LittleFS.open("/config.json", "w");
    if (!f) return;

    JsonDocument doc;
    doc["ssid"]        = cfg.ssid;
    doc["password"]    = cfg.password;
    doc["api_url"]     = cfg.apiUrl;
    doc["api_name"]    = cfg.apiName;
    doc["api_trigger"] = cfg.apiTrigger;
    serializeJson(doc, f);
    f.close();
}

// ---------------------------------------------------------------
// Template-Platzhalter ersetzen
// ---------------------------------------------------------------

static String processTemplate(const String &var) {
    AppConfig cfg;
    loadConfig(cfg);

    if (var == "IP_ADDRESS")   return WiFi.localIP().toString();
    if (var == "WLAN_SSID")    return cfg.ssid;
    if (var == "USB_SCANNER")  return scanner_connected ? "Verbunden" : "Nicht verbunden";
    if (var == "API_URL")      return cfg.apiUrl;
    if (var == "API_NAME")     return cfg.apiName;
    if (var == "API_TRIGGER_0") return cfg.apiTrigger == 0 ? "selected" : "";
    if (var == "API_TRIGGER_1") return cfg.apiTrigger == 1 ? "selected" : "";
    return "";
}

// ---------------------------------------------------------------
// Routen registrieren
// ---------------------------------------------------------------

static void registerRoutes() {

    // /info – IP als JSON
    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{\"ip\":\"" + WiFi.localIP().toString() + "\"}";
        request->send(200, "application/json", json);
    });

    // /save – WLAN-Einstellungen speichern
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
            request->send(400, "text/plain", "Fehlende Parameter");
            return;
        }
        AppConfig cfg;
        loadConfig(cfg);  // bestehende API-Settings behalten
        cfg.ssid     = request->getParam("ssid", true)->value();
        cfg.password = request->getParam("password", true)->value();
        saveConfig(cfg);
        request->send(200, "text/html", "<h2>Gespeichert! ESP startet neu...</h2>");
        delay(1500);
        ESP.restart();
    });

    // /api-save – API-Einstellungen speichern
    server.on("/api-save", HTTP_POST, [](AsyncWebServerRequest *request) {
        AppConfig cfg;
        loadConfig(cfg);  // bestehende WLAN-Settings behalten
        cfg.apiUrl     = request->hasParam("api_url",  true) ? request->getParam("api_url",  true)->value() : "";
        cfg.apiName    = request->hasParam("api_name", true) ? request->getParam("api_name", true)->value() : "";
        cfg.apiTrigger = request->hasParam("api_trigger", true) ? request->getParam("api_trigger", true)->value().toInt() : 0;
        saveConfig(cfg);
        request->send(200, "text/html", "<h2>API-Einstellungen gespeichert!</h2>");
    });

    // /testapi – API einmalig aufrufen
    server.on("/testapi", HTTP_GET, [](AsyncWebServerRequest *request) {
        AppConfig cfg;
        if (!loadConfig(cfg) || cfg.apiUrl.isEmpty()) {
            request->send(400, "text/plain", "Keine API-URL konfiguriert");
            return;
        }
        HTTPClient http;
        http.begin(cfg.apiUrl);
        http.addHeader("Content-Type", "application/json");
        String body = "{\"name\":\"" + cfg.apiName + "\",\"trigger\":" + cfg.apiTrigger + ",\"test\":true}";
        int code = http.POST(body);
        String response = http.getString();
        http.end();
        request->send(200, "application/json",
            "{\"code\":" + String(code) + ",\"response\":\"" + response + "\"}");
    });

    // /reset – alles löschen → Neustart
    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        LittleFS.remove("/config.json");
        request->send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart();
    });
}

// ---------------------------------------------------------------
// Mit bestehendem WLAN verbinden (STA-Mode)
// ---------------------------------------------------------------

static bool connectWiFi() {
    AppConfig cfg;
    if (!loadConfig(cfg)) return false;

    Serial.printf("Verbinde mit: %s\n", cfg.ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nVerbunden! IP: %s\n", WiFi.localIP().toString().c_str());

        // Template-Prozessor für Platzhalter in connected.html
        server.serveStatic("/", LittleFS, "/")
              .setDefaultFile("connected.html")
              .setTemplateProcessor(processTemplate);

        registerRoutes();
        server.begin();
        Serial.println("Webserver (STA) gestartet.");
        return true;
    }

    Serial.println("\nVerbindung fehlgeschlagen.");
    return false;
}

// ---------------------------------------------------------------
// Access Point + Webserver starten
// ---------------------------------------------------------------

static void startAccessPoint() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Skiscanner-Config", "12345678");
    Serial.printf("AP gestartet. IP: %s\n", WiFi.softAPIP().toString().c_str());

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    registerRoutes();
    server.begin();
    Serial.println("Webserver (AP) gestartet.");
}

// ---------------------------------------------------------------
// Hauptfunktion
// ---------------------------------------------------------------

void wlan_init() {
    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    if (!connectWiFi()) {
        startAccessPoint();
    }
}