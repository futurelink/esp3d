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

#include "uart.h"
#include "server.h"

//#define DEBUG

static const char TAG[] = "esp3d-print-uart";

extern Server server;

SerialPort::SerialPort(int baud, gpio_num_t rxd_pin, gpio_num_t txd_pin, void (*callback)(const char *)) {
    this->baud = baud;
    this->rxd_pin = rxd_pin;
    this->txd_pin = txd_pin;
    this->locked = false;
    this->str_pos = 0;
    this->task_send = nullptr;
    this->task_receive = nullptr;

    // Create command buffer
    command_id_cnt = 0;
    command_id_sent = 0;
    command_id_confirmed = 0;
    //command_buffer = (char **) malloc(COMMAND_BUFFER_SIZE * sizeof(char *));
    command_buffer_head = 0;
    command_buffer_tail = 0;
    command_buffer_tail_confirmed = 0;
    printer_buffer_size = 3;

    printer_response_parse_callback = (void (*)(const char *)) callback;
    //spinlock_initialize(&uart_mux);
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
    if ((next_head == command_buffer_tail_confirmed) || (next_head == command_buffer_tail)) return 0;

    // Allocate memory in a buffer and copy command, reserve 1 character in case
    // when there's no \n in the end, and we need to add it.
    uint8_t len = strlen(command) + 2;
    //command_buffer[command_buffer_head] = (char *) malloc(len);
    char *ptr = command_buffer[command_buffer_head];
    strcpy(ptr, command);
    if (ptr[len-3] != '\n') { // add newline character if it's not there
        ptr[len-2] = '\n';
        ptr[len-1] = 0;
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

    // Start UART tasks
    xTaskCreate(SerialPort::send_task, "uart_send_task", UART_TASK_STACK_SIZE, this, UART_TASK_PRIORITY, &task_send);
    xTaskCreate(SerialPort::receive_task, "uart_receive_task", UART_TASK_STACK_SIZE, this, UART_TASK_PRIORITY, &task_receive);

    return ESP_OK;
}

/**
 * Task function. Main printer communication cycle.
 * @param args
 */
void SerialPort::receive_task([[gnu::unused]] void *args) {
    auto serialPort = (SerialPort *)args;
    serialPort->str_pos = 0;
    while (true) {
        serialPort->receive();
        vPortYield();
    }
}

/**
 * Task function. Main printer communication cycle.
 * @param args
 */
void SerialPort::send_task(void *args) {
    auto serialPort = (SerialPort *)args;
    while (true) {
        if (!serialPort->is_locked()) serialPort->transmit_from_buffer();
        vPortYield();
    }
}

void SerialPort::receive() {
    int len = uart_read_bytes(UART, uart_rx_buffer, (UART_TMP_BUF_SIZE - 1), 0);
    if (len > 0) {
#ifdef DEBUG
        ESP_LOGI(TAG, "uart_receive start");
#endif
        uint16_t cnt = 0;
        do {
            if (uart_rx_buffer[cnt] == '\n') {
                str[str_pos] = 0;
                str_pos = 0;
#ifdef DEBUG
                ESP_LOGI(TAG, "<< %s", str);
#endif
                vPortEnterCritical(&uart_mux);
                printer_response_parse_callback(str);
                vPortExitCritical(&uart_mux);
            } else if (str_pos < UART_TMP_BUF_SIZE) str[str_pos++] = uart_rx_buffer[cnt];
        } while (++cnt < len);
#ifdef DEBUG
        ESP_LOGI(TAG, "uart_receive done");
#endif
    }
}

/**
 * Internal function.
 * Sends command from buffer's tail, but does not increment buffer confirmed pointer.
 * It should be incremented a bit later when confirmation comes from printer.
 */
bool SerialPort::transmit_from_buffer() {
    /*if ((command_id_sent - command_id_confirmed) > printer_buffer_size) {
#ifdef DEBUG
        ESP_LOGI(TAG, "Too many unconfirmed messages %lu / %lu", command_id_confirmed, command_id_sent);
#endif
        return true;
    }*/

    uint8_t tail = command_buffer_tail;
    if (tail == command_buffer_head) {
        vTaskPrioritySet(task_send, tskIDLE_PRIORITY); // Nothing is in buffer so lower the priority
        return false;            // Empty buffer
    }

    if (tail != command_buffer_tail_confirmed) {
#ifdef DEBUG
        ESP_LOGI(TAG, "There's unconfirmed message %d / %d", tail, command_buffer_tail_confirmed);
#endif
        return true;   // Don't send anything until we haven't got answer
    }

#ifdef DEBUG
    ESP_LOGI(TAG, "uart_transmit_from_buffer start ");
#else
    vPortEnterCritical(&uart_mux);
#endif

    uart_write_bytes(UART, command_buffer[tail], strlen(command_buffer[tail]));
    if (++tail == COMMAND_BUFFER_SIZE) tail = 0;        // Increment transmit pointer
    command_buffer_tail = tail;
    auto id = command_id_sent; command_id_sent = id + 1; // Increment sent command ID

#ifndef DEBUG
    vPortExitCritical(&uart_mux);
#else
    ESP_LOGI(TAG, "uart_transmit_from_buffer done");
#endif
    return true;
}

/**
 * Internal function. Shifts confirmed message pointer in UART ring buffer.
 */
void SerialPort::transmit_confirm() {
#ifdef DEBUG
//    ESP_LOGI(TAG, "uart_transmit_confirm start");
#endif
    //free(command_buffer[command_buffer_tail_confirmed]);    // Free sent command buffer
    uint8_t next = command_buffer_tail_confirmed + 1;       // Increment confirmed number
    if (next == COMMAND_BUFFER_SIZE) next = 0;
    command_buffer_tail_confirmed = next;

    // Increment confirmed command ID
    auto id = command_id_confirmed; command_id_confirmed = id + 1;

#ifdef DEBUG
//    ESP_LOGI(TAG, "uart_transmit_confirm done tail=%d / tail_conf=%d", command_buffer_tail, command_buffer_tail_confirmed);
#endif
}

unsigned long int SerialPort::get_command_id_confirmed() const {
    return command_id_confirmed;
}

unsigned long int SerialPort::get_command_id_sent() const {
    return command_id_sent;
}

bool SerialPort::is_all_confirmed() const {
    return (command_buffer_tail_confirmed == command_buffer_head);
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
void SerialPort::set_printer_buffer_size(size_t size) {
    printer_buffer_size = size;
}