/* SPDX-License-Identifier: MIT
 *
 * Host test for umode_plan.c as it is: the PMP plan, the context helper and
 * (by compiling) the context layout umode.S depends on.
 *
 *   make -C components/feckless_umode/test
 */
#include <stdio.h>
#include <string.h>

#include "feckless_umode.h"

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define R FECKLESS_UMODE_R
#define W FECKLESS_UMODE_W
#define X FECKLESS_UMODE_X
#define TOR 0x08u

static void test_empty(void)
{
    uint32_t a[16], c[4];
    memset(a, 0xaa, sizeof a);
    memset(c, 0xaa, sizeof c);
    CHECK(feckless_umode_pmp_plan(NULL, 0, a, c) == FECKLESS_UMODE_OK);
    for (int i = 0; i < 16; i++) CHECK(a[i] == 0);
    for (int i = 0; i < 4; i++) CHECK(c[i] == 0);        /* all OFF: U-mode gets nothing */
}

static void test_one_and_two(void)
{
    uint32_t a[16], c[4];
    const feckless_umode_grant_t one[] = { { 0x48100000, 0x48140000, R | W } };
    CHECK(feckless_umode_pmp_plan(one, 1, a, c) == FECKLESS_UMODE_OK);
    CHECK(a[0] == 0x48100000u >> 2 && a[1] == 0x48140000u >> 2);
    CHECK(c[0] == (((TOR | R | W) << 8)));               /* entry 1 = byte 1 of pmpcfg0 */
    CHECK(c[1] == 0 && c[2] == 0 && c[3] == 0);
    for (int i = 2; i < 16; i++) CHECK(a[i] == 0);

    const feckless_umode_grant_t two[] = {
        { 0x4ff00000, 0x4ff11d00, R | X },
        { 0x4ff40000, 0x4ff70000, R | W },
    };
    CHECK(feckless_umode_pmp_plan(two, 2, a, c) == FECKLESS_UMODE_OK);
    CHECK(a[0] == 0x4ff00000u >> 2 && a[1] == 0x4ff11d00u >> 2);
    CHECK(a[2] == 0x4ff40000u >> 2 && a[3] == 0x4ff70000u >> 2);
    /* entries 1 and 3 are bytes 1 and 3 of pmpcfg0 */
    CHECK(c[0] == (((TOR | R | X) << 8) | ((TOR | R | W) << 24)));
}

static void test_max(void)
{
    feckless_umode_grant_t g[FECKLESS_UMODE_MAX_GRANTS + 1];
    uint32_t a[16], c[4];
    for (int i = 0; i < FECKLESS_UMODE_MAX_GRANTS + 1; i++) {
        g[i].lo = 0x1000u * (uint32_t)(i + 1);
        g[i].hi = g[i].lo + 0x100;
        g[i].perm = R;
    }
    CHECK(feckless_umode_pmp_plan(g, FECKLESS_UMODE_MAX_GRANTS, a, c) == FECKLESS_UMODE_OK);
    for (int w = 0; w < 4; w++) CHECK(c[w] == ((TOR | R) << 8 | (TOR | R) << 24));
    CHECK(a[15] == g[7].hi >> 2);
    CHECK(feckless_umode_pmp_plan(g, FECKLESS_UMODE_MAX_GRANTS + 1, a, c) == FECKLESS_UMODE_ERR_COUNT);
    CHECK(feckless_umode_pmp_plan(g, -1, a, c) == FECKLESS_UMODE_ERR_COUNT);
    CHECK(feckless_umode_pmp_plan(NULL, 1, a, c) == FECKLESS_UMODE_ERR_COUNT);
}

static void test_rejects_and_leaves_outputs(void)
{
    uint32_t a[16], c[4];
    memset(a, 0x5a, sizeof a);
    memset(c, 0x5a, sizeof c);
    const feckless_umode_grant_t good = { 0x1000, 0x2000, R };
    const feckless_umode_grant_t empty = { 0x2000, 0x2000, R };
    const feckless_umode_grant_t back = { 0x3000, 0x2000, R };
    const feckless_umode_grant_t odd_lo = { 0x1001, 0x2000, R };
    const feckless_umode_grant_t odd_hi = { 0x1000, 0x2002, R };
    const feckless_umode_grant_t none = { 0x1000, 0x2000, 0 };
    const feckless_umode_grant_t w_only = { 0x1000, 0x2000, W };
    const feckless_umode_grant_t junk = { 0x1000, 0x2000, R | 8 };
    feckless_umode_grant_t mix[2];

    CHECK(feckless_umode_pmp_plan(&empty, 1, a, c) == FECKLESS_UMODE_ERR_RANGE);
    CHECK(feckless_umode_pmp_plan(&back, 1, a, c) == FECKLESS_UMODE_ERR_RANGE);
    CHECK(feckless_umode_pmp_plan(&odd_lo, 1, a, c) == FECKLESS_UMODE_ERR_ALIGN);
    CHECK(feckless_umode_pmp_plan(&odd_hi, 1, a, c) == FECKLESS_UMODE_ERR_ALIGN);
    CHECK(feckless_umode_pmp_plan(&none, 1, a, c) == FECKLESS_UMODE_ERR_PERM);
    CHECK(feckless_umode_pmp_plan(&w_only, 1, a, c) == FECKLESS_UMODE_ERR_PERM);
    CHECK(feckless_umode_pmp_plan(&junk, 1, a, c) == FECKLESS_UMODE_ERR_PERM);
    mix[0] = good;                                        /* a bad second grant ... */
    mix[1] = back;
    CHECK(feckless_umode_pmp_plan(mix, 2, a, c) == FECKLESS_UMODE_ERR_RANGE);
    for (int i = 0; i < 16; i++) CHECK(a[i] == 0x5a5a5a5au);   /* ... writes nothing */
    for (int i = 0; i < 4; i++) CHECK(c[i] == 0x5a5a5a5au);
}

static void test_ctx_init(void)
{
    feckless_umode_ctx_t ctx;
    memset(&ctx, 0xff, sizeof ctx);
    feckless_umode_ctx_init(&ctx, 0x48000100, 0x48200000, 0x48210000, 7);
    CHECK(ctx.pc == 0x48000100);
    CHECK(ctx.x[2] == 0x48210000);                        /* sp = the inclusive top */
    CHECK(ctx.x[10] == 7);
    CHECK(ctx.u_sp_min == 0x48200000 && ctx.u_sp_max == 0x48210000);
    for (int i = 0; i < 32; i++) {
        if (i != 2 && i != 10) CHECK(ctx.x[i] == 0);
    }
    CHECK(ctx.mcause == 0 && ctx.sp_fired == 0 && ctx.k_sp == 0);
}

int main(void)
{
    test_empty();
    test_one_and_two();
    test_max();
    test_rejects_and_leaves_outputs();
    test_ctx_init();
    printf(failed ? "%d FAILED\n" : "umode_plan: all passed\n", failed);
    return failed != 0;
}
