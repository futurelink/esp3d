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
    volatile unsigned int command_id_confirmed;

    char command_buffer[COMMAND_BUFFER_SIZE][COMMAND_MAX_LENGTH];
    uint8_t command_buffer_head;
    volatile uint8_t command_buffer_tail;
    volatile uint8_t command_buffer_tail_confirmed;
    bool locked;

    unsigned int last_sent_time;

    TaskHandle_t task_send;
    TaskHandle_t task_receive;
    TaskHandle_t task_watchdog;

    uint8_t printer_buffer_size;
    void (*printer_response_parse_callback)(const char *resp);

    char str[UART_TMP_BUF_SIZE];
    char uart_rx_buffer[UART_TMP_BUF_SIZE];
    uint16_t str_pos;

    portMUX_TYPE uart_mux;

public:
    explicit SerialPort(int baud, gpio_num_t rxd_pin, gpio_num_t txd_pin, void (*callback)(const char *));

    esp_err_t init();
    unsigned long send(const char *command);

    [[noreturn]] static void receive_task(void *args);
    [[noreturn]] static void send_task(void *args);
    [[noreturn]] static void watchdog_task(void *args);

    void receive();

    bool transmit_from_buffer();
    void transmit_confirm();

    [[nodiscard]] unsigned long int get_command_id_confirmed() const;
    [[nodiscard]] unsigned long int get_command_id_sent() const;
    [[nodiscard]] bool is_all_confirmed() const;

    void lock(bool locked);
    [[nodiscard]] bool is_locked() const;

    void set_printer_buffer_size(size_t size);
};



#endif //ESP32_PRINT_UART_H
