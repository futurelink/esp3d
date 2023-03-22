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
    int8_t              (*header_callback)(const char *header, const char *value);
    int8_t              (*data_callback)(const char *data, size_t len);
    int8_t              (*data_start_callback)(parser_state_t *parser);
    parser_state_t      state;
} parser_config_t;

void multipart_parse_init(parser_config_t *parser, size_t buffer_size);
void multipart_parse_free(parser_config_t *parser);
int8_t multipart_parse_chunk(parser_config_t *parser, char *buf, int len);
int8_t multipart_parse_filename(parser_state_t *parser, char *filename, size_t maxlen);
int8_t multipart_get_boundary(parser_config_t *parser, const char *content_type);

#endif // ESP32_PRINT_MULTIPART_H