#include "server.h"
#include "utils.h"
#include "sdcard.h"
#include "state.h"
#include "multipart.h"
#include "printer.h"

#include "resources/include/server_main_html.h"
#include "resources/include/server_main_css.h"
#include "resources/include/server_main_js.h"
#include "resources/include/jquery-3.6.4.min_js.h"
#include "resources/include/bootstrap.min_css.h"
#include "resources/include/favicon_png.h"

static const char TAG[] = "esp3d-print-http";

extern Printer printer;

#define TYPE_TEXT_CSS                   "text/css"
#define TYPE_TEXT_JAVASCRIPT            "text/javascript"
#define TYPE_APPLICATION_JSON           "application/json"
#define TYPE_IMAGE_PNG                  "image/png"

#define UPLOAD_FILE_NAME_MAX_LEN        48
#define UPLOAD_PART_BUFFER_SIZE         4096
#define UPLOAD_CONTENT_TYPE_MAX_LENGTH  256
#define COMMAND_MAX_LENGTH              64

const char *Server::printer_state_str() {
    switch (printer.get_status()) {
        case PRINTER_WORKING: return "Working";
        case PRINTER_PRINTING: return "Printing";
        case PRINTER_IDLE: return "Idle";
        default: return "Unknown";
    }
}

void Server::server_chunk_send(const char *chunk, void *context) {
    auto req = (httpd_req_t *)context;
    httpd_resp_sendstr_chunk(req, chunk);
}

void Server::server_err_send(const char *err, void *context) {
    auto req = (httpd_req_t *)context;
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err);
}

int8_t Server::upload_header_callback(const char *name, const char *value, void *context) {
    ESP_LOGI(TAG, "Got header %s = %s", name, value);
    return ESP_OK;
}

int8_t Server::upload_data_callback(const char *data, size_t len, void *context) {
    auto ctx = (context_t *)context;
    if (ctx == nullptr) {
        ESP_LOGE(TAG, "Context is empty, can't proceed!");
        return ESP_FAIL;
    }
    if (ctx->upload_file != nullptr) fwrite(data, 1, len, ctx->upload_file);
    else return ESP_FAIL;
    return ESP_OK;
}

