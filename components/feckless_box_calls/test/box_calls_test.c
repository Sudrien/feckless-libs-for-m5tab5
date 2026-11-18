/* SPDX-License-Identifier: MIT
 *
 * Host test for box_calls.c as it is, through feckless_umode's ext hook as
 * the box runs it, against recording stand-ins for the board (fake/,
 * fake_board.c). The box is mapped below 4 GB: the calls hold pointers in
 * 32 bits, as on the target.
 *
 *   make -C components/feckless_box_calls/test
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "feckless_box_calls.h"
#include "audio_out.h"   /* u-feckless-0021: ESP_OK, the capture sources */
#include "feckless_umode.h"   /* u-feckless-0022: FECKLESS_UMODE_R, _W */
#include "esp_heap_caps.h"   /* u-feckless-0045: MALLOC_CAP_SPIRAM */
#include "freertos/task.h"

extern int calls, last_i[8], log_kind;
extern char last_fn[32], last_str[700], log_tag[80];
extern uint32_t log_cfg;
extern char level_tag[80];
extern int level_set;

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define MEM (4 * 1024 * 1024)               /* u-feckless-0064: room for a ring past STREAM_MAX */
static uint8_t *mem;
static feckless_box_t box;
static feckless_umode_window_t w;
static feckless_umode_calls_t tab;
static const feckless_umode_call_t *calls_tab;
static uint32_t ncalls;

static uint32_t nr_of(const char *name)
{
    for (uint32_t i = 0; i < ncalls; i++)
        if (!strcmp(calls_tab[i].name, name)) return FECKLESS_SYS_EXT_BASE + i;
    printf("FAIL no call %s\n", name);
    failed++;
    return 0;
}

/* One call as the app's stub makes it: a0..a6 in x10.., a7 already in t0. */
static uint32_t call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                     uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    w.ctx.x[10] = a0; w.ctx.x[11] = a1; w.ctx.x[12] = a2; w.ctx.x[13] = a3;
    w.ctx.x[14] = a4; w.ctx.x[15] = a5; w.ctx.x[16] = a6; w.ctx.x[5] = a7;
    uint32_t ret = 0xdeadbeef;
    CHECK(feckless_umode_calls_ext(&w, nr_of(name), &w.ctx.x[10], &ret));
    return ret;
}
#define C0(n) call(n, 0, 0, 0, 0, 0, 0, 0, 0)
#define C1(n, a) call(n, a, 0, 0, 0, 0, 0, 0, 0)
#define C2(n, a, b) call(n, a, b, 0, 0, 0, 0, 0, 0)
#define C4(n, a, b, c, d) call(n, a, b, c, d, 0, 0, 0, 0)

static uint32_t P(const void *p) { return (uint32_t)(uintptr_t)p; }
static char *put_str(const char *s) { char *p = (char *)(uintptr_t)feckless_box_alloc(&box, (uint32_t)strlen(s) + 1); strcpy(p, s); return p; }
static uint64_t hi_lo(uint32_t lo) { return (uint64_t)w.ctx.x[11] << 32 | lo; }
static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double dbl(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

static uint64_t c64(const char *n, uint64_t x, uint64_t y)
{
    return hi_lo(C4(n, (uint32_t)x, (uint32_t)(x >> 32), (uint32_t)y, (uint32_t)(y >> 32)));
}

static void test_table(void)
{
    CHECK(ncalls > 30);
    for (uint32_t i = 0; i < ncalls; i++)
        for (uint32_t j = i + 1; j < ncalls; j++)
            CHECK(strcmp(calls_tab[i].name, calls_tab[j].name) != 0);       /* no name twice */
    int data = 0;
    for (uint32_t i = 0; i < ncalls; i++) if (!calls_tab[i].fn) { data++; CHECK(calls_tab[i].data && calls_tab[i].data_len >= 4); }
    CHECK(data == 13);       /* abi_v1, box_v1, storage_v2; Ark12's six tables (u-feckless-0021); audio_v1 (u-feckless-0052); nvs_v1 (0053); camera_v1 (0059); image_v1 (0065) */
    for (uint32_t i = 0; i < ncalls; i++) {
        if (!strcmp(calls_tab[i].name, "ark12_lit")) CHECK(calls_tab[i].data_len == 10);
        if (!strcmp(calls_tab[i].name, "ark12_tiles")) CHECK(calls_tab[i].data_len == 12);
        if (!strcmp(calls_tab[i].name, "ark12_runs")) CHECK(calls_tab[i].data_len == 12);
    }
    nr_of("malloc"); nr_of("feckless_abi_v1"); nr_of("feckless_abi_box_v1");
}

static void test_libgcc(void)
{
    CHECK(dbl(c64("__adddf3", bits(1.5), bits(2.25))) == 3.75);
    CHECK(dbl(c64("__subdf3", bits(1.0), bits(3.0))) == -2.0);
    CHECK(dbl(c64("__muldf3", bits(-1.5), bits(4.0))) == -6.0);
    CHECK(dbl(c64("__divdf3", bits(1.0), bits(8.0))) == 0.125);
    CHECK(isinf(dbl(c64("__divdf3", bits(1.0), bits(0.0)))));
    CHECK((int64_t)c64("__divdi3", (uint64_t)-100, 7) == -14);
    CHECK((int64_t)c64("__moddi3", (uint64_t)-100, 7) == -2);
    CHECK(c64("__divdi3", 5, 0) == UINT64_MAX);                     /* RISC-V: all ones */
    CHECK(c64("__moddi3", 5, 0) == 5);
    CHECK(c64("__divdi3", (uint64_t)INT64_MIN, (uint64_t)-1) == (uint64_t)INT64_MIN);
    CHECK(c64("__moddi3", (uint64_t)INT64_MIN, (uint64_t)-1) == 0);
    CHECK(c64("__udivdi3", 1ULL << 40, 1000) == (1ULL << 40) / 1000);
    CHECK(c64("__umoddi3", 1ULL << 40, 1000) == (1ULL << 40) % 1000);
    CHECK(c64("__udivdi3", 9, 0) == UINT64_MAX && c64("__umoddi3", 9, 0) == 9);
    CHECK(dbl(hi_lo(C1("__floatsidf", (uint32_t)-3))) == -3.0);
    CHECK(dbl(hi_lo(C1("__floatunsidf", 0xffffffffu))) == 4294967295.0);
    CHECK(dbl(hi_lo(C2("__floatdidf", (uint32_t)-5, 0xffffffffu))) == -5.0);
    CHECK(dbl(hi_lo(C2("__floatundidf", 0, 1))) == 4294967296.0);
    CHECK(C1("__clzsi2", 1) == 31 && C1("__clzsi2", 0) == 32);
    CHECK(C1("__ctzsi2", 8) == 3 && C1("__ctzsi2", 0) == 32);
    CHECK(C2("__clzdi2", 0, 1) == 31 && C2("__clzdi2", 1, 0) == 63 && C2("__clzdi2", 0, 0) == 64);
}

/* u-feckless-0025: the double libm names, by value in register pairs */
static void test_err_name(void)
{
    const uint32_t a = C1("esp_err_to_name", 0x102), b = C1("esp_err_to_name", 0);
    CHECK(a >= P(mem) && a < P(mem) + MEM && !strcmp((char *)(uintptr_t)a, "an error"));
    CHECK(b != a && !strcmp((char *)(uintptr_t)b, "ESP_OK"));
    CHECK(C1("esp_err_to_name", 0x102) == a);              /* the same copy again */
}

static void test_double_math(void)
{
    CHECK(dbl(hi_lo(C2("sqrt", (uint32_t)bits(2.0), (uint32_t)(bits(2.0) >> 32)))) == sqrt(2.0));
    CHECK(dbl(c64("pow", bits(10.0), bits(-2.0))) == pow(10.0, -2.0));
    CHECK(dbl(c64("atan2", bits(1.0), bits(-1.0))) == atan2(1.0, -1.0));
    CHECK(dbl(hi_lo(C4("ldexp", (uint32_t)bits(3.0), (uint32_t)(bits(3.0) >> 32), 4, 0))) == 48.0);
    CHECK((int32_t)C2("lround", (uint32_t)bits(-2.5), (uint32_t)(bits(-2.5) >> 32)) == -3);
    CHECK(isnan(dbl(hi_lo(C2("log", (uint32_t)bits(-1.0), (uint32_t)(bits(-1.0) >> 32))))));
    const char *d[] = { "sqrt", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "asinh", "exp",
                        "log", "log10", "floor", "ceil", "round", "fabs", "atan2", "pow", "fmod",
                        "ldexp", "lround" };
    for (unsigned i = 0; i < sizeof d / sizeof *d; i++) nr_of(d[i]);
}

static void test_console_time(void)
{
    char *s = put_str("hello box");
    calls = 0;
    CHECK(C1("puts", P(s)) != (uint32_t)EOF);
    CHECK(C1("puts", P(&box)) == (uint32_t)EOF);                    /* host memory: refused */
    s[strlen(s)] = 'x';                                             /* NUL gone: ... */
    memset(s, 'x', (size_t)(mem + MEM - (uint8_t *)s));             /* ... up to the end of the box */
    CHECK(C1("puts", P(s)) == (uint32_t)EOF);
    C1("vTaskDelay", 25);
    CHECK(!strcmp(last_fn, "vTaskDelay") && last_i[0] == 25);
    CHECK(hi_lo(C0("esp_timer_get_time")) == 0x123456789abULL);
    CHECK(C0("esp_log_timestamp") == 1234);
    C1("esp_rom_delay_us", 5000000);
    CHECK(last_i[0] == 100000);                                     /* capped */
    int64_t *tp = (int64_t *)(uintptr_t)feckless_box_alloc(&box, 8);
    const uint64_t t = hi_lo(C1("time", P(tp)));
    CHECK(t > 1600000000ULL && *tp == (int64_t)t);
    CHECK((int64_t)hi_lo(C1("time", P(&box))) == -1);              /* out-pointer outside: -1, nothing written */
    CHECK(hi_lo(C1("time", 0)) > 1600000000ULL);
    struct timeval *tv = (struct timeval *)(uintptr_t)feckless_box_alloc(&box, sizeof *tv);
    CHECK(C2("gettimeofday", P(tv), 0) == 0 && tv->tv_sec > 1600000000);
    CHECK(C2("gettimeofday", P(&box), 0) == (uint32_t)-1);
}

static void test_log(void)
{
    char *tag = put_str("spurious");
    char *msg = put_str("I (5) spurious: opened 42 words");
    calls = 0;
    C4("feckless_box_log", 0, 0x13, P(tag), P(msg));
    CHECK(log_kind == 0 && log_cfg == 0x13 && !strcmp(log_tag, "spurious") && !strcmp(last_str, msg));
    C4("feckless_box_log", 1, 0xfffffff2u, P(tag), P(msg));
    CHECK(log_kind == 1 && log_cfg == 2);                          /* the level, masked */
    const int before = calls;
    C4("feckless_box_log", 0, 3, P(&box), P(msg));                  /* tag outside: dropped */
    C4("feckless_box_log", 0, 3, P(tag), P(&box));
    CHECK(calls == before);
    char *longtag = (char *)(uintptr_t)feckless_box_alloc(&box, 200);
    memset(longtag, 't', 199); longtag[199] = 0;
    C4("feckless_box_log", 0, 3, P(longtag), P(msg));
    CHECK(calls == before);                                         /* a tag over 64: dropped */
    char *big = (char *)(uintptr_t)feckless_box_alloc(&box, 2000);
    memset(big, 'm', 1999); big[1999] = 0;
    C4("feckless_box_log", 0, 3, P(tag), P(big));
    CHECK(strlen(last_str) == 512);                                 /* a long message: cut */
}

/* A call that ends the app: the hook returns false. */
static bool ends(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    w.ctx.x[10] = a0; w.ctx.x[11] = a1; w.ctx.x[12] = a2; w.ctx.x[13] = a3; w.ctx.x[14] = a4;
    uint32_t ret;
    return !feckless_umode_calls_ext(&w, nr_of(name), &w.ctx.x[10], &ret);
}

static void test_give_up(void)
{
    char *file = put_str("main.c"), *fn = put_str("app_main"), *ex = put_str("lcd_init(&panel)");
    CHECK(ends("_esp_error_check_failed", 0x103, P(file), 833, P(fn), P(ex)));
    CHECK(log_kind == 1 && strstr(last_str, "0x103") && strstr(last_str, "main.c:833")
          && strstr(last_str, "lcd_init(&panel)"));
    CHECK(ends("_esp_error_check_failed", 1, P(&box), 1, P(&box), P(&box)));   /* host pointers: "?" */
    CHECK(strstr(last_str, "? ") || strstr(last_str, "?:"));
    CHECK(ends("abort", 0, 0, 0, 0, 0) && strstr(last_str, "abort()"));
    CHECK(ends("__assert_func", P(file), 12, P(fn), P(ex), 0) && strstr(last_str, "main.c:12"));
    CHECK(ends("esp_restart", 0, 0, 0, 0, 0) && strstr(last_str, "restart"));   /* u-feckless-0048 */
    char *tag = put_str("sd_io"), *star = put_str("*");
    CHECK(C1("esp_log_level_get", P(tag)) == 3 && !strcmp(level_tag, "sd_io"));
    CHECK(C1("esp_log_level_get", P(&box)) == 0);
    C2("esp_log_level_set", P(tag), 1);
    CHECK(level_set == 1);
    level_set = -1;
    C2("esp_log_level_set", P(star), 0);
    CHECK(level_set == -1);                                        /* not every tag */
}

static void test_board(void)
{
    uint32_t *panel = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    CHECK(C0("tab5io_init") == 0);
    CHECK(C1("lcd_init", P(&box)) == 0x102);                        /* out-pointer outside */
    CHECK(C1("lcd_init", P(panel)) == 0);
    CHECK(*panel != 0 && *panel != (uint32_t)(uintptr_t)&box);      /* a number, not a host pointer */
    CHECK(C4("gfx_init", 12345, 720, 1280, 0) == 0x102);            /* not the panel's number */
    CHECK(C4("gfx_init", *panel, 720, 1280, 0) == 0 && last_i[0] == 1 && last_i[1] == 720);
    const uint32_t bus = C0("tab5io_bus");
    CHECK(C4("touch_init", bus + 1, 720, 1280, 0) == 0x102);
    CHECK(C4("touch_init", bus, 720, 1280, 0) == 0 && last_i[0] == 1);
    C1("lcd_backlight_set", 60);
    CHECK(!strcmp(last_fn, "lcd_backlight_set") && last_i[0] == 60);
    int *xy = (int *)(uintptr_t)feckless_box_alloc(&box, 8);
    CHECK(C2("touch_get", P(xy), P(xy + 1)) == 1 && xy[0] == 77 && xy[1] == 88);
    CHECK(C2("touch_get", P(xy), P(&box)) == 0);
}

static void test_gfx(void)
{
    char *s = put_str("two words");
    call("gfx_fill_rect", 1, 2, 3, 4, 0x1f001f, 0, 0, 0);
    CHECK(!strcmp(last_fn, "gfx_fill_rect") && last_i[4] == 0x001f);   /* the colour as a uint16_t */
    C4("gfx_fill_circle", 10, 20, 5, 7);
    CHECK(last_i[2] == 5 && last_i[3] == 7);
    C2("gfx_blit", 0, 1280);
    CHECK(!strcmp(last_fn, "gfx_blit") && last_i[1] == 1280);
    call("gfx_draw_text", 5, 6, P(s), 2, 400, 0xffff, 0, 0);
    CHECK(!strcmp(last_fn, "gfx_draw_text") && !strcmp(last_str, "two words") && last_i[3] == 400);
    calls = 0;
    call("gfx_draw_text", 5, 6, P(&box), 2, 400, 0xffff, 0, 0);
    CHECK(calls == 0);                                              /* not drawn */
    CHECK(C2("gfx_text_w", P(s), 2) == 9 * 12);
    CHECK(C2("gfx_text_w", P(&box), 2) == 0);
    /* u-feckless-0051: styled, the style in a6 */
    call("gfx_draw_text_style", 5, 6, P(s), 3, 400, 0x07e0, 0x5, 0);
    CHECK(!strcmp(last_fn, "gfx_draw_text_style") && !strcmp(last_str, "two words"));
    CHECK(last_i[0] == 5 && last_i[1] == 6 && last_i[2] == 3 && last_i[3] == 400 && last_i[4] == 0x07e0 && last_i[5] == 0x5);
    calls = 0;
    call("gfx_draw_text_style", 5, 6, P(&box), 3, 400, 0x07e0, 0x5, 0);
    CHECK(calls == 0);
    CHECK(C4("gfx_text_w_style", P(s), 2, 0x2, 0) == 9 * 14 && last_i[1] == 0x2);
    CHECK(C4("gfx_text_w_style", P(s), 2, 0x1, 0) == 9 * 12);
    CHECK(C4("gfx_text_w_style", P(&box), 2, 0x2, 0) == 0);
    uint32_t *next = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    CHECK(C4("gfx_wrap_line", P(s), 2, 300, P(next)) == 3 && *next == P(s) + 4);
    CHECK(C4("gfx_wrap_line", P(s), 2, 300, 0) == 3);
    CHECK(C4("gfx_wrap_line", P(s), 2, 300, P(&box)) == 0);
    /* eight arguments: the colour is the eighth, in t0 */
    CHECK(call("gfx_draw_para", 1, 2, P(s), 2, 500, 24, 9, 0xabcd) == 3);
    CHECK(!strcmp(last_str, "two words") && last_i[4] == 24 && last_i[5] == 9 && last_i[6] == 0xabcd);
}

/* u-feckless-0021 */
extern int a_calls, a_last[4], a_begun, a_ended, a_streaming, a_mic_open, a_rotation;
extern const void *a_ptr;

static void test_audio(void)
{
    const uint32_t bus = C0("tab5io_bus"), e1 = C0("tab5io_exp1"), e2 = C0("tab5io_exp2");
    CHECK(e1 != e2 && e1 != bus);
    CHECK(C1("touch_present", 0) == 1);
    C1("touch_set_rotation", 3);
    CHECK(a_rotation == 3);

    /* handles are tokens, and only the right ones */
    CHECK(C4("audio_out_init", bus, e1, 48000, 0) == ESP_OK && a_last[0] == 1 && a_last[1] == 48000);
    CHECK(C4("audio_out_init", bus, e2, 48000, 0) == (uint32_t)ESP_ERR_INVALID_ARG);
    CHECK(C4("audio_out_init", P(mem), e1, 48000, 0) == (uint32_t)ESP_ERR_INVALID_ARG);
    CHECK(C1("usbhost_init", e2) == ESP_OK && a_last[0] == 1);
    CHECK(C1("usbhost_init", e1) == (uint32_t)ESP_ERR_INVALID_ARG);
    CHECK(C1("audio_out_set_volume", 300) == ESP_OK && a_last[0] == 44);   /* uint8_t, as the app's would be */
    CHECK(C0("audio_out_headphones") == 1);

    /* capture: max_frames * 2 int32, all of it in the box */
    int32_t *buf = (int32_t *)(uintptr_t)feckless_box_alloc(&box, 64 * 8);
    CHECK(C1("audio_out_capture_begin", AUDIO_CAPTURE_HEADSET) == ESP_OK);
    CHECK(C4("audio_out_capture_read", P(buf), 64, 10, 0) == 64 && a_ptr == buf && buf[127] == 127);
    CHECK(C4("audio_out_capture_read", P(buf), 64, 999999, 0) == 64 && a_last[1] == 2000);   /* capped */
    a_ptr = NULL;
    CHECK(C4("audio_out_capture_read", P(mem + MEM - 8 * 10), 11, 10, 0) == 0 && !a_ptr);    /* runs off the end */
    CHECK(C4("audio_out_capture_read", P(buf), 0x40000000u, 10, 0) == 0 && !a_ptr);          /* * 8 overflows */
    CHECK(C4("audio_out_capture_read", 0x1000, 4, 10, 0) == 0 && !a_ptr);                    /* host memory */
    int16_t *mon = (int16_t *)(uintptr_t)feckless_box_alloc(&box, 4 * 4);
    for (int i = 0; i < 8; i++) mon[i] = (int16_t)(i + 1);
    CHECK(C2("audio_out_monitor_write", P(mon), 4) == ESP_OK && a_last[0] == 36);
    CHECK(C2("audio_out_monitor_write", P(mem + MEM - 8), 3) == (uint32_t)ESP_ERR_INVALID_ARG);

    /* USB audio */
    CHECK(C0("uac_present") == 1);
    CHECK(C2("uac_stream_start", 48000, 2) == ESP_OK && a_streaming);
    uint8_t *pcm = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 6);
    memcpy(pcm, "\1\2\3\4\5\6", 6);
    CHECK(C4("uac_write", P(pcm), 6, 5, 0) == ESP_OK && a_last[0] == 21);
    CHECK(C4("uac_write", P(mem + MEM - 4), 6, 5, 0) == (uint32_t)ESP_ERR_INVALID_ARG);
    const uint32_t prod = C0("uac_product");
    CHECK(prod >= P(mem) && prod < P(mem) + MEM && !strcmp((char *)(uintptr_t)prod, "Blue Snowball"));

    uint32_t *rate = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 8);
    uint8_t *ch = (uint8_t *)(rate + 1), *bits = ch + 1;
    *rate = 48000; *ch = 1; *bits = 0;
    CHECK(C4("uac_mic_open_bits", P(rate), P(ch), 24, P(bits)) == ESP_OK);
    CHECK(a_last[0] == 48000 && a_last[1] == 1 && a_last[2] == 24);
    CHECK(*rate == 44100 && *ch == 2 && *bits == 24 && a_mic_open);
    CHECK(C4("uac_mic_open_bits", P(rate), P(ch), 24, 0) == ESP_OK);            /* bits may be NULL */
    CHECK(C4("uac_mic_open_bits", 0x1000, P(ch), 24, 0) == (uint32_t)ESP_ERR_INVALID_ARG);
    /* reads are sized by the channels the open reported: 2 */
    CHECK(C4("uac_mic_read", P(buf), 64, 10, 0) == 64 && buf[127] == -127);
    CHECK(C4("uac_mic_read", P(mem + MEM - 8 * 10), 11, 10, 0) == 0);
    const uint32_t mp = C0("uac_mic_product");
    CHECK(mp != prod && strlen((char *)(uintptr_t)mp) == 127);                   /* its own buffer, capped */
    CHECK(!strcmp((char *)(uintptr_t)prod, "Blue Snowball"));                    /* the other kept */
    CHECK(C0("uac_mic_gone") == 0);
    C0("uac_mic_close");
    CHECK(!a_mic_open && C4("uac_mic_read", P(buf), 4, 10, 0) == 0);            /* closed: nothing */

    /* what the app started, the end of the app stops */
    CHECK(C4("uac_mic_open_bits", P(rate), P(ch), 16, 0) == ESP_OK && a_mic_open);
    const int ended = a_ended;
    feckless_box_calls_end();
    CHECK(!a_mic_open && !a_streaming && a_ended == ended + 1);
    feckless_box_calls_end();
    CHECK(a_ended == ended + 1);                                                 /* once */
    calls_tab = feckless_box_calls_v1(&ncalls);
}

