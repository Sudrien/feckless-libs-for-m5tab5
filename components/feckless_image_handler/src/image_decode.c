/*
 * image_decode.c -- see feckless_image_decode.h. The order of
 * Defeatist's albumart_draw() and its software ladder, with the
 * decoders behind img_ops_t.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_image_decode.h"

#include <stdlib.h>
#include <string.h>

#include "feckless_image.h"
#include "feckless_image_jxl.h"
#include "feckless_image_png.h"
#include "feckless_image_stb.h"

static size_t psram_free(const img_ops_t *o)    { return o->psram_free ? o->psram_free() : (size_t)-1; }
static size_t psram_largest(const img_ops_t *o) { return o->psram_largest ? o->psram_largest() : (size_t)-1; }

/*
 * stb_image's turn, for a JPEG neither of the others will read -- a
 * progressive cover, or a flavour TJpgDec calls JDR_FMT3 -- asked about
 * memory first, because a progressive decode holds every coefficient of
 * the picture at once (about 10 bytes a pixel): refused with a reason
 * rather than by the allocator halfway through.
 */
static img_err_t run_stb(const img_ops_t *o, const uint8_t *in, size_t len,
                         int box_w, int box_h, img_pic_t *pic)
{
    if (!o->stb) { pic->why = "stb_image is not built here"; return IMG_ERR_UNSUPPORTED; }
    int w = 0, h = 0;
    const img_stb_err_t info = img_stb_info(in, len, &w, &h);
    if (info != IMG_STB_OK) { pic->why = img_stb_err_name(info); return IMG_ERR_UNSUPPORTED; }
    if (psram_free(o) < img_stb_peak_bytes(w, h)) {
        pic->why = "not enough PSRAM for stb_image's peak";
        return IMG_ERR_NO_MEM;
    }
    return o->stb(in, len, box_w, box_h, pic);
}

/* The software ladder: TJpgDec, and stb_image when TJpgDec refuses the
 * FORMAT (FAILED) rather than the memory (NO_MEM stays NO_MEM). At idle
 * priority for the length of it. */
static img_err_t soft_ladder(const img_ops_t *o, const uint8_t *in, size_t len,
                             uint32_t w, uint32_t h, int box_w, int box_h, img_pic_t *pic)
{
    const unsigned long tok = o->soft_begin ? o->soft_begin() : 0;
    img_err_t r = IMG_ERR_FAILED;
    if (o->tjpgd) r = o->tjpgd(in, len, w, h, box_w, box_h, pic);
    if (r == IMG_ERR_FAILED) {
        pic->skipped = "TJpgDec refused it";
        r = run_stb(o, in, len, box_w, box_h, pic);
        if (r == IMG_ERR_UNSUPPORTED) r = IMG_ERR_FAILED;
    }
    if (o->soft_end) o->soft_end(tok);
    return r;
}

