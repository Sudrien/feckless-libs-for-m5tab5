/* SPDX-License-Identifier: MIT
 * Stand-ins for the board libraries: each records what it was called with. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "touch.h"

int calls;
char last_fn[32], last_str[700], log_tag[80];
int last_i[8];
uint32_t log_cfg;
int log_kind = -1;
static int panel_obj, bus_obj;
void *host_panel = &panel_obj, *host_bus = &bus_obj;
const char *wrap_at;

static void rec(const char *fn, const char *s, int n, ...)
{
    calls++;
    snprintf(last_fn, sizeof last_fn, "%s", fn);
    snprintf(last_str, sizeof last_str, "%s", s ? s : "");
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n && i < 8; i++) last_i[i] = va_arg(ap, int);
    va_end(ap);
}

void esp_log(esp_log_config_t c, const char *tag, const char *f, ...)
{
    va_list ap; va_start(ap, f);
    log_kind = 0; log_cfg = c.data; snprintf(log_tag, sizeof log_tag, "%s", tag);
    vsnprintf(last_str, sizeof last_str, f, ap); va_end(ap); calls++;
}
void esp_log_write(esp_log_level_t l, const char *tag, const char *f, ...)
{
    va_list ap; va_start(ap, f);
    log_kind = 1; log_cfg = (uint32_t)l; snprintf(log_tag, sizeof log_tag, "%s", tag);
    vsnprintf(last_str, sizeof last_str, f, ap); va_end(ap); calls++;
}
uint32_t esp_log_timestamp(void) { return 1234; }
void esp_rom_delay_us(uint32_t us) { rec("esp_rom_delay_us", NULL, 1, (int)us); }
int64_t esp_timer_get_time(void) { return 0x123456789abLL; }
void vTaskDelay(TickType_t t) { rec("vTaskDelay", NULL, 1, (int)t); }
esp_err_t tab5io_init(void) { rec("tab5io_init", NULL, 0); return ESP_OK; }
i2c_master_bus_handle_t tab5io_bus(void) { return host_bus; }
esp_err_t lcd_init(esp_lcd_panel_handle_t *out) { rec("lcd_init", NULL, 0); *out = host_panel; return ESP_OK; }
esp_err_t lcd_backlight_set(int p) { rec("lcd_backlight_set", NULL, 1, p); return ESP_OK; }
esp_err_t gfx_init(esp_lcd_panel_handle_t p, int w, int h) { rec("gfx_init", NULL, 3, p == host_panel, w, h); return ESP_OK; }
esp_err_t touch_init(i2c_master_bus_handle_t b, int w, int h) { rec("touch_init", NULL, 3, b == host_bus, w, h); return ESP_OK; }
bool touch_get(int *x, int *y) { *x = 77; *y = 88; return true; }
void gfx_blit(int a, int b) { rec("gfx_blit", NULL, 2, a, b); }
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c) { rec("gfx_fill_rect", NULL, 5, x, y, w, h, (int)c); }
void gfx_fill_circle(int x, int y, int r, uint16_t c) { rec("gfx_fill_circle", NULL, 4, x, y, r, (int)c); }
void gfx_draw_text(int x, int y, const char *s, int sc, int mw, uint16_t c) { rec("gfx_draw_text", s, 5, x, y, sc, mw, (int)c); }
int gfx_text_w(const char *s, int sc) { rec("gfx_text_w", s, 1, sc); return (int)strlen(s) * 6 * sc; }
size_t gfx_wrap_line(const char *s, int sc, int mw, const char **next)
{
    rec("gfx_wrap_line", s, 2, sc, mw);
    const char *sp = strchr(s, ' ');
    if (next) *next = sp ? sp + 1 : s + strlen(s);
    return sp ? (size_t)(sp - s) : strlen(s);
}
int gfx_draw_para(int x, int y, const char *s, int sc, int mw, int st, int mr, uint16_t c)
{
    rec("gfx_draw_para", s, 7, x, y, sc, mw, st, mr, (int)c);
    return 3;
}
char level_tag[80];
int level_set = -1;
esp_log_level_t esp_log_level_get(const char *tag) { snprintf(level_tag, sizeof level_tag, "%s", tag); return ESP_LOG_INFO; }
void esp_log_level_set(const char *tag, esp_log_level_t l) { snprintf(level_tag, sizeof level_tag, "%s", tag); level_set = (int)l; }
