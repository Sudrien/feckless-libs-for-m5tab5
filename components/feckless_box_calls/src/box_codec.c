/* SPDX-License-Identifier: MIT
 *
 * esp_audio_codec's simple decoder for a boxed app (u-feckless-0052): FLAC,
 * AAC, M4A, Vorbis and Opus in Ogg, ALAC, WAV and the rest the launcher's
 * copy of the codec has, decoded on the host.
 *
 * Built only where the launcher links esp_audio_codec (as one that offers
 * the ABI's audio section does); elsewhere the table is empty, and an app
 * that imports these is refused at load by name, as before. The codec is
 * precompiled without -fPIC, so it cannot link into an app (8093 in
 * Defeatist), and a box is the only way a boxed app reaches it.
 *
 * FECKLESS_BOX_NO_CODEC leaves it out where the codec is linked, for a
 * launcher that keeps its codec to itself (and the host test, for the
 * other branch).
 *
 * - A decoder is the host's, its state and its pointers too; the app holds
 *   a number. Four at once.
 * - The decode runs on the calling thread's host task, in M-mode: a frame
 *   of FLAC takes milliseconds, and in a window those would be
 *   milliseconds with interrupts off on the core. Unlocked, so the app's
 *   other threads' calls are not held up behind it; each decoder is busy
 *   while one call is inside it, and a second call into a busy one, or a
 *   close, waits for the first (a close sets it closing, and the last call
 *   out closes it).
 * - The structs are copied in and the results out. Their buffers -- the
 *   input bytes, the PCM out -- are the app's, checked to lie in the box
 *   for their whole length, and read and written there by the CPU.
 * - A decoder's own configuration (dec_cfg) is copied into the host, at
 *   most CFG_MAX bytes, and kept for the decoder's life. Every one of the
 *   codec's decoder configurations is plain values except two: ALAC's,
 *   whose magic cookie is copied too, and Vorbis's two headers, which are
 *   refused (an app gives Vorbis in Ogg, which needs neither).
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("esp_audio_simple_dec.h") && !defined(FECKLESS_BOX_NO_CODEC)

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_alac_dec.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "feckless_umode_load.h"

#define DEC_MAX     4
#define TOKEN_DEC   0x41444500u             /* "ADE\0" + slot */
#define CFG_MAX     64u
#define COOKIE_MAX  4096u
#define WAIT_MS     10

typedef struct {
    esp_audio_simple_dec_handle_t h;
    uint8_t cfg[CFG_MAX];
    uint8_t *cookie;                        /* ALAC's, host memory */
    bool busy, closing;
} dec_t;

static dec_t s_dec[DEC_MAX];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static const uint32_t s_audio_v1 = 1;       /* feckless_abi_audio_v1 */

static void dec_close(dec_t *d)
{
    if (d->h) esp_audio_simple_dec_close(d->h);
    heap_caps_free(d->cookie);
    portENTER_CRITICAL(&s_mux);
    memset(d, 0, sizeof *d);
    portEXIT_CRITICAL(&s_mux);
}

/* The decoder for tok, made busy; NULL if none, or the app is stopping
 * while it waits for another thread's call to come out. */
static dec_t *take(uint32_t tok)
{
    const uint32_t i = tok - TOKEN_DEC;
    if (tok < TOKEN_DEC || i >= DEC_MAX) return NULL;
    dec_t *d = &s_dec[i];
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const bool live = d->h && !d->closing;
        const bool got = live && !d->busy;
        if (got) d->busy = true;
        portEXIT_CRITICAL(&s_mux);
        if (got) return d;
        if (!live || feckless_umode_app_stopping(feckless_umode_app_loaded())) return NULL;
        vTaskDelay(pdMS_TO_TICKS(WAIT_MS) ? pdMS_TO_TICKS(WAIT_MS) : 1);
    }
}

static void give(dec_t *d)
{
    portENTER_CRITICAL(&s_mux);
    d->busy = false;
    const bool close = d->closing;
    portEXIT_CRITICAL(&s_mux);
    if (close) dec_close(d);
}

/* Registration is the host's and lasts: once is enough, and a launcher
 * that registered them itself answers ALREADY_EXIST, which is success. */
