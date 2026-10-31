/*
 * feckless_image_stb.h -- the last JPEG decoder: stb_image, JPEG only, for what the
 * hardware and TJpgDec both refuse.
 *
 * 5062. Those two are baseline-only in practice. The hardware takes SOF0
 * and SOF1 at sizes divisible by 8; TJpgDec takes SOF0 only, with the
 * common chroma layouts. A progressive cover (SOF2) reached neither, and
 * the BBC World Service logo -- 145x145, which the hardware refuses on
 * size -- was refused by TJpgDec as JDR_FMT3, an unsupported flavour.
 * stb_image decodes SOF0, SOF1 and SOF2 at any size and any sampling.
 *
 * It is the last resort, not a replacement, for three reasons:
 *   - it decodes the whole picture at full size, where the hardware is
 *     milliseconds and TJpgDec can scale;
 *   - a progressive decode holds every DCT coefficient of the picture at
 *     once, about 10 bytes a pixel at its peak with the output, all in
 *     PSRAM (see img_stb_peak_bytes());
 *   - it has not had the hardening the other two have. Its input here
 *     comes from the internet and from cards, so it only ever sees
 *     pictures under IMG_STB_MAX_PIXELS that fit in memory.
 *
 * stb_image is Sean Barrett's, under MIT or the Unlicense at your
 * choice, pinned in cmake/vendored.cmake. This wrapper is MIT.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 4 megapixels, 2000x2000. At the ~10 bytes a pixel a progressive decode
 * peaks at, that is 40 MB -- more than the board has -- so the real bound
 * is the free-memory check the caller makes with img_stb_peak_bytes().
 * This one only keeps the arithmetic small and refuses the absurd. */
#define IMG_STB_MAX_PIXELS  (4u * 1000u * 1000u)

typedef enum {
    IMG_STB_OK = 0,
    IMG_STB_NOT_JPEG = -1,      /* no header stb_image could read */
    IMG_STB_TOO_BIG = -2,       /* over IMG_STB_MAX_PIXELS */
    IMG_STB_NO_MEM = -3,
    IMG_STB_FAILED = -4,        /* header fine, data refused */
} img_stb_err_t;

/* Width and height from the header, without decoding. */
img_stb_err_t img_stb_info(const uint8_t *in, size_t len, int *w, int *h);

/* What a decode of a w x h picture may hold at once, in bytes. */
size_t img_stb_peak_bytes(int w, int h);

/*
 * 6145: the same for a GIF, which stb_image also reads now (the first
 * frame of an animated one). img_stb_info() and img_stb_decode_rgb565()
 * take a GIF as they take a JPEG; transparent pixels come out as their
 * palette colour, there being no alpha in RGB565.
 */
size_t img_stb_gif_peak_bytes(int w, int h);

/*
 * Decode to native little-endian RGB565 (gfx.h's RGB()), thinned by
 * whole steps to the smallest size that still covers box_w x box_h, so
 * the picture kept afterwards is not bigger than the screen needs.
 * *out is malloc'd (PSRAM on the board) and belongs to the caller; its
 * stride is *w_out pixels.
 *
 * `last_error` is stb_image's own reason on failure, for the log.
 */
img_stb_err_t img_stb_decode_rgb565(const uint8_t *in, size_t len,
                                    int box_w, int box_h,
                                    uint16_t **out, size_t *out_size,
                                    int *w_out, int *h_out,
                                    const char **last_error);

const char *img_stb_err_name(img_stb_err_t e);

#ifdef __cplusplus
}
#endif
