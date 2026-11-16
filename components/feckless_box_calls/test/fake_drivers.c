/* SPDX-License-Identifier: MIT
 * u-feckless-0046: stand-ins for the playback half of audio_out, the
 * battery, the clock, polyrsp, and the rest of gfx; each records what it
 * was called with. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_out.h"
#include "battery.h"
#include "gfx.h"
#include "lcd.h"
#include "polyrsp.h"
#include "rtc8130.h"
#include "touch.h"
#include "usbhost.h"

extern void *host_bus;
int d_calls;
size_t d_len;
const void *d_ptr;
int d_idle = -1, d_mute = -1, d_filter, d_rot, d_poly_n, d_poly_xy[32], d_swallow, d_rsp_live;
uint32_t d_rate, d_duty;
uint8_t d_ch;
int64_t d_epoch;
char d_str[256], d_why[64];
int d_args[8];

esp_err_t audio_out_write(const void *data, size_t len) { d_calls++; d_ptr = data; d_len = len; return ESP_OK; }
esp_err_t audio_out_set_format(uint32_t rate, uint8_t ch) { d_rate = rate; d_ch = ch; return rate ? ESP_OK : ESP_ERR_INVALID_ARG; }
bool audio_out_rate_supported(uint32_t rate) { return rate == 44100 || rate == 48000; }
uint32_t audio_out_rate(void) { return 44100; }
void audio_out_set_mute(bool m) { d_mute = m; }
bool audio_out_muted(void) { return d_mute == 1; }
void audio_out_set_idle(bool i) { d_idle = i; }
audio_out_route_t audio_out_route(void) { return AUDIO_OUT_HEADPHONES; }
const char *audio_out_route_name(void) { return "headphones"; }

esp_err_t battery_init(i2c_master_bus_handle_t bus) { return bus == host_bus ? ESP_OK : ESP_FAIL; }
esp_err_t battery_start(void) { return ESP_OK; }
void battery_trace_arm(const char *why) { strncpy(d_why, why, sizeof d_why - 1); }
int battery_pct(void) { return 87; }
bool battery_charging(void) { return true; }
int battery_mv(void) { return 7712; }
bool battery_external(void) { return false; }

bool rtc8130_init(i2c_master_bus_handle_t bus) { return bus == host_bus; }
bool rtc8130_read(int64_t *e) { *e = 0x123456789LL; return true; }
bool rtc8130_write(int64_t e) { d_epoch = e; return true; }
bool rtc8130_write_forward(int64_t e) { d_epoch = -e; return true; }

struct polyrsp { uint32_t in, out, max; };
polyrsp_t *polyrsp_open(uint32_t in, uint32_t out, uint32_t max)
{
    if (in == out) return NULL;
    polyrsp_t *r = malloc(sizeof *r);
    r->in = in; r->out = out; r->max = max;
    d_rsp_live++;
    return r;
}
void polyrsp_close(polyrsp_t *r) { free(r); d_rsp_live--; }
void polyrsp_reset(polyrsp_t *r) { (void)r; d_calls++; }
uint32_t polyrsp_max_out(const polyrsp_t *r) { return (uint32_t)(((uint64_t)r->max * r->out + r->in - 1) / r->in) + 1; }
/* Writes every frame it may: the exact share rounded up, and one more. */
uint32_t polyrsp_process(polyrsp_t *r, const int16_t *in, uint32_t n, int16_t *out)
{
    const uint32_t m = (uint32_t)(((uint64_t)n * r->out + r->in - 1) / r->in) + 1;
    for (uint32_t i = 0; i < m * 2; i++) out[i] = in[0];
    return m;
}

void gfx_set_filter(int f) { d_filter = f; }
int gfx_filter(void) { return d_filter; }
esp_err_t gfx_blit_err(int y0, int y1) { d_args[0] = y0; d_args[1] = y1; return ESP_OK; }
void gfx_set_rotation(int q) { d_rot = q; }
int gfx_rotation(void) { return d_rot; }    /* u-feckless-0059 */
void gfx_px(int x, int y, uint16_t c) { d_args[0] = x; d_args[1] = y; d_args[2] = c; }
void gfx_fill_poly(const int *xy, int n, uint16_t c) { (void)c; d_poly_n = n; memcpy(d_poly_xy, xy, (size_t)n * 2 * sizeof(int)); }
void gfx_fill_star(int cx, int cy, int r, uint16_t c) { d_args[0] = cx; d_args[1] = cy; d_args[2] = r; d_args[3] = c; }
void gfx_draw_spinner(int cx, int cy, int r, uint32_t ms, uint16_t on, uint16_t off)
{
    d_args[0] = cx; d_args[1] = cy; d_args[2] = r; d_args[3] = (int)ms; d_args[4] = on; d_args[5] = off;
}
const char *gfx_time_text(char *out, size_t len, uint32_t sec, bool neg)
{
    char t[32];
    snprintf(t, sizeof t, "%s%u:%02u", neg ? "-" : "", (unsigned)(sec / 60), (unsigned)(sec % 60));
    strncpy(out, t, len - 1);
    out[len - 1] = '\0';
    return out;
}
int gfx_time_text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
void gfx_draw_time_text(int x, int y, const char *s, int scale, uint16_t c)
{
    d_args[0] = x; d_args[1] = y; d_args[2] = scale; d_args[3] = c; strncpy(d_str, s, sizeof d_str - 1);
}
void gfx_draw_text_tail(int x, int y, const char *s, int scale, int mw, uint16_t c)
{
    d_args[0] = x; d_args[1] = y; d_args[2] = scale; d_args[3] = mw; d_args[4] = c; strncpy(d_str, s, sizeof d_str - 1);
}
void gfx_draw_text_clipped(int x, int y, int wx, int ww, const char *s, int scale, uint16_t c)
{
    d_args[0] = x; d_args[1] = y; d_args[2] = wx; d_args[3] = ww; d_args[4] = scale; d_args[5] = c;
    strncpy(d_str, s, sizeof d_str - 1);
}
bool gfx_text_rtl(const char *s) { return (unsigned char)s[0] == 0xd8; }
int gfx_para_rows(const char *s, int scale, int mw) { (void)scale; (void)mw; return (int)strlen(s) / 10 + 1; }
esp_err_t lcd_backlight_set_counts(uint32_t duty) { d_duty = duty; return ESP_OK; }
void touch_swallow(void) { d_swallow++; }
bool usbhost_running(void) { return true; }
bool usbhost_vbus_on(void) { return false; }

/* u-feckless-0048 */
#include "esp_random.h"
uint32_t esp_random(void) { return 0xc0ffee11u; }
void esp_fill_random(void *buf, size_t len) { memset(buf, 0x3c, len); }
