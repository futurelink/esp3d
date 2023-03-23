#include "uart.h"
#include "state.h"
#include "server.h"

#define COMMAND_BUFFER_SIZE     10
#define COMMAND_PING            "M105\n"

#define UART                    UART_NUM_2
#define UART_TASK_DELAY         10      // ms
#define UART_TASK_STACK_SIZE    4096    // bytes
#define UART_TMP_BUF_SIZE       256     // bytes

static const char TAG[] = "stm32-print-uart";

uart_state_t uart_state;
extern printer_state_t printer_state;

char str[UART_TMP_BUF_SIZE];
uint16_t str_pos;

void uart_transmit_from_buffer();
void uart_transmit_confirm();
void uart_receive();

/**
 * Adds a command string to buffer to be sent via UART.
 * @param command
 * @return enqueued command number if added successfully or 0 if buffer was full
 */
unsigned long uart_send(const char *command) {
    //ESP_LOGI(TAG, "uart_send start");
    uint8_t next_head = uart_state.command_buffer_head + 1;
    if (next_head == COMMAND_BUFFER_SIZE) next_head = 0;

    // Cannot add - buffer is full or full of unconfirmed messages
    if ((next_head == uart_state.command_buffer_tail_confirmed) ||
        (next_head == uart_state.command_buffer_tail)) return 0;

    // Allocate memory in a buffer and copy command, reserve 1 character in case
    // when there's no \n in the end, and we need to add it.
    uint8_t len = strlen(command) + 2;
    uart_state.command_buffer[uart_state.command_buffer_head] = (char *) malloc(len);
    char *ptr = uart_state.command_buffer[uart_state.command_buffer_head];
    strcpy(ptr, command);
    if (ptr[len-3] != '\n') { // add newline character if it's not there
        ptr[len-2] = '\n';
        ptr[len-1] = 0;
    }
    //ESP_LOGI(TAG, "Got string to transmit %s", ptr);

    uart_state.command_buffer_head = next_head;

    //ESP_LOGI(TAG, "uart_send done");

    return ++uart_state.command_id_cnt;
}

esp_err_t uart_init() {
    uart_config_t uart_config = {
            .baud_rate = uart_state.baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT
    };
    esp_err_t err_uart = uart_param_config(UART, &uart_config);
    if (err_uart == ESP_OK) err_uart = uart_set_pin(UART, uart_state.rxd_pin, uart_state.txd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err_uart == ESP_OK) err_uart = uart_driver_install(UART, UART_TMP_BUF_SIZE, UART_TMP_BUF_SIZE, 0, nullptr, 0);
    if (err_uart != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed with error 0x%x", err_uart);
        return err_uart;
    }

    ESP_LOGI(TAG, "UART initialized");

    // Create command buffer
    uart_state.command_id_cnt = 0;
    uart_state.command_id_sent = 0;
    uart_state.command_id_confirmed = 0;
    uart_state.command_buffer = (char **) malloc(COMMAND_BUFFER_SIZE * sizeof(char *));
    uart_state.command_buffer_head = 0;
    uart_state.command_buffer_tail = 0;
    uart_state.command_buffer_tail_confirmed = 0;

    // Start UART tasks
    xTaskCreate(uart_send_task, "uart_send_task", UART_TASK_STACK_SIZE, nullptr, 10, nullptr);
    xTaskCreate(uart_receive_task, "uart_receive_task", UART_TASK_STACK_SIZE, nullptr, 10, nullptr);
    xTaskCreate(uart_task_ping, "uart_task_ping", UART_TASK_STACK_SIZE, nullptr, 10, nullptr);

    return ESP_OK;
}

/**
 * Task function. Printer ping cycle. Executed every second if there's nothing in buffer to send.
 * @param args
 */
