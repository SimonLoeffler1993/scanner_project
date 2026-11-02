#pragma once
#include <Arduino.h>

// Wird von version.py gesetzt
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Healthcheck der API: eigener Task fragt regelmäßig GET /api/v1/health ab
void health_init();
// Sofort neu prüfen, z.B. nach dem Ändern der Server-Adresse
void health_jetzt_pruefen();

// API zuletzt erreichbar (HTTP 200)
bool health_api_ok();
// Letzter HTTP-Code, negativ bei Verbindungsfehler, 0 = noch nicht geprüft
int health_http_code();
// Sekunden seit der letzten Prüfung, -1 = noch nie geprüft
long health_vor_sekunden();
