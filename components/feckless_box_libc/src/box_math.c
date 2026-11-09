/* SPDX-License-Identifier: MIT
 *
 * Single-precision maths for a boxed app (u-feckless-0020).
 *
 * The toolchain's picolibc keeps its libm in libc.a, but most of it was not
 * compiled position-independent: atan2f, expf, exp2f, logf, log10f, log2f,
 * powf and the sin/cos range reduction all carry R_RISCV_HI20 relocations
 * against their constant tables, and refuse to link into a shared object.
 * These are this component's own, compiled with the app.
 *
 * Accuracy: a few ulp across the normal range, not correctly rounded --
 * enough for audio levels, windows, colours and layout, which is what a
 * boxed app on the Tab5 uses them for. test/math_test.c checks each against
 * the host's libm and prints the worst error found. sinf/cosf/tanf reduce
 * their argument with pi/2 in nine pieces (Cody-Waite, extended), good for
 * |x| up to 65536 including the floats nearest a multiple of pi/2; beyond that the argument is first taken mod a float 2 pi, so the
 * phase is approximate -- bounded, but not the true value.  powf's error
 * grows with |y log2 x|: without a wider type than float, y log2 m carries
 * float's rounding, about |y| * 4e-8 relative at the result.
 *
 * errno is not set; the special values (NaN, the infinities, signed zeros)
 * follow C99 Annex F.
 */
#include <math.h>
#include <stdint.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

typedef union { float f; uint32_t u; int32_t i; } fbits;

static inline uint32_t bits(float x) { fbits b = { .f = x }; return b.u; }
static inline float from(uint32_t u) { fbits b = { .u = u }; return b.f; }
static inline float fabs_(float x) { return from(bits(x) & 0x7fffffffu); }
static inline int isnan_(float x) { return (bits(x) & 0x7fffffffu) > 0x7f800000u; }
static inline int isinf_(float x) { return (bits(x) & 0x7fffffffu) == 0x7f800000u; }
static inline float copysign_(float m, float s)
{
    return from((bits(m) & 0x7fffffffu) | (bits(s) & 0x80000000u));
}

/* x * 2^n, n anywhere an int reaches; through subnormals and to 0 or inf. */
static float scale2(float x, int n)
{
    while (n > 127) { x *= 0x1p127f; n -= 127; if (isinf_(x)) return x; }
    while (n < -126) { x *= 0x1p-126f; n += 126; if (x == 0.0f) return x; }
    return x * from((uint32_t)(n + 127) << 23);
}

/* Nearest integer, ties away from zero, as a float; |x| < 2^23. */
static inline float rint_away(float x)
{
    return (float)(int32_t)(x + copysign_(0.5f, x));
}

/* ---- rounding ---------------------------------------------------------- */

float BOX_NAME(truncf)(float x)
{
    if (!(fabs_(x) < 0x1p23f)) return x;            /* integral, inf or NaN */
    return copysign_((float)(int32_t)x, x);
}

float BOX_NAME(floorf)(float x)
{
    if (!(fabs_(x) < 0x1p23f)) return x;
    float t = (float)(int32_t)x;
    if (t > x) t -= 1.0f;
    return copysign_(t, x) == 0.0f ? copysign_(0.0f, x) : t;
}

float BOX_NAME(ceilf)(float x)
{
    if (!(fabs_(x) < 0x1p23f)) return x;
    float t = (float)(int32_t)x;
    if (t < x) t += 1.0f;
    return t == 0.0f ? copysign_(0.0f, x) : t;
}

float BOX_NAME(roundf)(float x)
{
    if (!(fabs_(x) < 0x1p23f)) return x;
    float t = (float)(int32_t)x;                    /* toward zero */
    if (fabs_(x - t) >= 0.5f) t += copysign_(1.0f, x);
    return t == 0.0f ? copysign_(0.0f, x) : t;
}

long BOX_NAME(lroundf)(float x) { return (long)BOX_NAME(roundf)(x); }
long BOX_NAME(lrintf)(float x) { return (long)BOX_NAME(roundf)(x); }

float BOX_NAME(fabsf)(float x) { return fabs_(x); }
float BOX_NAME(copysignf)(float x, float y) { return copysign_(x, y); }

