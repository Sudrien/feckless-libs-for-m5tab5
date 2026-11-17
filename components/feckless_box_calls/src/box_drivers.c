/* SPDX-License-Identifier: MIT
 *
 * A UART, I2C devices and GPIO for a boxed app (u-feckless-0062): the
 * ESP-IDF driver calls the ABI's core list has, for what an app plugs
 * into the Tab5 -- a GNSS receiver on a UART, a sensor on the board's bus.
 *
 * What an app may have is what the board's own drivers do not hold
 * (box_pins.h): a pin none of them names, an address none of them talks
 * to. Anything else is ESP_ERR_INVALID_ARG and the driver is not called.
 *
 * - UARTs 1 to 4. UART0 is the ROM's and the console's and is not given;
 *   nor is a port whose driver is already installed, which is the
 *   launcher's. No event queue: the queue would be the host's object, so
 *   an install that asks for one is ESP_ERR_NOT_SUPPORTED. The interrupt
 *   flags are the host's, 0. Buffers to UART_BUF_MAX.
 * - The driver is installed and deleted on the board's core, core 0, as
 *   the camera is started there (u-feckless-0060): its interrupt is then
 *   core 0's, and no window on the app's core holds it off.
 * - A read waits in slices of SLICE_MS and gives up when the app is
 *   stopping, as the queues do (box_rtos.c); it returns as soon as a slice
 *   brought bytes, which may be sooner than the driver would have. A write
 *   goes in WRITE_SLICE pieces for the same reason, and waiting for the
 *   transmitter is capped at WAIT_MAX_MS.
 * - I2C: the bus is the host's, by the number tab5io_bus() gave the app.
 *   A device is a number too (BOX_TOKEN_I2C + slot), I2C_DEVS of them,
 *   7-bit addresses, to 400 kHz. A transfer is to XFER_MAX bytes each way
 *   and waits at most WAIT_MAX_MS, whatever the app asked.
 * - GPIO: a pin's configuration, direction and level. Interrupts are not
 *   answered -- gpio_config()'s intr_type is taken as GPIO_INTR_DISABLE --
 *   since a handler is the app's function, and nothing here runs one.
 * - When the app ends (box_drivers_end()): its UART drivers are deleted,
 *   its I2C devices removed, and every pin it configured or routed is
 *   reset, so the next app and the launcher find the board as it was.
 */
#include <string.h>

#include "box_calls_priv.h"
#include "box_pins.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tab5io.h"

#include "feckless_umode_load.h"

#define BOARD_CORE    0
#define BOARD_STACK   (8 * 1024)
#define UART_FIRST    1                     /* 0 is the console's */
#define UART_LAST     4                     /* src: soc_caps.h SOC_UART_HP_NUM 5 */
#define UART_BUF_MAX  (64 * 1024)
#define SLICE_MS      100                   /* box_rtos.c's */
#define WRITE_SLICE   256                   /* 67 ms at 38400 baud */
#define WAIT_MAX_MS   2000                  /* under feckless_umode's STOP_WAIT_MS */
#define I2C_DEVS      4
#define I2C_HZ_MAX    400000
#define XFER_MAX      (16 * 1024)
#define BOX_TOKEN_I2C 0x49324300u           /* "I2C\0" + slot */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_uarts;                    /* bit per port this app installed */
static uint64_t s_pins;                     /* bit per pin this app configured */
static i2c_master_dev_handle_t s_dev[I2C_DEVS];

static bool stopping(void) { return feckless_umode_app_stopping(feckless_umode_app_loaded()); }

static bool port_ok(uint32_t p) { return p >= UART_FIRST && p <= UART_LAST; }

static bool port_mine(uint32_t p)
{
    return port_ok(p) && (s_uarts >> p & 1);
}

/* A port to configure or route: one of the app's, or one with no driver
 * yet. A port the launcher installed is the launcher's. */
static bool port_usable(uint32_t p)
{
    return port_ok(p) && (port_mine(p) || !uart_is_driver_installed((uart_port_t)p));
}

static void pin_taken(int pin)
{
    portENTER_CRITICAL(&s_mux);
    s_pins |= 1ull << pin;
    portEXIT_CRITICAL(&s_mux);
}

static TickType_t capped(uint32_t ticks)
{
    const TickType_t max = pdMS_TO_TICKS(WAIT_MAX_MS);
    return ticks > max ? max : ticks;
}

/* fn(arg) on the board's core: here if this is it, else on a task pinned
 * there while the caller waits. ESP_ERR_NO_MEM if the task cannot be made,
 * and fn is not called. */
