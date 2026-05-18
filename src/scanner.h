#pragma once
#include <Arduino.h>

extern bool scan_complete;  // wird true wenn Barcode gescannt wurde

void scanner_init();   // ersetzt den USB-Setup-Block in setup()
void scanner_loop();   // ersetzt den scan_complete-Block in loop()