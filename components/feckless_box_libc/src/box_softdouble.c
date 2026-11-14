/* SPDX-License-Identifier: MIT
 *
 * Double arithmetic for a boxed app, in the app (u-feckless-0036).
 *
 * The P4's FPU is single precision, so every double operation is a call:
 * a + b is __adddf3(a, b). libgcc's versions are not position-independent
 * (__divdf3 has an R_RISCV_HI20 against a table of its own, and the rest
 * reach __clz_tab through __clzsi2), so they could not be linked into a
 * boxed app and were host calls instead (u-feckless-0005, 0025): a trap
 * into the launcher and back for each operation. Lothesome's level meter
 * lost a third of its frame rate to that (0029), and Defeatist's DSD filter
 * design, a few tens of thousands of operations, took 0.2 to 0.6 s
 * (u-defeatist-0007).
 *
 * These are this component's own, compiled with the app: add, subtract,
 * multiply, divide, the four conversions from an integer, and the three
 * bit counts libgcc's remaining in-app members call. IEEE 754 binary64,
 * round to nearest, ties to even; no exception flags and no other rounding
 * mode, as libgcc's have none on this target. A NaN result is the
 * canonical quiet NaN, 0x7ff8000000000000, whatever the operands' payloads,
 * which is what RISC-V specifies and libgcc returns there.
 *
 * NOTHING IN THIS FILE MAY USE A DOUBLE OPERATOR, divide a 64-bit integer,
 * or count bits with a builtin: each of those is a call to a function this
 * file is there to provide, or to one that is still a host call. The
 * doubles are handled as their bits. The build's check is
 * that the object has no undefined symbol (ARCHITECTURE.md, 0036).
 *
 * test/softdouble_test.c runs every function against the host's own double
 * arithmetic: all pairs of some ninety edge values and tens of millions of
 * random ones, bit for bit.
 */
#include <stdint.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

typedef uint64_t u64;
typedef uint32_t u32;

typedef union { double d; u64 u; } dbits;
static inline u64 bits(double x) { dbits b = { .d = x }; return b.u; }
static inline double from(u64 u) { dbits b = { .u = u }; return b.d; }

#define SIGN_BIT  0x8000000000000000ull
#define FRAC_MASK 0x000fffffffffffffull
#define HIDDEN    0x0010000000000000ull
#define EXP_MAX   0x7ff
#define INF_BITS  0x7ff0000000000000ull
#define QNAN_BITS 0x7ff8000000000000ull

/* ---- bit counts ----------------------------------------------------------
 * Zero has no leading or trailing bit; libgcc leaves that undefined, and
 * these say the width. */

static inline int clz32(u32 x)
{
    int n = 0;
    if (!x) return 32;
    if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xff000000u)) { n += 8;  x <<= 8; }
    if (!(x & 0xf0000000u)) { n += 4;  x <<= 4; }
    if (!(x & 0xc0000000u)) { n += 2;  x <<= 2; }
    if (!(x & 0x80000000u)) { n += 1; }
    return n;
}

static inline int clz64(u64 x)
{
    const u32 hi = (u32)(x >> 32);
    return hi ? clz32(hi) : 32 + clz32((u32)x);
}

int BOX_NAME(__clzsi2)(u32 x) { return clz32(x); }
int BOX_NAME(__clzdi2)(u64 x) { return clz64(x); }

int BOX_NAME(__ctzsi2)(u32 x)
{
    if (!x) return 32;
    return 31 - clz32(x & (0u - x));        /* the lowest set bit alone */
}

/* ---- rounding and packing ------------------------------------------------ */

/* x >> n, with every bit shifted out ORed into bit 0 ("jamming"): the low
 * bit then says "and something below", which is all rounding needs. */
static inline u64 shr_jam(u64 x, int n)
{
    if (n <= 0) return x;
    if (n >= 64) return x != 0;
    return (x >> n) | ((x << (64 - n)) != 0);
}

/*
 * sig has its leading 1 at bit 62 (or is smaller, for a result below the
 * normal range) and ten bits below the 53 that are kept: bits 9..0 are the
 * rounding bits, bit 0 jammed. exp is the biased exponent LESS ONE: the
 * leading 1 lands on bit 52 of the result, where it adds the one back to
 * the exponent field. That is also what makes a carry out of the rounding
 * right without a test: 0x1f...f rounds up into the next exponent.
 */