float BOX_NAME(fmaxf)(float x, float y)
{
    if (isnan_(x)) return y;
    if (isnan_(y)) return x;
    if (x == y) return bits(x) & 0x80000000u ? y : x;   /* +0 over -0 */
    return x > y ? x : y;
}

float BOX_NAME(fminf)(float x, float y)
{
    if (isnan_(x)) return y;
    if (isnan_(y)) return x;
    if (x == y) return bits(x) & 0x80000000u ? x : y;
    return x < y ? x : y;
}

float BOX_NAME(sqrtf)(float x)
{
#if defined(__riscv) && defined(__riscv_flen)
    float r;
    __asm__("fsqrt.s %0, %1" : "=f"(r) : "f"(x));
    return r;
#else
    return __builtin_sqrtf(x);
#endif
}

/* Exact: the remainder of x by y, by long division on the significands. */
float BOX_NAME(fmodf)(float x, float y)
{
    uint32_t ux = bits(x), uy = bits(y), sx = ux & 0x80000000u;
    ux &= 0x7fffffffu;
    uy &= 0x7fffffffu;
    if (uy == 0 || ux >= 0x7f800000u || uy > 0x7f800000u) return (x * y) / (x * y);
    if (ux < uy) return x;
    int ex = (int)(ux >> 23), ey = (int)(uy >> 23);
    uint32_t mx, my;
    if (ex == 0) { mx = ux; while (!(mx & 0x00800000u)) { mx <<= 1; ex--; } ex++; }
    else mx = (ux & 0x007fffffu) | 0x00800000u;
    if (ey == 0) { my = uy; while (!(my & 0x00800000u)) { my <<= 1; ey--; } ey++; }
    else my = (uy & 0x007fffffu) | 0x00800000u;
    for (; ex > ey; ex--) {
        if (mx >= my) mx -= my;
        if (mx == 0) return from(sx);
        mx <<= 1;
    }
    if (mx >= my) mx -= my;
    if (mx == 0) return from(sx);
    while (!(mx & 0x00800000u)) { mx <<= 1; ex--; }
    if (ex > 0) return from(sx | ((uint32_t)ex << 23) | (mx & 0x007fffffu));
    return from(sx | (mx >> (1 - ex)));
}

/* ---- exp and log ------------------------------------------------------- */

static const float LN2_HI = 6.9314575195e-01f;   /* 0x3f317200, 16 low bits clear */
static const float LN2_LO = 1.4286067653e-06f;
static const float LOG2E = 1.4426950216e+00f;

/* e^r - 1 for |r| <= ln2/2, a degree-7 Taylor polynomial. */
static inline float expm1_kernel(float r)
{
    return r * (1.0f + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120
           + r * (1.0f / 720 + r * (1.0f / 5040)))))));
}

float BOX_NAME(expf)(float x)
{
    if (isnan_(x)) return x;
    if (x > 88.72284f) return 0x1p127f * 0x1p127f;   /* inf */
    if (x < -103.97208f) return 0.0f;
    const float k = rint_away(x * LOG2E);
    const float r = (x - k * LN2_HI) - k * LN2_LO;
    return scale2(1.0f + expm1_kernel(r), (int)k);
}

float BOX_NAME(exp2f)(float x)
{
    if (isnan_(x)) return x;
    if (x >= 128.0f) return 0x1p127f * 0x1p127f;
    if (x < -150.0f) return 0.0f;
    const float k = rint_away(x);
    const float r = (x - k) * 0.69314718056f;
    return scale2(1.0f + expm1_kernel(r), (int)k);
}

float BOX_NAME(exp10f)(float x)
{
    if (isnan_(x)) return x;
    if (x > 38.5318394f) return 0x1p127f * 0x1p127f;
    if (x < -45.1544993f) return 0.0f;
    /* 10^x = 2^(x log2 10); the multiply split for the extra bits */
    const float k = rint_away(x * 3.3219280949f);
    const float r = (x - k * 0.30102539062f) - k * 4.6050390802e-06f;  /* x - k log10(2) */
    return scale2(1.0f + expm1_kernel(r * 2.302585093f), (int)k);
}

