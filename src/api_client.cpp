#include "api_client.h"
#include "einstellungen.h"
#include "zertifikate.h"

bool ApiAnfrage::begin(const char *pfad, uint16_t timeout_ms) {
    return begin_url(api_url(pfad), timeout_ms, LETSENCRYPT_WURZELN);
}

bool ApiAnfrage::begin_url(const String &url, uint16_t timeout_ms, const char *wurzeln) {
    http.setTimeout(timeout_ms);
    http.setConnectTimeout(timeout_ms);

    String klein = url;
    klein.toLowerCase();
    if (klein.startsWith("https://")) {
        tls.setCACert(wurzeln);
        // Sekunden, Standard wären 120 s. Der Handshake dauert auf dem S3 deutlich unter einer Sekunde
        tls.setHandshakeTimeout(timeout_ms / 1000 + 3);
        return http.begin(tls, url);
    }
    return http.begin(url);
}

String ApiAnfrage::fehler(int code) {
    String text = HTTPClient::errorToString(code);
    char grund[96];
    if (tls.lastError(grund, sizeof(grund)) != 0) {
        text += " (TLS: ";
        text += grund;
        text += ")";
    }
    return text;
}
