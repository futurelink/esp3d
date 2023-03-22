#ifndef ESP32_PRINT_WIFI_H
#define ESP32_PRINT_WIFI_H

#include "sdkconfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>

#define WIFI_SSID       "MikroTik"
#define WIFI_PASS       "Sae90W80"

#define WIFI_FAIL_BIT       BIT0
#define WIFI_CONNECTED_BIT  BIT1

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_connect();

#endif