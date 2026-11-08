/* SPDX-License-Identifier: MIT
 *
 * The names a boxed program may import, and what each does on the host.
 *
 * A function entry becomes a stub in the box: an ecall numbered
 * FECKLESS_SYS_EXT_BASE + its index. The loader resolves the program's import
 * to the stub, so the program calls it like any function; the window's ext
 * hook, feckless_umode_calls_ext(), sends the ecall to `fn` with the
 * program's a0..a6. A data entry is copied into the box at load and the import
 * resolves to the copy: the program reads its own copy, never the host's
 * variable (the ABI's version symbols are such entries).
 *
 * An import with no entry here fails the load by name, as with ABI v1. An
 * import that somehow resolved to a host address would fault on its first
 * use, not run: the PMP grants the box and nothing else.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feckless_umode_box.h"
#include "feckless_umode_run.h"

#ifdef __cplusplus
extern "C" {
#endif

/* args: a0..a6 as the program left them. Check every pointer with
 * feckless_umode_range_ok(w, ...) before using it. Set *ret; return false to
 * retire the program. */
typedef bool (*feckless_umode_call_fn)(feckless_umode_window_t *w, const uint32_t args[7], uint32_t *ret);

typedef struct {
    const char *name;
    feckless_umode_call_fn fn;      /* a function; or NULL and data: */
    const void *data;               /* copied into the box at load */
    uint32_t data_len;
} feckless_umode_call_t;

typedef struct {
    const feckless_umode_call_t *calls;
    uint32_t n;
    feckless_box_t *box;            /* for calls that allocate (malloc and the rest) */
} feckless_umode_calls_t;

/* The window's ext hook; w->ext_arg is a feckless_umode_calls_t. An ecall
 * numbered past the table, or at a data entry, is FECKLESS_SYS_ERR_BADNR. */
bool feckless_umode_calls_ext(feckless_umode_window_t *w, uint32_t nr, const uint32_t args[7], uint32_t *ret);

/* Argument i (from 0) of the call being serviced, as the C calling convention
 * put it: a0..a6 are args[0..6]; the eighth, a7, is in t0 (the stub moved it
 * there); from the ninth on they are on the program's stack, at sp, sp+4, ...
 * -- read only if inside the window, else *ok is set false and 0 returned.
 * For int, pointer and uint32 arguments; a 64-bit one is two in a row. */
static inline uint32_t feckless_umode_arg(const feckless_umode_window_t *w, unsigned i, bool *ok)
{
    if (i < 7) {
        return w->ctx.x[10 + i];
    }
    if (i == 7) {
        return w->ctx.x[5];
    }
    const uintptr_t at = (uintptr_t)w->ctx.x[2] + 4u * (i - 8);
    if (!feckless_umode_range_ok(w, at, 4)) {
        *ok = false;
        return 0;
    }
    return *(const uint32_t *)at;
}

/* A 64-bit result: the low word is *ret, the high one goes to a1 here, which
 * the host loop leaves as the handler wrote it. */
static inline void feckless_umode_ret64(feckless_umode_window_t *w, uint64_t v, uint32_t *ret)
{
    *ret = (uint32_t)v;
    w->ctx.x[11] = (uint32_t)(v >> 32);
}

/* The calls' table as the box's ext hook will see it: w->ext_arg. */
static inline const feckless_umode_calls_t *feckless_umode_calls_of(const feckless_umode_window_t *w)
{
    return (const feckless_umode_calls_t *)w->ext_arg;
}

/* The heap calls every program needs, over the box's allocator:
 * malloc, calloc, realloc, free, heap_caps_malloc (caps ignored: the box is
 * the program's only memory). Append them to an embedder's table. */
extern const feckless_umode_call_t feckless_umode_heap_calls[];
extern const uint32_t feckless_umode_heap_calls_n;

#ifdef __cplusplus
}
#endif
