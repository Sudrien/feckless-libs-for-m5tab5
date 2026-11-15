/* SPDX-License-Identifier: MIT
 *
 * sscanf and vsscanf for a boxed app (u-feckless-0048). picolibc's vfscanf
 * is not among the members of libc.a that link into a shared object.
 *
 * C11 7.21.6.2 for a string: white space in the format matches any amount
 * (none included); a literal must match; conversions d i u o x X c s [ n p
 * and the floating a e f g (E F G A), with a width, `*` to skip the
 * assignment, and the lengths hh h l ll j z t. L is refused: long double is
 * a quad on ilp32f, and nothing here makes one. The result is the count of
 * assignments, or EOF if the input ends before the first conversion.
 *
 * Integers go through this component's strtoull/strtoll family for the
 * digits it accepts (the width is honoured by copying at most that many
 * characters first), floating ones through its strtod.
 *
 * Named box_ in the source and given the standard names by BOX_NAME, so the
 * host test can link these beside its own libc.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

unsigned long long BOX_NAME(strtoull)(const char *s, char **end, int base);
long long BOX_NAME(strtoll)(const char *s, char **end, int base);
double BOX_NAME(strtod)(const char *s, char **end);

#define FIELD_MAX 64                            /* a numeric field's characters, at most */

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

enum { L_NONE, L_HH, L_H, L_L, L_LL, L_J, L_Z, L_T, L_LD };

/* Up to width characters of s into buf, so a converter cannot read past
 * the field. */
static void field(const char *s, unsigned width, char *buf)
{
    unsigned n = width && width < FIELD_MAX - 1 ? width : FIELD_MAX - 1;
    unsigned i = 0;
    while (i < n && s[i]) {
        buf[i] = s[i];
        i++;
    }
    buf[i] = '\0';
}

static void store_int(va_list *ap, int len, unsigned long long v)
{
    switch (len) {
    case L_HH: *va_arg(*ap, signed char *) = (signed char)v; break;
    case L_H: *va_arg(*ap, short *) = (short)v; break;
    case L_L: *va_arg(*ap, long *) = (long)v; break;
    case L_LL: *va_arg(*ap, long long *) = (long long)v; break;
    case L_J: *va_arg(*ap, intmax_t *) = (intmax_t)v; break;
    case L_Z: *va_arg(*ap, size_t *) = (size_t)v; break;
    case L_T: *va_arg(*ap, ptrdiff_t *) = (ptrdiff_t)v; break;
    default: *va_arg(*ap, int *) = (int)v; break;
    }
}

/* A scanset at f (after the '['): sets in[256]; returns past the ']'. */
static const char *scanset(const char *f, bool in[256])
{
    bool neg = false;
    if (*f == '^') {
        neg = true;
        f++;
    }
    bool set[256] = { false };
    if (*f == ']') {                            /* a leading ] is a member */
        set[(unsigned char)']'] = true;
        f++;
    }
    while (*f && *f != ']') {
        if (f[1] == '-' && f[2] && f[2] != ']' && (unsigned char)f[2] >= (unsigned char)f[0]) {
            for (unsigned c = (unsigned char)f[0]; c <= (unsigned char)f[2]; c++) set[c] = true;
            f += 3;
        } else {
            set[(unsigned char)*f++] = true;
        }
    }
    for (int c = 0; c < 256; c++) in[c] = neg ? !set[c] : set[c];
    in[0] = false;
    return *f == ']' ? f + 1 : f;
}

int BOX_NAME(vsscanf)(const char *str, const char *f, va_list ap0)
{
    va_list ap;
    va_copy(ap, ap0);
    const char *s = str;
    int count = 0;
    bool any_conv = false;
    while (*f) {
        if (space(*f)) {
            while (space(*f)) f++;
            while (space(*s)) s++;
            continue;
        }
        if (*f != '%' || f[1] == '%') {
            if (*f == '%') {
                f++;
                while (space(*s)) s++;          /* %% skips white space first, as a conversion does */
            }
            if (*s != *f) {
                if (!*s && !any_conv) count = EOF;
                break;
            }
            s++;
            f++;
            continue;
        }
        f++;
        bool skip = false;
        if (*f == '*') {
            skip = true;
            f++;
        }
        unsigned width = 0;
        while (*f >= '0' && *f <= '9') width = width * 10 + (unsigned)(*f++ - '0');
        int len = L_NONE;
        if (f[0] == 'h' && f[1] == 'h') { len = L_HH; f += 2; }
        else if (f[0] == 'l' && f[1] == 'l') { len = L_LL; f += 2; }
        else if (*f == 'h') { len = L_H; f++; }
        else if (*f == 'l') { len = L_L; f++; }
        else if (*f == 'j') { len = L_J; f++; }
        else if (*f == 'z') { len = L_Z; f++; }
        else if (*f == 't') { len = L_T; f++; }
        else if (*f == 'L') { len = L_LD; f++; }
        const char c = *f++;
        if (!c) break;

        if (c == 'n') {
            if (!skip) store_int(&ap, len, (unsigned long long)(s - str));
            continue;
        }
        if (c != 'c' && c != '[') {
            while (space(*s)) s++;
        }
        if (!*s) {
            if (!any_conv) count = EOF;
            break;
        }
        any_conv = true;

        if (c == 'c') {
            const unsigned n = width ? width : 1;
            if (strnlen(s, n) < n) break;
            if (!skip) {
                memcpy(va_arg(ap, char *), s, n);
                count++;
            }
            s += n;
            continue;
        }
        if (c == 's' || c == '[') {
            bool in[256];
            if (c == '[') {
                f = scanset(f, in);
            } else {
                for (int k = 0; k < 256; k++) in[k] = k && !space((char)k);
            }
            const char *b = s;
            while (*s && in[(unsigned char)*s] && (!width || (unsigned)(s - b) < width)) s++;
            if (s == b) break;                  /* a matching failure */
            if (!skip) {
                char *d = va_arg(ap, char *);
                memcpy(d, b, (size_t)(s - b));
                d[s - b] = '\0';
                count++;
            }
            continue;
        }

        char buf[FIELD_MAX], *e;
        field(s, width, buf);
        if (c == 'd' || c == 'i' || c == 'u' || c == 'o' || c == 'x' || c == 'X' || c == 'p') {
            const int base = c == 'd' || c == 'u' ? 10 : c == 'i' ? 0 : c == 'o' ? 8 : 16;
            unsigned long long v;
            if (c == 'd' || c == 'i') v = (unsigned long long)BOX_NAME(strtoll)(buf, &e, base);
            else v = BOX_NAME(strtoull)(buf, &e, base);
            if (e == buf) break;
            s += e - buf;
            if (!skip) {
                if (c == 'p') *va_arg(ap, void **) = (void *)(uintptr_t)v;
                else store_int(&ap, len, v);
                count++;
            }
            continue;
        }
        if (c == 'a' || c == 'e' || c == 'f' || c == 'g' || c == 'A' || c == 'E' || c == 'F' || c == 'G') {
            if (len == L_LD) break;             /* long double is a quad on ilp32f: not taken */
            const double v = BOX_NAME(strtod)(buf, &e);
            if (e == buf) break;
            s += e - buf;
            if (!skip) {
                if (len == L_L) *va_arg(ap, double *) = v;
                else *va_arg(ap, float *) = (float)v;
                count++;
            }
            continue;
        }
        break;                                  /* not a conversion */
    }
    va_end(ap);
    return count;
}

int BOX_NAME(sscanf)(const char *str, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    const int n = BOX_NAME(vsscanf)(str, f, ap);
    va_end(ap);
    return n;
}
