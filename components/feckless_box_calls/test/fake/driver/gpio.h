#pragma once
/* u-feckless-0062: the part of ESP-IDF's GPIO driver box_drivers.c uses */
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef int gpio_mode_t;
typedef enum { GPIO_INTR_DISABLE = 0, GPIO_INTR_POSEDGE = 1 } gpio_int_type_t;
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    int pull_up_en, pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_reset_pin(gpio_num_t pin);
