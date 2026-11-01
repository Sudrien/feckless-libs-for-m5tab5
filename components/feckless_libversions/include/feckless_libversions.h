/* SPDX-License-Identifier: MIT
 *
 * feckless_libversions: the libraries an app was built from, with their
 * versions and licences, as a table. Avoidant, Lothesome, Mahjong, Errant
 * and Defeatist each kept a copy of the generator; this is one.
 *
 * feckless_libversions() (the component's project_include.cmake) writes,
 * at configure time, from the build's own records -- the lock, vendored
 * components, the fonts, the image decoders' pins, and the app's own
 * EXTRA rows -- so nothing drifts from what was compiled:
 *
 *   libversions.h     k_libversions[LIBVERSIONS_COUNT], a libv_t each
 *   libversions.tsv   the same rows, tab-separated with a header line,
 *                     for documentation or a release
 *
 * Drawing it is the app's. The generated header defines libv_t itself,
 * so including it needs no REQUIRES; this header is the same type, for
 * code that takes rows without including the generated table.
 */
#pragma once

#ifndef FECKLESS_LIBV_T
#define FECKLESS_LIBV_T
/* One library. */
typedef struct {
    const char *module;     /* "ESP-IDF", "feckless_keyboard", ... */
    const char *checkout;   /* a version, a git commit's first seven, a date */
    const char *licence;    /* SPDX where there is one */
} libv_t;
#endif