int8_t Server::upload_data_start_callback(parser_state_t *parser, void *context) {
    auto ctx = (context_t *)context;
    if (ctx == nullptr) {
        ESP_LOGE(TAG, "Context is empty, can't proceed!");
        return ESP_FAIL;
    }
    if (ctx->upload_file == nullptr) {
        char fn[UPLOAD_FILE_NAME_MAX_LEN];
        if (multipart_parse_filename(parser, fn, UPLOAD_FILE_NAME_MAX_LEN) != 0) {
            ESP_LOGE(TAG, "No file name in multipart data!");
            return ESP_FAIL;
        }
        ctx->upload_file = sdcard_open_file(fn, "wb");
        if (ctx->upload_file == nullptr) {
            ESP_LOGE(TAG, "%s", "Failed to open file for writing");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t Server::post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    auto ctx = (context_t *)req->user_ctx;
    if (ctx->upload_file != nullptr) {
        // Someone else is uploading file, so we can't proceed with this as
        // there's only one upload permitted in a moment.
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "There's a file being uploaded now, pleas try again later");
        return ESP_OK;
    }

    int received;
    size_t remain = req->content_len;
    ESP_LOGI(TAG, "Got file size %d", req->content_len);

    ctx->upload_file = nullptr;
    ctx->upload_buffer = (char *) malloc(UPLOAD_PART_BUFFER_SIZE);

    parser_config_t mp_parser;
    multipart_parse_init(&mp_parser, UPLOAD_PART_BUFFER_SIZE * 2);
    mp_parser.callback_context = ctx;

    // Extract boundary from Content-Type header
    char *c_type = (char*) malloc(UPLOAD_CONTENT_TYPE_MAX_LENGTH);
    httpd_req_get_hdr_value_str(req, "Content-Type", c_type, UPLOAD_CONTENT_TYPE_MAX_LENGTH);
    int8_t r = multipart_get_boundary(&mp_parser, c_type);
    free(c_type);
    if (r != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request is not multipart/form-data");
        return ESP_OK;
    }

    mp_parser.header_callback = upload_header_callback;
    mp_parser.data_callback = upload_data_callback;
    mp_parser.data_start_callback = upload_data_start_callback;

    esp_err_t res = ESP_OK;
    while (remain > 0) {
        if ((received = httpd_req_recv(req, ctx->upload_buffer, MIN(remain, UPLOAD_PART_BUFFER_SIZE))) <= 0) { res = ESP_FAIL; break; }
        if (multipart_parse_chunk(&mp_parser, ctx->upload_buffer, received) != 0) { res = ESP_FAIL; break; }
        remain -= received;
    }

    // Clean up a bit...
    multipart_parse_free(&mp_parser);
    if (ctx->upload_file != nullptr)  { fflush(ctx->upload_file); fclose(ctx->upload_file); }
    free(ctx->upload_buffer);
    ctx->upload_file = nullptr;

    if (res == ESP_OK) {
        httpd_resp_sendstr_chunk(req, R"({"result":"ok"})");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error uploading file!");
    }

    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

esp_err_t Server::options_handler(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

void Server::send_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "accept, content-type");
}

esp_err_t Server::get_printer_handler(httpd_req_t *req) {
    send_cors_headers(req);

    auto ctx = (context_t *) req->user_ctx;
    if (strcmp(req->uri, "/printer/status") == 0) {
        httpd_resp_set_type(req, TYPE_APPLICATION_JSON);
        char str[64];
        sprintf(str, R"({"status":"%s","hot_end":"%.2f","bed":"%.2f"})", printer_state_str(),
                printer.get_temp_hot_end(), printer.get_temp_bed());
        httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
    } else if (strncmp(req->uri, "/printer/send?cmd=", 18) == 0) {
        if (printer.get_status() == PRINTER_IDLE) {
            size_t len = MIN(strlen(req->uri) - 18, COMMAND_MAX_LENGTH);
            char *cmd = (char *) malloc(len);
            strcpy(cmd, &req->uri[18]);
            ESP_LOGI(TAG, "Got command: %s", cmd);
            unsigned long cmd_id = printer.send_cmd(cmd);
            free(cmd);

            char *result = (char *)malloc(32);
            sprintf(result, R"({"result":"ok","cmd":"%lu"})", cmd_id);
            httpd_resp_set_type(req, TYPE_APPLICATION_JSON);
            httpd_resp_send(req, result, HTTPD_RESP_USE_STRLEN);
            free(result);
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, R"({"error":"Can't send command"})");
        }
    } else if (strcmp(req->uri, "/printer/start") == 0) {
        httpd_resp_set_type(req, TYPE_APPLICATION_JSON);
        if (ctx->selected_file != nullptr) {
            FILE *f = sdcard_open_file(ctx->selected_file, "r");
            if (f == nullptr) {
                httpd_resp_send(req, R"({"error":"File does not exist!"})", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
            printer.set_opened_file(f);
            httpd_resp_send(req, R"({"result":"ok"})", HTTPD_RESP_USE_STRLEN);
        } else {
            httpd_resp_send(req, R"({"error":"File is not selected!"})", HTTPD_RESP_USE_STRLEN);
        }
    } else if (strcmp(req->uri, "/printer/stop") == 0) {
        if (printer.get_status() == PRINTER_PRINTING) {
            printer.stop();
            httpd_resp_send(req, R"({"result":"ok"})", HTTPD_RESP_USE_STRLEN);
        } else {
            httpd_resp_send(req, R"({"error":"Printer is not printing. Nothing to stop."})", HTTPD_RESP_USE_STRLEN);
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, R"({"error":"Bad request"})");
    }

    return ESP_OK;
}

esp_err_t Server::get_files_handler(httpd_req_t *req) {
    send_cors_headers(req);

    auto ctx = (context_t *) req->user_ctx;
    if (strncmp(req->uri, "/files/?select=", 15) == 0) {
        free(ctx->selected_file);
        ctx->selected_file = (char *)malloc(strlen(req->uri) - 14 + 1);
        url_decode(ctx->selected_file, &req->uri[15]);
        if (!sdcard_has_file(ctx->selected_file)) {
            //free(printer_state.selected_file);
            //printer_state.selected_file = nullptr;
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, R"({ "error" : "File not found" })");
            return ESP_OK;
        }
    } else if (strncmp(req->uri, "/files/?delete", 14) == 0) {
        if (ctx->selected_file != nullptr) {
            if (sdcard_delete_file(ctx->selected_file) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, R"({ "error" : "Could not delete file" })");
                return ESP_OK;
            } else ctx->selected_file = nullptr;
        } else {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, R"({ "error" : "File not found" })");
            return ESP_OK;
        }
    } else if (strcmp(req->uri, "/files/") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, R"({ "error" : "Bad request" })");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Sending files list");
    httpd_resp_set_type(req, "application/json");
    bool res = sdcard_get_files(server_chunk_send, server_err_send, ctx->selected_file, req);
    if (res) httpd_resp_sendstr_chunk(req, nullptr);

    return ESP_OK;
}

