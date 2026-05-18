#include <Arduino.h>
#include "usb/usb_host.h"   // ← USB Host Stack (muss vor HID gestartet werden)
#include "hid_host.h"
#include "hid_usage_keyboard.h"
#include "led.h"

// ---------------------------------------------------------------
// Barcode-Puffer
// ---------------------------------------------------------------
#define MAX_LEN 256
static char barcode[MAX_LEN];
static int  barcode_len   = 0;
bool scan_complete = false;  // extern zugänglich für main.cpp

// ---------------------------------------------------------------
// Keycode → ASCII  (US-Layout)
// ---------------------------------------------------------------
// TODO Später Layout einstellungen ermöglichen
const uint8_t keycode2ascii[57][2] = {
    {0, 0},       {0, 0},       {0, 0},       {0, 0},
    {'a', 'A'},   {'b', 'B'},   {'c', 'C'},   {'d', 'D'},
    {'e', 'E'},   {'f', 'F'},   {'g', 'G'},   {'h', 'H'},
    {'i', 'I'},   {'j', 'J'},   {'k', 'K'},   {'l', 'L'},
    {'m', 'M'},   {'n', 'N'},   {'o', 'O'},   {'p', 'P'},
    {'q', 'Q'},   {'r', 'R'},   {'s', 'S'},   {'t', 'T'},
    {'u', 'U'},   {'v', 'V'},   {'w', 'W'},   {'x', 'X'},
    {'y', 'Y'},   {'z', 'Z'},
    {'1', '!'},   {'2', '@'},   {'3', '#'},   {'4', '$'},
    {'5', '%'},   {'6', '^'},   {'7', '&'},   {'8', '*'},
    {'9', '('},   {'0', ')'},
    {'\r', '\r'}, {0, 0},       {'\b', 0},    {0, 0},
    {' ', ' '},   {'-', '_'},   {'=', '+'},   {'[', '{'},
    {']', '}'},   {'\\', '|'},  {'\\', '|'},  {';', ':'},
    {'\'', '"'},  {'`', '~'},   {',', '<'},   {'.', '>'},
    {'/', '?'},
};

// ---------------------------------------------------------------
// Deutsche Keymap (QWERTZ): basiert auf US-Map, aber y<->z getauscht
// und sonst identisch (erweiterbar wenn nötig)
// ---------------------------------------------------------------
const uint8_t keycode2ascii_de[57][2] = {
    {0, 0},       {0, 0},       {0, 0},       {0, 0},
    {'a', 'A'},   {'b', 'B'},   {'c', 'C'},   {'d', 'D'},
    {'e', 'E'},   {'f', 'F'},   {'g', 'G'},   {'h', 'H'},
    {'i', 'I'},   {'j', 'J'},   {'k', 'K'},   {'l', 'L'},
    {'m', 'M'},   {'n', 'N'},   {'o', 'O'},   {'p', 'P'},
    {'q', 'Q'},   {'r', 'R'},   {'s', 'S'},   {'t', 'T'},
    {'u', 'U'},   {'v', 'V'},   {'w', 'W'},   {'x', 'X'},
    {'z', 'Z'},   {'y', 'Y'},
    {'1', '!'},   {'2', '@'},   {'3', '#'},   {'4', '$'},
    {'5', '%'},   {'6', '^'},   {'7', '/'},   {'8', '*'},
    {'9', '('},   {'0', ')'},
    {'\r', '\r'}, {0, 0},       {'\b', 0},    {0, 0},
    {' ', ' '},   {'-', '_'},   {'=', '+'},   {'[', '{'},
    {']', '}'},   {'\\', '|'},  {'\\', '|'},  {';', ':'},
    {'\'', '"'},  {'`', '~'},   {',', ';'},   {'.', ':'},
    {'/', '?'},
};

// Active layout: true = DE, false = US
static bool use_de_layout = true;

