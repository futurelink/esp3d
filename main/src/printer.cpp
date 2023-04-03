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

#include <cmath>
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "server.h"
#include "printer.h"
#include "settings.h"

#define TIMEOUT_VALUE                   5000
#define COMMAND_PING                    "M105\n"
#define PRINTER_TASK_STACK_SIZE         4096
#define PRINTER_TASK_STATE_STACK_SIZE   2048

extern Printer printer;
extern Server server;
extern Settings settings;

static const char TAG[] = "esp3d-printer";

Printer::Printer() {
    state = {
            .connected = false,
            .status = PRINTER_IDLE,
            .status_requested = false,
            .status_updated = false,
            .temp_hot_end = 0,
            .temp_hot_end_target = 0,
            .temp_bed = 0,
            .temp_bed_target = 0,
            .printing_stop = false,
            .print_file = nullptr,
            .print_file_bytes = 0,
            .print_file_bytes_sent = 0
    };
    last_sent_command_time = 0;
    uart = nullptr;
}

/**
 * Parses Marlin temperature report string and updates the status,
 * which looks like this: T:248.34 /245.00 B:100.81 /100.00 @:33 B@:128 W:9
 * @param report
 */
void Printer::parse_temperature_report(const char *report) {
    ESP_LOGI(TAG, "Parsing temperature report");

    unsigned short flag = 0, cnt = 0, pos = 0;
    char val[10];
    do {
        unsigned short len;
        switch (report[cnt]) {
            case 'T': flag = (1 << 0); break;
            case 'B': flag = (1 << 1); break;
            case '@': if (flag & (1 << 1)) flag = ((1 << 2) | (1 << 1)); else flag = ((1 << 2) | (1 << 0)); break;
            case ':': case '/':  // Save position
                pos = cnt + 1;
                if (report[cnt] == '/') flag |= (1 << 3);       // Target
                break;
            case 'W': flag = (1 << 5); break;
            case ' ': case 0: // 0 is EOL
                len = (cnt - pos > 9) ? 10 : cnt - pos;
                strncpy(val, &report[pos], len);
                val[len] = 0;
                char *end;
                float val_f = strtof(val, &end);
                if (flag & (1 << 0)) {
                    if (flag & (1 << 3)) { state.temp_hot_end_target = val_f; flag = 0; }
                    else if (flag & (1 << 2)) { } // Power
                    else state.temp_hot_end = val_f;
                } else if (flag & (1 << 1)) {
                    if (flag & (1 << 3)) { state.temp_bed_target = val_f; flag = 0; }
                    else if (flag & (1 << 2)) { } // Power
                    else state.temp_bed = val_f;
                }
                break;
        }
    } while (report[cnt++] != 0);

    state.status_updated = true;
}

/**
 * UART calls this method to get to know if it can send a command once again,
 * because there was a timeout.
 * @return
 */
bool Printer::is_timeout() {
    if (last_sent_command_time == 0) return false; // It can't be timeout when no command sent
    if (uart->is_locked()) return false;
    unsigned int current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    return ((current_time - last_sent_command_time) > TIMEOUT_VALUE); // 5sec is a timeout
}

void Printer::on_timeout() {
    state.connected = false;
    state.status_updated = true;
}

/**
 * When UART sends data to printer it calls this method to let printer know,
 * that command was sent and handle this fact i.e. start counting seconds to timeout.
 */
