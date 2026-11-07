// Nicht ESPAsyncWebServer.h einbinden: dessen HTTP_GET usw. kollidieren mit http_parser.h aus esp_http_server
#include "https_seite.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_https_server.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

static const char *NAMENSRAUM_TLS = "scanner_tls";
static const char *ZIEL           = "http://127.0.0.1";
static const uint16_t TIMEOUT_MS  = 15000;  // /testapi wartet selbst bis zu einigen Sekunden auf die API

// esp_https_server parst die PEM-Puffer bei jeder Verbindung neu, sie müssen dauerhaft gültig bleiben
static String zertifikat_pem;
static String schluessel_pem;
static httpd_handle_t https_server = nullptr;

// ---------------------------------------------------------------
// Selbst signiertes Zertifikat (ECDSA P-256)
// ---------------------------------------------------------------

static bool zertifikat_erzeugen() {
    mbedtls_entropy_context  entropie;
    mbedtls_ctr_drbg_context zufall;
    mbedtls_pk_context       schluessel;
    mbedtls_x509write_cert   crt;
    mbedtls_mpi              seriennummer;
    mbedtls_entropy_init(&entropie);
    mbedtls_ctr_drbg_init(&zufall);
    mbedtls_pk_init(&schluessel);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi_init(&seriennummer);

    static const char *PERSONALISIERUNG = "skiscanner-tls";
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String name = "CN=Skiscanner-" + mac + ",O=Skiservice";

    const size_t GROESSE = 2048;
    unsigned char *puffer = (unsigned char *)malloc(GROESSE);
    int fehler = puffer ? 0 : -1;

    if (!fehler) fehler = mbedtls_ctr_drbg_seed(&zufall, mbedtls_entropy_func, &entropie,
                                                (const unsigned char *)PERSONALISIERUNG, strlen(PERSONALISIERUNG));
    if (!fehler) fehler = mbedtls_pk_setup(&schluessel, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (!fehler) fehler = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(schluessel),
                                              mbedtls_ctr_drbg_random, &zufall);
    if (!fehler) fehler = mbedtls_mpi_fill_random(&seriennummer, 16, mbedtls_ctr_drbg_random, &zufall);
    if (!fehler) {
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &schluessel);
        mbedtls_x509write_crt_set_issuer_key(&crt, &schluessel);
        fehler = mbedtls_x509write_crt_set_subject_name(&crt, name.c_str());
    }
    if (!fehler) fehler = mbedtls_x509write_crt_set_issuer_name(&crt, name.c_str());
    if (!fehler) fehler = mbedtls_x509write_crt_set_serial(&crt, &seriennummer);
    // Der Scanner kennt keine Uhrzeit, deshalb ein großzügiger Zeitraum
    if (!fehler) fehler = mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20491231235959");
    if (!fehler) fehler = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    if (!fehler) fehler = mbedtls_x509write_crt_pem(&crt, puffer, GROESSE, mbedtls_ctr_drbg_random, &zufall);
    if (!fehler) {
        zertifikat_pem = (const char *)puffer;
        fehler = mbedtls_pk_write_key_pem(&schluessel, puffer, GROESSE);
    }
    if (!fehler) schluessel_pem = (const char *)puffer;

    if (puffer) {
        memset(puffer, 0, GROESSE);  // enthielt zuletzt den privaten Schlüssel
        free(puffer);
    }
    mbedtls_mpi_free(&seriennummer);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&schluessel);
    mbedtls_ctr_drbg_free(&zufall);
    mbedtls_entropy_free(&entropie);

    if (fehler) {
        Serial.printf("[HTTPS] Zertifikat konnte nicht erzeugt werden (mbedTLS -0x%04x)\n", (unsigned)-fehler);
        zertifikat_pem = "";
        schluessel_pem = "";
        return false;
    }
    return true;
}

static bool zertifikat_laden_oder_erzeugen() {
    Preferences prefs;
    prefs.begin(NAMENSRAUM_TLS, true);
    zertifikat_pem = prefs.getString("cert", "");
    schluessel_pem = prefs.getString("key", "");
    prefs.end();
    if (!zertifikat_pem.isEmpty() && !schluessel_pem.isEmpty()) return true;

    Serial.println("[HTTPS] Erzeuge selbst signiertes Zertifikat...");
    if (!zertifikat_erzeugen()) return false;

    prefs.begin(NAMENSRAUM_TLS, false);
    prefs.putString("cert", zertifikat_pem);
    prefs.putString("key", schluessel_pem);
    prefs.end();
    Serial.println("[HTTPS] Zertifikat erzeugt.");
    return true;
}

// ---------------------------------------------------------------
// Weiterleitung an den AsyncWebServer auf 127.0.0.1:80
// ---------------------------------------------------------------

// Liest den Body der HTTPS-Anfrage stückweise, damit auch Firmware-Images nicht komplett in den RAM müssen
class AnfrageStrom : public Stream {
public:
    explicit AnfrageStrom(httpd_req_t *req) : req(req), rest(req->content_len) {}
    int available() override { return rest > 0 ? (int)min(rest, (size_t)4096) : 0; }
    size_t readBytes(char *ziel, size_t laenge) override {
        size_t gelesen = 0;
        int timeouts = 0;
        while (gelesen < laenge && rest > 0) {
            int n = httpd_req_recv(req, ziel + gelesen, min(laenge - gelesen, rest));
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
            if (n <= 0) break;
            gelesen += n;
            rest -= n;
        }
        return gelesen;
    }
    int read() override {
        char c;
        return readBytes(&c, 1) == 1 ? (uint8_t)c : -1;
    }
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }

private:
    httpd_req_t *req;
    size_t rest;
};

// Schreibt die Antwort des AsyncWebServers chunkweise in die HTTPS-Antwort
class AntwortStrom : public Stream {
public:
    explicit AntwortStrom(httpd_req_t *req) : req(req) {}
    size_t write(const uint8_t *daten, size_t laenge) override {
        if (!ok || laenge == 0) return 0;
        ok = httpd_resp_send_chunk(req, (const char *)daten, laenge) == ESP_OK;
        return ok ? laenge : 0;
    }
    size_t write(uint8_t b) override { return write(&b, 1); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    bool ok = true;

private:
    httpd_req_t *req;
};

static const char *status_text(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 204: return "204 No Content";
        case 301: return "301 Moved Permanently";
        case 302: return "302 Found";
        case 304: return "304 Not Modified";
        case 400: return "400 Bad Request";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
        default:  return nullptr;
    }
}

static String kopfzeile(httpd_req_t *req, const char *name) {
    size_t laenge = httpd_req_get_hdr_value_len(req, name);
    if (laenge == 0) return "";
    String wert;
    wert.reserve(laenge + 1);
    char *puffer = (char *)malloc(laenge + 1);
    if (!puffer) return "";
    if (httpd_req_get_hdr_value_str(req, name, puffer, laenge + 1) == ESP_OK) wert = puffer;
    free(puffer);
    return wert;
}

static esp_err_t weiterleiten(httpd_req_t *req) {
    const char *methode = req->method == HTTP_POST ? "POST" : "GET";

    HTTPClient http;
    http.setTimeout(TIMEOUT_MS);
    http.setConnectTimeout(3000);
    http.setReuse(false);
    if (!http.begin(String(ZIEL) + req->uri)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Weiterleitung fehlgeschlagen");
        return ESP_OK;
    }
    const char *antwort_koepfe[] = {"Content-Type", "Location", "Cache-Control"};
    http.collectHeaders(antwort_koepfe, 3);

    String typ = kopfzeile(req, "Content-Type");
    if (!typ.isEmpty()) http.addHeader("Content-Type", typ);

    int code;
    if (req->method == HTTP_POST && req->content_len > 0) {
        AnfrageStrom body(req);
        code = http.sendRequest(methode, &body, req->content_len);
    } else {
        code = http.sendRequest(methode, (uint8_t *)nullptr, 0);
    }

    if (code <= 0) {
        Serial.printf("[HTTPS] %s %s → %s\n", methode, req->uri, HTTPClient::errorToString(code).c_str());
        http.end();
        httpd_resp_set_status(req, "502 Bad Gateway");
        httpd_resp_sendstr(req, "Interne Weiterleitung fehlgeschlagen");
        return ESP_OK;
    }

    // Die Strings müssen bis zum Senden der Kopfzeilen (erster Chunk) gültig bleiben
    char status_puffer[16];
    const char *status = status_text(code);
    if (!status) {
        snprintf(status_puffer, sizeof(status_puffer), "%d Status", code);
        status = status_puffer;
    }
    String antwort_typ = http.header("Content-Type");
    String ort         = http.header("Location");
    String cache       = http.header("Cache-Control");
    httpd_resp_set_status(req, status);
    if (!antwort_typ.isEmpty()) httpd_resp_set_type(req, antwort_typ.c_str());
    if (!ort.isEmpty())         httpd_resp_set_hdr(req, "Location", ort.c_str());
    if (!cache.isEmpty())       httpd_resp_set_hdr(req, "Cache-Control", cache.c_str());

    AntwortStrom ausgabe(req);
    http.writeToStream(&ausgabe);
    http.end();
    if (ausgabe.ok) httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------
// Start
// ---------------------------------------------------------------

void https_seite_start() {
    if (https_server) return;
    if (!zertifikat_laden_oder_erzeugen()) return;

    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    // IDF 4.4: cacert_pem ist hier das eigene Serverzertifikat. Länge inklusive \0, sonst lehnt mbedTLS das PEM ab
    cfg.cacert_pem  = (const uint8_t *)zertifikat_pem.c_str();
    cfg.cacert_len  = zertifikat_pem.length() + 1;
    cfg.prvtkey_pem = (const uint8_t *)schluessel_pem.c_str();
    cfg.prvtkey_len = schluessel_pem.length() + 1;
    cfg.httpd.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.httpd.max_open_sockets = 3;   // je TLS-Verbindung rund 40 KB RAM
    cfg.httpd.lru_purge_enable = true;
    cfg.httpd.recv_wait_timeout = 10;
    cfg.httpd.send_wait_timeout = 10;

    esp_err_t fehler = httpd_ssl_start(&https_server, &cfg);
    if (fehler != ESP_OK) {
        Serial.printf("[HTTPS] Server konnte nicht starten: %s\n", esp_err_to_name(fehler));
        https_server = nullptr;
        return;
    }

    static const httpd_uri_t get_alles  = {.uri = "/*", .method = HTTP_GET,  .handler = weiterleiten, .user_ctx = nullptr};
    static const httpd_uri_t post_alles = {.uri = "/*", .method = HTTP_POST, .handler = weiterleiten, .user_ctx = nullptr};
    httpd_register_uri_handler(https_server, &get_alles);
    httpd_register_uri_handler(https_server, &post_alles);
    Serial.println("[HTTPS] Konfigurationsseite auch unter https:// erreichbar (Port 443).");
}