static img_err_t jpeg(const img_ops_t *o, const uint8_t *in, size_t len,
                      int box_w, int box_h, img_pic_t *pic)
{
    uint8_t sof = 0;
    uint32_t sw = 0, sh = 0;
    const bool baseline = img_jpeg_sof(in, len, &sof, &sw, &sh);
    pic->sof = sof;
    pic->sof_w = sw;
    pic->sof_h = sh;

    if (!baseline) {
        if (sof != 0xC2) {
            pic->why = "a JPEG flavour no decoder here reads";
            return IMG_ERR_UNSUPPORTED;
        }
        /* Progressive: only stb_image reads it, at idle priority. */
        const unsigned long tok = o->soft_begin ? o->soft_begin() : 0;
        const img_err_t r = run_stb(o, in, len, box_w, box_h, pic);
        if (o->soft_end) o->soft_end(tok);
        return r;
    }

    /* The hardware: there and ready, a picture it will take, and room in
     * one block for the padded result. Anything else goes to software --
     * a hardware refusal used to be the end of the picture, and the
     * software decoders have no size limit or grid. */
    img_hw_info_t hw = {0};
    const char *skip = NULL;
    if (!o->hw_ready || !o->hw_ready()) {
        skip = "no hardware JPEG engine";
    } else if (!o->hw_info || o->hw_info(in, len, &hw) != IMG_OK) {
        skip = "hardware JPEG could not read the header";
    } else {
        const uint32_t pad_w = img_pad_up(hw.width, hw.mcu_w);
        const uint32_t pad_h = img_pad_up(hw.height, hw.mcu_h);
        const size_t want = (size_t)pad_w * pad_h * 2u;
        if (psram_largest(o) < want) {
            skip = "no block of PSRAM big enough for the hardware's output";
        } else if (!o->hw_decode) {
            skip = "hardware JPEG is not built here";
        } else {
            const img_err_t r = o->hw_decode(in, len, &hw, pad_w, pad_h, pic);
            if (r == IMG_OK) {
                /* The decoder reports what it wrote; everything after
                 * indexes at a stride derived from the header, and a
                 * disagreement is a sheared picture, not an error. */
                if (pic->size != want || pic->stride != (int)pad_w) {
                    free(pic->px);
                    pic->px = NULL;
                    pic->why = "the hardware wrote a different size than its header implied";
                    return IMG_ERR_FAILED;
                }
                pic->w = (int)hw.width;
                pic->h = (int)hw.height;
                pic->decoder = "hardware";
                return IMG_OK;
            }
            skip = r == IMG_ERR_NO_MEM ? "no memory for the hardware's buffers"
                                       : "hardware JPEG refused it";
        }
    }

    pic->skipped = skip;
    return soft_ladder(o, in, len, hw.width ? hw.width : sw, hw.height ? hw.height : sh,
                       box_w, box_h, pic);
}

img_err_t img_decode(const img_ops_t *o, const uint8_t *in, size_t len,
                     int box_w, int box_h, uint16_t bg, img_pic_t *pic)
{
    memset(pic, 0, sizeof *pic);
    if (!o) return IMG_ERR_UNSUPPORTED;

    const img_format_t fmt = img_sniff(in, len);
    img_err_t r;
    switch (fmt) {
    case IMG_JPEG:
        r = jpeg(o, in, len, box_w, box_h, pic);
        break;

    case IMG_GIF: {
        if (!o->gif) { pic->why = "GIF is not built here"; return IMG_ERR_UNSUPPORTED; }
        int w = 0, h = 0;
        const img_stb_err_t info = img_stb_info(in, len, &w, &h);
        if (info != IMG_STB_OK) { pic->why = img_stb_err_name(info); return IMG_ERR_UNSUPPORTED; }
        if (psram_free(o) < img_stb_gif_peak_bytes(w, h)) {
            pic->why = "not enough PSRAM for a GIF's peak";
            return IMG_ERR_NO_MEM;
        }
        const unsigned long tok = o->soft_begin ? o->soft_begin() : 0;
        r = o->gif(in, len, box_w, box_h, pic);
        if (o->soft_end) o->soft_end(tok);
        break;
    }

    case IMG_PNG: {
        if (!o->png) { pic->why = "PNG is not built here"; return IMG_ERR_UNSUPPORTED; }
        const unsigned long tok = o->soft_begin ? o->soft_begin() : 0;
        r = o->png(in, len, box_w, box_h, bg, pic);
        if (o->soft_end) o->soft_end(tok);
        break;
    }

    case IMG_JXL: {
        if (!o->jxl) { pic->why = "JPEG XL is not built here"; return IMG_ERR_UNSUPPORTED; }
        int w = 0, h = 0;
        const img_jxl_err_t info = img_jxl_info(in, len, &w, &h);
        if (info != IMG_JXL_OK) { pic->why = img_jxl_err_name(info); return IMG_ERR_UNSUPPORTED; }
        if (psram_free(o) < img_jxl_peak_bytes(w, h) + IMG_JXL_STACK) {
            pic->why = "not enough PSRAM for J40's peak";
            return IMG_ERR_NO_MEM;
        }
        r = o->jxl(in, len, box_w, box_h, bg, pic);
        break;
    }

    default:
        return IMG_ERR_BAD;
    }

    if (r == IMG_OK && (!pic->px || pic->w < 1 || pic->h < 1)) r = IMG_ERR_FAILED;
    if (r != IMG_OK) {
        free(pic->px);          /* a backend that failed owns nothing, but be sure */
        pic->px = NULL;
    }
    return r;
}
