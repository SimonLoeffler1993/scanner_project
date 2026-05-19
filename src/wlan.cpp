#include "wlan.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);

// ---------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------

static bool loadConfig(String &ssid, String &password) {
    File f = LittleFS.open("/wifi_config.json", "r");
    if (!f) return false;

    JsonDocument doc;
    if (deserializeJson(doc, f)) return false;

    ssid     = doc["ssid"]     | "";
    password = doc["password"] | "";
    f.close();
    return ssid.length() > 0;
}

static void saveConfig(const String &ssid, const String &password) {
    File f = LittleFS.open("/wifi_config.json", "w");
    if (!f) return;

    JsonDocument doc;
    doc["ssid"]     = ssid;
    doc["password"] = password;
    serializeJson(doc, f);
    f.close();
}

// ---------------------------------------------------------------
// Mit bestehendem WLAN verbinden (STA-Mode)
// ---------------------------------------------------------------

static bool connectWiFi() {
    String ssid, password;
    if (!loadConfig(ssid, password)) return false;

    Serial.printf("Verbinde mit: %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nVerbunden! IP: %s\n", WiFi.localIP().toString().c_str());
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

    // Statische Dateien aus LittleFS ausliefern
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // POST /save → SSID + Passwort speichern → Neustart
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            String ssid     = request->getParam("ssid", true)->value();
            String password = request->getParam("password", true)->value();
            saveConfig(ssid, password);
            request->send(200, "text/html",
                "<h2>Gespeichert! ESP startet neu...</h2>");
            delay(1500);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Fehlende Parameter");
        }
    });

    server.begin();
    Serial.println("Webserver gestartet.");
}

// ---------------------------------------------------------------
// Hauptfunktion
// ---------------------------------------------------------------

void wlan_init() {
    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    // Versuche zuerst mit bestehendem WLAN zu verbinden
    if (!connectWiFi()) {
        // Keine Config oder Verbindung fehlgeschlagen → AP starten
        startAccessPoint();
    }
}