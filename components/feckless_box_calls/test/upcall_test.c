/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0075: callbacks into the app, with real threads.
 *
 * Built with FAKE_REAL_LOCKS and FAKE_REAL_SEMS: the app's lock is a real
 * recursive mutex, taken for every call not flagged UNLOCKED, and the
 * binary semaphores and critical sections box_upcall.c uses really wait.
 * One thread plays feckless_box_libc's loop -- asks
 * feckless_box_upcall_next() for work, "calls" the address it is given
 * (a box address the test maps to a C function), hands back the answer --
 * and others play the host tasks that ask for callbacks. Every wait has a
 * watchdog: five seconds without an answer is a hang, reported, and the
 * test exits.
 *
 *   make -C components/feckless_box_calls/test run-upcall
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "esp_err.h"
#include "feckless_box_calls.h"
#include "feckless_umode_load.h"
#include "../src/box_calls_priv.h"

extern bool fake_stopping;
extern uint32_t s_fn, s_stack, s_calls;    /* fake_audio.c: the last spawn */
extern int s_prio, s_core;

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define MEM (256 * 1024)
static uint8_t *mem;
static feckless_box_t box;
static feckless_umode_calls_t tab;
static const feckless_umode_call_t *calls_tab;
static uint32_t ncalls;

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

