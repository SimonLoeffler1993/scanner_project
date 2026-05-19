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
    // ── LED-Zustand (non-blocking) ──────────────────────────
    static bool last_connected   = true;
    static bool last_ap_mode     = false;
    static uint32_t last_blink_trigger = 0;

    bool ap_mode = (WiFi.getMode() == WIFI_AP);

    // Alle 3 Sekunden Blink-Sequenz neu starten
    if (millis() - last_blink_trigger > 3000) {
        if (!scanner_connected) {
            led_blink_red_twice_start();
        } else if (ap_mode) {
            led_blink_blue_twice_slow_start();
        } else if (!api_configured()) {
            led_blink_orange_twice_start();  // sofort beim Boot anzeigen
        }
        last_blink_trigger = millis();
    }

    led_update();  // ← non-blocking, kein delay()

    // ── Gescannten Code aus Queue holen & senden ────────────
    if (scan_queue != NULL) {
        char buf[256];
        if (xQueueReceive(scan_queue, buf, 0) == pdTRUE) {
            Serial.printf("[SCAN] Code: %s\n", buf);
            if (WiFi.status() == WL_CONNECTED) {
                send_to_api(buf);
            } else {
                Serial.println("[SCAN] Kein WLAN – Code nicht gesendet.");
            }
        }
    }

    // ── Scanner ticken ──────────────────────────────────────
    scanner_loop();

    // Kein delay() mehr!
}