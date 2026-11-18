/* SPDX-License-Identifier: MIT
 *
 * Host test for umode_box.c and umode_calls.c as they are: the box's
 * allocator, the stubs (written to stubs.bin for `make asm` to compare with
 * the assembler's), and the call table.
 *
 *   make -C components/feckless_umode/test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "feckless_umode_calls.h"

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

/* The calls return pointers in a uint32_t, as the 32-bit target does, so on a
 * 64-bit host the box must be below 4 GB: MAP_32BIT. */
#define MEM_SIZE (64 * 1024)
static uint8_t *mem;
static feckless_box_t box;
#define B ((uintptr_t)mem)

static void test_init(void)
{
    CHECK(!feckless_box_init(&box, B + 4, MEM_SIZE, 1024));      /* misaligned */
    CHECK(!feckless_box_init(&box, 0, MEM_SIZE, 1024));
    CHECK(!feckless_box_init(&box, B, 1024, 1024));                /* no room for a heap */
    CHECK(!feckless_box_init(&box, B, MEM_SIZE, 0));
    CHECK(feckless_box_init(&box, B, MEM_SIZE, 4096));
    CHECK(box.heap_end == MEM_SIZE - 4096);
    uint32_t lo, hi;
    feckless_box_stack(&box, &lo, &hi);
    CHECK(lo == B + MEM_SIZE - 4096 && hi == B + MEM_SIZE - 16 && hi % 16 == 0);
}

static void test_alloc_free(void)
{
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    const uintptr_t a = feckless_box_alloc(&box, 10);
    const uintptr_t b = feckless_box_alloc(&box, 100);
    const uintptr_t c = feckless_box_alloc(&box, 16);
    CHECK(a == B && b == B + 16 && c == B + 16 + 112);
    CHECK(a % 16 == 0 && b % 16 == 0 && c % 16 == 0);
    CHECK(feckless_box_len(&box, b) == 100);
    CHECK(feckless_box_free(&box, b));
    CHECK(!feckless_box_free(&box, b));                 /* twice */
    CHECK(!feckless_box_free(&box, a + 1));             /* not a start */
    CHECK(!feckless_box_free(&box, B + box.heap_end));  /* the stack */
    CHECK(!feckless_box_free(&box, B - 16));            /* outside */
    CHECK(!feckless_box_free(&box, (uintptr_t)&box));   /* the host's own memory */
    CHECK(feckless_box_alloc(&box, 112) == b);          /* the hole is reused, first fit */
    CHECK(feckless_box_alloc(&box, 0) == 0);
    CHECK(feckless_box_alloc(&box, box.heap_end) == 0); /* too big now */
    CHECK(feckless_box_alloc(&box, 0xfffffff8u) == 0);  /* rounding would wrap */
}

/* u-feckless-0065: aligned on the address, in the holes, freed as any */
static void test_alloc_aligned(void)
{
    /* a base only 16-aligned, so the offset alone would give the wrong start */
    feckless_box_init(&box, B + 16, MEM_SIZE - 16, 4096);
    const uintptr_t a = feckless_box_alloc(&box, 10);                /* B+16 */
    const uintptr_t x = feckless_box_alloc_aligned(&box, 1000, 128);
    CHECK(a == B + 16 && x == B + 128 && x % 128 == 0);
    const uintptr_t c = feckless_box_alloc(&box, 16);                /* first fit: the gap before x */
    CHECK(c == B + 32);
    const uintptr_t y = feckless_box_alloc_aligned(&box, 256, 128);  /* after x, on the next line */
    CHECK(y % 128 == 0 && y == B + 1152);
    CHECK(feckless_box_alloc(&box, 32) == B + 48);                   /* what is left of that gap */
    CHECK(feckless_box_free(&box, x) && !feckless_box_free(&box, x));
    CHECK(feckless_box_alloc_aligned(&box, 64, 128) == x);           /* its hole again */
    CHECK(feckless_box_alloc_aligned(&box, 64, 0) == 0);
    CHECK(feckless_box_alloc_aligned(&box, 64, 8) == 0);             /* under the box's own */
    CHECK(feckless_box_alloc_aligned(&box, 64, 96) == 0);            /* not a power of two */
    CHECK(feckless_box_alloc_aligned(&box, 64, 1u << 30) == 0);
    CHECK(feckless_box_alloc_aligned(&box, box.heap_end, 128) == 0);
    CHECK(feckless_box_alloc_aligned(&box, 64, 16) != 0);            /* 16 is plain alloc */
}

