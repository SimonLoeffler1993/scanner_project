#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern bool scanner_connected;
extern bool scan_complete;
extern QueueHandle_t scan_queue;  // ← neu

void scanner_init();
void scanner_loop();