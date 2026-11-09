#pragma once
#include <stdbool.h>
#include "tab5io.h"
esp_err_t touch_init(i2c_master_bus_handle_t bus, int panel_w, int panel_h);
bool touch_get(int *x, int *y);
bool touch_present(void);
void touch_set_rotation(int quarter_turns);
