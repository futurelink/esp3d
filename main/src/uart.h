#ifndef ESP32_PRINT_UART_H
#define ESP32_PRINT_UART_H

#include <cstring>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>

typedef struct {
    int baud;
    gpio_num_t txd_pin;
    gpio_num_t rxd_pin;
    unsigned long command_id_cnt;
    unsigned long command_id_sent;
    unsigned long command_id_confirmed;
    char **command_buffer;
    uint8_t command_buffer_head;
    volatile uint8_t command_buffer_tail;
    volatile uint8_t command_buffer_tail_confirmed;
    bool ping_sent;
} uart_state_t;

esp_err_t uart_init();
unsigned long uart_send(const char *command);

[[noreturn]] void uart_receive_task(void *args);
[[noreturn]] void uart_send_task(void *args);
[[noreturn]] void uart_task_ping(void *args);


#endif //ESP32_PRINT_UART_H
