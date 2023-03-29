/*
  sdcard.cpp - SD-card specific routines
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

#include <dirent.h>
#include "sdcard.h"

typedef struct {
    bool is_mounted;
    bool is_initialized;
    sdmmc_host_t host;
    sdmmc_slot_config_t slot_config;
    sdmmc_card_t card;
} sdcard_t;

sdcard_t sdcard_state;

static const char TAG[] = "esp3d-print-sdcard";

/**
 * Initialize SD card
 */
void sdcard_init() {
    ESP_LOGI(TAG, "Initialize SD card");
    sdcard_state.host = SDMMC_HOST_DEFAULT();
    sdcard_state.slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    sdcard_state.slot_config.width = 1;
    sdcard_state.slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t ret = sdcard_mount(&sdcard_state.card);
    if (ret != ESP_OK) ESP_LOGE(TAG, "SD card not mounted");
}

bool sdcard_has_file(const char *name) {
    if (sdcard_mount(&sdcard_state.card) != ESP_OK) {
        ESP_LOGE(TAG, "SD card not mounted");
        return false;
    }
    char path[255];
    sprintf(path, "%s/%s", MOUNT_POINT, name);
    struct stat st{};
    return (stat(path, &st) == 0);
}

FILE *sdcard_open_file(const char *name, const char *mode) {
    if (sdcard_mount(&sdcard_state.card) != ESP_OK) {
        ESP_LOGE(TAG, "SD card not mounted");
        return nullptr;
    }
    char path[255];
    sprintf(path, "%s/%s", MOUNT_POINT, name);
    ESP_LOGI(TAG, "Opening '%s'", path);
    return fopen(path, mode);
}

esp_err_t sdcard_delete_file(const char *name) {
    if (sdcard_mount(&sdcard_state.card) != ESP_OK) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_FAIL;
    }

    char path[255];
    sprintf(path, "%s/%s", MOUNT_POINT, name);
    ESP_LOGI(TAG, "Deleting '%s'", path);
    struct stat st{};
    if (stat(path, &st) == 0) unlink(path);

    return ESP_OK;
}

bool sdcard_get_files(
        void (*send_proc)(const char *file_entry_chunk, void *),
        void (*err_send_proc)(const char *error, void *),
        const char *selected, void *ctx) {
    if (sdcard_mount(&sdcard_state.card) != ESP_OK) {
        ESP_LOGE(TAG, "SD card not mounted");
        err_send_proc(R"({ "error": "Can't get access to SD card" })", ctx);
        return false;
    }

    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) {
        sdcard_umount();
        err_send_proc(R"({ "error": "Can't open SD card file system" })", ctx);
        return false;
    } else {
        send_proc("[", ctx);
        dirent *entry;
        bool first = true;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type != DT_DIR) {
                if (first) first = false; else send_proc(",", ctx);
                send_proc(R"({"name":")", ctx);
                send_proc(entry->d_name, ctx);
                send_proc(R"(","date":"2020-01-02")", ctx);
                if ((selected != nullptr) && (strcmp(entry->d_name, selected) == 0)) send_proc(R"(,"selected":"1")", ctx);
                send_proc(R"(})", ctx);
            }
        }
        send_proc("]", ctx);
    }
    return true;
}

/**
 * Mount SD card
 */
esp_err_t sdcard_mount(sdmmc_card_t* card) {
    if (!sdcard_state.is_mounted) {
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
                .format_if_mount_failed = false,
                .max_files = 5,
                .allocation_unit_size = 16 * 1024,
                .disk_status_check_enable = true
        };
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT,
                                                &sdcard_state.host,
                                                &sdcard_state.slot_config,
                                                &mount_config,
                                                &card);
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) ESP_LOGE(TAG, "Failed to mount filesystem");
            else ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
            return ret;
        }

        sdcard_state.is_mounted = true;
        sdmmc_card_print_info(stdout, card);
    }
    return ESP_OK;
}

void sdcard_umount() {
    if (sdcard_state.is_mounted) {
        esp_vfs_fat_sdmmc_unmount();
        sdcard_state.is_mounted = false;
        ESP_LOGI(TAG, "%s", "Card unmounted");
    }
}

void sdcard_test() {
    sdmmc_card_t card;
    if(sdcard_mount(&card) != ESP_OK) return;

    ESP_LOGI(TAG, "%s", "Opening file");
    FILE* f = fopen("/sdcard/hello.txt", "w");
    if (f == nullptr) {
        ESP_LOGI(TAG, "%s", "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello %s!\n", card.cid.name);
    fclose(f);
    ESP_LOGI(TAG, "%s", "File written");

    struct stat st;
    if (stat("/sdcard/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/sdcard/foo.txt");
    }

    ESP_LOGI(TAG, "%s", "Renaming file");
    if (rename("/sdcard/hello.txt", "/sdcard/foo.txt") != 0) {
        ESP_LOGI(TAG, "%s", "Rename failed");
        return;
    }

    ESP_LOGI(TAG, "%s","Reading file");
    f = fopen("/sdcard/foo.txt", "r");
    if (f == nullptr) {
        ESP_LOGI(TAG, "%s","Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);

    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: %s", line);

    sdcard_umount();
}