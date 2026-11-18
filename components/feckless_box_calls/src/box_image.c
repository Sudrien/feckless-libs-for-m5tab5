/* SPDX-License-Identifier: MIT
 *
 * The hardware JPEG decoder for a boxed app (u-feckless-0065): the ABI's
 * image section, whole -- feckless_abi_image_v1 and the five calls of
 * ESP-IDF's esp_driver_jpeg it names.
 *
 * Without it a boxed app's pictures decode in software, in its window, with
 * interrupts held off on its core for as long as the decode takes
 * (u-feckless-0017). Defeatist's cover art did that for longer than the
 * interrupt watchdog allows, an hour into a run:
 *
 *     Guru Meditation Error: Core  1 panic'ed (Interrupt wdt timeout on CPU1).
 *
 * With it, a baseline JPEG is one host call: the engine decodes it by DMA
 * while the app's thread waits in M-mode, interrupts on.
 *
 * - The engine is the host's, made at the app's first
 *   jpeg_new_decoder_engine() and deleted when the app ends; the app holds a
 *   number. One engine, shared by the app's threads; decodes are taken one
 *   at a time. Its timeout is TIMEOUT_MS, whatever the app asked, so no
 *   decode can outwait feckless_umode's stop.
 * - jpeg_alloc_decoder_mem() allocates in the box, on the cache line the
 *   engine needs for its output (esp_cache_get_line_size_by_addr() at the
 *   box), its size rounded up to whole lines. The app frees it with
 *   free(), as on a trusted run. Not for PSRAM encryption with its exempt
 *   region: the box is not in it.
 * - jpeg_decoder_process() takes the app's input and output where they are,
 *   both checked to lie in the box for their whole length; the engine reads
 *   and writes them by DMA, and the driver keeps the cache right. An output
 *   that is not on a cache line is refused by the driver, as it would be.
 *   Unlocked: the decode waits on the engine, and the app's other threads'
 *   calls need not wait behind it.
 * - jpeg_decoder_get_info() reads the header in the box, in place.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#define BOX_JPEG_CHIP SOC_JPEG_CODEC_SUPPORTED
#else
#define BOX_JPEG_CHIP 1                     /* the host test */
#endif

#if BOX_JPEG_CHIP && __has_include("driver/jpeg_decode.h") && !defined(FECKLESS_BOX_NO_JPEG)

#include "driver/jpeg_decode.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TOKEN_JPEG   0x4a504700u            /* "JPG\0" */
#define TIMEOUT_MS   2000                   /* under feckless_umode's STOP_WAIT_MS */
#define LINE_MIN     64u                    /* the P4's L1 line; the driver's DMA2D wants 16 */

static const uint32_t s_image_v1 = 1;       /* feckless_abi_image_v1 */
static jpeg_decoder_handle_t s_engine;
static SemaphoreHandle_t s_lock;            /* one decode at a time */
static bool s_app_has;                      /* the app holds TOKEN_JPEG */

/* The line the engine's output must start and end on: the cache's line at
 * the box, which is PSRAM, as the driver's jpeg_alloc_decoder_mem() takes
 * it, and never under LINE_MIN (the driver's DMA2D alignment is 16). */
static uint32_t line_size(const win_t *w)
{
    size_t a = esp_cache_get_line_size_by_addr((const void *)box_of_win((win_t *)w)->base);
    if (a < LINE_MIN) a = LINE_MIN;
    return (uint32_t)a;
}

/* (const jpeg_decode_engine_cfg_t *, jpeg_decoder_handle_t *) */
H(c_jpeg_new_decoder_engine)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (ok_ptr(w, a[0], sizeof(jpeg_decode_engine_cfg_t)) && ok_ptr(w, a[1], sizeof(uint32_t))) {
        *(uint32_t *)(uintptr_t)a[1] = 0;
        e = ESP_OK;
        if (!s_lock && (s_lock = xSemaphoreCreateBinary()) != NULL) xSemaphoreGive(s_lock);
        if (!s_lock) e = ESP_ERR_NO_MEM;
        if (e == ESP_OK && !s_engine) {
            const jpeg_decode_engine_cfg_t cfg = { .timeout_ms = TIMEOUT_MS };
            e = jpeg_new_decoder_engine(&cfg, &s_engine);
            if (e != ESP_OK) s_engine = NULL;
        }
        if (e == ESP_OK) {
            s_app_has = true;
            *(uint32_t *)(uintptr_t)a[1] = TOKEN_JPEG;
        }
    }
    *ret = (uint32_t)e;
    return true;
}