static void test_fill_and_table_limit(void)
{
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    const uint32_t heap = box.heap_end;
    CHECK(feckless_box_alloc(&box, heap) == B);         /* exactly all of it */
    CHECK(feckless_box_alloc(&box, 1) == 0);
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    int k = 0;
    while (feckless_box_alloc(&box, 1)) k++;
    CHECK(k == FECKLESS_BOX_MAX_ALLOCS);                /* the table, not the memory, ran out */
    for (uint32_t i = 1; i < box.n; i++) CHECK(box.a[i].off > box.a[i - 1].off);
}

/* u-feckless-0048: a bigger table, given after init, and the heap's stats */
static void test_bigger_table(void)
{
    static feckless_box_alloc_t big[1000];
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    int k = 0;
    while (feckless_box_alloc(&box, 1)) k++;
    CHECK(k == FECKLESS_BOX_MAX_ALLOCS);
    CHECK(!feckless_box_set_table(&box, big, 100));                 /* the live ones do not fit */
    CHECK(box.a == box.own && box.cap == FECKLESS_BOX_MAX_ALLOCS);
    CHECK(!feckless_box_set_table(&box, NULL, 1000));
    CHECK(feckless_box_set_table(&box, big, 1000));
    CHECK(box.a == big && box.n == (uint32_t)k && big[k - 1].off == (uint32_t)(k - 1) * 16);
    while (feckless_box_alloc(&box, 1)) k++;
    CHECK(k == 1000);                                               /* 16 KB of 60: the table again */
    for (uint32_t i = 1; i < box.n; i++) CHECK(box.a[i].off > box.a[i - 1].off);
    CHECK(feckless_box_free(&box, B + 16 * 500) && feckless_box_len(&box, B + 16 * 501) == 1);

    feckless_box_stats_t st;
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    feckless_box_stats(&box, &st);
    CHECK(st.total == box.heap_end && st.free == box.heap_end && st.largest == box.heap_end);
    CHECK(st.blocks == 0 && st.free_blocks == 1 && st.min_free == box.heap_end);
    const uintptr_t a = feckless_box_alloc(&box, 100);              /* 112 placed */
    const uintptr_t b = feckless_box_alloc(&box, 1000);             /* 1008 */
    const uintptr_t c = feckless_box_alloc(&box, 10);               /* 16 */
    (void)a; (void)c;
    CHECK(feckless_box_free(&box, b));
    feckless_box_stats(&box, &st);
    CHECK(st.free == box.heap_end - 128 && st.blocks == 2 && st.free_blocks == 2);
    CHECK(st.largest == box.heap_end - 112 - 1008 - 16);
    CHECK(st.min_free == box.heap_end - 112 - 1008 - 16);           /* the least it was */
    CHECK(feckless_box_realloc(&box, a, 600) == a);                 /* into b's hole: 608 */
    feckless_box_stats(&box, &st);
    CHECK(st.free == box.heap_end - 608 - 16 && box.used == 608 + 16);
}

static void test_realloc(void)
{
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    const uintptr_t a = feckless_box_alloc(&box, 8);
    memcpy((void *)a, "abcdefg", 8);
    CHECK(feckless_box_realloc(&box, a, 16) == a);      /* grows in place into its padding */
    const uintptr_t b = feckless_box_alloc(&box, 8);
    const uintptr_t a2 = feckless_box_realloc(&box, a, 100);   /* b is in the way: moves */
    CHECK(a2 && a2 != a && strcmp((char *)a2, "abcdefg") == 0);
    CHECK(feckless_box_len(&box, a) == 0 && feckless_box_len(&box, a2) == 100);
    CHECK(feckless_box_realloc(&box, a2 + 4, 10) == 0);        /* not a start */
    CHECK(feckless_box_len(&box, a2) == 100);
    CHECK(feckless_box_realloc(&box, a2, 0xffff0) == 0);       /* does not fit: the old one is kept */
    CHECK(feckless_box_len(&box, a2) == 100);
    CHECK(feckless_box_realloc(&box, b, 0) == 0 && feckless_box_len(&box, b) == 0);
    const uintptr_t n = feckless_box_realloc(&box, 0, 32);
    CHECK(n && feckless_box_len(&box, n) == 32);
}

