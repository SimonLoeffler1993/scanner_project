#pragma once
#include <Arduino.h>

void led_init();
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_off();
void led_update();
void led_blink_red_twice_start();
void led_blink_blue_twice_slow_start();