/* log(x) for finite x > 0, returned as hi + lo (hi carries e ln2's top). */
static void log_parts(float x, float *hi, float *lo)
{
    uint32_t u = bits(x);
    int e = 0;
    if (u < 0x00800000u) { x *= 0x1p25f; u = bits(x); e = -25; }   /* subnormal */
    e += (int)(u >> 23) - 127;
    u = (u & 0x007fffffu) | 0x3f800000u;                           /* m in [1,2) */
    if (u > 0x3fb504f3u) { u -= 0x00800000u; e++; }                /* m in [0.707,1.414) */
    const float f = from(u) - 1.0f;
    const float s = f / (2.0f + f);
    const float z = s * s;
    const float r = z * (2.0f / 3 + z * (2.0f / 5 + z * (2.0f / 7 + z * (2.0f / 9 + z * (2.0f / 11)))));
    const float hf = 0.5f * f * f;
    /* log(m) = f - hf + s(hf + r), the fdlibm arrangement */
    *hi = (float)e * LN2_HI + f;
    *lo = (float)e * LN2_LO + (s * (hf + r) - hf);
}

static int log_special(float x, float *out)
{
    if (isnan_(x)) { *out = x; return 1; }
    if (x == 0.0f) { *out = -0x1p127f * 0x1p127f; return 1; }      /* -inf */
    if (bits(x) & 0x80000000u) { *out = (x - x) / (x - x); return 1; }   /* NaN */
    if (isinf_(x)) { *out = x; return 1; }
    return 0;
}

float BOX_NAME(logf)(float x)
{
    float o, hi, lo;
    if (log_special(x, &o)) return o;
    log_parts(x, &hi, &lo);
    return hi + lo;
}

float BOX_NAME(log2f)(float x)
{
    float o, hi, lo;
    if (log_special(x, &o)) return o;
    log_parts(x, &hi, &lo);
    return hi * LOG2E + lo * LOG2E;
}

float BOX_NAME(log10f)(float x)
{
    float o, hi, lo;
    if (log_special(x, &o)) return o;
    log_parts(x, &hi, &lo);
    return hi * 0.43429448190f + lo * 0.43429448190f;
}

/* x^y.  log2|x| as an exponent plus a small fraction, so that y times the
 * exponent part is carried apart from the rounding of the rest. */
float BOX_NAME(powf)(float x, float y)
{
    if (y == 0.0f || x == 1.0f) return 1.0f;
    if (isnan_(x) || isnan_(y)) return x + y;
    /* The bases an app means by name, and the most accurate route to them:
     * decibels and octaves. */
    if (x == 10.0f) return BOX_NAME(exp10f)(y);
    if (x == 2.0f) return BOX_NAME(exp2f)(y);

    /* y an integer?  odd? */
    int yint = 0;
    const float ay = fabs_(y);
    if (ay >= 0x1p24f) yint = 2;
    else if (BOX_NAME(floorf)(y) == y) yint = ((int32_t)ay & 1) ? 1 : 2;

    if (isinf_(y)) {
        const float ax = fabs_(x);
        if (ax == 1.0f) return 1.0f;
        return (ax > 1.0f) == (y > 0.0f) ? ay : 0.0f;
    }
    if (x == 0.0f) {
        const float r = y < 0.0f ? 0x1p127f * 0x1p127f : 0.0f;
        return yint == 1 ? copysign_(r, x) : r;
    }
    if (isinf_(x)) {
        const float r = y < 0.0f ? 0.0f : ay * 0x1p127f * 0x1p127f;
        return (x < 0.0f && yint == 1) ? -r : r;
    }
    float sign = 1.0f;
    if (x < 0.0f) {
        if (!yint) return (x - x) / (x - x);
        if (yint == 1) sign = -1.0f;
        x = -x;
    }

    /* x = 2^e m, m in [0.707, 1.414) */
    uint32_t u = bits(x);
    int e = 0;
    if (u < 0x00800000u) { x *= 0x1p25f; u = bits(x); e = -25; }
    e += (int)(u >> 23) - 127;
    u = (u & 0x007fffffu) | 0x3f800000u;
    if (u > 0x3fb504f3u) { u -= 0x00800000u; e++; }
    float hi, lo;
    log_parts(from(u), &hi, &lo);                    /* log m, |.| < 0.35 */
    const float lm2 = (hi + lo) * LOG2E;             /* log2 m */

    /* y log2 x = y e + y lm2.  y e in two exact halves: y's top 12 bits and
     * the rest, each times an e of at most 8 bits, so neither product
     * rounds; the integer part comes off the first exactly. */
    const float yh = from(bits(y) & 0xfffff000u), yl = y - yh;
    const float ye = yh * (float)e;
    if (ye > 300.0f) return sign * 0x1p127f * 0x1p127f;
    if (ye < -400.0f) return sign * 0.0f;
    const float ke = rint_away(ye);
    const float t = (ye - ke) + yl * (float)e + y * lm2;
    if (t > 300.0f) return sign * 0x1p127f * 0x1p127f;
    if (t < -400.0f) return sign * 0.0f;
    const float kt = rint_away(t);
    const float r = (t - kt) * 0.69314718056f;
    return sign * scale2(1.0f + expm1_kernel(r), (int)ke + (int)kt);
}

