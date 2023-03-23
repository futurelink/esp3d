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

extern uart_state_t uart_state;
printer_state_t printer_state;

void app_main(void) {

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    uart_state.baud = 250000;
    uart_state.rxd_pin = GPIO_NUM_16;
    uart_state.txd_pin = GPIO_NUM_13;

    printer_state.status = PRINTER_UNKNOWN;
    printer_state.temp_bed = 0;
    printer_state.temp_hot_end = 0;
    printer_state.selected_file = nullptr;

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ret = nvs_flash_erase();
        if (ret == ESP_OK) ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return;

    gpio_set_level(RED_LED_GPIO_NUM, false);

    sdcard_init();
    wifi_connect();
    start_webserver();
    uart_init();
}

#ifdef __cplusplus
}
#endif