static u64 round_pack(u32 sign, int exp, u64 sig)
{
    if (exp < 0) {                                  /* subnormal, or to zero */
        sig = shr_jam(sig, -exp);
        exp = 0;
    } else if (exp > 0x7fd || (exp == 0x7fd && sig + 0x200 >= SIGN_BIT)) {
        return ((u64)sign << 63) | INF_BITS;        /* overflow */
    }
    const u32 round = (u32)sig & 0x3ff;
    sig = (sig + 0x200) >> 10;
    if (round == 0x200) sig &= ~1ull;               /* a tie: to even */
    if (!sig) exp = 0;
    return ((u64)sign << 63) + ((u64)(u32)exp << 52) + sig;
}

/* sig, non-zero, is worth sig * 2^(exp - 1023 - 62): the biased exponent it
 * would have with its leading 1 at bit 62. Put it there, then round. */
static u64 norm_round_pack(u32 sign, int exp, u64 sig)
{
    const int shift = clz64(sig) - 1;
    if (shift < 0) {                                /* at bit 63: down one, jammed */
        sig = (sig >> 1) | (sig & 1);
        exp += 1;
    } else {
        sig <<= shift;
        exp -= shift;
    }
    return round_pack(sign, exp - 1, sig);
}

/* A subnormal's fraction, non-zero: shift its leading 1 to bit 52 and give
 * the exponent that goes with it. */
static inline int norm_subnormal(u64 *frac)
{
    const int shift = clz64(*frac) - 11;
    *frac <<= shift;
    return 1 - shift;
}

/* ---- add and subtract ---------------------------------------------------- */

static u64 add_bits(u64 a, u64 b)
{
    const u32 sa = (u32)(a >> 63), sb = (u32)(b >> 63);
    int ea = (int)((a >> 52) & EXP_MAX), eb = (int)((b >> 52) & EXP_MAX);
    u64 ma = a & FRAC_MASK, mb = b & FRAC_MASK;

    if (ea == EXP_MAX) {
        if (ma) return QNAN_BITS;
        if (eb == EXP_MAX) return (mb || sa != sb) ? QNAN_BITS : a;     /* inf - inf */
        return a;
    }
    if (eb == EXP_MAX) return mb ? QNAN_BITS : b;

    /* The hidden bit in, or a subnormal read at the smallest exponent; then
     * ten guard bits below, the leading 1 at bit 62 at most. */
    if (ea) ma |= HIDDEN; else ea = 1;
    if (eb) mb |= HIDDEN; else eb = 1;
    ma <<= 10;
    mb <<= 10;

    int e = ea;
    if (ea > eb) {
        mb = shr_jam(mb, ea - eb);
    } else if (eb > ea) {
        ma = shr_jam(ma, eb - ea);
        e = eb;
    }
    if (sa == sb) {
        const u64 m = ma + mb;                      /* each under 2^63 */
        if (!m) return (u64)sa << 63;               /* 0 + 0, -0 + -0 */
        return norm_round_pack(sa, e, m);
    }
    if (ma == mb) return 0;                         /* exactly nothing: +0 */
    return ma > mb ? norm_round_pack(sa, e, ma - mb) : norm_round_pack(sb, e, mb - ma);
}

double BOX_NAME(__adddf3)(double a, double b) { return from(add_bits(bits(a), bits(b))); }
double BOX_NAME(__subdf3)(double a, double b) { return from(add_bits(bits(a), bits(b) ^ SIGN_BIT)); }

/* ---- multiply ------------------------------------------------------------ */

/* The high 64 bits of a * b, with the low 64 jammed into bit 0. From 32-bit
 * halves: a 64 x 64 multiply to 128 bits would be a call of its own. */
static inline u64 mul_hi_jam(u64 a, u64 b)
{
    const u32 a0 = (u32)a, a1 = (u32)(a >> 32), b0 = (u32)b, b1 = (u32)(b >> 32);
    const u64 p00 = (u64)a0 * b0, p01 = (u64)a0 * b1, p10 = (u64)a1 * b0, p11 = (u64)a1 * b1;
    const u64 mid = p01 + p10;
    const u64 mid_carry = mid < p01;
    const u64 lo = p00 + (mid << 32);
    const u64 hi = p11 + (mid >> 32) + (mid_carry << 32) + (lo < p00);
    return hi | (lo != 0);
}

