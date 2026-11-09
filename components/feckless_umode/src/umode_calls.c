/* SPDX-License-Identifier: MIT
 *
 * See feckless_umode_calls.h. No ESP-IDF.
 */
#include "feckless_umode_calls.h"

#include <string.h>

bool feckless_umode_calls_ext(feckless_umode_window_t *w, uint32_t nr, const uint32_t args[7], uint32_t *ret)
{
    const feckless_umode_calls_t *t = feckless_umode_calls_of(w);
    const uint32_t i = nr - FECKLESS_SYS_EXT_BASE;
    if (!t || nr < FECKLESS_SYS_EXT_BASE || i >= t->n || !t->calls[i].fn) {
        *ret = FECKLESS_SYS_ERR_BADNR;
        return true;
    }
    return t->calls[i].fn(w, args, ret);
}

static feckless_box_t *box_of(feckless_umode_window_t *w)
{
    return feckless_umode_calls_of(w)->box;
}

static bool c_malloc(feckless_umode_window_t *w, const uint32_t a[7], uint32_t *ret)
{
    *ret = (uint32_t)feckless_box_alloc(box_of(w), a[0]);
    return true;
}

static bool c_heap_caps_malloc(feckless_umode_window_t *w, const uint32_t a[7], uint32_t *ret)
{
    *ret = (uint32_t)feckless_box_alloc(box_of(w), a[0]);     /* a[1], the caps: the box is all there is */
    return true;
}

static bool c_calloc(feckless_umode_window_t *w, const uint32_t a[7], uint32_t *ret)
{
    const uint64_t len = (uint64_t)a[0] * a[1];
    *ret = 0;
    if (len == 0 || len > UINT32_MAX) {
        return true;
    }
    const uintptr_t p = feckless_box_alloc(box_of(w), (uint32_t)len);
    if (p) {
        memset((void *)p, 0, (size_t)len);
    }
    *ret = (uint32_t)p;
    return true;
}

static bool c_realloc(feckless_umode_window_t *w, const uint32_t a[7], uint32_t *ret)
{
    *ret = (uint32_t)feckless_box_realloc(box_of(w), a[0], a[1]);
    return true;
}

static bool c_free(feckless_umode_window_t *w, const uint32_t a[7], uint32_t *ret)
{
    if (a[0]) {
        feckless_box_free(box_of(w), a[0]);     /* a bad pointer is refused, not a crash */
    }
    *ret = 0;
    return true;
}

const feckless_umode_call_t feckless_umode_heap_calls[] = {
    { "malloc", c_malloc, NULL, 0, 0 },
    { "calloc", c_calloc, NULL, 0, 0 },
    { "realloc", c_realloc, NULL, 0, 0 },
    { "free", c_free, NULL, 0, 0 },
    { "heap_caps_malloc", c_heap_caps_malloc, NULL, 0, 0 },
};
const uint32_t feckless_umode_heap_calls_n = sizeof feckless_umode_heap_calls / sizeof feckless_umode_heap_calls[0];
