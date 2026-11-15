#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "tab5io.h"
esp_err_t battery_init(i2c_master_bus_handle_t bus);
esp_err_t battery_start(void);
void battery_trace_arm(const char *why);
int battery_pct(void);
bool battery_charging(void);
int battery_mv(void);
bool battery_external(void);
