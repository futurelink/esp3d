#ifndef ESP32_PRINT_PRINTER_H
#define ESP32_PRINT_PRINTER_H

#include <cstdio>

#include "sdkconfig.h"
#include "uart.h"

/**
 * Callbacks definitions
 */
void parse_report_callback(const char *report);

/**
 * Printer class definition
 */
enum PrinterStatus { PRINTER_UNKNOWN, PRINTER_IDLE, PRINTER_WORKING, PRINTER_PRINTING };

typedef struct {
    enum PrinterStatus status;  // Current printer status
    bool status_requested;
    float temp_hot_end;         // Hot end temperature
    float temp_bed;             // Heat bed temperature
    FILE *opened_file;          // Descriptor of G-code file
    bool printing_stop;         // Flags printer to stop its job
} printer_state_t;

class Printer {
private:
    SerialPort      *uart;
    printer_state_t state;

public:
    Printer() {
        state = {
                .status = PRINTER_UNKNOWN,
                .temp_hot_end = 0,
                .temp_bed = 0,
                .opened_file = nullptr,
                .printing_stop = false
        };

        uart = new SerialPort(250000, GPIO_NUM_16, GPIO_NUM_13, parse_report_callback);
    }

    void init();
    void start(FILE *f);
    void stop();

    void set_status(PrinterStatus st);
    [[nodiscard]] FILE *get_opened_file() const;

    void parse_report(const char *report);
    void parse_temperature_report(const char *report);

    unsigned long int send_cmd(const char *cmd);
    SerialPort *get_uart();
    [[nodiscard]] PrinterStatus get_status() const;
    [[nodiscard]] float get_temp_hot_end() const;
    [[nodiscard]] float get_temp_bed() const;

private:
    [[noreturn]] static void task_status_report(void *arg);
    [[noreturn]] static void task_print(void *arg);
};

#endif //ESP32_PRINT_PRINTER_H
