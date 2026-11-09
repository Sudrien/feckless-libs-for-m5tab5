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
#include "tab5io.h"
#include "uac.h"
#include "usbhost.h"

#include "box_calls_priv.h"

#define TIMEOUT_MAX_MS  2000u       /* a read or write blocks the app's task, no longer */
#define PRODUCT_MAX     128u

static bool s_capturing, s_streaming, s_mic_open;
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

/* ---- usbhost --------------------------------------------------------------- */

H(c_usbhost_init)
{
    (void)w;
    *ret = a[0] == BOX_TOKEN_EXP2 ? (uint32_t)usbhost_init(tab5io_exp2())
                                  : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}
H(c_usbhost_start) { (void)w; (void)a; usbhost_start(); *ret = 0; return true; }

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
};
const uint32_t box_audio_calls_n = sizeof box_audio_calls / sizeof box_audio_calls[0];

void box_audio_end(void)
{
    if (s_mic_open) uac_mic_close();
    if (s_streaming) uac_stream_stop();
    if (s_capturing) audio_out_capture_end();
    s_mic_open = s_streaming = s_capturing = false;
    s_mic_ch = 0;
    s_product = s_mic_product = 0;          /* the box goes with the app */
}
