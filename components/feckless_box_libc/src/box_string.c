/* SPDX-License-Identifier: MIT
 *
 * strdup, strndup, strerror and strftime for a boxed app (u-feckless-0048):
 * pure functions, so the app's own, as strcpy is. picolibc's either reach
 * absolute tables (strftime's locale, strerror's messages) or are not among
 * the members of libc.a that link into a shared object.
 *
 * strftime is C11's conversions in the C locale, with %E and %O taken as
 * the plain ones; %Z is "UTC" and %z "+0000", since a box has no timezone
 * of its own. strerror knows the errno values a file, a socket or a
 * conversion gives, and says "Unknown error" for the rest.
 *
 * Named box_ in the source and given the standard names by BOX_NAME, so the
 * host test can link these beside its own libc.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BOX_NAME
#define BOX_NAME(n) n
#endif

char *BOX_NAME(strdup)(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

char *BOX_NAME(strndup)(const char *s, size_t max)
{
    const size_t n = strnlen(s, max);
    char *d = malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

char *BOX_NAME(strerror)(int e)
{
    switch (e) {
    case 0: return (char *)"Success";
    case EPERM: return (char *)"Not owner";
    case ENOENT: return (char *)"No such file or directory";
    case EINTR: return (char *)"Interrupted system call";
    case EIO: return (char *)"I/O error";
    case ENXIO: return (char *)"No such device or address";
    case EBADF: return (char *)"Bad file number";
    case EAGAIN: return (char *)"No more processes";
    case ENOMEM: return (char *)"Not enough space";
    case EACCES: return (char *)"Permission denied";
    case EFAULT: return (char *)"Bad address";
    case EBUSY: return (char *)"Device or resource busy";
    case EEXIST: return (char *)"File exists";
    case ENODEV: return (char *)"No such device";
    case ENOTDIR: return (char *)"Not a directory";
    case EISDIR: return (char *)"Is a directory";
    case EINVAL: return (char *)"Invalid argument";
    case ENFILE: return (char *)"Too many open files in system";
    case EMFILE: return (char *)"File descriptor value too large";
    case EFBIG: return (char *)"File too large";
    case ENOSPC: return (char *)"No space left on device";
    case ESPIPE: return (char *)"Illegal seek";
    case EROFS: return (char *)"Read-only file system";
    case EPIPE: return (char *)"Broken pipe";
    case EDOM: return (char *)"Mathematics argument out of domain of function";
    case ERANGE: return (char *)"Result too large";
    case ENAMETOOLONG: return (char *)"File or path name too long";
    case ENOSYS: return (char *)"Function not implemented";
    case ENOTEMPTY: return (char *)"Directory not empty";
    case ETIMEDOUT: return (char *)"Connection timed out";
    case ECONNREFUSED: return (char *)"Connection refused";
    case ECONNRESET: return (char *)"Connection reset by peer";
    case EHOSTUNREACH: return (char *)"Host is unreachable";
    case ENOTSUP: return (char *)"Not supported";
    default: return (char *)"Unknown error";
    }
}

static const char *const k_wday[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const k_mon[12] = { "January", "February", "March", "April", "May", "June", "July",
                                        "August", "September", "October", "November", "December" };

typedef struct {
    char *out;
    size_t cap, n;
    bool full;
} sink_t;

static void put(sink_t *k, const char *s, size_t len)
{
    if (k->n + len >= k->cap) {
        k->full = true;
        return;
    }
    memcpy(k->out + k->n, s, len);
    k->n += len;
}

static void put_s(sink_t *k, const char *s) { put(k, s, strlen(s)); }

/* v in at least w digits, zero- or space-padded */
static void put_num(sink_t *k, long v, int w, char pad)
{
    char b[24];
    int i = (int)sizeof b;
    const bool neg = v < 0;
    unsigned long u = neg ? 0ul - (unsigned long)v : (unsigned long)v;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while ((int)sizeof b - i < w && i > 1) b[--i] = pad;
    if (neg) b[--i] = '-';
    put(k, b + i, sizeof b - (size_t)i);
}

static bool leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* ISO 8601 week-based year and week, as C11 7.27.3.5's %G and %V */
static void iso_week(const struct tm *t, long *year, int *week)
{
    const long y = (long)t->tm_year + 1900;
    const int wd = (t->tm_wday + 6) % 7;                    /* Monday 0 */
    int w = (t->tm_yday - wd + 10) / 7;
    *year = y;
    if (w < 1) {
        *year = y - 1;
        const int pyd = t->tm_yday + (leap(y - 1) ? 366 : 365);
        w = (pyd - wd + 10) / 7;
    } else if (w == 53) {
        const int days = leap(y) ? 366 : 365;
        if (t->tm_yday - wd + 3 >= days) {                 /* that Thursday is next year's */
            *year = y + 1;
            w = 1;
        }
    }
    *week = w;
}

