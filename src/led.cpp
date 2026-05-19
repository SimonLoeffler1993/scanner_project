#include <Arduino.h>
#include "led.h"
#include <FastLED.h>

#define LED_PIN     48
#define NUM_LEDS    1
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

static uint8_t  blink_r, blink_g, blink_b;
static int      blink_count   = 0;
static int      blink_total   = 0;
static uint32_t blink_last_ms = 0;
static bool     blink_on      = false;
static uint32_t blink_on_ms   = 800;
static uint32_t blink_off_ms  = 400;

void led_init() {
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    leds[0] = CRGB(r, g, b);
    FastLED.show();
}

void led_off() {
    leds[0] = CRGB::Black;
    FastLED.show();
}

static void led_blink_start(uint8_t r, uint8_t g, uint8_t b,
                             int times, uint32_t on_ms, uint32_t off_ms) {
    blink_r       = r;
    blink_g       = g;
    blink_b       = b;
    blink_total   = times * 2;
    blink_count   = 0;
    blink_on_ms   = on_ms;
    blink_off_ms  = off_ms;
    blink_last_ms = millis();
    blink_on      = true;
    led_set_color(r, g, b);
}

void led_blink_red_twice_start() {
    led_blink_start(255, 0, 0, 2, 500, 250);
}

void led_blink_blue_twice_slow_start() {
    led_blink_start(0, 0, 255, 2, 800, 400);
}

void led_update() {
    if (blink_count >= blink_total) return;

    uint32_t now     = millis();
    uint32_t elapsed = now - blink_last_ms;
    uint32_t target  = blink_on ? blink_on_ms : blink_off_ms;

    if (elapsed >= target) {
        blink_on = !blink_on;
        blink_on ? led_set_color(blink_r, blink_g, blink_b) : led_off();
        blink_last_ms = now;
        blink_count++;
    }
}