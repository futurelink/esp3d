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