void Printer::command_sent() {
    last_sent_command_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

bool Printer::parse_report(const char *report) {
    strcpy(state.last_report, report); // Save last report

#ifdef DEBUG
    ESP_LOGI(TAG, "Got report %s", report);
#endif

    // ok - confirmed message from printer. Each sent command MUST be answered with
    // 'ok'. Even if it was unknown command, Marlin answers 'ok' with preceding 'echo'.
    if ((report[0] == 'o') && (report[1] == 'k')) {
        last_sent_command_time = 0; // Reset timeout
#ifdef DEBUG
        ESP_LOGI(TAG, "Confirmed #%lu", uart->get_command_id_confirmed());
#endif
        if ((report[2] != 0) && (report[2] != '\n')) {
            // Temperature report may be after 'ok', so we need to get it here
            if (strncmp(&report[3], "T:", 2) == 0) parse_temperature_report(&report[3]);
        }
        if (state.status == PRINTER_BUSY) state.status = PRINTER_IDLE;
        state.connected = true;
        uart->lock(false);
        return true;
    }

    if (strncmp(report, "echo:busy: ", 11) == 0) {
        uart->lock(true);
        if (state.status != PRINTER_PRINTING) state.status = PRINTER_BUSY;
    }
    else if (strncmp(report, "T:", 2) == 0) parse_temperature_report(report);
    else if (strncmp(report, " T:", 3) == 0) parse_temperature_report(&report[1]);
    else if (strncmp(report, "X:", 2) == 0) ESP_LOGI(TAG, "Got position report %s", report);
    else if (strncmp(report, "measured", 8) == 0) ESP_LOGI(TAG, "Got probe report %s", report);

    return false;
}

/**
 * Task function. Printer ping cycle. Executed every second if there's nothing in buffer to send.
 * @param args
 */
[[noreturn]] void Printer::task_status_report(void *args) {
    auto p = (Printer *) args;
    while (true) {
        if (p->state.status_updated) {
            server.send_status_ws();
            p->state.status_updated = false;
            p->state.status_requested = false;
        } else if (!p->state.status_requested && (p->state.status != PRINTER_BUSY)) {
            // Wait until ping request is added
            while (p->get_uart()->send(COMMAND_PING) == 0) vTaskDelay(10 / portTICK_PERIOD_MS);
            p->state.status_requested = true;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);  // Wait 0.5 sec, total cycle request-update takes ~1sec
    }
}

[[noreturn]] void Printer::task_state_log(void *args) {
    auto p = (Printer *) args;
    while (true) {
        ESP_LOGI(TAG, "Command log: sent #%lu, lock: %d, last report: '%s'",
                 p->uart->get_command_id_sent(), p->uart->is_locked(), p->state.last_report);
        //ESP_LOGI(TAG, "Command log: head #%d, tail #%d", p->uart->get_buffer_head(), p->uart->get_buffer_tail());
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

                // Handle print stop signal
                if (p->state.printing_stop) {
                    p->state.printing_stop = false;
                    p->state.print_file = nullptr;
                    p->send_stop_script();
                    break;
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

esp_err_t Printer::init() {
    uart = new SerialPort((int) settings.get_baud_rate(), GPIO_NUM_12, GPIO_NUM_13);
    esp_err_t res = uart->init();
    if (res != ESP_OK) return res;

    uart->set_sent_callback(sent_callback);
    uart->set_response_callback(receive_callback);
    uart->set_timeout_callback(is_timeout_callback, on_timeout_callback);

    xTaskCreate(Printer::task_status_report, "printer_task_report", PRINTER_TASK_STACK_SIZE, this, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(Printer::task_print, "printer_task_print", PRINTER_TASK_STACK_SIZE, this, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(Printer::task_state_log, "printer_task_state", PRINTER_TASK_STATE_STACK_SIZE, this, tskIDLE_PRIORITY, nullptr);

    return ESP_OK;
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
    if (state.print_file != nullptr) {
        fclose(state.print_file);
        state.print_file = nullptr;
    }
    return ESP_OK;
}

void Printer::send_stop_script() {
    ESP_LOGI(TAG, "Sending stop script commands");
    for (auto &i : stop_script) {
        while (!send_cmd(i)) asm volatile("nop");
    }
}

unsigned long int Printer::send_cmd(const char *cmd) { return uart->send(cmd); }
SerialPort *Printer::get_uart() { return uart; }
float Printer::get_temp_bed() const { return state.temp_bed; }
float Printer::get_temp_bed_target() const { return state.temp_bed_target; }
float Printer::get_temp_hot_end() const { return state.temp_hot_end; }
float Printer::get_temp_hot_end_target() const { return state.temp_hot_end_target; }
PrinterStatus Printer::get_status() const { if (state.connected) return state.status; else return PRINTER_DISCONNECTED; }
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
void sent_callback() { printer.command_sent(); }
bool receive_callback(const char *report) { return printer.parse_report(report); }
bool is_timeout_callback() { return printer.is_timeout(); }
void on_timeout_callback() { printer.on_timeout(); }