// ---------------------------------------------------------------
// USB Host Daemon Task
// Muss als separater FreeRTOS-Task laufen, damit der HID-Driver
// sich erfolgreich beim USB-Stack registrieren kann.
// ---------------------------------------------------------------
static void usb_host_task(void *arg)
{
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            break;
        }
    }
    usb_host_uninstall();
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------
// Interface-Callback: Tastendrücke + Disconnect
// ---------------------------------------------------------------
static void keyboard_event_cb(hid_host_device_handle_t dev,
                               const hid_host_interface_event_t event,
                               void *arg)
{
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
            uint8_t data[10] = {0};
            size_t  data_len = 0;
            hid_host_device_get_raw_input_report_data(dev, data, sizeof(data), &data_len);
            // Serial.printf("[USB] Raw report len=%d:", (int)data_len);
            // for (size_t i = 0; i < data_len; ++i) Serial.printf(" %02X", data[i]);
            // Serial.println();
            if (data_len < 3) break;

            bool shift = (data[0] & 0x22) != 0;

            for (int i = 2; i < (int)data_len; i++) {
                uint8_t kc = data[i];
                if (kc == 0) continue;
                if (kc >= sizeof(keycode2ascii) / sizeof(keycode2ascii[0])) continue;

                const uint8_t (*map)[2] = use_de_layout ? keycode2ascii_de : keycode2ascii;
                uint8_t ch = shift ? map[kc][1] : map[kc][0];
                // Serial.printf("[USB] kc=%d shift=%d ch=0x%02X\n", kc, shift ? 1 : 0, (int)ch);
                if (ch == 0) continue;

                if (ch == '\r') {
                    if (barcode_len > 0) {
                        barcode[barcode_len] = '\0';
                        scan_complete = true;
                        led_blink_blue_twice();  // 2x schnell blau blinken
                    }
                } else if (ch >= 0x20 && ch < 0x7F) {
                    if (barcode_len < MAX_LEN - 1)
                        barcode[barcode_len++] = (char)ch;
                }
            }
            break;
        }

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            Serial.println("[USB] Scanner getrennt.");
            hid_host_device_close(dev);
            break;

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            Serial.println("[USB] Transfer-Fehler.");
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------
// Driver-Callback: Gerät verbunden
// ---------------------------------------------------------------
// Verspäteter Task, um Input-Reports zu starten (einige Geräte brauchen Zeit)
static void start_reports_task(void *a)
{
    hid_host_device_handle_t d = (hid_host_device_handle_t)a;
    vTaskDelay(pdMS_TO_TICKS(500));
    if (hid_host_device_start(d) == ESP_OK) {
        Serial.println("[USB] Input reports started (delayed).");
    } else {
        Serial.println("[USB] Failed to start input reports (delayed).");
    }
    vTaskDelete(NULL);
}

static void hid_host_event_cb(hid_host_device_handle_t dev,
                               const hid_host_driver_event_t event,
                               void *arg)
{
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;

    Serial.println("[USB] Scanner verbunden → öffne...");

    const hid_host_device_config_t dev_config = {
        .callback     = keyboard_event_cb,
        .callback_arg = NULL,
    };

    esp_err_t err = hid_host_device_open(dev, &dev_config);
    if (err == ESP_OK) {
        Serial.println("[USB] Bereit! Bitte scannen.");
        // Try to start input reports after a small delay in a task — some devices need time.
        xTaskCreate(start_reports_task, "hid_start", 2048, dev, 5, NULL);
    } else {
        Serial.printf("[USB] Fehler: %s\n", esp_err_to_name(err));
    }
}

void scanner_init()
{
    Serial.println("\n============================================");
    Serial.println("  NS-91 Scanner → Serial Monitor");
    Serial.println("  ESP32-S3 N16R8");
    Serial.println("============================================");
    Serial.println("Warte auf Scanner am USB-Port...\n");

    // 1) USB Host Stack starten
    const usb_host_config_t usb_config = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&usb_config));

    // 2) USB Host Daemon als eigenen Task starten
    //    (muss laufen bevor hid_host_install aufgerufen wird)
    xTaskCreate(usb_host_task, "usb_host", 4096, NULL, 5, NULL);

    // 3) HID Host Driver starten
    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority          = 5,
        .stack_size             = 4096,
        .core_id                = 0,
        .callback               = hid_host_event_cb,
        .callback_arg           = NULL,
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_config));
}

static void process_scan()        // ← erst definieren
{
    Serial.println("---");
    Serial.print("SCAN: ");
    Serial.println(barcode);
    barcode_len   = 0;
    scan_complete = false;

}

void scanner_loop()
{
    if (scan_complete) {
        process_scan();
    }
}