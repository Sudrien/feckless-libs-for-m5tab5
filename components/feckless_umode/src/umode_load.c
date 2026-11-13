/* SPDX-License-Identifier: MIT
 *
 * See feckless_umode_load.h.
 *
 * esp_elf allocates the app's text and data, and its own working buffers,
 * with esp_elf_malloc(). The component's link wraps it (CMakeLists.txt): while
 * a box is being loaded, every allocation comes from the box, so the program
 * lands where the grant is, and everything else -- the trusted path, a
 * launcher's own loads -- goes to the heap as before. What esp_elf keeps after
 * the load (the symbol names) is in the box too, where the program could
 * rewrite it; it is only ever freed, and a free into the box is refused unless
 * it is exactly an allocation.
 *
 * esp_elf_request() is not used: it calls the entry, and the constructors,
 * in M-mode. esp_elf_deinit() runs the fini functions in M-mode, so they are
 * cleared first.
 */
#include "feckless_umode_load.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "private/elf_symbol.h"     /* u-feckless-0014: elf_set_symbol_resolver() */
#include "private/elf_types.h"

#include "feckless_umode.h"          /* FECKLESS_UMODE_MAX_GRANTS, _R, _W */
#include "feckless_umode_box.h"
#include "feckless_umode_idf.h"
#include "feckless_umode_pmp.h"

static const char *TAG = "feckless_umode";

#define MAX_CTORS 64
#define BOX_ALIGN 4096u
#define TLS_BYTES 16u               /* a thread's errno, and room */
#define HOST_STACK 8192u            /* a thread's host task: the run loop and the host calls */

/* u-feckless-0023: one of the app's threads. In host memory (the app struct
 * is internal RAM): the window holds the thread's registers, and the program
 * must not reach it. */
typedef struct {
    feckless_umode_window_t win;
    struct feckless_umode_app *app;
    uintptr_t stack;                /* box allocations, freed as the thread ends */
    uintptr_t tls;
    TaskHandle_t task;
    volatile bool running;
    feckless_umode_end_t end;
} app_thread_t;

struct feckless_umode_app {
    feckless_box_t box;
    feckless_umode_window_t win;
    feckless_umode_calls_t calls;
    esp_elf_t elf;
    struct esp_elfsym *syms;        /* host memory: the names point at calls[] */
    void *block;
    uintptr_t exit_stub;
    bool relocated;
    feckless_umode_grant_t grants[FECKLESS_UMODE_MAX_GRANTS];   /* [0] is the box */
    int ngrants;
    /* u-feckless-0023: threads */
    SemaphoreHandle_t lock;         /* recursive: one host call at a time (but UNLOCKED ones) */
    SemaphoreHandle_t ended;        /* given by each thread as it ends */
    volatile bool stopping;
    bool stuck;                     /* a thread would not stop: never freed */
    bool have_bad;
    feckless_umode_end_t bad;       /* the first end that was not EXIT */
    uintptr_t tls_main;             /* app_main's errno block (tp) */
    app_thread_t th[FECKLESS_UMODE_MAX_THREADS];
};

/* The box being loaded or unloaded, or NULL: the wrap's switch. */
static feckless_box_t *s_loading;
static feckless_umode_app_t *s_loaded;

void *__real_esp_elf_malloc(uint32_t n, bool exec);
void __real_esp_elf_free(void *ptr);

void *__wrap_esp_elf_malloc(uint32_t n, bool exec)
{
    if (s_loading) {
        return (void *)feckless_box_alloc(s_loading, n);
    }
    return __real_esp_elf_malloc(n, exec);
}

void __wrap_esp_elf_free(void *ptr)
{
    const uintptr_t p = (uintptr_t)ptr;
    if (s_loaded && p >= s_loaded->box.base && p < s_loaded->box.base + s_loaded->box.size) {
        feckless_box_free(&s_loaded->box, p);   /* refused unless it is an allocation */
        return;
    }
    if (s_loading && p >= s_loading->base && p < s_loading->base + s_loading->size) {
        feckless_box_free(s_loading, p);
        return;
    }
    __real_esp_elf_free(ptr);
}

