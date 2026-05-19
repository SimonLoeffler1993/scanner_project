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
void loop()
{
    // Scanner verarbeiten
    scanner_loop();    
    delay(20);
}