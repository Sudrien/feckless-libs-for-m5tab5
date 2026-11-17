/* SPDX-License-Identifier: MIT
 * u-feckless-0062: stand-ins for ESP-IDF's UART, GPIO and I2C master
 * drivers. Each records what it was given; the UART has `per_uart_rx`
 * bytes to read, one byte value per read; an I2C device is a small heap
 * object, so one left behind is a leak the test's sanitizer reports. */
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"

extern int fake_core;                       /* fake_rtos.c: the caller's */

int per_installs, per_deletes, per_install_core = -1, per_delete_core = -1, per_install_flags = -1;
int per_uart_installed;                     /* bit per port */
int per_cfg_baud, per_cfg_flags = -1, per_cfgs, per_setpins, per_pin[6];
int per_reads, per_read_ticks[8], per_uart_rx, per_writes, per_write_max, per_written;
int per_wait_ticks = -1, per_flushes;
const void *per_read_dst;

bool uart_is_driver_installed(uart_port_t p) { return per_uart_installed >> p & 1; }
esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int qsize, QueueHandle_t *q, int flags)
{
    (void)rx; (void)tx; (void)qsize; (void)q;
    per_installs++;
    per_install_core = fake_core;
    per_install_flags = flags;
    per_uart_installed |= 1 << p;
    return ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t p)
{
    per_deletes++;
    per_delete_core = fake_core;
    per_uart_installed &= ~(1 << p);
    return ESP_OK;
}
esp_err_t uart_param_config(uart_port_t p, const uart_config_t *cfg)
{
    (void)p;
    per_cfgs++;
    per_cfg_baud = cfg->baud_rate;
    per_cfg_flags = (int)cfg->flags.allow_pd | (int)cfg->flags.backup_before_sleep << 1;
    return ESP_OK;
}
esp_err_t _uart_set_pin6(uart_port_t p, int tx, int rx, int rts, int cts, int dtr, int dsr)
{
    (void)p;
    per_setpins++;
    per_pin[0] = tx; per_pin[1] = rx; per_pin[2] = rts; per_pin[3] = cts; per_pin[4] = dtr; per_pin[5] = dsr;
    return ESP_OK;
}
int uart_read_bytes(uart_port_t p, void *buf, uint32_t len, TickType_t ticks)
{
    (void)p;
    if (per_reads < 8) per_read_ticks[per_reads] = (int)ticks;
    per_reads++;
    per_read_dst = buf;
    int n = per_uart_rx < (int)len ? per_uart_rx : (int)len;
    memset(buf, 0xA0 + per_reads, (size_t)n);
    per_uart_rx -= n;
    return n;
}
int uart_write_bytes(uart_port_t p, const void *src, size_t size)
{
    (void)p; (void)src;
    per_writes++;
    if ((int)size > per_write_max) per_write_max = (int)size;
    per_written += (int)size;
    return (int)size;
}
esp_err_t uart_wait_tx_done(uart_port_t p, TickType_t ticks) { (void)p; per_wait_ticks = (int)ticks; return ESP_OK; }
esp_err_t uart_flush_input(uart_port_t p) { (void)p; per_flushes++; return ESP_OK; }
esp_err_t uart_get_buffered_data_len(uart_port_t p, size_t *size) { (void)p; *size = (size_t)per_uart_rx; return ESP_OK; }

int per_gpio_cfgs, per_gpio_intr = -1, per_gpio_resets, per_gpio_sets, per_gpio_dirs;
uint64_t per_gpio_mask, per_gpio_reset_mask;

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    per_gpio_cfgs++;
    per_gpio_mask = cfg->pin_bit_mask;
    per_gpio_intr = (int)cfg->intr_type;
    return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode) { (void)pin; (void)mode; per_gpio_dirs++; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) { (void)pin; (void)level; per_gpio_sets++; return ESP_OK; }
int gpio_get_level(gpio_num_t pin) { (void)pin; return 1; }
esp_err_t gpio_reset_pin(gpio_num_t pin) { per_gpio_resets++; per_gpio_reset_mask |= 1ull << pin; return ESP_OK; }

typedef struct { uint16_t addr; } fake_dev_t;
int per_probes, per_probe_ms, per_adds, per_rms, per_xfers, per_xfer_ms, per_dev_live;
uint16_t per_probe_addr, per_xfer_addr;
const void *per_i2c_bus, *per_xfer_rd;
esp_err_t per_add_result = ESP_OK;

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int ms)
{
    per_probes++; per_i2c_bus = bus; per_probe_addr = addr; per_probe_ms = ms;
    return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *out)
{
    per_adds++; per_i2c_bus = bus;
    if (per_add_result != ESP_OK) return per_add_result;
    fake_dev_t *d = malloc(sizeof *d);
    d->addr = cfg->device_address;
    per_dev_live++;
    *out = d;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) { per_rms++; per_dev_live--; free(dev); return ESP_OK; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t n, int ms)
{
    (void)wr; (void)n;
    per_xfers++; per_xfer_addr = ((fake_dev_t *)dev)->addr; per_xfer_ms = ms;
    return ESP_OK;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *rd, size_t n, int ms)
{
    per_xfers++; per_xfer_addr = ((fake_dev_t *)dev)->addr; per_xfer_ms = ms; per_xfer_rd = rd;
    memset(rd, 0x5a, n);
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t wn, uint8_t *rd, size_t rn, int ms)
{
    (void)wr; (void)wn;
    return i2c_master_receive(dev, rd, rn, ms);
}
