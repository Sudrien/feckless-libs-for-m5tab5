/* SPDX-License-Identifier: MIT
 *
 * The rest of stdio a boxed app writes and reads with (u-feckless-0048):
 * fputc, fputs, fprintf, vfprintf, fgetc and getc, and stdin, stdout and
 * stderr. Formatting is the app's own (vsnprintf, box_printf.c); the bytes
 * go to the host through fwrite and fread (feckless_box_calls), one call
 * a string rather than a character. picolibc's putchar(c) is the macro
 * fputc(c, stdout).
 *
 * A FILE in a box is a number the host gave; stdin, stdout and stderr are
 * numbers of this file's that the host never gives, and writes to them go
 * to the host's console through putchar, as printf's do. Reading stdin
 * gives EOF.
 *
 * Named box_ in the source and given the standard names by BOX_NAME, so the
 * host test can link these beside its own libc; BOX_HOST(n) names the host
 * calls they make, which the test supplies.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* picolibc's macros for these would turn the definitions below into calls */
#undef putchar
#undef putc
#undef getc
#undef getchar

#ifndef BOX_NAME
#define BOX_NAME(n) n
#define BOX_HOST(n) n
#define BOX_STD(n) n
#endif

#define STDIN_TOKEN  ((FILE *)(uintptr_t)0x53544430u)    /* "STD0" */
#define STDOUT_TOKEN ((FILE *)(uintptr_t)0x53544431u)
#define STDERR_TOKEN ((FILE *)(uintptr_t)0x53544432u)

FILE *BOX_STD(stdin) = STDIN_TOKEN;
FILE *BOX_STD(stdout) = STDOUT_TOKEN;
FILE *BOX_STD(stderr) = STDERR_TOKEN;

/* the host's, through the box's table */
int BOX_HOST(putchar)(int c);
size_t BOX_HOST(fwrite)(const void *p, size_t size, size_t n, FILE *f);
size_t BOX_HOST(fread)(void *p, size_t size, size_t n, FILE *f);
int BOX_NAME(vsnprintf)(char *out, size_t n, const char *f, va_list ap);

static int is_console(FILE *f)
{
    return f == STDOUT_TOKEN || f == STDERR_TOKEN;
}

static size_t put(const char *s, size_t n, FILE *f)
{
    if (f == STDIN_TOKEN) return 0;
    if (!is_console(f)) return BOX_HOST(fwrite)(s, 1, n, f);
    for (size_t i = 0; i < n; i++) {
        if (BOX_HOST(putchar)((unsigned char)s[i]) == EOF) return i;
    }
    return n;
}

int BOX_NAME(fputc)(int c, FILE *f)
{
    const char ch = (char)c;
    return put(&ch, 1, f) == 1 ? (unsigned char)c : EOF;
}

int BOX_NAME(putc)(int c, FILE *f)
{
    return BOX_NAME(fputc)(c, f);
}

int BOX_NAME(fputs)(const char *s, FILE *f)
{
    const size_t n = strlen(s);
    return put(s, n, f) == n ? 0 : EOF;
}

int BOX_NAME(vfprintf)(FILE *f, const char *fmt, va_list ap)
{
    char small[256];
    va_list ap2;
    va_copy(ap2, ap);
    const int n = BOX_NAME(vsnprintf)(small, sizeof small, fmt, ap);
    if (n < 0) {
        va_end(ap2);
        return n;
    }
    const char *s = small;
    char *big = NULL;
    if ((size_t)n >= sizeof small) {            /* too long for the stack: once more, into the heap */
        big = malloc((size_t)n + 1);
        if (!big) {
            va_end(ap2);
            return -1;
        }
        BOX_NAME(vsnprintf)(big, (size_t)n + 1, fmt, ap2);
        s = big;
    }
    va_end(ap2);
    const size_t put_n = put(s, (size_t)n, f);
    free(big);
    return put_n == (size_t)n ? n : -1;
}

int BOX_NAME(fprintf)(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = BOX_NAME(vfprintf)(f, fmt, ap);
    va_end(ap);
    return n;
}

int BOX_NAME(fgetc)(FILE *f)
{
    unsigned char c;
    if (f == STDIN_TOKEN || is_console(f)) return EOF;
    return BOX_HOST(fread)(&c, 1, 1, f) == 1 ? c : EOF;
}

int BOX_NAME(getc)(FILE *f)
{
    return BOX_NAME(fgetc)(f);
}
