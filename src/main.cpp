/**
 * =============================================================
 * Netum NS-91 QR/Barcode Scanner → ESP32-S3 N16R8
 * Schritt 1: Gescannten Code im Serial Monitor anzeigen
 *
 * Verkabelung:
 *   Scanner  →  USB-Port  (nativer OTG, linke USB-C Buchse)
 *   PC/Flash →  COM-Port  (UART-Chip,  rechte USB-C Buchse)
 *   ⚠ NIEMALS beide gleichzeitig am PC!
 * =============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include "scanner.h"
#include "led.h"
#include "wlan.h"

// Dateisytem neu aufspielen mit
// platformio run --target uploadfs 
// wird nötig wen data Datein geändert wurden.

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup()
{   
    Serial.begin(115200);
    delay(500);
    led_init();
    scanner_init();
    wlan_init();
}

// ---------------------------------------------------------------
// Loop
// ---------------------------------------------------------------
// void loopalt()
// {
//     static bool last_scan_complete = false;

//     if (!scanner_connected) {
//         led_blink_red_twice();
//     }

//     if (WiFi.getMode() == WIFI_AP) {
//         led_blink_blue_twice_slow();
//     }

//     scanner_loop();
//     delay(20);
// }

void loop() {
    static uint32_t last_blink_trigger = 0;
    static bool     status_led_set     = false;

    bool ap_mode          = (WiFi.getMode() == WIFI_AP);
    bool all_ok           = scanner_connected && !ap_mode && api_configured();

    // Alle 3 Sekunden Blink-Sequenz neu starten
    if (millis() - last_blink_trigger > 3000) {
        if (!scanner_connected) {
            led_blink_red_twice_start();
            status_led_set = false;
        } else if (ap_mode) {
            led_blink_blue_twice_slow_start();
            status_led_set = false;
        } else if (!api_configured()) {
            led_blink_orange_twice_start();
            status_led_set = false;
        }
        last_blink_trigger = millis();
    }

    // Default: dauerhaft Grün wenn alles OK und Blinken fertig
    if (all_ok && led_blink_done() && !status_led_set) {
        led_set_color(0, 255, 0);
        status_led_set = true;
    }

    led_update();

    // ── Gescannten Code aus Queue holen & senden ────────────
    if (scan_queue != NULL) {
        char buf[256];
        if (xQueueReceive(scan_queue, buf, 0) == pdTRUE) {
            Serial.printf("[SCAN] Code: %s\n", buf);
            status_led_set = false;  // nach Scan neu evaluieren
            if (WiFi.status() == WL_CONNECTED) {
                bool ok = send_to_api(buf);
                ok ? led_blink_green_twice_start() : led_blink_red_twice_start_fast();
            } else {
                Serial.println("[SCAN] Kein WLAN – Code nicht gesendet.");
                led_blink_red_twice_start();
            }
        }
    }

    scanner_loop();
}