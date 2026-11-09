/* SPDX-License-Identifier: MIT
 *
 * src/box_math.c against the host's libm, in double: the worst error of
 * each function over a sweep of its domain, in ulps of the float result,
 * and the special values C99 Annex F gives.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOX_NAME(n) box_##n
#define F1(n) float box_##n(float);
F1(truncf) F1(floorf) F1(ceilf) F1(roundf) F1(fabsf) F1(sqrtf) F1(expf) F1(exp2f)
F1(exp10f) F1(logf) F1(log2f) F1(log10f) F1(sinf) F1(cosf) F1(tanf) F1(atanf)
F1(asinf) F1(acosf) F1(tanhf)
float box_fmaxf(float, float), box_fminf(float, float), box_fmodf(float, float);
float box_powf(float, float), box_atan2f(float, float), box_copysignf(float, float);
long box_lroundf(float);
float box_ldexpf(float, int), box_rintf(float), box_hypotf(float, float), box_cbrtf(float);
void box_sincosf(float, float *, float *);

static int failures;

static double ulps(float got, double want)
{
    if (isnan(want)) return isnan(got) ? 0 : 1e9;
    if (isinf(want)) return got == (float)want ? 0 : 1e9;
    const float wf = (float)want;
    if (isinf(wf)) return isinf(got) && (got > 0) == (want > 0) ? 0 : 1e9;
    int e;
    frexp(wf == 0 ? 0x1p-149 : wf, &e);
    double ulp = ldexp(1.0, e - 24);
    if (ulp < 0x1p-149) ulp = 0x1p-149;
    return fabs((double)got - want) / ulp;
}

static uint32_t rng = 12345;
static float rnd(float lo, float hi)
{
    rng = rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)((rng >> 8) * 0x1p-24);
}

static void check1(const char *name, float (*f)(float), double (*ref)(double),
                   float lo, float hi, double limit)
{
    double worst = 0;
    float at = 0;
    for (int i = 0; i < 400000; i++) {
        const float x = rnd(lo, hi);
        const double u = ulps(f(x), ref((double)x));
        if (u > worst) { worst = u; at = x; }
    }
    printf("%-8s [%g, %g]: worst %.2f ulp at %a\n", name, lo, hi, worst, at);
    if (worst > limit) { printf("  FAIL: over %.1f\n", limit); failures++; }
}

static double exp10_(double x) { return pow(10.0, x); }

#define SAME(expr, want) do { const float g_ = (expr), w_ = (want); \
    if (memcmp(&g_, &w_, 4) && !(isnan(g_) && isnan(w_))) { \
        printf("FAIL: %s = %a, want %a\n", #expr, g_, w_); failures++; } } while (0)

int main(void)
{
    double worst;
    check1("expf", box_expf, exp, -87, 88, 3);
    check1("expf", box_expf, exp, -103, -87, 3);
    check1("exp2f", box_exp2f, exp2, -126, 127, 3);
    check1("exp10f", box_exp10f, exp10_, -37, 38, 4);
    check1("logf", box_logf, log, 1e-30f, 1e30f, 3);
    check1("logf", box_logf, log, 0.5f, 2, 3);
    check1("log2f", box_log2f, log2, 1e-30f, 1e30f, 3);
    check1("log10f", box_log10f, log10, 1e-30f, 1e30f, 3);
    check1("log10f", box_log10f, log10, 0.5f, 2, 3);
    check1("sinf", box_sinf, sin, -10, 10, 3);
    check1("sinf", box_sinf, sin, -100, 100, 3);
    check1("sinf", box_sinf, sin, -60000, 60000, 4);
    check1("cosf", box_cosf, cos, -10, 10, 3);
    check1("cosf", box_cosf, cos, -60000, 60000, 4);
    /* the hard cases: floats nearest a multiple of pi/2, where the result
     * is all cancellation */
    worst = 0;
    for (int k = 1; k < 41000; k++) {
        const float x = (float)(k * M_PI_2);
        double u = ulps(box_sinf(x), sin((double)x));
        if (u > worst) worst = u;
        u = ulps(box_cosf(x), cos((double)x));
        if (u > worst) worst = u;
    }
    printf("sin/cos  floats nearest k pi/2, k < 41000: worst %.2f ulp\n", worst);
    if (worst > 4) { printf("  FAIL\n"); failures++; }
    check1("tanf", box_tanf, tan, -1.5f, 1.5f, 4);
    check1("atanf", box_atanf, atan, -10, 10, 3);
    check1("atanf", box_atanf, atan, -1e6f, 1e6f, 3);
    check1("asinf", box_asinf, asin, -1, 1, 4);
    check1("acosf", box_acosf, acos, -1, 1, 4);
    check1("tanhf", box_tanhf, tanh, -10, 10, 4);
    check1("sqrtf", box_sqrtf, sqrt, 0, 1e30f, 0.5);
    check1("floorf", box_floorf, floor, -1e8f, 1e8f, 0);
    check1("floorf", box_floorf, floor, -3, 3, 0);
    check1("ceilf", box_ceilf, ceil, -3, 3, 0);
    check1("truncf", box_truncf, trunc, -3, 3, 0);
    check1("roundf", box_roundf, round, -3, 3, 0);

    /* powf: the bases and exponents an app uses -- dB, gamma, squares */
    worst = 0;
    for (int i = 0; i < 400000; i++) {
        const float x = rnd(1e-6f, 100), y = rnd(-8, 8);
        const double u = ulps(box_powf(x, y), pow(x, y));
        if (u > worst) worst = u;
    }
    printf("powf     x in (0,100], y in [-8,8]: worst %.2f ulp\n", worst);
    if (worst > 8) { printf("  FAIL\n"); failures++; }
    worst = 0;
    for (int i = 0; i < 400000; i++) {
        const float y = rnd(-37, 38);
        const double u = ulps(box_powf(10, y), pow(10, y));
        if (u > worst) worst = u;
    }
    printf("powf     10^y, y in [-37,38]: worst %.2f ulp\n", worst);
    if (worst > 8) { printf("  FAIL\n"); failures++; }

    /* atan2f over all four quadrants */
    worst = 0;
    for (int i = 0; i < 400000; i++) {
        const float y = rnd(-100, 100), x = rnd(-100, 100);
        const double u = ulps(box_atan2f(y, x), atan2(y, x));
        if (u > worst) worst = u;
    }
    printf("atan2f   [-100,100]^2: worst %.2f ulp\n", worst);
    if (worst > 4) { printf("  FAIL\n"); failures++; }

    /* fmodf is exact */
    for (int i = 0; i < 400000; i++) {
        const float x = rnd(-1e6f, 1e6f), y = rnd(-100, 100);
        SAME(box_fmodf(x, y), fmodf(x, y));
    }
    SAME(box_fmodf(0x1p-140f, 0x1p-147f), fmodf(0x1p-140f, 0x1p-147f));
    SAME(box_fmodf(5.5f, 0x1p-149f), fmodf(5.5f, 0x1p-149f));
    SAME(box_fmodf(-0.0f, 3), -0.0f);

    /* Annex F */
    SAME(box_floorf(-0.0f), -0.0f);
    SAME(box_floorf(-0.5f), -1.0f);
    SAME(box_ceilf(-0.5f), -0.0f);
    SAME(box_roundf(2.5f), 3.0f);
    SAME(box_roundf(-2.5f), -3.0f);
    SAME(box_roundf(-0.4f), -0.0f);
    SAME(box_truncf(-0.7f), -0.0f);
    SAME(box_floorf(1e30f), 1e30f);
    SAME(box_fmaxf(NAN, 1), 1.0f);
    SAME(box_fmaxf(-0.0f, 0.0f), 0.0f);
    SAME(box_fminf(0.0f, -0.0f), -0.0f);
    SAME(box_expf(-INFINITY), 0.0f);
    SAME(box_expf(INFINITY), INFINITY);
    SAME(box_expf(89), INFINITY);
    SAME(box_logf(0), -INFINITY);
    SAME(box_logf(-1), NAN);
    SAME(box_logf(1), 0.0f);
    SAME(box_logf(0x1p-149f), logf(0x1p-149f));
    SAME(box_sinf(-0.0f), -0.0f);
    SAME(box_sinf(INFINITY), NAN);
    SAME(box_atan2f(0.0f, -0.0f), (float)M_PI);
    SAME(box_atan2f(-0.0f, -0.0f), -(float)M_PI);
    SAME(box_atan2f(-0.0f, 1), -0.0f);
    SAME(box_atan2f(1, 0), (float)M_PI_2);
    SAME(box_atan2f(-INFINITY, -INFINITY), -3 * (float)M_PI_4);
    SAME(box_powf(NAN, 0), 1.0f);
    SAME(box_powf(1, NAN), 1.0f);
    SAME(box_powf(-2, 3), -8.0f);
    SAME(box_powf(-2, 0.5f), NAN);
    SAME(box_powf(-0.0f, -3), -INFINITY);
    SAME(box_powf(0.0f, -2), INFINITY);
    SAME(box_powf(-1, INFINITY), 1.0f);
    SAME(box_powf(0.5f, INFINITY), 0.0f);
    SAME(box_powf(2, 10), 1024.0f);
    SAME(box_powf(10, 2), 100.0f);
    SAME(box_powf(2, -149), 0x1p-149f);
    SAME(box_powf(2, 128), INFINITY);
    SAME(box_sqrtf(-0.0f), -0.0f);
    SAME(box_tanhf(-INFINITY), -1.0f);
    SAME(box_copysignf(3, -0.0f), -3.0f);
    if (box_lroundf(-2.5f) != -3) { printf("FAIL: lroundf(-2.5)\n"); failures++; }
    float s, c;
    box_sincosf(1.0f, &s, &c);
    SAME(s, box_sinf(1.0f));
    SAME(c, box_cosf(1.0f));

    /* u-feckless-0025 */
    check1("cbrtf", box_cbrtf, cbrt, -1e30f, 1e30f, 2);
    check1("rintf", box_rintf, rint, -10, 10, 0);
    worst = 0;
    for (int i = 0; i < 400000; i++) {
        const float x = rnd(-1e18f, 1e18f), y = rnd(-1e18f, 1e18f);
        const double u = ulps(box_hypotf(x, y), hypot(x, y));
        if (u > worst) worst = u;
    }
    printf("hypotf   [-1e18,1e18]^2: worst %.2f ulp\n", worst);
    if (worst > 2) { printf("  FAIL\n"); failures++; }
    SAME(box_rintf(2.5f), 2.0f);
    SAME(box_rintf(3.5f), 4.0f);
    SAME(box_rintf(-0.5f), -0.0f);
    SAME(box_ldexpf(1.5f, 4), 24.0f);
    SAME(box_ldexpf(1.0f, -149), 0x1p-149f);
    SAME(box_ldexpf(1.0f, 128), INFINITY);
    SAME(box_ldexpf(-0.0f, 5), -0.0f);
    SAME(box_hypotf(3, 4), 5.0f);
    SAME(box_hypotf(INFINITY, NAN), INFINITY);
    SAME(box_cbrtf(-27), -3.0f);
    SAME(box_cbrtf(-0.0f), -0.0f);

    printf(failures ? "math: %d FAILED\n" : "math: all passed\n", failures);
    return failures != 0;
}
