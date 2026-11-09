#pragma once
#include "esp_err.h"
#include "tab5io.h"
esp_err_t usbhost_init(i2c_master_dev_handle_t exp2);
void usbhost_start(void);
