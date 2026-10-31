/*
 * feckless_image_fit.h -- a decoded picture, fitted to a box (7009; the
 * body of Defeatist's blit_cover(), without the panel in it).
 *
 *   img_fit_size(pic->w, pic->h, box_w, box_h, &cw, &ch, &k);
 *   // the app decides where cw x ch sits: centred, offset, keystoned...
 *   img_fit_draw(&pic, fb + y * fb_stride + x, fb_stride, cw, ch, k);
 *
 * Fit, not fill: nothing is cropped; the short axis is left for the
 * caller's background. Both directions (a 300 px cover in a 720 px box
 * is enlarged), nearest neighbour. Whole pixels (`k`) for a small
 * picture, where a 16.16 step would make the first block of a 3x
 * enlargement four wide (5088). The source may have a stride wider than
 * its width: the hardware JPEG engine's MCU padding.
 *
 * Nothing here touches a framebuffer, a panel or the heap.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "feckless_image_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The size the picture takes in a box, and `*k`: the whole-number scale
 * when img_int_scale() chose one (cw = iw*k, ch = ih*k), else 0. Never
 * below 1x1. iw, ih, box_w, box_h are all >= 1 or the answer is 1x1. */
void img_fit_size(int iw, int ih, int box_w, int box_h, int *cw, int *ch, int *k);

/* Writes the picture at cw x ch into `dst` (stride `dst_stride` pixels).
 * `k` as img_fit_size() returned it. pic->px NULL, or a size < 1, does
 * nothing. */
void img_fit_draw(const img_pic_t *pic, uint16_t *dst, size_t dst_stride, int cw, int ch, int k);

#ifdef __cplusplus
}
#endif