double BOX_NAME(__muldf3)(double x, double y)
{
    const u64 a = bits(x), b = bits(y);
    const u32 sign = (u32)((a ^ b) >> 63);
    int ea = (int)((a >> 52) & EXP_MAX), eb = (int)((b >> 52) & EXP_MAX);
    u64 ma = a & FRAC_MASK, mb = b & FRAC_MASK;

    if (ea == EXP_MAX || eb == EXP_MAX) {
        if ((ea == EXP_MAX && ma) || (eb == EXP_MAX && mb)) return from(QNAN_BITS);
        if (!(ea | ma) || !(eb | mb)) return from(QNAN_BITS);          /* inf * 0 */
        return from(((u64)sign << 63) | INF_BITS);
    }
    if (!ea) {
        if (!ma) return from((u64)sign << 63);
        ea = norm_subnormal(&ma);
    }
    if (!eb) {
        if (!mb) return from((u64)sign << 63);
        eb = norm_subnormal(&mb);
    }
    /* One at bit 62 and one at bit 63: the product's top half has its
     * leading 1 at bit 62 or 61. */
    int e = ea + eb - 0x3ff;
    u64 m = mul_hi_jam((ma | HIDDEN) << 10, (mb | HIDDEN) << 11);
    if (m < 0x4000000000000000ull) {
        m <<= 1;
        e--;
    }
    return from(round_pack(sign, e, m));
}

/* ---- divide -------------------------------------------------------------- */

/* A hook for test/softdouble_test.c, which counts the corrections below to
 * hold the reciprocal to its bound; nothing in an app. */
#ifdef BOX_DIV_FIX
void box_div_fix(void);
#else
#define BOX_DIV_FIX() ((void)0)
#endif

/* a * b, all 128 bits, from 32-bit halves: a 64 x 64 multiply would be a
 * call of its own on RV32. */
static inline void mul_128(u64 a, u64 b, u64 *hi, u64 *lo)
{
    const u32 a0 = (u32)a, a1 = (u32)(a >> 32), b0 = (u32)b, b1 = (u32)(b >> 32);
    const u64 p00 = (u64)a0 * b0, p01 = (u64)a0 * b1, p10 = (u64)a1 * b0, p11 = (u64)a1 * b1;
    const u64 mid = p01 + p10;
    const u64 mid_carry = mid < p01;
    const u64 l = p00 + (mid << 32);
    *lo = l;
    *hi = p11 + (mid >> 32) + (mid_carry << 32) + (l < p00);
}

static inline u64 mul_hi(u64 a, u64 b)
{
    u64 hi, lo;
    mul_128(a, b, &hi, &lo);
    return hi;
}

/* The low 64 bits of a * b. */
static inline u64 mul_lo(u64 a, u64 b)
{
    const u32 a0 = (u32)a, a1 = (u32)(a >> 32), b0 = (u32)b, b1 = (u32)(b >> 32);
    return (u64)a0 * b0 + ((u64)(a0 * b1 + a1 * b0) << 32);    /* the cross terms wrap: wanted */
}

/*
 * floor(ma * 2^54 / mb) give or take two, for 53-bit mb with its leading 1
 * at bit 52 and not a power of two, and mb <= ma < 2 mb.
 *
 * d = mb << 11 is in (2^63, 2^64), and y approximates 2^127 / d, which is
 * then under 2^64. Every y here is below it: the seed is taken from d's top
 * 16 bits plus one, a Newton step from below stays below (y (2 - d y) is at
 * most 1/d), and each truncation only lowers it. So d * y < 2^127 always and
 * the error term below is never negative.
 *
 * The seed is good to about 2^-15, one step to 2^-30, two to 2^-60 and a
 * few units of truncation. The quotient ma * 2^65 / d = ma * y / 2^62 then
 * comes out low by under two; the remainder in __divdf3 fixes that.
 */
static inline u64 div_q55(u64 ma, u64 mb)
{
    const u64 d = mb << 11;
    const u32 dh = (u32)(d >> 48);                           /* [2^15, 2^16) */
    u64 y = (u64)(0xffffffffu / (dh + 1)) << 47;            /* a 32-bit divide: an instruction */
    for (int k = 0; k < 2; k++) {
        u64 ph, pl;
        mul_128(d, y, &ph, &pl);
        /* e = 2^127 - d y, which is under 2^112 after the seed; its top
         * bits, e >> 63, under 2^49. */
        const u64 el = 0ull - pl;
        const u64 eh = 0x8000000000000000ull - ph - (pl != 0);
        const u64 e63 = (eh << 1) | (el >> 63);
        y += mul_hi(y, e63);                                /* y e / 2^127 */
    }
    return mul_hi(ma << 2, y);                               /* ma y / 2^62 */
}

