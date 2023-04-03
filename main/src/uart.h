/*
  uart.h - UART specific routines
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

#ifndef ESP32_PRINT_UART_H
#define ESP32_PRINT_UART_H

#include <cstring>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>

#define COMMAND_BUFFER_SIZE     32
#define COMMAND_MAX_LENGTH      64

#define UART                    UART_NUM_2
#define UART_TASK_PRIORITY      tskIDLE_PRIORITY
#define UART_TASK_STACK_SIZE    4096    // bytes
#define UART_TMP_BUF_SIZE       512     // bytes

class SerialPort {
private:
    int baud;
    gpio_num_t txd_pin;
    gpio_num_t rxd_pin;

    volatile unsigned int command_id_cnt;
    volatile unsigned int command_id_sent;

    char command_buffer[COMMAND_BUFFER_SIZE][COMMAND_MAX_LENGTH]{};
    uint8_t command_buffer_head;
    volatile uint8_t command_buffer_tail;
    bool locked;

    TaskHandle_t task_rx_tx;

    void (*printer_command_sent_callback)();
    bool (*printer_response_parse_callback)(const char *resp);
    bool (*printer_response_timeout_callback)();
    void (*printer_on_timeout_callback)();

    char *rx_buffer;
    char str[UART_TMP_BUF_SIZE]{};
    uint16_t str_pos;

    bool receive();
    bool transmit();

public:
    explicit        SerialPort(int baud, gpio_num_t rxd_pin, gpio_num_t txd_pin);
                    ~SerialPort();

    esp_err_t       init();
    unsigned long   send(const char *command);

    void set_sent_callback(void (*callback)());
    void set_response_callback(bool (*callback)(const char *));
    void set_timeout_callback(bool (*resp_timeout_callback)(), void (*on_timeout_callback)());

    [[noreturn]] static void rx_tx_task(void *args);

    [[nodiscard]] unsigned long int get_command_id_sent() const;

    void lock(bool locked);
    [[nodiscard]] bool is_locked() const;
    [[nodiscard]] int get_buffer_head() const;
    [[nodiscard]] int get_buffer_tail() const;
};

#endif //ESP32_PRINT_UART_H