static uint32_t flags_of(const char *name);
/* u-feckless-0022 */
extern uint16_t *fake_fb;
extern uint32_t g_lo, g_hi, g_perm;
extern int g_calls;
extern esp_err_t g_result;

static void test_fb(void)
{
    CHECK(C0("gfx_w") == 8 && C0("gfx_h") == 8);
    const uint32_t fb = C0("gfx_fb");
    CHECK(fb == (uint32_t)(uintptr_t)fake_fb);
    CHECK(g_calls == 1 && g_lo == fb && g_hi == fb + 8 * 8 * 2);
    CHECK(g_perm == (FECKLESS_UMODE_R | FECKLESS_UMODE_W));      /* never X */
    CHECK(C0("gfx_fb") == fb && g_calls == 1);                     /* u-feckless-0030: granted once */
    CHECK(flags_of("gfx_fb") == FECKLESS_UMODE_CALL_UNLOCKED && flags_of("gfx_w") == FECKLESS_UMODE_CALL_UNLOCKED);
    CHECK(flags_of("gfx_blit") == FECKLESS_UMODE_CALL_UNLOCKED);    /* u-feckless-0032 */
    feckless_box_calls_end();                                      /* a new app asks again */
    calls_tab = feckless_box_calls_v1(&ncalls);
    g_result = ESP_ERR_NO_MEM;
    CHECK(C0("gfx_fb") == 0 && g_calls == 2);                      /* refused: no pointer */
    g_result = ESP_OK;
    uint16_t *keep = fake_fb;
    fake_fb = NULL;
    CHECK(C0("gfx_fb") == 0 && g_calls == 2);                      /* no framebuffer: no grant asked */
    fake_fb = keep;
}

/* u-feckless-0023 */
extern uint32_t s_fn, s_arg, s_stack, s_calls;
extern int s_prio, s_core;
extern esp_err_t s_result;

static uint32_t flags_of(const char *name)
{
    for (uint32_t i = 0; i < ncalls; i++)
        if (!strcmp(calls_tab[i].name, name)) return calls_tab[i].flags;
    return 0xffffffffu;
}

