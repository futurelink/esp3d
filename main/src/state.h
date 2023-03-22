#ifndef ESP32_PRINT_STATE_H
#define ESP32_PRINT_STATE_H

enum PrinterStatus { PRINTER_UNKNOWN, PRINTER_IDLE, PRINTER_WORKING };

typedef struct {
    enum PrinterStatus status;
    float temp_hot_end;
    float temp_bed;
    char *selected_file;
} printer_state_t;

#endif //ESP32_PRINT_STATE_H