/*
 * u-feckless-0014: the names a box's ELF is relocated against are the box's
 * table and nothing else.
 *
 * esp_elf resolves an import with elf_find_sym(), whose default searches
 * elf_loader's own tables first -- with CONFIG_ELF_LOADER_LIBC_SYMBOLS, its
 * default, malloc, calloc, realloc, free, puts, putchar, printf, __divdf3 and
 * more, as the host's functions -- and only then the tables registered with
 * esp_elf_register_symbol(). On a Tab5 the box's table was registered and
 * Spurious's calloc still resolved to IDF's: `instruction access fault at
 * 0x4ff09c62` (calloc, heap.c), stopped by the PMP. So the box's table is not
 * registered at all; while a box is relocated, this is the resolver. A name
 * not here returns 0, and esp_elf fails the load naming it.
 */
static struct esp_elfsym *s_resolving;
static const feckless_umode_call_t *s_resolving_calls;

/* u-feckless-0021: a data entry is copied into the box the first time the
 * ELF names it, not at every load -- Ark12's tables are 380 KB, and only an
 * app that draws its own text reads them. Allocated from the box as the
 * relocation's own allocations are (s_loading). */
static uintptr_t box_resolve(const char *name)
{
    for (struct esp_elfsym *p = s_resolving; p && p->name; p++) {
        if (strcmp(p->name, name) != 0) {
            continue;
        }
        if (!p->sym) {
            const feckless_umode_call_t *c = &s_resolving_calls[p - s_resolving];
            const uint32_t len = c->data_len ? c->data_len : 4;
            const uintptr_t d = s_loading ? feckless_box_alloc(s_loading, len) : 0;
            if (!d) {
                ESP_LOGE(TAG, "no room in the box for %s (%u bytes)", name, (unsigned)len);
                return 0;
            }
            memset((void *)d, 0, len);
            if (c->data && c->data_len) {
                memcpy((void *)d, c->data, c->data_len);
            }
            p->sym = (const void *)d;
        }
        return (uintptr_t)p->sym;
    }
    return 0;
}

static bool in_box(const feckless_umode_app_t *a, uintptr_t p)
{
    return p >= a->box.base && p < a->box.base + a->box.size;
}

/* u-feckless-0023: the first end that was not EXIT is the app's. A KILLED
 * that the stop itself caused is not a cause. Under the lock. */
static void note_end(feckless_umode_app_t *a, const feckless_umode_end_t *e)
{
    if (e->reason == FECKLESS_UMODE_END_EXIT) {
        return;
    }
    xSemaphoreTakeRecursive(a->lock, portMAX_DELAY);
    if (!a->have_bad && !(e->reason == FECKLESS_UMODE_END_KILLED && a->stopping)) {
        a->bad = *e;
        a->have_bad = true;
    }
    a->stopping = true;
    xSemaphoreGiveRecursive(a->lock);
}

/* ---- u-feckless-0029: what the host calls cost ---------------------------- */
#if CONFIG_FECKLESS_UMODE_CALL_STATS
#define STAT_TOP 12
#define STAT_THREADS (FECKLESS_UMODE_MAX_THREADS + 1)   /* [0] is app_main */
static uint32_t s_st_n[FECKLESS_BOX_MAX_STUBS], s_st_us[FECKLESS_BOX_MAX_STUBS];
static uint32_t s_st_last_n[FECKLESS_BOX_MAX_STUBS], s_st_last_us[FECKLESS_BOX_MAX_STUBS];
/* u-feckless-0032: of each call's time, how much was the wait for the app's
 * lock -- another thread's call holding it -- rather than the call itself */