static void test_threads(void)
{
    uint32_t *h = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    *h = 0;
    const uint32_t fn = P(mem) + 0x100;
    CHECK(call("xTaskCreatePinnedToCore", fn, 0, 6144, 0x1234, 5, P(h), 1, 0) == 1);   /* pdPASS */
    CHECK(s_fn == fn && s_arg == 0x1234 && s_stack == 6144 && s_prio == 5 && s_core == 1);
    CHECK(*h == 3);
    CHECK(call("xTaskCreatePinnedToCore", fn, 0, 6144, 0, 5, 0, 0x7fffffff, 0) == 1);   /* no handle, any core */
    CHECK(s_core == 1);                     /* u-feckless-0045: the caller's core (the stand-in's 1) */
    const uint32_t before = s_calls;
    CHECK(call("xTaskCreatePinnedToCore", fn, 0, 6144, 0, 5, 0x1000, 0, 0) == (uint32_t)-1);
    CHECK(s_calls == before);                                       /* a host handle pointer: not asked */
    s_result = ESP_ERR_NO_MEM;
    *h = 0;
    CHECK(call("xTaskCreatePinnedToCore", fn, 0, 6144, 0, 5, P(h), 0, 0) == (uint32_t)-1 && *h == 0);
    s_result = ESP_OK;
    CHECK(C0("uac_generation") == 7);

    /* the calls that wait, or only read, go without the app's lock */
    const char *unlocked[] = { "vTaskDelay", "esp_timer_get_time", "audio_out_capture_read",
                               "uac_mic_read", "uac_write", "audio_out_monitor_write",
                               "uac_mic_gone", "__divdi3", "feckless_box_log" };
    for (unsigned i = 0; i < sizeof unlocked / sizeof *unlocked; i++)
        CHECK(flags_of(unlocked[i]) == FECKLESS_UMODE_CALL_UNLOCKED);
    const char *locked[] = { "malloc", "free", "f_read", "fopen", "gfx_fill_rect", "uac_mic_open_bits",
                             "uac_mic_close", "xTaskCreatePinnedToCore", "audio_out_capture_begin" };
    for (unsigned i = 0; i < sizeof locked / sizeof *locked; i++)
        CHECK(flags_of(locked[i]) == 0);
}

/* u-feckless-0045: FreeRTOS, against the single-threaded stand-in
 * (fake_rtos.c), whose calls never wait and abort() where FreeRTOS would
 * configASSERT(). */
extern void *fake_task, *fake_thread_task[16];
extern int fake_take_tries, fake_live, fake_crit_depth;
extern TickType_t fake_last_ticks;
extern unsigned fake_caps_last;
extern uint32_t fake_thread_id, fake_stack_lo[16], fake_stack_hi[16];
extern bool fake_stopping;
extern UBaseType_t fake_prio;
extern TaskHandle_t fake_prio_task;

#define TOK 0x52540000u

static void test_rtos(void)
{
    static int t1, t2, t3;
    const int live0 = fake_live;
    fake_task = &t1;
    fake_thread_id = 9;

    /* a mutex: given only by its holder */
    const uint32_t m = C1("xQueueCreateMutex", 1);
    CHECK(m >= TOK && m < TOK + 64);
    CHECK(C2("xQueueSemaphoreTake", m, 0) == 1);
    fake_task = &t2;
    CHECK(C4("xQueueGenericSend", m, 0, 0, 0) == 0);              /* not the holder: refused, no assert */
    fake_task = &t1;
    CHECK(C4("xQueueGenericSend", m, 0, 0, 0) == 1);
    CHECK(C4("xQueueGenericSend", m, 0, 0, 0) == 0);              /* not held any more */
    CHECK(C2("xQueueTakeMutexRecursive", m, 0) == 0);             /* the wrong kind */

    /* a recursive mutex */
    const uint32_t r = C1("xQueueCreateMutex", 4);
    CHECK(r && r != m);
    CHECK(C2("xQueueSemaphoreTake", r, 0) == 0);                  /* xSemaphoreTakeRecursive, not this */
    CHECK(C2("xQueueTakeMutexRecursive", r, 0) == 1 && C2("xQueueTakeMutexRecursive", r, 0) == 1);
    CHECK(C4("xQueueGenericSend", r, 0, 0, 0) == 0);
    fake_task = &t2;
    CHECK(C1("xQueueGiveMutexRecursive", r) == 0);
    fake_task = &t1;
    CHECK(C1("xQueueGiveMutexRecursive", r) == 1 && C1("xQueueGiveMutexRecursive", r) == 1);
    CHECK(C1("xQueueGiveMutexRecursive", r) == 0);
    CHECK(C1("xQueueCreateMutex", 0) == 0 && C1("xQueueCreateMutex", 3) == 0);

    /* a binary semaphore; a wait in slices of 100 ms */
    const uint32_t b = C4("xQueueGenericCreate", 1, 0, 3, 0);
    CHECK(b != 0);
    fake_take_tries = 0;
    CHECK(C2("xQueueSemaphoreTake", b, 250) == 0);
    CHECK(fake_take_tries == 3 && fake_last_ticks == 50);         /* 100, 100, 50 */
    CHECK(C4("xQueueGenericSend", b, 0, 0, 0) == 1);
    fake_take_tries = 0;
    CHECK(C4("xQueueGenericSend", b, 0, 0, 0) == 0 && fake_take_tries == 1);   /* full; no wait asked */
    CHECK(C2("xQueueSemaphoreTake", b, 0) == 1);
    fake_stopping = true;                                          /* forever, but the app stops */
    fake_take_tries = 0;
    CHECK(C2("xQueueSemaphoreTake", b, 0xffffffffu) == 0 && fake_take_tries == 1);
    fake_stopping = false;
    CHECK(C4("xQueueGenericCreate", 1, 4, 3, 0) == 0);            /* a semaphore with items */
    CHECK(C4("xQueueGenericCreate", 1, 0, 1, 0) == 0);            /* a mutex is not made so */

    /* a counting semaphore */
    const uint32_t c = C2("xQueueCreateCountingSemaphore", 5, 2);
    CHECK(c && C2("xQueueSemaphoreTake", c, 0) == 1 && C2("xQueueSemaphoreTake", c, 0) == 1);
    CHECK(C2("xQueueSemaphoreTake", c, 0) == 0);
    CHECK(C2("xQueueCreateCountingSemaphore", 2, 3) == 0 && C2("xQueueCreateCountingSemaphore", 0, 0) == 0);

    /* a queue of 8-byte items: copied in and out of the box only */
    const uint32_t q = C4("xQueueGenericCreate", 3, 8, 0, 0);
    CHECK(q && (fake_caps_last & MALLOC_CAP_SPIRAM));             /* the storage in PSRAM */
    uint8_t *it = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 16);
    memcpy(it, "ABCDEFGH", 8);
    CHECK(C4("xQueueGenericSend", q, P(it), 0, 0) == 1);
    memcpy(it, "12345678", 8);
    CHECK(C4("xQueueGenericSend", q, P(it), 0, 1) == 1);          /* to the front */
    CHECK(C4("xQueueGenericSend", q, 0x1000, 0, 0) == 0);         /* the host's memory */
    CHECK(C4("xQueueGenericSend", q, P(it), 0, 2) == 0);          /* overwrite: a queue of one only */
    CHECK(C4("xQueueGenericSend", q, P(it), 0, 3) == 0);
    CHECK(C1("uxQueueMessagesWaiting", q) == 2);
    CHECK(C2("xQueueSemaphoreTake", q, 0) == 0);                  /* items: not a semaphore */
    memset(it, 0, 16);
    CHECK(C4("xQueuePeek", q, P(it), 0, 0) == 1 && !memcmp(it, "12345678", 8));
    CHECK(C4("xQueueReceive", q, P(it), 0, 0) == 1 && !memcmp(it, "12345678", 8));
    CHECK(C4("xQueueReceive", q, 0x1000, 0, 0) == 0);
    CHECK(C1("uxQueueMessagesWaiting", q) <= 1);
    CHECK(C2("xQueueGenericReset", q, 0) == 1 && C1("uxQueueMessagesWaiting", q) == 0);
    CHECK(C2("xQueueGenericReset", m, 0) == 0);                   /* not a mutex */
    CHECK(C4("xQueueGenericCreate", 65536, 2, 0, 0) == 0 && C4("xQueueGenericCreate", 0, 4, 0, 0) == 0);
    CHECK(C4("xQueueReceive", b, P(it), 0, 0) == 0);              /* a semaphore has nothing to receive */

    /* tokens the box did not give */
    CHECK(C2("xQueueSemaphoreTake", 0, 0) == 0 && C2("xQueueSemaphoreTake", TOK + 64, 0) == 0);
    CHECK(C2("xQueueSemaphoreTake", 0x12345678, 0) == 0 && C1("uxQueueMessagesWaiting", TOK - 1) == 0);
    const int before = fake_live;
    C1("vQueueDelete", c);
    CHECK(fake_live == before - 1 && C2("xQueueSemaphoreTake", c, 0) == 0);
    C1("vQueueDelete", c);                                         /* twice: nothing */
    CHECK(fake_live == before - 1);

    /* stream buffers: the app's storage in place (u-feckless-0049), its
     * control block never */
    uint8_t *sb_store = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 64);
    uint8_t *sb_ctl = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 64);
    memset(sb_store, 0, 64);
    memset(sb_ctl, 0, 64);
    const uint32_t sb = call("xStreamBufferGenericCreateStatic", 64, 1, 0, P(sb_store), P(sb_ctl), 0, 0, 0);
    CHECK(sb && sb != q);
    CHECK(call("xStreamBufferGenericCreateStatic", 64, 1, 0, 0, 0, 0x100, 0, 0) == 0);   /* a callback */
    CHECK(call("xStreamBufferGenericCreateStatic", 64, 65, 0, 0, 0, 0, 0, 0) == 0);      /* trigger past size */
    CHECK(call("xStreamBufferGenericCreateStatic", 4, 1, 1, 0, 0, 0, 0, 0) == 0);        /* a message buffer of 4 */
    CHECK(call("xStreamBufferGenericCreateStatic", 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    memcpy(it, "0123456789", 10);
    CHECK(C4("xStreamBufferSend", sb, P(it), 10, 0) == 10);
    CHECK(!memcmp(sb_store, "0123456789", 10));                    /* the app's storage, in place */
    for (int i = 0; i < 64; i++) CHECK(sb_ctl[i] == 0);            /* its control block untouched */
    const uint32_t far = call("xStreamBufferGenericCreateStatic", 64, 1, 0, P(mem) + MEM - 32, 0, 0, 0, 0);
    CHECK(far != 0);                                               /* storage past the box: the host's */
    CHECK(C4("xStreamBufferSend", far, P(it), 4, 0) == 4);
    C1("vStreamBufferDelete", far);
    /* u-feckless-0064: the app's storage at any size -- Defeatist's PCM
     * ring, 3.5 MB, past the 1 MB the host gives of its own */
    const uint32_t ring_bytes = 3520u * 1024u;
    uint8_t *ring = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, ring_bytes + 1);
    CHECK(ring != NULL);
    const uint32_t big = call("xStreamBufferGenericCreateStatic", ring_bytes, 4096, 0, P(ring), P(sb_ctl), 0, 0, 0);
    CHECK(big != 0);
    CHECK(C4("xStreamBufferSend", big, P(it), 10, 0) == 10 && !memcmp(ring, "0123456789", 10));
    CHECK(C1("xStreamBufferSpacesAvailable", big) == ring_bytes - 10);
    C1("vStreamBufferDelete", big);
    CHECK(call("xStreamBufferGenericCreateStatic", ring_bytes, 4096, 0, 0, 0, 0, 0, 0) == 0);          /* the host's: capped */
    CHECK(call("xStreamBufferGenericCreate", ring_bytes, 4096, 0, 0, 0, 0, 0, 0) == 0);                /* likewise */
    CHECK(call("xStreamBufferGenericCreateStatic", ring_bytes, 4096, 0, P(mem) + MEM - 1024, 0, 0, 0, 0) == 0); /* runs past the box: the host's, capped */
    const uint32_t mb = call("xStreamBufferGenericCreateStatic", 1024u * 1024u, 4096, 0, 0, 0, 0, 0, 0);
    CHECK(mb != 0);                                                /* 1 MB of the host's still */
    C1("vStreamBufferDelete", mb);
    CHECK(C4("xStreamBufferSend", sb, 0x1000, 10, 0) == 0);
    CHECK(C1("xStreamBufferBytesAvailable", sb) == 10 && C1("xStreamBufferSpacesAvailable", sb) == 54);
    memset(it, 0, 16);
    CHECK(C4("xStreamBufferReceive", sb, P(it), 4, 0) == 4 && !memcmp(it, "0123", 4));
    CHECK(C4("xStreamBufferReceive", sb, 0x1000, 4, 0) == 0);
    CHECK(C1("xStreamBufferIsEmpty", sb) == 0 && C1("xStreamBufferIsFull", sb) == 0);
    CHECK(C1("xStreamBufferReset", sb) == 1 && C1("xStreamBufferIsEmpty", sb) == 1);
    fake_take_tries = 0;
    CHECK(C4("xStreamBufferReceive", sb, P(it), 4, 150) == 0 && fake_take_tries == 2);
    CHECK(C1("xStreamBufferBytesAvailable", q) == 0);              /* a queue is not a stream buffer */
    const uint32_t sb2 = call("xStreamBufferGenericCreate", 32, 0, 1, 0, 0, 0, 0, 0);
    CHECK(sb2 != 0);
    C1("vStreamBufferDelete", sb2);
    C1("vQueueDelete", sb);                                        /* the wrong kind: nothing */
    CHECK(C1("xStreamBufferIsEmpty", sb) == 1);

    /* notifications: a value and a state per thread, not the host task's */
    uint32_t *prev = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    fake_thread_task[3] = &t3;
    CHECK(call("xTaskGenericNotify", 3, 0, 0, 2, 0, 0, 0, 0) == 1);           /* eIncrement */
    CHECK(call("xTaskGenericNotify", 3, 0, 0, 2, 0, 0, 0, 0) == 1);
    fake_thread_id = 3;
    CHECK(C4("ulTaskGenericNotifyTake", 0, 0, 0, 0) == 2);        /* not cleared: down by one */
    CHECK(C4("ulTaskGenericNotifyTake", 0, 1, 0, 0) == 1);
    fake_take_tries = 0;
    CHECK(C4("ulTaskGenericNotifyTake", 0, 1, 120, 0) == 0);
    fake_thread_id = 9;
    CHECK(call("xTaskGenericNotify", 3, 0, 0x55, 3, P(prev), 0, 0, 0) == 1 && *prev == 0);
    CHECK(call("xTaskGenericNotify", 3, 0, 0x66, 4, P(prev), 0, 0, 0) == 0 && *prev == 0x55);   /* pending */
    CHECK(call("xTaskGenericNotify", 3, 0, 0x0f, 1, 0, 0, 0, 0) == 1);        /* eSetBits */
    fake_thread_id = 3;
    CHECK(call("xTaskGenericNotifyWait", 0, 0, 0xffffffffu, P(prev), 0, 0, 0, 0) == 1 && *prev == 0x5f);
    CHECK(call("xTaskGenericNotifyWait", 0, 0, 0, P(prev), 0, 0, 0, 0) == 0);  /* nothing pending */
    CHECK(call("xTaskGenericNotifyWait", 0, 0, 0, 0x1000, 0, 0, 0, 0) == 0);
    fake_thread_id = 9;
    CHECK(call("xTaskGenericNotify", 5, 0, 1, 2, 0, 0, 0, 0) == 0);           /* not running */
    CHECK(call("xTaskGenericNotify", 3, 1, 1, 2, 0, 0, 0, 0) == 0);           /* index 1 */
    CHECK(call("xTaskGenericNotify", 0x1234, 0, 1, 2, 0, 0, 0, 0) == 0);
    CHECK(call("xTaskGenericNotify", 3, 0, 1, 9, 0, 0, 0, 0) == 0);           /* no such action */
    CHECK(call("xTaskGenericNotify", 3, 0, 1, 2, 0x1000, 0, 0, 0) == 0);

    /* critical sections: one recursive mutex for the app */
    CHECK(C2("xPortEnterCriticalTimeout", P(it), 0xffffffffu) == 1);
    CHECK(C2("xPortEnterCriticalTimeout", P(it), 0xffffffffu) == 1);          /* nested */
    fake_task = &t2;
    CHECK(C2("xPortEnterCriticalTimeout", 0x1000, 0) == 0);                   /* held: one try */
    C1("vPortExitCriticalMultiCore", 0);                                       /* not its: nothing */
    fake_task = &t1;
    C1("vPortExitCriticalMultiCore", P(it));
    C0("vPortExitCritical");
    fake_task = &t2;
    CHECK(C2("xPortEnterCriticalTimeout", 0, 0) == 1);
    C1("vPortExitCriticalMultiCore", 0);
    fake_task = &t1;
    CHECK(fake_crit_depth == 0);

    /* tasks */
    CHECK(C0("xPortInIsrContext") == 0 && C0("xTaskGetTickCount") == 4242);
    CHECK(C0("xTaskGetCurrentTaskHandle") == 9);
    const uint32_t nm = C1("pcTaskGetName", 0);
    CHECK(nm >= P(mem) && nm < P(mem) + MEM && !strcmp((char *)(uintptr_t)nm, "main"));
    const uint32_t fn = P(mem) + 0x100;
    char *name = put_str("media-and-a-long-name");
    CHECK(call("xTaskCreateStaticPinnedToCore", fn, P(name), 16384, 0x77, 1, 0x1000, 0x1000, 0x7fffffff) == 3);
    CHECK(s_fn == fn && s_arg == 0x77 && s_stack == 16384 && s_prio == 1 && s_core == 1);
    CHECK(!strcmp((char *)(uintptr_t)C1("pcTaskGetName", 3), "media-and-a-lon"));
    CHECK(C1("pcTaskGetName", 0x99) == 0);
    s_result = ESP_ERR_NO_MEM;
    CHECK(call("xTaskCreateStaticPinnedToCore", fn, 0, 16384, 0, 1, 0, 0, 0) == 0);
    s_result = ESP_OK;
    uint8_t *stk = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 256);
    memset(stk, 0xa5, 100);
    memset(stk + 100, 0, 156);
    fake_thread_task[9] = &t1;
    fake_stack_lo[9] = P(stk);
    fake_stack_hi[9] = P(stk) + 256;
    CHECK(C1("uxTaskGetStackHighWaterMark", 0) == 100 && C1("xTaskGetStackStart", 0) == P(stk));
    memset(stk, 0xa5, 256);
    CHECK(C1("uxTaskGetStackHighWaterMark", 9) == 256);            /* never past the top */
    CHECK(C1("uxTaskGetStackHighWaterMark", 5) == 0 && C1("xTaskGetStackStart", 5) == 0);
    C2("vTaskPrioritySet", 3, 99);
    CHECK(fake_prio_task == &t3 && fake_prio == 23 && C1("uxTaskPriorityGet", 3) == 23);
    C2("vTaskPrioritySet", 0, 0);
    CHECK(fake_prio_task == &t1 && fake_prio == 1);
    C2("vTaskPrioritySet", 5, 3);                                   /* not running: nothing */
    CHECK(fake_prio_task == &t1 && C1("uxTaskPriorityGet", 5) == 0);
    CHECK(C1("eTaskGetState", 0) == 0 && C1("eTaskGetState", 5) == 4 && C1("eTaskGetState", 3) == 2);
    const int delays = calls;
    C1("vTaskSuspend", 3);                                          /* another thread: ignored */
    CHECK(calls == delays + 1);                                     /* only the log line */
    fake_stopping = true;
    C1("vTaskSuspend", 0);                                          /* itself: until the app stops */
    CHECK(!strcmp(last_fn, "vTaskDelay"));
    fake_stopping = false;

    const char *unl[] = { "xQueueGenericSend", "xQueueSemaphoreTake", "xQueueReceive",
                          "xQueueTakeMutexRecursive", "xStreamBufferSend", "xStreamBufferReceive",
                          "ulTaskGenericNotifyTake", "xTaskGenericNotifyWait",
                          "xPortEnterCriticalTimeout", "vPortExitCriticalMultiCore", "vTaskSuspend",
                          "xTaskGetTickCount" };
    for (unsigned i = 0; i < sizeof unl / sizeof *unl; i++) CHECK(flags_of(unl[i]) == FECKLESS_UMODE_CALL_UNLOCKED);
    const char *lck[] = { "xQueueGenericCreate", "xQueueCreateMutex", "vQueueDelete",
                          "xStreamBufferGenericCreateStatic", "xTaskGenericNotify", "pcTaskGetName",
                          "uxTaskGetStackHighWaterMark", "vTaskPrioritySet", "eTaskGetState",
                          "xTaskCreateStaticPinnedToCore" };
    for (unsigned i = 0; i < sizeof lck / sizeof *lck; i++) CHECK(flags_of(lck[i]) == 0);

    /* the box goes: every object, wake semaphore and the critical mutex with it */
    feckless_box_calls_end();
    CHECK(fake_live == live0);
    CHECK(C2("xQueueSemaphoreTake", m, 0) == 0);
    fake_thread_task[3] = fake_thread_task[9] = NULL;
}

