/*
  sdcard.h - SD-card specific routines
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

#ifndef ESP32_PRINT_SDCARD_H
#define ESP32_PRINT_SDCARD_H

#include "sdkconfig.h"

#include <cstdio>
#include <cstring>
#include <sys/unistd.h>
#include <sys/stat.h>

#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <esp_http_server.h>

#define MOUNT_POINT         "/sdcard"

void sdcard_init();
esp_err_t sdcard_mount(sdmmc_card_t* card);
void sdcard_umount();
bool sdcard_has_file(const char *name);
bool sdcard_get_files(void (*send_proc)(const char *file_entry_chunk, void *),
                      void (*err_send_proc)(const char *error, void *),
                      const char *selected, void *ctx);
FILE *sdcard_open_file(const char *name, const char *mode);
esp_err_t sdcard_delete_file(const char *name);
void sdcard_test();

#endif