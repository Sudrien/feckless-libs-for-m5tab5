/* SPDX-License-Identifier: MIT
 *
 * feckless_libversions: the libraries an app was built from, with their
 * versions and licences, for its settings or about page. Avoidant,
 * Lothesome, Mahjong and Defeatist each kept a copy; this is one.
 *
 * Two halves. At configure time, feckless_libversions() (the component's
 * project_include.cmake) writes libversions.h from the build's own
 * records -- the lock, vendored components, the fonts -- so the screen
 * cannot drift from what was compiled:
 *
 *   #include "libversions.h"      k_libversions[], LIBVERSIONS_COUNT
 *
 * At run time, libv_draw() draws them as three columns into a band of the
 * framebuffer. As with feckless_keyboard, it owns no hardware and no
 * task: the app draws, blits and reads its own taps.
 *
 * Plain code, so it links into an app's ELF (ELF_COMPONENTS); the gfx
 * calls it makes are the ABI's.
 */
#pragma once

#include <stdint.h>

/* One library. */
typedef struct {
    const char *module;     /* "ESP-IDF", "feckless_keyboard", ... */
    const char *checkout;   /* a version, or a git commit's first seven */
    const char *licence;    /* SPDX where there is one */
} libv_t;

/* Where and how it is drawn: a band w by h from (x, y), a row every
 * row_h pixels (0: the glyph's height and 10), columns at 0, 45% and 70%
 * of w -- module, checkout, licence -- each cut to its column. */
typedef struct {
    int x, y, w, h;
    int scale;              /* gfx text scale */
    int row_h;
    uint16_t text;          /* the module's colour */
    uint16_t dim;           /* the checkout's, the licence's, "and N more" */
} libv_view_t;

/* The rows that fit in the band. */
int libv_rows(const libv_view_t *v);

/* list[first..n-1] into the band, as many as fit; when they do not all
 * fit, the last row says "and N more" instead. Returns how many entries
 * were drawn, so an app that pages starts the next page at first + that.
 * The caller blits. */
int libv_draw(const libv_view_t *v, const libv_t *list, int n, int first);
