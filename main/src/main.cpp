#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include "wifi.h"
#include "sdcard.h"
#include "server.h"
#include "uart.h"
#include "printer.h"

Printer printer;
Server server;

#ifdef __cplusplus
extern "C" {
#endif

void app_main(void) {

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ret = nvs_flash_erase();
        if (ret == ESP_OK) ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return;

    sdcard_init();
    wifi_connect();
    server.start();
    printer.init();
}

#ifdef __cplusplus
}
#endif
