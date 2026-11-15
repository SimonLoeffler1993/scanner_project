#pragma once
#include <Arduino.h>

// Selbst-Update: der Scanner lädt Firmware und Konfigurationsseiten von GitHub Pages und installiert sie.
// Angestoßen wird es im Center (Verwalten → Hardware-Scanner), die API gibt es über /melden weiter.

// Startet die Prüfung in einem eigenen Task. Läuft schon eine, passiert nichts
void update_anfordern();
// Prüfung oder Download läuft: keine Scans senden, LED blinkt weiß
bool update_laeuft();
// "" (nichts), "pruefe", "laedt", "aktuell" oder "fehlgeschlagen" – geht mit jeder Meldung an die API
const char *update_status();
// Grund bei "fehlgeschlagen", sonst leer
String update_fehler();
