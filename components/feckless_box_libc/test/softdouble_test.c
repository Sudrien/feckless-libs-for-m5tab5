/* SPDX-License-Identifier: MIT
 *
 * src/box_softdouble.c against the host's own double arithmetic, bit for
 * bit (u-feckless-0036).
 *
 * The host's is IEEE 754 binary64, round to nearest even, which is what the
 * file implements; so every result must be the same 64 bits, except that a
 * NaN may differ in sign and payload (the file always returns RISC-V's
 * canonical one; an x86 host returns others). Built with -ffp-contract=off
 * and on SSE2 or AArch64, where a double operation is one rounded operation.
 *
 *     make -C components/feckless_box_libc/test softdouble_test
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

double box___adddf3(double, double);
double box___subdf3(double, double);
double box___muldf3(double, double);
double box___divdf3(double, double);
double box___floatsidf(int32_t);
double box___floatunsidf(uint32_t);
double box___floatdidf(int64_t);
double box___floatundidf(uint64_t);
int box___clzsi2(uint32_t);
int box___ctzsi2(uint32_t);
int box___clzdi2(uint64_t);

static uint64_t checks, failures;

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double from(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static int is_nan(uint64_t u) { return (u & 0x7fffffffffffffffull) > 0x7ff0000000000000ull; }

static void same(const char *op, uint64_t a, uint64_t b, double got, volatile double want)
{
    checks++;
    const uint64_t g = bits(got), w = bits(want);
    if (is_nan(w) ? g == 0x7ff8000000000000ull : g == w) return;
    if (failures++ < 20) {
        printf("FAIL %s: %016" PRIx64 " , %016" PRIx64 " -> %016" PRIx64 ", the host has %016" PRIx64 "\n",
               op, a, b, g, w);
    }
}

static void pair(uint64_t a, uint64_t b)
{
    /* volatile: the host must do these at run time, in its FPU, and not
     * fold them in the compiler. */
    volatile double x = from(a), y = from(b);
    same("add", a, b, box___adddf3(x, y), x + y);
    same("sub", a, b, box___subdf3(x, y), x - y);
    same("mul", a, b, box___muldf3(x, y), x * y);
    same("div", a, b, box___divdf3(x, y), x / y);
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* The values where the cases change: zeros, the subnormal range's ends,
 * the normal range's ends, one and its neighbours, ties, the infinities
 * and NaNs of both kinds. */
static const uint64_t edge[] = {
    0x0000000000000000ull, 0x8000000000000000ull,
    0x0000000000000001ull, 0x8000000000000001ull, 0x0000000000000002ull, 0x0000000000000003ull,
    0x0008000000000000ull, 0x000fffffffffffffull, 0x800fffffffffffffull, 0x000ffffffffffffeull,
    0x0010000000000000ull, 0x8010000000000000ull, 0x0010000000000001ull, 0x001fffffffffffffull,
    0x0020000000000000ull, 0x0350000000000000ull, 0x0360000000000000ull, 0x0370000000000000ull,
    0x3fe0000000000000ull, 0x3fefffffffffffffull, 0xbfefffffffffffffull,
    0x3ff0000000000000ull, 0xbff0000000000000ull, 0x3ff0000000000001ull, 0x3ff0000000000002ull,
    0x3ff8000000000000ull, 0x3fffffffffffffffull, 0x4000000000000000ull, 0xc000000000000000ull,
    0x4008000000000000ull, 0x4014000000000000ull, 0x401c000000000000ull, 0x4024000000000000ull,
    0x3fd5555555555555ull, 0x3fb999999999999aull, 0x400921fb54442d18ull, 0x4005bf0a8b145769ull,
    0x4330000000000000ull, 0x4330000000000001ull, 0x433fffffffffffffull, 0x4340000000000000ull,
    0x4340000000000001ull, 0x4350000000000000ull, 0x43e0000000000000ull, 0xc3e0000000000000ull,
    0x43f0000000000000ull, 0x3ca0000000000000ull, 0x3cb0000000000000ull, 0x3c90000000000000ull,
    0x3ca0000000000001ull, 0x3c9fffffffffffffull, 0x7fd0000000000000ull, 0x7fdfffffffffffffull,
    0x7fe0000000000000ull, 0x7fe0000000000001ull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
    0x7feffffffffffffeull, 0x7ca0000000000000ull, 0x7c90000000000000ull,
    0x7ff0000000000000ull, 0xfff0000000000000ull,
    0x7ff8000000000000ull, 0xfff8000000000000ull, 0x7ff0000000000001ull, 0x7ff4000000000000ull,
    0xfff0000000000001ull, 0x7fffffffffffffffull,
    0x1ff0000000000000ull, 0x2000000000000000ull, 0x5fe0000000000000ull, 0x5ff0000000000000ull,
    0x1ff0000000000001ull, 0x5fefffffffffffffull, 0x0000000100000000ull, 0x00000000ffffffffull,
    0x000000007fffffffull, 0x0000ffffffffffffull, 0x3ff00000ffffffffull, 0x3ff0000100000000ull,
    0x3fffffff00000000ull, 0x3ff7ffffffffffffull, 0x3ff8000000000001ull, 0x4000000000000001ull,
    0x3fefffffffffffeull, 0x0018000000000000ull, 0x0017ffffffffffffull, 0x000c000000000000ull,
    0x4007ffffffffffffull, 0x3fe5555555555555ull, 0x3fe5555555555556ull, 0x3cafffffffffffffull,
};
#define NEDGE (sizeof edge / sizeof edge[0])

