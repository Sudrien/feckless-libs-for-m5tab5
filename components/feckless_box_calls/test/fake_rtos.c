/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0045: a single-threaded stand-in for the FreeRTOS box_rtos.c
 * calls, and for feckless_umode's thread lookups. Nothing waits: a call
 * that would block fails at once, and records the ticks it was given, so a
 * test sees how box_rtos.c slices a wait. The "current task" and the
 * window's thread are what the test says they are.
 */
#include <stdlib.h>
#include <string.h>
#ifdef FAKE_REAL_SEMS
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "feckless_umode_load.h"

unsigned fake_caps_last;
int fake_crit_depth;
void *fake_task;                    /* xTaskGetCurrentTaskHandle() */
int fake_take_tries;                /* blocking attempts that failed */
TickType_t fake_last_ticks;
int fake_live;                      /* objects not deleted */
UBaseType_t fake_prio;
TaskHandle_t fake_prio_task;

uint32_t fake_thread_id = FECKLESS_UMODE_MAIN_THREAD;   /* the window's: app_main */
void *fake_thread_task[16];
uint32_t fake_stack_lo[16], fake_stack_hi[16];
bool fake_stopping;

struct fake_q {
    uint32_t len, item, count, head;
    uint8_t type;
    uint8_t *store;
    void *holder;
    uint32_t rec;
#ifdef FAKE_REAL_SEMS
    /* u-feckless-0075: a binary semaphore that really waits, for upcall_test's threads */
    bool real;
    pthread_mutex_t m;
    pthread_cond_t c;
#endif
};

struct fake_sb {
    size_t size, n, head;
    uint8_t *store;
};

/* u-feckless-0060: the core the caller is on -- the app's, 1, unless a test
 * says otherwise -- and a pinned task run at once, to its end, as if on
 * its core. */
int fake_core = 1, fake_tasks, fake_task_core = -1;
bool fake_task_fail;
BaseType_t xPortGetCoreID(void) { return fake_core; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                   UBaseType_t prio, TaskHandle_t *out, BaseType_t core)
{
    (void)name; (void)stack; (void)prio; (void)out;
    if (fake_task_fail) return pdFAIL;
    const int was = fake_core;
    fake_tasks++;
    fake_task_core = fake_core = core;
    fn(arg);
    fake_core = was;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t t) { (void)t; }
TickType_t xTaskGetTickCount(void) { return 4242; }
#ifdef FAKE_REAL_SEMS
static pthread_mutex_t s_crit = PTHREAD_MUTEX_INITIALIZER;
void fake_crit_enter(void) { pthread_mutex_lock(&s_crit); fake_crit_depth++; }
void fake_crit_exit(void) { fake_crit_depth--; pthread_mutex_unlock(&s_crit); }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)(uintptr_t)pthread_self(); }
#else
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return fake_task; }
#endif
UBaseType_t uxTaskPriorityGet(TaskHandle_t t) { return t == fake_prio_task ? fake_prio : 7; }
void vTaskPrioritySet(TaskHandle_t t, UBaseType_t p) { fake_prio_task = t; fake_prio = p; }
eTaskState eTaskGetState(TaskHandle_t t) { (void)t; return eBlocked; }

static struct fake_q *q_new(uint32_t len, uint32_t item, uint8_t type, uint32_t count, uint8_t *store)
{
    struct fake_q *q = calloc(1, sizeof *q);
    q->len = len; q->item = item; q->type = type; q->count = count; q->store = store;
#ifdef FAKE_REAL_SEMS
    if (type == queueQUEUE_TYPE_BINARY_SEMAPHORE) {
        q->real = true;
        pthread_mutex_init(&q->m, NULL);
        pthread_cond_init(&q->c, NULL);
    }
#endif
    fake_live++;
    return q;
}

