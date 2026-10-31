/*
 * image_stb.c -- see feckless_image_stb.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_image_stb.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
/* PSRAM, all of it. A progressive decode is megabytes, and the internal
 * RAM this would otherwise reach for is what the Wi-Fi transport runs
 * out of first (see sdkconfig.defaults, 5054). */
static void *img_stb_malloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void *img_stb_realloc(void *p, size_t n)
{
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#define STBI_MALLOC(n)      img_stb_malloc(n)
#define STBI_REALLOC(p, n)  img_stb_realloc((p), (n))
#define STBI_FREE(p)        heap_caps_free(p)
#define OUT_MALLOC(n)       img_stb_malloc(n)
#else
#define OUT_MALLOC(n)       malloc(n)
#endif

/* JPEG, and since 6145 GIF (its first frame): every other format here
 * has its own decoder, and each one compiled in is attack surface fed
 * from the internet. GIF costs about 5 KB of code. */
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

img_stb_err_t img_stb_info(const uint8_t *in, size_t len, int *w, int *h)
{
    int comp = 0;
    *w = *h = 0;
    if (!in || len < 4 || len > (size_t)INT32_MAX) return IMG_STB_NOT_JPEG;
    if (!stbi_info_from_memory(in, (int)len, w, h, &comp)) return IMG_STB_NOT_JPEG;
    if (*w < 1 || *h < 1) return IMG_STB_NOT_JPEG;
    if ((uint64_t)*w * (uint64_t)*h > IMG_STB_MAX_PIXELS) return IMG_STB_TOO_BIG;
    return IMG_STB_OK;
}

size_t img_stb_gif_peak_bytes(int w, int h)
{
    /* stb's GIF reader holds the frame and the background at 4 bytes a
     * pixel and a byte of history, then the RGB888 copy at 3, and the
     * RGB565 result at 2: 14 a pixel, and the LZW tables. */
    return (size_t)w * (size_t)h * 14u + 256u * 1024u;
}

size_t img_stb_peak_bytes(int w, int h)
{
    /* Coefficients for three full-size components at 2 bytes (the
     * progressive worst case, 4:4:4), the RGB888 result at 3, and room
     * for the RGB565 copy and stb's line buffers. */
    return (size_t)w * (size_t)h * 10u + 256u * 1024u;
}

img_stb_err_t img_stb_decode_rgb565(const uint8_t *in, size_t len,
                                    int box_w, int box_h,
                                    uint16_t **out, size_t *out_size,
                                    int *w_out, int *h_out,
                                    const char **last_error)
{
    *out = NULL;
    *out_size = 0;
    *w_out = *h_out = 0;
    if (last_error) *last_error = NULL;

    int w, h;
    const img_stb_err_t info = img_stb_info(in, len, &w, &h);
    if (info != IMG_STB_OK) {
        if (last_error) *last_error = stbi_failure_reason();
        return info;
    }

    int got_w = 0, got_h = 0, comp = 0;
    uint8_t *rgb = stbi_load_from_memory(in, (int)len, &got_w, &got_h, &comp, 3);
    if (!rgb) {
        const char *why = stbi_failure_reason();
        if (last_error) *last_error = why;
        return (why && strcmp(why, "outofmem") == 0) ? IMG_STB_NO_MEM
                                                      : IMG_STB_FAILED;
    }

    /* Whole-step thinning: the largest step that still covers the box.
     * Nearest neighbour, like blit_cover()'s own scaling after it. */
    int step = 1;
    if (box_w > 0 && box_h > 0) {
        while (got_w / (step + 1) >= box_w && got_h / (step + 1) >= box_h) step++;
    }
    const int ow = got_w / step, oh = got_h / step;

    const size_t need = (size_t)ow * (size_t)oh * 2u;
    uint16_t *px = OUT_MALLOC(need);
    if (!px) {
        stbi_image_free(rgb);
        return IMG_STB_NO_MEM;
    }
    for (int y = 0; y < oh; y++) {
        const uint8_t *src = rgb + ((size_t)y * step * (size_t)got_w) * 3u;
        uint16_t *dst = px + (size_t)y * ow;
        for (int x = 0; x < ow; x++, src += 3u * (size_t)step) {
            /* gfx.h RGB(): native RGB565, as TJpgDec's output is here. */
            dst[x] = (uint16_t)(((src[0] & 0xF8) << 8) |
                                ((src[1] & 0xFC) << 3) |
                                (src[2] >> 3));
        }
    }
    stbi_image_free(rgb);

    *out = px;
    *out_size = need;
    *w_out = ow;
    *h_out = oh;
    return IMG_STB_OK;
}

const char *img_stb_err_name(img_stb_err_t e)
{
    switch (e) {
    case IMG_STB_OK:       return "ok";
    case IMG_STB_NOT_JPEG: return "not a JPEG or GIF stb_image can read";
    case IMG_STB_TOO_BIG:  return "too many pixels";
    case IMG_STB_NO_MEM:   return "out of memory";
    case IMG_STB_FAILED:   return "decode failed";
    }
    return "?";
}
