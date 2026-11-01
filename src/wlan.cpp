#include "wlan.h"
#include "scanner.h"
#include "einstellungen.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>

static AsyncWebServer server(80);

bool api_configured() {
    return server_konfiguriert();
}

static String modus_selected(const AppConfig &cfg, int modus) {
    return cfg.modus == modus ? "selected" : "";
}

// ---------------------------------------------------------------
// Template-Platzhalter ersetzen
// ---------------------------------------------------------------

static String processTemplate(const String &var) {
    AppConfig cfg = einstellungen_laden();

    if (var == "IP_ADDRESS")   return WiFi.localIP().toString();
    if (var == "WLAN_SSID")    return cfg.ssid;
    if (var == "USB_SCANNER")  return scanner_connected ? "Verbunden" : "Nicht verbunden";
    if (var == "SERVER_URL")   return cfg.serverUrl;
    if (var == "API_NAME")     return cfg.name;
    if (var == "API_TRIGGER_0") return modus_selected(cfg, MODUS_BEARBEITUNG);
    if (var == "API_TRIGGER_1") return modus_selected(cfg, MODUS_FERTIG);
    if (var == "API_TRIGGER_2") return modus_selected(cfg, MODUS_BINDUNG);
    return "";
}

// Server-Adresse, Name und Modus aus einem Formular übernehmen (nur vorhandene Felder)
static void api_felder_uebernehmen(AsyncWebServerRequest *request, AppConfig &cfg) {
    if (request->hasParam("server_url", true))  cfg.serverUrl = request->getParam("server_url", true)->value();
    if (request->hasParam("api_name", true))    cfg.name      = request->getParam("api_name", true)->value();
    if (request->hasParam("api_trigger", true)) cfg.modus     = request->getParam("api_trigger", true)->value().toInt();
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
        AppConfig cfg = einstellungen_laden();  // bestehende API-Einstellungen behalten
        cfg.ssid     = request->getParam("ssid", true)->value();
        cfg.password = request->getParam("password", true)->value();
        // Im Einrichtungsmodus kommen Server-Adresse, Name und Modus im selben Formular
        api_felder_uebernehmen(request, cfg);
        einstellungen_speichern(cfg);
        request->send(200, "text/html", "<h2>Gespeichert! ESP startet neu...</h2>");
        delay(1500);
        ESP.restart();
    });

    // /api-save – API-Einstellungen speichern
    server.on("/api-save", HTTP_POST, [](AsyncWebServerRequest *request) {
        AppConfig cfg = einstellungen_laden();  // bestehende WLAN-Einstellungen behalten
        api_felder_uebernehmen(request, cfg);
        einstellungen_speichern(cfg);
        request->redirect("/");
    });

    // /testapi – Erreichbarkeit der API über den Health-Endpunkt prüfen
    server.on("/testapi", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!server_konfiguriert()) {
            request->send(400, "application/json", "{\"ok\":false,\"meldung\":\"Keine Server-Adresse eingetragen\"}");
            return;
        }
        HTTPClient http;
        http.setTimeout(3000);
        http.begin(api_url("/api/v1/health"));
        int code = http.GET();
        String antwort = code > 0 ? http.getString() : "";
        http.end();

        JsonDocument ergebnis;
        ergebnis["ok"] = code == 200;
        ergebnis["http_code"] = code;
        if (code > 0) {
            JsonDocument health;
            if (!deserializeJson(health, antwort)) ergebnis["status"] = health["status"];
            ergebnis["meldung"] = code == 200 ? "API erreichbar" : "API antwortet mit Fehler";
        } else {
            ergebnis["meldung"] = String("Keine Verbindung: ") + HTTPClient::errorToString(code);
        }
        String json;
        serializeJson(ergebnis, json);
        request->send(200, "application/json", json);
    });

    // /reset – alles löschen → Neustart
    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        einstellungen_loeschen();
        request->send(200, "text/html", "<h2>Einstellungen gelöscht, Scanner startet neu...</h2>");
        delay(1000);
        ESP.restart();
    });
}

// ---------------------------------------------------------------
// Mit bestehendem WLAN verbinden (STA-Mode)
// ---------------------------------------------------------------

static bool connectWiFi() {
    AppConfig cfg = einstellungen_laden();
    if (cfg.ssid.isEmpty()) return false;

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
// Globale Funktion zum API-Aufruf mit gescanntem Code
// ----------------------------------------------------------------
bool send_to_api(const char* code) {
    AppConfig cfg = einstellungen_laden();
    if (cfg.serverUrl.isEmpty()) {
        Serial.println("[API] Keine Server-Adresse eingetragen.");
        return false;
    }

    // Mit ArduinoJson, damit Anführungszeichen o.ä. im Code das JSON nicht zerstören
    JsonDocument anfrage;
    anfrage["code"]    = code;
    anfrage["name"]    = cfg.name;
    anfrage["trigger"] = cfg.modus;
    String body;
    serializeJson(anfrage, body);

    HTTPClient http;
    http.setTimeout(5000);
    http.begin(api_url("/api/v1/scanner/skiscanner/scan"));
    http.addHeader("Content-Type", "application/json");
    int code_http = http.POST(body);
    String antwort = code_http > 0 ? http.getString() : "";
    http.end();

    if (code_http < 200 || code_http >= 300) {
        Serial.printf("[API] Fehler → HTTP %d %s\n", code_http, code_http < 0 ? HTTPClient::errorToString(code_http).c_str() : antwort.c_str());
        return false;
    }

    // Die API antwortet mit {success, message}: z.B. falsches Etikett für den Modus → success false
    JsonDocument ergebnis;
    if (deserializeJson(ergebnis, antwort)) {
        Serial.printf("[API] Unerwartete Antwort: %s\n", antwort.c_str());
        return false;
    }
    bool ok = ergebnis["success"] | false;
    Serial.printf("[API] %s: %s\n", ok ? "OK" : "Abgelehnt", (const char*)(ergebnis["message"] | ""));
    return ok;
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
    einstellungen_init();
    if (!connectWiFi()) {
        startAccessPoint();
    }
}