#pragma once
#include "gfx.h"
esp_err_t lcd_init(esp_lcd_panel_handle_t *out);
esp_err_t lcd_backlight_set(int percent);
