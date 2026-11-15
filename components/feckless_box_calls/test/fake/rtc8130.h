#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tab5io.h"
bool rtc8130_init(i2c_master_bus_handle_t bus);
bool rtc8130_read(int64_t *epoch);
bool rtc8130_write(int64_t epoch);
bool rtc8130_write_forward(int64_t epoch);
