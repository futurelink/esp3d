/*
  printer.cpp - printer specific class
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

#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <cmath>

#include "server.h"
#include "printer.h"

#define COMMAND_PING                    "M105\n"
#define PRINTER_TASK_STACK_SIZE         4096
#define PRINTER_TASK_STATE_STACK_SIZE   2048

extern Printer printer;
extern Server server;

static const char TAG[] = "esp3d-printer";

Printer::Printer() {
    state = {
            .status = PRINTER_UNKNOWN,
            .status_requested = false,
            .temp_hot_end = 0,
            .temp_bed = 0,
            .printing_stop = false,
            .print_file = nullptr,
            .print_file_bytes = 0,
            .print_file_bytes_sent = 0
    };

    uart = new SerialPort(250000, GPIO_NUM_16, GPIO_NUM_13, parse_report_callback);
}

void Printer::parse_temperature_report(const char *report) {
    //ESP_LOGI(TAG, "Got temperature report %s", report);
    float tc, bc;
    int x, y;
    sscanf(report, "T:%f /%f B:%f /%f @:%d B@:%d", &state.temp_hot_end, &tc, &state.temp_bed, &bc, &x, &y);
    state.status_updated = true;
}

void Printer::parse_report(const char *report) {
    // Save last report
    strcpy(state.last_report, report);

    // ok - confirmed message from printer. Each sent command MUST be answered with
    // 'ok'. Even if it was unknown command, Marlin answers 'ok' with preceding 'echo'.
    if ((report[0] == 'o') && (report[1] == 'k')) {
        uart->lock(false);
        uart->transmit_confirm();
#ifdef DEBUG
//        ESP_LOGI(TAG, "Confirmed #%lu", uart->get_command_id_confirmed());
#endif
        if (report[2] != 0) {
            // Temperature report may be after 'ok', so we need to get it here
            if (strncmp(&report[3], "T:", 2) == 0) {
                parse_temperature_report(&report[3]);
            }
        }
        if (state.status == PRINTER_UNKNOWN) state.status = PRINTER_IDLE;
    }
    else if (strncmp(report, "echo:busy: ", 11) == 0) {
        if (state.status != PRINTER_PRINTING) state.status = PRINTER_WORKING;
        uart->lock(true);
//        ESP_LOGI(TAG, "Busy");
    }
    else if (strncmp(report, "T:", 2) == 0) {
        parse_temperature_report(report);
    }
    else if (strncmp(&report[1], "T:", 2) == 0) {
        parse_temperature_report(&report[1]);
    }
    else if (strncmp(report, "X:", 2) == 0) {
//        ESP_LOGI(TAG, "Got position report %s", report);
    }
    else if (strncmp(report, "measured", 8) == 0) {
//        ESP_LOGI(TAG, "Got probe report %s", report);
    }
}

/**
 * Task function. Printer ping cycle. Executed every second if there's nothing in buffer to send.
 * @param args
 */
[[noreturn]] void Printer::task_status_report([[gnu::unused]] void *args) {
    auto p = (Printer *) args;
    while (true) {
        if (p->state.status_updated) {
            server.send_status_ws();
            p->state.status_updated = false;
            p->state.status_requested = false;
        } else if (!p->state.status_requested && (p->state.status != PRINTER_WORKING)) {
            // Wait until ping request is added
            while (p->get_uart()->send(COMMAND_PING) == 0) vTaskDelay(10 / portTICK_PERIOD_MS);
            p->state.status_requested = true;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);  // Wait 0.5 sec, total cycle request-update takes ~1sec
    }
}

[[noreturn]] void Printer::task_state_log([[gnu::unused]] void *args) {
    auto p = (Printer *) args;
    while (true) {
        ESP_LOGI(TAG, "Command log: sent #%lu, confirmed #%lu, lock: %d, last report: '%s'",
                 p->uart->get_command_id_sent(), p->uart->get_command_id_confirmed(), p->uart->is_locked(), p->state.last_report);
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait 1 sec
    }
}

void Printer::task_print(void *arg) {
    auto p = (Printer *) arg;
    while (true) {
        auto f = p->get_opened_file();
        if (f != nullptr) {
            char line[80];
            ESP_LOGI(TAG, "Starting print...");
            p->state.status = PRINTER_PRINTING;
            while (fgets(line, 80, f) != nullptr) {
                if (p->state.printing_stop) {
                    p->state.printing_stop = false;
                    p->state.print_file = nullptr;
                    break;
                }
#ifdef DEBUG
                ESP_LOGI(TAG, "Got line: %s", line);
#endif
                p->state.print_file_bytes_sent += strlen(line); // To track progress
                if ((line[0] != 'G') && (line[0] != 'M')) continue; // Send only M and G codes
                while (!p->send_cmd(line)) {
                    // If we haven't managed to send because buffer was full,
                    // then we wait 100ms and try again.
                    vTaskDelay(100 / portTICK_PERIOD_MS); // 100ms delay
                }
                vPortYield();
            }
            p->state.status = PRINTER_IDLE;
            p->stop();
            ESP_LOGI(TAG, "Ended print.");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 100ms delay
    }
}

void Printer::init() {
    uart->init();
    xTaskCreate(Printer::task_status_report, "printer_task_report", PRINTER_TASK_STACK_SIZE, this, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(Printer::task_print, "printer_task_print", PRINTER_TASK_STACK_SIZE, this, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(Printer::task_state_log, "printer_task_state", PRINTER_TASK_STATE_STACK_SIZE, this, tskIDLE_PRIORITY, nullptr);
}

esp_err_t Printer::start(FILE *f) {
    if (state.print_file != nullptr) return ESP_FAIL;
    fseek(f, 0, SEEK_END);              // Determine file size
    state.print_file_bytes = ftell(f);
    rewind(f);                          // Go back
    state.print_file_bytes_sent = 0;
    state.print_file = f;
    return ESP_OK;
}

esp_err_t Printer::stop() {
    if (state.print_file == nullptr) return ESP_FAIL;
    state.printing_stop = true;
    state.print_file_bytes = 0;
    state.print_file_bytes_sent = 0;
    return ESP_OK;
}

unsigned long int Printer::send_cmd(const char *cmd) { return uart->send(cmd); }
SerialPort *Printer::get_uart() { return uart; }
float Printer::get_temp_bed() const { return state.temp_bed; }
float Printer::get_temp_hot_end() const { return state.temp_hot_end; }
PrinterStatus Printer::get_status() const { return state.status; }
void Printer::set_status(PrinterStatus st) { state.status = st; }
FILE *Printer::get_opened_file() const { return state.print_file; }
float Printer::get_progress() const {
    if ((get_opened_file() != nullptr) && (state.print_file_bytes != 0)) {
        return roundf(((float)state.print_file_bytes_sent / (float)state.print_file_bytes) * 100) / 100;
    } else return 0;
}

/**
 * Callbacks
 */
void parse_report_callback(const char *report) {
    printer.parse_report(report);
}