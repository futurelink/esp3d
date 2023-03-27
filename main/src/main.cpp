/*
  main.cpp - Main file
  Part of esp3D-print

  Copyright (c) 2023 Denis Pavlov

  esp3D-print is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  esp3D-print is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the MIT License
  along with esp3D-print. If not, see <https://opensource.org/license/mit/>.
*/

#include <esp_event.h>
#include <nvs_flash.h>

#include "wifi.h"
#include "sdcard.h"
#include "server.h"
#include "printer.h"
#include "settings.h"
#include "camera.h"

Printer printer;
Server server;
Settings settings;

#ifdef __cplusplus
extern "C" {
#endif

void app_main(void) {

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gpio_pullup_en(GPIO_NUM_12);

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ret = nvs_flash_erase();
        if (ret == ESP_OK) ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return;

    sdcard_init();
    if (settings.load() == ESP_OK) {
        wifi_connect(settings.get_ssid(), settings.get_password(), settings.get_ip());
        server.start();
        printer.init();

        if (camera_init() == ESP_OK) {
            camera_take_picture();
        }
    }
}

#ifdef __cplusplus
}
#endif
