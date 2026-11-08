/* SPDX-License-Identifier: MIT
 *
 * See feckless_umode_box.h.
 */
#include "feckless_umode_box.h"

#include <string.h>

#include "feckless_umode_sys.h"

#define ALIGN_UP(x) (((x) + (FECKLESS_BOX_ALIGN - 1)) & ~(uint32_t)(FECKLESS_BOX_ALIGN - 1))

bool feckless_box_init(feckless_box_t *b, uintptr_t base, uint32_t size, uint32_t stack)
{
    memset(b, 0, sizeof *b);
    if (base == 0 || (base & (FECKLESS_BOX_ALIGN - 1)) != 0) {
        return false;
    }
    size &= ~(uint32_t)(FECKLESS_BOX_ALIGN - 1);
    stack = ALIGN_UP(stack);
    if (stack < FECKLESS_BOX_ALIGN || stack >= size) {
        return false;
    }
    if ((uint64_t)base + size > UINTPTR_MAX) {
        return false;
    }
    b->base = base;
    b->size = size;
    b->heap_end = size - stack;
    return true;
}

void feckless_box_stack(const feckless_box_t *b, uint32_t *lo, uint32_t *hi)
{
    *lo = (uint32_t)(b->base + b->heap_end);
    *hi = (uint32_t)(b->base + b->size - FECKLESS_BOX_ALIGN);   /* sp 16-aligned, inside */
}

/* Where the allocation starting at off is in the table, or -1. */
static int find(const feckless_box_t *b, uint32_t off)
{
    int lo = 0, hi = (int)b->n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (b->a[mid].off == off) {
            return mid;
        }
        if (b->a[mid].off < off) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

static bool off_of(const feckless_box_t *b, uintptr_t p, uint32_t *off)
{
    if (p < b->base || p >= b->base + b->heap_end) {
        return false;
    }
    *off = (uint32_t)(p - b->base);
    return true;
}

uintptr_t feckless_box_alloc(feckless_box_t *b, uint32_t len)
{
    if (len == 0 || b->n >= FECKLESS_BOX_MAX_ALLOCS || len > b->heap_end) {
        return 0;
    }
    const uint32_t need = ALIGN_UP(len);
    if (need < len) {
        return 0;
    }
    uint32_t at = 0;
    uint32_t i;
    for (i = 0; i < b->n; i++) {
        if (b->a[i].off >= at && b->a[i].off - at >= need) {
            break;
        }
        at = ALIGN_UP(b->a[i].off + b->a[i].len);
    }
    if (at > b->heap_end || b->heap_end - at < need) {
        return 0;
    }
    memmove(&b->a[i + 1], &b->a[i], (b->n - i) * sizeof b->a[0]);
    b->a[i].off = at;
    b->a[i].len = len;
    b->n++;
    return b->base + at;
}

bool feckless_box_free(feckless_box_t *b, uintptr_t p)
{
    uint32_t off;
    if (!off_of(b, p, &off)) {
        return false;
    }
    const int i = find(b, off);
    if (i < 0) {
        return false;
    }
    memmove(&b->a[i], &b->a[i + 1], (b->n - (uint32_t)i - 1) * sizeof b->a[0]);
    b->n--;
    return true;
}

uint32_t feckless_box_len(const feckless_box_t *b, uintptr_t p)
{
    uint32_t off;
    if (!off_of(b, p, &off)) {
        return 0;
    }
    const int i = find(b, off);
    return i < 0 ? 0 : b->a[i].len;
}

uintptr_t feckless_box_realloc(feckless_box_t *b, uintptr_t p, uint32_t len)
{
    if (p == 0) {
        return feckless_box_alloc(b, len);
    }
    uint32_t off;
    if (!off_of(b, p, &off)) {
        return 0;
    }
    const int i = find(b, off);
    if (i < 0) {
        return 0;
    }
    if (len == 0) {
        feckless_box_free(b, p);
        return 0;
    }
    /* In place: up to the next allocation, or heap_end. */
    const uint32_t limit = (uint32_t)i + 1 < b->n ? b->a[i + 1].off : b->heap_end;
    if (len <= limit - off) {
        b->a[i].len = len;
        return p;
    }
    const uint32_t old = b->a[i].len;
    const uintptr_t q = feckless_box_alloc(b, len);
    if (!q) {
        return 0;
    }
    memmove((void *)q, (const void *)p, old < len ? old : len);
    feckless_box_free(b, p);
    return q;
}

/* ---- the stubs --------------------------------------------------------- */

#define RV_ECALL    0x00000073u
#define RV_RET      0x00008067u                 /* jalr x0, 0(ra) */
#define RV_J_SELF   0x0000006fu                 /* jal x0, 0 */
#define RV_MV_T0_A7 0x00088293u                 /* addi t0, a7, 0 */
#define RV_ADDI(rd, imm) ((((uint32_t)(imm) & 0xfffu) << 20) | ((uint32_t)(rd) << 7) | 0x13u)
#define REG_A0  10
#define REG_A7  17

static void put(uintptr_t at, uint32_t insn)
{
    memcpy((void *)at, &insn, 4);               /* little-endian, as the target */
}

uintptr_t feckless_box_write_stubs(uintptr_t dst, uint32_t n)
{
    if (n > FECKLESS_BOX_MAX_STUBS || (dst & 3u) != 0) {
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uintptr_t s = dst + i * FECKLESS_BOX_STUB_SIZE;
        put(s, RV_MV_T0_A7);
        put(s + 4, RV_ADDI(REG_A7, FECKLESS_SYS_EXT_BASE + i));
        put(s + 8, RV_ECALL);
        put(s + 12, RV_RET);
    }
    const uintptr_t e = dst + n * FECKLESS_BOX_STUB_SIZE;
    put(e, RV_ADDI(REG_A7, FECKLESS_SYS_EXIT));
    put(e + 4, RV_ADDI(REG_A0, 0));
    put(e + 8, RV_ECALL);
    put(e + 12, RV_J_SELF);
    return e;
}