/* u-feckless-0046: playback, the battery, the clock, polyrsp, the rest of gfx */
extern int d_calls, d_idle, d_mute, d_filter, d_rot, d_poly_n, d_poly_xy[32], d_swallow, d_rsp_live, d_args[8];
extern size_t d_len;
extern const void *d_ptr;
extern uint32_t d_rate, d_duty;
extern uint8_t d_ch;
extern int64_t d_epoch;
extern char d_str[256], d_why[64];

static void test_drivers(void)
{
    uint8_t *pcm = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 4096);
    d_calls = 0;
    CHECK(C2("audio_out_write", P(pcm), 4096) == ESP_OK && d_ptr == pcm && d_len == 4096);
    CHECK(C2("audio_out_write", P(mem) + MEM - 16, 4096) == (uint32_t)ESP_ERR_INVALID_ARG);   /* past the box */
    CHECK(C2("audio_out_write", P(mem), 65537) == (uint32_t)ESP_ERR_INVALID_ARG);          /* past WRITE_MAX */
    CHECK(C2("audio_out_write", 0x1000, 16) == (uint32_t)ESP_ERR_INVALID_ARG);
    CHECK(C2("audio_out_write", P(pcm), 0) == ESP_OK && d_calls == 1);               /* nothing to send */
    CHECK(C2("audio_out_set_format", 48000, 2) == ESP_OK && d_rate == 48000 && d_ch == 2);
    CHECK(C1("audio_out_rate_supported", 44100) == 1 && C1("audio_out_rate_supported", 22050) == 0);
    CHECK(C0("audio_out_rate") == 44100);
    C1("audio_out_set_mute", 1);
    CHECK(d_mute == 1 && C0("audio_out_muted") == 1);
    C1("audio_out_set_idle", 0);
    CHECK(d_idle == 0 && C0("audio_out_route") == AUDIO_OUT_HEADPHONES);
    const uint32_t rn = C0("audio_out_route_name");
    CHECK(rn >= P(mem) && rn < P(mem) + MEM && !strcmp((char *)(uintptr_t)rn, "headphones"));

    CHECK(C1("battery_init", 0x42555331u) == ESP_OK && C1("battery_init", 7) == (uint32_t)ESP_ERR_INVALID_ARG);
    CHECK(C0("battery_start") == ESP_OK && C0("battery_pct") == 87 && C0("battery_mv") == 7712);
    CHECK(C0("battery_charging") == 1 && C0("battery_external") == 0);
    char *why = put_str("seek");
    C1("battery_trace_arm", P(why));
    CHECK(!strcmp(d_why, "seek"));
    C1("battery_trace_arm", 0x1000);
    CHECK(!strcmp(d_why, "app"));

    CHECK(C1("rtc8130_init", 0x42555331u) == 1 && C1("rtc8130_init", 0) == 0);
    int64_t *ep = (int64_t *)(uintptr_t)feckless_box_alloc(&box, 8);
    CHECK(C1("rtc8130_read", P(ep)) == 1 && *ep == 0x123456789LL);
    CHECK(C1("rtc8130_read", 0x1000) == 0);
    CHECK(C2("rtc8130_write", 0x9abcdef0u, 0x12) == 1 && d_epoch == 0x129abcdef0LL);
    CHECK(C2("rtc8130_write_forward", 5, 0) == 1 && d_epoch == -5);

    /* polyrsp: a number, and an output checked against what the input can make */
    const uint32_t r = C4("polyrsp_open", 44100, 48000, 256, 0);
    CHECK(r == 0x52535000u);
    CHECK(C4("polyrsp_open", 48000, 48000, 256, 0) == 0);             /* refused by polyrsp */
    CHECK(C4("polyrsp_open", 44100, 48000, 0, 0) == 0);
    CHECK(C1("polyrsp_max_out", r) == 280);
    int16_t *in = (int16_t *)(uintptr_t)feckless_box_alloc(&box, 256 * 4);
    const uint32_t most = (100u * 48000u + 44099u) / 44100u + 1u;      /* 110 */
    int16_t *out = (int16_t *)(uintptr_t)feckless_box_alloc(&box, most * 4);
    in[0] = 77;
    CHECK(C4("polyrsp_process", r, P(in), 100, P(out)) == most && out[2 * most - 1] == 77);
    CHECK(C4("polyrsp_process", r, P(in), 100, P(mem) + MEM - (most - 1) * 4) == 0);   /* one frame short of the box's end */
    CHECK(C4("polyrsp_process", r, P(in), 100, P(mem) + MEM - most * 4) == most);
    CHECK(C4("polyrsp_process", r, P(in), 257, P(out)) == 0);          /* past max_in */
    CHECK(C4("polyrsp_process", r, 0x1000, 100, P(out)) == 0);
    CHECK(C4("polyrsp_process", r + 1, P(in), 100, P(out)) == 0);      /* not open */
    C1("polyrsp_reset", r);
    const uint32_t r2 = C4("polyrsp_open", 48000, 44100, 64, 0);
    CHECK(r2 && r2 != r && d_rsp_live == 2);
    C1("polyrsp_close", r);
    CHECK(d_rsp_live == 1 && C1("polyrsp_max_out", r) == 0);

    /* gfx */
    C1("gfx_set_filter", 3);
    CHECK(d_filter == 3 && C0("gfx_filter") == 3);
    CHECK(C2("gfx_blit_err", 10, 20) == ESP_OK && d_args[0] == 10 && d_args[1] == 20);
    C1("gfx_set_rotation", 1);
    CHECK(d_rot == 1);
    int *xy = (int *)(uintptr_t)feckless_box_alloc(&box, 32 * sizeof(int));
    for (int i = 0; i < 20; i++) xy[i] = i * 3;
    C4("gfx_fill_poly", P(xy), 10, 0xffff, 0);
    CHECK(d_poly_n == 10 && d_poly_xy[19] == 57);
    d_poly_n = 0;
    C4("gfx_fill_poly", P(xy), 17, 0xffff, 0);                          /* more than 16 points */
    C4("gfx_fill_poly", P(xy), 0, 0xffff, 0);
    C4("gfx_fill_poly", P(mem) + MEM - 8 * sizeof(int), 10, 0xffff, 0);    /* runs past the box */
    C4("gfx_fill_poly", 0x1000, 3, 0, 0);
    CHECK(d_poly_n == 0);
    C4("gfx_fill_star", 1, 2, 3, 4);
    CHECK(d_args[0] == 1 && d_args[3] == 4);
    call("gfx_draw_spinner", 5, 6, 7, 1234, 0xf800, 0x07e0, 0, 0);
    CHECK(d_args[3] == 1234 && d_args[5] == 0x07e0);
    char *tt = (char *)(uintptr_t)feckless_box_alloc(&box, 16);
    CHECK(C4("gfx_time_text", P(tt), 16, 161, 1) == P(tt) && !strcmp(tt, "-2:41"));
    CHECK(C4("gfx_time_text", P(tt), 3, 161, 0) == P(tt) && !strcmp(tt, "2:"));
    CHECK(C4("gfx_time_text", P(tt), 64, 6061, 0) == P(tt) && !strcmp(tt, "101:01"));   /* capped to 16 */
    CHECK(C4("gfx_time_text", 0x1000, 16, 1, 0) == 0 && C4("gfx_time_text", P(tt), 0, 1, 0) == 0);
    CHECK(C2("gfx_time_text_w", P(tt), 2) == 6 * 12);
    char *s = put_str("Long title, kept at its end");
    call("gfx_draw_time_text", 1, 2, P(tt), 3, 4, 0, 0, 0);
    CHECK(!strcmp(d_str, "101:01") && d_args[3] == 4);
    call("gfx_draw_text_tail", 1, 2, P(s), 2, 300, 0xbeef, 0, 0);
    CHECK(!strcmp(d_str, s) && d_args[3] == 300 && d_args[4] == 0xbeef);
    call("gfx_draw_text_clipped", -40, 2, 10, 200, P(s), 2, 0x1234, 0);
    CHECK(d_args[0] == -40 && d_args[3] == 200 && d_args[5] == 0x1234);
    d_str[0] = 0;
    call("gfx_draw_text_clipped", 0, 0, 0, 10, 0x1000, 1, 0, 0);
    CHECK(d_str[0] == 0);
    char *ar = put_str("\xd8\xa7");
    CHECK(C1("gfx_text_rtl", P(ar)) == 1 && C1("gfx_text_rtl", P(s)) == 0 && C1("gfx_text_rtl", 0x1000) == 0);
    CHECK(C4("gfx_para_rows", P(s), 1, 100, 0) == 3);
    CHECK(C1("lcd_backlight_set_counts", 4095) == ESP_OK && d_duty == 4095);
    C0("touch_swallow");
    CHECK(d_swallow == 1);
    CHECK(C0("usbhost_running") == 1 && C0("usbhost_vbus_on") == 0);

    const char *unl[] = { "audio_out_write", "battery_pct", "audio_out_rate", "gfx_blit_err", "usbhost_running" };
    for (unsigned i = 0; i < sizeof unl / sizeof *unl; i++) CHECK(flags_of(unl[i]) == FECKLESS_UMODE_CALL_UNLOCKED);
    const char *lck[] = { "polyrsp_process", "polyrsp_open", "audio_out_set_format", "gfx_fill_poly", "rtc8130_read" };
    for (unsigned i = 0; i < sizeof lck / sizeof *lck; i++) CHECK(flags_of(lck[i]) == 0);

    /* u-feckless-0048: randomness and the clock */
    CHECK(C0("esp_random") == 0xc0ffee11u);
    uint8_t *rnd = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 32);
    memset(rnd, 0, 32);
    C2("esp_fill_random", P(rnd), 32);
    CHECK(rnd[0] == 0x3c && rnd[31] == 0x3c);
    C2("esp_fill_random", P(mem) + MEM - 8, 32);                    /* past the box: nothing */
    CHECK(C2("settimeofday", 0, 0) == (uint32_t)-1 && C2("settimeofday", 0x1000, 0) == (uint32_t)-1);

    /* the box goes: resamplers closed, the amplifier idle again */
    d_idle = 0;
    feckless_box_calls_end();
    CHECK(d_rsp_live == 0 && d_idle == 1);
}

