/*
  settings.h - application settings class
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
    char *netmask;

    unsigned int baud_rate;

    bool extract(char **setting, const char *str, const char *name);

public:
    explicit Settings();

    esp_err_t load();
    void unload();

    [[nodiscard]] char *get_ip() const;
    [[nodiscard]] char *get_netmask() const;
    [[nodiscard]] char *get_ssid() const;
    [[nodiscard]] char *get_password() const;
    [[nodiscard]] unsigned int get_baud_rate() const;
};

#endif //ESP32_PRINT_SETTINGS_H
