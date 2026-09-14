#include "wlan.h"
#include "scanner.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_flash.h>

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

bool api_configured() {
    AppConfig cfg;
    return loadConfig(cfg) && cfg.apiUrl.length() > 0;
}

// ---------------------------------------------------------------
// WLAN-Konfiguration aus Flash lesen (für Webflasher-Integration)
// ---------------------------------------------------------------
bool loadFlashWLANConfig(String &ssid, String &password) {
    // Die WLAN-Konfiguration wird am Ende der Firmware erwartet
    // Wir suchen nach dem Magic-Marker "WCFG"
    const uint32_t search_start = 0x300000; // Suche ab 3MB beginnen
    const uint32_t search_end = 0x3F0000; // Bis kurz vor Ende

    for (uint32_t addr = search_start; addr < search_end; addr += 4096) {
        uint8_t buffer[256];
        esp_err_t err = esp_flash_read(NULL, buffer, addr, sizeof(buffer));

        if (err == ESP_OK) {
            // Nach Magic-Marker suchen
            if (buffer[0] == 'W' && buffer[1] == 'C' && buffer[2] == 'F' && buffer[3] == 'G') {
                Serial.printf("[FLASH] WLAN-Konfiguration gefunden bei 0x%08X\n", addr);

                // WLAN-Konfiguration als JSON lesen
                const char* jsonStart = (const char*)(buffer + 4);
                String jsonStr = String(jsonStart);

                // JSON parsen
                JsonDocument doc;
                if (deserializeJson(doc, jsonStr) == DeserializationError::Ok) {
                    ssid = doc["ssid"] | "";
                    password = doc["password"] | "";
                    bool configured = doc["configured"] | false;

                    if (configured && ssid.length() > 0) {
                        Serial.printf("[FLASH] SSID: %s\n", ssid.c_str());
                        Serial.println("[FLASH] WLAN-Konfiguration geladen");
                        return true;
                    }
                }
            }
        }
    }

    Serial.println("[FLASH] Keine WLAN-Konfiguration im Flash gefunden");
    return false;
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

    // /update – OTA Firmware Update (POST)
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Anfrage-Handler nach dem Upload
        if (Update.hasError()) {
            request->send(500, "text/plain", "Update fehlgeschlagen!");
        } else {
            request->send(200, "text/plain", "Update erfolgreich! ESP startet neu...");
            delay(1000);
            ESP.restart();
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // Upload-Handler
        if (!index) {
            Serial.printf("Update gestartet: %s\n", filename.c_str());
            // Update starten mit 16MB Flash-Größe (passend zur Konfiguration)
            if (Update.begin(16 * 1024 * 1024)) {
                Serial.println("Update gestartet");
            } else {
                Serial.println("Update Fehler beim Starten");
            }
        }

        if (Update.write(data, len) != len) {
            Serial.println("Update Fehler beim Schreiben");
        }

        if (final) {
            if (Update.end(true)) {
                Serial.printf("Update erfolgreich: %u Bytes\n", index + len);
            } else {
                Serial.println("Update fehlgeschlagen");
            }
        }
    });

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
    bool useFlashConfig = false;

    // Zuerst versuchen, WLAN-Konfiguration aus Flash zu laden
    String flashSsid, flashPassword;
    if (loadFlashWLANConfig(flashSsid, flashPassword)) {
        Serial.println("[WLAN] Verwende Flash-WLAN-Konfiguration");
        cfg.ssid = flashSsid;
        cfg.password = flashPassword;
        useFlashConfig = true;
    } else if (!loadConfig(cfg)) {
        return false;
    }

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

        // Wenn Flash-Konfiguration verwendet wurde, speichere sie in LittleFS
        if (useFlashConfig) {
            Serial.println("[WLAN] Speichere Flash-Konfiguration in LittleFS");
            saveConfig(cfg);
        }

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
// Globale Funktion zum API-Aufruf mit gescanntem Code
// ----------------------------------------------------------------
bool send_to_api(const char* code) {
    AppConfig cfg;
    if (!loadConfig(cfg) || cfg.apiUrl.isEmpty()) {
        Serial.println("[API] Keine URL konfiguriert.");
        return false;
    }

    HTTPClient http;
    http.begin(cfg.apiUrl);
    http.addHeader("Content-Type", "application/json");

    String body = "{\"code\":\"" + String(code) + "\""
                + ",\"name\":\""    + cfg.apiName    + "\""
                + ",\"trigger\":"   + cfg.apiTrigger
                + "}";

    int code_http = http.POST(body);
    Serial.printf("[API] Gesendet → HTTP %d\n", code_http);
    http.end();
    return code_http >= 200 && code_http < 300;
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