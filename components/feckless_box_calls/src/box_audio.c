/* SPDX-License-Identifier: MIT
 *
 * Audio and USB host calls for a boxed app (u-feckless-0021): the speaker
 * and microphones (audio_out), a USB audio device (uac) and the USB host
 * port it hangs off (usbhost).
 *
 * The drivers' handles never reach the app: tab5io_exp1() and tab5io_exp2()
 * answer tokens (box_calls.c), which audio_out_init() and usbhost_init()
 * here take back. Every buffer is checked to lie in the box for its whole
 * length before the driver writes or reads it; the drivers copy with the
 * CPU (their DMA has its own buffers), so the box's PSRAM is never a DMA
 * target. Strings the drivers own (product names) are copied into a buffer
 * in the box.
 *
 * What the app starts, the end of the app stops: a capture, a USB stream,
 * an open USB microphone (box_audio_end()).
 */
#include <string.h>

#include "esp_err.h"
#include "audio_out.h"
#include "battery.h"            /* u-feckless-0046 */
#include "polyrsp.h"
#include "rtc8130.h"
#include "tab5io.h"
#include "uac.h"
#include "usbhost.h"

#include "box_calls_priv.h"

#define TIMEOUT_MAX_MS  2000u       /* a read or write blocks the app's task, no longer */
#define PRODUCT_MAX     128u

static bool s_capturing, s_streaming, s_mic_open;
static bool s_played;               /* u-feckless-0046: the app wrote PCM */
static uint32_t s_route_name;       /* audio_out_route_name()'s copy in the box */
static uint8_t s_mic_ch;            /* the open microphone's channels */
static uint32_t s_product, s_mic_product;  /* box buffers for the product strings */

static uint32_t clamp_ms(uint32_t ms) { return ms < TIMEOUT_MAX_MS ? ms : TIMEOUT_MAX_MS; }

/* n items of size bytes at p, all in the box, without overflowing. */
static bool ok_array(const win_t *w, uint32_t p, uint32_t n, uint32_t size)
{
    if (n == 0) return true;
    if (n > UINT32_MAX / size) return false;
    return ok_ptr(w, p, n * size);
}

/* A host string, copied into the box buffer at *slot (allocated, or again
 * if the app freed it); the app's pointer to it, or 0 if the box is full. */
static uint32_t to_box(win_t *w, uint32_t *slot, const char *s)
{
    feckless_box_t *b = box_of_win(w);
    if (!*slot || feckless_box_len(b, *slot) != PRODUCT_MAX) {
        *slot = (uint32_t)feckless_box_alloc(b, PRODUCT_MAX);
        if (!*slot) return 0;
    }
    char *d = (char *)(uintptr_t)*slot;
    strncpy(d, s ? s : "", PRODUCT_MAX - 1);
    d[PRODUCT_MAX - 1] = '\0';
    return *slot;
}

/* ---- audio_out ----------------------------------------------------------- */

H(c_audio_out_init)
{
    (void)w;
    *ret = (a[0] == BOX_TOKEN_BUS && a[1] == BOX_TOKEN_EXP1)
         ? (uint32_t)audio_out_init(tab5io_bus(), tab5io_exp1(), a[2])
         : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}
H(c_audio_out_set_volume) { (void)w; *ret = (uint32_t)audio_out_set_volume((uint8_t)a[0]); return true; }
H(c_audio_out_headphones) { (void)w; (void)a; *ret = audio_out_headphones(); return true; }
H(c_audio_out_capture_begin)
{
    (void)w;
    const esp_err_t e = audio_out_capture_begin((audio_capture_src_t)a[0]);
    if (e == ESP_OK) s_capturing = true;
    *ret = (uint32_t)e;
    return true;
}
/* The buffer holds max_frames * 2 int32 whichever source runs (audio_out.h). */
H(c_audio_out_capture_read)
{
    if (!ok_array(w, a[0], a[1], 2 * sizeof(int32_t))) {
        *ret = 0;
        return true;
    }
    *ret = (uint32_t)audio_out_capture_read((int32_t *)(uintptr_t)a[0], a[1], clamp_ms(a[2]));
    return true;
}
H(c_audio_out_capture_end)
{
    (void)w; (void)a;
    audio_out_capture_end();
    s_capturing = false;
    *ret = 0;
    return true;
}
H(c_audio_out_monitor_write)
{
    if (!ok_array(w, a[0], a[1], 2 * sizeof(int16_t))) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    *ret = (uint32_t)audio_out_monitor_write((const int16_t *)(uintptr_t)a[0], a[1]);
    return true;
}

