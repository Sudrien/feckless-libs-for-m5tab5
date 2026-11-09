/* SPDX-License-Identifier: MIT
 *
 * See feckless_box_calls.h. Each handler gets the app's a0..a6 (args[]),
 * the eighth argument through feckless_umode_arg(), and returns its result
 * in *ret (and a1, for 64 bits, through feckless_umode_ret64()).
 *
 * A pointer that fails its check is an error result, as a bad argument would
 * be from any library -- not the end of the app: the PMP already stops the
 * app reaching the host's memory, and this stops the host being talked into
 * it on the app's behalf.
 */
#include "feckless_box_calls.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "feckless_abi.h"
#include "ark12.h"
#include "gfx.h"
#include "lcd.h"
#include "tab5io.h"
#include "touch.h"

#include "box_calls_priv.h"

#define TOKEN_PANEL  BOX_TOKEN_PANEL
#define TOKEN_BUS    BOX_TOKEN_BUS

#define TAG_MAX      64u
#define LOG_MAX      512u
#define DELAY_US_MAX 100000u        /* esp_rom_delay_us busy-waits in M-mode */

/* ---- libgcc's value helpers ---------------------------------------------
 * The ones that cannot link into an app (they reach __clz_tab absolutely,
 * u-feckless-0005). On ilp32f a double and a 64-bit integer travel in a pair
 * of integer registers, low word first; nothing here is a pointer. The ones
 * that take or return a float (__extendsfdf2, __fixsfdi, __fixunssfdi,
 * __floatdisf, __floatundisf) pass it in fa0, which the trap does not save:
 * they are not here, so an app that needs one fails the load by name. */