static uint32_t s_st_wait[FECKLESS_BOX_MAX_STUBS], s_st_last_wait[FECKLESS_BOX_MAX_STUBS];
static uint32_t s_th_n[STAT_THREADS], s_th_us[STAT_THREADS], s_th_last_n[STAT_THREADS], s_th_last_us[STAT_THREADS];
static esp_timer_handle_t s_st_timer;
static int64_t s_st_last_t;

static int thread_of(const feckless_umode_app_t *a, const feckless_umode_window_t *w)
{
    if (w == &a->win) return 0;
    for (int i = 0; i < FECKLESS_UMODE_MAX_THREADS; i++)
        if (w == &a->th[i].win) return i + 1;
    return 0;
}

static void stats_tick(void *arg)
{
    const feckless_umode_app_t *a = arg;
    const int64_t now = esp_timer_get_time();
    const float secs = (float)(now - s_st_last_t) * 1e-6f;
    s_st_last_t = now;
    if (secs <= 0.0f || !a) return;
    /* the deltas since the last line; unsigned, so a wrap is still right */
    static uint32_t dn[FECKLESS_BOX_MAX_STUBS], dus[FECKLESS_BOX_MAX_STUBS], dwait[FECKLESS_BOX_MAX_STUBS];
    uint32_t total_n = 0, total_us = 0;
    for (uint32_t i = 0; i < a->calls.n; i++) {
        const uint32_t n = __atomic_load_n(&s_st_n[i], __ATOMIC_RELAXED);
        const uint32_t us = __atomic_load_n(&s_st_us[i], __ATOMIC_RELAXED);
        const uint32_t wt = __atomic_load_n(&s_st_wait[i], __ATOMIC_RELAXED);
        dn[i] = n - s_st_last_n[i];
        dus[i] = us - s_st_last_us[i];
        dwait[i] = wt - s_st_last_wait[i];
        s_st_last_n[i] = n;
        s_st_last_us[i] = us;
        s_st_last_wait[i] = wt;
        total_n += dn[i];
        total_us += dus[i];
    }
    ESP_LOGI(TAG, "host calls, last %.1f s: %.0f/s, %.1f ms/s in the host", (double)secs,
             (double)(total_n / secs), (double)(total_us / secs / 1000.0f));
    for (int k = 0; k < STAT_TOP; k++) {            /* the busiest by time, a selection at a time */
        uint32_t best = UINT32_MAX, best_us = 0;
        for (uint32_t i = 0; i < a->calls.n; i++)
            if (dn[i] && dus[i] >= best_us) { best = i; best_us = dus[i]; }
        if (best == UINT32_MAX) break;
        ESP_LOGI(TAG, "  %-28s %8.0f/s %8.2f us each (%7.2f waiting for the lock) %6.1f ms/s",
                 a->calls.calls[best].name, (double)(dn[best] / secs),
                 (double)((float)dus[best] / (float)dn[best]),
                 (double)((float)dwait[best] / (float)dn[best]), (double)(dus[best] / secs / 1000.0f));
        dn[best] = 0;
    }
    for (int t = 0; t < STAT_THREADS; t++) {
        const uint32_t n = __atomic_load_n(&s_th_n[t], __ATOMIC_RELAXED);
        const uint32_t us = __atomic_load_n(&s_th_us[t], __ATOMIC_RELAXED);
        const uint32_t d = n - s_th_last_n[t], du = us - s_th_last_us[t];
        s_th_last_n[t] = n;
        s_th_last_us[t] = us;
        if (d) {
            ESP_LOGI(TAG, "  %s: %.0f calls/s, %.1f ms/s in them", t ? "thread" : "app_main",
                     (double)(d / secs), (double)(du / secs / 1000.0f));
        }
    }
}

