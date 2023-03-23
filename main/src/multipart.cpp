#include <esp_log.h>

#include "multipart.h"

static const char TAG[] = "multipart-parse";

void multipart_parse_init(parser_config_t *parser, size_t buffer_size) {
    parser->state.state = PARSE_NONE;

    // Allocate buffer
    parser->state.buf_size = buffer_size;
    parser->state.buf_pos = 0;
    parser->state.buf = (char *)malloc(buffer_size);

    // Allocate temporary arrays
    parser->state.header_max_len = 100;
    parser->state.header_name = (char *)malloc(parser->state.header_max_len);
    parser->state.header_val = (char *)malloc(parser->state.header_max_len);

    parser->state.content_type = nullptr;
    parser->state.content_disposition = nullptr;

    parser->callback_context = nullptr;
    parser->boundary = nullptr;
    parser->header_callback = nullptr;
    parser->data_callback = nullptr;
    parser->data_start_callback = nullptr;
}

void multipart_parse_free(parser_config_t *parser) {
    ESP_LOGI(TAG, "Parser done, freeing");
    if (parser->state.content_type != nullptr) free(parser->state.content_type);
    if (parser->state.content_disposition != nullptr) free(parser->state.content_disposition);
    if (parser->boundary != nullptr) free(parser->boundary);
    free(parser->state.buf);
    free(parser->state.header_name);
    free(parser->state.header_val);
}

int8_t multipart_get_boundary(parser_config_t *parser, const char *content_type) {
    char *substr;
    if ((substr = strstr(content_type, "boundary=")) != nullptr) {
        substr += 9;
        parser->boundary_len = strlen(substr);
        parser->boundary = (char *) malloc(parser->boundary_len + 1);
        strcpy(parser->boundary, substr);
        ESP_LOGI(TAG, "Got boundary: %s", parser->boundary);
        return 0;
    }
    return -1;
}

/**
 * Gets a file name from Content-Disposition header
 */
int8_t multipart_parse_filename(parser_state_t *parser_st, char *filename, size_t maxlen) {
    char *pos = strcasestr(parser_st->content_disposition, "filename=\"");
    if (pos == nullptr) return -1;
    else {
        pos += strlen("filename=\"");
        size_t i = 0;
        while (pos[i] != 0) {
            if (pos[i] == '"') break;
            i++;
        }
        size_t len = (i<maxlen)?i:maxlen;
        memcpy(filename, &pos[0], len);
        filename[len] = 0;
    }
    return 0;
}

int8_t multipart_parse_content_type(char *content_type) {
    return 0;
}

int8_t multipart_parse_chunk(parser_config_t *parser, char *buf, int len) {
    int prev_i = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {   // Found newline char, copy data from last found pos to newline
            size_t l = i - prev_i + 1; // +1 character is ok!
            memcpy(&parser->state.buf[parser->state.buf_pos], &buf[prev_i], l);
            parser->state.buf[parser->state.buf_pos + l] = 0;
            parser->state.buf_pos = 0;
            if (!memcmp(parser->state.buf + 2, parser->boundary, parser->boundary_len)) { // Skip 2 chars = '--'
                switch (parser->state.state) {
                    case PARSE_NONE: parser->state.state = PARSE_HEADERS; break;
                    case PARSE_BODY: parser->state.state = PARSE_NONE; break;
                    default: break;
                }
            } else {
                switch (parser->state.state) {
                    // No state, content outside of boundary marks!
                    case PARSE_NONE: break;

                    // Found body string
                    case PARSE_BODY:
                        if (parser->data_callback != nullptr)
                            if(parser->data_callback(parser->state.buf, l, parser->callback_context) != 0) return -1;
                        break;

                    case PARSE_HEADERS:
                        if (!strcmp(parser->state.buf, "\n") || !strcmp(parser->state.buf, "\r\n")) {
                            // End parse headers, starting body
                            parser->state.state = PARSE_BODY;
                            if (parser->data_start_callback != nullptr) {
                                if (parser->data_start_callback(&parser->state, parser->callback_context) != 0)
                                    return -1; // Bail out if something went wrong
                            }
                            ESP_LOGI(TAG, "Parsing body\n");
                        } else {
                            size_t n = 0;
                            size_t h_val_len;
                            char *ptr = parser->state.buf;
                            char *dest = &parser->state.header_name[0];
                            while (*ptr != 0) {
                                // Skip leading spaces and newlines
                                if (((ptr[0] == ' ') || (ptr[0] == '\n')) && (n == 0)) { ptr++; continue; }
                                // Found name-value delimiter ':'
                                else if (ptr[0] == ':') {
                                    dest[n] = 0;
                                    dest = &parser->state.header_val[0];
                                    n = 0;
                                }
                                // We only can get specific bytes of header, no more!
                                else if (n < parser->state.header_max_len) {
                                    dest[n] = ptr[0];
                                    n++;
                                }
                                ptr++;
                            }
                            dest[n] = 0;
                            h_val_len = n;

                            // Fill in headers in parser struct
                            // --------------------------------
                            ESP_LOGI(TAG, "Got header %s = %s\n", parser->state.header_name, parser->state.header_val);
                            if (!strcmp(parser->state.header_name, "Content-Type")) {
                                parser->state.content_type = (char *)malloc(h_val_len + 1);
                                strcpy(parser->state.content_type, parser->state.header_val);
                                if (parser->header_callback != nullptr) {
                                    if (parser->header_callback("Content-Type", parser->state.content_type, parser->callback_context) != 0)
                                        parser->state.content_type = nullptr;
                                }

                            } else if (!strcmp(parser->state.header_name, "Content-Disposition")) {
                                parser->state.content_disposition = (char *)malloc(h_val_len + 1);
                                strcpy(parser->state.content_disposition, parser->state.header_val);
                                if (parser->header_callback != nullptr) {
                                    if (parser->header_callback("Content-Disposition", parser->state.content_disposition, parser->callback_context) != 0) {
                                        parser->state.content_disposition = nullptr;
                                    }
                                }
                            }
                        }
                        break;
                }
            }
            prev_i = i + 1;
        }
    }


    if (parser->state.state != PARSE_BODY) {
        // Check if we have enough buffer space
        if (parser->state.buf_pos + len - prev_i >= parser->state.buf_size) return -1;

        // When we're not in body section then,
        // all that was not processed to be added to the end of the buffer
        memcpy(&parser->state.buf[parser->state.buf_pos], &buf[prev_i], len - prev_i);
        parser->state.buf_pos += len - prev_i;
    } else {
        // For data section we just pass all the data to callback
        if (parser->data_callback != nullptr) {
            if (parser->data_callback(&buf[prev_i], len - prev_i, parser->callback_context) != 0) return -1;
        }
        parser->state.buf_pos = 0;
    }

    return 0;
}