/* The engine stays the host's until the app ends; the number stops working. */
H(c_jpeg_del_decoder_engine)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (a[0] == TOKEN_JPEG && s_app_has) {
        s_app_has = false;
        e = ESP_OK;
    }
    *ret = (uint32_t)e;
    return true;
}

/* (const uint8_t *bit_stream, uint32_t size, jpeg_decode_picture_info_t *) */
H(c_jpeg_decoder_get_info)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (a[1] && ok_ptr(w, a[0], a[1]) && ok_ptr(w, a[2], sizeof(jpeg_decode_picture_info_t))) {
        jpeg_decode_picture_info_t info;
        memset(&info, 0, sizeof info);
        e = jpeg_decoder_get_info((const uint8_t *)(uintptr_t)a[0], a[1], &info);
        memcpy((void *)(uintptr_t)a[2], &info, sizeof info);
    }
    *ret = (uint32_t)e;
    return true;
}

/* (size_t size, const jpeg_decode_memory_alloc_cfg_t *, size_t *allocated)
 * -> a pointer in the box, or NULL */
H(c_jpeg_alloc_decoder_mem)
{
    *ret = 0;
    if (a[0] == 0 || !ok_ptr(w, a[1], sizeof(jpeg_decode_memory_alloc_cfg_t)) || !ok_ptr(w, a[2], sizeof(uint32_t))) {
        return true;
    }
    *(uint32_t *)(uintptr_t)a[2] = 0;
    const uint32_t line = line_size(w);
    if (a[0] > UINT32_MAX - line) return true;
    const uint32_t len = (a[0] + line - 1) & ~(line - 1);
    const uintptr_t p = feckless_box_alloc_aligned(box_of_win(w), len, line);
    if (p) {
        *(uint32_t *)(uintptr_t)a[2] = len;
        *ret = (uint32_t)p;
    }
    return true;
}

/* (engine, const jpeg_decode_cfg_t *, bit_stream, stream_size, outbuf,
 *  outbuf_size, uint32_t *out_size) */
H(c_jpeg_decoder_process)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (a[0] == TOKEN_JPEG && s_app_has && s_engine && ok_ptr(w, a[1], sizeof(jpeg_decode_cfg_t)) &&
        a[3] && ok_ptr(w, a[2], a[3]) && a[5] && ok_ptr(w, a[4], a[5]) && ok_ptr(w, a[6], sizeof(uint32_t))) {
        jpeg_decode_cfg_t cfg;
        memcpy(&cfg, (const void *)(uintptr_t)a[1], sizeof cfg);
        uint32_t out = 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        e = jpeg_decoder_process(s_engine, &cfg, (const uint8_t *)(uintptr_t)a[2], a[3],
                                 (uint8_t *)(uintptr_t)a[4], a[5], &out);
        xSemaphoreGive(s_lock);
        *(uint32_t *)(uintptr_t)a[6] = out;
    }
    *ret = (uint32_t)e;
    return true;
}

const feckless_umode_call_t box_image_calls[] = {
    { "feckless_abi_image_v1", NULL, &s_image_v1, sizeof s_image_v1, 0 },
    FN(jpeg_new_decoder_engine), FN(jpeg_del_decoder_engine), FNU(jpeg_decoder_get_info),
    FN(jpeg_alloc_decoder_mem), FNU(jpeg_decoder_process),
};
const uint32_t box_image_calls_n = sizeof box_image_calls / sizeof box_image_calls[0];

/* After the app is unloaded: no thread is inside a decode. */
void box_image_end(void)
{
    s_app_has = false;
    if (s_engine) jpeg_del_decoder_engine(s_engine);
    s_engine = NULL;
}

#else   /* no JPEG engine on this chip, or left out */

const feckless_umode_call_t box_image_calls[1] = { { NULL, NULL, NULL, 0, 0 } };
const uint32_t box_image_calls_n = 0;
void box_image_end(void) {}

#endif