/* u-feckless-0052: esp_audio_codec's simple decoder, against fake_codec.c */
#include "esp_audio_simple_dec.h"
#include "esp_alac_dec.h"
extern int c_live, c_regs, c_simple_regs, c_cfg_size;
extern esp_audio_err_t c_reg_result;
extern uint8_t c_cookie[64];
extern uint32_t c_cookie_len;
extern const void *c_cookie_ptr, *c_raw_ptr, *c_out_ptr;

static void test_codec(void)
{
    const int live0 = c_live;
    CHECK(C0("esp_audio_dec_register_default") == ESP_AUDIO_ERR_OK);       /* ALREADY_EXIST is success */
    CHECK(C0("esp_audio_dec_register_default") == ESP_AUDIO_ERR_OK && c_regs == 1);   /* once */
    CHECK(C0("esp_audio_simple_dec_register_default") == ESP_AUDIO_ERR_OK && c_simple_regs == 1);
    nr_of("feckless_abi_audio_v1");

    esp_audio_simple_dec_cfg_t *cfg = (void *)(uintptr_t)feckless_box_alloc(&box, sizeof *cfg);
    uint32_t *h = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    memset(cfg, 0, sizeof *cfg);
    cfg->dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == ESP_AUDIO_ERR_OK);
    const uint32_t flac = *h;
    CHECK(flac == 0x41444500u && c_live == live0 + 1);

    /* a decode: the app's buffers, read and written in place; results back */
    esp_audio_simple_dec_raw_t *raw = (void *)(uintptr_t)feckless_box_alloc(&box, sizeof *raw);
    esp_audio_simple_dec_out_t *out = (void *)(uintptr_t)feckless_box_alloc(&box, sizeof *out);
    uint8_t *in = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 64), *pcm = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 128);
    for (int i = 0; i < 64; i++) in[i] = (uint8_t)(i + 1);
    *raw = (esp_audio_simple_dec_raw_t){ in, 64, false, 999, 0 };
    *out = (esp_audio_simple_dec_out_t){ pcm, 128, 7, 7 };
    CHECK(C4("esp_audio_simple_dec_process", flac, P(raw), P(out), 0) == ESP_AUDIO_ERR_OK);
    CHECK(c_raw_ptr == in && c_out_ptr == pcm);
    CHECK(raw->consumed == 32 && out->decoded_size == 64 && out->needed_size == 0);
    CHECK(raw->buffer == in && out->buffer == pcm);                 /* the app's pointers left as they were */
    CHECK(pcm[0] == 1 && pcm[1] == 1 && pcm[63] == 32);
    *out = (esp_audio_simple_dec_out_t){ pcm, 16, 0, 0 };
    CHECK(C4("esp_audio_simple_dec_process", flac, P(raw), P(out), 0) == (uint32_t)ESP_AUDIO_ERR_BUFF_NOT_ENOUGH);
    CHECK(out->needed_size == 64);
    *raw = (esp_audio_simple_dec_raw_t){ (uint8_t *)(uintptr_t)(P(mem) + MEM - 8), 64, false, 0, 0 };
    c_raw_ptr = NULL;
    CHECK(C4("esp_audio_simple_dec_process", flac, P(raw), P(out), 0) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    CHECK(c_raw_ptr == NULL);                                       /* never reached the codec */
    *raw = (esp_audio_simple_dec_raw_t){ in, 64, false, 0, 0 };
    *out = (esp_audio_simple_dec_out_t){ (uint8_t *)(uintptr_t)0x1000, 128, 0, 0 };
    CHECK(C4("esp_audio_simple_dec_process", flac, P(raw), P(out), 0) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    CHECK(C4("esp_audio_simple_dec_process", flac, 0x1000, P(out), 0) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    *out = (esp_audio_simple_dec_out_t){ pcm, 128, 0, 0 };
    CHECK(C4("esp_audio_simple_dec_process", flac + 1, P(raw), P(out), 0) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    esp_audio_simple_dec_info_t *info = (void *)(uintptr_t)feckless_box_alloc(&box, sizeof *info);
    CHECK(C2("esp_audio_simple_dec_get_info", flac, P(info)) == ESP_AUDIO_ERR_OK && info->sample_rate == 96000 &&
          info->bits_per_sample == 24 && info->channel == 2);
    CHECK(C2("esp_audio_simple_dec_get_info", flac, 0x1000) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);

    /* ALAC: the configuration and its cookie copied into the host */
    esp_alac_dec_cfg_t *al = (void *)(uintptr_t)feckless_box_alloc(&box, sizeof *al);
    uint8_t *cookie = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 24);
    for (int i = 0; i < 24; i++) cookie[i] = (uint8_t)(0xa0 + i);
    *al = (esp_alac_dec_cfg_t){ cookie, 24 };
    *cfg = (esp_audio_simple_dec_cfg_t){ ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC, al, (int)sizeof *al, true };
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == ESP_AUDIO_ERR_OK);
    const uint32_t alac = *h;
    CHECK(alac != flac && c_cookie_len == 24 && c_cookie[23] == 0xa0 + 23);
    CHECK(c_cookie_ptr != cookie && ((uintptr_t)c_cookie_ptr < P(mem) || (uintptr_t)c_cookie_ptr >= P(mem) + MEM));
    CHECK(cfg->dec_cfg == al && al->codec_spec_info == cookie);     /* the app's left alone */
    *al = (esp_alac_dec_cfg_t){ (uint8_t *)(uintptr_t)0x1000, 24 };
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER && *h == 0);
    *al = (esp_alac_dec_cfg_t){ cookie, 100000 };
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    *cfg = (esp_audio_simple_dec_cfg_t){ ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC, al, 4, true };     /* too short for its struct */
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    *cfg = (esp_audio_simple_dec_cfg_t){ ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS, al, (int)sizeof *al, false };
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_NOT_SUPPORT);
    *cfg = (esp_audio_simple_dec_cfg_t){ ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC, al, 65, false };   /* past CFG_MAX */
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    *cfg = (esp_audio_simple_dec_cfg_t){ ESP_AUDIO_SIMPLE_DEC_TYPE_NONE, NULL, 0, false };
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_NOT_SUPPORT && *h == 0);
    CHECK(C2("esp_audio_simple_dec_open", 0x1000, P(h)) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    CHECK(c_live == live0 + 2);

    /* four at once */
    *cfg = (esp_audio_simple_dec_cfg_t){ ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC, NULL, 0, false };
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == ESP_AUDIO_ERR_OK);
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == ESP_AUDIO_ERR_OK);
    CHECK(C2("esp_audio_simple_dec_open", P(cfg), P(h)) == (uint32_t)ESP_AUDIO_ERR_MEM_LACK);
    C1("esp_audio_simple_dec_close", flac);
    CHECK(c_live == live0 + 3);
    CHECK(C2("esp_audio_simple_dec_get_info", flac, P(info)) == (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER);
    C1("esp_audio_simple_dec_close", flac);                          /* twice: nothing */
    C1("esp_audio_simple_dec_close", 0x12345);
    CHECK(c_live == live0 + 3);

    CHECK(flags_of("esp_audio_simple_dec_process") == FECKLESS_UMODE_CALL_UNLOCKED);
    CHECK(flags_of("esp_audio_simple_dec_open") == 0);
    feckless_box_calls_end();                                        /* the rest closed */
    CHECK(c_live == live0);
    (void)alac;
}

/* u-feckless-0053: NVS in the launcher's namespaces only, against fake_nvs.c */
#include "nvs_handler.h"
extern const void *n_last_src;
extern int n_writes;