void uart_task_ping([[gnu::unused]] void *args) {
    while (true) {
        // If buffer is empty then we need to add a command to ping printer.
        // Here we preliminarily update printer status and will update it
        // once again if something comes from serial port
        if (uart_state.command_buffer_tail_confirmed == uart_state.command_buffer_head) {
            printer_state.status = PRINTER_IDLE;
            uart_state.ping_sent = true;
            uart_send(COMMAND_PING);
        } else {
            // If ping request was sent but left unanswered in task period (1 sec)
            // then we put printer in unknown mode
            if (uart_state.ping_sent) {
                printer_state.status = PRINTER_UNKNOWN;
            } else {
                printer_state.status = PRINTER_WORKING;
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait 1 sec
    }
}

/**
 * Task function. Main printer communication cycle.
 * @param args
 */
void uart_receive_task([[gnu::unused]] void *args) {
    str_pos = 0;
    while (true) {
        uart_receive();
        vTaskDelay(UART_TASK_DELAY / portTICK_PERIOD_MS);  // Wait 10 msec
    }
}

/**
 * Task function. Main printer communication cycle.
 * @param args
 */
void uart_send_task([[gnu::unused]] void *args) {
    while (true) {
        uart_transmit_from_buffer();
        vTaskDelay(UART_TASK_DELAY / portTICK_PERIOD_MS);  // Wait 10 msec
    }
}

void parse_temperature_report(char *report) {
    ESP_LOGI(TAG, "Got temperature report %s", report);
    float tc, bc;
    int x, y;
    sscanf(report, "T:%f /%f B:%f /%f @:%d B@:%d",
           &printer_state.temp_hot_end, &tc, &printer_state.temp_bed, &bc,
           &x, &y);
}

void uart_receive() {
    char uart_tmp_buffer[UART_TMP_BUF_SIZE];
    int len = uart_read_bytes(UART, uart_tmp_buffer, (UART_TMP_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
    if (len > 0) {
        //ESP_LOGI(TAG, "uart_receive start");
        uint16_t cnt = 0;
        do {
            if (uart_tmp_buffer[cnt] == '\n') {
                str[str_pos] = 0;
                str_pos = 0;
                ESP_LOGI(TAG, "Got response from printer %s", str);
                uart_state.ping_sent = false; // When something comes then we reset ping flag anyway

                // ok - confirmed message from printer. Each sent command MUST be answered with
                // 'ok'. Even if it was unknown command, Marlin answers 'ok' with preceding 'echo'.
                if (strncmp(str, "ok", 2) == 0) {
                    uart_transmit_confirm();
                    uart_state.command_id_confirmed++;
                    ESP_LOGI(TAG, "Confirmed #%lu", uart_state.command_id_confirmed);

                    // Temperature report may be after 'ok', so we need to get it here
                    if (strncmp(&str[3], "T:", 2) == 0) {
                       parse_temperature_report(&str[3]);
                    }
                }
                else if (strncmp(str, "echo:busy: ", 11) == 0) {
                    printer_state.status = PRINTER_WORKING;
                    ESP_LOGI(TAG, "Busy");
                }
                else if (strncmp(str, "T:", 2) == 0) {
                    parse_temperature_report(&str[3]);
                }
                else if (strncmp(str, "X:", 2) == 0) {
                    ESP_LOGI(TAG, "Got position report %s", str);
                }
                else if (strncmp(str, "measured", 8) == 0) {
                    ESP_LOGI(TAG, "Got probe report %s", str);
                }
            } else if (str_pos < UART_TMP_BUF_SIZE) {
                str[str_pos++] = uart_tmp_buffer[cnt];
            }
        } while (++cnt < len);

        //ESP_LOGI(TAG, "uart_receive done");
    }
}

/**
 * Internal function.
 * Sends command from buffer's tail, but does not increment buffer confirmed pointer.
 * It should be incremented a bit later when confirmation comes from printer.
 */
void uart_transmit_from_buffer() {
    uint8_t tail = uart_state.command_buffer_tail;
    if (tail == uart_state.command_buffer_head) return;             // Empty buffer
    if (tail != uart_state.command_buffer_tail_confirmed) {
        ESP_LOGI(TAG, "There's unconfirmed message %d / %d", tail, uart_state.command_buffer_tail_confirmed);
        return;   // Don't send anything until we haven't got answer
    }

    //ESP_LOGI(TAG, "uart_transmit_from_buffer start ");

    uart_write_bytes(UART, uart_state.command_buffer[tail], strlen(uart_state.command_buffer[tail]));
    uart_state.command_id_sent++;
    ESP_LOGI(TAG, "UART transmitted #%lu: %s ", uart_state.command_id_sent, uart_state.command_buffer[tail]);

    // Increment transmit pointer
    if (++tail == COMMAND_BUFFER_SIZE) tail = 0;
    uart_state.command_buffer_tail = tail;

    //ESP_LOGI(TAG, "uart_transmit_from_buffer done");
}

/**
 * Internal function. Shifts confirmed message pointer in UART ring buffer.
 */
void uart_transmit_confirm() {
    //ESP_LOGI(TAG, "uart_transmit_confirm start");
    free(uart_state.command_buffer[uart_state.command_buffer_tail_confirmed]);  // Free sent command buffer
    uint8_t next = uart_state.command_buffer_tail_confirmed + 1;                // Increment confirmed number
    if (next == COMMAND_BUFFER_SIZE) next = 0;
    uart_state.command_buffer_tail_confirmed = next;
    //ESP_LOGI(TAG, "uart_transmit_confirm done tail=%d / tail_conf=%d",
    //         uart_state.command_buffer_tail, uart_state.command_buffer_tail_confirmed);
}