/* Random alloc/free/realloc against an independent check: no two live
 * allocations overlap, all are inside the heap, and the table stays sorted. */
static void test_random(void)
{
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    uintptr_t live[200] = { 0 };
    srand(12345);
    for (int step = 0; step < 20000; step++) {
        const int i = rand() % 200;
        const int op = rand() % 3;
        if (op == 0 && !live[i]) {
            live[i] = feckless_box_alloc(&box, 1 + (uint32_t)(rand() % 600));
        } else if (op == 1 && live[i]) {
            CHECK(feckless_box_free(&box, live[i]));
            live[i] = 0;
        } else if (op == 2 && live[i]) {
            const uintptr_t r = feckless_box_realloc(&box, live[i], 1 + (uint32_t)(rand() % 900));
            if (r) live[i] = r;
        }
        if (step % 500 == 0) {
            for (uint32_t k = 1; k < box.n; k++) {
                CHECK(box.a[k].off >= box.a[k - 1].off + box.a[k - 1].len);
            }
            if (box.n) CHECK(box.a[box.n - 1].off + box.a[box.n - 1].len <= box.heap_end);
        }
    }
    int count = 0;
    uint32_t used = 0;
    for (int i = 0; i < 200; i++) if (live[i]) {
        count++;
        CHECK(feckless_box_len(&box, live[i]) > 0);
        used += (feckless_box_len(&box, live[i]) + 15u) & ~15u;
    }
    CHECK((uint32_t)count == box.n);
    feckless_box_stats_t st;                                        /* u-feckless-0048 */
    feckless_box_stats(&box, &st);
    CHECK(box.used == used && st.free == box.heap_end - used && st.min_free <= st.free);
}

static void test_stubs(void)
{
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    const uintptr_t s = feckless_box_alloc(&box, FECKLESS_BOX_STUBS_BYTES(3));
    CHECK(feckless_box_write_stubs(s + 2, 3) == 0);                    /* misaligned */
    CHECK(feckless_box_write_stubs(s, FECKLESS_BOX_MAX_STUBS + 1) == 0);
    const uintptr_t e = feckless_box_write_stubs(s, 3);
    CHECK(e == s + 3 * FECKLESS_BOX_STUB_SIZE);
    FILE *f = fopen("stubs.bin", "wb");                                /* for make asm */
    CHECK(f && fwrite((void *)s, 1, FECKLESS_BOX_STUBS_BYTES(3), f) == FECKLESS_BOX_STUBS_BYTES(3));
    if (f) fclose(f);
    /* the largest number still fits addi */
    feckless_box_init(&box, B, MEM_SIZE, 1024);
    CHECK(feckless_box_write_stubs(B, FECKLESS_BOX_MAX_STUBS) != 0);
    uint32_t last;
    memcpy(&last, (void *)(B + (FECKLESS_BOX_MAX_STUBS - 1) * FECKLESS_BOX_STUB_SIZE + 4), 4);
    CHECK((last >> 20) == 2046u);                                      /* EXT_BASE + MAX - 1 */
}

static int seen;
static bool c_add(feckless_umode_window_t *w, const uint32_t a[7], uint32_t *ret)
{
    (void)w;
    seen++;
    *ret = a[0] + a[6];
    return true;
}