QueueHandle_t xQueueGenericCreateStatic(UBaseType_t len, UBaseType_t item, uint8_t *store, StaticQueue_t *ctl, uint8_t type)
{
    if (!ctl || len == 0 || (item && !store) || (!item && store)) abort();      /* FreeRTOS's asserts */
    return q_new(len, item, type, 0, store);
}
QueueHandle_t xQueueCreateMutexStatic(uint8_t type, StaticQueue_t *ctl)
{
    if (!ctl) abort();
    return q_new(1, 0, type, 1, NULL);
}
QueueHandle_t xQueueCreateCountingSemaphoreStatic(UBaseType_t max, UBaseType_t init, StaticQueue_t *ctl)
{
    if (!ctl || !max || init > max) abort();
    return q_new(max, 0, queueQUEUE_TYPE_COUNTING_SEMAPHORE, init, NULL);
}
/* u-feckless-0060: a semaphore of the kernel's own */
QueueHandle_t xQueueGenericCreate(UBaseType_t len, UBaseType_t item, uint8_t type)
{
    if (item) abort();                      /* semaphores only */
    return q_new(len, 0, type, 0, NULL);
}
void vQueueDelete(QueueHandle_t q) { free(q); fake_live--; }

static bool q_mutex(const struct fake_q *q)
{
    return q->type == queueQUEUE_TYPE_MUTEX || q->type == queueQUEUE_TYPE_RECURSIVE_MUTEX;
}

BaseType_t xQueueGenericSend(QueueHandle_t q, const void *item, TickType_t t, BaseType_t pos)
{
#ifdef FAKE_REAL_SEMS
    if (q->real) {
        pthread_mutex_lock(&q->m);
        const bool ok = q->count == 0;
        if (ok) { q->count = 1; pthread_cond_signal(&q->c); }
        pthread_mutex_unlock(&q->m);
        return ok ? pdTRUE : pdFALSE;
    }
#endif
    fake_last_ticks = t;
    if (q->item && !item) abort();
    if (pos == queueOVERWRITE && q->len != 1) abort();
    if (q->type == queueQUEUE_TYPE_MUTEX) {
        if (q->holder != fake_task) abort();        /* xTaskPriorityDisinherit's assert */
        q->holder = NULL;
        q->count = 1;
        return pdTRUE;
    }
    if (q->count == q->len && pos != queueOVERWRITE) {
        fake_take_tries++;
        return pdFALSE;
    }
    if (q->item) {
        uint32_t at;
        if (pos == queueOVERWRITE) { at = q->head; q->count = 0; }
        else if (pos == queueSEND_TO_FRONT) { q->head = (q->head + q->len - 1) % q->len; at = q->head; }
        else at = (q->head + q->count) % q->len;
        memcpy(q->store + at * q->item, item, q->item);
    }
    q->count++;
    return pdTRUE;
}
BaseType_t xQueueSemaphoreTake(QueueHandle_t q, TickType_t t)
{
#ifdef FAKE_REAL_SEMS
    if (q->real) {                          /* t in ms: the fake's 1 kHz tick */
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += t / 1000;
        until.tv_nsec += (long)(t % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
        pthread_mutex_lock(&q->m);
        int r = 0;
        while (!q->count && r != ETIMEDOUT) r = pthread_cond_timedwait(&q->c, &q->m, &until);
        const bool ok = q->count != 0;
        if (ok) q->count = 0;
        pthread_mutex_unlock(&q->m);
        return ok ? pdTRUE : pdFALSE;
    }
#endif
    fake_last_ticks = t;
    if (q->item) abort();
    if (!q->count) { fake_take_tries++; return pdFALSE; }
    q->count--;
    if (q_mutex(q)) q->holder = fake_task;
    return pdTRUE;
}
static BaseType_t q_get(QueueHandle_t q, void *buf, TickType_t t, bool peek)
{
    fake_last_ticks = t;
    if (q->item && !buf) abort();
    if (!q->count) { fake_take_tries++; return pdFALSE; }
    memcpy(buf, q->store + q->head * q->item, q->item);
    if (!peek) { q->head = (q->head + 1) % q->len; q->count--; }
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t t) { return q_get(q, buf, t, false); }
BaseType_t xQueuePeek(QueueHandle_t q, void *buf, TickType_t t) { return q_get(q, buf, t, true); }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t newq) { (void)newq; q->count = q->head = 0; return pdPASS; }
BaseType_t xQueueTakeMutexRecursive(QueueHandle_t q, TickType_t t)
{
    fake_last_ticks = t;
    if (q->holder == fake_task && q->rec) { q->rec++; return pdTRUE; }
    if (!q->count) { fake_take_tries++; return pdFALSE; }
    q->count = 0; q->holder = fake_task; q->rec = 1;
    return pdTRUE;
}
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t q)
{
    if (q->holder != fake_task || !q->rec) return pdFAIL;
    if (--q->rec == 0) { q->holder = NULL; q->count = 1; }
    return pdPASS;
}

