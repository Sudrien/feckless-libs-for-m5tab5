/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0048: strtod/strtof, strdup/strndup/strerror/strftime,
 * sscanf, the stdio wrappers and the heap's figures, against the host's
 * libc where it has the same function, and against stand-in host calls
 * where the box's go to the launcher.
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "multi_heap.h"

double box_strtod(const char *, char **);
float box_strtof(const char *, char **);
double box_atof(const char *);
char *box_strdup(const char *);
char *box_stpcpy(char *, const char *);
char *box_strndup(const char *, size_t);
char *box_strerror(int);
size_t box_strftime(char *, size_t, const char *, const struct tm *);
int box_sscanf(const char *, const char *, ...);
int box_fputc(int, FILE *);
int box_fputs(const char *, FILE *);
int box_fprintf(FILE *, const char *, ...);
int box_fgetc(FILE *);
extern FILE *box_stdout, *box_stderr, *box_stdin;
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);
size_t heap_caps_get_minimum_free_size(uint32_t);
size_t heap_caps_get_total_size(uint32_t);
void heap_caps_get_info(multi_heap_info_t *, uint32_t);
int heap_caps_register_failed_alloc_callback(void (*)(size_t, uint32_t, const char *));
void *heap_caps_malloc(size_t, uint32_t);
void *heap_caps_calloc(size_t, size_t, uint32_t);

static int failed, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

/* ---- the host calls box_stdio.c and box_heap.c make --------------------- */
static char console[512];
static size_t console_n;
static char written[4096];
static size_t written_n;
static FILE *const g_file = (FILE *)(uintptr_t)0x46494c03u;    /* a number, as the box gives */
int host_putchar(int c) { if (console_n < sizeof console - 1) console[console_n++] = (char)c; return c; }
size_t host_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    if (f != g_file) return 0;
    memcpy(written + written_n, p, size * n);
    written_n += size * n;
    return n;
}
static const char *g_read = "xy";
size_t host_fread(void *p, size_t size, size_t n, FILE *f)
{
    if (f != g_file || !*g_read) return 0;
    memcpy(p, g_read++, size * n);
    return n;
}
int box_vsnprintf(char *out, size_t n, const char *f, va_list ap) { return vsnprintf(out, n, f, ap); }
typedef struct { uint32_t total, free, largest, min_free, blocks, free_blocks; } stats_t;
int feckless_box_heap_stats(stats_t *out)
{
    *out = (stats_t){ 1000000, 600000, 400000, 350000, 12, 3 };
    return 1;
}

static int ulps(double a, double b)
{
    if (a == b) return 0;
    int64_t x, y;
    memcpy(&x, &a, 8);
    memcpy(&y, &b, 8);
    const int64_t d = x > y ? x - y : y - x;
    return d > 1000 ? 1000 : (int)d;
}

