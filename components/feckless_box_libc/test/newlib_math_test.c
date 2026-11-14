/* SPDX-License-Identifier: MIT
 *
 * newlib_math/ against the toolchain's own libm.a (u-feckless-0041).
 *
 * The 21 double functions a boxed app gets, 200,000 inputs each (any bit
 * patterns, the middle of the range, near zero, the small integers, large,
 * and the edges), every result into a hash by its bits, and the hash held
 * to the constant below. Each constant is what the ESP-IDF toolchain's
 * libm.a (riscv32-esp-elf esp-15.2.0_20251204, rv32imafc/ilp32f) gives for
 * the same inputs, run under qemu-riscv32: the functions the box answered
 * as host calls until now. The last checks errno across log, pow, sqrt and
 * exp.
 *
 * It runs on a host because fdlibm is double arithmetic and integer bit
 * operations only: compiled with every operation rounded once
 * (-ffp-contract=off) and no builtins, x86-64 and AArch64 give the bits
 * RV32's soft double does. A NaN is hashed as one pattern, since which NaN
 * comes out is the machine's to choose. newlib_shim/ stands in for
 * newlib's <machine/ieeefp.h> as it reads for the P4.
 *
 *     make -C components/feckless_box_libc/test newlib_math_test
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define err errno

typedef union { double d; uint64_t u; } dbits;
static uint64_t B(double x) { dbits b = { .d = x }; return b.u; }
static double D(uint64_t u) { dbits b = { .u = u }; return b.d; }
static uint64_t rng;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static uint32_t h;
static void mix(uint64_t v) { if ((v & 0x7fffffffffffffffull) > 0x7ff0000000000000ull) v = 0x7ff8000000000000ull; for (int i = 0; i < 8; i++) { h = (h ^ (uint32_t)(v & 0xff)) * 16777619u; v >>= 8; } }
/* operands: any bits, the middle of the range, near zero, near the small integers, large */
static double arg(void)
{
    uint64_t a = rnd();
    switch (rnd() % 6) {
    case 0: return D(a);
    case 1: return D((a & 0x800fffffffffffffull) | ((uint64_t)(0x3ff - 8 + rnd() % 16) << 52));
    case 2: return D((a & 0x800fffffffffffffull) | ((uint64_t)(0x3ff - 60 + rnd() % 60) << 52));
    case 3: return D((a & 0x800fffffffffffffull) | ((uint64_t)(0x3ff + rnd() % 12) << 52));
    case 4: return D((a & 0x800fffffffffffffull) | ((uint64_t)(0x3ff + 20 + rnd() % 900) << 52));
    default: { static const uint64_t e[] = { 0, 0x8000000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
        0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull, 1, 0x7fefffffffffffffull, 0x400921fb54442d18ull,
        0x3ff921fb54442d18ull, 0x4086232bdd7abcd2ull, 0xc0874910d52d3051ull, 0x3fe0000000000000ull, 0x4000000000000000ull };
        return D(e[rnd() % 15]); }
    }
}
static unsigned checks, failures;
static void got(const char *name, uint32_t want)
{
    checks++;
    if (h == want) return;
    failures++;
    printf("FAIL %s: hash %08x, the toolchain's libm.a gives %08x\n", name, (unsigned)h, (unsigned)want);
}
#define ONE(name, want, expr) do { h = 2166136261u; rng = seed; \
    for (int i = 0; i < K; i++) { double x = arg(); double y = arg(); (void)y; mix(B(expr)); } \
    got(name, want); } while (0)
int main(void)
{
    const uint64_t seed = 0x6a09e667f3bcc908ull;
    enum { K = 200000 };
    ONE("sqrt", 0x64e1b139u, sqrt(x)); ONE("sin", 0x5b900faeu, sin(x)); ONE("cos", 0xa3350032u, cos(x)); ONE("tan", 0xde207ed2u, tan(x));
    ONE("asin", 0x4ebc775au, asin(x)); ONE("acos", 0x185b7d11u, acos(x)); ONE("atan", 0x1ab1756fu, atan(x)); ONE("sinh", 0x87c5e754u, sinh(x)); ONE("asinh", 0x41c60de6u, asinh(x));
    ONE("exp", 0xd101f44du, exp(x)); ONE("log", 0x32a8b88bu, log(x)); ONE("log10", 0x868b6877u, log10(x)); ONE("floor", 0x8252a628u, floor(x)); ONE("ceil", 0x5464110fu, ceil(x));
    ONE("round", 0xf375d771u, round(x)); ONE("fabs", 0x0084e042u, fabs(x)); ONE("atan2", 0x416486e7u, atan2(x, y)); ONE("pow", 0xb6486a74u, pow(x, y));
    ONE("fmod", 0xa9b95786u, fmod(x, y)); ONE("ldexp", 0x0379c559u, ldexp(x, (int)(rnd() % 2200) - 1100));
    ONE("lround", 0x8efb6260u, (fabs(x) < 2147483647.0 ? (double)lround(x) : 0.0));
    ONE("errno", 0xc4ab87e5u, (err = 0, (void)log(x), (void)pow(x, y), (void)sqrt(x), (void)exp(x), (double)err));
    printf(failures ? "newlib_math: %u functions, %u FAILED\n"
                    : "newlib_math: %u functions, each the toolchain's libm.a to the bit\n", checks, failures);
    return failures != 0;
}
