#ifndef ESP32_PRINT_SERVER_H
#define ESP32_PRINT_SERVER_H

#include "sdkconfig.h"

#include <esp_http_server.h>

httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);

esp_err_t post_handler(httpd_req_t *req);

#endif

