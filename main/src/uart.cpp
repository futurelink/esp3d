/*
  uart.cpp - UART specific routines
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

#include <ctime>
#include "uart.h"
#include "server.h"

//#define DEBUG

static const char TAG[] = "esp3d-print-uart";

extern Server server;

SerialPort::SerialPort(int baud, gpio_num_t rxd_pin, gpio_num_t txd_pin) {
    this->baud = baud;
    this->rxd_pin = rxd_pin;
    this->txd_pin = txd_pin;
    this->locked = false;
    this->str_pos = 0;
    this->task_rx_tx = nullptr;
    this->rx_buffer = (char *) malloc(UART_TMP_BUF_SIZE);

    this->printer_response_timeout_callback = nullptr;
    this->printer_response_parse_callback = nullptr;
    this->printer_command_sent_callback = nullptr;

    // Create command buffer
    command_id_cnt = 0;
    command_id_sent = 0;
    command_buffer_head = 0;
    command_buffer_tail = 0;
}

void SerialPort::set_sent_callback(void (*callback)()) { printer_command_sent_callback = callback; }
void SerialPort::set_response_callback(bool (*callback)(const char *)) { printer_response_parse_callback = callback; }
void SerialPort::set_timeout_callback(bool (*resp_timeout_callback)(), void (*on_timeout_callback)()) {
    printer_response_timeout_callback = resp_timeout_callback;
    printer_on_timeout_callback = on_timeout_callback;
}

/**
 * Adds a command string to buffer to be sent via UART.
 * @param command
 * @return enqueued command number if added successfully or 0 if buffer was full
 */
unsigned long SerialPort::send(const char *command) {
#ifdef DEBUG
    ESP_LOGI(TAG, "uart_send start");
#endif
    uint8_t next_head = command_buffer_head + 1;
    if (next_head == COMMAND_BUFFER_SIZE) next_head = 0;

    // Cannot add - buffer is full or full of unconfirmed messages
    if (next_head == command_buffer_tail) return 0;

    // Allocate memory in a buffer and copy command, reserve 1 character in case
    // when there's no \n in the end, and we need to add it.
    uint8_t len = strlen(command);
    char *ptr = command_buffer[command_buffer_head];
    strcpy(ptr, command);
    if (ptr[len-1] != '\n') {   // add newline character if last character is not
        ptr[len] = '\n';        // a new line then set next to \n and add \0
        ptr[len+1] = 0;
    }
    command_buffer_head = next_head;

#ifdef DEBUG
    ESP_LOGI(TAG, "uart_send done: >> %s", ptr);
#endif

    // Increment command ID
    auto id = command_id_cnt; command_id_cnt = id + 1;

    return command_id_cnt;
}

esp_err_t SerialPort::init() {
    uart_config_t uart_config = {
            .baud_rate = baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT
    };
    esp_err_t err_uart = uart_param_config(UART, &uart_config);
    if (err_uart == ESP_OK) err_uart = uart_set_pin(UART, rxd_pin, txd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err_uart == ESP_OK) err_uart = uart_driver_install(UART, UART_TMP_BUF_SIZE, UART_TMP_BUF_SIZE, 0, nullptr, 0);
    if (err_uart != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed with error 0x%x", err_uart);
        return err_uart;
    }

    ESP_LOGI(TAG, "UART initialized");

    xTaskCreate(SerialPort::rx_tx_task, "uart_rx_tx_task", UART_TASK_STACK_SIZE, this, UART_TASK_PRIORITY, &task_rx_tx);

    return ESP_OK;
}

/**
 * Task function. Main printer communication cycle.
 * @param args
 */
void SerialPort::rx_tx_task(void *args) {
    auto serialPort = (SerialPort *)args;
    serialPort->str_pos = 0;

    bool can_send = true;
    while (true) {
        can_send = serialPort->receive() || can_send;
        if (can_send) can_send = !serialPort->transmit();
        vPortYield();
    }
}

