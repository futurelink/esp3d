#ifndef ESP32_PRINT_UART_H
#define ESP32_PRINT_UART_H

#include <cstring>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>

#define COMMAND_BUFFER_SIZE     32

#define UART                    UART_NUM_2
#define UART_TASK_PRIORITY      tskIDLE_PRIORITY
#define UART_TASK_STACK_SIZE    4096    // bytes
#define UART_TMP_BUF_SIZE       256     // bytes

class SerialPort {
private:
    int baud;
    gpio_num_t txd_pin;
    gpio_num_t rxd_pin;

    unsigned long command_id_cnt;
    unsigned long command_id_sent;
    unsigned long command_id_confirmed;

    char command_buffer[COMMAND_BUFFER_SIZE][64];
    uint8_t command_buffer_head;
    volatile uint8_t command_buffer_tail;
    volatile uint8_t command_buffer_tail_confirmed;
    bool locked;

    TaskHandle_t task_send;
    TaskHandle_t task_receive;

    uint8_t printer_buffer_size;
    void (*printer_response_parse_callback)(const char *resp);

    char str[UART_TMP_BUF_SIZE];
    uint16_t str_pos;

public:
    explicit SerialPort(int baud, gpio_num_t rxd_pin, gpio_num_t txd_pin, void (*callback)(const char *));

    esp_err_t init();
    unsigned long send(const char *command);

    [[noreturn]] static void receive_task(void *args);
    [[noreturn]] static void send_task(void *args);

    void receive();

    bool transmit_from_buffer();
    void transmit_confirm();

    [[nodiscard]] unsigned long int get_command_id_confirmed() const;
    [[nodiscard]] bool is_all_confirmed() const;

    void lock(bool locked);
    [[nodiscard]] bool is_locked() const;
};



#endif //ESP32_PRINT_UART_H