static void fmt(sink_t *k, const char *f, const struct tm *t);

static void conv(sink_t *k, char c, const struct tm *t)
{
    const int wd = t->tm_wday >= 0 && t->tm_wday < 7 ? t->tm_wday : 0;
    const int mo = t->tm_mon >= 0 && t->tm_mon < 12 ? t->tm_mon : 0;
    long gy;
    int gw;
    switch (c) {
    case 'a': put(k, k_wday[wd], 3); break;
    case 'A': put_s(k, k_wday[wd]); break;
    case 'b': case 'h': put(k, k_mon[mo], 3); break;
    case 'B': put_s(k, k_mon[mo]); break;
    case 'c': fmt(k, "%a %b %e %H:%M:%S %Y", t); break;
    case 'C': put_num(k, ((long)t->tm_year + 1900) / 100, 2, '0'); break;
    case 'd': put_num(k, t->tm_mday, 2, '0'); break;
    case 'D': fmt(k, "%m/%d/%y", t); break;
    case 'e': put_num(k, t->tm_mday, 2, ' '); break;
    case 'F': fmt(k, "%Y-%m-%d", t); break;
    case 'g': iso_week(t, &gy, &gw); put_num(k, ((gy % 100) + 100) % 100, 2, '0'); break;
    case 'G': iso_week(t, &gy, &gw); put_num(k, gy, 1, '0'); break;
    case 'H': put_num(k, t->tm_hour, 2, '0'); break;
    case 'I': put_num(k, t->tm_hour % 12 ? t->tm_hour % 12 : 12, 2, '0'); break;
    case 'j': put_num(k, t->tm_yday + 1, 3, '0'); break;
    case 'm': put_num(k, t->tm_mon + 1, 2, '0'); break;
    case 'M': put_num(k, t->tm_min, 2, '0'); break;
    case 'n': put(k, "\n", 1); break;
    case 'p': put_s(k, t->tm_hour < 12 ? "AM" : "PM"); break;
    case 'r': fmt(k, "%I:%M:%S %p", t); break;
    case 'R': fmt(k, "%H:%M", t); break;
    case 'S': put_num(k, t->tm_sec, 2, '0'); break;
    case 't': put(k, "\t", 1); break;
    case 'T': fmt(k, "%H:%M:%S", t); break;
    case 'u': put_num(k, wd ? wd : 7, 1, '0'); break;
    case 'U': put_num(k, (t->tm_yday + 7 - wd) / 7, 2, '0'); break;
    case 'V': iso_week(t, &gy, &gw); put_num(k, gw, 2, '0'); break;
    case 'w': put_num(k, wd, 1, '0'); break;
    case 'W': put_num(k, (t->tm_yday + 7 - (wd + 6) % 7) / 7, 2, '0'); break;
    case 'x': fmt(k, "%m/%d/%y", t); break;
    case 'X': fmt(k, "%H:%M:%S", t); break;
    case 'y': put_num(k, ((((long)t->tm_year + 1900) % 100) + 100) % 100, 2, '0'); break;
    case 'Y': put_num(k, (long)t->tm_year + 1900, 1, '0'); break;
    case 'z': put_s(k, "+0000"); break;
    case 'Z': put_s(k, "UTC"); break;
    case '%': put(k, "%", 1); break;
    default: {                                  /* not a conversion: as written */
        const char b[2] = { '%', c };
        put(k, b, c ? 2 : 1);
    }
    }
}

static void fmt(sink_t *k, const char *f, const struct tm *t)
{
    for (; *f && !k->full; f++) {
        if (*f != '%') {
            put(k, f, 1);
            continue;
        }
        f++;
        if (*f == 'E' || *f == 'O') f++;        /* the C locale's alternatives are the plain ones */
        conv(k, *f, t);
        if (!*f) break;
    }
}

size_t BOX_NAME(strftime)(char *out, size_t max, const char *f, const struct tm *t)
{
    if (max == 0) return 0;
    sink_t k = { out, max, 0, false };
    fmt(&k, f, t);
    if (k.full) {
        out[0] = '\0';
        return 0;
    }
    out[k.n] = '\0';
    return k.n;
}
