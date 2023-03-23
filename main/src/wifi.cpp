#include "wifi.h"

static const char TAG[] = "stm32-print-wifi";

typedef struct {
    bool                connected;
    uint8_t             retry_num;
    EventGroupHandle_t  event_group;
} wifi_state_t;

wifi_state_t wifi_state;

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI(TAG, "Got WiFi event...");

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (wifi_state.retry_num < 999) {
            ESP_ERROR_CHECK(esp_wifi_connect());
            wifi_state.retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_state.event_group, WIFI_FAIL_BIT);
            wifi_state.connected = false;
        }
        ESP_LOGI(TAG,"Connect to the AP fail");
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        auto* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got an IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_state.retry_num = 0;
        wifi_state.connected = true;
        xEventGroupSetBits(wifi_state.event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_connect() {
    wifi_state.connected = false;
    wifi_state.event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    ESP_LOGI(TAG, "Initialized. Configuring...");

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
            .sta = {
                    .ssid = WIFI_SSID,
                    .password = WIFI_PASS,
                    .threshold = { .authmode = WIFI_AUTH_WPA2_PSK },
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Started. Waiting for connection...");

    EventBits_t bits = xEventGroupWaitBits(wifi_state.event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s password: %s", WIFI_SSID, WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s, password: %s", WIFI_SSID, WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* Unregister event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(wifi_state.event_group);
}