static uint32_t dcall(feckless_umode_window_t *w, const char *name, uint32_t a0, uint32_t a1)
{
    const uint32_t i = idx_of(name);
    w->ctx.x[10] = a0; w->ctx.x[11] = a1;
    for (int r = 12; r <= 15; r++) w->ctx.x[r] = 0;
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
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- the app's side: its loop, and the functions it is asked to call ---- */

#define F_LOOP   (P(mem) + 64)
#define F_ADD    (P(mem) + 128)
#define F_SLOW   (P(mem) + 160)
#define F_AGAIN  (P(mem) + 192)             /* asks for a callback itself */
#define F_LOCKED (P(mem) + 224)             /* makes a locked host call */

typedef struct { uint32_t fn, a[4]; } up_t;
static volatile int loop_ended, loop_calls;
static feckless_umode_window_t loop_w;

static uint32_t run(const up_t *u)
{
    loop_calls++;
    if (u->fn == F_ADD) return u->a[0] + u->a[1] + u->a[2] + u->a[3];
    if (u->fn == F_SLOW) { sleep_ms(300); return 7; }
    if (u->fn == F_AGAIN) {
        uint32_t r = 0;
        return (uint32_t)box_upcall(F_ADD, (const uint32_t[4]){ 1, 1, 1, 1 }, &r);
    }
    if (u->fn == F_LOCKED) return dcall(&loop_w, "esp_err_to_name", 0, 0);   /* a locked one */
    printf("FAIL the loop was handed %#x\n", (unsigned)u->fn);
    failed++;
    return 0;
}

static void *loop_thread(void *arg)
{
    (void)arg;
    window(&loop_w);
    up_t *u = (up_t *)(uintptr_t)feckless_box_alloc(&box, sizeof *u);
    uint32_t ret = 0;
    while (dcall(&loop_w, "feckless_box_upcall_next", P(u), ret)) ret = run(u);
    loop_ended = 1;
    return NULL;
}

/* ---- the host's side ------------------------------------------------------ */

typedef struct { int base, n, wrong; esp_err_t last; volatile int done; } asker_t;

static void *asker(void *arg)
{
    asker_t *k = arg;
    for (int i = 0; i < k->n; i++) {
        uint32_t r = 0;
        const uint32_t a[4] = { (uint32_t)(k->base + i), 1000, 0, 2 };
        k->last = box_upcall(F_ADD, a, &r);
        if (k->last != ESP_OK || r != (uint32_t)(k->base + i) + 1002) k->wrong++;
    }
    k->done = 1;
    return NULL;
}

typedef struct { uint32_t fn; esp_err_t e; uint32_t r; int64_t took; volatile int done; } one_t;

static void *one(void *arg)
{
    one_t *o = arg;
    const int64_t t0 = now_ms();
    o->e = box_upcall(o->fn, (const uint32_t[4]){ 0 }, &o->r);
    o->took = now_ms() - t0;
    o->done = 1;
    return NULL;
}

static void wait_for(volatile int *flag, const char *what)
{
    for (int i = 0; i < 500 && !*flag; i++) sleep_ms(10);
    if (!*flag) {
        printf("FAIL %s: no answer in 5 s -- hung\nupcall_test: FAILED (exiting rather than hanging)\n", what);
        exit(1);
    }
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

    feckless_umode_window_t w;
    window(&w);
    CHECK(calls_tab[idx_of("feckless_box_upcall_next")].flags & FECKLESS_UMODE_CALL_UNLOCKED);
    CHECK(!(calls_tab[idx_of("feckless_box_upcall_loop_is")].flags & FECKLESS_UMODE_CALL_UNLOCKED));
    CHECK(!(calls_tab[idx_of("esp_err_to_name")].flags & FECKLESS_UMODE_CALL_UNLOCKED));

    /* nothing before the app says where its loop is, or with a loop outside its box */
    uint32_t r = 0;
    CHECK(box_upcall(F_ADD, NULL, &r) == ESP_ERR_NOT_SUPPORTED);
    dcall(&w, "feckless_box_upcall_loop_is", P(mem) + MEM, 0);
    pthread_mutex_lock(&s_app);
    CHECK(box_upcall_start(&w, 8192, 5) == ESP_ERR_NOT_SUPPORTED);
    pthread_mutex_unlock(&s_app);
    CHECK(!box_upcall_ready());

    /* the constructor's call, then the first start spawns the loop, once */
    dcall(&w, "feckless_box_upcall_loop_is", F_LOOP, 0);
    const uint32_t spawns = s_calls;
    pthread_mutex_lock(&s_app);
    CHECK(box_upcall_start(&w, 8192, 5) == ESP_OK);
    CHECK(box_upcall_start(&w, 16384, 9) == ESP_OK);
    pthread_mutex_unlock(&s_app);
    CHECK(s_calls == spawns + 1 && s_fn == F_LOOP && s_stack == 8192 && s_prio == 5 && s_core == 1);
    CHECK(box_upcall_ready());

    pthread_t lt;
    pthread_create(&lt, NULL, loop_thread, NULL);

    /* one call, its answer */
    one_t o = { .fn = F_ADD };
    {
        pthread_t t;
        asker_t k = { .base = 1, .n = 1 };
        pthread_create(&t, NULL, asker, &k);
        wait_for(&k.done, "one call");
        pthread_join(t, NULL);
        CHECK(k.wrong == 0);
    }

    /* three hosts asking at once: each gets its own answers, in order */
    {
        asker_t k[3] = { { .base = 0, .n = 200 }, { .base = 10000, .n = 200 }, { .base = 20000, .n = 200 } };
        pthread_t t[3];
        for (int i = 0; i < 3; i++) pthread_create(&t[i], NULL, asker, &k[i]);
        for (int i = 0; i < 3; i++) wait_for(&k[i].done, "three askers");
        for (int i = 0; i < 3; i++) { pthread_join(t[i], NULL); CHECK(k[i].wrong == 0); }
        printf("  600 callbacks from three host tasks at once: each got its own answers\n");
    }

    /* a callback that asks for one from the loop's own thread: refused, not hung */
    o = (one_t){ .fn = F_AGAIN };
    {
        pthread_t t;
        pthread_create(&t, NULL, one, &o);
        wait_for(&o.done, "a callback from the loop's thread");
        pthread_join(t, NULL);
        CHECK(o.e == ESP_OK && o.r == (uint32_t)ESP_ERR_INVALID_STATE);
    }

    /* the app's function makes a locked host call: the asker holds no lock */
    o = (one_t){ .fn = F_LOCKED };
    {
        pthread_t t;
        pthread_mutex_lock(&s_app);         /* and another thread of the app holds it a while */
        pthread_create(&t, NULL, one, &o);
        sleep_ms(50);
        pthread_mutex_unlock(&s_app);
        wait_for(&o.done, "a callback making a locked call");
        pthread_join(t, NULL);
        CHECK(o.e == ESP_OK && o.r >= P(mem) && o.r < P(mem) + MEM && !strcmp((char *)(uintptr_t)o.r, "ESP_OK"));
    }

    /* the app stops while a callback runs: the asker is let go within a
     * slice, before the callback ends; the loop ends after it */
    o = (one_t){ .fn = F_SLOW };
    {
        pthread_t t;
        pthread_create(&t, NULL, one, &o);
        sleep_ms(50);
        fake_stopping = true;
        wait_for(&o.done, "the stop during a callback");
        pthread_join(t, NULL);
        CHECK(o.e == ESP_ERR_INVALID_STATE);
        CHECK(o.took < 250);                /* < the callback's 300 ms */
        wait_for(&loop_ended, "the loop's end");
        pthread_join(lt, NULL);
        CHECK(box_upcall(F_ADD, NULL, &r) == ESP_ERR_INVALID_STATE);
        printf("  the stop let the asker go after %d ms, mid-callback; the loop ended\n", (int)o.took);
    }

    /* the app's end: the next app starts from nothing */
    feckless_box_calls_end();
    fake_stopping = false;
    CHECK(!box_upcall_ready());
    CHECK(box_upcall(F_ADD, NULL, &r) == ESP_ERR_NOT_SUPPORTED);
    pthread_mutex_lock(&s_app);
    CHECK(box_upcall_start(&w, 8192, 5) == ESP_ERR_NOT_SUPPORTED);   /* it has not said where its loop is */
    pthread_mutex_unlock(&s_app);

    if (failed) { printf("upcall_test: %d FAILED\n", failed); return 1; }
    printf("upcall_test: all passed (%d callbacks)\n", loop_calls);
    return 0;
}
