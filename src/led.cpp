#include "led.h"
#include <FastLED.h>

#define LED_PIN     48
#define NUM_LEDS    1
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------
// LED initialisieren
// ---------------------------------------------------------------
void led_init()
{
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
}

// ---------------------------------------------------------------
// LED auf RGB-Farbe setzen
// ---------------------------------------------------------------
void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    leds[0] = CRGB(r, g, b);
    FastLED.show();
}

// ---------------------------------------------------------------
// LED ausschalten
// ---------------------------------------------------------------
void led_off()
{
    leds[0] = CRGB::Black;
    FastLED.show();
}

// ---------------------------------------------------------------
// 2x schnell blau blinken
// ---------------------------------------------------------------
void led_blink_blue_twice()
{
    // 1. Blink
    led_set_color(0, 0, 255);  // Blau
    delay(200);
    
    led_off();
    delay(150);
    
    // 2. Blink
    led_set_color(0, 0, 255);  // Blau
    delay(200);
    
    led_off();
    delay(150);
}