/* ---- trigonometry ------------------------------------------------------ */

static const float TWO_OVER_PI = 6.3661977237e-01f;

/* pi/2 in 8-bit pieces: k < 2^16 times any of them is exact in a float,
 * so the subtractions below cancel without rounding until the remainder is
 * small, and r keeps its relative precision even when x is within a hair
 * of a multiple of pi/2. */
static const float PIO2_8[9] = {
    0x1.92p+0f, 0x1.fap-12f, 0x1.54p-20f, 0x1.1p-30f, 0x1.68p-39f,
    0x1.84p-48f, 0x1.a6p-58f, 0x1.3p-69f, 0x1.98a2e0p-77f,
};

/* x = k pi/2 + r, |r| <= pi/4; returns k. */
static int reduce(float x, float *r)
{
    if (fabs_(x) <= 0.78539816f) { *r = x; return 0; }
    if (fabs_(x) > 65536.0f) x = BOX_NAME(fmodf)(x, 6.2831855f);   /* phase not exact */
    const float k = rint_away(x * TWO_OVER_PI);
    float v = x;
    for (int i = 0; i < 9; i++) v -= k * PIO2_8[i];
    *r = v;
    return (int)(int32_t)k;
}

static inline float sin_kernel(float r)
{
    const float z = r * r;
    return r + r * z * (-1.0f / 6 + z * (1.0f / 120 + z * (-1.0f / 5040 + z * (1.0f / 362880))));
}

static inline float cos_kernel(float r)
{
    const float z = r * r;
    return 1.0f - 0.5f * z + z * z * (1.0f / 24 + z * (-1.0f / 720 + z * (1.0f / 40320
           + z * (-1.0f / 3628800))));
}

static void sincos_(float x, float *s, float *c)
{
    if (isnan_(x) || isinf_(x)) { *s = *c = x - x; return; }
    float r;
    const int k = reduce(x, &r);
    const float sr = sin_kernel(r), cr = cos_kernel(r);
    switch (k & 3) {
    case 0: *s = sr;  *c = cr;  break;
    case 1: *s = cr;  *c = -sr; break;
    case 2: *s = -sr; *c = -cr; break;
    default: *s = -cr; *c = sr; break;
    }
}

float BOX_NAME(sinf)(float x)
{
    if (fabs_(x) < 0x1p-12f) return x;     /* keeps -0 and subnormals */
    float s, c;
    sincos_(x, &s, &c);
    return s;
}

float BOX_NAME(cosf)(float x)
{
    float s, c;
    sincos_(x, &s, &c);
    return c;
}

void BOX_NAME(sincosf)(float x, float *s, float *c)
{
    sincos_(x, s, c);
    if (fabs_(x) < 0x1p-12f) *s = x;
}

float BOX_NAME(tanf)(float x)
{
    if (fabs_(x) < 0x1p-12f) return x;
    float s, c;
    sincos_(x, &s, &c);
    return s / c;
}

/* atan for 0 <= x <= 1 */
static float atan01(float x)
{
    float off = 0.0f;
    if (x > 0.26794919f) {                              /* tan(pi/12) */
        x = (x * 1.7320508f - 1.0f) / (x + 1.7320508f); /* atan x = pi/6 + atan(that) */
        off = 0.52359878f;
    }
    const float z = x * x;
    return off + x - x * z * (1.0f / 3 - z * (1.0f / 5 - z * (1.0f / 7 - z * (1.0f / 9
           - z * (1.0f / 11 - z * (1.0f / 13))))));
}

