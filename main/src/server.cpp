#include "server.h"
#include "resources/include/server_main_html.h"
#include "resources/include/server_main_css.h"
#include "resources/include/server_main_js.h"
#include "resources/include/jquery-3.6.4.min_js.h"
#include "resources/include/bootstrap.min_css.h"
#include "resources/include/favicon_png.h"

#include "utils.h"
#include "sdcard.h"
#include "state.h"
#include "multipart.h"
#include "uart.h"

static const char TAG[] = "stm32-print-http";

extern printer_state_t  printer_state;
extern uart_state_t     uart_state;

#define UPLOAD_FILE_NAME_MAX_LEN    48
#define UPLOAD_PART_BUFFER_SIZE     4096
#define COMMAND_MAX_LENGTH          64

typedef struct request_t {
    httpd_req_t     *req;
    char            *upload_buffer;
    FILE            *upload_file;
} request_t;

request_t   request;

const char *printer_state_str() {
    switch (printer_state.status) {
        case PRINTER_UNKNOWN: return "Unknown";
        case PRINTER_WORKING: return "Working";
        case PRINTER_IDLE: return "Idle";
        default: return "Unknown";
    }
}

void server_chunk_send(const char *chunk) {
    httpd_resp_sendstr_chunk(request.req, chunk);
}

void server_err_send(const char *err) {
    httpd_resp_send_err(request.req, HTTPD_500_INTERNAL_SERVER_ERROR, err);
}

int8_t upload_header_callback(const char *name, const char *value) {
    ESP_LOGI(TAG, "Got header %s = %s", name, value);
    return ESP_OK;
}

int8_t upload_data_callback(const char *data, size_t len) {
    if (request.upload_file != nullptr) fwrite(data, 1, len, request.upload_file);
    else return ESP_FAIL;
    return ESP_OK;
}

int8_t upload_data_start_callback(parser_state_t *parser) {
    if (request.upload_file == nullptr) {
        char fn[UPLOAD_FILE_NAME_MAX_LEN];
        if (multipart_parse_filename(parser, fn, UPLOAD_FILE_NAME_MAX_LEN) != 0) {
            ESP_LOGI(TAG, "No file name in multipart data!");
            return ESP_FAIL;
        }
        request.upload_file = sdcard_open_file(fn);
        if (request.upload_file == nullptr) {
            ESP_LOGI(TAG, "%s", "Failed to open file for writing");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    int received;
    size_t remain = req->content_len;
    ESP_LOGI(TAG, "Got file size %d", req->content_len);

    request.req = req;
    request.upload_file = nullptr;
    request.upload_buffer = (char *) malloc(UPLOAD_PART_BUFFER_SIZE);

    parser_config_t mp_parser;
    multipart_parse_init(&mp_parser, UPLOAD_PART_BUFFER_SIZE * 2);

    // Extract boundary from Content-Type header
    char *c_type = (char*) malloc(256);
    httpd_req_get_hdr_value_str(req, "Content-Type", c_type, 256);
    int8_t r = multipart_get_boundary(&mp_parser, c_type);
    free(c_type);
    if (r != 0) {
        httpd_resp_send_err(request.req, HTTPD_400_BAD_REQUEST, "Request is not multipart/form-data");
        return ESP_OK;
    }

    mp_parser.header_callback = upload_header_callback;
    mp_parser.data_callback = upload_data_callback;
    mp_parser.data_start_callback = upload_data_start_callback;

    esp_err_t res = ESP_OK;
    while (remain > 0) {
        if ((received = httpd_req_recv(req, request.upload_buffer, MIN(remain, UPLOAD_PART_BUFFER_SIZE))) <= 0) { res = ESP_FAIL; break; }
        if (multipart_parse_chunk(&mp_parser, request.upload_buffer, received) != 0) { res = ESP_FAIL; break; }
        remain -= received;
    }

    // Clean up a bit...
    multipart_parse_free(&mp_parser);
    if (request.upload_file != nullptr)  { fflush(request.upload_file); fclose(request.upload_file); }
    free(request.upload_buffer);

    if (res == ESP_OK) {
        httpd_resp_sendstr_chunk(req, R"({"result":"ok"})");
    } else {
        httpd_resp_send_err(request.req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error uploading file!");
    }

    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

esp_err_t options_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "accept, content-type");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

esp_err_t get_printer_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (strcmp(req->uri, "/printer/status") == 0) {
        httpd_resp_set_type(req, "application/json");
        char str[64];
        sprintf(str, R"({"status":"%s","hot_end":"%.2f","bed":"%.2f"})", printer_state_str(),
                printer_state.temp_hot_end, printer_state.temp_bed);
        httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
    } else if (strncmp(req->uri, "/printer/send?cmd=", 18) == 0) {
        if (printer_state.status == PRINTER_IDLE) {
            size_t len = MIN(strlen(req->uri) - 18, COMMAND_MAX_LENGTH);
            char *cmd = (char *) malloc(len);
            strcpy(cmd, &req->uri[18]);
            ESP_LOGI(TAG, "Got command: %s", cmd);
            unsigned long cmd_id = uart_send(cmd);
            free(cmd);

            char *result = (char *)malloc(32);
            sprintf(result, R"({"result":"ok","cmd":"%lu"})", cmd_id);
            httpd_resp_send(req, result, HTTPD_RESP_USE_STRLEN);
            free(result);
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, R"({"error":"Can't send command"})");
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, R"({"error":"Bad request"})");
    }

    return ESP_OK;
}

esp_err_t get_files_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (strncmp(req->uri, "/files/?select=", 15) == 0) {
        free(printer_state.selected_file);
        printer_state.selected_file = (char *)malloc(strlen(req->uri) - 14 + 1);
        url_decode(printer_state.selected_file, &req->uri[15]);
        if (!sdcard_has_file(printer_state.selected_file)) {
            free(printer_state.selected_file);
            printer_state.selected_file = nullptr;
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, R"({ "error" : "File not found" })");
            return ESP_OK;
        }
    } else if (strncmp(req->uri, "/files/?delete", 14) == 0) {
        if (printer_state.selected_file != nullptr) {
            if (sdcard_delete_file(printer_state.selected_file) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, R"({ "error" : "Could not delete file" })");
                return ESP_OK;
            } else printer_state.selected_file = nullptr;
        } else {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, R"({ "error" : "File not found" })");
            return ESP_OK;
        }
    } else if (strcmp(req->uri, "/files/") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, R"({ "error" : "Bad request" })");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Sending files list");
    request.req = req;
    httpd_resp_set_type(req, "application/json");
    bool res = sdcard_get_files(server_chunk_send, server_err_send, printer_state.selected_file);
    if (res) httpd_resp_sendstr_chunk(req, nullptr);

    return ESP_OK;
}

