/* SPDX-License-Identifier: MIT
 *
 * libgcc's float <-> double and float <-> 64-bit conversions, for a boxed app
 * (u-feckless-0027): __extendsfdf2, __truncdfsf2's partner; __floatdisf,
 * __floatundisf, __fixsfdi, __fixunssfdi.
 *
 * libgcc's own cannot be host calls: on ilp32f each takes or returns its
 * float in fa0, and the trap saves only the integer registers (u-feckless-
 * 0006). And they cannot be copied out of libgcc.a as the others are: they
 * are not position-independent. So they are written here, on the bits,
 * with no float arithmetic that the compiler would turn back into a call to
 * one of them, and no __clzdi2 (a host call): the leading zeros are counted
 * by hand. Rounding is to nearest, ties to even, as the FPU's default mode;
 * the fix* truncate toward zero, and saturate as RISC-V's fcvt does (NaN to
 * the largest value).
 */
#include <stdint.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

typedef union { float f; uint32_t u; } f32;
typedef union { double d; uint64_t u; } f64;

static int clz64(uint64_t x)
{
    int n = 0;
    uint32_t hi = (uint32_t)(x >> 32);
    uint32_t w = hi ? hi : (uint32_t)x;
    if (!hi) n = 32;
    while (!(w & 0x80000000u)) { w <<= 1; n++; }
    return n;
}

double BOX_NAME(__extendsfdf2)(float a)
{
    f32 in = { .f = a };
    const uint64_t sign = (uint64_t)(in.u >> 31) << 63;
    uint32_t e = (in.u >> 23) & 0xffu, m = in.u & 0x7fffffu;
    f64 out;
    if (e == 0xffu) {                               /* inf, NaN: the payload kept, quieted */
        out.u = sign | 0x7ff0000000000000ull | ((uint64_t)m << 29) | (m ? 0x0008000000000000ull : 0);
    } else if (e == 0) {
        if (m == 0) {
            out.u = sign;                           /* +-0 */
        } else {                                    /* subnormal: normal in double */
            int s = 0;
            while (!(m & 0x800000u)) { m <<= 1; s++; }
            out.u = sign | ((uint64_t)(1023 - 126 - s) << 52) | ((uint64_t)(m & 0x7fffffu) << 29);
        }
    } else {
        out.u = sign | ((uint64_t)(e - 127 + 1023) << 52) | ((uint64_t)m << 29);
    }
    return out.d;
}

/* A float from a 64-bit magnitude, rounded to nearest even. */
static float u64_to_f(uint64_t x, uint32_t sign)
{
    f32 out;
    if (x == 0) {
        out.u = sign;
        return out.f;
    }
    const int lz = clz64(x);
    const int top = 63 - lz;                        /* the leading bit's place */
    uint32_t m;
    int e = top;
    if (top <= 23) {
        m = (uint32_t)(x << (23 - top));            /* exact */
    } else {
        const int sh = top - 23;
        m = (uint32_t)(x >> sh);
        const uint64_t rest = x & ((1ull << sh) - 1);
        const uint64_t half = 1ull << (sh - 1);
        if (rest > half || (rest == half && (m & 1u))) {
            m++;
            if (m == 0x1000000u) { m >>= 1; e++; }  /* carried into a new bit */
        }
    }
    out.u = sign | ((uint32_t)(e + 127) << 23) | (m & 0x7fffffu);
    return out.f;
}

float BOX_NAME(__floatundisf)(uint64_t x)
{
    return u64_to_f(x, 0);
}

float BOX_NAME(__floatdisf)(int64_t x)
{
    return x < 0 ? u64_to_f(0 - (uint64_t)x, 0x80000000u) : u64_to_f((uint64_t)x, 0);
}

/* |a| truncated, as a 64-bit magnitude; *big if it is 2^64 or more. */
static uint64_t f_to_u64(uint32_t u, int *big)
{
    const int e = (int)((u >> 23) & 0xffu) - 127;
    const uint64_t m = (uint64_t)((u & 0x7fffffu) | 0x800000u);
    *big = 0;
    if (e < 0) return 0;
    if (e >= 64) { *big = 1; return 0; }
    return e >= 23 ? m << (e - 23) : m >> (23 - e);
}

int64_t BOX_NAME(__fixsfdi)(float a)
{
    f32 in = { .f = a };
    const int nan = (in.u & 0x7fffffffu) > 0x7f800000u;
    const int neg = !nan && (in.u >> 31);
    int big;
    const uint64_t v = f_to_u64(in.u & 0x7fffffffu, &big);
    if (neg) {
        if (big || v > 0x8000000000000000ull) return INT64_MIN;
        return (int64_t)(0 - v);
    }
    if (nan || big || v > (uint64_t)INT64_MAX) return INT64_MAX;
    return (int64_t)v;
}

uint64_t BOX_NAME(__fixunssfdi)(float a)
{
    f32 in = { .f = a };
    if ((in.u & 0x7fffffffu) > 0x7f800000u) return UINT64_MAX;   /* NaN */
    if (in.u >> 31) return 0;                                     /* negative, -0 */
    int big;
    const uint64_t v = f_to_u64(in.u, &big);
    return big ? UINT64_MAX : v;
}

/* newlib's float classification helper, which picolibc's headers can also
 * reach: a NaN with its quiet bit clear. */
int BOX_NAME(__issignalingf)(float a)
{
    f32 in = { .f = a };
    return (in.u & 0x7fffffffu) > 0x7f800000u && !(in.u & 0x00400000u);
}
