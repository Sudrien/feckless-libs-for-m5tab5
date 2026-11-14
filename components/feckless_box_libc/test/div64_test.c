/* SPDX-License-Identifier: MIT
 *
 * src/box_div64.c against the host's own 64-bit division (u-feckless-0038).
 *
 *     make -C components/feckless_box_libc/test div64_test
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

uint64_t box___udivdi3(uint64_t, uint64_t);
uint64_t box___umoddi3(uint64_t, uint64_t);
int64_t box___divdi3(int64_t, int64_t);
int64_t box___moddi3(int64_t, int64_t);

static uint64_t checks, failures;

static void fail(const char *what, uint64_t n, uint64_t d)
{
    if (failures++ < 20) printf("FAIL %s: %016" PRIx64 " by %016" PRIx64 "\n", what, n, d);
}

/* Both the host's answer and the identity n == q * d + r with r < d, so a
 * mistake shared with the host's compiler would still be seen. */
static void pair(uint64_t n, uint64_t d)
{
    if (d) {
        const uint64_t q = box___udivdi3(n, d), r = box___umoddi3(n, d);
        checks += 3;
        if (q != n / d) fail("udivdi3", n, d);
        if (r != n % d) fail("umoddi3", n, d);
        if (r >= d || q * d + r != n) fail("the unsigned identity", n, d);
    }
    const int64_t sn = (int64_t)n, sd = (int64_t)d;
    if (sd && !(sn == INT64_MIN && sd == -1)) {          /* the host would trap */
        const int64_t q = box___divdi3(sn, sd), r = box___moddi3(sn, sd);
        checks += 2;
        if (q != sn / sd) fail("divdi3", n, d);
        if (r != sn % sd) fail("moddi3", n, d);
    }
}

static uint64_t rng = 0x243f6a8885a308d3ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static const uint64_t edge[] = {
    0, 1, 2, 3, 7, 10, 1000, 1000000, 0xffff, 0x10000, 0x10001, 0xfffe, 0x7fff, 0x8000,
    0x7fffffffull, 0x80000000ull, 0xffffffffull, 0x100000000ull, 0x100000001ull, 0xffffffff0000ull,
    0xffff00000000ull, 0x1000000000000ull, 0xffffffffffffull, 0x7fffffffffffffffull,
    0x8000000000000000ull, 0x8000000000000001ull, 0xffffffffffffffffull, 0xfffffffffffffffeull,
    0xffffffff00000000ull, 0x00000000fffffffeull, 0xfffffffffffffc18ull /* -1000 */,
    0xffffffffffff0000ull, 0xffffffffffff0001ull, 0xffffffff80000000ull, 0x0000ffff0000ffffull,
    0x123456789abcdef0ull, 0x0fedcba987654321ull, 3600000000ull, 86400000000ull, 44100, 48000,
};
#define NEDGE (sizeof edge / sizeof edge[0])

int main(void)
{
    for (size_t i = 0; i < NEDGE; i++) {
        for (size_t j = 0; j < NEDGE; j++) pair(edge[i], edge[j]);
    }
    for (int k = 0; k < 6000000; k++) {
        /* every length of dividend against every length of divisor, so all
         * three ways are taken and their boundaries crossed */
        const uint64_t n = rnd() >> (rnd() % 64), d = rnd() >> (rnd() % 64);
        pair(n, d);
        pair(0ull - n, d);
        pair(n, 0ull - d);
        pair(n, edge[rnd() % NEDGE]);
        pair(edge[rnd() % NEDGE], d);
        pair(n, (d & 0xffff) | 1);                  /* the 16-bit way */
        pair(n * (d | 1), d | 1);                   /* exact, when it does not wrap */
        pair(n, n - (rnd() & 3));                   /* a quotient of 0 or 1 */
    }

    /* What C leaves undefined and this file defines. */
    checks += 6;
    if (box___udivdi3(12345, 0) != ~0ull || box___umoddi3(12345, 0) != 12345) fail("unsigned by zero", 12345, 0);
    if (box___udivdi3(0, 0) != ~0ull || box___umoddi3(0, 0) != 0) fail("zero by zero", 0, 0);
    if (box___divdi3(INT64_MIN, -1) != INT64_MIN || box___moddi3(INT64_MIN, -1) != 0) {
        fail("the most negative by -1", (uint64_t)INT64_MIN, ~0ull);
    }
    if (box___divdi3(-7, 0) != -1 || box___moddi3(-7, 0) != -7) fail("signed by zero", (uint64_t)-7, 0);
    if (box___divdi3(7, 0) != -1 || box___moddi3(7, 0) != 7) fail("signed by zero", 7, 0);
    if (box___divdi3(-7, 2) != -3 || box___moddi3(-7, 2) != -1 || box___divdi3(7, -2) != -3 ||
        box___moddi3(7, -2) != 1) fail("toward zero", 7, 2);

    printf(failures ? "div64: %" PRIu64 " checks, %" PRIu64 " FAILED\n"
                    : "div64: %" PRIu64 " checks, all the host's\n", checks, failures);
    return failures != 0;
}
