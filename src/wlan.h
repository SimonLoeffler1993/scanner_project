#pragma once
#include <Arduino.h>

void wlan_init();
bool send_to_api(const char* code);
bool api_configured();
bool loadFlashWLANConfig(String &ssid, String &password);