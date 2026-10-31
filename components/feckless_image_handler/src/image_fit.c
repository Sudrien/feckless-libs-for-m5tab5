/*
 * image_fit.c -- see feckless_image_fit.h. blit_cover()'s arithmetic,
 * unchanged: the fit by cross-multiplication, the 16.16 step, the exact
 * blocks for a whole-number scale.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_image_fit.h"

#include <string.h>

#include "feckless_image.h"

void img_fit_size(int iw, int ih, int box_w, int box_h, int *cw, int *ch, int *k)
{
    int w = 1, h = 1, kk = 0;
    if (iw >= 1 && ih >= 1 && box_w >= 1 && box_h >= 1) {
        if ((int64_t)iw * box_h >= (int64_t)ih * box_w) {   /* width binds */
            w = box_w;
            h = (int)(((int64_t)ih * box_w) / iw);
        } else {
            h = box_h;
            w = (int)(((int64_t)iw * box_h) / ih);
        }
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        kk = img_int_scale(iw, ih, box_w, box_h);
        if (kk) { w = iw * kk; h = ih * kk; }
    }
    if (cw) *cw = w;
    if (ch) *ch = h;
    if (k) *k = kk;
}

void img_fit_draw(const img_pic_t *pic, uint16_t *dst, size_t dst_stride, int cw, int ch, int k)
{
    if (!pic || !pic->px || !dst || pic->w < 1 || pic->h < 1 || cw < 1 || ch < 1) return;
    const int iw = pic->w, ih = pic->h;
    const size_t stride = pic->stride >= iw ? (size_t)pic->stride : (size_t)iw;

    /* 16.16, so the picture lands on the box exactly: 1920 into 720 is
     * 720 whole, not 960 cropped. */
    const uint32_t xstep = (uint32_t)(((uint64_t)iw << 16) / (uint32_t)cw);
    const uint32_t ystep = (uint32_t)(((uint64_t)ih << 16) / (uint32_t)ch);

    for (int y = 0; y < ch; y++) {
        int srow = k ? y / k : (int)(((uint32_t)y * ystep) >> 16);
        if (srow >= ih) srow = ih - 1;
        const uint16_t *row = &pic->px[(size_t)srow * stride];
        uint16_t *out = &dst[(size_t)y * dst_stride];

        if (xstep == (1u << 16)) {
            memcpy(out, row, (size_t)cw * 2);
        } else if (k) {
            /* 1/3 in 16.16 is 21845, and 3 of them fall one short of a
             * source pixel: exact blocks, or the first is four wide. */
            for (int x = 0; x < cw; x++) out[x] = row[x / k];
        } else {
            uint32_t sxf = 0;
            for (int x = 0; x < cw; x++, sxf += xstep) {
                int sc = (int)(sxf >> 16);
                if (sc >= iw) sc = iw - 1;
                out[x] = row[sc];
            }
        }
    }
}