static void test_strtod(void)
{
    const char *exact[] = { "0", "1", "-1", "3.14159", "1e10", "-2.5e-3", "0.1", "123456789012345",
                            "  42.", ".5", "+7e+2", "1.7976931348623157e308", "2.2250738585072014e-308",
                            "4.9e-324", "0x1.8p1", "0X10", "1e-22", "9007199254740993", "-0.0", "6.02214076e23" };
    for (unsigned i = 0; i < sizeof exact / sizeof *exact; i++) {
        char *e1, *e2;
        const double a = box_strtod(exact[i], &e1), b = strtod(exact[i], &e2);
        CHECK(e1 - exact[i] == e2 - exact[i]);
        CHECK(ulps(a, b) <= 1);
        if (ulps(a, b)) printf("  %s: %.17g vs %.17g\n", exact[i], a, b);
    }
    CHECK(box_strtod("0.1", NULL) == 0.1 && box_strtod("-0.0", NULL) == 0 && signbit(box_strtod("-0.0", NULL)));
    /* every value of up to 15 digits and a power of ten up to 22 is exact */
    srand(4);
    int off = 0;
    for (int i = 0; i < 200000; i++) {
        char b[64];
        const long long m = ((long long)rand() << 20 ^ rand()) % 1000000000000000LL;
        const int e = rand() % 45 - 22;
        snprintf(b, sizeof b, "%lldE%d", m, e);
        if (box_strtod(b, NULL) != strtod(b, NULL)) off++;
    }
    CHECK(off == 0);
    int worst = 0;
    for (int i = 0; i < 200000; i++) {
        char b[64];
        snprintf(b, sizeof b, "%.17g", ldexp((double)rand() / RAND_MAX + 0.5, rand() % 2000 - 1000));
        const int u = ulps(box_strtod(b, NULL), strtod(b, NULL));
        if (u > worst) worst = u;
    }
    CHECK(worst <= 3);
    printf("  strtod: 17-digit values within %d ulp of the host's\n", worst);
    char *e;
    CHECK(box_strtod("abc", &e) == 0 && *e == 'a');
    CHECK(box_strtod("  -", &e) == 0 && *e == ' ');
    CHECK(isinf(box_strtod("-Infinity", &e)) && *e == 0 && box_strtod("-inf", NULL) < 0);
    CHECK(isnan(box_strtod("nan(123)x", &e)) && *e == 'x');
    CHECK(box_strtod("0x", &e) == 0 && *e == 'x');
    CHECK(box_strtod("1e", &e) == 1 && *e == 'e');
    errno = 0;
    CHECK(isinf(box_strtod("1e999", NULL)) && errno == ERANGE);
    errno = 0;
    CHECK(box_strtod("1e-999", NULL) == 0 && errno == ERANGE);
    errno = 0;
    CHECK(box_strtod("123.25", NULL) == 123.25 && errno == 0);
    CHECK(box_strtod("1000000000000000000000000000000", NULL) == 1e30);
    CHECK(box_strtof("3.25", NULL) == 3.25f && box_atof("2.5") == 2.5);
    errno = 0;
    CHECK(isinf(box_strtof("1e39", NULL)) && errno == ERANGE);
}

static void test_strings(void)
{
    char *d = box_strdup("hello");
    CHECK(d && !strcmp(d, "hello"));
    free(d);
    d = box_strndup("hello", 3);
    CHECK(d && !strcmp(d, "hel"));
    free(d);
    d = box_strndup("hi", 10);
    CHECK(d && !strcmp(d, "hi"));
    free(d);
    CHECK(!strcmp(box_strerror(ENOENT), "No such file or directory") && !strcmp(box_strerror(12345), "Unknown error"));
    CHECK(!strcmp(box_strerror(0), "Success"));

    /* u-feckless-0058: stpcpy returns the terminator's place */
    char b[16];
    char *e = box_stpcpy(b, "ab");
    CHECK(e == b + 2 && *e == '\0' && !strcmp(b, "ab"));
    e = box_stpcpy(box_stpcpy(e, "cd"), "");
    CHECK(e == b + 4 && !strcmp(b, "abcd"));
    CHECK(box_stpcpy(b, "") == b && b[0] == '\0');

    /* strftime against the host's, in the C locale */
    const char *fmts[] = { "%Y-%m-%d %H.%M.%S", "%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%d %H:%MZ", "%a %A %b %B %d %e %j",
                           "%I %p %y %C %u %w %U %W %V %G %g", "%D %F %R %T %r %%", "x%nx%tx", "%c" };
    int bad = 0;
    srand(9);
    for (int i = 0; i < 20000; i++) {
        const time_t t = (time_t)(rand() % 2000000000) + (time_t)(rand() % 3) * 1000000000;
        struct tm tm;
        gmtime_r(&t, &tm);
        for (unsigned k = 0; k < sizeof fmts / sizeof *fmts; k++) {
            char a[128], b[128];
            const size_t na = box_strftime(a, sizeof a, fmts[k], &tm), nb = strftime(b, sizeof b, fmts[k], &tm);
            if (na != nb || strcmp(a, b)) {
                if (bad < 5) printf("  strftime %s: \"%s\" vs \"%s\"\n", fmts[k], a, b);
                bad++;
            }
        }
    }
    CHECK(bad == 0);
    struct tm tm = { .tm_year = 126, .tm_mon = 10, .tm_mday = 15, .tm_hour = 9 };
    char small[8];
    CHECK(box_strftime(small, sizeof small, "%Y-%m-%d", &tm) == 0 && small[0] == 0);   /* does not fit */
    CHECK(box_strftime(small, sizeof small, "%Y", &tm) == 4 && !strcmp(small, "2026"));
    CHECK(box_strftime(small, 0, "%Y", &tm) == 0);
    char z[32];
    CHECK(box_strftime(z, sizeof z, "%z %Z %Ey %Od", &tm) > 0 && !strcmp(z, "+0000 UTC 26 15"));
}

