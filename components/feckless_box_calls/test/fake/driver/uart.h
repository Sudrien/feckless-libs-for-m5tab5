#pragma once
/* u-feckless-0062: the part of ESP-IDF's UART driver box_drivers.c uses,
 * the shapes as IDF 6.1's. fake_periph.c records the calls. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
typedef int uart_port_t;
#define UART_PIN_NO_CHANGE (-1)
typedef struct {
    int baud_rate;
    int data_bits, parity, stop_bits, flow_ctrl;
    uint8_t rx_flow_ctrl_thresh;
    int source_clk;
    struct { uint32_t allow_pd : 1; uint32_t backup_before_sleep : 1; } flags;
} uart_config_t;
bool uart_is_driver_installed(uart_port_t p);
esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int qsize, QueueHandle_t *q, int flags);
esp_err_t uart_driver_delete(uart_port_t p);
esp_err_t uart_param_config(uart_port_t p, const uart_config_t *cfg);
esp_err_t _uart_set_pin6(uart_port_t p, int tx, int rx, int rts, int cts, int dtr, int dsr);
int uart_read_bytes(uart_port_t p, void *buf, uint32_t len, TickType_t ticks);
int uart_write_bytes(uart_port_t p, const void *src, size_t size);
esp_err_t uart_wait_tx_done(uart_port_t p, TickType_t ticks);
esp_err_t uart_flush_input(uart_port_t p);
esp_err_t uart_get_buffered_data_len(uart_port_t p, size_t *size);
