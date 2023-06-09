/*
  printer.h - printer specific class
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

#ifndef ESP32_PRINT_PRINTER_H
#define ESP32_PRINT_PRINTER_H

#include <cstdio>

#include "sdkconfig.h"
#include "uart.h"

/**
 * Callbacks definitions
 */
void sent_callback();
bool receive_callback(const char *report);
bool is_timeout_callback();
void on_timeout_callback();

/**
 * Printer class definition
 */
enum PrinterStatus { PRINTER_DISCONNECTED, PRINTER_IDLE, PRINTER_BUSY, PRINTER_PRINTING };

typedef struct {
    bool connected;
    enum PrinterStatus status;  // Current printer status
    bool status_requested;
    bool status_updated;
    float temp_hot_end;         // Hot end temperature
    float temp_hot_end_target;
    float temp_bed;             // Heat bed temperature
    float temp_bed_target;

    bool printing_stop;         // Flags printer to stop its job
    FILE *print_file;           // Descriptor of G-code file
    unsigned long int print_file_bytes;
    unsigned long int print_file_bytes_sent;

    char last_report[256];
} printer_state_t;

class Printer {
private:
    SerialPort      *uart;
    printer_state_t state;

    char stop_script[4][32] = { "M104 S0\n", "M140 S0\n", "G28\n", "M84\n" };

    // Variables to identify a timeout happened
    unsigned int    last_sent_command_time;

    void send_stop_script();

public:
    Printer();

    esp_err_t init();
    esp_err_t start(FILE *f);
    esp_err_t stop();

    void set_status(PrinterStatus st);
    [[nodiscard]] FILE *get_opened_file() const;

    bool is_timeout();
    void on_timeout();
    void command_sent();
    bool parse_report(const char *report);
    void parse_temperature_report(const char *report);

    unsigned long int send_cmd(const char *cmd);
    SerialPort *get_uart();
    [[nodiscard]] PrinterStatus get_status() const;
    [[nodiscard]] float get_temp_hot_end() const;
    [[nodiscard]] float get_temp_hot_end_target() const;
    [[nodiscard]] float get_temp_bed() const;
    [[nodiscard]] float get_temp_bed_target() const;
    [[nodiscard]] float get_progress() const;

private:
    [[noreturn]] static void task_status_report(void *arg);
    [[noreturn]] static void task_print(void *arg);
    [[noreturn]] static void task_state_log(void *arg);
};

#endif //ESP32_PRINT_PRINTER_H