static double d_of(uint32_t lo, uint32_t hi)
{
    const uint64_t u = (uint64_t)hi << 32 | lo;
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static uint64_t u_of_d(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

#define U64(lo, hi) ((uint64_t)(hi) << 32 | (lo))

H(c_adddf3) { feckless_umode_ret64(w, u_of_d(d_of(a[0], a[1]) + d_of(a[2], a[3])), ret); return true; }
H(c_subdf3) { feckless_umode_ret64(w, u_of_d(d_of(a[0], a[1]) - d_of(a[2], a[3])), ret); return true; }
H(c_muldf3) { feckless_umode_ret64(w, u_of_d(d_of(a[0], a[1]) * d_of(a[2], a[3])), ret); return true; }
H(c_divdf3) { feckless_umode_ret64(w, u_of_d(d_of(a[0], a[1]) / d_of(a[2], a[3])), ret); return true; }

/* RISC-V's own answers where C has none: x/0 is all ones, x%0 is x, and
 * INT64_MIN/-1 is INT64_MIN with remainder 0. */
H(c_divdi3)
{
    const int64_t x = (int64_t)U64(a[0], a[1]), y = (int64_t)U64(a[2], a[3]);
    const int64_t q = y == 0 ? -1 : (x == INT64_MIN && y == -1) ? INT64_MIN : x / y;
    feckless_umode_ret64(w, (uint64_t)q, ret);
    return true;
}
H(c_moddi3)
{
    const int64_t x = (int64_t)U64(a[0], a[1]), y = (int64_t)U64(a[2], a[3]);
    const int64_t r = y == 0 ? x : (x == INT64_MIN && y == -1) ? 0 : x % y;
    feckless_umode_ret64(w, (uint64_t)r, ret);
    return true;
}
H(c_udivdi3)
{
    const uint64_t x = U64(a[0], a[1]), y = U64(a[2], a[3]);
    feckless_umode_ret64(w, y == 0 ? UINT64_MAX : x / y, ret);
    return true;
}
H(c_umoddi3)
{
    const uint64_t x = U64(a[0], a[1]), y = U64(a[2], a[3]);
    feckless_umode_ret64(w, y == 0 ? x : x % y, ret);
    return true;
}
H(c_floatsidf) { feckless_umode_ret64(w, u_of_d((double)(int32_t)a[0]), ret); return true; }
H(c_floatunsidf) { feckless_umode_ret64(w, u_of_d((double)a[0]), ret); return true; }
H(c_floatdidf) { feckless_umode_ret64(w, u_of_d((double)(int64_t)U64(a[0], a[1])), ret); return true; }
H(c_floatundidf) { feckless_umode_ret64(w, u_of_d((double)U64(a[0], a[1])), ret); return true; }
/* libgcc leaves 0 undefined; 32 (64) is what the instructions give. */
H(c_clzsi2) { (void)w; *ret = a[0] ? (uint32_t)__builtin_clz(a[0]) : 32; return true; }
H(c_ctzsi2) { (void)w; *ret = a[0] ? (uint32_t)__builtin_ctz(a[0]) : 32; return true; }
H(c_clzdi2)
{
    (void)w;
    const uint64_t x = U64(a[0], a[1]);
    *ret = x ? (uint32_t)__builtin_clzll(x) : 64;
    return true;
}

/* ---- console, time, log -------------------------------------------------- */

H(c_putchar) { (void)w; *ret = (uint32_t)putchar((int)(a[0] & 0xff)); return true; }
H(c_puts)
{
    const char *s = str(w, a[0], FECKLESS_BOX_STR_MAX);
    *ret = s ? (uint32_t)puts(s) : (uint32_t)EOF;
    return true;
}
H(c_vTaskDelay) { (void)w; vTaskDelay((TickType_t)a[0]); *ret = 0; return true; }
H(c_esp_timer_get_time) { (void)a; feckless_umode_ret64(w, (uint64_t)esp_timer_get_time(), ret); return true; }
H(c_esp_log_timestamp) { (void)w; (void)a; *ret = esp_log_timestamp(); return true; }
H(c_esp_rom_delay_us)
{
    (void)w;
    esp_rom_delay_us(a[0] < DELAY_US_MAX ? a[0] : DELAY_US_MAX);
    *ret = 0;
    return true;
}
/* time_t is 64 bits on both sides (one picolibc). */
H(c_time)
{
    const time_t t = time(NULL);
    if (a[0]) {
        if (!ok_ptr(w, a[0], sizeof t)) {
            feckless_umode_ret64(w, (uint64_t)(int64_t)-1, ret);
            return true;
        }
        memcpy((void *)(uintptr_t)a[0], &t, sizeof t);
    }
    feckless_umode_ret64(w, (uint64_t)(int64_t)t, ret);
    return true;
}
H(c_gettimeofday)
{
    struct timeval tv;
    if (a[0] && !ok_ptr(w, a[0], sizeof tv)) {
        *ret = (uint32_t)-1;
        return true;
    }
    const int r = gettimeofday(&tv, NULL);      /* the app's tz, a[1], is ignored, as picolibc's is */
    if (r == 0 && a[0]) {
        memcpy((void *)(uintptr_t)a[0], &tv, sizeof tv);
    }
    *ret = (uint32_t)r;
    return true;
}
/* feckless_box_libc's esp_log() and esp_log_write() format the line in the
 * app; this writes it. Copied first: the host formats from its own memory. */
H(c_feckless_box_log)
{
    char tag[TAG_MAX + 1], msg[LOG_MAX + 1];
    const int tl = feckless_umode_strlen(w, (uintptr_t)a[2], TAG_MAX + 1);
    int ml = feckless_umode_strlen(w, (uintptr_t)a[3], FECKLESS_BOX_STR_MAX);
    *ret = 0;
    if (tl < 0 || ml < 0) {
        return true;                            /* a log line is not worth ending the app over */
    }
    memcpy(tag, (const void *)(uintptr_t)a[2], (size_t)tl + 1);
    if ((uint32_t)ml > LOG_MAX) {
        ml = LOG_MAX;
    }
    memcpy(msg, (const void *)(uintptr_t)a[3], (size_t)ml);
    msg[ml] = '\0';
    if (a[0] == 0) {
        esp_log_config_t cfg;
        cfg.data = a[1];
        esp_log(cfg, tag, "%s", msg);
    } else {
        esp_log_write((esp_log_level_t)(a[1] & ESP_LOG_CONFIG_LEVEL_MASK), tag, "%s", msg);
    }
    return true;
}

/* ---- the app gives up -------------------------------------------------
 * ESP_ERROR_CHECK(), abort() and assert() end the app, not the launcher: the
 * reason is logged (strings checked and copied) and the handler returns
 * false, so the window ends with FECKLESS_UMODE_END_KILLED. */

static void copy_str(const win_t *w, uint32_t p, char *out, size_t cap)
{
    const int n = feckless_umode_strlen(w, (uintptr_t)p, (uint32_t)cap);
    if (n < 0) {
        snprintf(out, cap, "?");
        return;
    }
    memcpy(out, (const void *)(uintptr_t)p, (size_t)n + 1);
}

/* _esp_error_check_failed(rc, file, line, function, expression) */
H(c__esp_error_check_failed)
{
    char file[96], fn[64], expr[128];
    copy_str(w, a[1], file, sizeof file);
    copy_str(w, a[3], fn, sizeof fn);
    copy_str(w, a[4], expr, sizeof expr);
    ESP_LOGE("box", "ESP_ERROR_CHECK failed: 0x%x at %s:%d in %s(): %s", (unsigned)a[0], file,
             (int)a[2], fn, expr);
    *ret = 0;
    return false;
}
H(c_abort)
{
    (void)a;
    ESP_LOGE("box", "the app called abort() at pc 0x%08x", (unsigned)w->ctx.pc);
    *ret = 0;
    return false;
}
/* __assert_func(file, line, function, expression) */
H(c___assert_func)
{
    char file[96], fn[64], expr[128];
    copy_str(w, a[0], file, sizeof file);
    copy_str(w, a[2], fn, sizeof fn);
    copy_str(w, a[3], expr, sizeof expr);
    ESP_LOGE("box", "assert failed: %s:%d in %s(): %s", file, (int)a[1], fn, expr);
    *ret = 0;
    return false;
}
H(c_esp_log_level_get)
{
    const char *tag = str(w, a[0], TAG_MAX + 1);
    *ret = tag ? (uint32_t)esp_log_level_get(tag) : (uint32_t)ESP_LOG_NONE;
    return true;
}
/* The app's own tags; it can quieten the launcher's too, which is noise and
 * not a hazard. "*" is refused: that is the launcher's default for every tag. */
H(c_esp_log_level_set)
{
    const char *tag = str(w, a[0], TAG_MAX + 1);
    if (tag && strcmp(tag, "*") != 0) {
        esp_log_level_set(tag, (esp_log_level_t)(a[1] & ESP_LOG_CONFIG_LEVEL_MASK));
    }
    *ret = 0;
    return true;
}

/* ---- the board, the panel, gfx, touch ------------------------------------
 * The launcher brought them up; an app's init calls them again, which the
 * libraries allow (feckless_abi's README), and gets numbers for the handles. */

static esp_lcd_panel_handle_t s_panel;

H(c_tab5io_init) { (void)w; (void)a; *ret = (uint32_t)tab5io_init(); return true; }
H(c_tab5io_bus) { (void)w; (void)a; *ret = TOKEN_BUS; return true; }
/* u-feckless-0021: the I/O expanders, for audio_out_init() and
 * usbhost_init() (box_audio.c), which take the tokens back. */
H(c_tab5io_exp1) { (void)w; (void)a; *ret = BOX_TOKEN_EXP1; return true; }
H(c_tab5io_exp2) { (void)w; (void)a; *ret = BOX_TOKEN_EXP2; return true; }
H(c_touch_present) { (void)w; (void)a; *ret = touch_present(); return true; }
H(c_touch_set_rotation) { (void)w; touch_set_rotation((int)a[0]); *ret = 0; return true; }
H(c_lcd_init)
{
    if (!ok_ptr(w, a[0], 4)) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    const esp_err_t e = lcd_init(&s_panel);
    if (e == ESP_OK) {
        const uint32_t t = TOKEN_PANEL;
        memcpy((void *)(uintptr_t)a[0], &t, 4);
    }
    *ret = (uint32_t)e;
    return true;
}
H(c_gfx_init)
{
    (void)w;
    *ret = (a[0] == TOKEN_PANEL && s_panel) ? (uint32_t)gfx_init(s_panel, (int)a[1], (int)a[2])
                                           : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}
H(c_touch_init)
{
    (void)w;
    *ret = a[0] == TOKEN_BUS ? (uint32_t)touch_init(tab5io_bus(), (int)a[1], (int)a[2])
                             : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}
H(c_lcd_backlight_set) { (void)w; *ret = (uint32_t)lcd_backlight_set((int)a[0]); return true; }
H(c_touch_get)
{
    if (!ok_ptr(w, a[0], sizeof(int)) || !ok_ptr(w, a[1], sizeof(int))) {
        *ret = 0;
        return true;
    }
    int x = 0, y = 0;
    const bool down = touch_get(&x, &y);
    memcpy((void *)(uintptr_t)a[0], &x, sizeof x);
    memcpy((void *)(uintptr_t)a[1], &y, sizeof y);
    *ret = down;
    return true;
}
H(c_gfx_fill_rect) { (void)w; gfx_fill_rect((int)a[0], (int)a[1], (int)a[2], (int)a[3], (uint16_t)a[4]); *ret = 0; return true; }
H(c_gfx_fill_circle) { (void)w; gfx_fill_circle((int)a[0], (int)a[1], (int)a[2], (uint16_t)a[3]); *ret = 0; return true; }
H(c_gfx_blit) { (void)w; gfx_blit((int)a[0], (int)a[1]); *ret = 0; return true; }
H(c_gfx_draw_text)
{
    const char *s = str(w, a[2], FECKLESS_BOX_STR_MAX);
    if (s) {
        gfx_draw_text((int)a[0], (int)a[1], s, (int)a[3], (int)a[4], (uint16_t)a[5]);
    }
    *ret = 0;
    return true;
}
H(c_gfx_text_w)
{
    const char *s = str(w, a[0], FECKLESS_BOX_STR_MAX);
    *ret = s ? (uint32_t)gfx_text_w(s, (int)a[1]) : 0;
    return true;
}
H(c_gfx_wrap_line)
{
    const char *s = str(w, a[0], FECKLESS_BOX_STR_MAX);
    if (!s || (a[3] && !ok_ptr(w, a[3], 4))) {
        *ret = 0;
        return true;
    }
    const char *next = s;
    *ret = (uint32_t)gfx_wrap_line(s, (int)a[1], (int)a[2], a[3] ? &next : NULL);
    if (a[3]) {
        /* Inside the app's own string: gfx_wrap_line only moves along it. */
        const uint32_t p = (uint32_t)(uintptr_t)next;
        memcpy((void *)(uintptr_t)a[3], &p, 4);
    }
    return true;
}
H(c_gfx_draw_para)
{
    bool ok = true;
    const uint32_t c = feckless_umode_arg(w, 7, &ok);   /* the eighth: from t0 */
    const char *s = str(w, a[2], FECKLESS_BOX_STR_MAX);
    *ret = s ? (uint32_t)gfx_draw_para((int)a[0], (int)a[1], s, (int)a[3], (int)a[4], (int)a[5],
                                       (int)a[6], (uint16_t)c)
             : 0;
    return true;
}

/* ---- the table ----------------------------------------------------------- */

static const uint32_t s_abi_v1 = FECKLESS_ABI_VERSION;
static const uint32_t s_box_v1 = 1;

#define FNG(n) { "__" #n, c_##n, NULL, 0 }
/* u-feckless-0021: Ark12's tables, which ark12.h's inline ark12_glyph()
 * reads in place. Copied into the box only if the app imports them
 * (feckless_umode's resolver copies a data entry on first use): about
 * 380 KB, for an app that draws its own text. */
#define DATA(n, len) { #n, NULL, n, (uint32_t)(len) }

static const feckless_umode_call_t s_own[] = {
    { FECKLESS_ABI_NAME, NULL, &s_abi_v1, sizeof s_abi_v1 },
    { "feckless_abi_box_v1", NULL, &s_box_v1, sizeof s_box_v1 },
    FNG(adddf3), FNG(subdf3), FNG(muldf3), FNG(divdf3),
    FNG(divdi3), FNG(moddi3), FNG(udivdi3), FNG(umoddi3),
    FNG(floatsidf), FNG(floatunsidf), FNG(floatdidf), FNG(floatundidf),
    FNG(clzsi2), FNG(ctzsi2), FNG(clzdi2),
    FN(putchar), FN(puts), FN(vTaskDelay), FN(esp_timer_get_time), FN(esp_log_timestamp),
    FN(esp_rom_delay_us), FN(time), FN(gettimeofday), FN(feckless_box_log),
    FN(_esp_error_check_failed), FN(abort), FN(__assert_func), FN(esp_log_level_get),
    FN(esp_log_level_set),
    FN(tab5io_init), FN(tab5io_bus), FN(lcd_init), FN(gfx_init), FN(touch_init),
    FN(lcd_backlight_set), FN(touch_get),
    FN(tab5io_exp1), FN(tab5io_exp2), FN(touch_present), FN(touch_set_rotation),
    DATA(ark12_runs, sizeof ark12_runs), DATA(ark12_flags, sizeof ark12_flags),
    DATA(ark12_ckpt, sizeof ark12_ckpt), DATA(ark12_litbase, sizeof ark12_litbase),
    DATA(ark12_lit, ARK12_LIT_BYTES), DATA(ark12_tiles, ARK12_TILES * 3u),
    FN(gfx_fill_rect), FN(gfx_fill_circle), FN(gfx_blit), FN(gfx_draw_text), FN(gfx_text_w),
    FN(gfx_wrap_line), FN(gfx_draw_para),
};

#define OWN_N (sizeof s_own / sizeof s_own[0])
#define HEAP_MAX 8
#define STORAGE_MAX 32
#define AUDIO_MAX 32

const feckless_umode_call_t *feckless_box_calls_v1(uint32_t *n)
{
    static feckless_umode_call_t table[HEAP_MAX + OWN_N + STORAGE_MAX + AUDIO_MAX];
    static uint32_t count;
    if (!count) {
        uint32_t k = 0;
        for (uint32_t i = 0; i < feckless_umode_heap_calls_n && i < HEAP_MAX; i++) {
            table[k++] = feckless_umode_heap_calls[i];
        }
        for (uint32_t i = 0; i < OWN_N; i++) {
            table[k++] = s_own[i];
        }
        for (uint32_t i = 0; i < box_storage_calls_n && i < STORAGE_MAX; i++) {
            table[k++] = box_storage_calls[i];
        }
        for (uint32_t i = 0; i < box_audio_calls_n && i < AUDIO_MAX; i++) {
            table[k++] = box_audio_calls[i];
        }
        count = k;
    }
    *n = count;
    return table;
}

void feckless_box_calls_end(void)
{
    box_storage_end();
    box_audio_end();
    s_panel = NULL;
}
