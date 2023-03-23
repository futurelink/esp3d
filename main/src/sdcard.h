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