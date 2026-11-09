/* SPDX-License-Identifier: MIT
 * Stand-ins for audio_out, uac, usbhost, the expanders, touch's extras and
 * Ark12's tables: each records what it was called with. */
#include <string.h>
#include "ark12.h"
#include "audio_out.h"
#include "touch.h"
#include "uac.h"
#include "usbhost.h"

int a_calls, a_last[4], a_begun, a_ended, a_streaming, a_mic_open, a_rotation;
const void *a_ptr;
static int exp1_obj, exp2_obj;
void *host_exp1 = &exp1_obj, *host_exp2 = &exp2_obj;
extern void *host_bus;

const ark12_run_t ark12_runs[2] = { { 32, 95, 0 }, { 160, 96, 95 } };
const uint32_t ark12_flags[3] = { 1, 2, 3 };
const uint16_t ark12_ckpt[5] = { 0 };
const uint32_t ark12_litbase[6] = { 0 };
const uint8_t ark12_lit[ARK12_LIT_BYTES] = { 9 };
const uint8_t ark12_tiles[ARK12_TILES * 3] = { 7 };

i2c_master_dev_handle_t tab5io_exp1(void) { return host_exp1; }
i2c_master_dev_handle_t tab5io_exp2(void) { return host_exp2; }
bool touch_present(void) { return true; }
void touch_set_rotation(int q) { a_rotation = q; }

esp_err_t audio_out_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t exp1, uint32_t rate)
{
    a_calls++;
    a_last[0] = bus == host_bus && exp1 == host_exp1;
    a_last[1] = (int)rate;
    return ESP_OK;
}
esp_err_t audio_out_set_volume(uint8_t p) { a_last[0] = p; return ESP_OK; }
bool audio_out_headphones(void) { return true; }
esp_err_t audio_out_capture_begin(audio_capture_src_t src) { a_begun++; a_last[0] = (int)src; return src <= AUDIO_CAPTURE_HEADSET ? ESP_OK : ESP_ERR_INVALID_ARG; }
size_t audio_out_capture_read(int32_t *f, size_t max, uint32_t ms)
{
    a_ptr = f; a_last[1] = (int)ms;
    for (size_t i = 0; i < max * 2; i++) f[i] = (int32_t)i;    /* the whole buffer it may fill */
    return max;
}
void audio_out_capture_end(void) { a_ended++; }
esp_err_t audio_out_monitor_write(const int16_t *s, size_t frames)
{
    int sum = 0;
    for (size_t i = 0; i < frames * 2; i++) sum += s[i];         /* reads it all */
    a_last[0] = sum;
    return ESP_OK;
}

esp_err_t usbhost_init(i2c_master_dev_handle_t exp2) { a_last[0] = exp2 == host_exp2; return ESP_OK; }
void usbhost_start(void) { a_calls++; }

esp_err_t uac_init(void) { return ESP_OK; }
bool uac_present(void) { return true; }
esp_err_t uac_stream_start(uint32_t rate, uint8_t ch) { a_streaming = 1; a_last[0] = (int)rate; a_last[1] = ch; return ESP_OK; }
void uac_stream_stop(void) { a_streaming = 0; }
esp_err_t uac_write(const void *d, size_t len, uint32_t ms)
{
    int sum = 0;
    for (size_t i = 0; i < len; i++) sum += ((const uint8_t *)d)[i];
    a_last[0] = sum; a_last[1] = (int)ms;
    return ESP_OK;
}
const char *uac_product(void) { return "Blue Snowball"; }
bool uac_mic_announced(void) { return true; }
esp_err_t uac_mic_open_bits(uint32_t *rate, uint8_t *ch, uint8_t max_bits, uint8_t *bits)
{
    a_last[0] = (int)*rate; a_last[1] = *ch; a_last[2] = max_bits;
    *rate = 44100; *ch = 2;
    if (bits) *bits = 24;
    a_mic_open = 1;
    return ESP_OK;
}
size_t uac_mic_read(int32_t *f, size_t max, uint32_t ms)
{
    (void)ms;
    for (size_t i = 0; i < max * 2; i++) f[i] = -(int32_t)i;
    return max;
}
bool uac_mic_gone(void) { return false; }
void uac_mic_close(void) { a_mic_open = 0; }
const char *uac_mic_product(void) { return "a very long product name indeed, longer than anything a box should be handed back unbounded by a call that copies it into the app's own memory -- 128 is the cap"; }

/* u-feckless-0022: the framebuffer, and feckless_umode's grant (umode_load.c
 * runs only under ESP-IDF): records what was asked. */
#include "gfx.h"
#include "feckless_umode.h"
#include "feckless_umode_load.h"
static uint16_t fb_obj[64] __attribute__((aligned(128)));
uint16_t *fake_fb = fb_obj;
int fake_fb_w = 8, fake_fb_h = 8;
uint32_t g_lo, g_hi, g_perm;
int g_calls;
esp_err_t g_result = ESP_OK;
static int app_obj;
uint16_t *gfx_fb(void) { return fake_fb; }
int gfx_w(void) { return fake_fb_w; }
int gfx_h(void) { return fake_fb_h; }
feckless_umode_app_t *feckless_umode_app_loaded(void) { return (feckless_umode_app_t *)&app_obj; }
esp_err_t feckless_umode_app_grant(feckless_umode_app_t *a, uint32_t lo, uint32_t hi, uint32_t perm)
{
    g_calls++;
    if (a != (feckless_umode_app_t *)&app_obj) return ESP_ERR_INVALID_STATE;
    g_lo = lo; g_hi = hi; g_perm = perm;
    return g_result;
}
const char *esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "ESP_OK" : "an error"; }

/* u-feckless-0023: threads, as feckless_umode's spawn would see them. */
uint32_t uac_generation(void) { return 7; }
uint32_t s_fn, s_arg, s_stack, s_calls;
int s_prio, s_core;
esp_err_t s_result = ESP_OK;
esp_err_t feckless_umode_app_spawn(feckless_umode_app_t *a, uint32_t fn, uint32_t arg,
                                   uint32_t stack_bytes, int prio, int core, uint32_t *id)
{
    s_calls++;
    if (a != (feckless_umode_app_t *)&app_obj) return ESP_ERR_INVALID_STATE;
    s_fn = fn; s_arg = arg; s_stack = stack_bytes; s_prio = prio; s_core = core;
    if (s_result == ESP_OK && id) *id = 3;
    return s_result;
}