static void stats_start(feckless_umode_app_t *a)
{
    memset(s_st_n, 0, sizeof s_st_n); memset(s_st_us, 0, sizeof s_st_us);
    memset(s_st_last_n, 0, sizeof s_st_last_n); memset(s_st_last_us, 0, sizeof s_st_last_us);
    memset(s_st_wait, 0, sizeof s_st_wait); memset(s_st_last_wait, 0, sizeof s_st_last_wait);
    memset(s_th_n, 0, sizeof s_th_n); memset(s_th_us, 0, sizeof s_th_us);
    memset(s_th_last_n, 0, sizeof s_th_last_n); memset(s_th_last_us, 0, sizeof s_th_last_us);
    const esp_timer_create_args_t args = { .callback = stats_tick, .arg = a, .name = "box_stats" };
    if (esp_timer_create(&args, &s_st_timer) == ESP_OK) {
        s_st_last_t = esp_timer_get_time();
        esp_timer_start_periodic(s_st_timer, (uint64_t)CONFIG_FECKLESS_UMODE_CALL_STATS_SECONDS * 1000000u);
    }
}

static void stats_stop(void)
{
    if (s_st_timer) {
        esp_timer_stop(s_st_timer);
        esp_timer_delete(s_st_timer);
        s_st_timer = NULL;
    }
}

static void stats_add(const feckless_umode_app_t *a, const feckless_umode_window_t *w, uint32_t i, int64_t t0)
{
    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    if (i < FECKLESS_BOX_MAX_STUBS) {
        __atomic_fetch_add(&s_st_n[i], 1u, __ATOMIC_RELAXED);
        __atomic_fetch_add(&s_st_us[i], us, __ATOMIC_RELAXED);
    }
    const int t = thread_of(a, w);
    __atomic_fetch_add(&s_th_n[t], 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_th_us[t], us, __ATOMIC_RELAXED);
}
#endif

/* Every window's ext hook: the app's lock around the call, unless the call
 * says it needs none; and once the app is stopping, the end of the thread. */
static bool app_ext(feckless_umode_window_t *w, uint32_t nr, const uint32_t args[7], uint32_t *ret)
{
    feckless_umode_app_t *a = s_loaded;
    if (!a || a->stopping) {
        *ret = 0;
        return false;                           /* KILLED */
    }
    const feckless_umode_calls_t *t = feckless_umode_calls_of(w);
    const uint32_t i = nr - FECKLESS_SYS_EXT_BASE;
    const bool unlocked = t && nr >= FECKLESS_SYS_EXT_BASE && i < t->n &&
                          (t->calls[i].flags & FECKLESS_UMODE_CALL_UNLOCKED);
#if CONFIG_FECKLESS_UMODE_CALL_STATS
    const int64_t t0 = esp_timer_get_time();    /* the lock's wait counts: it is the call's cost */
#endif
    bool go_on;
    if (unlocked) {
        go_on = feckless_umode_calls_ext(w, nr, args, ret);
    } else {
        xSemaphoreTakeRecursive(a->lock, portMAX_DELAY);
#if CONFIG_FECKLESS_UMODE_CALL_STATS
        if (i < FECKLESS_BOX_MAX_STUBS) {
            __atomic_fetch_add(&s_st_wait[i], (uint32_t)(esp_timer_get_time() - t0), __ATOMIC_RELAXED);
        }
#endif
        go_on = feckless_umode_calls_ext(w, nr, args, ret);
        xSemaphoreGiveRecursive(a->lock);
    }
#if CONFIG_FECKLESS_UMODE_CALL_STATS
    stats_add(a, w, i, t0);
#endif
    return go_on;
}

static void thread_main(void *arg)
{
    app_thread_t *t = arg;
    feckless_umode_app_t *a = t->app;
    t->end = feckless_umode_run_default(&t->win);
    note_end(a, &t->end);
    if (t->end.reason != FECKLESS_UMODE_END_EXIT) {
        ESP_LOGE(TAG, "thread %d ended: reason %d, %s at 0x%08x", (int)(t - a->th) + 1,
                 (int)t->end.reason,
                 t->end.reason == FECKLESS_UMODE_END_FAULT ? feckless_umode_cause_name(t->end.mcause) : "-",
                 (unsigned)t->end.pc);
    }
    xSemaphoreTakeRecursive(a->lock, portMAX_DELAY);
    feckless_box_free(&a->box, t->stack);
    feckless_box_free(&a->box, t->tls);
    t->stack = t->tls = 0;
    t->task = NULL;
    t->running = false;
    xSemaphoreGiveRecursive(a->lock);
    xSemaphoreGive(a->ended);
    vTaskDelete(NULL);
}

