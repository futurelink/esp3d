#include <stdio.h>

#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include "wifi.h"
#include "sdcard.h"
#include "server.h"
#include "uart.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RED_LED_GPIO_NUM  GPIO_NUM_33

printer_state_t printer_state;

void app_main(void) {

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    printer_state.status = PRINTER_UNKNOWN;
    printer_state.temp_bed = 0;
    printer_state.temp_hot_end = 0;
    printer_state.selected_file = nullptr;

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_config_t io_conf = {
            .pin_bit_mask = (uint64_t) (1 << GPIO_NUM_12 | 1 << GPIO_NUM_13),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(RED_LED_GPIO_NUM, false);

    sdcard_init();
    wifi_connect();
    start_webserver();
    uart_init();
}

#ifdef __cplusplus
}
#endif