/* ---- audio_out: playback (u-feckless-0046) ----------------------------------
 * As Defeatist's writer uses it. audio_out_write() copies the PCM into the
 * driver's DMA buffers with the CPU (or hands it to uac_write(), which
 * copies too), so the app's buffer in PSRAM is read in place; WRITE_MAX
 * bounds how long one call can hold the writer's task. */
#define WRITE_MAX (64u * 1024u)

H(c_audio_out_write)
{
    if (a[1] > WRITE_MAX || !ok_ptr(w, a[0], a[1])) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    s_played = true;
    *ret = a[1] ? (uint32_t)audio_out_write((const void *)(uintptr_t)a[0], a[1]) : (uint32_t)ESP_OK;
    return true;
}
H(c_audio_out_set_format) { (void)w; *ret = (uint32_t)audio_out_set_format(a[0], (uint8_t)a[1]); return true; }
H(c_audio_out_rate_supported) { (void)w; *ret = audio_out_rate_supported(a[0]); return true; }
H(c_audio_out_rate) { (void)w; (void)a; *ret = audio_out_rate(); return true; }
H(c_audio_out_set_mute) { (void)w; audio_out_set_mute(a[0] != 0); *ret = 0; return true; }
H(c_audio_out_muted) { (void)w; (void)a; *ret = audio_out_muted(); return true; }
H(c_audio_out_set_idle) { (void)w; audio_out_set_idle(a[0] != 0); *ret = 0; return true; }
H(c_audio_out_route) { (void)w; (void)a; *ret = (uint32_t)audio_out_route(); return true; }
H(c_audio_out_route_name) { (void)a; *ret = to_box(w, &s_route_name, audio_out_route_name()); return true; }

/* ---- the battery and the clock (u-feckless-0046) --------------------------- */

H(c_battery_init)
{
    (void)w;
    *ret = a[0] == BOX_TOKEN_BUS ? (uint32_t)battery_init(tab5io_bus()) : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}
H(c_battery_start) { (void)w; (void)a; *ret = (uint32_t)battery_start(); return true; }
H(c_battery_pct) { (void)w; (void)a; *ret = (uint32_t)battery_pct(); return true; }
H(c_battery_mv) { (void)w; (void)a; *ret = (uint32_t)battery_mv(); return true; }
H(c_battery_charging) { (void)w; (void)a; *ret = battery_charging(); return true; }
H(c_battery_external) { (void)w; (void)a; *ret = battery_external(); return true; }
/* The reason is logged later, from the trace's task: a copy of the host's,
 * never the app's pointer, which may be gone by then (and the box with it). */
H(c_battery_trace_arm)
{
    static char why[48];
    const char *s = str(w, a[0], FECKLESS_BOX_STR_MAX);
    strncpy(why, s ? s : "app", sizeof why - 1);
    why[sizeof why - 1] = '\0';
    battery_trace_arm(why);
    *ret = 0;
    return true;
}

H(c_rtc8130_init)
{
    (void)w;
    *ret = a[0] == BOX_TOKEN_BUS && rtc8130_init(tab5io_bus());
    return true;
}
/* (int64_t *epoch) */
H(c_rtc8130_read)
{
    int64_t t = 0;
    if (!ok_ptr(w, a[0], sizeof t)) {
        *ret = false;
        return true;
    }
    const bool ok = rtc8130_read(&t);
    if (ok) memcpy((void *)(uintptr_t)a[0], &t, sizeof t);
    *ret = ok;
    return true;
}
/* (int64_t epoch): two registers, low word first */
H(c_rtc8130_write) { (void)w; *ret = rtc8130_write((int64_t)((uint64_t)a[1] << 32 | a[0])); return true; }
H(c_rtc8130_write_forward)
{
    (void)w;
    *ret = rtc8130_write_forward((int64_t)((uint64_t)a[1] << 32 | a[0]));
    return true;
}

/* ---- polyrsp (u-feckless-0046) ---------------------------------------------
 * The resampler's state is the host's (its history and coefficients, and
 * the pointers to them), so the app holds a number. It runs on the calling
 * thread's host task, in M-mode: interrupts are not held off while it
 * computes, as they would be in a window. Locked: two threads in one
 * resampler could walk its history out of its buffer. */
#define RSP_MAX   4
#define TOKEN_RSP 0x52535000u               /* "RSP\0" + slot */

static struct {
    polyrsp_t *r;
    uint32_t in_rate, out_rate, max_in;
} s_rsp[RSP_MAX];

