#include "settings.h"
#include "sdcard.h"

static const char TAG[] = "esp3d-settings";

static const char settings_ip[] = "ip=";
static const char settings_ssid[] = "ssid=";
static const char settings_password[] = "password=";

#define SETTINGS_MAX_LEN    128
#define SETTINGS_FILE       "esp3d/settings"

Settings::Settings() {
    ssid = nullptr;
    password = nullptr;
    ip = nullptr;
}

esp_err_t Settings::load() {
    FILE *f = sdcard_open_file(SETTINGS_FILE, "r");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Can't open settings file");
        return ESP_FAIL;
    }

    char str[SETTINGS_MAX_LEN];
    while (fgets(str, SETTINGS_MAX_LEN, f) != nullptr) {
        size_t len = strlen(str);
        str[len-1] = 0; // Cut out \n
        if ((len > 1) && (str[len-2] == '\r')) str[len-2] = 0; // Cut out \r if it is there
        if (extract(&ssid, str, settings_ssid)) continue;
        if (extract(&password, str, settings_password)) continue;
        if (extract(&ip, str, settings_ip)) continue;
    }

    fclose(f);

    return ESP_OK;
}

bool Settings::extract(char **setting, const char *str, const char *name) {
    if (strncmp(str, name, strlen(name)) == 0) {
        size_t l = strlen(str) - strlen(name);
        *setting = (char *) malloc(l + 1);
        memcpy(*setting, &str[strlen(name)], l);
        (*setting)[l] = 0;
        return true;
    }
    return false;
}

void Settings::unload() {
    free(ip);
    free(ssid);
    free(password);
}

char *Settings::get_ip() const { return ip; }
char *Settings::get_ssid() const { return ssid; }
char *Settings::get_password() const { return password; }