/* Receive cycle */
bool SerialPort::receive() {
    bool ok_received = false;
    int len = uart_read_bytes(UART, rx_buffer, UART_TMP_BUF_SIZE - 1, 0);
    if (len > 0) {
#ifdef DEBUG
        esp_log_write(ESP_LOG_INFO, TAG, "uart_receive start");
#endif
        uint16_t cnt = 0;

#ifdef DEBUG
        char str_t[128];
        int l = (len > 127) ? 127 : len;
        memcpy(str_t, rx_buffer, l); str_t[l] = 0;
        esp_log_write(ESP_LOG_INFO, TAG, "<%d:%s>\n", len, str_t);
        // Workaround(!!!)
        // There's a bug in IDF that corrupts an answer from printer,
        // so we can't get confirmation. I decided that any string starting with 'ok'
        // is enough to be certain that printer confirmed our previous command.
        /*if ((len >= 2) && (rx_buffer[0] == 'o') && (rx_buffer[1] == 'k')) {
            printer_response_parse_callback(str_t);
            ok_received = true;
            cnt = 2;
        }*/
#endif

        // Parse the rest of the string
        do {
            if (rx_buffer[cnt] == '\n') {
                str[str_pos] = 0;
                if (str_pos > 0) {
                    if (printer_response_parse_callback != nullptr)
                        ok_received = printer_response_parse_callback(str) || ok_received;
                    else ESP_LOGE(TAG, "Response process callback was not set!");
                    str_pos = 0;
                }
            } else {
                str[str_pos++] = rx_buffer[cnt];
                str[str_pos] = 0;
            }
        } while (++cnt < len);
#ifdef DEBUG
        ESP_LOGI(TAG, "uart_receive done");
#endif
    } else {
        if (printer_response_timeout_callback != nullptr) {
            bool timeout_triggered = printer_response_timeout_callback();
            if (timeout_triggered) {
                if (printer_on_timeout_callback != nullptr) printer_on_timeout_callback();
                ESP_LOGI(TAG, "Response timeout triggered, re-sending last command");

                // Rewind to previous command
                if (command_id_sent > 0) {
                    auto id = command_id_sent; command_id_sent = id - 1; // Increment sent command ID
                }
                uint8_t tail = (command_buffer_tail == 0) ? COMMAND_BUFFER_SIZE : command_buffer_tail - 1;
                command_buffer_tail = tail;
                return true;
            }
        }
    }

    return ok_received;
}

/**
 * Internal function.
 * Sends command from buffer's tail, but does not increment buffer confirmed pointer.
 * It should be incremented a bit later when confirmation comes from printer.
 */
bool SerialPort::transmit() {
    uint8_t tail = command_buffer_tail;
    if (tail == command_buffer_head) return false;            // Empty buffer

#ifdef DEBUG
    ESP_LOGI(TAG, "uart_transmit_from_buffer start ");
#endif

    uart_write_bytes(UART, command_buffer[tail], strlen(command_buffer[tail]));
    if (++tail == COMMAND_BUFFER_SIZE) tail = 0;        // Increment transmit pointer
    command_buffer_tail = tail;
    auto id = command_id_sent; command_id_sent = id + 1; // Increment sent command ID
    if (printer_command_sent_callback != nullptr) printer_command_sent_callback();

#ifdef DEBUG
    ESP_LOGI(TAG, "uart_transmit_from_buffer done");
#endif

    return true;
}

unsigned long int SerialPort::get_command_id_sent() const {
    return command_id_sent;
}

void SerialPort::lock(bool lock) {
    this->locked = lock;
}

bool SerialPort::is_locked() const {
    return locked;
}

/**
 * Defines how many unconfirmed commands printer can bear. Technically this is its command buffer
 * size.
 *
 * @param size
 * @return
 */
int SerialPort::get_buffer_head() const { return command_buffer_head; }
int SerialPort::get_buffer_tail() const { return command_buffer_tail; }

SerialPort::~SerialPort() {
    free(rx_buffer);
}
