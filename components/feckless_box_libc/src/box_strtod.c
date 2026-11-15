/* SPDX-License-Identifier: MIT
 *
 * strtod, strtof and atof for a boxed app (u-feckless-0048). picolibc's are
 * not among the members of libc.a that link into a shared object, and they
 * set errno as a thread-local; Defeatist's cJSON parses every number
 * through strtod.
 *
 * Decimal and hexadecimal, "inf", "infinity" and "nan" in any case, as C11
 * 7.22.1.3. Up to 19 significant decimal digits are kept (the rest only
 * move the exponent). A value with at most 15 of them and a power of ten
 * up to 22 either way is one correctly rounded multiplication or division,
 * which is every number a JSON file or a tag is likely to hold. Past that,
 * 10^e is 5^e * 2^e: 5^e is built in double-double arithmetic (about 106
 * bits, products split by Veltkamp's method -- no fused multiply-add is
 * assumed), the digits multiplied or divided by it there, and the power of
 * two applied last, so the result is within an ulp. Not correctly rounded
 * in every case, as a bignum strtod is. Overflow gives HUGE_VAL and ERANGE;
 * underflow to zero, 0 and ERANGE. With no number, 0 and *end = the
 * string.
 *
 * Named box_ in the source and given the standard names by BOX_NAME, so the
 * host test can link these beside its own libc.
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* s starts with word (lower case), case ignored */
static bool starts(const char *s, const char *word)
{
    while (*word) {
        if (lower(*s++) != *word++) return false;
    }
    return true;
}

static const double k_exact[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

/* ---- double-double: a value as hi + lo, |lo| <= ulp(hi) / 2 ------------- */

typedef struct { double hi, lo; } dd_t;

static dd_t two_sum(double a, double b)
{
    const double s = a + b, bb = s - a;
    return (dd_t){ s, (a - (s - bb)) + (b - bb) };
}

static void split(double a, double *hi, double *lo)
{
    const double c = 134217729.0 * a;           /* 2^27 + 1 */
    *hi = c - (c - a);
    *lo = a - *hi;
}

/* a * b exactly, as hi + lo */
static dd_t two_prod(double a, double b)
{
    double ah, al, bh, bl;
    split(a, &ah, &al);
    split(b, &bh, &bl);
    const double p = a * b;
    return (dd_t){ p, ((ah * bh - p) + ah * bl + al * bh) + al * bl };
}

static dd_t dd_mul(dd_t a, dd_t b)
{
    dd_t p = two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return two_sum(p.hi, p.lo);
}

/* 5^k, k <= 350: at most 2.3e244, so no product here overflows */
static dd_t pow5(int k)
{
    dd_t r = { 1.0, 0.0 }, b = { 5.0, 0.0 };
    while (k) {
        if (k & 1) r = dd_mul(r, b);
        b = dd_mul(b, b);
        k >>= 1;
    }
    return r;
}

/* m (any 64 bits) as hi + lo, exactly */
static dd_t dd_of(uint64_t m)
{
    return two_sum((double)(m - (m & 0x7ff)), (double)(m & 0x7ff));
}

/* m * 10^e for |e| <= 350, within an ulp */
static double scale10(uint64_t m, int e)
{
    const dd_t x = dd_of(m);
    const dd_t p = pow5(e < 0 ? -e : e);
    dd_t r;
    if (e >= 0) {
        r = dd_mul(x, p);
    } else {
        /* x / p: a quotient, then the remainder's */
        const double q1 = x.hi / p.hi;
        const dd_t qp = dd_mul((dd_t){ q1, 0.0 }, p);
        const dd_t rem = two_sum(x.hi - qp.hi, x.lo - qp.lo);
        const double q2 = (rem.hi + rem.lo) / p.hi;
        r = two_sum(q1, q2);
    }
    return ldexp(r.hi + r.lo, e);
}

static double hex(const char *s, const char **end, bool *any)
{
    uint64_t m = 0;
    int shift = 0;
    bool dot = false;
    *any = false;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (lower(*s) >= 'a' && lower(*s) <= 'f') d = lower(*s) - 'a' + 10;
        else if (*s == '.' && !dot) { dot = true; continue; }
        else break;
        *any = true;
        if (m >> 60) {
            if (!dot) shift += 4;               /* past 60 bits: only the exponent moves */
        } else {
            m = m << 4 | (uint64_t)d;
            if (dot) shift -= 4;
        }
    }
    if (*any && lower(*s) == 'p') {
        const char *p = s + 1;
        bool neg = false;
        if (*p == '+' || *p == '-') neg = *p++ == '-';
        if (*p >= '0' && *p <= '9') {
            int e = 0;
            while (*p >= '0' && *p <= '9') {
                if (e < 100000) e = e * 10 + (*p - '0');
                p++;
            }
            shift += neg ? -e : e;
            s = p;
        }
    }
    *end = s;
    return ldexp((double)m, shift);
}