static void test_calls(void)
{
    feckless_box_init(&box, B, MEM_SIZE, 4096);
    static const uint32_t ver = 1;
    const feckless_umode_call_t calls[] = {
        { "add", c_add, NULL, 0 },
        { "feckless_abi_v1", NULL, &ver, 4 },
    };
    feckless_umode_calls_t t = { calls, 2, &box };
    feckless_umode_window_t w;
    memset(&w, 0, sizeof w);
    w.ext_arg = &t;
    uint32_t args[7] = { 40, 0, 0, 0, 0, 0, 2 }, r = 0;
    CHECK(feckless_umode_calls_ext(&w, FECKLESS_SYS_EXT_BASE, args, &r) && r == 42 && seen == 1);
    CHECK(feckless_umode_calls_ext(&w, FECKLESS_SYS_EXT_BASE + 1, args, &r) && r == FECKLESS_SYS_ERR_BADNR);  /* data */
    CHECK(feckless_umode_calls_ext(&w, FECKLESS_SYS_EXT_BASE + 2, args, &r) && r == FECKLESS_SYS_ERR_BADNR);  /* past */
    w.ext_arg = NULL;
    CHECK(feckless_umode_calls_ext(&w, FECKLESS_SYS_EXT_BASE, args, &r) && r == FECKLESS_SYS_ERR_BADNR);

    /* the heap calls, by name */
    feckless_umode_calls_t h = { feckless_umode_heap_calls, feckless_umode_heap_calls_n, &box };
    w.ext_arg = &h;
    uint32_t idx[5] = { 0 };
    const char *want[5] = { "malloc", "calloc", "realloc", "free", "heap_caps_malloc" };
    for (int k = 0; k < 5; k++)
        for (uint32_t j = 0; j < h.n; j++)
            if (!strcmp(h.calls[j].name, want[k])) idx[k] = FECKLESS_SYS_EXT_BASE + j;
    uint32_t a[7] = { 24 }, p;
    CHECK(feckless_umode_calls_ext(&w, idx[0], a, &p) && p >= B && p < B + box.heap_end);
    memset((void *)(uintptr_t)p, 0x55, 24);
    a[0] = p; a[1] = 64;
    uint32_t q;
    CHECK(feckless_umode_calls_ext(&w, idx[2], a, &q) && q && ((uint8_t *)(uintptr_t)q)[23] == 0x55);
    a[0] = 4; a[1] = 8;
    uint32_t z;
    CHECK(feckless_umode_calls_ext(&w, idx[1], a, &z) && z);
    for (int k = 0; k < 32; k++) CHECK(((uint8_t *)(uintptr_t)z)[k] == 0);
    a[0] = 0x10000; a[1] = 0x10000;                                     /* product overflows 32 bits */
    CHECK(feckless_umode_calls_ext(&w, idx[1], a, &z) && z == 0);
    a[0] = (uint32_t)(uintptr_t)&box;                                   /* free of host memory: refused, harmless */
    const uint32_t before = box.n;
    CHECK(feckless_umode_calls_ext(&w, idx[3], a, &z) && box.n == before);
    a[0] = q;
    CHECK(feckless_umode_calls_ext(&w, idx[3], a, &z) && box.n == before - 1);
    a[0] = 16; a[1] = 0xffffffff;
    CHECK(feckless_umode_calls_ext(&w, idx[4], a, &p) && p);

    /* u-feckless-0048: the heap's stats, into the box only */
    uint32_t hs = 0;
    for (uint32_t j = 0; j < h.n; j++)
        if (!strcmp(h.calls[j].name, "feckless_box_heap_stats")) hs = FECKLESS_SYS_EXT_BASE + j;
    CHECK(hs != 0);
    w.lo = B;
    w.hi = B + MEM_SIZE;
    const uintptr_t out = feckless_box_alloc(&box, sizeof(feckless_box_stats_t));
    a[0] = (uint32_t)out;
    CHECK(feckless_umode_calls_ext(&w, hs, a, &z) && z == 1);
    const feckless_box_stats_t *st = (const feckless_box_stats_t *)out;
    CHECK(st->total == box.heap_end && st->blocks == box.n && st->free < st->total);
    a[0] = (uint32_t)(uintptr_t)&box;                                   /* the host's memory */
    CHECK(feckless_umode_calls_ext(&w, hs, a, &z) && z == 0);
}

int main(void)
{
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED || (uintptr_t)mem > 0xffffffffu - MEM_SIZE) {
        printf("no memory below 4 GB for the box\n");
        return 1;
    }
    test_init();
    test_alloc_free();
    test_alloc_aligned();
    test_fill_and_table_limit();
    test_bigger_table();
    test_realloc();
    test_random();
    test_stubs();
    test_calls();
    printf(failed ? "%d FAILED\n" : "umode_box: all passed\n", failed);
    return failed != 0;
}
