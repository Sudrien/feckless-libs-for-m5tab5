/*
 * feckless_image_decode.h -- one call from a picture's bytes to RGB565
 * pixels, with the decoders tried in the order Defeatist found right
 * (7007; its albumart.c, moved).
 *
 *   JPEG, baseline    the hardware engine, when it is there and the
 *                     padded result fits in one block of PSRAM; if not,
 *                     or if it refuses the picture, TJpgDec (esp_jpeg)
 *                     at the gentlest scale that still covers the box;
 *                     and if TJpgDec refuses the FORMAT, stb_image.
 *   JPEG, progressive stb_image only: neither of the others reads it.
 *   JPEG, other SOF   refused, with the marker named.
 *   GIF               stb_image, first frame, on a stack of its own.
 *   PNG               pngle.
 *   JPEG XL           J40, on a stack of its own.
 *
 * WHAT IS HERE AND WHAT IS NOT. This file and image_decode.c are the
 * ORDER: which decoder, when, on what memory, and what a refusal does
 * next. They call the decoders, the memory figures and the priority drop
 * through an img_ops_t, so the order runs and is tested on a host with
 * fakes; image_esp.c (7008) fills the table in with the real ones.
 *
 * Output is native RGB565 in *pic, `stride` pixels a row (the hardware
 * pads to its MCU grid and the picture is a window into that; every other
 * decoder's stride is its width). pic->px is malloc'd, PSRAM on the board,
 * and the caller's to free().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMG_OK = 0,
    IMG_ERR_BAD = -1,           /* not a picture this library knows */
    IMG_ERR_UNSUPPORTED = -2,   /* a picture, but no decoder reads that flavour */
    IMG_ERR_NO_MEM = -3,
    IMG_ERR_FAILED = -4,        /* the decoder refused the data */
} img_err_t;

typedef struct {
    uint16_t *px;
    size_t size;                /* bytes allocated */
    int w, h;                   /* the picture */
    int stride;                 /* pixels a row, >= w */
    const char *decoder;        /* which one made it: "hardware", "tjpgd", "stb", "pngle", "j40" */
    const char *skipped;        /* why an earlier one was passed over, or NULL */
    const char *why;            /* on failure, the last decoder's own reason, or NULL */
    uint32_t sof_w, sof_h;      /* a JPEG's size from its header, even when refused */
    uint8_t sof;                /* and its SOF marker: 0xC2 is progressive */
} img_pic_t;

/* Stacks the two deep decoders are run on, PSRAM, for the length of one
 * decode. J40: 11.5 KB measured on a 64-bit host, so 24 KB. stb's GIF
 * reader: 35 KB frame plus 4096 recursions of 32 bytes, so 192 KB
 * (Defeatist's 6147, 6148). */
#define IMG_JXL_STACK  (24 * 1024)
#define IMG_GIF_STACK  (192 * 1024)

/* What the hardware JPEG engine says about a picture before it decodes. */
typedef struct {
    uint32_t width, height;
    int mcu_w, mcu_h;           /* from the chroma subsampling */
} img_hw_info_t;

/*
 * The backends and the figures. Every pointer may be NULL: a NULL decoder
 * is "not built here", and the order goes on to the next. A decoder
 * returns IMG_OK with *pic filled; IMG_ERR_NO_MEM when it could not
 * allocate; IMG_ERR_FAILED when it refused the data (the next decoder
 * may take it); IMG_ERR_UNSUPPORTED for what it cannot ever read.
 */
typedef struct img_ops {
    size_t (*psram_free)(void);         /* total free, bytes */
    size_t (*psram_largest)(void);      /* largest single block */

    /* Hardware JPEG. hw_ready makes the engine if it can: 0 means none. */
    int       (*hw_ready)(void);
    img_err_t (*hw_info)(const uint8_t *in, size_t len, img_hw_info_t *info);
    /* Decodes `in` to a buffer of exactly pad_w * pad_h * 2 bytes; fills
     * pic->px/size/stride. IMG_ERR_FAILED is a refusal. */
    img_err_t (*hw_decode)(const uint8_t *in, size_t len, const img_hw_info_t *info,
                           uint32_t pad_w, uint32_t pad_h, img_pic_t *pic);

    /* Software. soft_begin drops the calling task to idle priority and
     * returns what soft_end needs to put it back (5093). */
    unsigned long (*soft_begin)(void);
    void          (*soft_end)(unsigned long token);
    img_err_t (*tjpgd)(const uint8_t *in, size_t len, uint32_t w, uint32_t h,
                       int box_w, int box_h, img_pic_t *pic);
    img_err_t (*stb)(const uint8_t *in, size_t len, int box_w, int box_h, img_pic_t *pic);
    img_err_t (*gif)(const uint8_t *in, size_t len, int box_w, int box_h, img_pic_t *pic);
    img_err_t (*png)(const uint8_t *in, size_t len, int box_w, int box_h, uint16_t bg, img_pic_t *pic);
    img_err_t (*jxl)(const uint8_t *in, size_t len, int box_w, int box_h, uint16_t bg, img_pic_t *pic);
} img_ops_t;

/*
 * Decode `in` for a box_w x box_h box (the size to cover; 0 for the whole
 * picture). `bg` is the RGB565 colour transparency shows. On a failure
 * *pic has no pixels, and has sof/sof_w/sof_h/skipped/why for the log.
 */
img_err_t img_decode(const img_ops_t *ops, const uint8_t *in, size_t len,
                     int box_w, int box_h, uint16_t bg, img_pic_t *pic);

/* ---- the arithmetic, pure ------------------------------------------- */

/* The hardware decodes whole MCUs and writes the padding: the buffer is
 * the padded picture, and the padded width is the row stride. A 3000x3000
 * 4:2:0 cover is 3008x3008, not 3000x3000. */
static inline uint32_t img_pad_up(uint32_t v, int mcu)
{
    return ((v + (uint32_t)mcu - 1u) / (uint32_t)mcu) * (uint32_t)mcu;
}

/*
 * TJpgDec's first scale to try, as an index into 1/1, 1/2, 1/4, 1/8: the
 * gentlest reduction that still covers the box, never 1/1 (what the
 * hardware already failed to allocate), never past 1/8.
 */
static inline int img_tjpgd_first_scale(uint32_t w, uint32_t h, int box_w, int box_h)
{
    int idx = 1;
    while (idx < 3 && (int)(w >> (idx + 1)) >= box_w && (int)(h >> (idx + 1)) >= box_h) idx++;
    return idx;
}

#ifdef __cplusplus
}
#endif
