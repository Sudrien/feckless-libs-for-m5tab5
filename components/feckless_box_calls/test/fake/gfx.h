#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *esp_lcd_panel_handle_t;
esp_err_t gfx_init(esp_lcd_panel_handle_t panel, int w, int h);
void gfx_blit(int y0, int y1);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c);
void gfx_fill_circle(int cx, int cy, int r, uint16_t c);
void gfx_draw_text(int x, int y, const char *s, int scale, int max_w, uint16_t c);
int gfx_text_w(const char *s, int scale);
size_t gfx_wrap_line(const char *s, int scale, int max_w, const char **next);
int gfx_draw_para(int x, int y, const char *s, int scale, int max_w, int step, int max_rows, uint16_t c);
