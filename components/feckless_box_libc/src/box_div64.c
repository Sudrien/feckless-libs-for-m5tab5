/* SPDX-License-Identifier: MIT
 *
 * 64-bit integer division for a boxed app, in the app (u-feckless-0038).
 *
 * RV32 has no instruction that divides a 64-bit integer, so `a / b` and
 * `a % b` on one are calls: __udivdi3, __umoddi3, __divdi3, __moddi3.
 * libgcc's reach __clz_tab absolutely and could not be linked into a boxed
 * app (u-feckless-0005), so the box answered them: a trap into the launcher
 * and back for every division. A program that keeps time in microseconds
 * divides a 64-bit count by 1000 wherever it wants milliseconds.
 *
 * Three ways, by the divisor, which is what decides the work:
 *   - both fit in 32 bits: one hardware divide;
 *   - the divisor is under 2^16 (the / 1000 above): long division in
 *     16-bit digits, four hardware divides at most, each of a number that
 *     fits in 32 bits because the running remainder is under the divisor;
 *   - anything else: shift and subtract, one bit a step, for as many bits
 *     as the dividend is longer than the divisor.
 *
 * Division by zero is undefined in C. Here it gives what the P4's own
 * divide instructions give: a quotient of all ones and the dividend as the
 * remainder. The most negative number divided by -1, also undefined, gives
 * itself and a remainder of 0.
 *
 * NOTHING IN THIS FILE MAY DIVIDE OR TAKE THE REMAINDER OF A 64-BIT VALUE:
 * that is a call to one of these four, and on a host, where the test runs,
 * it would pass. Every / and % below has 32-bit operands. The build's check
 * is that the RISC-V object has no relocation against any of the four
 * names (ARCHITECTURE.md, 0038).
 *
 * test/div64_test.c runs all four against the host's own division.
 */
#include <stdint.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

typedef uint64_t u64;
typedef uint32_t u32;

static inline int clz32(u32 x)                  /* x is not zero */
{
    int n = 0;
    if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xff000000u)) { n += 8;  x <<= 8; }
    if (!(x & 0xf0000000u)) { n += 4;  x <<= 4; }
    if (!(x & 0xc0000000u)) { n += 2;  x <<= 2; }
    if (!(x & 0x80000000u)) { n += 1; }
    return n;
}

static inline int clz64(u64 x)                  /* x is not zero */
{
    const u32 hi = (u32)(x >> 32);
    return hi ? clz32(hi) : 32 + clz32((u32)x);
}

/* n / d, with n % d left in *rem. */
static u64 udivmod(u64 n, u64 d, u64 *rem)
{
    if (!d) {
        *rem = n;
        return ~0ull;
    }
    if (n < d) {
        *rem = n;
        return 0;
    }
    const u32 nh = (u32)(n >> 32), nl = (u32)n;
    const u32 dl = (u32)d;                      /* all of d, in the first two cases */

    if (!nh) {                                  /* d <= n, so d fits as well */
        *rem = nl % dl;
        return nl / dl;
    }
    if (d < 0x10000u) {
        /* The remainder carried into each step is under d, under 2^16, so
         * with the next 16-bit digit below it the number divided fits in 32
         * bits and its quotient in 16. */
        u32 r = (nh >> 16) % dl;
        const u32 q3 = (nh >> 16) / dl;
        u32 t = (r << 16) | (nh & 0xffffu);
        const u32 q2 = t / dl;
        r = t % dl;
        t = (r << 16) | (nl >> 16);
        const u32 q1 = t / dl;
        r = t % dl;
        t = (r << 16) | (nl & 0xffffu);
        const u32 q0 = t / dl;
        *rem = t % dl;
        return ((u64)((q3 << 16) | q2) << 32) | ((q1 << 16) | q0);
    }
    /* Line the divisor's leading bit up under the dividend's, then take it
     * off wherever it fits, moving it down a bit at a time. */
    int shift = clz64(d) - clz64(n);            /* 0 or more: d <= n */
    u64 q = 0;
    d <<= shift;
    for (; shift >= 0; shift--) {
        q <<= 1;
        if (n >= d) {
            n -= d;
            q |= 1;
        }
        d >>= 1;
    }
    *rem = n;
    return q;
}

u64 BOX_NAME(__udivdi3)(u64 n, u64 d)
{
    u64 r;
    return udivmod(n, d, &r);
}

u64 BOX_NAME(__umoddi3)(u64 n, u64 d)
{
    u64 r;
    (void)udivmod(n, d, &r);
    return r;
}

/* Signed: on the magnitudes. The quotient is negative when the signs
 * differ and the remainder has the dividend's sign, which is C's
 * truncation toward zero. */
int64_t BOX_NAME(__divdi3)(int64_t n, int64_t d)
{
    if (!d) return -1;                          /* all ones, as unsigned and as the P4's div */
    const u64 un = n < 0 ? 0ull - (u64)n : (u64)n;
    const u64 ud = d < 0 ? 0ull - (u64)d : (u64)d;
    u64 r;
    const u64 q = udivmod(un, ud, &r);
    return (int64_t)(((n < 0) != (d < 0)) ? 0ull - q : q);
}

int64_t BOX_NAME(__moddi3)(int64_t n, int64_t d)
{
    const u64 un = n < 0 ? 0ull - (u64)n : (u64)n;
    const u64 ud = d < 0 ? 0ull - (u64)d : (u64)d;
    u64 r;
    (void)udivmod(un, ud, &r);
    return (int64_t)(n < 0 ? 0ull - r : r);
}
