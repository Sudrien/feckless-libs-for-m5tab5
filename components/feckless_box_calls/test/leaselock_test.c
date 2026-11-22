/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0072: the card's lease and the app's lock, with real threads.
 *
 * Built with FAKE_REAL_LOCKS: the storage handler's lease is a real one
 * (fake_storage.c: one owner, nested by the same thread, others wait, and
 * storage_io_fread() leases each read as the real one does), the app's
 * lock is a real recursive mutex, and calls go through a dispatcher that
 * takes that lock for every call not flagged UNLOCKED -- feckless_umode's
 * rule. Then what the boxed player did on the board, when Defeatist froze
 * with only its unlocked audio writer still running:
 *
 *   thread A  storage_io_acquire()       -- holds the lease across calls,
 *             ...                            as an app's sidecar write does
 *             fopen()                     -- locked: needs the app's lock
 *   thread B  storage_io_fread()          -- locked, and waits for the lease
 *
 * B holds the lock waiting for A's lease; A waits for B's lock. Each case
 * runs on a watchdog: a thread still in its calls after five seconds is a
 * deadlock, reported, and the test exits rather than hangs.
 *
 *   make -C components/feckless_box_calls/test run-leaselock
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "feckless_box_calls.h"
#include "feckless_umode_load.h"
#include "storage_io.h"

extern int lease_waits;                     /* fake_storage.c: a thread found it held */
extern size_t io_counted;                   /* storage_io_count()'s bytes */

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define MEM (256 * 1024)
static uint8_t *mem;
static feckless_box_t box;
static feckless_umode_calls_t tab;
static const feckless_umode_call_t *calls_tab;
static uint32_t ncalls;
static char path[96];

/* ---- the app's lock, as feckless_umode's dispatcher holds it ----------- */

static pthread_mutex_t s_app;

void feckless_umode_app_lock(feckless_umode_app_t *a) { (void)a; pthread_mutex_lock(&s_app); }
void feckless_umode_app_unlock(feckless_umode_app_t *a) { (void)a; pthread_mutex_unlock(&s_app); }

static uint32_t idx_of(const char *name)
{
    for (uint32_t i = 0; i < ncalls; i++)
        if (!strcmp(calls_tab[i].name, name)) return i;
    printf("FAIL no call %s\n", name);
    exit(1);
}

/* One call on this thread's own window, locked unless flagged otherwise. */
static uint32_t dcall(feckless_umode_window_t *w, const char *name, uint32_t a0, uint32_t a1,
                      uint32_t a2, uint32_t a3)
{
    const uint32_t i = idx_of(name);
    w->ctx.x[10] = a0; w->ctx.x[11] = a1; w->ctx.x[12] = a2; w->ctx.x[13] = a3;
    w->ctx.x[14] = 0;  w->ctx.x[15] = 0;
    uint32_t ret = 0xdeadbeef;
    const bool locked = !(calls_tab[i].flags & FECKLESS_UMODE_CALL_UNLOCKED);
    if (locked) pthread_mutex_lock(&s_app);
    const bool ok = feckless_umode_calls_ext(w, FECKLESS_SYS_EXT_BASE + i, &w->ctx.x[10], &ret);
    if (locked) pthread_mutex_unlock(&s_app);
    CHECK(ok);
    return ret;
}

static void window(feckless_umode_window_t *w)
{
    memset(w, 0, sizeof *w);
    w->lo = (uintptr_t)mem;
    w->hi = (uintptr_t)mem + MEM;
    w->ext_arg = &tab;
}

static uint32_t P(const void *p) { return (uint32_t)(uintptr_t)p; }
static void *bx(uint32_t n) { void *p = (void *)(uintptr_t)feckless_box_alloc(&box, n); memset(p, 0, n); return p; }
static char *bstr(const char *s) { char *p = bx((uint32_t)strlen(s) + 1); strcpy(p, s); return p; }

static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

/* ---- the two threads ---------------------------------------------------- */

static volatile int a_has_lease, a_done, b_done, b_got;
static uint32_t b_tok, b_dst, b_len;
static int b_how;                           /* 0 storage_io_fread, 1 storage_io_read_at */

