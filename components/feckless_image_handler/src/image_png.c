/*
 * image_png.c -- see feckless_image_png.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_image_png.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pngle.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define OUT_MALLOC(n)  heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define OUT_MALLOC(n)  malloc(n)
#endif

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

img_png_err_t img_png_info(const uint8_t *in, size_t len, int *w, int *h)
{
    *w = *h = 0;
    /* signature, then IHDR's length (13), its name, width, height. */
    if (!in || len < 8 + 8 + 8 || memcmp(in, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        be32(in + 8) != 13 || memcmp(in + 12, "IHDR", 4) != 0) {
        return IMG_PNG_NOT_PNG;
    }
    const uint32_t pw = be32(in + 16), ph = be32(in + 20);
    if (pw == 0 || ph == 0 || pw > INT32_MAX || ph > INT32_MAX) return IMG_PNG_NOT_PNG;
    *w = (int)pw;
    *h = (int)ph;
    if ((uint64_t)pw * (uint64_t)ph > IMG_PNG_MAX_PIXELS) return IMG_PNG_TOO_BIG;
    return IMG_PNG_OK;
}

size_t img_png_peak_bytes(int w, int h)
{
    /* The RGB565 result at full size, a scanline of RGBA8 or 16 bits a
     * channel for pngle's filter and its copy, and room for its inflate
     * state and window. An estimate, and a generous one. */
    return (size_t)w * (size_t)h * 2u + (size_t)w * 16u + 192u * 1024u;
}

typedef struct {
    uint16_t *px;
    size_t size;
    int w, h;               /* source */
    int step, ow, oh;       /* thinning, and the result's size */
    int box_w, box_h;
    uint16_t bg;
    uint8_t bg_r, bg_g, bg_b;   /* bg widened to 8 bits a channel */
    bool inited, too_big, no_mem;
} png_ctx_t;

static void on_init(pngle_t *pngle, uint32_t w, uint32_t h)
{
    png_ctx_t *c = pngle_get_user_data(pngle);
    c->inited = true;
    if (w == 0 || h == 0 || (uint64_t)w * h > IMG_PNG_MAX_PIXELS) {
        c->too_big = true;
        return;
    }
    c->w = (int)w;
    c->h = (int)h;

    /* Whole-step thinning, as img_stb_decode_rgb565(): the largest step
     * that still covers the box. */
    int step = 1;
    if (c->box_w > 0 && c->box_h > 0) {
        while (c->w / (step + 1) >= c->box_w && c->h / (step + 1) >= c->box_h) step++;
    }
    c->step = step;
    c->ow = c->w / step;
    c->oh = c->h / step;

    c->size = (size_t)c->ow * (size_t)c->oh * 2u;
    c->px = OUT_MALLOC(c->size);
    if (!c->px) {
        c->no_mem = true;
        return;
    }
    for (size_t i = 0; i < (size_t)c->ow * (size_t)c->oh; i++) c->px[i] = c->bg;
}

/* ceil(a / step) for a >= 0 */
static inline int ceil_div(uint32_t a, int step)
{
    return (int)((a + (uint32_t)step - 1u) / (uint32_t)step);
}

static void on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    const uint8_t rgba[4])
{
    png_ctx_t *c = pngle_get_user_data(pngle);
    if (!c->px) return;
    const uint8_t a = rgba[3];
    if (a == 0) return;                         /* bg is already there */

    uint16_t px;
    if (a == 255) {
        px = (uint16_t)(((rgba[0] & 0xF8) << 8) | ((rgba[1] & 0xFC) << 3) | (rgba[2] >> 3));
    } else {
        const unsigned ia = 255u - a;
        const unsigned r = (rgba[0] * a + c->bg_r * ia + 127u) / 255u;
        const unsigned g = (rgba[1] * a + c->bg_g * ia + 127u) / 255u;
        const unsigned b = (rgba[2] * a + c->bg_b * ia + 127u) / 255u;
        px = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    /* Output pixel (ox, oy) stands for source pixel (ox*step, oy*step), so
     * the run [x, x+w) x [y, y+h) paints the outputs whose source lies in
     * it. An Adam7 pass hands over blocks wider than a pixel and a later
     * pass overwrites them, so the last write is the exact one. */
    int ox0 = ceil_div(x, c->step), ox1 = ceil_div(x + w, c->step);
    int oy0 = ceil_div(y, c->step), oy1 = ceil_div(y + h, c->step);
    if (ox1 > c->ow) ox1 = c->ow;
    if (oy1 > c->oh) oy1 = c->oh;
    for (int oy = oy0; oy < oy1; oy++) {
        uint16_t *row = c->px + (size_t)oy * (size_t)c->ow;
        for (int ox = ox0; ox < ox1; ox++) row[ox] = px;
    }
}

img_png_err_t img_png_decode_rgb565(const uint8_t *in, size_t len,
                                    int box_w, int box_h, uint16_t bg,
                                    uint16_t **out, size_t *out_size,
                                    int *w_out, int *h_out,
                                    const char **why)
{
    *out = NULL;
    *out_size = 0;
    *w_out = *h_out = 0;
    if (why) *why = NULL;

    int iw, ih;
    const img_png_err_t info = img_png_info(in, len, &iw, &ih);
    if (info != IMG_PNG_OK) return info;

    png_ctx_t c;
    memset(&c, 0, sizeof c);
    c.box_w = box_w;
    c.box_h = box_h;
    c.bg = bg;
    c.bg_r = (uint8_t)(((bg >> 11) & 31) * 255 / 31);
    c.bg_g = (uint8_t)(((bg >> 5) & 63) * 255 / 63);
    c.bg_b = (uint8_t)((bg & 31) * 255 / 31);

    pngle_t *p = pngle_new();
    if (!p) return IMG_PNG_NO_MEM;
    pngle_set_user_data(p, &c);
    pngle_set_init_callback(p, on_init);
    pngle_set_draw_callback(p, on_draw);
    const int fed = pngle_feed(p, in, len);
    if (why && fed < 0) *why = pngle_error(p);
    img_png_err_t ret = IMG_PNG_OK;
    if (c.no_mem)          ret = IMG_PNG_NO_MEM;
    else if (c.too_big)    ret = IMG_PNG_TOO_BIG;
    else if (fed < 0)      ret = IMG_PNG_FAILED;
    else if (!c.inited)    ret = IMG_PNG_NOT_PNG;    /* consumed it all, saw no image */
    pngle_destroy(p);

    if (ret != IMG_PNG_OK) {
        free(c.px);
        return ret;
    }
    *out = c.px;
    *out_size = c.size;
    *w_out = c.ow;
    *h_out = c.oh;
    return IMG_PNG_OK;
}

const char *img_png_err_name(img_png_err_t e)
{
    switch (e) {
    case IMG_PNG_OK:      return "ok";
    case IMG_PNG_NOT_PNG: return "not a PNG pngle can read";
    case IMG_PNG_TOO_BIG: return "too many pixels";
    case IMG_PNG_NO_MEM:  return "out of memory";
    case IMG_PNG_FAILED:  return "decode failed";
    }
    return "?";
}