esp_err_t Server::get_main_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Sending main page");
    httpd_resp_send(req, (const char *) server_main_html, (ssize_t) server_main_html_len);
    return ESP_OK;
}

esp_err_t Server::get_favicon_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, TYPE_IMAGE_PNG);
    httpd_resp_send(req, (const char *) favicon_png, (ssize_t) favicon_png_len);
    return ESP_OK;
}

esp_err_t Server::get_resource_handler(httpd_req_t *req) {
    send_cors_headers(req);

    ESP_LOGI(TAG, "Serving request on %s", req->uri);
    if (strcmp(req->uri, "/res/server_main.css") == 0) {
        httpd_resp_set_type(req, TYPE_TEXT_CSS);
        httpd_resp_send(req, (const char *) server_main_css, (ssize_t) server_main_css_len);
    }
    else if (strcmp(req->uri, "/res/jquery-3.6.4.min.js") == 0) {
        httpd_resp_set_type(req, TYPE_TEXT_JAVASCRIPT);
        httpd_resp_send(req, (const char *) jquery_3_6_4_min_js, (ssize_t) jquery_3_6_4_min_js_len);
    }
    else if (strcmp(req->uri, "/res/bootstrap.min.css") == 0) {
        httpd_resp_set_type(req, TYPE_TEXT_CSS);
        httpd_resp_send(req, (const char *) bootstrap_min_css, (ssize_t) bootstrap_min_css_len);
    }
    else if (strcmp(req->uri, "/res/server_main.js") == 0) {
        httpd_resp_set_type(req, TYPE_TEXT_JAVASCRIPT);
        httpd_resp_send(req, (const char *) server_main_js, (ssize_t) server_main_js_len);
    }
    else {
        const char not_found_page[] = "Not found";
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, not_found_page);
    }
    return ESP_OK;
}

esp_err_t Server::send_status_ws() const {
    char str[64];
    sprintf(str, R"({"status":"%s","hot_end":"%.2f","bed":"%.2f"})", printer_state_str(),
            printer.get_temp_hot_end(), printer.get_temp_bed());
    send_ws(str);

    return ESP_OK;
}

esp_err_t Server::send_ws(const char *string) const {
    httpd_ws_frame_t ws_pkt;

    // Create a frame
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)string;
    ws_pkt.len = strlen(string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Send a frame to all who's listening
    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET)
            ret = httpd_ws_send_frame_async(context->ws_hd, client_fds[i], &ws_pkt);
    }

    return ret;
}

esp_err_t Server::get_ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        auto ctx = (context_t *) req->user_ctx;
        ctx->ws_hd = req->handle;
        return ESP_OK;
    }

    return ESP_OK;
}

void Server::start() {
    server = nullptr;
    context = (context_t *) malloc(sizeof(context_t));
    context->upload_file = nullptr;
    context->upload_buffer = nullptr;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); /* Generate default configuration */
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t uri_get_main = { .uri = "/", .method = HTTP_GET, .handler = get_main_handler, .user_ctx = context,
                                 .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_favicon = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = get_favicon_handler,
                                    .user_ctx = context, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_printer = { .uri = "/printer/*", .method = HTTP_GET, .handler = get_printer_handler,
                                    .user_ctx = context, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_post = { .uri = "/upload", .method = HTTP_POST, .handler = post_handler, .user_ctx = context,
                             .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_res = { .uri = "/res/*", .method = HTTP_GET, .handler = get_resource_handler,
                                .user_ctx = context, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_get_files = { .uri = "/files/*", .method = HTTP_GET, .handler = get_files_handler,
                                  .user_ctx = context, .is_websocket = false, .handle_ws_control_frames = false };
    httpd_uri_t uri_options = { .uri = "/*", .method = HTTP_OPTIONS, .handler = options_handler, .user_ctx = context,
                                .is_websocket = false, .handle_ws_control_frames = false };

    httpd_uri_t uri_get_ws = { .uri = "/ws", .method = HTTP_GET, .handler = get_ws_handler, .user_ctx = context,
                               .is_websocket = true, .handle_ws_control_frames = false };


    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get_favicon);
        httpd_register_uri_handler(server, &uri_get_main);
        httpd_register_uri_handler(server, &uri_get_res);
        httpd_register_uri_handler(server, &uri_get_printer);
        httpd_register_uri_handler(server, &uri_get_files);
        httpd_register_uri_handler(server, &uri_post);
        httpd_register_uri_handler(server, &uri_options);
        httpd_register_uri_handler(server, &uri_get_ws);
    }
}

void Server::stop() const {
    httpd_stop(server);
    free(context);
}