float BOX_NAME(atanf)(float x)
{
    if (isnan_(x)) return x;
    const float a = fabs_(x);
    if (a < 0x1p-12f) return x;
    const float r = a <= 1.0f ? atan01(a) : 1.5707963268f - atan01(1.0f / a);
    return copysign_(r, x);
}

float BOX_NAME(atan2f)(float y, float x)
{
    if (isnan_(x) || isnan_(y)) return x + y;
    const float pi = 3.1415926536f, pio2 = 1.5707963268f;
    const int xneg = (bits(x) & 0x80000000u) != 0;
    if (y == 0.0f) return xneg ? copysign_(pi, y) : y;          /* incl. x == +-0 */
    if (x == 0.0f) return copysign_(pio2, y);
    if (isinf_(x)) {
        if (isinf_(y)) return copysign_(xneg ? 3 * pi / 4 : pi / 4, y);
        return xneg ? copysign_(pi, y) : copysign_(0.0f, y);
    }
    if (isinf_(y)) return copysign_(pio2, y);
    const float ax = fabs_(x), ay = fabs_(y);
    float r = ay <= ax ? atan01(ay / ax) : pio2 - atan01(ax / ay);
    if (xneg) r = pi - r;
    return copysign_(r, y);
}

float BOX_NAME(asinf)(float x)
{
    if (isnan_(x)) return x;
    const float a = fabs_(x);
    if (a > 1.0f) return (x - x) / (x - x);
    if (a < 0x1p-12f) return x;
    return BOX_NAME(atan2f)(x, BOX_NAME(sqrtf)((1.0f - a) * (1.0f + a)));
}

float BOX_NAME(acosf)(float x)
{
    if (isnan_(x)) return x;
    const float a = fabs_(x);
    if (a > 1.0f) return (x - x) / (x - x);
    return BOX_NAME(atan2f)(BOX_NAME(sqrtf)((1.0f - a) * (1.0f + a)), x);
}

/* ---- hyperbolic -------------------------------------------------------- */

float BOX_NAME(tanhf)(float x)
{
    if (isnan_(x)) return x;
    const float a = fabs_(x);
    if (a < 0x1p-12f) return x;
    if (a > 9.0f) return copysign_(1.0f, x);
    float t;
    if (a < 0.5f) {
        /* e^2a - 1 without the cancellation: 2a < 1, so k is 0 or 1 */
        const float k = rint_away(2.0f * a * LOG2E);
        const float r = (2.0f * a - k * LN2_HI) - k * LN2_LO;
        const float m = k == 0.0f ? expm1_kernel(r) : 2.0f * expm1_kernel(r) + 1.0f;
        t = m / (m + 2.0f);
    } else {
        const float e2 = BOX_NAME(expf)(2.0f * a);
        t = 1.0f - 2.0f / (e2 + 1.0f);
    }
    return copysign_(t, x);
}

/* ---- u-feckless-0025: the rest of the ABI's float names ------------------ */

float BOX_NAME(ldexpf)(float x, int n)
{
    if (isnan_(x) || isinf_(x) || x == 0.0f) return x;
    return scale2(x, n);
}

/* Round to nearest, ties to even (the FPU's default mode): adding and taking
 * away 2^23 leaves no fraction bits for the rounding to keep. */
float BOX_NAME(rintf)(float x)
{
    if (!(fabs_(x) < 0x1p23f)) return x;
    const float big = copysign_(0x1p23f, x);
    volatile float t = x + big;                 /* volatile: not folded away */
    return copysign_(t - big, x);
}

float BOX_NAME(hypotf)(float x, float y)
{
    float a = fabs_(x), b = fabs_(y);
    if (isinf_(a) || isinf_(b)) return 0x1p127f * 0x1p127f;   /* inf, even with a NaN */
    if (isnan_(a) || isnan_(b)) return a + b;
    if (a < b) { const float t = a; a = b; b = t; }
    if (a == 0.0f) return 0.0f;
    const float r = b / a;
    return a * BOX_NAME(sqrtf)(1.0f + r * r);
}

float BOX_NAME(cbrtf)(float x)
{
    if (isnan_(x) || isinf_(x) || x == 0.0f) return x;
    const float a = fabs_(x);
    float y = BOX_NAME(exp2f)(BOX_NAME(log2f)(a) * (1.0f / 3));
    y -= (y * y * y - a) / (3.0f * y * y);      /* one Newton step */
    return copysign_(y, x);
}