static void *thread_a(void *arg)
{
    (void)arg;
    feckless_umode_window_t w;
    window(&w);
    dcall(&w, "storage_io_acquire", STORAGE_IO_BACKGROUND, 0, 0, 0);
    a_has_lease = 1;
    sleep_ms(100);                          /* B is waiting for the lease by now */
    const uint32_t f = dcall(&w, "fopen", P(bstr(path)), P(bstr("rb")), 0, 0);
    if (f) dcall(&w, "fclose", f, 0, 0, 0);
    dcall(&w, "storage_io_release", 0, 0, 0, 0);
    a_done = 1;
    return NULL;
}

static void *thread_b(void *arg)
{
    (void)arg;
    feckless_umode_window_t w;
    window(&w);
    while (!a_has_lease) sleep_ms(1);
    if (b_how == 0) {
        b_got = (int)dcall(&w, "storage_io_fread", b_dst, b_len, b_tok, STORAGE_IO_PLAYBACK);
    } else {
        w.ctx.x[14] = b_len;                /* (f, off lo, off hi, dst, len, class) */
        w.ctx.x[15] = STORAGE_IO_PLAYBACK;
        const uint32_t i = idx_of("storage_io_read_at");
        w.ctx.x[10] = b_tok; w.ctx.x[11] = 3; w.ctx.x[12] = 0; w.ctx.x[13] = b_dst;
        uint32_t ret = 0;
        pthread_mutex_lock(&s_app);
        CHECK(feckless_umode_calls_ext(&w, FECKLESS_SYS_EXT_BASE + i, &w.ctx.x[10], &ret));
        pthread_mutex_unlock(&s_app);
        b_got = (int)ret;
    }
    b_done = 1;
    return NULL;
}

static void one_case(const char *what, int how)
{
    feckless_umode_window_t w;
    window(&w);
    b_tok = dcall(&w, "storage_io_open", P(bstr(path)), P(bstr("rb")), 0, 0);
    CHECK(b_tok != 0);
    b_len = 40000;                          /* five slices of the box's 8 KB */
    b_dst = P(bx(b_len));
    b_how = how;
    a_has_lease = a_done = b_done = 0;
    b_got = -1;
    const int waits = lease_waits;
    const size_t counted = io_counted;

    pthread_t ta, tb;
    pthread_create(&ta, NULL, thread_a, NULL);
    pthread_create(&tb, NULL, thread_b, NULL);
    for (int i = 0; i < 500 && !(a_done && b_done); i++) sleep_ms(10);
    if (!(a_done && b_done)) {
        printf("FAIL %s: deadlock -- after 5 s, thread A %s, thread B %s\n", what,
               a_done ? "done" : "stuck (holds the lease, wants the app's lock)",
               b_done ? "done" : "stuck (holds the app's lock, wants the lease)");
        printf("leaselock_test: FAILED (deadlocked; exiting rather than hanging)\n");
        exit(1);
    }
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK(lease_waits > waits);             /* B really did wait for A's lease */
    const uint8_t *d = (const uint8_t *)(uintptr_t)b_dst;
    const uint32_t from = how ? 3 : 0;
    bool right = true;
    for (uint32_t i = 0; i < b_len; i++) if (d[i] != (uint8_t)('a' + (i + from) % 26)) { right = false; break; }
    CHECK(how == 0 ? b_got == (int)b_len : b_got == 1);
    CHECK(right);
    CHECK(io_counted - counted == b_len);   /* the bytes still reach the arbiter's count */
    dcall(&w, "storage_io_close", b_tok, 0, 0, 0);
    printf("  %s: no deadlock; %u bytes read after waiting for the lease\n", what, (unsigned)b_len);
}

int main(void)
{
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_app, &at);

    mem = mmap(NULL, MEM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED) { printf("no memory below 4 GB\n"); return 1; }
    feckless_box_init(&box, (uintptr_t)mem, MEM, 4096);
    calls_tab = feckless_box_calls_v1(&ncalls);
    tab = (feckless_umode_calls_t){ calls_tab, ncalls, &box };

    snprintf(path, sizeof path, "/tmp/boxlease.%d", (int)getpid());
    FILE *f = fopen(path, "wb");
    for (int i = 0; i < 100000; i++) fputc('a' + i % 26, f);
    fclose(f);

    one_case("lease held across calls vs storage_io_fread", 0);
    one_case("lease held across calls vs storage_io_read_at", 1);

    remove(path);
    if (failed) { printf("leaselock_test: %d FAILED\n", failed); return 1; }
    printf("leaselock_test: all passed\n");
    return 0;
}
