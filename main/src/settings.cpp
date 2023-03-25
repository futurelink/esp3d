/*
  settings.cpp - application settings class
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
