/*
 * feckless_image_png.h -- PNG, through pngle, to an RGB565 buffer.
 *
 * Defeatist drew a PNG by streaming pngle's runs straight into the
 * panel's framebuffer, fitted to the screen as they came (its
 * albumart_draw_png()). That cannot be shared: it knows the panel. This
 * is the same decode with the destination made a buffer, so it answers
 * the way img_stb_decode_rgb565() and img_jxl_decode_rgb565() do and the
 * caller fits and draws whatever it was handed.
 *
 * pngle streams, so the full RGBA picture never exists: a 1400x1400 RGBA
 * buffer would be 7.8 MB, and what is kept here is the RGB565 result,
 * already thinned. Thinning is the same whole-step nearest neighbour as
 * the other two: the largest step that still covers the box, 1 when
 * there is no box. Transparency is blended over `bg`, an RGB565 colour,
 * as img_jxl_decode_rgb565() does for JPEG XL; a fully transparent pixel
 * is `bg` exactly. (Defeatist left a transparent pixel as it was and
 * drew a half-transparent one opaque; the first is the same, the second
 * is now blended.)
 *
 * A truncated file gives the part of the picture that decoded, the rest
 * `bg`, and IMG_PNG_OK, as it did: pngle does not call a short stream an
 * error. A stream with no image header gives IMG_PNG_NOT_PNG.
 *
 * pngle is kikuchan's, MIT, and miniz is Rich Geldreich's, public
 * domain or MIT; both fetched at a pinned commit and checked
 * (cmake/vendored.cmake). This wrapper is MIT.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 16 megapixels (4000x4000). pngle's time is per source pixel, thinned
 * or not, and this keeps the worst case to seconds; Defeatist's only
 * bound was 100000 a side, which its scaler's 32-bit edge map needed,
 * and a cover that fits its tag-size limit is nowhere near either. */
#define IMG_PNG_MAX_PIXELS  (16u * 1000u * 1000u)

typedef enum {
    IMG_PNG_OK = 0,
    IMG_PNG_NOT_PNG = -1,       /* no PNG header pngle could read */
    IMG_PNG_TOO_BIG = -2,       /* over IMG_PNG_MAX_PIXELS */
    IMG_PNG_NO_MEM = -3,
    IMG_PNG_FAILED = -4,        /* pngle refused the data */
} img_png_err_t;

/* Width and height from the IHDR, without decoding. */
img_png_err_t img_png_info(const uint8_t *in, size_t len, int *w, int *h);

/* What a decode of a w x h picture may hold at once, in bytes: the
 * unthinned RGB565 result, an upper bound, and pngle's own state. */
size_t img_png_peak_bytes(int w, int h);

/*
 * Decode to native little-endian RGB565 (gfx.h's RGB()). *out is
 * malloc'd (PSRAM on the board) and belongs to the caller; its stride is
 * *w_out pixels. `why` is pngle's own message on failure, for the log.
 */
img_png_err_t img_png_decode_rgb565(const uint8_t *in, size_t len,
                                    int box_w, int box_h, uint16_t bg,
                                    uint16_t **out, size_t *out_size,
                                    int *w_out, int *h_out,
                                    const char **why);

const char *img_png_err_name(img_png_err_t e);

#ifdef __cplusplus
}
#endif