static int rsp_of(uint32_t tok)
{
    const uint32_t i = tok - TOKEN_RSP;
    return (tok >= TOKEN_RSP && i < RSP_MAX && s_rsp[i].r) ? (int)i : -1;
}

/* (in rate, out rate, max in frames) */
H(c_polyrsp_open)
{
    (void)w;
    *ret = 0;
    int i = 0;
    while (i < RSP_MAX && s_rsp[i].r) i++;
    if (i == RSP_MAX || a[0] == 0 || a[1] == 0 || a[2] == 0 || a[2] > (1u << 20)) return true;
    polyrsp_t *r = polyrsp_open(a[0], a[1], a[2]);
    if (!r) return true;
    s_rsp[i].r = r;
    s_rsp[i].in_rate = a[0];
    s_rsp[i].out_rate = a[1];
    s_rsp[i].max_in = a[2];
    *ret = TOKEN_RSP + (uint32_t)i;
    return true;
}
H(c_polyrsp_close)
{
    (void)w;
    const int i = rsp_of(a[0]);
    if (i >= 0) {
        polyrsp_close(s_rsp[i].r);
        memset(&s_rsp[i], 0, sizeof s_rsp[i]);
    }
    *ret = 0;
    return true;
}
H(c_polyrsp_reset)
{
    (void)w;
    const int i = rsp_of(a[0]);
    if (i >= 0) polyrsp_reset(s_rsp[i].r);
    *ret = 0;
    return true;
}
H(c_polyrsp_max_out)
{
    (void)w;
    const int i = rsp_of(a[0]);
    *ret = i >= 0 ? polyrsp_max_out(s_rsp[i].r) : 0;
    return true;
}
/* (r, in *, in frames, out *): stereo int16. out must hold what in_frames
 * can make -- its exact share rounded up, and one more (polyrsp.h). */
H(c_polyrsp_process)
{
    *ret = 0;
    const int i = rsp_of(a[0]);
    if (i < 0 || a[2] == 0 || a[2] > s_rsp[i].max_in) return true;
    const uint64_t most = ((uint64_t)a[2] * s_rsp[i].out_rate + s_rsp[i].in_rate - 1) / s_rsp[i].in_rate + 1;
    if (!ok_array(w, a[1], a[2], 2 * sizeof(int16_t)) || most > UINT32_MAX ||
        !ok_array(w, a[3], (uint32_t)most, 2 * sizeof(int16_t))) {
        return true;
    }
    *ret = polyrsp_process(s_rsp[i].r, (const int16_t *)(uintptr_t)a[1], a[2], (int16_t *)(uintptr_t)a[3]);
    return true;
}

/* ---- usbhost --------------------------------------------------------------- */

H(c_usbhost_init)
{
    (void)w;
    *ret = a[0] == BOX_TOKEN_EXP2 ? (uint32_t)usbhost_init(tab5io_exp2())
                                  : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}
H(c_usbhost_start) { (void)w; (void)a; usbhost_start(); *ret = 0; return true; }
H(c_usbhost_running) { (void)w; (void)a; *ret = usbhost_running(); return true; }    /* u-feckless-0046 */
H(c_usbhost_vbus_on) { (void)w; (void)a; *ret = usbhost_vbus_on(); return true; }

/* ---- uac ------------------------------------------------------------------ */

H(c_uac_init) { (void)w; (void)a; *ret = (uint32_t)uac_init(); return true; }
H(c_uac_present) { (void)w; (void)a; *ret = uac_present(); return true; }
H(c_uac_generation) { (void)w; (void)a; *ret = uac_generation(); return true; }   /* u-feckless-0023 */
H(c_uac_stream_start)
{
    (void)w;
    const esp_err_t e = uac_stream_start(a[0], (uint8_t)a[1]);
    if (e == ESP_OK) s_streaming = true;
    *ret = (uint32_t)e;
    return true;
}
H(c_uac_stream_stop) { (void)w; (void)a; uac_stream_stop(); s_streaming = false; *ret = 0; return true; }
H(c_uac_write)
{
    if (!ok_ptr(w, a[0], a[1])) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    *ret = (uint32_t)uac_write((const void *)(uintptr_t)a[0], a[1], clamp_ms(a[2]));
    return true;
}
H(c_uac_product) { (void)a; *ret = to_box(w, &s_product, uac_product()); return true; }
H(c_uac_mic_announced) { (void)w; (void)a; *ret = uac_mic_announced(); return true; }

/* (rate *, channels *, max_bits, bits * or NULL). Through locals: the app's
 * memory is read once and written once. */