static char *bstr(const char *s)
{
    char *p = (char *)(uintptr_t)feckless_box_alloc(&box, (uint32_t)strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static void test_nvs(void)
{
    char *mine = bstr("defeatist"), *wifi = bstr("wifi"), *k = bstr("prefs"), *k2 = bstr("lang");
    uint8_t *u8 = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    uint32_t *len = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    uint8_t *buf = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 64);

    /* named nothing: nothing found, nothing changed */
    CHECK(C0("nvsh_ready") == 0);
    CHECK(C4("nvsh_set_u8", P(mine), P(k2), 3, 0) == ESP_ERR_NOT_ALLOWED && n_writes == 0);
    *u8 = 77;
    CHECK(C4("nvsh_get_u8", P(mine), P(k2), P(u8), 0) == ESP_ERR_NVS_NOT_FOUND && *u8 == 77);

    /* the launcher's own value, which the app must never reach */
    CHECK(nvsh_set_str("wifi", "pass", "secret") == ESP_OK);
    const char *const one[] = { "defeatist" };
    CHECK(feckless_box_calls_nvs(one, 1));
    CHECK(C0("nvsh_ready") == 1);
    *len = 64;
    CHECK(C4("nvsh_get_str", P(wifi), P(bstr("pass")), P(buf), P(len)) == ESP_ERR_NVS_NOT_FOUND);
    CHECK(C4("nvsh_set_str", P(wifi), P(bstr("pass")), P(bstr("x")), 0) == ESP_ERR_NOT_ALLOWED);
    CHECK(C4("nvsh_erase_key", P(wifi), P(bstr("pass")), 0, 0) == ESP_ERR_NOT_ALLOWED);
    CHECK(C1("nvsh_erase_ns", P(wifi)) == ESP_ERR_NOT_ALLOWED);
    size_t hl = 16; char hb[16];
    CHECK(nvsh_get_str("wifi", "pass", hb, &hl) == ESP_OK && !strcmp(hb, "secret"));
    CHECK(C4("nvsh_get_u8", P(bstr("defeatis")), P(k2), P(u8), 0) == ESP_ERR_NVS_NOT_FOUND);  /* a prefix is not it */

    /* integers */
    CHECK(C4("nvsh_set_u8", P(mine), P(k2), 0x1ff, 0) == ESP_OK);       /* the low byte, as the u8 it is */
    CHECK(C4("nvsh_get_u8", P(mine), P(k2), P(u8), 0) == ESP_OK && *u8 == 0xff);
    CHECK(C4("nvsh_set_i32", P(mine), P(bstr("clk")), (uint32_t)-5, 0) == ESP_OK);
    int32_t *i32 = (int32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    CHECK(C4("nvsh_get_i32", P(mine), P(bstr("clk")), P(i32), 0) == ESP_OK && *i32 == -5);
    CHECK(C4("nvsh_get_u8", P(mine), P(k2), 0x1000, 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("nvsh_get_u8", P(mine), 0x1000, P(u8), 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("nvsh_get_u8", P(mine), P(bstr("")), P(u8), 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("nvsh_get_u8", P(mine), P(bstr("a_key_far_too_long")), P(u8), 0) == ESP_ERR_INVALID_ARG);

    /* a blob through the host: what NVS writes from is never the app's */
    for (int i = 0; i < 7; i++) buf[i] = (uint8_t)(i + 1);
    CHECK(C4("nvsh_set_blob", P(mine), P(k), P(buf), 7) == ESP_OK);
    CHECK(n_last_src && ((uintptr_t)n_last_src < P(mem) || (uintptr_t)n_last_src >= P(mem) + MEM));
    memset(buf, 0, 64);
    *len = 0;
    CHECK(C4("nvsh_get_blob", P(mine), P(k), 0, P(len)) == ESP_OK && *len == 7);     /* the size only */
    *len = 3;
    CHECK(C4("nvsh_get_blob", P(mine), P(k), P(buf), P(len)) == ESP_ERR_NVS_INVALID_LENGTH && *len == 7 && buf[0] == 0);
    *len = 64;
    CHECK(C4("nvsh_get_blob", P(mine), P(k), P(buf), P(len)) == ESP_OK && *len == 7 && buf[6] == 7 && buf[7] == 0);
    *len = 64;
    CHECK(C4("nvsh_get_blob", P(mine), P(k), P(mem + MEM - 8), P(len)) == ESP_ERR_INVALID_ARG);   /* room past the box */
    CHECK(C4("nvsh_set_blob", P(mine), P(k), P(mem + MEM - 4), 7) == ESP_ERR_INVALID_ARG);
    CHECK(C4("nvsh_set_blob", P(mine), P(k), P(buf), 8193) == ESP_ERR_INVALID_ARG);
    CHECK(C4("nvsh_set_blob", P(mine), P(k), 0, 0) == ESP_OK);        /* empty */

    /* a string */
    CHECK(C4("nvsh_set_str", P(mine), P(bstr("key")), P(bstr("abc")), 0) == ESP_OK);
    *len = 64;
    CHECK(C4("nvsh_get_str", P(mine), P(bstr("key")), P(buf), P(len)) == ESP_OK && *len == 4 && !strcmp((char *)buf, "abc"));
    char *unterminated = (char *)(mem + MEM - 4);
    memcpy(unterminated, "abcd", 4);
    CHECK(C4("nvsh_set_str", P(mine), P(bstr("key")), P(unterminated), 0) == ESP_ERR_INVALID_ARG);

    /* erase */
    CHECK(C4("nvsh_erase_key", P(mine), P(bstr("key")), 0, 0) == ESP_OK);
    CHECK(C4("nvsh_erase_key", P(mine), P(bstr("key")), 0, 0) == ESP_ERR_NVS_NOT_FOUND);
    CHECK(C1("nvsh_erase_ns", P(mine)) == ESP_OK);
    CHECK(C4("nvsh_get_u8", P(mine), P(k2), P(u8), 0) == ESP_ERR_NVS_NOT_FOUND);
    CHECK(nvsh_get_str("wifi", "pass", hb, &hl) == ESP_OK);           /* still the launcher's */

    /* bad lists change nothing */
    const char *const five[] = { "a", "b", "c", "d", "e" }, *const longn[] = { "sixteen_chars_xx" }, *const empty[] = { "" };
    CHECK(!feckless_box_calls_nvs(five, 5) && !feckless_box_calls_nvs(longn, 1) && !feckless_box_calls_nvs(empty, 1));
    CHECK(!feckless_box_calls_nvs(NULL, 1));
    CHECK(C0("nvsh_ready") == 1);
    CHECK(flags_of("nvsh_set_blob") == FECKLESS_UMODE_CALL_UNLOCKED);
    nr_of("feckless_abi_nvs_v1");

    /* the names go with the app */
    feckless_box_calls_end();
    CHECK(C0("nvsh_ready") == 0);
    CHECK(C4("nvsh_set_u8", P(mine), P(k2), 1, 0) == ESP_ERR_NOT_ALLOWED);
}

/* u-feckless-0059: feckless_camera's frames, against fake_camera.c; and
 * gfx_px and gfx_rotation, which a camera's view is drawn with */
extern int cam_inits, cam_starts, cam_stops, cam_grabs;
extern bool cam_sensor, cam_on;
extern const void *cam_bus, *cam_dst;
extern esp_err_t cam_grab_result;
extern void *host_bus;
extern int d_rot, d_args[8];
/* u-feckless-0060: the board's core */
extern int cam_core_init, cam_core_start, cam_core_stop, cam_core_grab;
extern int fake_core, fake_tasks, fake_task_core;
extern bool fake_task_fail;

static void test_camera(void)
{
    const int live0 = fake_live;
    nr_of("feckless_abi_camera_v1");
    int *wh = (int *)(uintptr_t)feckless_box_alloc(&box, 2 * sizeof(int));
    uint8_t *buf = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 128);
    memset(buf, 0xee, 128);

    /* the bus is the app's number for it, and the host's own goes down */
    CHECK(C0("camera_present") == 0);
    CHECK(C1("camera_init", P(host_bus)) == ESP_ERR_INVALID_ARG && cam_inits == 0);
    CHECK(C1("camera_init", 0) == ESP_ERR_INVALID_ARG && cam_inits == 0);
    cam_sensor = false;
    CHECK(C1("camera_init", C0("tab5io_bus")) == (uint32_t)ESP_FAIL && cam_inits == 1 && cam_bus == host_bus);
    CHECK(C0("camera_present") == 0);
    CHECK(C0("camera_start") == ESP_ERR_INVALID_STATE);
    cam_sensor = true;
    CHECK(C1("camera_init", C0("tab5io_bus")) == ESP_OK && C0("camera_present") == 1);
    /* u-feckless-0060: brought up on the board's core, from the app's */
    CHECK(fake_core == 1 && cam_core_init == 0 && fake_tasks == 3 && fake_task_core == 0);   /* two inits and a refused start */

    /* not started: no size, and nothing written wherever dst points */
    wh[0] = wh[1] = 77;
    C4("camera_luma_size", 1, P(&wh[0]), P(&wh[1]), 0);
    CHECK(wh[0] == 0 && wh[1] == 0);
    CHECK(C4("camera_grab_luma", P(buf), 1, 0, 0) == ESP_ERR_INVALID_STATE && cam_grabs == 0);
    CHECK(C4("camera_grab_luma", 0x1000, 1, 0, 0) == ESP_ERR_INVALID_STATE && cam_grabs == 0);
    CHECK(C0("camera_stop") == ESP_OK && cam_stops == 0);

    /* u-feckless-0060: no task for the board's core, no start from this one */
    fake_task_fail = true;
    CHECK(C0("camera_start") == ESP_ERR_NO_MEM && cam_starts == 0 && fake_live == live0);
    fake_task_fail = false;
    CHECK(C0("camera_start") == ESP_OK && cam_starts == 1);
    CHECK(cam_core_start == 0 && fake_core == 1);
    C4("camera_luma_size", 1, P(&wh[0]), P(&wh[1]), 0);
    CHECK(wh[0] == 12 && wh[1] == 8);
    C4("camera_luma_size", 2, P(&wh[0]), 0, 0);                     /* either may be NULL */
    CHECK(wh[0] == 6 && wh[1] == 8);
    C4("camera_luma_size", 5, 0, P(&wh[1]), 0);
    CHECK(wh[0] == 6 && wh[1] == 2);
    C4("camera_luma_size", 0, P(&wh[0]), P(&wh[1]), 0);             /* a step below 1 is 1 */
    CHECK(wh[0] == 12 && wh[1] == 8);
    wh[0] = 77;
    C4("camera_luma_size", 1, P(&wh[0]), 0x1000, 0);                /* one out of the box: neither written */
    CHECK(wh[0] == 77);
    C4("camera_luma_size", 1, P(mem + MEM - 2), 0, 0);              /* an int across the box's end */

    /* a frame: 96 bytes at step 1, written in the box and nowhere past it */
    CHECK(C4("camera_grab_luma", P(buf), 1, P(&wh[0]), P(&wh[1])) == ESP_OK && cam_grabs == 1);
    CHECK(cam_dst == buf && wh[0] == 12 && wh[1] == 8);
    CHECK(cam_core_grab == 1);                                      /* a grab stays on the caller */
    CHECK(buf[0] == 0 && buf[13] == 13 && buf[95] == 95 && buf[96] == 0xee);
    memset(buf, 0xee, 128);
    CHECK(C4("camera_grab_luma", P(buf), 2, 0, 0) == ESP_OK && cam_grabs == 2);     /* w and h may be NULL */
    CHECK(buf[1] == 2 && buf[6] == 24 && buf[23] == 82 && buf[24] == 0xee);
    /* the buffer must hold the frame at the step asked: the host's size, not the app's */
    CHECK(C4("camera_grab_luma", P(mem + MEM - 95), 1, 0, 0) == ESP_ERR_INVALID_ARG && cam_grabs == 2);
    CHECK(C4("camera_grab_luma", P(mem + MEM - 96), 1, 0, 0) == ESP_OK && cam_grabs == 3);       /* to the last byte */
    CHECK(C4("camera_grab_luma", P(mem + MEM - 24), 2, 0, 0) == ESP_OK && cam_grabs == 4);
    CHECK(C4("camera_grab_luma", P(mem + MEM - 23), 2, 0, 0) == ESP_ERR_INVALID_ARG && cam_grabs == 4);
    CHECK(C4("camera_grab_luma", 0, 1, 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("camera_grab_luma", 0x1000, 1, 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("camera_grab_luma", P(&box), 1, 0, 0) == ESP_ERR_INVALID_ARG);         /* the host's memory */
    CHECK(C4("camera_grab_luma", P(buf), 1, 0x1000, 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("camera_grab_luma", P(buf), 1, 0, P(mem + MEM - 2)) == ESP_ERR_INVALID_ARG && cam_grabs == 4);
    /* the driver's own failure comes back, and w and h are left */
    cam_grab_result = ESP_ERR_TIMEOUT;
    wh[0] = wh[1] = 55;
    CHECK(C4("camera_grab_luma", P(buf), 1, P(&wh[0]), P(&wh[1])) == ESP_ERR_TIMEOUT && wh[0] == 55 && wh[1] == 55);
    cam_grab_result = ESP_OK;

    /* u-feckless-0060: a stop that cannot reach the board's core stops nothing,
     * and the app's end still does */
    fake_task_fail = true;
    CHECK(C0("camera_stop") == ESP_ERR_NO_MEM && cam_on && cam_stops == 0);
    fake_task_fail = false;
    CHECK(C0("camera_stop") == ESP_OK && cam_stops == 1 && !cam_on && cam_core_stop == 0);
    CHECK(C4("camera_grab_luma", P(buf), 1, 0, 0) == ESP_ERR_INVALID_STATE);
    CHECK(fake_crit_depth == 0);
    CHECK(flags_of("camera_grab_luma") == FECKLESS_UMODE_CALL_UNLOCKED);

    /* left streaming: stopped as the app ends; stopped by the app: left alone */
    CHECK(C0("camera_start") == ESP_OK && cam_on);
    feckless_box_calls_end();
    CHECK(!cam_on && cam_stops == 2);
    feckless_box_calls_end();
    CHECK(cam_stops == 2);
    /* u-feckless-0060: already on the board's core, the call is made in place */
    const int tasks = fake_tasks;
    fake_core = 0;
    CHECK(C0("camera_start") == ESP_OK && cam_core_start == 0 && fake_tasks == tasks);
    CHECK(C0("camera_stop") == ESP_OK && fake_tasks == tasks);
    fake_core = 1;
    CHECK(fake_live == live0);                                      /* every semaphore deleted */

    /* one pixel, and the angle */
    C4("gfx_px", 7, (uint32_t)-3, 0x1f800, 0);
    CHECK(d_args[0] == 7 && d_args[1] == -3 && d_args[2] == 0xf800);
    C1("gfx_set_rotation", 3);
    CHECK(C0("gfx_rotation") == 3 && d_rot == 3);
    C1("gfx_set_rotation", 0);
    CHECK(flags_of("gfx_px") == 0 && flags_of("gfx_rotation") == FECKLESS_UMODE_CALL_UNLOCKED);
}

/* u-feckless-0062: a UART, I2C devices and GPIO, against fake_periph.c */
#include "../src/box_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
extern int per_installs, per_deletes, per_install_core, per_delete_core, per_install_flags, per_uart_installed;
extern int per_cfg_baud, per_cfg_flags, per_cfgs, per_setpins, per_pin[6];
extern int per_reads, per_read_ticks[8], per_uart_rx, per_writes, per_write_max, per_written, per_wait_ticks;
extern const void *per_read_dst, *per_i2c_bus, *per_xfer_rd;
extern int per_gpio_cfgs, per_gpio_intr, per_gpio_resets, per_gpio_sets;
extern uint64_t per_gpio_mask, per_gpio_reset_mask;
extern int per_probes, per_probe_ms, per_adds, per_rms, per_xfers, per_xfer_ms, per_dev_live;
extern uint16_t per_probe_addr, per_xfer_addr;
extern esp_err_t per_add_result;
extern bool fake_stopping, fake_task_fail;

static void test_pins(void)
{
    /* the map's: GNSS on 6 and 7, PPS on 51; the Grove port's 53 and 54 */
    CHECK(box_pin_free(6) && box_pin_free(7) && box_pin_free(51) && box_pin_free(53) && box_pin_free(54));
    CHECK(box_pin_free(0) && box_pin_free(14) && box_pin_free(16) && box_pin_free(21) && box_pin_free(33) && box_pin_free(45));
    const int held[] = { 8, 9, 10, 11, 12, 13, 15, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                         34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44 };
    int free_n = 0;
    for (int p = 0; p < BOX_PIN_COUNT; p++) free_n += box_pin_free(p);
    for (unsigned i = 0; i < sizeof held / sizeof *held; i++) CHECK(!box_pin_free(held[i]));
    CHECK(free_n == BOX_PIN_COUNT - (int)(sizeof held / sizeof *held));
    CHECK(!box_pin_free(-1) && !box_pin_free(55) && !box_pin_free(1000));
    const uint32_t taken[] = { 0x10, 0x14, 0x32, 0x36, 0x40, 0x41, 0x43, 0x44, 0x55, 0x5d };
    for (unsigned i = 0; i < sizeof taken / sizeof *taken; i++) CHECK(!box_i2c_addr_free(taken[i]));
    CHECK(box_i2c_addr_free(0x68) && box_i2c_addr_free(0x69) && box_i2c_addr_free(0x08) && box_i2c_addr_free(0x77));
    CHECK(!box_i2c_addr_free(0) && !box_i2c_addr_free(0x07) && !box_i2c_addr_free(0x78) && !box_i2c_addr_free(0x168));
}

static void test_periph(void)
{
    const int live0 = fake_live;
    const uint32_t NC = (uint32_t)UART_PIN_NO_CHANGE;
    uint8_t *buf = (uint8_t *)(uintptr_t)feckless_box_alloc(&box, 512);

    /* install: UART 1-4, no queue, the host's flags, on the board's core */
    CHECK(call("uart_driver_install", 0, 2048, 0, 0, 0, 0, 0, 0) == ESP_ERR_INVALID_ARG);       /* the console's */
    CHECK(call("uart_driver_install", 5, 2048, 0, 0, 0, 0, 0, 0) == ESP_ERR_INVALID_ARG);       /* the LP UART */
    CHECK(call("uart_driver_install", 1, 65537, 0, 0, 0, 0, 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(call("uart_driver_install", 1, 2048, 0, 8, P(buf), 0, 0, 0) == ESP_ERR_NOT_SUPPORTED);
    per_uart_installed = 1 << 2;                                    /* the launcher has UART2 */
    CHECK(call("uart_driver_install", 2, 2048, 0, 0, 0, 0, 0, 0) == ESP_ERR_INVALID_STATE);
    uart_config_t *cfg = (uart_config_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *cfg);
    memset(cfg, 0, sizeof *cfg);
    cfg->baud_rate = 38400;
    cfg->flags.allow_pd = 1;
    CHECK(C2("uart_param_config", 2, P(cfg)) == ESP_ERR_INVALID_ARG);                         /* not the app's */
    CHECK(call("_uart_set_pin6", 2, 7, 6, NC, NC, NC, NC, 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("uart_read_bytes", 2, P(buf), 16, 0) == (uint32_t)-1);
    CHECK(C1("uart_driver_delete", 2) == ESP_ERR_INVALID_ARG && per_deletes == 0);
    CHECK(per_installs == 0 && per_cfgs == 0 && per_setpins == 0 && per_reads == 0);
    fake_task_fail = true;                                          /* no task on core 0: not installed */
    CHECK(call("uart_driver_install", 1, 2048, 0, 0, 0, 0x400, 0, 0) == ESP_ERR_NO_MEM && per_installs == 0);
    fake_task_fail = false;
    CHECK(C4("uart_read_bytes", 1, P(buf), 16, 0) == (uint32_t)-1 && per_reads == 0);          /* before the install */
    CHECK(call("uart_driver_install", 1, 2048, 0, 0, 0, 0x400, 0, 0) == ESP_OK);
    CHECK(per_installs == 1 && per_install_core == 0 && per_install_flags == 0);
    CHECK(call("uart_driver_install", 1, 2048, 0, 0, 0, 0, 0, 0) == ESP_ERR_INVALID_STATE && per_installs == 1);

    /* the configuration is a copy, its sleep flags cleared */
    CHECK(C2("uart_param_config", 1, P(cfg)) == ESP_OK && per_cfg_baud == 38400 && per_cfg_flags == 0);
    CHECK(C2("uart_param_config", 1, 0x1000) == ESP_ERR_INVALID_ARG);
    CHECK(C2("uart_param_config", 1, P(mem + MEM - 4)) == ESP_ERR_INVALID_ARG && per_cfgs == 1);
    CHECK(C2("uart_param_config", 3, P(cfg)) == ESP_OK && per_cfgs == 2);                      /* before an install, as IDF allows */

    /* pins: free ones, or no change */
    CHECK(call("_uart_set_pin6", 1, 7, 6, NC, NC, NC, NC, 0) == ESP_OK && per_setpins == 1);
    CHECK(per_pin[0] == 7 && per_pin[1] == 6 && per_pin[2] == -1 && per_pin[5] == -1);
    CHECK(call("_uart_set_pin6", 1, 31, 6, NC, NC, NC, NC, 0) == ESP_ERR_INVALID_ARG);          /* the board's SDA */
    CHECK(call("_uart_set_pin6", 1, 7, 6, NC, NC, NC, 43, 0) == ESP_ERR_INVALID_ARG);           /* the card's clock */
    CHECK(call("_uart_set_pin6", 1, 7, 55, NC, NC, NC, NC, 0) == ESP_ERR_INVALID_ARG && per_setpins == 1);

    /* a read lands in the box, in slices, and ends with the first bytes */
    per_uart_rx = 5;
    CHECK(C4("uart_read_bytes", 1, P(buf), 16, 20) == 5 && per_reads == 1 && per_read_ticks[0] == 20);
    CHECK(per_read_dst == buf && buf[0] == 0xA1 && buf[4] == 0xA1);
    CHECK(C4("uart_read_bytes", 1, P(buf), 16, 250) == 0 && per_reads == 4);                   /* 100, 100, 50 */
    CHECK(per_read_ticks[1] == 100 && per_read_ticks[2] == 100 && per_read_ticks[3] == 50);
    fake_stopping = true;
    CHECK(C4("uart_read_bytes", 1, P(buf), 16, portMAX_DELAY) == 0 && per_reads == 5);         /* forever ends with the app */
    fake_stopping = false;
    CHECK(C4("uart_read_bytes", 1, 0x1000, 16, 0) == (uint32_t)-1);
    CHECK(C4("uart_read_bytes", 1, P(mem + MEM - 8), 16, 0) == (uint32_t)-1 && per_reads == 5);
    CHECK(C4("uart_read_bytes", 1, P(mem + MEM - 16), 16, 0) == 0 && per_reads == 6);          /* to the last byte */

    /* a write goes in pieces; waiting for the transmitter is capped */
    CHECK(C4("uart_write_bytes", 1, P(buf), 500, 0) == 500 && per_writes == 2 && per_write_max == 256 && per_written == 500);
    CHECK(C4("uart_write_bytes", 1, 0x1000, 8, 0) == (uint32_t)-1 && per_writes == 2);
    CHECK(C4("uart_write_bytes", 3, P(buf), 8, 0) == (uint32_t)-1 && per_writes == 2);         /* configured, not installed */
    CHECK(C2("uart_wait_tx_done", 1, portMAX_DELAY) == ESP_OK && per_wait_ticks == 2000);
    CHECK(C2("uart_wait_tx_done", 1, 100) == ESP_OK && per_wait_ticks == 100);
    uint32_t *len = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    per_uart_rx = 9;
    CHECK(C2("uart_get_buffered_data_len", 1, P(len)) == ESP_OK && *len == 9);
    CHECK(C2("uart_get_buffered_data_len", 1, 0x1000) == ESP_ERR_INVALID_ARG);
    CHECK(C1("uart_flush_input", 1) == ESP_OK && C1("uart_flush_input", 4) == ESP_ERR_INVALID_ARG);
    per_uart_rx = 0;

    /* I2C: the host's bus by its number; an address no driver holds */
    const uint32_t bus = C0("tab5io_bus");
    CHECK(C4("i2c_master_probe", P(host_bus), 0x68, 50, 0) == ESP_ERR_INVALID_ARG && per_probes == 0);
    CHECK(C4("i2c_master_probe", bus, 0x43, 50, 0) == ESP_ERR_INVALID_ARG && per_probes == 0);  /* an expander */
    CHECK(C4("i2c_master_probe", bus, 0x68, (uint32_t)-1, 0) == ESP_OK && per_probes == 1);
    CHECK(per_i2c_bus == host_bus && per_probe_addr == 0x68 && per_probe_ms == 2000);
    i2c_device_config_t *dc = (i2c_device_config_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *dc);
    memset(dc, 0, sizeof *dc);
    dc->device_address = 0x68;
    dc->scl_speed_hz = 400000;
    uint32_t *dev = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4 * 6);
    CHECK(C4("i2c_master_bus_add_device", 0, P(dc), P(dev), 0) == ESP_ERR_INVALID_ARG);
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), 0x1000, 0) == ESP_ERR_INVALID_ARG);
    dc->device_address = 0x32;                                      /* the RTC */
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(dev), 0) == ESP_ERR_INVALID_ARG);
    dc->device_address = 0x68;
    dc->scl_speed_hz = 1000000;
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(dev), 0) == ESP_ERR_INVALID_ARG);
    dc->scl_speed_hz = 400000;
    dc->dev_addr_length = I2C_ADDR_BIT_LEN_10;
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(dev), 0) == ESP_ERR_INVALID_ARG && per_adds == 0);
    dc->dev_addr_length = I2C_ADDR_BIT_LEN_7;
    per_add_result = ESP_FAIL;
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(dev), 0) == (uint32_t)ESP_FAIL && dev[0] == 0);
    per_add_result = ESP_OK;
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(dev), 0) == ESP_OK && per_i2c_bus == host_bus);
    CHECK(dev[0] != 0 && (dev[0] < P(mem) || dev[0] >= P(mem) + MEM));                         /* a number, not a pointer */
    CHECK(call("i2c_master_transmit_receive", dev[0], P(buf), 1, P(buf + 8), 6, 100, 0, 0) == ESP_OK);
    CHECK(per_xfers == 1 && per_xfer_addr == 0x68 && per_xfer_rd == buf + 8 && buf[8] == 0x5a && per_xfer_ms == 100);
    CHECK(C4("i2c_master_transmit", dev[0], P(buf), 2, (uint32_t)-1) == ESP_OK && per_xfer_ms == 2000);
    CHECK(C4("i2c_master_receive", dev[0], P(buf), 4, 10) == ESP_OK && per_xfers == 3);
    CHECK(C4("i2c_master_transmit", dev[0] + 1, P(buf), 2, 10) == ESP_ERR_INVALID_ARG);         /* no such device */
    CHECK(C4("i2c_master_transmit", P(host_bus), P(buf), 2, 10) == ESP_ERR_INVALID_ARG);
    CHECK(C4("i2c_master_transmit", dev[0], 0x1000, 2, 10) == ESP_ERR_INVALID_ARG);
    CHECK(C4("i2c_master_transmit", dev[0], P(buf), 16385, 10) == ESP_ERR_INVALID_ARG);
    CHECK(call("i2c_master_transmit_receive", dev[0], P(buf), 1, P(mem + MEM - 2), 6, 100, 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(per_xfers == 3);
    for (int i = 1; i < 4; i++) {
        dc->device_address = (uint16_t)(0x68 + i);
        CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(&dev[i]), 0) == ESP_OK && dev[i] != dev[i - 1]);
    }
    CHECK(C4("i2c_master_bus_add_device", bus, P(dc), P(&dev[4]), 0) == ESP_ERR_NO_MEM && per_dev_live == 4);
    CHECK(C1("i2c_master_bus_rm_device", dev[1]) == ESP_OK && per_dev_live == 3);
    CHECK(C1("i2c_master_bus_rm_device", dev[1]) == ESP_ERR_INVALID_ARG && per_rms == 1);
    CHECK(C4("i2c_master_receive", dev[1], P(buf), 4, 10) == ESP_ERR_INVALID_ARG);

    /* GPIO: free pins, and never an interrupt */
    gpio_config_t *gc = (gpio_config_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *gc);
    memset(gc, 0, sizeof *gc);
    gc->pin_bit_mask = 1ull << 51;
    gc->intr_type = GPIO_INTR_POSEDGE;
    CHECK(C1("gpio_config", P(gc)) == ESP_OK && per_gpio_mask == 1ull << 51 && per_gpio_intr == GPIO_INTR_DISABLE);
    CHECK(gc->intr_type == GPIO_INTR_POSEDGE);                      /* the app's copy untouched */
    gc->pin_bit_mask = 1ull << 51 | 1ull << 22;                     /* and the backlight */
    CHECK(C1("gpio_config", P(gc)) == ESP_ERR_INVALID_ARG);
    gc->pin_bit_mask = 1ull << 60;
    CHECK(C1("gpio_config", P(gc)) == ESP_ERR_INVALID_ARG);
    gc->pin_bit_mask = 0;
    CHECK(C1("gpio_config", P(gc)) == ESP_ERR_INVALID_ARG);
    CHECK(C1("gpio_config", 0x1000) == ESP_ERR_INVALID_ARG && per_gpio_cfgs == 1);
    CHECK(C2("gpio_set_level", 53, 1) == ESP_OK && C2("gpio_set_level", 15, 0) == ESP_ERR_INVALID_ARG && per_gpio_sets == 1);
    CHECK(C1("gpio_get_level", 53) == 1 && C1("gpio_get_level", 23) == 0);
    CHECK(C2("gpio_set_direction", 31, 3) == ESP_ERR_INVALID_ARG && C1("gpio_reset_pin", 44) == ESP_ERR_INVALID_ARG);
    CHECK(per_gpio_resets == 0);

    /* the app ends: its driver deleted on the board's core, its devices
     * removed, its pins reset -- and only its own */
    feckless_box_calls_end();
    CHECK(per_deletes == 1 && per_delete_core == 0 && per_uart_installed == 1 << 2);
    CHECK(per_dev_live == 0 && per_rms == 4);
    CHECK(per_gpio_reset_mask == (1ull << 6 | 1ull << 7 | 1ull << 51 | 1ull << 53) && per_gpio_resets == 4);
    CHECK(C4("uart_read_bytes", 1, P(buf), 16, 0) == (uint32_t)-1);
    CHECK(C4("i2c_master_receive", dev[0], P(buf), 4, 10) == ESP_ERR_INVALID_ARG);
    per_gpio_resets = 0;
    feckless_box_calls_end();                                       /* again: nothing left to undo */
    CHECK(per_deletes == 1 && per_gpio_resets == 0);
    per_uart_installed = 0;
    CHECK(fake_live == live0);                                      /* every semaphore deleted */
    CHECK(flags_of("uart_read_bytes") == FECKLESS_UMODE_CALL_UNLOCKED && flags_of("uart_driver_install") == 0);
    CHECK(flags_of("i2c_master_transmit_receive") == FECKLESS_UMODE_CALL_UNLOCKED);
}

