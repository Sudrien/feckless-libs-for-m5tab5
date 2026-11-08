/* SPDX-License-Identifier: MIT
 * Shared by box_calls.c and box_storage.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feckless_box_calls.h"

typedef feckless_umode_window_t win_t;
#define H(name) static bool name(win_t *w, const uint32_t a[7], uint32_t *ret)
#define FN(n) { #n, c_##n, NULL, 0 }

static inline bool ok_ptr(const win_t *w, uint32_t p, uint32_t len)
{
    return feckless_umode_range_ok(w, (uintptr_t)p, len);
}

/* A string the app passed, or NULL: terminated within max and the box. */
static inline const char *str(const win_t *w, uint32_t p, uint32_t max)
{
    return feckless_umode_strlen(w, (uintptr_t)p, max) >= 0 ? (const char *)(uintptr_t)p : NULL;
}

static inline feckless_box_t *box_of_win(win_t *w)
{
    return feckless_umode_calls_of(w)->box;
}

extern const feckless_umode_call_t box_storage_calls[];
extern const uint32_t box_storage_calls_n;
void box_storage_end(void);