StreamBufferHandle_t xStreamBufferGenericCreateStatic(size_t size, size_t trigger, BaseType_t is_msg, uint8_t *store,
                                                      StaticStreamBuffer_t *ctl, StreamBufferCallbackFunction_t s,
                                                      StreamBufferCallbackFunction_t r)
{
    if (!store || !ctl || trigger > size || s || r || (is_msg && size <= sizeof(size_t))) abort();
    struct fake_sb *b = calloc(1, sizeof *b);
    b->size = size; b->store = store;
    fake_live++;
    return b;
}
void vStreamBufferDelete(StreamBufferHandle_t h) { free(h); fake_live--; }
size_t xStreamBufferSend(StreamBufferHandle_t h, const void *p, size_t n, TickType_t t)
{
    fake_last_ticks = t;
    size_t k = 0;
    while (k < n && h->n < h->size) {
        h->store[(h->head + h->n) % h->size] = ((const uint8_t *)p)[k++];
        h->n++;
    }
    if (!k) fake_take_tries++;
    return k;
}
size_t xStreamBufferReceive(StreamBufferHandle_t h, void *p, size_t n, TickType_t t)
{
    fake_last_ticks = t;
    if (!p) abort();
    size_t k = 0;
    while (k < n && h->n) {
        ((uint8_t *)p)[k++] = h->store[h->head];
        h->head = (h->head + 1) % h->size;
        h->n--;
    }
    if (!k) fake_take_tries++;
    return k;
}
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t h) { return h->n; }
size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t h) { return h->size - h->n; }
BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t h) { return h->n == 0; }
BaseType_t xStreamBufferIsFull(StreamBufferHandle_t h) { return h->n == h->size; }
BaseType_t xStreamBufferReset(StreamBufferHandle_t h) { h->n = h->head = 0; return pdPASS; }

/* feckless_umode's, as the loaded app would answer */
uint32_t feckless_umode_app_thread_id(feckless_umode_app_t *a, const feckless_umode_window_t *w)
{
    (void)a; (void)w;
    return fake_thread_id;
}
void *feckless_umode_app_thread_task(feckless_umode_app_t *a, uint32_t id)
{
    (void)a;
    return id < 16 ? fake_thread_task[id] : NULL;
}
bool feckless_umode_app_thread_stack(feckless_umode_app_t *a, uint32_t id, uint32_t *lo, uint32_t *hi)
{
    (void)a;
    if (id >= 16 || !fake_thread_task[id]) return false;
    *lo = fake_stack_lo[id];
    *hi = fake_stack_hi[id];
    return true;
}
bool feckless_umode_app_stopping(const feckless_umode_app_t *a)
{
    (void)a;
    return fake_stopping;
}

/* u-feckless-0077: feckless_umode_app_where(), for the stubs a test names */
uint32_t fake_stub_at[4];
const char *fake_stub_name[4];
int feckless_umode_app_where(uintptr_t addr, uint32_t *link, const char **call)
{
    (void)link;
    for (int i = 0; i < 4; i++) {
        if (fake_stub_at[i] && fake_stub_at[i] == addr) {
            if (call) *call = fake_stub_name[i];
            return FECKLESS_UMODE_WHERE_STUB;
        }
    }
    return FECKLESS_UMODE_WHERE_NONE;
}