/* u-feckless-0065: the hardware JPEG decoder, against fake_periph.c */
#include "driver/jpeg_decode.h"
extern int jpg_news, jpg_dels, jpg_infos, jpg_procs, jpg_new_timeout, jpg_proc_fmt;
extern esp_err_t jpg_proc_result;
extern const void *jpg_proc_in, *jpg_proc_out, *jpg_info_in;
extern uint32_t jpg_proc_out_size;

static void test_jpeg(void)
{
    CHECK(flags_of("jpeg_decoder_process") == FECKLESS_UMODE_CALL_UNLOCKED && flags_of("jpeg_alloc_decoder_mem") == 0);
    jpeg_decode_engine_cfg_t *ec = (jpeg_decode_engine_cfg_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *ec);
    memset(ec, 0, sizeof *ec);
    ec->timeout_ms = -1;                                            /* forever: not given */
    uint32_t *h = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    CHECK(C2("jpeg_new_decoder_engine", P(ec), 0x1000) == ESP_ERR_INVALID_ARG && jpg_news == 0);
    CHECK(C2("jpeg_new_decoder_engine", P(ec), P(h)) == ESP_OK && jpg_news == 1 && jpg_new_timeout == 2000);
    CHECK(*h != 0 && (*h < P(mem) || *h >= P(mem) + MEM));          /* a number, not the host's pointer */
    const uint32_t eng = *h;
    CHECK(C2("jpeg_new_decoder_engine", P(ec), P(h)) == ESP_OK && *h == eng && jpg_news == 1);   /* the one engine */

    /* memory in the box, on the line, whole lines */
    jpeg_decode_memory_alloc_cfg_t *mc = (jpeg_decode_memory_alloc_cfg_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *mc);
    mc->buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    uint32_t *got = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    const uint32_t outp = C4("jpeg_alloc_decoder_mem", 1000, P(mc), P(got), 0);
    CHECK(outp && outp % 128 == 0 && *got == 1024 && outp >= P(mem) && outp + 1024 <= P(mem) + MEM);
    CHECK(feckless_box_len(&box, outp) == 1024);
    CHECK(C4("jpeg_alloc_decoder_mem", 0, P(mc), P(got), 0) == 0);
    CHECK(C4("jpeg_alloc_decoder_mem", 64, 0x1000, P(got), 0) == 0);
    CHECK(C4("jpeg_alloc_decoder_mem", 64, P(mc), 0x1000, 0) == 0);
    CHECK(C4("jpeg_alloc_decoder_mem", MEM, P(mc), P(got), 0) == 0 && *got == 0);   /* does not fit */
    mc->buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    const uint32_t inp = C4("jpeg_alloc_decoder_mem", 300, P(mc), P(got), 0);
    CHECK(inp && inp % 128 == 0 && *got == 384);
    memset((void *)(uintptr_t)inp, 0xff, 300);

    /* the header, read in place */
    jpeg_decode_picture_info_t *pi = (jpeg_decode_picture_info_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *pi);
    CHECK(C4("jpeg_decoder_get_info", inp, 300, P(pi), 0) == ESP_OK && jpg_info_in == (void *)(uintptr_t)inp);
    CHECK(pi->width == 640 && pi->height == 480);
    CHECK(C4("jpeg_decoder_get_info", P(mem) + MEM - 100, 300, P(pi), 0) == ESP_ERR_INVALID_ARG);  /* runs past the box */
    CHECK(C4("jpeg_decoder_get_info", inp, 300, 0x1000, 0) == ESP_ERR_INVALID_ARG && jpg_infos == 1);

    /* the decode: the app's buffers, in place */
    jpeg_decode_cfg_t *dc = (jpeg_decode_cfg_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *dc);
    memset(dc, 0, sizeof *dc);
    dc->output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    uint32_t *dec = (uint32_t *)(uintptr_t)feckless_box_alloc(&box, 4);
    CHECK(call("jpeg_decoder_process", eng, P(dc), inp, 300, outp, 1024, P(dec), 0) == ESP_OK);
    CHECK(jpg_procs == 1 && jpg_proc_in == (void *)(uintptr_t)inp && jpg_proc_out == (void *)(uintptr_t)outp);
    CHECK(jpg_proc_out_size == 1024 && jpg_proc_fmt == JPEG_DECODE_OUT_FORMAT_RGB565 && *dec == 1024);
    CHECK(((uint8_t *)(uintptr_t)outp)[1023] == 0x77);
    CHECK(call("jpeg_decoder_process", eng + 1, P(dc), inp, 300, outp, 1024, P(dec), 0) == ESP_ERR_INVALID_ARG);
    CHECK(call("jpeg_decoder_process", eng, 0x1000, inp, 300, outp, 1024, P(dec), 0) == ESP_ERR_INVALID_ARG);
    CHECK(call("jpeg_decoder_process", eng, P(dc), inp, 0, outp, 1024, P(dec), 0) == ESP_ERR_INVALID_ARG);
    CHECK(call("jpeg_decoder_process", eng, P(dc), inp, 300, P(mem) + MEM - 512, 1024, P(dec), 0) == ESP_ERR_INVALID_ARG);
    CHECK(call("jpeg_decoder_process", eng, P(dc), 0x1000, 300, outp, 1024, P(dec), 0) == ESP_ERR_INVALID_ARG);
    CHECK(call("jpeg_decoder_process", eng, P(dc), inp, 300, outp, 1024, 0x1000, 0) == ESP_ERR_INVALID_ARG);
    CHECK(jpg_procs == 1);
    jpg_proc_result = ESP_ERR_TIMEOUT;
    CHECK(call("jpeg_decoder_process", eng, P(dc), inp, 300, outp, 1024, P(dec), 0) == (uint32_t)ESP_ERR_TIMEOUT && *dec == 0);
    jpg_proc_result = ESP_OK;

    /* the app frees what it was given, as any allocation */
    CHECK(C1("free", outp) == 0 && feckless_box_len(&box, outp) == 0);
    CHECK(C1("free", inp) == 0);

    /* deleted: the number stops working; the engine is the host's until the end */
    CHECK(C1("jpeg_del_decoder_engine", eng) == ESP_OK && jpg_dels == 0);
    CHECK(C1("jpeg_del_decoder_engine", eng) == ESP_ERR_INVALID_ARG);
    CHECK(call("jpeg_decoder_process", eng, P(dc), inp, 300, outp, 1024, P(dec), 0) == ESP_ERR_INVALID_ARG);
    feckless_box_calls_end();
    CHECK(jpg_dels == 1);
    feckless_box_calls_end();
    CHECK(jpg_dels == 1);                                           /* nothing left to delete */
}

