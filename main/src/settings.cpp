#include "settings.h"
#include "sdcard.h"

static const char TAG[] = "esp3d-settings";
static const char settings_ssid[] = "ssid=";
static const char settings_password[] = "password=";

#define SETTINGS_FILE "esp3d/settings"

Settings::Settings() {
    ssid = nullptr;
    password = nullptr;
}

esp_err_t Settings::load() {
    FILE *f = sdcard_open_file(SETTINGS_FILE, "r");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Can't open settings file");
        return ESP_FAIL;
    }

    char str[32];
    while (fgets(str, 32, f) != nullptr) {
        ESP_LOGI(TAG, "Got settings string '%s'", str);
        size_t len = strlen(str);
        if (strncmp(str, settings_ssid, strlen(settings_ssid)) == 0) {
            ssid = (char *) malloc(len - strlen(settings_ssid) + 1);
            strcpy(ssid, &str[strlen(settings_ssid)]);
        } else if (strncmp(str, settings_password, strlen(settings_password)) == 0) {
            password = (char *) malloc(len - strlen(settings_password) + 1);
            strcpy(password, &str[strlen(settings_password)]);
        }
    }

    fclose(f);

    return ESP_OK;
}

void Settings::unload() {
    free(ssid);
    free(password);
}

char *Settings::get_ssid() const { return ssid; }
char *Settings::get_password() const { return password; }