int main(void)
{
    /* every pair of edges */
    for (size_t i = 0; i < NEDGE; i++) {
        for (size_t j = 0; j < NEDGE; j++) pair(edge[i], edge[j]);
    }
    /* an edge against random bits, and random against random */
    for (int n = 0; n < 2000000; n++) {
        pair(edge[rnd() % NEDGE], rnd());
        pair(rnd(), edge[rnd() % NEDGE]);
        pair(rnd(), rnd());
    }
    /* exponents close together, where add and subtract align, cancel and
     * round; and the same magnitude with the last few bits different */
    for (int n = 0; n < 4000000; n++) {
        const uint64_t a = rnd();
        const int d = (int)(rnd() % 131) - 65;
        int e = (int)((a >> 52) & 0x7ff) + d;
        if (e < 0) e = 0;
        if (e > 0x7fe) e = 0x7fe;
        const uint64_t b = (rnd() & 0x800fffffffffffffull) | ((uint64_t)e << 52);
        pair(a, b);
        pair(a, (a ^ (rnd() & 7)) ^ (rnd() & 0x8000000000000000ull));
    }
    /* few significant bits, so that products and quotients land on ties and
     * exact results; and the subnormal range, both sides and across it */
    for (int n = 0; n < 4000000; n++) {
        const uint64_t ka = ~0ull << (rnd() % 53), kb = ~0ull << (rnd() % 53);
        pair(rnd() & (0xfff0000000000000ull | ka), rnd() & (0xfff0000000000000ull | kb));
        const uint64_t tiny = (rnd() & 0x800fffffffffffffull) | ((rnd() % 4) << 52);
        pair(tiny, rnd());
        pair(rnd(), tiny);
        pair(tiny, (rnd() & 0x800fffffffffffffull) | ((rnd() % 4) << 52));
        /* a product or a quotient that ends up there */
        const uint64_t lo = (rnd() & 0x800fffffffffffffull) | ((uint64_t)(rnd() % 120 + 1) << 52);
        const uint64_t mid = (rnd() & 0x800fffffffffffffull) | ((uint64_t)(0x3ff - rnd() % 120) << 52);
        pair(lo, mid);
        const uint64_t hi = (rnd() & 0x800fffffffffffffull) | ((uint64_t)(0x7fe - rnd() % 120) << 52);
        const uint64_t up = (rnd() & 0x800fffffffffffffull) | ((uint64_t)(0x3ff + rnd() % 120) << 52);
        pair(hi, up);
        pair(hi, mid);
    }

    /* from integers */
    static const uint64_t ints[] = {
        0, 1, 2, 3, 0x7fffffffull, 0x80000000ull, 0xffffffffull, 0x100000000ull,
        0x1fffffffffffffull, 0x20000000000000ull, 0x20000000000001ull, 0x20000000000002ull,
        0x20000000000003ull, 0x3fffffffffffffull, 0x40000000000001ull, 0x7fffffffffffffffull,
        0x8000000000000000ull, 0x8000000000000001ull, 0xfffffffffffff800ull, 0xfffffffffffffbffull,
        0xfffffffffffffc00ull, 0xfffffffffffffc01ull, 0xffffffffffffffffull, 0x7ffffffffffffc00ull,
        0x7ffffffffffffdffull, 0x7ffffffffffffe00ull,
    };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
        volatile uint64_t u = ints[i];
        same("floatundidf", u, 0, box___floatundidf(u), (double)u);
        same("floatdidf", u, 0, box___floatdidf((int64_t)u), (double)(int64_t)u);
        same("floatunsidf", u, 0, box___floatunsidf((uint32_t)u), (double)(uint32_t)u);
        same("floatsidf", u, 0, box___floatsidf((int32_t)u), (double)(int32_t)u);
    }
    for (int n = 0; n < 4000000; n++) {
        volatile uint64_t u = rnd() >> (rnd() % 64);
        if (rnd() & 1) u = 0ull - u;
        same("floatundidf", u, 0, box___floatundidf(u), (double)u);
        same("floatdidf", u, 0, box___floatdidf((int64_t)u), (double)(int64_t)u);
        same("floatunsidf", u, 0, box___floatunsidf((uint32_t)u), (double)(uint32_t)u);
        same("floatsidf", u, 0, box___floatsidf((int32_t)u), (double)(int32_t)u);
    }

    /* the bit counts, against the compiler's; zero says the width */
    for (int n = 0; n < 2000000; n++) {
        const uint64_t v = rnd() >> (rnd() % 64);
        const uint32_t w = (uint32_t)(rnd() >> (rnd() % 64));
        checks += 3;
        if (v && box___clzdi2(v) != __builtin_clzll(v)) { failures++; printf("FAIL clzdi2 %016" PRIx64 "\n", v); }
        if (w && box___clzsi2(w) != __builtin_clz(w)) { failures++; printf("FAIL clzsi2 %08x\n", w); }
        if (w && box___ctzsi2(w) != __builtin_ctz(w)) { failures++; printf("FAIL ctzsi2 %08x\n", w); }
    }
    for (int k = 0; k < 64; k++) {
        checks += 3;
        if (box___clzdi2(1ull << k) != 63 - k) { failures++; printf("FAIL clzdi2 bit %d\n", k); }
        if (k < 32 && (box___clzsi2(1u << k) != 31 - k || box___ctzsi2(1u << k) != k)) {
            failures++;
            printf("FAIL clzsi2/ctzsi2 bit %d\n", k);
        }
    }
    checks += 3;
    if (box___clzsi2(0) != 32 || box___ctzsi2(0) != 32 || box___clzdi2(0) != 64) {
        failures++;
        printf("FAIL zero does not say the width\n");
    }

    printf(failures ? "softdouble: %" PRIu64 " checks, %" PRIu64 " FAILED\n"
                    : "softdouble: %" PRIu64 " checks, all the host's bits\n", checks, failures);
    return failures != 0;
}
