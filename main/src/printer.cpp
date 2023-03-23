#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "server.h"
#include "printer.h"

#define COMMAND_PING                "M105\n"
#define PRINTER_TASK_STACK_SIZE     2048

extern Printer printer;
extern Server server;

static const char TAG[] = "esp3d-printer";

void Printer::parse_temperature_report(const char *report) {
    ESP_LOGI(TAG, "Got temperature report %s", report);
    float tc, bc;
    int x, y;
    sscanf(report, "T:%f /%f B:%f /%f @:%d B@:%d",
           &state.temp_hot_end, &tc, &state.temp_bed, &bc,
           &x, &y);
    server.send_status_ws();
}

void Printer::parse_report(const char *report) {
    // ok - confirmed message from printer. Each sent command MUST be answered with
    // 'ok'. Even if it was unknown command, Marlin answers 'ok' with preceding 'echo'.
    if ((report[0] == 'o') && (report[1] == 'k')) {
        uart->lock(false);
        uart->transmit_confirm();
#ifdef DEBUG
        ESP_LOGI(TAG, "Confirmed #%lu", uart->get_command_id_confirmed());
#endif
        if (report[2] != 0) {
            // Temperature report may be after 'ok', so we need to get it here
            if (strncmp(&report[3], "T:", 2) == 0) parse_temperature_report(&report[3]);
        }
    }
    else if (strncmp(report, "echo:busy: ", 11) == 0) {
        if (state.status != PRINTER_PRINTING) state.status = PRINTER_WORKING;
        uart->lock(true);
        ESP_LOGI(TAG, "Busy");
    }
    else if (strncmp(report, "T:", 2) == 0) {
        parse_temperature_report(report);
    }
    else if (strncmp(&report[1], "T:", 2) == 0) {
        parse_temperature_report(&report[1]);
    }
    else if (strncmp(report, "X:", 2) == 0) {
        ESP_LOGI(TAG, "Got position report %s", report);
    }
    else if (strncmp(report, "measured", 8) == 0) {
        ESP_LOGI(TAG, "Got probe report %s", report);
    }
}

/**
 * Task function. Printer ping cycle. Executed every second if there's nothing in buffer to send.
 * @param args
 */
[[noreturn]] void Printer::task_status_report([[gnu::unused]] void *args) {
    auto p = (Printer *) args;
    while (true) {
        // Blocks until ping request is added
        while (p->get_uart()->send(COMMAND_PING) == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        p->set_status(PRINTER_IDLE);

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
                    p->state.opened_file = nullptr;
                    break;
                }
#ifdef DEBUG
                ESP_LOGI(TAG, "Got line: %s", line);
#endif
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
}

void Printer::start(FILE *f) { state.opened_file = f; }
void Printer::stop() { state.printing_stop = true; }

unsigned long int Printer::send_cmd(const char *cmd) { return uart->send(cmd); }
SerialPort *Printer::get_uart() { return uart; }
float Printer::get_temp_bed() const { return state.temp_bed; }
float Printer::get_temp_hot_end() const { return state.temp_hot_end; }
PrinterStatus Printer::get_status() const { return state.status; }
void Printer::set_status(PrinterStatus st) { state.status = st; }
FILE *Printer::get_opened_file() const { return state.opened_file; }

/**
 * Callbacks
 */
void parse_report_callback(const char *report) {
    printer.parse_report(report);
}