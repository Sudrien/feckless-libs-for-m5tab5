#pragma once
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
esp_err_t tab5io_init(void);
i2c_master_bus_handle_t tab5io_bus(void);