/* u-feckless-0026: the table's names are feckless_box_libc's
 * boxcalls_names.txt, which boxcheck holds every import of a boxed ELF to:
 * the same set, one name a line. */
static void test_names_file(void)
{
    FILE *f = fopen("../../feckless_box_libc/boxcalls_names.txt", "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[128];
    uint32_t listed = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        listed++;
        uint32_t i = 0;
        while (i < ncalls && strcmp(calls_tab[i].name, line)) i++;
        if (i == ncalls) { printf("FAIL boxcalls_names.txt has %s, the table does not\n", line); failed++; }
    }
    fclose(f);
    if (listed != ncalls) { printf("FAIL boxcalls_names.txt has %u names, the table %u\n", listed, ncalls); failed++; }
}

int main(int argc, char **argv)
{
    mem = mmap(NULL, MEM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED) { printf("no memory below 4 GB\n"); return 1; }
    feckless_box_init(&box, (uintptr_t)mem, MEM, 4096);
    calls_tab = feckless_box_calls_v1(&ncalls);
    CHECK(feckless_box_calls_v1(&ncalls) == calls_tab);             /* built once */
    tab = (feckless_umode_calls_t){ calls_tab, ncalls, &box };
    w.lo = (uintptr_t)mem;
    w.hi = (uintptr_t)mem + MEM;
    w.ext_arg = &tab;
    if (argc > 1 && !strcmp(argv[1], "--names")) {     /* to write boxcalls_names.txt */
        for (uint32_t i = 0; i < ncalls; i++) printf("%s\n", calls_tab[i].name);
        return 0;
    }
    test_table();
    test_names_file();
    test_libgcc();
    test_double_math();
    test_err_name();
    test_console_time();
    test_log();
    test_give_up();
    test_board();
    test_gfx();
    test_audio();
    test_fb();
    test_threads();
    test_rtos();
    test_drivers();
    test_codec();
    test_nvs();
    test_camera();
    test_pins();
    test_periph();
    test_jpeg();
    printf(failed ? "%d FAILED\n" : "box_calls: all passed (%u calls)\n", failed ? failed : (int)ncalls);
    return failed != 0;
}