static esp_audio_err_t once(esp_audio_err_t (*reg)(void), int *state)
{
    if (!*state) {
        const esp_audio_err_t e = reg();
        *state = (e == ESP_AUDIO_ERR_OK || e == ESP_AUDIO_ERR_ALREADY_EXIST) ? 1 : -1;
    }
    return *state > 0 ? ESP_AUDIO_ERR_OK : ESP_AUDIO_ERR_FAIL;
}
static int s_dec_reg, s_simple_reg;

H(c_esp_audio_dec_register_default) { (void)w; (void)a; *ret = (uint32_t)once(esp_audio_dec_register_default, &s_dec_reg); return true; }
H(c_esp_audio_simple_dec_register_default)
{
    (void)w; (void)a;
    *ret = (uint32_t)once(esp_audio_simple_dec_register_default, &s_simple_reg);
    return true;
}

/* (cfg *, handle *) */
H(c_esp_audio_simple_dec_open)
{
    *ret = (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER;
    esp_audio_simple_dec_cfg_t cfg;
    if (!ok_ptr(w, a[0], sizeof cfg) || !ok_ptr(w, a[1], 4)) return true;
    memcpy(&cfg, (const void *)(uintptr_t)a[0], sizeof cfg);
    const uint32_t zero = 0;
    memcpy((void *)(uintptr_t)a[1], &zero, 4);
    const uint32_t app_cfg = (uint32_t)(uintptr_t)cfg.dec_cfg;
    if (cfg.dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS && app_cfg) {
        *ret = (uint32_t)ESP_AUDIO_ERR_NOT_SUPPORT;    /* its headers are pointers too; Ogg needs none */
        return true;
    }
    if (app_cfg && (cfg.cfg_size <= 0 || (uint32_t)cfg.cfg_size > CFG_MAX || !ok_ptr(w, app_cfg, (uint32_t)cfg.cfg_size))) {
        return true;
    }
    int i = 0;
    portENTER_CRITICAL(&s_mux);
    while (i < DEC_MAX && (s_dec[i].h || s_dec[i].busy)) i++;
    if (i < DEC_MAX) s_dec[i].busy = true;  /* held while it is made */
    portEXIT_CRITICAL(&s_mux);
    if (i == DEC_MAX) {
        *ret = (uint32_t)ESP_AUDIO_ERR_MEM_LACK;
        return true;
    }
    dec_t *d = &s_dec[i];
    cfg.dec_cfg = NULL;
    if (app_cfg) {
        memcpy(d->cfg, (const void *)(uintptr_t)app_cfg, (size_t)cfg.cfg_size);
        cfg.dec_cfg = d->cfg;
        if (cfg.dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC) {
            esp_alac_dec_cfg_t *al = (esp_alac_dec_cfg_t *)d->cfg;
            const uint32_t at = (uint32_t)(uintptr_t)al->codec_spec_info;
            if ((size_t)cfg.cfg_size < sizeof *al) {
                give(d);
                return true;
            }
            if (at) {
                if (al->spec_info_len == 0 || al->spec_info_len > COOKIE_MAX || !ok_ptr(w, at, al->spec_info_len) ||
                    !(d->cookie = heap_caps_malloc(al->spec_info_len, MALLOC_CAP_8BIT))) {
                    give(d);
                    return true;
                }
                memcpy(d->cookie, (const void *)(uintptr_t)at, al->spec_info_len);
                al->codec_spec_info = d->cookie;
            }
        }
    }
    esp_audio_simple_dec_handle_t h = NULL;
    const esp_audio_err_t e = esp_audio_simple_dec_open(&cfg, &h);
    if (e != ESP_AUDIO_ERR_OK || !h) {
        heap_caps_free(d->cookie);
        d->cookie = NULL;
        give(d);
        *ret = (uint32_t)(e != ESP_AUDIO_ERR_OK ? e : ESP_AUDIO_ERR_FAIL);
        return true;
    }
    portENTER_CRITICAL(&s_mux);
    d->h = h;
    portEXIT_CRITICAL(&s_mux);
    give(d);
    const uint32_t tok = TOKEN_DEC + (uint32_t)i;
    memcpy((void *)(uintptr_t)a[1], &tok, 4);
    *ret = ESP_AUDIO_ERR_OK;
    return true;
}

/* (handle, raw *, out *): raw.buffer for raw.len and out.buffer for out.len
 * in the box; consumed, needed_size and decoded_size copied back. */
H(c_esp_audio_simple_dec_process)
{
    *ret = (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER;
    esp_audio_simple_dec_raw_t raw;
    esp_audio_simple_dec_out_t out;
    if (!ok_ptr(w, a[1], sizeof raw) || !ok_ptr(w, a[2], sizeof out)) return true;
    memcpy(&raw, (const void *)(uintptr_t)a[1], sizeof raw);
    memcpy(&out, (const void *)(uintptr_t)a[2], sizeof out);
    if ((raw.len && !ok_ptr(w, (uint32_t)(uintptr_t)raw.buffer, raw.len)) ||
        (out.len && !ok_ptr(w, (uint32_t)(uintptr_t)out.buffer, out.len))) {
        return true;
    }
    if (!raw.len) raw.buffer = NULL;
    if (!out.len) out.buffer = NULL;
    dec_t *d = take(a[0]);
    if (!d) return true;
    raw.consumed = 0;
    out.needed_size = out.decoded_size = 0;
    *ret = (uint32_t)esp_audio_simple_dec_process(d->h, &raw, &out);
    give(d);
    /* the fields the call sets, and no pointer back: those stay the app's */
    esp_audio_simple_dec_raw_t *ar = (esp_audio_simple_dec_raw_t *)(uintptr_t)a[1];
    esp_audio_simple_dec_out_t *ao = (esp_audio_simple_dec_out_t *)(uintptr_t)a[2];
    ar->consumed = raw.consumed < raw.len ? raw.consumed : raw.len;
    ao->needed_size = out.needed_size;
    ao->decoded_size = out.decoded_size < out.len ? out.decoded_size : out.len;
    return true;
}

/* (handle, info *) */
H(c_esp_audio_simple_dec_get_info)
{
    esp_audio_simple_dec_info_t info;
    *ret = (uint32_t)ESP_AUDIO_ERR_INVALID_PARAMETER;
    if (!ok_ptr(w, a[1], sizeof info)) return true;
    dec_t *d = take(a[0]);
    if (!d) return true;
    memset(&info, 0, sizeof info);
    *ret = (uint32_t)esp_audio_simple_dec_get_info(d->h, &info);
    give(d);
    memcpy((void *)(uintptr_t)a[1], &info, sizeof info);
    return true;
}

/* (handle): closed now, or by the call inside it as that one leaves */
H(c_esp_audio_simple_dec_close)
{
    (void)w;
    *ret = 0;
    const uint32_t i = a[0] - TOKEN_DEC;
    if (a[0] < TOKEN_DEC || i >= DEC_MAX) return true;
    dec_t *d = &s_dec[i];
    portENTER_CRITICAL(&s_mux);
    const bool live = d->h && !d->closing;
    const bool now = live && !d->busy;
    if (live) d->closing = true;
    if (now) d->busy = true;
    portEXIT_CRITICAL(&s_mux);
    if (now) dec_close(d);
    return true;
}

const feckless_umode_call_t box_codec_calls[] = {
    { "feckless_abi_audio_v1", NULL, &s_audio_v1, sizeof s_audio_v1, 0 },
    FN(esp_audio_dec_register_default), FN(esp_audio_simple_dec_register_default),
    FN(esp_audio_simple_dec_open), FNU(esp_audio_simple_dec_process), FNU(esp_audio_simple_dec_get_info),
    FNU(esp_audio_simple_dec_close),
};
const uint32_t box_codec_calls_n = sizeof box_codec_calls / sizeof box_codec_calls[0];

/* After the app is unloaded: no thread is inside one. */
void box_codec_end(void)
{
    for (int i = 0; i < DEC_MAX; i++) {
        if (s_dec[i].h) dec_close(&s_dec[i]);
    }
}

#else   /* no esp_audio_codec in this launcher */

const feckless_umode_call_t box_codec_calls[1] = { { NULL, NULL, NULL, 0, 0 } };
const uint32_t box_codec_calls_n = 0;
void box_codec_end(void) {}

#endif
