#pragma once
#include <Arduino.h>

// Modus des Scanners, muss zu TriggerStatus in der API passen (app/schemas/scanner.py)
enum ScanModus : int {
    MODUS_BEARBEITUNG = 0,
    MODUS_FERTIG      = 1,  // nur Service-Etiketten, Ski wird im Center fertig gestellt
    MODUS_BINDUNG     = 2,  // Service- und Verleih-Etiketten, Bindung wird geprüft
};

struct AppConfig {
    String ssid;
    String password;
    // Adresse des Backends ohne Pfad, z.B. http://192.168.1.10:8000
    String serverUrl;
    // Name des Scanners, wird im Center angezeigt
    String name;
    int    modus = MODUS_FERTIG;
};

// Einstellungen liegen im NVS (Preferences), damit ein Firmware- oder Dateisystem-Update sie nicht löscht
void einstellungen_init();
AppConfig einstellungen_laden();
void einstellungen_speichern(const AppConfig &cfg);
void einstellungen_loeschen();

bool wlan_konfiguriert();
bool server_konfiguriert();
// Server-Adresse + Pfad, z.B. api_url("/api/v1/health")
String api_url(const char *pfad);
