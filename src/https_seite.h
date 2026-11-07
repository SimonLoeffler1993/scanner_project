#pragma once

// Konfigurationsseite zusätzlich unter https://<IP> (Port 443), http://<IP> bleibt unverändert.
// Der HTTPS-Server (ESP-IDF esp_https_server) leitet jede Anfrage an den AsyncWebServer auf
// 127.0.0.1:80 weiter, die Routen gibt es also nur einmal in wlan.cpp.
// Das Zertifikat ist selbst signiert (Browserwarnung einmal bestätigen). Es wird beim ersten Start
// erzeugt und liegt im NVS-Namensraum "scanner_tls", den das Zurücksetzen nicht löscht.
void https_seite_start();
