/* SPDX-License-Identifier: MIT
 *
 * box_strto.c against the host's libc: the same value, the same end, the
 * same errno, for every input and base below, and for a random sweep. The
 * widths are the host's (long is 64-bit here, 32 on the target); the code
 * takes its limits from <limits.h>, so this checks the same logic.
 *
 *   make -C components/feckless_box_libc/test
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOX_NAME(n) box_##n
long box_strtol(const char *, char **, int);
unsigned long box_strtoul(const char *, char **, int);
long long box_strtoll(const char *, char **, int);
unsigned long long box_strtoull(const char *, char **, int);
int box_atoi(const char *);
long box_atol(const char *);

static int failed, checked;

#define SAME(fn, s, base) do {                                                    \
    char *e1, *e2;                                                                \
    errno = 0; __typeof__(fn(s, &e1, base)) a = fn(s, &e1, base); int x1 = errno; \
    errno = 0; __typeof__(a) b = box_##fn(s, &e2, base); int x2 = errno;          \
    checked++;                                                                    \
    /* An invalid base is unspecified by C; glibc leaves *end unset there. */  \
    const int valid = (base) == 0 || ((base) >= 2 && (base) <= 36);             \
    if (a != b || (valid && e1 != e2) || x1 != x2) {                              \
        failed++;                                                                 \
        if (failed < 20) printf("FAIL %s(\"%s\", %d): libc %lld end+%td errno %d, box %lld end+%td errno %d\n", \
               #fn, s, base, (long long)a, e1 - (s), x1, (long long)b, e2 - (s), x2); \
    }                                                                             \
} while (0)

static void all(const char *s, int base)
{
    SAME(strtol, s, base);
    SAME(strtoul, s, base);
    SAME(strtoll, s, base);
    SAME(strtoull, s, base);
}

int main(void)
{
    static const char *in[] = {
        "", " ", "0", "-0", "+0", "1", "-1", "+1", "42", "  \t\n42xyz", "-42", "007", "08", "0x", "0x1f", "0X1F",
        "0xg", "-0x10", "+0x10", "0b101", "z", "Z", "zz", "  -", "+", "-", "--1", "+-1", "1e5", "12 34",
        "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295", "4294967296", "-4294967295",
        "9223372036854775807", "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
        "18446744073709551615", "18446744073709551616", "-18446744073709551615", "-18446744073709551616",
        "99999999999999999999999999999", "-99999999999999999999999999999", "0x7fffffffffffffff",
        "0x8000000000000000", "0xffffffffffffffff", "0x10000000000000000", "777777777777777777777", "1777777777777777777777",
        "0000000000000000000000000000001", "\v\f\r 7",
    };
    static const int bases[] = { 0, 2, 8, 10, 16, 36, 1, 37, -1 };
    for (size_t i = 0; i < sizeof in / sizeof in[0]; i++)
        for (size_t j = 0; j < sizeof bases / sizeof bases[0]; j++)
            all(in[i], bases[j]);

    /* random strings from an alphabet that hits every path */
    static const char alpha[] = " \t+-0123456789abcdefxyzXZ";
    char s[40];
    srand(7);
    for (int k = 0; k < 200000; k++) {
        const int n = rand() % 24;
        for (int i = 0; i < n; i++) s[i] = alpha[rand() % (int)(sizeof alpha - 1)];
        s[n] = '\0';
        all(s, (int[]){ 0, 2, 8, 10, 16, 36 }[rand() % 6]);
    }

    if (atoi("  -17x") != box_atoi("  -17x") || atol("123456") != box_atol("123456")) { failed++; printf("FAIL atoi/atol\n"); }
    printf(failed ? "%d of %d FAILED\n" : "box_strto: %d comparisons, all the same as libc\n", failed ? failed : checked, checked);
    return failed != 0;
}