static void test_sscanf(void)
{
    /* Defeatist's three, against the host's */
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, hy = 0, hmo = 0, hd = 0, hh = 0, hmi = 0, hs = 0;
    CHECK(box_sscanf("2026-11-15 09.04.33.flac", "%4d-%2d-%2d %2d.%2d.%2d", &y, &mo, &d, &h, &mi, &s) == 6);
    CHECK(sscanf("2026-11-15 09.04.33.flac", "%4d-%2d-%2d %2d.%2d.%2d", &hy, &hmo, &hd, &hh, &hmi, &hs) == 6);
    CHECK(y == hy && mo == hmo && d == hd && h == hh && mi == hmi && s == hs && y == 2026 && s == 33);
    char mon[4], hmon[4];
    CHECK(box_sscanf("Nov 15 2026", "%3s %2d %4d", mon, &d, &y) == 3 && !strcmp(mon, "Nov") && d == 15 && y == 2026);
    CHECK(sscanf("Nov 15 2026", "%3s %2d %4d", hmon, &hd, &hy) == 3 && !strcmp(mon, hmon));
    CHECK(box_sscanf("12:34:56", "%2d:%2d:%2d", &h, &mi, &s) == 3 && h == 12 && mi == 34 && s == 56);
    CHECK(box_sscanf("123456", "%2d%2d%2d", &h, &mi, &s) == 3 && h == 12 && mi == 34 && s == 56);
    /* the rest of the language */
    unsigned u = 0; long l = 0; long long ll = 0; short sh = 0; signed char hc = 0; unsigned x = 0; int n = -1;
    float f = 0; double df = 0; char c3[3] = { 0 }, word[16], set[16];
    CHECK(box_sscanf("  -17 4000000000 0x1F", "%ld %u %i", &l, &u, &d) == 3 && l == -17 && u == 4000000000u && d == 31);
    CHECK(box_sscanf("077 ff 9223372036854775807", "%o %x %lld", &u, &x, &ll) == 3 && u == 63 && x == 255 && ll == INT64_MAX);
    CHECK(box_sscanf("300 -2", "%hd %hhd", &sh, &hc) == 2 && sh == 300 && hc == -2);
    CHECK(box_sscanf("3.5 -1e-3", "%f %lf", &f, &df) == 2 && f == 3.5f && df == -1e-3);
    CHECK(box_sscanf("abcdef", "%2c%n", c3, &n) == 1 && !memcmp(c3, "ab", 2) && n == 2);
    CHECK(box_sscanf("key=value;rest", "%15[^=]=%15[a-z]", word, set) == 2 && !strcmp(word, "key") && !strcmp(set, "value"));
    CHECK(box_sscanf("]x", "%[]x]", set) == 1 && !strcmp(set, "]x"));
    CHECK(box_sscanf("10 20", "%*d %d", &d) == 1 && d == 20);
    CHECK(box_sscanf("100%", "%d%%", &d) == 1 && d == 100);
    CHECK(box_sscanf("", "%d", &d) == EOF && sscanf("", "%d", &d) == EOF);
    CHECK(box_sscanf("   ", "%d", &d) == EOF);
    CHECK(box_sscanf("abc", "%d", &d) == 0 && sscanf("abc", "%d", &d) == 0);
    CHECK(box_sscanf("12 abc", "%d %d", &h, &mi) == 1);
    CHECK(box_sscanf("x", "y%d", &d) == 0 && box_sscanf("", "y%d", &d) == EOF);
    CHECK(box_sscanf("1.5", "%Lf", &df) == 0);                       /* long double: refused */
    char longw[8];
    CHECK(box_sscanf("abcdefghij", "%7s", longw) == 1 && !strcmp(longw, "abcdefg"));
}

