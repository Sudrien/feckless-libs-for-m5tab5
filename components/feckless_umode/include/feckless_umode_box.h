/* SPDX-License-Identifier: MIT
 *
 * A box: one block of memory that is all a U-mode program may touch. It
 * holds the program's code and data (the loader allocates them here), the
 * ecall stubs it calls the host with, its heap and its stack. The PMP grants
 * the block and nothing else; feckless_umode_range_ok() checks against the
 * same range.
 *
 * Nothing about the allocations is kept in the block. A program can write
 * every byte of it, so a header beside each allocation would be a pointer
 * the program could aim the host's allocator with. The table is the box's
 * own, in host memory, and a free of anything that is not exactly the start
 * of a live allocation is refused.
 *
 * No ESP-IDF here; the host test runs it on a buffer.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FECKLESS_BOX_MAX_ALLOCS  256
#define FECKLESS_BOX_ALIGN       16     /* every allocation starts on this */
#define FECKLESS_BOX_STUB_SIZE   12     /* addi a7,x0,N ; ecall ; ret */
#define FECKLESS_BOX_EXIT_SIZE   16     /* addi a7,x0,EXIT ; addi a0,x0,0 ; ecall ; j . */
#define FECKLESS_BOX_MAX_STUBS   (2047 - 16)    /* N must fit addi's 12-bit immediate */

typedef struct {
    uint32_t off;
    uint32_t len;
} feckless_box_alloc_t;

typedef struct {
    uintptr_t base;                 /* the block; FECKLESS_BOX_ALIGN-aligned */
    uint32_t size;
    uint32_t heap_end;              /* offset: allocations stay below it; the stack is above */
    uint32_t n;                     /* live allocations, sorted by off */
    feckless_box_alloc_t a[FECKLESS_BOX_MAX_ALLOCS];
} feckless_box_t;

/* A box over [base, base+size). stack bytes at the top are the stack's and
 * never allocated. False if base is misaligned, or the stack does not leave
 * room. */
bool feckless_box_init(feckless_box_t *b, uintptr_t base, uint32_t size, uint32_t stack);

/* The stack: [lo, hi] for feckless_umode_ctx_init() (hi inclusive, sp starts
 * there, 16-aligned). */
void feckless_box_stack(const feckless_box_t *b, uint32_t *lo, uint32_t *hi);

/* First fit below heap_end. 0 when it does not fit, len is 0, or the table is
 * full. The memory is not cleared. */
uintptr_t feckless_box_alloc(feckless_box_t *b, uint32_t len);

/* Free the allocation that starts at p. False, and nothing freed, for any p
 * that is not one. */
bool feckless_box_free(feckless_box_t *b, uintptr_t p);

/* The live allocation that starts at p: its length, or 0. */
uint32_t feckless_box_len(const feckless_box_t *b, uintptr_t p);

/* Grow or shrink: in place when the room after it is free, else a new
 * allocation, the old contents copied, the old freed. 0 on failure, and the
 * old one is kept. realloc(0, n) is alloc; realloc(p, 0) frees and is 0. */
uintptr_t feckless_box_realloc(feckless_box_t *b, uintptr_t p, uint32_t len);

/* ---- the stubs --------------------------------------------------------- */

/* Write n stubs at dst, stub i doing ecall FECKLESS_SYS_EXT_BASE + i and
 * returning, then the exit stub (ra for a program's entry and its
 * constructors: returning means SYS_EXIT 0). dst is in the box and 4-aligned;
 * n <= FECKLESS_BOX_MAX_STUBS. Returns the exit stub's address, 0 on a bad n.
 * The caller makes them visible to instruction fetch (a cache sync). */
uintptr_t feckless_box_write_stubs(uintptr_t dst, uint32_t n);

/* Bytes the stubs and the exit stub take. */
#define FECKLESS_BOX_STUBS_BYTES(n)  ((uint32_t)(n) * FECKLESS_BOX_STUB_SIZE + FECKLESS_BOX_EXIT_SIZE)

#ifdef __cplusplus
}
#endif
