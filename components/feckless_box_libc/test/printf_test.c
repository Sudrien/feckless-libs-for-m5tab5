/* SPDX-License-Identifier: MIT
 *
 * The vendored printf (as box_printf.c builds it, less the aliases, which
 * would collide with the host's) against the host's snprintf, for the
 * conversions an app uses. Where the two may differ by design -- the last
 * digit of a float the vendored one rounds in double -- the case is not here.
 *
 *   make -C components/feckless_box_libc/test
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "printf/printf.h"

void putchar_(char c) { (void)c; }

static int failed, checked;

static void same(const char *fmt, ...)
{
    char a[256], b[256];
    va_list ap;
    va_start(ap, fmt);
    const int na = vsnprintf(a, sizeof a, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    const int nb = vsnprintf_(b, sizeof b, fmt, ap);
    va_end(ap);
    checked++;
    if (na != nb || strcmp(a, b) != 0) {
        failed++;
        printf("FAIL \"%s\": libc \"%s\" (%d), box \"%s\" (%d)\n", fmt, a, na, b, nb);
    }
}

int main(void)
{
    same("plain");
    same("%d %i %u", -42, 17, 4000000000u);
    same("%5d|%-5d|%05d|%+d|% d", 42, 42, 42, 42, 42);
    same("%x %X %#x %o %#o", 0xbeefu, 0xbeefu, 255u, 8u, 8u);
    same("%ld %lu %lld %llu", -1234567L, 1234567UL, -1234567890123LL, 18446744073709551615ULL);
    same("%hd %hhu %zu %td", (short)-3, (unsigned char)250, (size_t)99, (ptrdiff_t)-5);
    same("%s|%10s|%-10s|%.3s", "word", "word", "word", "wordy");
    same("%c%c%c", 'a', 'b', 'c');
    same("%%");
    same("%.2f %.0f %.3f %f", 1.5, 2.5, -0.125, 3.0);
    same("%8.3f|%-8.2f|%08.2f", 3.14159, 2.5, -1.25);
    same("%e %.2e %E", 12345.678, 0.000123, 1e10);
    same("%g %g %g", 100000.0, 1e-5, 123.456);
    same("%*d %-*s %.*f", 6, 42, 5, "ab", 2, 1.005);
    same("%s=%u (%c) %.1f%%", "progress", 3u, 'k', 45.5);
    char small[8];
    const int n1 = snprintf(small, sizeof small, "%s", "truncated text");
    const int n2 = snprintf_(small, sizeof small, "%s", "truncated text");
    checked++;
    if (n1 != n2 || strcmp(small, "truncat") != 0) { failed++; printf("FAIL truncation\n"); }
    printf(failed ? "%d of %d FAILED\n" : "printf: %d formats, all the same as libc\n", failed ? failed : checked, checked);
    return failed != 0;
}