double BOX_NAME(__divdf3)(double x, double y)
{
    const u64 a = bits(x), b = bits(y);
    const u32 sign = (u32)((a ^ b) >> 63);
    int ea = (int)((a >> 52) & EXP_MAX), eb = (int)((b >> 52) & EXP_MAX);
    u64 ma = a & FRAC_MASK, mb = b & FRAC_MASK;

    if (ea == EXP_MAX) {
        if (ma || eb == EXP_MAX) return from(QNAN_BITS);               /* NaN, or inf / inf, or / NaN */
        return from(((u64)sign << 63) | INF_BITS);
    }
    if (eb == EXP_MAX) return from(mb ? QNAN_BITS : (u64)sign << 63);  /* x / inf */
    if (!eb) {
        if (!mb) {
            if (!(ea | ma)) return from(QNAN_BITS);                    /* 0 / 0 */
            return from(((u64)sign << 63) | INF_BITS);                 /* x / 0 */
        }
        eb = norm_subnormal(&mb);
    }
    if (!ea) {
        if (!ma) return from((u64)sign << 63);
        ea = norm_subnormal(&ma);
    }
    ma |= HIDDEN;
    mb |= HIDDEN;

    /* Make the dividend at least the divisor, so the quotient is in [1, 2)
     * and its leading bit is known. 55 bits of quotient are wanted -- the 53
     * kept, one to round on and one more so that the rest can be jammed --
     * q = floor(ma * 2^54 / mb), with its leading 1 at bit 54.
     *
     * u-feckless-0039: by a reciprocal, not a bit a step. 0036 took 54
     * steps of shift, compare and subtract, 3.2 us on the P4 against 0.3 us
     * for a multiply (u-defeatist-0011). Now: the divisor's reciprocal to
     * about 60 bits from one 32-bit hardware divide and two Newton steps,
     * the quotient from one multiply by it, and then the exact remainder,
     * which puts right the last bit or two whatever the reciprocal's error
     * -- so the result is correct by the remainder, not by the bound. */
    int e = ea - eb + 0x3fe;
    if (ma < mb) {
        ma <<= 1;
        e--;
    }
    u64 q, rem;
    if (mb == HIDDEN) {
        q = ma << 2;                            /* a power of two: exact */
        rem = 0;
    } else {
        q = div_q55(ma, mb);
        /* ma * 2^54 - q * mb, modulo 2^64: the true value is within a few
         * divisors of zero, under 2^56, so modulo 2^64 is enough and the
         * sign is the top bit. */
        rem = (ma << 54) - mul_lo(q, mb);
        while ((int64_t)rem < 0) {
            q--;
            rem += mb;
            BOX_DIV_FIX();
        }
        while (rem >= mb) {
            q++;
            rem -= mb;
            BOX_DIV_FIX();
        }
    }
    /* Up to bit 62, and what is left of the dividend as the jam bit. */
    return from(round_pack(sign, e, (q << 8) | (rem != 0)));
}

/* ---- from integers ------------------------------------------------------- */

/* 32 bits fit in 53: exact, nothing to round. */
static inline u64 from_u32(u32 sign, u32 m)
{
    if (!m) return 0;
    const int lz = clz32(m);
    return ((u64)sign << 63) | ((u64)(u32)(0x3ff + 31 - lz) << 52) |
           (((u64)m << (lz + 21)) & FRAC_MASK);
}

double BOX_NAME(__floatunsidf)(u32 i) { return from(from_u32(0, i)); }

double BOX_NAME(__floatsidf)(int32_t i)
{
    const u32 sign = i < 0;
    return from(from_u32(sign, sign ? 0u - (u32)i : (u32)i));
}

double BOX_NAME(__floatundidf)(u64 i)
{
    return from(i ? norm_round_pack(0, 0x3ff + 62, i) : 0);
}

double BOX_NAME(__floatdidf)(int64_t i)
{
    const u32 sign = i < 0;
    const u64 m = sign ? 0ull - (u64)i : (u64)i;
    return from(m ? norm_round_pack(sign, 0x3ff + 62, m) : 0);
}