typedef struct {
    esp_err_t (*fn)(const uint32_t *);
    const uint32_t *arg;
    esp_err_t e;
    SemaphoreHandle_t done;
} job_t;

static void job_task(void *arg)
{
    job_t *j = arg;
    j->e = j->fn(j->arg);
    xSemaphoreGive(j->done);                /* j is the waiter's: not touched again */
    vTaskDelete(NULL);
}

static esp_err_t on_board_core(esp_err_t (*fn)(const uint32_t *), const uint32_t *arg)
{
    if (xPortGetCoreID() == BOARD_CORE) return fn(arg);
    job_t j = { .fn = fn, .arg = arg, .e = ESP_FAIL, .done = xSemaphoreCreateBinary() };
    if (!j.done) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(job_task, "box_uart", BOARD_STACK, &j, uxTaskPriorityGet(NULL), NULL,
                                BOARD_CORE) != pdPASS) {
        vSemaphoreDelete(j.done);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(j.done, portMAX_DELAY);
    vSemaphoreDelete(j.done);
    return j.e;
}

/* ---- the UART ---------------------------------------------------------- */

static esp_err_t install_here(const uint32_t *a)
{
    return uart_driver_install((uart_port_t)a[0], (int)a[1], (int)a[2], 0, NULL, 0);
}

static esp_err_t delete_here(const uint32_t *a) { return uart_driver_delete((uart_port_t)a[0]); }

/* (port, rx_size, tx_size, queue_size, queue, intr_flags) */
H(c_uart_driver_install)
{
    (void)w;
    esp_err_t e;
    if (!port_ok(a[0]) || a[1] > UART_BUF_MAX || a[2] > UART_BUF_MAX) {
        e = ESP_ERR_INVALID_ARG;
    } else if (a[3] || a[4]) {
        e = ESP_ERR_NOT_SUPPORTED;          /* an event queue is the host's object */
    } else if (uart_is_driver_installed((uart_port_t)a[0])) {
        e = ESP_ERR_INVALID_STATE;          /* this app's already, or the launcher's */
    } else {
        e = on_board_core(install_here, a);
        if (e == ESP_OK) s_uarts |= 1u << a[0];
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_uart_driver_delete)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (port_mine(a[0])) {
        e = on_board_core(delete_here, a);
        s_uarts &= ~(1u << a[0]);
    }
    *ret = (uint32_t)e;
    return true;
}

/* (port, const uart_config_t *): a copy, its sleep flags cleared */
H(c_uart_param_config)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (port_usable(a[0]) && ok_ptr(w, a[1], sizeof(uart_config_t))) {
        uart_config_t cfg;
        memcpy(&cfg, (const void *)(uintptr_t)a[1], sizeof cfg);
        memset(&cfg.flags, 0, sizeof cfg.flags);
        e = uart_param_config((uart_port_t)a[0], &cfg);
    }
    *ret = (uint32_t)e;
    return true;
}

/* (port, tx, rx, rts, cts, dtr, dsr): each a free pin or UART_PIN_NO_CHANGE */
H(c__uart_set_pin6)
{
    (void)w;
    esp_err_t e = port_usable(a[0]) ? ESP_OK : ESP_ERR_INVALID_ARG;
    for (int i = 1; i <= 6 && e == ESP_OK; i++) {
        if ((int32_t)a[i] != UART_PIN_NO_CHANGE && !box_pin_free((int32_t)a[i])) e = ESP_ERR_INVALID_ARG;
    }
    if (e == ESP_OK) {
        for (int i = 1; i <= 6; i++) if ((int32_t)a[i] != UART_PIN_NO_CHANGE) pin_taken((int32_t)a[i]);
        e = _uart_set_pin6((uart_port_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4],
                           (int32_t)a[5], (int32_t)a[6]);
    }
    *ret = (uint32_t)e;
    return true;
}

/* (port, buf, len, ticks) -> bytes, or -1 */
H(c_uart_read_bytes)
{
    int n = -1;
    if (port_mine(a[0]) && a[2] <= UART_BUF_MAX && ok_ptr(w, a[1], a[2])) {
        const TickType_t slice = pdMS_TO_TICKS(SLICE_MS);
        TickType_t left = a[3];
        for (;;) {
            const TickType_t t = left < slice ? left : slice;
            n = uart_read_bytes((uart_port_t)a[0], (void *)(uintptr_t)a[1], a[2], t);
            if (n != 0 || left <= slice || stopping()) break;
            if (a[3] != portMAX_DELAY) left -= slice;
        }
    }
    *ret = (uint32_t)n;
    return true;
}

