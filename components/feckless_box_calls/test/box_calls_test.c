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
    CHECK(s_core == 0x7fffffff);
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
    printf(failed ? "%d FAILED\n" : "box_calls: all passed (%u calls)\n", failed ? failed : (int)ncalls);
    return failed != 0;
}
