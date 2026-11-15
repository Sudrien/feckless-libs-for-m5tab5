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

#define MEM (64 * 1024)
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
    CHECK(data == 9);       /* abi_v1, box_v1, storage_v2; Ark12's six tables (u-feckless-0021) */
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
    printf(failed ? "%d FAILED\n" : "box_calls: all passed (%u calls)\n", failed ? failed : (int)ncalls);
    return failed != 0;
}