static void test_stdio(void)
{
    console_n = written_n = 0;
    CHECK(box_fputc('A', box_stdout) == 'A' && box_fputs("bc", box_stderr) == 0);
    CHECK(box_fprintf(box_stdout, "[%d]", 42) == 4);
    console[console_n] = 0;
    CHECK(!strcmp(console, "Abc[42]"));
    CHECK(box_fputc('z', g_file) == 'z' && box_fputs("hello", g_file) == 0);
    char big[600];
    memset(big, 'q', sizeof big - 1);
    big[sizeof big - 1] = 0;
    CHECK(box_fprintf(g_file, "%s!", big) == 600);                   /* past the 256 on the stack */
    CHECK(written_n == 606 && written[0] == 'z' && !memcmp(written + 1, "hello", 5) && written[605] == '!');
    CHECK(box_fputc('x', box_stdin) == EOF && box_fputc('x', (FILE *)(uintptr_t)0x1234) == EOF);
    CHECK(box_fgetc(g_file) == 'x' && box_fgetc(g_file) == 'y' && box_fgetc(g_file) == EOF);
    CHECK(box_fgetc(box_stdin) == EOF && box_fgetc(box_stdout) == EOF);
}

static size_t s_failed_size;
static const char *s_failed_fn;
static void on_failed(size_t size, uint32_t caps, const char *fn) { (void)caps; s_failed_size = size; s_failed_fn = fn; }

static void test_heap(void)
{
    CHECK(heap_caps_get_free_size(0) == 600000 && heap_caps_get_largest_free_block(1 << 11) == 400000);
    CHECK(heap_caps_get_minimum_free_size(0) == 350000 && heap_caps_get_total_size(0) == 1000000);
    multi_heap_info_t i;
    heap_caps_get_info(&i, 0);
    CHECK(i.total_free_bytes == 600000 && i.total_allocated_bytes == 400000 && i.largest_free_block == 400000);
    CHECK(i.minimum_free_bytes == 350000 && i.allocated_blocks == 12 && i.free_blocks == 3 && i.total_blocks == 15);
    CHECK(heap_caps_register_failed_alloc_callback(NULL) != 0);
    CHECK(heap_caps_register_failed_alloc_callback(on_failed) == 0);
    void *p = heap_caps_malloc(16, 0);
    CHECK(p && s_failed_size == 0);
    free(p);
    CHECK(heap_caps_malloc((size_t)1 << 62, 0) == NULL && s_failed_size == (size_t)1 << 62);
    CHECK(s_failed_fn && !strcmp(s_failed_fn, "heap_caps_malloc"));
}

/* u-feckless-0065: feckless_box_checkin(), one host call per 16 K of work */
static int s_ticks;
uint32_t xTaskGetTickCount(void) { return (uint32_t)++s_ticks; }
void feckless_box_checkin(uint32_t work);

static void test_checkin(void)
{
    s_ticks = 0;
    for (int i = 0; i < 16383; i++) feckless_box_checkin(1);
    CHECK(s_ticks == 0);
    feckless_box_checkin(1);
    CHECK(s_ticks == 1);                        /* the 16384th unit */
    feckless_box_checkin(0);
    CHECK(s_ticks == 1);
    feckless_box_checkin(1000000);
    CHECK(s_ticks == 2);                        /* a big step is one call, not sixty */
    for (int i = 0; i < 64; i++) feckless_box_checkin(1024);
    CHECK(s_ticks == 6);
}

/* u-feckless-0075: the callback loop. The host's two calls, recorded: the
 * constructor's announcement before main, and the loop's asks. Calls into
 * u.fn are not made here -- a 32-bit address of a 64-bit host's function --
 * the loop is checked to hand each answer back and to end on 0. */
typedef struct { uint32_t fn, a[4]; } up_t;
void feckless_box_upcall_loop(void *arg);
static void (*s_announced)(void *);
static int s_announces, s_asks;
static uint32_t s_last_ret = 0xdead;
void feckless_box_upcall_loop_is(void (*loop)(void *)) { s_announced = loop; s_announces++; }
uint32_t feckless_box_upcall_next(up_t *u, uint32_t ret)
{
    s_asks++;
    s_last_ret = ret;
    CHECK(u != NULL);
    return 0;                               /* the app is stopping */
}

static void test_upcall(void)
{
    CHECK(s_announces == 1 && s_announced == feckless_box_upcall_loop);   /* before main, once */
    feckless_box_upcall_loop(NULL);
    CHECK(s_asks == 1 && s_last_ret == 0);  /* asked once, with nothing to answer; ended on 0 */
}

int main(void)
{
    test_strtod();
    test_strings();
    test_sscanf();
    test_stdio();
    test_heap();
    test_checkin();
    test_upcall();
    printf(failed ? "libc48: %d FAILED\n" : "libc48: all passed (%d checks)\n", failed ? failed : checks);
    return failed != 0;
}
