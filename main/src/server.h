/*
  server.h - web-server
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

#ifndef ESP32_PRINT_SERVER_H
#define ESP32_PRINT_SERVER_H

#include "sdkconfig.h"
#include "multipart.h"

#include <esp_http_server.h>

typedef struct context_t {
    char *upload_buffer;
    FILE *upload_file;
    char *selected_file;

    httpd_handle_t  ws_hd;
} context_t;

class Server {
public:
    context_t       *context;
    httpd_handle_t  server;

    void start();
    void stop() const;
    esp_err_t send_ws(const char *string) const;
    esp_err_t send_status_ws() const;

private:
    static const char *printer_state_str();
    static void server_chunk_send(const char *chunk, void *context);
    static void server_err_send(const char *err, void *context);
    static void send_cors_headers(httpd_req_t *req);

    static esp_err_t post_handler(httpd_req_t *req);
    static esp_err_t options_handler(httpd_req_t *req);
    static esp_err_t get_printer_handler(httpd_req_t *req);
    static esp_err_t get_main_handler(httpd_req_t *req);
    static esp_err_t get_favicon_handler(httpd_req_t *req);
    static esp_err_t get_resource_handler(httpd_req_t *req);
    static esp_err_t get_files_handler(httpd_req_t *req);
    static esp_err_t get_ws_handler(httpd_req_t *req);

    static int8_t upload_header_callback(const char *name, const char *value, void *context);
    static int8_t upload_data_callback(const char *data, size_t len, void *context);
    static int8_t upload_data_start_callback(parser_state_t *parser, void *context);
};

#endif

