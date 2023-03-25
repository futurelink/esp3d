/*
  multipart.h - multipart/form-data processing
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

#ifndef ESP32_PRINT_MULTIPART_H
#define ESP32_PRINT_MULTIPART_H

#include <cstring>
#include <cstdlib>

enum ParserState { PARSE_NONE, PARSE_HEADERS, PARSE_BODY };

typedef struct {
    enum ParserState    state;
    size_t              buf_size;
    int                 buf_pos;
    char                *buf;
    size_t              header_max_len;
    char                *header_name;
    char                *header_val;
    char                *content_type;
    char                *content_disposition;
} parser_state_t;

typedef struct {
    char                *boundary;
    size_t              boundary_len;
    void                *callback_context;
    int8_t              (*header_callback)(const char *header, const char *value, void *context);
    int8_t              (*data_callback)(const char *data, size_t len, void *context);
    int8_t              (*data_start_callback)(parser_state_t *parser, void *context);
    parser_state_t      state;
} parser_config_t;

void multipart_parse_init(parser_config_t *parser, size_t buffer_size);
void multipart_parse_free(parser_config_t *parser);
int8_t multipart_parse_chunk(parser_config_t *parser, char *buf, int len);
int8_t multipart_parse_filename(parser_state_t *parser, char *filename, size_t maxlen);
int8_t multipart_get_boundary(parser_config_t *parser, const char *content_type);

#endif // ESP32_PRINT_MULTIPART_H