H(c_uac_mic_open_bits)
{
    if (!ok_ptr(w, a[0], sizeof(uint32_t)) || !ok_ptr(w, a[1], 1) || (a[3] && !ok_ptr(w, a[3], 1))) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    uint32_t rate;
    uint8_t ch, bits = 0;
    memcpy(&rate, (const void *)(uintptr_t)a[0], sizeof rate);
    memcpy(&ch, (const void *)(uintptr_t)a[1], 1);
    const esp_err_t e = uac_mic_open_bits(&rate, &ch, (uint8_t)a[2], &bits);
    if (e == ESP_OK) {
        s_mic_open = true;
        s_mic_ch = ch;
    }
    memcpy((void *)(uintptr_t)a[0], &rate, sizeof rate);
    memcpy((void *)(uintptr_t)a[1], &ch, 1);
    if (a[3]) memcpy((void *)(uintptr_t)a[3], &bits, 1);
    *ret = (uint32_t)e;
    return true;
}
H(c_uac_mic_read)
{
    if (!s_mic_open || !ok_array(w, a[0], a[1], (uint32_t)s_mic_ch * sizeof(int32_t))) {
        *ret = 0;
        return true;
    }
    *ret = (uint32_t)uac_mic_read((int32_t *)(uintptr_t)a[0], a[1], clamp_ms(a[2]));
    return true;
}
H(c_uac_mic_gone) { (void)w; (void)a; *ret = uac_mic_gone(); return true; }
H(c_uac_mic_close)
{
    (void)w; (void)a;
    uac_mic_close();
    s_mic_open = false;
    s_mic_ch = 0;
    *ret = 0;
    return true;
}
H(c_uac_mic_product) { (void)a; *ret = to_box(w, &s_mic_product, uac_mic_product()); return true; }

/* u-feckless-0023: the reads and writes that wait, and the queries that
 * only read a driver's flag, are UNLOCKED (FNU): a capture thread waiting for
 * samples must not hold up the drawing thread's calls. They touch nothing of
 * this file's but s_mic_open/s_mic_ch, which a read only looks at. */
const feckless_umode_call_t box_audio_calls[] = {
    FN(audio_out_init), FN(audio_out_set_volume), FNU(audio_out_headphones),
    FN(audio_out_capture_begin), FNU(audio_out_capture_read), FN(audio_out_capture_end),
    FNU(audio_out_monitor_write),
    FN(usbhost_init), FN(usbhost_start),
    FN(uac_init), FNU(uac_present), FNU(uac_generation), FN(uac_stream_start), FN(uac_stream_stop),
    FNU(uac_write), FN(uac_product), FNU(uac_mic_announced), FN(uac_mic_open_bits),
    FNU(uac_mic_read), FNU(uac_mic_gone), FN(uac_mic_close), FN(uac_mic_product),
    /* u-feckless-0046: the writer's write waits on the DMA, unlocked; the
     * rest are quick, and the queries only read */
    FNU(audio_out_write), FN(audio_out_set_format), FNU(audio_out_rate_supported),
    FNU(audio_out_rate), FN(audio_out_set_mute), FNU(audio_out_muted), FN(audio_out_set_idle),
    FNU(audio_out_route), FN(audio_out_route_name),
    FN(battery_init), FN(battery_start), FNU(battery_pct), FNU(battery_mv),
    FNU(battery_charging), FNU(battery_external), FN(battery_trace_arm),
    FN(rtc8130_init), FN(rtc8130_read), FN(rtc8130_write), FN(rtc8130_write_forward),
    FN(polyrsp_open), FN(polyrsp_close), FN(polyrsp_reset), FN(polyrsp_max_out),
    FN(polyrsp_process),
    FNU(usbhost_running), FNU(usbhost_vbus_on),
};
const uint32_t box_audio_calls_n = sizeof box_audio_calls / sizeof box_audio_calls[0];

void box_audio_end(void)
{
    if (s_mic_open) uac_mic_close();
    if (s_streaming) uac_stream_stop();
    if (s_capturing) audio_out_capture_end();
    if (s_played) audio_out_set_idle(true);    /* u-feckless-0046: the amplifier off again */
    for (int i = 0; i < RSP_MAX; i++) {
        if (s_rsp[i].r) polyrsp_close(s_rsp[i].r);
    }
    memset(s_rsp, 0, sizeof s_rsp);
    s_mic_open = s_streaming = s_capturing = s_played = false;
    s_mic_ch = 0;
    s_product = s_mic_product = s_route_name = 0;  /* the box goes with the app */
}
