/*
 * feckless_image.h -- pictures, from their bytes: what kind, how big,
 * how to fit it, and a favicon as a PNG. The pure half of image loading,
 * which has no ESP-IDF in it and builds and tests on a host.
 *
 * Moved from Defeatist's artformat.h, icoimg.[ch] and the integer-scale
 * rule in albumart.c (7004), so every app asks the same question the same
 * way. Decoding and drawing follow in later patches of this component.
 *
 * Magic bytes decide, never a MIME type or a file extension: a tag's
 * MIME field and a file's name are both written by whoever wrote the
 * file, and are wrong often enough that the picture's own first bytes
 * are the only thing worth believing.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMG_NONE = 0,       /* not a picture this library knows */
    IMG_JPEG,
    IMG_PNG,
    IMG_JXL,            /* JPEG XL, bare codestream or ISOBMFF container */
    IMG_GIF,            /* GIF87a or GIF89a; the first frame is shown */
} img_format_t;

/* JPEG XL's two signatures: the bare codestream, and the container's
 * first box ("JXL " with its fixed 12-byte header). */
#define IMG_JXL_BOX "\x00\x00\x00\x0cJXL \x0d\x0a\x87\x0a"

static inline img_format_t img_sniff(const uint8_t *p, size_t len)
{
    if (!p) return IMG_NONE;
    if (len > 3 && p[0] == 0xFF && p[1] == 0xD8) return IMG_JPEG;
    if (len > 8 && memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return IMG_PNG;
    if (len > 3 && p[0] == 0xFF && p[1] == 0x0A) return IMG_JXL;
    if (len > 12 && memcmp(p, IMG_JXL_BOX, 12) == 0) return IMG_JXL;
    if (len > 13 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0)) return IMG_GIF;
    return IMG_NONE;
}

/* The MIME type for a picture, for the web remote. */
static inline const char *img_mime(img_format_t f)
{
    switch (f) {
    case IMG_JPEG: return "image/jpeg";
    case IMG_PNG:  return "image/png";
    case IMG_JXL:  return "image/jxl";
    case IMG_GIF:  return "image/gif";
    default:       return "application/octet-stream";
    }
}

/*
 * A JPEG's SOF marker and size, by walking the markers to the first SOF
 * (why it is asked: a progressive file is a picture, but not one the hardware or a baseline-only software decoder can take, and the caller that has somewhere else to go wants to know which).
 * True for SOF0 and SOF1, the two the hardware and TJpgDec take.
 * `*sof_out` is the marker found, 0 for none; the size comes from its
 * payload even when the answer is false.
 *
 * Markers are two bytes, 0xFF then a code, and every one except the
 * standalone few carries a big-endian length that includes itself. SOS
 * ends the header, so the SOF has to appear before it or there is not
 * one.
 */
static inline bool img_jpeg_sof(const uint8_t *p, size_t len, uint8_t *sof_out,
                                      uint32_t *w_out, uint32_t *h_out)
{
    *sof_out = 0;
    *w_out = 0;
    *h_out = 0;
    if (len < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;   /* no SOI */

    size_t i = 2;
    while (i + 3 < len) {
        if (p[i] != 0xFF) { i++; continue; }             /* fill byte or junk */
        const uint8_t m = p[i + 1];
        if (m == 0xFF) { i++; continue; }                /* fill */
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        if (m == 0xDA) return false;                     /* SOS, no SOF seen */

        /* SOFn: C0-CF except C4 (DHT), C8 (JPG), CC (DAC). C0 is
         * baseline, C1 is extended sequential and the hardware takes it
         * too; C2 is progressive, which it does not. */
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            *sof_out = m;
            /* Every SOF has the same first five payload bytes: precision,
             * then height and width, both big-endian 16-bit. */
            if (i + 9 < len) {
                *h_out = ((uint32_t)p[i + 5] << 8) | p[i + 6];
                *w_out = ((uint32_t)p[i + 7] << 8) | p[i + 8];
            }
            return (m == 0xC0 || m == 0xC1);
        }

        const size_t seg = ((size_t)p[i + 2] << 8) | p[i + 3];
        if (seg < 2) return false;
        i += 2 + seg;
    }
    return false;
}

/*
 * The whole-number scale at which a small picture fills a box: the
 * largest k with iw*k <= box_w and ih*k <= box_h, or 0 when that is
 * under 2 or the picture is over IMG_INT_SCALE_MAX on a side.
 *
 * Pixel art and icons look right only at whole multiples; larger
 * pictures, which are album covers, keep a fractional fit -- a 370 px
 * cover in a 720 px box would otherwise be drawn at 1x.
 */
#define IMG_INT_SCALE_MAX  (256)

static inline int img_int_scale(int iw, int ih, int box_w, int box_h)
{
    if (iw < 1 || ih < 1 || iw > IMG_INT_SCALE_MAX || ih > IMG_INT_SCALE_MAX) return 0;
    const int kx = box_w / iw, ky = box_h / ih;
    const int k = kx < ky ? kx : ky;
    return k >= 2 ? k : 0;
}

/* ---- favicon.ico ---------------------------------------------------- */
#define IMG_ICO_MAX_DIM  (256)

typedef struct {
    int  w, h;          /* the entry chosen */
    int  bpp;           /* its bit depth; 0 for an embedded PNG */
    bool was_png;       /* the entry was a PNG, copied out unchanged */
} img_ico_info_t;

/* A Windows icon directory (type 1), with at least one entry. */
bool img_ico_is_ico(const uint8_t *p, size_t len);

/*
 * The largest readable entry of `ico` as a PNG. On success *out is a
 * fresh allocation (PSRAM on the target) that the caller frees, and
 * *info describes the entry. On failure nothing is allocated.
 */
bool img_ico_to_png(const uint8_t *ico, size_t len,
                   uint8_t **out, size_t *out_len, img_ico_info_t *info);

#ifdef __cplusplus
}
#endif