/* (port, src, size) -> size, or -1 */
H(c_uart_write_bytes)
{
    int n = -1;
    if (port_mine(a[0]) && a[2] <= UART_BUF_MAX && ok_ptr(w, a[1], a[2])) {
        const char *src = (const char *)(uintptr_t)a[1];
        uint32_t done = 0;
        n = 0;
        while (done < a[2] && !stopping()) {
            const uint32_t part = a[2] - done < WRITE_SLICE ? a[2] - done : WRITE_SLICE;
            const int r = uart_write_bytes((uart_port_t)a[0], src + done, part);
            if (r < 0) { n = -1; break; }
            done += (uint32_t)r;
            n = (int)done;
        }
    }
    *ret = (uint32_t)n;
    return true;
}

H(c_uart_wait_tx_done)
{
    (void)w;
    *ret = (uint32_t)(port_mine(a[0]) ? uart_wait_tx_done((uart_port_t)a[0], capped(a[1])) : ESP_ERR_INVALID_ARG);
    return true;
}

H(c_uart_flush_input)
{
    (void)w;
    *ret = (uint32_t)(port_mine(a[0]) ? uart_flush_input((uart_port_t)a[0]) : ESP_ERR_INVALID_ARG);
    return true;
}

/* (port, size_t *) */
H(c_uart_get_buffered_data_len)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (port_mine(a[0]) && ok_ptr(w, a[1], sizeof(uint32_t))) {
        size_t n = 0;
        e = uart_get_buffered_data_len((uart_port_t)a[0], &n);
        *(uint32_t *)(uintptr_t)a[1] = (uint32_t)n;
    }
    *ret = (uint32_t)e;
    return true;
}

/* ---- I2C devices on the board's bus ------------------------------------ */

static int xfer_ms(uint32_t ms)
{
    return (int32_t)ms < 0 || ms > WAIT_MAX_MS ? WAIT_MAX_MS : (int)ms;
}

static i2c_master_dev_handle_t dev_of(uint32_t tok)
{
    const uint32_t i = tok - BOX_TOKEN_I2C;
    return i < I2C_DEVS ? s_dev[i] : NULL;
}

/* (bus, address, timeout_ms) */
H(c_i2c_master_probe)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (a[0] == BOX_TOKEN_BUS && box_i2c_addr_free(a[1])) {
        e = i2c_master_probe(tab5io_bus(), (uint16_t)a[1], xfer_ms(a[2]));
    }
    *ret = (uint32_t)e;
    return true;
}

/* (bus, const i2c_device_config_t *, i2c_master_dev_handle_t *) */
H(c_i2c_master_bus_add_device)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (a[0] == BOX_TOKEN_BUS && ok_ptr(w, a[1], sizeof(i2c_device_config_t)) && ok_ptr(w, a[2], sizeof(uint32_t))) {
        i2c_device_config_t cfg;
        memcpy(&cfg, (const void *)(uintptr_t)a[1], sizeof cfg);
        *(uint32_t *)(uintptr_t)a[2] = 0;
        if (cfg.dev_addr_length == I2C_ADDR_BIT_LEN_7 && box_i2c_addr_free(cfg.device_address) &&
            cfg.scl_speed_hz && cfg.scl_speed_hz <= I2C_HZ_MAX) {
            int slot = 0;
            while (slot < I2C_DEVS && s_dev[slot]) slot++;
            e = ESP_ERR_NO_MEM;
            if (slot < I2C_DEVS) {
                e = i2c_master_bus_add_device(tab5io_bus(), &cfg, &s_dev[slot]);
                if (e == ESP_OK) *(uint32_t *)(uintptr_t)a[2] = BOX_TOKEN_I2C + (uint32_t)slot;
                else s_dev[slot] = NULL;
            }
        }
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_i2c_master_bus_rm_device)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (dev_of(a[0])) {
        e = i2c_master_bus_rm_device(dev_of(a[0]));
        if (e == ESP_OK) s_dev[a[0] - BOX_TOKEN_I2C] = NULL;
    }
    *ret = (uint32_t)e;
    return true;
}

/* (dev, write, write_size, timeout_ms) */
H(c_i2c_master_transmit)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (dev_of(a[0]) && a[2] && a[2] <= XFER_MAX && ok_ptr(w, a[1], a[2])) {
        e = i2c_master_transmit(dev_of(a[0]), (const uint8_t *)(uintptr_t)a[1], a[2], xfer_ms(a[3]));
    }
    *ret = (uint32_t)e;
    return true;
}

