/*
  camera.cpp - camera specific routines
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

#include "camera.h"
#include "sdcard.h"

static const char TAG[] = "esp32-print-camera";

esp_err_t Camera::init() {
    static camera_config_t camera_config = {
        .pin_pwdn  = CAM_PIN_PWDN, .pin_reset = CAM_PIN_RESET, .pin_xclk = CAM_PIN_XCLK, .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC, .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6, .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4, .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2, .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC, .pin_href = CAM_PIN_HREF, .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG, .frame_size = FRAMESIZE_HD,
        .jpeg_quality = 5, .fb_count = 1, .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_LATEST
    };

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed (%d)", err);
        return err;
    }

    initialized = true;

    return ESP_OK;
}

unsigned int Camera::take_photo() {
    if (initialized) {
        camera_fb_t *pic = esp_camera_fb_get();

        const char photoFilename[] = "test_0000001.jpg";
        FILE *f = sdcard_open_file(photoFilename, "wb");
        ESP_LOGI(TAG, "Photo taken. Its size was: %zu bytes", pic->len);
        fwrite(pic->buf, 1, pic->len, f);
        fflush(f);
        fclose(f);

        esp_camera_fb_return(pic);

        return 1;
    }
    return 0;
}
