#ifndef ESP32_PRINT_SETTINGS_H
#define ESP32_PRINT_SETTINGS_H

#include "sdkconfig.h"
#include <cstdio>
#include <esp_err.h>

class Settings {
private:
    char *ssid;
    char *password;
    char *ip;

    bool extract(char **setting, const char *str, const char *name);

public:
    explicit Settings();

    esp_err_t load();
    void unload();

    char *get_ip() const;
    char *get_ssid() const;
    char *get_password() const;
};

#endif //ESP32_PRINT_SETTINGS_H
