#pragma once
/* u-feckless-0062: the part of ESP-IDF's I2C master driver box_drivers.c
 * uses; the handles are tab5io.h's */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "tab5io.h"
typedef enum { I2C_ADDR_BIT_LEN_7 = 0, I2C_ADDR_BIT_LEN_10 = 1 } i2c_addr_bit_len_t;
typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
    struct { uint32_t disable_ack_check : 1; } flags;
} i2c_device_config_t;
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *out);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t n, int ms);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *rd, size_t n, int ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t wn, uint8_t *rd, size_t rn, int ms);
