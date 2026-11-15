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
void gfx_draw_text_style(int x, int y, const char *s, int scale, int max_w, uint16_t c, unsigned style);
int gfx_text_w_style(const char *s, int scale, unsigned style);
uint16_t *gfx_fb(void);
int gfx_w(void);
int gfx_h(void);
size_t gfx_wrap_line(const char *s, int scale, int max_w, const char **next);
int gfx_draw_para(int x, int y, const char *s, int scale, int max_w, int step, int max_rows, uint16_t c);
/* u-feckless-0046 */
#include <stdbool.h>
#define GFX_POLY_MAX_PTS  (16)
#define GFX_TIME_TEXT_MAX (16)
void gfx_set_filter(int filter);
int gfx_filter(void);
esp_err_t gfx_blit_err(int y0, int y1);
void gfx_set_rotation(int quarter_turns);
void gfx_fill_poly(const int *xy, int n, uint16_t c);
void gfx_fill_star(int cx, int cy, int r, uint16_t c);
void gfx_draw_spinner(int cx, int cy, int r, uint32_t ms, uint16_t on, uint16_t off);
const char *gfx_time_text(char *out, size_t out_len, uint32_t sec, bool neg);
int gfx_time_text_w(const char *s, int scale);
void gfx_draw_time_text(int x, int y, const char *s, int scale, uint16_t c);
void gfx_draw_text_tail(int x, int y, const char *s, int scale, int max_w, uint16_t c);
void gfx_draw_text_clipped(int x, int y, int win_x, int win_w, const char *s, int scale, uint16_t c);
bool gfx_text_rtl(const char *s);
int gfx_para_rows(const char *s, int scale, int max_w);
