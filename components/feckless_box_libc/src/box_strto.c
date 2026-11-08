/* SPDX-License-Identifier: MIT
 *
 * strtol and its family, for a boxed app. picolibc's set errno as a
 * thread-local, which cannot be in a shared object; these set it through
 * __errno() (box_errno.c). As C11 7.22.1.4: leading space, a sign, a 0x/0X
 * prefix for base 16 or 0, a leading 0 for octal under base 0; on overflow
 * the limit and ERANGE; with no digits, 0 and *end = the string.
 *
 * Named box_ in the source and given the standard names by the macros at the
 * bottom, so the host test can link these beside its own libc.
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

static int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

static bool space(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/* The magnitude, saturated at max; *neg, *any and *over as found. */
static unsigned long long parse(const char *s, char **end, int base, unsigned long long max,
                                bool *neg, bool *over)
{
    const char *p = s;
    *neg = false;
    *over = false;
    if (base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (end) *end = (char *)s;
        return 0;
    }
    while (space(*p)) p++;
    if (*p == '+' || *p == '-') {
        *neg = *p == '-';
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit(p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    const char *first = p;
    unsigned long long v = 0;
    for (int d; (d = digit(*p)) < base; p++) {
        if (*over) continue;
        if (v > (max - (unsigned)d) / (unsigned)base) {
            *over = true;
            v = max;
        } else {
            v = v * (unsigned)base + (unsigned)d;
        }
    }
    if (p == first) {               /* no digits: nothing was converted */
        *neg = false;
        if (end) *end = (char *)s;
        return 0;
    }
    if (end) *end = (char *)p;
    return v;
}

unsigned long long BOX_NAME(strtoull)(const char *s, char **end, int base)
{
    bool neg, over;
    unsigned long long v = parse(s, end, base, ULLONG_MAX, &neg, &over);
    if (over) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return neg ? -v : v;
}

unsigned long BOX_NAME(strtoul)(const char *s, char **end, int base)
{
    bool neg, over;
    unsigned long long v = parse(s, end, base, ULONG_MAX, &neg, &over);
    if (over) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    return neg ? -(unsigned long)v : (unsigned long)v;
}

long long BOX_NAME(strtoll)(const char *s, char **end, int base)
{
    bool neg, over;
    /* The magnitude of LLONG_MIN is one more than LLONG_MAX. */
    unsigned long long v = parse(s, end, base, (unsigned long long)LLONG_MAX + 1, &neg, &over);
    if (over || (!neg && v > (unsigned long long)LLONG_MAX)) {
        errno = ERANGE;
        return neg ? LLONG_MIN : LLONG_MAX;
    }
    if (neg) {
        return v == (unsigned long long)LLONG_MAX + 1 ? LLONG_MIN : -(long long)v;
    }
    return (long long)v;
}

long BOX_NAME(strtol)(const char *s, char **end, int base)
{
    bool neg, over;
    unsigned long long v = parse(s, end, base, (unsigned long long)LONG_MAX + 1, &neg, &over);
    if (over || (!neg && v > (unsigned long long)LONG_MAX)) {
        errno = ERANGE;
        return neg ? LONG_MIN : LONG_MAX;
    }
    if (neg) {
        return v == (unsigned long long)LONG_MAX + 1 ? LONG_MIN : -(long)v;
    }
    return (long)v;
}

int BOX_NAME(atoi)(const char *s)
{
    return (int)BOX_NAME(strtol)(s, NULL, 10);
}

long BOX_NAME(atol)(const char *s)
{
    return BOX_NAME(strtol)(s, NULL, 10);
}