esp_err_t feckless_umode_app_spawn(feckless_umode_app_t *a, uint32_t fn, uint32_t arg,
                                   uint32_t stack_bytes, int prio, int core, uint32_t *id)
{
    if (!a || a != s_loaded || a->stopping) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!in_box(a, fn) || stack_bytes < 1024 || stack_bytes > (1u << 20) ||
        !(core == 0 || core == 1 || core == tskNO_AFFINITY)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTakeRecursive(a->lock, portMAX_DELAY);
    app_thread_t *t = NULL;
    for (int i = 0; i < FECKLESS_UMODE_MAX_THREADS; i++) {
        if (!a->th[i].running && !a->th[i].task) {
            t = &a->th[i];
            break;
        }
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!t) {
        goto out;
    }
    stack_bytes = (stack_bytes + 15u) & ~15u;
    t->stack = feckless_box_alloc(&a->box, stack_bytes);
    t->tls = feckless_box_alloc(&a->box, TLS_BYTES);
    if (!t->stack || !t->tls) {
        goto undo;
    }
    memset((void *)t->tls, 0, TLS_BYTES);
    t->app = a;
    t->win = a->win;                            /* the box's range and calls; counters from 0 */
    t->win.syscalls = t->win.irqs = t->win.migrations = 0;
    t->win.pie = false;                         /* u-feckless-0034: the unit is per host task */
    feckless_umode_ctx_init(&t->win.ctx, fn, (uint32_t)t->stack,
                            (uint32_t)(t->stack + stack_bytes - 16u), arg);
    t->win.ctx.x[1] = (uint32_t)a->exit_stub;  /* ra: returning ends the thread */
    t->win.ctx.x[4] = (uint32_t)t->tls;        /* tp */
    if (prio < 1) prio = 1;
    if (prio > configMAX_PRIORITIES - 2) prio = configMAX_PRIORITIES - 2;
    char name[configMAX_TASK_NAME_LEN];
    snprintf(name, sizeof name, "box_t%d", (int)(t - a->th) + 1);
    t->running = true;
    if (xTaskCreatePinnedToCore(thread_main, name, HOST_STACK, t, (UBaseType_t)prio, &t->task,
                                core == tskNO_AFFINITY ? tskNO_AFFINITY : (BaseType_t)core) != pdPASS) {
        t->running = false;
        t->task = NULL;
        goto undo;
    }
    if (id) {
        *id = (uint32_t)(t - a->th) + 1;
    }
    ESP_LOGI(TAG, "thread %d: %u bytes of stack in the box, priority %d, core %d",
             (int)(t - a->th) + 1, (unsigned)stack_bytes, prio, core == tskNO_AFFINITY ? -1 : core);
    err = ESP_OK;
    goto out;
undo:
    feckless_box_free(&a->box, t->stack);
    feckless_box_free(&a->box, t->tls);
    t->stack = t->tls = 0;
out:
    xSemaphoreGiveRecursive(a->lock);
    return err;
}

static bool any_running(const feckless_umode_app_t *a)
{
    for (int i = 0; i < FECKLESS_UMODE_MAX_THREADS; i++) {
        if (a->th[i].running) {
            return true;
        }
    }
    return false;
}