double BOX_NAME(strtod)(const char *str, char **end)
{
    const char *s = str;
    while (space(*s)) s++;
    bool neg = false;
    if (*s == '+' || *s == '-') neg = *s++ == '-';

    if (starts(s, "inf")) {
        s += starts(s, "infinity") ? 8 : 3;
        if (end) *end = (char *)s;
        return neg ? -HUGE_VAL : HUGE_VAL;
    }
    if (starts(s, "nan")) {
        s += 3;
        if (*s == '(') {                        /* nan(n-char-sequence) */
            const char *p = s + 1;
            while ((*p >= '0' && *p <= '9') || (lower(*p) >= 'a' && lower(*p) <= 'z') || *p == '_') p++;
            if (*p == ')') s = p + 1;
        }
        if (end) *end = (char *)s;
        return neg ? -NAN : NAN;
    }
    if (s[0] == '0' && lower(s[1]) == 'x') {
        const char *e2;
        bool any;
        const double v = hex(s + 2, &e2, &any);
        if (!any) {                             /* "0x" alone is the 0 */
            if (end) *end = (char *)(s + 1);
            return neg ? -0.0 : 0.0;
        }
        if (end) *end = (char *)e2;
        if (isinf(v)) errno = ERANGE;
        return neg ? -v : v;
    }

    uint64_t m = 0;
    int digits = 0, exp10 = 0;
    bool any = false, dot = false;
    for (;; s++) {
        if (*s == '.' && !dot) {
            dot = true;
            continue;
        }
        if (*s < '0' || *s > '9') break;
        any = true;
        if (digits < 19) {
            if (m || *s != '0') {
                m = m * 10 + (uint64_t)(*s - '0');
                digits++;
            }
            if (dot) exp10--;
        } else if (!dot) {
            exp10++;                            /* a digit past the 19th: only the scale */
        }
    }
    if (!any) {
        if (end) *end = (char *)str;
        return 0.0;
    }
    if (lower(*s) == 'e') {
        const char *p = s + 1;
        bool eneg = false;
        if (*p == '+' || *p == '-') eneg = *p++ == '-';
        if (*p >= '0' && *p <= '9') {
            int e = 0;
            while (*p >= '0' && *p <= '9') {
                if (e < 100000) e = e * 10 + (*p - '0');
                p++;
            }
            exp10 += eneg ? -e : e;
            s = p;
        }
    }
    if (end) *end = (char *)s;
    if (m == 0) return neg ? -0.0 : 0.0;

    double v;
    if (m < (1ULL << 53) && exp10 >= -22 && exp10 <= 22) {
        v = exp10 < 0 ? (double)m / k_exact[-exp10] : (double)m * k_exact[exp10];
    } else if (exp10 > 309) {
        v = HUGE_VAL;
    } else if (exp10 < -343) {
        v = 0.0;
    } else {
        v = scale10(m, exp10);
    }
    if (isinf(v) || (v == 0.0)) errno = ERANGE;
    return neg ? -v : v;
}

float BOX_NAME(strtof)(const char *str, char **end)
{
    const double d = BOX_NAME(strtod)(str, end);
    const float f = (float)d;
    if ((isinf(f) && !isinf(d)) || (f == 0.0f && d != 0.0)) errno = ERANGE;
    return f;
}

double BOX_NAME(atof)(const char *s)
{
    return BOX_NAME(strtod)(s, NULL);
}