/* (dev, read, read_size, timeout_ms) */
H(c_i2c_master_receive)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (dev_of(a[0]) && a[2] && a[2] <= XFER_MAX && ok_ptr(w, a[1], a[2])) {
        e = i2c_master_receive(dev_of(a[0]), (uint8_t *)(uintptr_t)a[1], a[2], xfer_ms(a[3]));
    }
    *ret = (uint32_t)e;
    return true;
}

/* (dev, write, write_size, read, read_size, timeout_ms) */
H(c_i2c_master_transmit_receive)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (dev_of(a[0]) && a[2] && a[2] <= XFER_MAX && ok_ptr(w, a[1], a[2]) &&
        a[4] && a[4] <= XFER_MAX && ok_ptr(w, a[3], a[4])) {
        e = i2c_master_transmit_receive(dev_of(a[0]), (const uint8_t *)(uintptr_t)a[1], a[2],
                                        (uint8_t *)(uintptr_t)a[3], a[4], xfer_ms(a[5]));
    }
    *ret = (uint32_t)e;
    return true;
}

/* ---- GPIO -------------------------------------------------------------- */

/* (const gpio_config_t *): every pin in the mask free; no interrupt */
H(c_gpio_config)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (ok_ptr(w, a[0], sizeof(gpio_config_t))) {
        gpio_config_t cfg;
        memcpy(&cfg, (const void *)(uintptr_t)a[0], sizeof cfg);
        bool ok = cfg.pin_bit_mask != 0;
        for (int pin = 0; pin < 64 && ok; pin++) {
            if ((cfg.pin_bit_mask >> pin & 1) && !box_pin_free(pin)) ok = false;
        }
        if (ok) {
            cfg.intr_type = GPIO_INTR_DISABLE;
            portENTER_CRITICAL(&s_mux);
            s_pins |= cfg.pin_bit_mask;
            portEXIT_CRITICAL(&s_mux);
            e = gpio_config(&cfg);
        }
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_gpio_set_direction)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (box_pin_free((int32_t)a[0])) {
        pin_taken((int32_t)a[0]);
        e = gpio_set_direction((gpio_num_t)a[0], (gpio_mode_t)a[1]);
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_gpio_set_level)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (box_pin_free((int32_t)a[0])) {
        pin_taken((int32_t)a[0]);
        e = gpio_set_level((gpio_num_t)a[0], a[1]);
    }
    *ret = (uint32_t)e;
    return true;
}

/* a pin the board holds reads 0: its level is not the app's to know */
H(c_gpio_get_level)
{
    (void)w;
    *ret = box_pin_free((int32_t)a[0]) ? (uint32_t)gpio_get_level((gpio_num_t)a[0]) : 0;
    return true;
}

H(c_gpio_reset_pin)
{
    (void)w;
    *ret = (uint32_t)(box_pin_free((int32_t)a[0]) ? gpio_reset_pin((gpio_num_t)a[0]) : ESP_ERR_INVALID_ARG);
    return true;
}

const feckless_umode_call_t box_drivers_calls[] = {
    /* Unlocked: a read or a transfer waits, and the app's other threads'
     * host calls need not wait behind it. The drivers lock themselves. */
    FN(uart_driver_install), FN(uart_driver_delete), FN(uart_param_config), FN(_uart_set_pin6),
    FNU(uart_read_bytes), FNU(uart_write_bytes), FNU(uart_wait_tx_done),
    FNU(uart_flush_input), FNU(uart_get_buffered_data_len),
    FNU(i2c_master_probe), FN(i2c_master_bus_add_device), FN(i2c_master_bus_rm_device),
    FNU(i2c_master_transmit), FNU(i2c_master_receive), FNU(i2c_master_transmit_receive),
    FN(gpio_config), FN(gpio_set_direction), FNU(gpio_set_level), FNU(gpio_get_level),
    FN(gpio_reset_pin),
};
const uint32_t box_drivers_calls_n = sizeof box_drivers_calls / sizeof box_drivers_calls[0];

void box_drivers_end(void)
{
    for (uint32_t p = UART_FIRST; p <= UART_LAST; p++) {
        if (s_uarts >> p & 1) on_board_core(delete_here, &p);
    }
    s_uarts = 0;
    for (int i = 0; i < I2C_DEVS; i++) {
        if (s_dev[i]) i2c_master_bus_rm_device(s_dev[i]);
        s_dev[i] = NULL;
    }
    for (int pin = 0; pin < BOX_PIN_COUNT; pin++) {
        if (s_pins >> pin & 1) gpio_reset_pin((gpio_num_t)pin);
    }
    s_pins = 0;
}