esp_err_t get_main_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Sending main page");
    httpd_resp_send(req, (const char *) server_main_html, (ssize_t) server_main_html_len);
    return ESP_OK;
}

esp_err_t get_favicon_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "image/png");
    httpd_resp_send(req, (const char *) favicon_png, (ssize_t) favicon_png_len);
    return ESP_OK;
}

esp_err_t get_resource_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    ESP_LOGI(TAG, "Serving request on %s", req->uri);
    if (strcmp(req->uri, "/res/server_main.css") == 0) {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_send(req, (const char *) server_main_css, (ssize_t) server_main_css_len);
    }
    else if (strcmp(req->uri, "/res/jquery-3.6.4.min.js") == 0) {
        httpd_resp_set_type(req, "text/javascript");
        httpd_resp_send(req, (const char *) jquery_3_6_4_min_js, (ssize_t) jquery_3_6_4_min_js_len);
    }
    else if (strcmp(req->uri, "/res/bootstrap.min.css") == 0) {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_send(req, (const char *) bootstrap_min_css, (ssize_t) bootstrap_min_css_len);
    }
    else if (strcmp(req->uri, "/res/server_main.js") == 0) {
        httpd_resp_set_type(req, "text/javascript");
        httpd_resp_send(req, (const char *) server_main_js, (ssize_t) server_main_js_len);
    }
    else {
        const char not_found_page[] = "Not found";
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, not_found_page);
    }
    return ESP_OK;
}

httpd_handle_t start_webserver() {
    httpd_handle_t server = nullptr;   /* Empty handle to esp_http_server */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); /* Generate default configuration */
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.task_priority = 10;

    httpd_uri_t uri_get_main = { .uri = "/", .method = HTTP_GET, .handler = get_main_handler, .user_ctx = nullptr,
                                 .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_favicon = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = get_favicon_handler,
                                    .user_ctx = nullptr, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_printer = { .uri = "/printer/*", .method = HTTP_GET, .handler = get_printer_handler,
                                    .user_ctx = nullptr, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_post = { .uri = "/upload", .method = HTTP_POST, .handler = post_handler, .user_ctx = nullptr,
                             .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_res = { .uri = "/res/*", .method = HTTP_GET, .handler = get_resource_handler,
                                .user_ctx = nullptr, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_files = { .uri = "/files/*", .method = HTTP_GET, .handler = get_files_handler,
                                  .user_ctx = nullptr, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_options = { .uri = "/*", .method = HTTP_OPTIONS, .handler = options_handler, .user_ctx = nullptr,
                                .is_websocket = false, .handle_ws_control_frames = false };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get_favicon);
        httpd_register_uri_handler(server, &uri_get_main);
        httpd_register_uri_handler(server, &uri_get_res);
        httpd_register_uri_handler(server, &uri_get_printer);
        httpd_register_uri_handler(server, &uri_get_files);
        httpd_register_uri_handler(server, &uri_post);
        httpd_register_uri_handler(server, &uri_options);
    }

    return server; /* If server failed to start, handle will be NULL */
}

void stop_webserver(httpd_handle_t server) {
    if (server) httpd_stop(server);
}