/* After app_main (or a constructor) has ended: wait for the threads. */
static feckless_umode_end_t finish(feckless_umode_app_t *a, feckless_umode_end_t end)
{
    note_end(a, &end);
    while (any_running(a)) {
        const TickType_t wait = a->stopping ? pdMS_TO_TICKS(FECKLESS_UMODE_STOP_WAIT_MS) : portMAX_DELAY;
        if (xSemaphoreTake(a->ended, wait) != pdTRUE) {
            a->stuck = true;
            ESP_LOGE(TAG, "a thread of the app made no host call in %d ms and cannot be stopped",
                     FECKLESS_UMODE_STOP_WAIT_MS);
            break;
        }
    }
    return a->have_bad ? a->bad : end;
}

static void free_app(feckless_umode_app_t *a)
{
    if (a->relocated) {
        a->elf.fini_count = 0;                  /* never in M-mode */
        a->elf.fini_array = NULL;
        s_loading = &a->box;
        esp_elf_deinit(&a->elf);
        s_loading = NULL;
    }
    if (a->lock) vSemaphoreDelete(a->lock);
    if (a->ended) vSemaphoreDelete(a->ended);
    heap_caps_free(a->syms);
    heap_caps_free(a->block);
    heap_caps_free(a);
}

esp_err_t feckless_umode_load(const uint8_t *image, size_t size,
                              const feckless_umode_call_t *calls, uint32_t n,
                              uint32_t box_size, uint32_t stack,
                              feckless_umode_app_t **out)
{
    *out = NULL;
    if (s_loaded || s_loading) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!image || (!calls && n) || n > FECKLESS_BOX_MAX_STUBS) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The app's bookkeeping is host memory, internal RAM: never the box. */
    feckless_umode_app_t *a = heap_caps_calloc(1, sizeof *a, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    a->lock = xSemaphoreCreateRecursiveMutex();
    a->ended = xSemaphoreCreateCounting(FECKLESS_UMODE_MAX_THREADS, 0);
    if (!a->lock || !a->ended) {
        free_app(a);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    /* u-feckless-0011: 4 KB at both ends, whatever the PMP's granularity
     * turns out to be up to that (umode_pmp.c logs it). */
    box_size &= ~(uint32_t)(BOX_ALIGN - 1);
    a->block = heap_caps_aligned_alloc(BOX_ALIGN, box_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!a->block || !feckless_box_init(&a->box, (uintptr_t)a->block, box_size, stack)) {
        ESP_LOGE(TAG, "no box of %u KB with %u KB of stack", (unsigned)(box_size >> 10), (unsigned)(stack >> 10));
        goto fail;
    }

    /* The stubs first; the symbol table points at them, and at each data
     * entry once box_resolve() has copied it (u-feckless-0021). */
    const uintptr_t stubs = feckless_box_alloc(&a->box, FECKLESS_BOX_STUBS_BYTES(n));
    a->syms = heap_caps_calloc(n + 1, sizeof *a->syms, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!stubs || !a->syms) {
        goto fail;
    }
    a->exit_stub = feckless_box_write_stubs(stubs, n);
    for (uint32_t i = 0; i < n; i++) {
        a->syms[i].name = calls[i].name;
        if (calls[i].fn) {
            a->syms[i].sym = (const void *)(stubs + i * FECKLESS_BOX_STUB_SIZE);
        } else {
            a->syms[i].sym = NULL;              /* copied on first use, by box_resolve() */
        }
    }
    /* syms[n] is the terminator, zeroed by calloc. */

    /* Relocate, every allocation into the box and every name from the box's
     * table alone. The relocation's own cache flush (a full write-back and
     * I-cache invalidate on the P4) covers the stubs written above. */
    esp_elf_init(&a->elf);
    s_loading = &a->box;
    s_resolving = a->syms;
    s_resolving_calls = calls;
    elf_set_symbol_resolver(box_resolve);
    int r = esp_elf_relocate(&a->elf, image, size);
    elf_reset_symbol_resolver();
    s_resolving = NULL;
    s_resolving_calls = NULL;
    s_loading = NULL;
    if (r < 0) {
        ESP_LOGE(TAG, "esp_elf_relocate: %d (an import not in the box's table is named above)", r);
        err = r == -ENOMEM ? ESP_ERR_NO_MEM : ESP_ERR_NOT_FOUND;
        goto fail;
    }
    a->relocated = true;
    if (!a->elf.entry || !in_box(a, (uintptr_t)a->elf.entry)) {
        ESP_LOGE(TAG, "the entry is not in the box");
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }

    /* The grant: the box, and nothing else (until feckless_umode_app_grant()). */
    a->grants[0] = (feckless_umode_grant_t){
        (uint32_t)a->box.base, (uint32_t)(a->box.base + a->box.size),
        FECKLESS_UMODE_R | FECKLESS_UMODE_W | FECKLESS_UMODE_X,
    };
    a->ngrants = 1;
    if (feckless_umode_pmp_apply(a->grants, a->ngrants) != ESP_OK) {
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }

    /* app_main's errno block: the box's, so the program reaches it by tp */
    a->tls_main = feckless_box_alloc(&a->box, TLS_BYTES);
    if (!a->tls_main) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    memset((void *)a->tls_main, 0, TLS_BYTES);

    a->calls.calls = calls;
    a->calls.n = n;
    a->calls.box = &a->box;
    a->win.lo = a->box.base;
    a->win.hi = a->box.base + a->box.size;
    a->win.ext = app_ext;
    a->win.ext_arg = &a->calls;
    s_loaded = a;
    *out = a;
#if CONFIG_FECKLESS_UMODE_CALL_STATS
    stats_start(a);
#endif
    ESP_LOGI(TAG, "app in its box at %p, %u KB, %u calls; %u KB used by the load",
             a->block, (unsigned)(box_size >> 10), (unsigned)n,
             (unsigned)((a->box.n ? a->box.a[a->box.n - 1].off + a->box.a[a->box.n - 1].len : 0) >> 10));
    return ESP_OK;

fail:
    free_app(a);
    return err;
}

/* One function, in a fresh context on the box's stack, returning to the exit stub. */
static feckless_umode_end_t run_one(feckless_umode_app_t *a, uintptr_t fn)
{
    uint32_t lo, hi;
    feckless_box_stack(&a->box, &lo, &hi);
    feckless_umode_ctx_init(&a->win.ctx, (uint32_t)fn, lo, hi, 0);
    a->win.ctx.x[1] = (uint32_t)a->exit_stub;   /* ra */
    a->win.ctx.x[4] = (uint32_t)a->tls_main;    /* tp: errno (feckless_box_libc), u-feckless-0023 */
    return feckless_umode_run_default(&a->win);
}

feckless_umode_end_t feckless_umode_app_run(feckless_umode_app_t *a)
{
    feckless_umode_end_t end = { .reason = FECKLESS_UMODE_END_REFUSED };
    if (!a || a != s_loaded) {
        return end;
    }
    /* The arrays are in the box, where the program could rewrite them while
     * the first constructor runs, so they are read once, here. A pointer
     * outside the box would only fault, but it is not worth a window. */
    uintptr_t ctors[MAX_CTORS];
    uint32_t nc = 0;
    void (**const lists[2])(void) = { a->elf.preinit_array, a->elf.init_array };
    const uint32_t counts[2] = { a->elf.preinit_count, a->elf.init_count };
    for (int l = 0; l < 2; l++) {
        for (uint32_t i = 0; lists[l] && i < counts[l]; i++) {
            const uint32_t k = (l == 1 && a->elf.init_reverse) ? counts[l] - 1 - i : i;
            const uintptr_t f = (uintptr_t)lists[l][k];
            if (nc == MAX_CTORS || !in_box(a, f)) {
                ESP_LOGE(TAG, "constructor %u: %s", (unsigned)nc, nc == MAX_CTORS ? "too many" : "not in the box");
                return end;
            }
            ctors[nc++] = f;
        }
    }
    a->elf.init_done = true;
    for (uint32_t i = 0; i < nc; i++) {
        end = run_one(a, ctors[i]);
        if (end.reason != FECKLESS_UMODE_END_EXIT) {
            return finish(a, end);
        }
    }
    return finish(a, run_one(a, (uintptr_t)a->elf.entry));
}

/* u-feckless-0031 */
typedef struct {
    feckless_umode_app_t *app;
    feckless_umode_end_t end;
    SemaphoreHandle_t done;
} run_on_t;

static void run_on_task(void *arg)
{
    run_on_t *r = arg;
    r->end = feckless_umode_app_run(r->app);
    xSemaphoreGive(r->done);
    vTaskDelete(NULL);
}

feckless_umode_end_t feckless_umode_app_run_on(feckless_umode_app_t *a, int core)
{
    run_on_t r = { .app = a, .done = xSemaphoreCreateBinary() };
    if (r.done && (core == 0 || core == 1) &&
        xTaskCreatePinnedToCore(run_on_task, "box_main", FECKLESS_UMODE_APP_STACK, &r,
                                uxTaskPriorityGet(NULL), NULL, core) == pdPASS) {
        ESP_LOGI(TAG, "app_main on core %d", core);
        xSemaphoreTake(r.done, portMAX_DELAY);
        vSemaphoreDelete(r.done);
        return r.end;
    }
    if (r.done) vSemaphoreDelete(r.done);
    ESP_LOGW(TAG, "app_main could not be moved to core %d; it runs here", core);
    return feckless_umode_app_run(a);
}

feckless_umode_app_t *feckless_umode_app_loaded(void)
{
    return s_loaded;
}

/* u-feckless-0022 */
esp_err_t feckless_umode_app_grant(feckless_umode_app_t *a, uint32_t lo, uint32_t hi, uint32_t perm)
{
    if (!a || a != s_loaded) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((perm & FECKLESS_UMODE_X) || !(perm & FECKLESS_UMODE_R) ||
        (perm & ~(FECKLESS_UMODE_R | FECKLESS_UMODE_W)) || lo >= hi) {
        return ESP_ERR_INVALID_ARG;             /* data only: nothing outside the box runs */
    }
    for (int i = 0; i < a->ngrants; i++) {
        const feckless_umode_grant_t *g = &a->grants[i];
        if (g->lo == lo && g->hi == hi && g->perm == perm) {
            return ESP_OK;                      /* already */
        }
        if (lo < g->hi && g->lo < hi) {
            return ESP_ERR_INVALID_ARG;         /* overlapping grants: the earlier would win */
        }
    }
    if (a->ngrants == FECKLESS_UMODE_MAX_GRANTS) {
        return ESP_ERR_NO_MEM;
    }
    a->grants[a->ngrants] = (feckless_umode_grant_t){ lo, hi, perm };
    esp_err_t e = feckless_umode_pmp_apply(a->grants, a->ngrants + 1);
    if (e != ESP_OK) {
        feckless_umode_pmp_apply(a->grants, a->ngrants);   /* back to what it was */
        return e;
    }
    a->ngrants++;
    ESP_LOGI(TAG, "granted 0x%08x..0x%08x (%s) beside the box", (unsigned)lo, (unsigned)hi,
             perm & FECKLESS_UMODE_W ? "read, write" : "read");
    return ESP_OK;
}

feckless_umode_window_t *feckless_umode_app_window(feckless_umode_app_t *a)
{
    return &a->win;
}

void feckless_umode_unload(feckless_umode_app_t *a)
{
    if (!a || a != s_loaded) {
        return;
    }
    if (a->stuck || any_running(a)) {
        /* Its PMP grant cannot be taken back (the IPC would wait on a core
         * whose window holds interrupts off) and its box is in use. */
        ESP_LOGE(TAG, "not unloaded: a thread of the app is still running; reset to recover");
        return;
    }
#if CONFIG_FECKLESS_UMODE_CALL_STATS
    stats_stop();
#endif
    feckless_umode_pmp_apply(NULL, 0);          /* U-mode is granted nothing again */
    s_loaded = NULL;
    free_app(a);
}
