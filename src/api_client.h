#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Eine Anfrage an die API, wahlweise über http:// oder https:// (je nach Server-Adresse).
// Bei https wird das Zertifikat gegen die Let's-Encrypt-Wurzeln geprüft (zertifikate.h), der Hostname muss passen.
//
//   ApiAnfrage api;
//   api.begin("/api/v1/health", 3000);
//   int code = api.http.GET();
//   api.http.end();
struct ApiAnfrage {
    // tls muss vor http stehen: Member werden rückwärts zerstört, ~HTTPClient ruft noch stop() auf dem Client auf
    WiFiClientSecure tls;
    HTTPClient       http;

    // Server-Adresse + Pfad öffnen. false bei ungültiger Adresse
    bool begin(const char *pfad, uint16_t timeout_ms);
    // Beliebige Adresse öffnen, bei https geprüft gegen die übergebenen Wurzelzertifikate (PEM, muss bestehen bleiben)
    bool begin_url(const String &url, uint16_t timeout_ms, const char *wurzeln);
    // Fehlertext zu einem negativen HTTP-Code, bei https mit dem TLS-Grund (z.B. Zertifikat abgelehnt)
    String fehler(int code);
};
