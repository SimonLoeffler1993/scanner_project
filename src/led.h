#pragma once
#include <Arduino.h>

// LED initialisieren
void led_init();

// 2x schnell blau blinken
void led_blink_blue_twice();

// LED auf eine bestimmte Farbe setzen
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

// LED ausschalten
void led_off();
