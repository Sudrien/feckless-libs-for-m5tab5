/* SPDX-License-Identifier: MIT
 *
 * FreeRTOS for a boxed app (u-feckless-0045): mutexes, semaphores, queues,
 * stream buffers, task notifications, critical sections, and the task
 * queries Defeatist makes -- the names its ELF imports, and their nearest
 * siblings.
 *
 * Nothing FreeRTOS keeps is in the box. A queue's Queue_t holds list
 * pointers the scheduler follows, and an app that could rewrite one could
 * aim the kernel anywhere; so every object is the host's -- its control
 * block in internal RAM, as FreeRTOS on the P4 needs (the spinlock), its
 * storage in PSRAM -- and the app gets a number for it. A static create's
 * control block, which the app passed from its own memory, is not used: an
 * app's StaticQueue_t is the app's to scribble on. The items, the stream
 * bytes and the values copied out are the only things that cross, each
 * checked to lie in the box.
 *
 * u-feckless-0049: a static stream buffer's storage is the app's own, in
 * place, when it lies in the box. It holds only the bytes in flight -- the
 * indices are in the host's control block -- so whatever the app writes
 * there is data, never a pointer the host follows; and Defeatist's three
 * PCM rings are 3.5 MB each, which 0045 allocated a second time in the
 * host's PSRAM.
 *
 * The calls that may wait are UNLOCKED (u-feckless-0023): a thread blocked
 * on a mutex must not hold every other thread's host calls up. They wait in
 * slices of SLICE_MS and give up as soon as the app is stopping, so no host
 * call outlives FECKLESS_UMODE_STOP_WAIT_MS. Since they run without the
 * app's lock, the object table has a spinlock of its own and each object a
 * count of the unlocked calls inside it: one deleted while a thread waits
 * on it (the app's mistake, which FreeRTOS leaves undefined) is freed by
 * the last call to leave it, never under it.
 *
 * A notification is not the host task's own: the host task that runs a
 * thread's window also runs its host calls, and a driver among those may
 * wait on its task's notification -- which an app's xTaskNotify() would
 * then satisfy, or swallow. So each thread has a value, a state and a
 * binary semaphore of its own here, with FreeRTOS's semantics on top.
 *
 * Critical sections are one recursive mutex for the app. A window holds
 * interrupts off on its core, so an app's threads on one core already run
 * one at a time between host calls; the mutex is what keeps the guarantee
 * across a host call, and across cores. The app's portMUX_TYPE is not
 * touched.
 *
 * What FreeRTOS would configASSERT() on -- an overwrite to a queue longer
 * than one, a mutex given by a task that does not hold it, a trigger level
 * past the size -- is refused here first: an assertion in the host is the
 * launcher's panic, not the app's.
 */
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include "feckless_umode_load.h"

#include "box_calls_priv.h"

#define MAX_OBJ        64
#define TOKEN_OBJ      0x52540000u          /* "RT" + slot: a queue, semaphore, mutex or stream buffer */
#define SLICE_MS       100
#define QUEUE_MAX      (64u * 1024u)        /* a queue's storage, length times item */
#define STREAM_MAX     (1024u * 1024u)      /* storage the host gives; the app's own, any size (0064) */
#define NTHREADS       (FECKLESS_UMODE_MAIN_THREAD + 1)     /* ids 1..MAIN_THREAD; 0 unused */
#define NAME_LEN       16u                  /* the app's task names, as configMAX_TASK_NAME_LEN */

/* The app's TaskHandle_t values (u-feckless-0023): 1 + the slot, and app_main. */
#define IS_ID(h)       ((h) >= 1 && (h) <= FECKLESS_UMODE_MAIN_THREAD)

typedef enum { OBJ_FREE = 0, OBJ_QUEUE, OBJ_STREAM } kind_t;

typedef struct {
    kind_t kind;
    uint8_t qtype;                          /* queueQUEUE_TYPE_* */
    uint32_t len, item;                     /* a queue's; a stream buffer's size in len */
    void *h;                                /* QueueHandle_t, StreamBufferHandle_t */
    void *ctl;                              /* StaticQueue_t, StaticStreamBuffer_t: internal */
    uint8_t *store;                         /* PSRAM, or NULL */
    void *holder;                           /* a plain mutex's: the host task that took it */
    uint32_t users;                         /* unlocked calls inside it now */
    bool dying;                             /* being made, or deleted while in use */
} obj_t;

typedef struct {
    uint32_t value;
    bool pending;                           /* eNotified */
    void *wake;                             /* a binary semaphore of the host's */
    void *wake_ctl;
    uint32_t name_box;                      /* pcTaskGetName()'s copy in the box */
    char name[NAME_LEN];
} thr_t;

static obj_t s_obj[MAX_OBJ];
static thr_t s_thr[NTHREADS];
static void *s_crit;                        /* the app's critical sections: recursive mutex */
static void *s_crit_ctl;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_warned_suspend;

static void *internal(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

/* PSRAM where there is some: what the app's own memory would have been. */
static void *outer(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_8BIT);
}

static TickType_t slice_of(TickType_t left)
{
    TickType_t s = pdMS_TO_TICKS(SLICE_MS);
    if (s == 0) s = 1;
    return left < s ? left : s;
}

/* After a slice that did not succeed: whether to wait another. */
static bool wait_more(TickType_t *left, TickType_t slice)
{
    if (feckless_umode_app_stopping(feckless_umode_app_loaded())) {
        return false;
    }
    if (*left == portMAX_DELAY) {
        return true;
    }
    *left -= slice;
    return *left > 0;
}

static uint32_t self_id(win_t *w)
{
    return feckless_umode_app_thread_id(feckless_umode_app_loaded(), w);
}

/* A TaskHandle_t from the app: NULL is the caller. 0 if neither. */
static uint32_t id_of(win_t *w, uint32_t h)
{
    if (h == 0) return self_id(w);
    return IS_ID(h) ? h : 0;
}

/* ---- the object table ---------------------------------------------------- */

static obj_t *use(uint32_t tok, kind_t kind)
{
    const uint32_t i = tok - TOKEN_OBJ;
    if (tok < TOKEN_OBJ || i >= MAX_OBJ) {
        return NULL;
    }
    obj_t *o = &s_obj[i];
    portENTER_CRITICAL(&s_mux);
    const bool ok = o->kind == kind && !o->dying && o->h;
    if (ok) o->users++;
    portEXIT_CRITICAL(&s_mux);
    return ok ? o : NULL;
}

static void destroy(obj_t *o)
{
    if (o->h) {
        if (o->kind == OBJ_QUEUE) vQueueDelete((QueueHandle_t)o->h);
        else vStreamBufferDelete((StreamBufferHandle_t)o->h);
    }
    heap_caps_free(o->ctl);
    heap_caps_free(o->store);
    portENTER_CRITICAL(&s_mux);
    memset(o, 0, sizeof *o);
    portEXIT_CRITICAL(&s_mux);
}

static void done(obj_t *o)
{
    portENTER_CRITICAL(&s_mux);
    o->users--;
    const bool last = o->dying && o->users == 0;
    portEXIT_CRITICAL(&s_mux);
    if (last) destroy(o);
}

/* A slot, held (dying) until made() or unmade(). */
static obj_t *reserve(kind_t kind)
{
    portENTER_CRITICAL(&s_mux);
    obj_t *o = NULL;
    for (int i = 0; i < MAX_OBJ; i++) {
        if (s_obj[i].kind == OBJ_FREE) {
            o = &s_obj[i];
            o->kind = kind;
            o->dying = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return o;
}

static uint32_t made(obj_t *o)
{
    portENTER_CRITICAL(&s_mux);
    o->dying = false;
    portEXIT_CRITICAL(&s_mux);
    return TOKEN_OBJ + (uint32_t)(o - s_obj);
}

static uint32_t unmade(obj_t *o)
{
    destroy(o);
    return 0;
}

static void delete_obj(uint32_t tok, kind_t kind)
{
    const uint32_t i = tok - TOKEN_OBJ;
    if (tok < TOKEN_OBJ || i >= MAX_OBJ) return;
    obj_t *o = &s_obj[i];
    portENTER_CRITICAL(&s_mux);
    const bool mine = o->kind == kind && !o->dying && o->h;
    if (mine) o->dying = true;
    const bool now = mine && o->users == 0;
    portEXIT_CRITICAL(&s_mux);
    if (now) destroy(o);
}

/* ---- queues, semaphores, mutexes ----------------------------------------- */

static uint32_t queue_new(uint32_t len, uint32_t item, uint8_t type)
{
    obj_t *o = reserve(OBJ_QUEUE);
    if (!o) return 0;
    o->qtype = type;
    o->len = len;
    o->item = item;
    o->ctl = internal(sizeof(StaticQueue_t));
    if (!o->ctl) return unmade(o);
    switch (type) {
    case queueQUEUE_TYPE_MUTEX:
    case queueQUEUE_TYPE_RECURSIVE_MUTEX:
        o->h = xQueueCreateMutexStatic(type, (StaticQueue_t *)o->ctl);
        break;
    case queueQUEUE_TYPE_COUNTING_SEMAPHORE:
        o->h = xQueueCreateCountingSemaphoreStatic(len, item, (StaticQueue_t *)o->ctl);
        o->item = 0;                        /* len is its maximum; item was its first count */
        break;
    default:
        if (item) {
            o->store = outer((size_t)len * item);
            if (!o->store) return unmade(o);
        }
        o->h = xQueueGenericCreateStatic(len, item, o->store, (StaticQueue_t *)o->ctl, type);
        break;
    }
    return o->h ? made(o) : unmade(o);
}

static bool is_mutex(const obj_t *o)
{
    return o->qtype == queueQUEUE_TYPE_MUTEX || o->qtype == queueQUEUE_TYPE_RECURSIVE_MUTEX;
}

/* (length, item size, type): the queue and the binary semaphore. */
H(c_xQueueGenericCreate)
{
    (void)w;
    const uint32_t len = a[0], item = a[1];
    const uint8_t type = (uint8_t)a[2];
    *ret = 0;
    if (len == 0 || (item && len > QUEUE_MAX / item) ||
        !(type == queueQUEUE_TYPE_BASE || type == queueQUEUE_TYPE_BINARY_SEMAPHORE ||
          type == queueQUEUE_TYPE_COUNTING_SEMAPHORE)) {
        return true;
    }
    if (type != queueQUEUE_TYPE_BASE && item) {
        return true;                        /* a semaphore has no items */
    }
    *ret = queue_new(len, item, type);
    return true;
}

H(c_xQueueCreateMutex)
{
    (void)w;
    const uint8_t type = (uint8_t)a[0];
    *ret = (type == queueQUEUE_TYPE_MUTEX || type == queueQUEUE_TYPE_RECURSIVE_MUTEX)
               ? queue_new(1, 0, type) : 0;
    return true;
}

/* (maximum, first count) */
H(c_xQueueCreateCountingSemaphore)
{
    (void)w;
    *ret = (a[0] && a[1] <= a[0]) ? queue_new(a[0], a[1], queueQUEUE_TYPE_COUNTING_SEMAPHORE) : 0;
    return true;
}

H(c_vQueueDelete)
{
    (void)w;
    delete_obj(a[0], OBJ_QUEUE);
    *ret = 0;
    return true;
}

/* (queue, item *, ticks, position). xSemaphoreGive() is this with no item. */
H(c_xQueueGenericSend)
{
    *ret = pdFALSE;
    obj_t *o = use(a[0], OBJ_QUEUE);
    if (!o) return true;
    const BaseType_t pos = (BaseType_t)a[3];
    bool go = (o->item ? ok_ptr(w, a[1], o->item) : true) &&
              pos >= queueSEND_TO_BACK && pos <= queueOVERWRITE &&
              !(pos == queueOVERWRITE && o->len != 1) &&
              o->qtype != queueQUEUE_TYPE_RECURSIVE_MUTEX;     /* xSemaphoreGiveRecursive(), not this */
    void *const me = xTaskGetCurrentTaskHandle();
    if (go && o->qtype == queueQUEUE_TYPE_MUTEX) {
        /* FreeRTOS asserts the giver holds it (xTaskPriorityDisinherit), and
         * the IDF builds it without xQueueGetMutexHolder(): the holder is
         * kept here. Let go of before the give, so that a thread taking it
         * at once is not then forgotten. */
        portENTER_CRITICAL(&s_mux);
        go = o->holder == me;
        if (go) o->holder = NULL;
        portEXIT_CRITICAL(&s_mux);
    }
    if (go) {
        const void *item = o->item ? (const void *)(uintptr_t)a[1] : NULL;
        TickType_t left = (TickType_t)a[2];
        for (;;) {
            const TickType_t s = slice_of(left);
            if (xQueueGenericSend((QueueHandle_t)o->h, item, s, pos) == pdTRUE) {
                *ret = pdTRUE;
                break;
            }
            if (!wait_more(&left, s)) break;
        }
        if (*ret != pdTRUE && o->qtype == queueQUEUE_TYPE_MUTEX) {
            portENTER_CRITICAL(&s_mux);
            o->holder = me;                 /* cannot happen: a held mutex takes a give */
            portEXIT_CRITICAL(&s_mux);
        }
    }
    done(o);
    return true;
}

/* (semaphore, ticks): xSemaphoreTake(), for a mutex too. */
H(c_xQueueSemaphoreTake)
{
    (void)w;
    *ret = pdFALSE;
    obj_t *o = use(a[0], OBJ_QUEUE);
    if (!o) return true;
    if (o->item == 0 && o->qtype != queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        TickType_t left = (TickType_t)a[1];
        for (;;) {
            const TickType_t s = slice_of(left);
            if (xQueueSemaphoreTake((QueueHandle_t)o->h, s) == pdTRUE) {
                *ret = pdTRUE;
                break;
            }
            if (!wait_more(&left, s)) break;
        }
        if (*ret == pdTRUE && o->qtype == queueQUEUE_TYPE_MUTEX) {
            portENTER_CRITICAL(&s_mux);
            o->holder = xTaskGetCurrentTaskHandle();
            portEXIT_CRITICAL(&s_mux);
        }
    }
    done(o);
    return true;
}

static void receive(win_t *w, const uint32_t a[7], uint32_t *ret, bool peek)
{
    *ret = pdFALSE;
    obj_t *o = use(a[0], OBJ_QUEUE);
    if (!o) return;
    if (o->qtype == queueQUEUE_TYPE_BASE && o->item && ok_ptr(w, a[1], o->item)) {
        void *buf = (void *)(uintptr_t)a[1];
        TickType_t left = (TickType_t)a[2];
        for (;;) {
            const TickType_t s = slice_of(left);
            const BaseType_t r = peek ? xQueuePeek((QueueHandle_t)o->h, buf, s)
                                      : xQueueReceive((QueueHandle_t)o->h, buf, s);
            if (r == pdTRUE) {
                *ret = pdTRUE;
                break;
            }
            if (!wait_more(&left, s)) break;
        }
    }
    done(o);
}

/* (queue, buffer *, ticks) */
H(c_xQueueReceive) { receive(w, a, ret, false); return true; }
H(c_xQueuePeek) { receive(w, a, ret, true); return true; }

H(c_uxQueueMessagesWaiting)
{
    (void)w;
    obj_t *o = use(a[0], OBJ_QUEUE);
    *ret = o ? (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)o->h) : 0;
    if (o) done(o);
    return true;
}

/* (queue, new queue?): emptied. Not a mutex's, whose holder would be lost. */
H(c_xQueueGenericReset)
{
    (void)w;
    obj_t *o = use(a[0], OBJ_QUEUE);
    *ret = pdFAIL;
    if (o && !is_mutex(o)) {
        *ret = (uint32_t)xQueueGenericReset((QueueHandle_t)o->h, pdFALSE);
    }
    if (o) done(o);
    return true;
}

/* (mutex, ticks) */
H(c_xQueueTakeMutexRecursive)
{
    (void)w;
    *ret = pdFALSE;
    obj_t *o = use(a[0], OBJ_QUEUE);
    if (!o) return true;
    if (o->qtype == queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        TickType_t left = (TickType_t)a[1];
        for (;;) {
            const TickType_t s = slice_of(left);
            if (xQueueTakeMutexRecursive((QueueHandle_t)o->h, s) == pdTRUE) {
                *ret = pdTRUE;
                break;
            }
            if (!wait_more(&left, s)) break;
        }
    }
    done(o);
    return true;
}

/* (mutex): FreeRTOS itself refuses a task that does not hold it. */
H(c_xQueueGiveMutexRecursive)
{
    (void)w;
    obj_t *o = use(a[0], OBJ_QUEUE);
    *ret = pdFALSE;
    if (o && o->qtype == queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        *ret = (uint32_t)xQueueGiveMutexRecursive((QueueHandle_t)o->h);
    }
    if (o) done(o);
    return true;
}

/* ---- stream buffers ------------------------------------------------------ */

/* in_box: the app's storage, already checked to hold size bytes in the
 * box; 0 for the host's own, in PSRAM.
 *
 * u-feckless-0064: STREAM_MAX caps only the host's storage, which is the
 * host's PSRAM. The app's storage is the app's, already in its box and
 * checked to hold size bytes, and costs the host a control block whatever
 * its size: Defeatist's three 3.5 MB PCM rings were refused by the cap,
 * and its player booted saying "no PCM ring; this boot cannot play
 * audio". */
static uint32_t stream_new(uint32_t size, uint32_t trigger, uint32_t is_msg, uint32_t cb1, uint32_t cb2,
                           uint32_t in_box)
{
    if (size == 0 || (!in_box && size > STREAM_MAX) || trigger > size || is_msg > 1 || cb1 || cb2 ||
        (is_msg && size <= sizeof(size_t))) {
        return 0;                           /* no callbacks: the host cannot run the app's code */
    }
    obj_t *o = reserve(OBJ_STREAM);
    if (!o) return 0;
    o->len = size;
    o->ctl = internal(sizeof(StaticStreamBuffer_t));
    if (!in_box) o->store = outer(size);    /* freed with it; the app's is the app's */
    uint8_t *const store = in_box ? (uint8_t *)(uintptr_t)in_box : o->store;
    if (!o->ctl || !store) return unmade(o);
    o->h = xStreamBufferGenericCreateStatic(size, trigger, (BaseType_t)is_msg, store,
                                            (StaticStreamBuffer_t *)o->ctl, NULL, NULL);
    return o->h ? made(o) : unmade(o);
}

/* (size, trigger, message?, storage *, static *, send callback, receive
 * callback): the app's storage in place if it holds size bytes in the box
 * (u-feckless-0049), else the host's; the app's control block never. */
H(c_xStreamBufferGenericCreateStatic)
{
    const uint32_t in_box = (a[3] && a[0] && ok_ptr(w, a[3], a[0])) ? a[3] : 0;
    *ret = stream_new(a[0], a[1], a[2], a[5], a[6], in_box);
    return true;
}

/* (size, trigger, message?, send callback, receive callback) */
H(c_xStreamBufferGenericCreate)
{
    (void)w;
    *ret = stream_new(a[0], a[1], a[2], a[3], a[4], 0);
    return true;
}

H(c_vStreamBufferDelete)
{
    (void)w;
    delete_obj(a[0], OBJ_STREAM);
    *ret = 0;
    return true;
}

/* (buffer, data *, bytes, ticks): the bytes sent */
H(c_xStreamBufferSend)
{
    *ret = 0;
    obj_t *o = use(a[0], OBJ_STREAM);
    if (!o) return true;
    if (a[2] && ok_ptr(w, a[1], a[2])) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)a[1];
        TickType_t left = (TickType_t)a[3];
        for (;;) {
            const TickType_t s = slice_of(left);
            /* the host's FreeRTOS reads the app's bytes in place: they are in
             * the box, which is all the host is ever let touch for it */
            const size_t n = xStreamBufferSend((StreamBufferHandle_t)o->h, p, a[2], s);
            if (n) {
                *ret = (uint32_t)n;
                break;
            }
            if (!wait_more(&left, s)) break;
        }
    }
    done(o);
    return true;
}

/* (buffer, data *, bytes, ticks): the bytes received */
H(c_xStreamBufferReceive)
{
    *ret = 0;
    obj_t *o = use(a[0], OBJ_STREAM);
    if (!o) return true;
    if (a[2] && ok_ptr(w, a[1], a[2])) {
        uint8_t *p = (uint8_t *)(uintptr_t)a[1];
        TickType_t left = (TickType_t)a[3];
        for (;;) {
            const TickType_t s = slice_of(left);
            const size_t n = xStreamBufferReceive((StreamBufferHandle_t)o->h, p, a[2], s);
            if (n) {
                *ret = (uint32_t)n;
                break;
            }
            if (!wait_more(&left, s)) break;
        }
    }
    done(o);
    return true;
}

#define SB_QUERY(name, expr)                                    \
    H(c_##name)                                                 \
    {                                                           \
        (void)w;                                                \
        obj_t *o = use(a[0], OBJ_STREAM);                       \
        *ret = 0;                                               \
        if (o) {                                                \
            StreamBufferHandle_t h = (StreamBufferHandle_t)o->h; \
            *ret = (uint32_t)(expr);                            \
            done(o);                                            \
        }                                                       \
        return true;                                            \
    }
SB_QUERY(xStreamBufferBytesAvailable, xStreamBufferBytesAvailable(h))
SB_QUERY(xStreamBufferSpacesAvailable, xStreamBufferSpacesAvailable(h))
SB_QUERY(xStreamBufferIsEmpty, xStreamBufferIsEmpty(h))
SB_QUERY(xStreamBufferIsFull, xStreamBufferIsFull(h))
/* pdFAIL while a task waits on it, as FreeRTOS says */
SB_QUERY(xStreamBufferReset, xStreamBufferReset(h))

/* ---- notifications ------------------------------------------------------- */

static void *wake_of(uint32_t id)
{
    thr_t *t = &s_thr[id];
    if (t->wake) return t->wake;
    StaticQueue_t *ctl = internal(sizeof(StaticQueue_t));
    void *s = ctl ? xQueueGenericCreateStatic(1, 0, NULL, ctl, queueQUEUE_TYPE_BINARY_SEMAPHORE) : NULL;
    portENTER_CRITICAL(&s_mux);
    const bool first = !t->wake;
    if (first && s) {
        t->wake = s;
        t->wake_ctl = ctl;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!first && s) {
        vQueueDelete((QueueHandle_t)s);     /* another thread made it meanwhile */
        heap_caps_free(ctl);
    } else if (!s) {
        heap_caps_free(ctl);
    }
    return t->wake;
}

/* (index, clear?, ticks): ulTaskNotifyTake() */
H(c_ulTaskGenericNotifyTake)
{
    const uint32_t id = self_id(w);
    *ret = 0;
    void *wake = (id && a[0] == 0) ? wake_of(id) : NULL;
    if (!wake) return true;
    thr_t *t = &s_thr[id];
    TickType_t left = (TickType_t)a[2];
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const uint32_t v = t->value;
        if (v) {
            t->value = a[1] ? 0 : v - 1;
            t->pending = false;
        }
        portEXIT_CRITICAL(&s_mux);
        if (v) {
            *ret = v;
            break;
        }
        const TickType_t s = slice_of(left);
        if (xQueueSemaphoreTake((QueueHandle_t)wake, s) == pdTRUE) continue;    /* look again */
        if (!wait_more(&left, s)) break;
    }
    return true;
}

/* (index, clear on entry, clear on exit, value *, ticks): xTaskNotifyWait() */
H(c_xTaskGenericNotifyWait)
{
    const uint32_t id = self_id(w);
    *ret = pdFALSE;
    void *wake = (id && a[0] == 0 && (!a[3] || ok_ptr(w, a[3], 4))) ? wake_of(id) : NULL;
    if (!wake) return true;
    thr_t *t = &s_thr[id];
    portENTER_CRITICAL(&s_mux);
    if (!t->pending) t->value &= ~a[1];
    portEXIT_CRITICAL(&s_mux);
    TickType_t left = (TickType_t)a[4];
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const bool got = t->pending;
        const uint32_t v = t->value;
        if (got) {
            t->value &= ~a[2];
            t->pending = false;
        }
        portEXIT_CRITICAL(&s_mux);
        if (got) {
            if (a[3]) memcpy((void *)(uintptr_t)a[3], &v, 4);
            *ret = pdTRUE;
            break;
        }
        const TickType_t s = slice_of(left);
        if (xQueueSemaphoreTake((QueueHandle_t)wake, s) == pdTRUE) continue;
        if (!wait_more(&left, s)) break;
    }
    if (*ret == pdFALSE && a[3]) {
        portENTER_CRITICAL(&s_mux);
        const uint32_t v = t->value;
        portEXIT_CRITICAL(&s_mux);
        memcpy((void *)(uintptr_t)a[3], &v, 4);
    }
    return true;
}

/* (task, index, value, action, previous value *): xTaskNotify(),
 * xTaskNotifyGive() (eIncrement) and the rest. pdFAIL for a thread that is
 * not running, and for eSetValueWithoutOverwrite on one already notified. */
H(c_xTaskGenericNotify)
{
    *ret = pdFAIL;
    const uint32_t id = IS_ID(a[0]) ? a[0] : 0;
    if (!id || a[1] != 0 || a[3] > eSetValueWithoutOverwrite || (a[4] && !ok_ptr(w, a[4], 4))) {
        return true;
    }
    uint32_t lo, hi;
    if (!feckless_umode_app_thread_stack(feckless_umode_app_loaded(), id, &lo, &hi)) {
        return true;                        /* not running (read under the lock: a locked call) */
    }
    void *wake = wake_of(id);
    if (!wake) return true;
    thr_t *t = &s_thr[id];
    bool ok = true;
    portENTER_CRITICAL(&s_mux);
    const uint32_t prev = t->value;
    switch ((eNotifyAction)a[3]) {
    case eNoAction: break;
    case eSetBits: t->value |= a[2]; break;
    case eIncrement: t->value++; break;
    case eSetValueWithOverwrite: t->value = a[2]; break;
    case eSetValueWithoutOverwrite:
        if (t->pending) ok = false;
        else t->value = a[2];
        break;
    }
    if (ok) t->pending = true;
    portEXIT_CRITICAL(&s_mux);
    if (a[4]) memcpy((void *)(uintptr_t)a[4], &prev, 4);
    if (ok) xQueueGenericSend((QueueHandle_t)wake, NULL, 0, queueSEND_TO_BACK);
    *ret = ok ? pdPASS : pdFAIL;
    return true;
}

/* ---- critical sections --------------------------------------------------- */

static void *crit(void)
{
    if (s_crit) return s_crit;
    StaticQueue_t *ctl = internal(sizeof(StaticQueue_t));
    void *m = ctl ? xQueueCreateMutexStatic(queueQUEUE_TYPE_RECURSIVE_MUTEX, ctl) : NULL;
    portENTER_CRITICAL(&s_mux);
    const bool first = !s_crit;
    if (first) {
        s_crit = m;
        s_crit_ctl = ctl;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!first) {
        if (m) vQueueDelete((QueueHandle_t)m);
        heap_caps_free(ctl);
    } else if (!m) {
        heap_caps_free(ctl);
        s_crit_ctl = NULL;
    }
    return s_crit;
}

static BaseType_t crit_enter(TickType_t ticks)
{
    void *m = crit();
    if (!m) return pdFAIL;
    TickType_t left = ticks;
    for (;;) {
        const TickType_t s = slice_of(left);
        if (xQueueTakeMutexRecursive((QueueHandle_t)m, s) == pdTRUE) return pdPASS;
        if (!wait_more(&left, s)) return pdFAIL;
    }
}

/* (mux *, timeout): portENTER_CRITICAL() is this with portMUX_NO_TIMEOUT;
 * any other timeout (spin cycles, to FreeRTOS) is one try. */
H(c_xPortEnterCriticalTimeout)
{
    (void)w;
    *ret = (uint32_t)crit_enter((int32_t)a[1] < 0 ? portMAX_DELAY : 0);
    return true;
}
H(c_vPortEnterCritical)
{
    (void)w; (void)a;
    crit_enter(portMAX_DELAY);
    *ret = 0;
    return true;
}
static void crit_exit(void)
{
    void *m = s_crit;
    if (m) xQueueGiveMutexRecursive((QueueHandle_t)m);  /* refused unless this task holds it */
}
H(c_vPortExitCriticalMultiCore) { (void)w; (void)a; crit_exit(); *ret = 0; return true; }
H(c_vPortExitCritical) { (void)w; (void)a; crit_exit(); *ret = 0; return true; }

/* No app code runs in an interrupt. */
H(c_xPortInIsrContext) { (void)w; (void)a; *ret = 0; return true; }

/* ---- tasks --------------------------------------------------------------- */

H(c_xTaskGetTickCount) { (void)w; (void)a; *ret = (uint32_t)xTaskGetTickCount(); return true; }
H(c_xTaskGetCurrentTaskHandle) { (void)a; *ret = self_id(w); return true; }

/* The calls below take a TaskHandle_t, and read a thread's host task or
 * stack: locked, since a thread frees both under the app's lock as it ends. */

/* (task): the name the app gave it, copied into the box; "main" for
 * app_main. NULL for a handle that is not a thread. */
H(c_pcTaskGetName)
{
    const uint32_t id = id_of(w, a[0]);
    *ret = 0;
    if (!id) return true;
    thr_t *t = &s_thr[id];
    feckless_box_t *b = box_of_win(w);
    if (!t->name_box || feckless_box_len(b, t->name_box) != NAME_LEN) {
        t->name_box = (uint32_t)feckless_box_alloc(b, NAME_LEN);
    }
    if (!t->name_box) return true;
    const char *n = id == FECKLESS_UMODE_MAIN_THREAD && !t->name[0] ? "main" : t->name;
    char *d = (char *)(uintptr_t)t->name_box;
    strncpy(d, n, NAME_LEN - 1);
    d[NAME_LEN - 1] = '\0';
    *ret = t->name_box;
    return true;
}

/* (task): bytes of its stack in the box never used -- the run of
 * FECKLESS_UMODE_STACK_FILL at the bottom, as FreeRTOS measures. */
H(c_uxTaskGetStackHighWaterMark)
{
    uint32_t lo, hi;
    *ret = 0;
    if (!feckless_umode_app_thread_stack(feckless_umode_app_loaded(), id_of(w, a[0]), &lo, &hi)) {
        return true;
    }
    const uint8_t *p = (const uint8_t *)(uintptr_t)lo;
    uint32_t n = 0;
    while (lo + n < hi && p[n] == FECKLESS_UMODE_STACK_FILL) n++;
    *ret = n;
    return true;
}

/* (task): the lowest address of its stack in the box */
H(c_xTaskGetStackStart)
{
    uint32_t lo, hi;
    *ret = feckless_umode_app_thread_stack(feckless_umode_app_loaded(), id_of(w, a[0]), &lo, &hi) ? lo : 0;
    return true;
}

H(c_uxTaskPriorityGet)
{
    TaskHandle_t t = (TaskHandle_t)feckless_umode_app_thread_task(feckless_umode_app_loaded(), id_of(w, a[0]));
    *ret = t ? (uint32_t)uxTaskPriorityGet(t) : 0;
    return true;
}

/* (task, priority), clamped as a thread's is at spawn */
H(c_vTaskPrioritySet)
{
    TaskHandle_t t = (TaskHandle_t)feckless_umode_app_thread_task(feckless_umode_app_loaded(), id_of(w, a[0]));
    int p = (int)a[1];
    if (p < 1) p = 1;
    if (p > configMAX_PRIORITIES - 2) p = configMAX_PRIORITIES - 2;
    if (t) vTaskPrioritySet(t, (UBaseType_t)p);
    *ret = 0;
    return true;
}

/* (task): eRunning for the caller, eDeleted for a thread that has ended,
 * else its host task's state (blocked in a host call, or ready). */
H(c_eTaskGetState)
{
    const uint32_t id = id_of(w, a[0]);
    TaskHandle_t t = (TaskHandle_t)feckless_umode_app_thread_task(feckless_umode_app_loaded(), id);
    if (!t) *ret = eDeleted;
    else if (id == self_id(w)) *ret = eRunning;
    else *ret = (uint32_t)eTaskGetState(t);
    return true;
}

/* (task): only the caller, which then waits until the app stops. Another
 * thread is in a window or a host call, and neither can be stopped from
 * here (u-feckless-0023): ignored, and said once. */
H(c_vTaskSuspend)
{
    *ret = 0;
    if (a[0] == 0 || a[0] == self_id(w)) {
        TickType_t left = portMAX_DELAY;
        do {
            vTaskDelay(slice_of(left));
        } while (wait_more(&left, slice_of(left)));
        return true;
    }
    if (!s_warned_suspend) {
        s_warned_suspend = true;
        ESP_LOGW("box", "vTaskSuspend() of another thread: ignored");
    }
    return true;
}

/* (fn, name, stack bytes, param, priority, stack *, TCB *, core [t0]): the
 * app's stack and TCB are not used; the thread's stack comes from the box's
 * heap, as xTaskCreatePinnedToCore()'s does. The handle, or NULL. */
H(c_xTaskCreateStaticPinnedToCore)
{
    bool ok = true;
    const int core = box_rtos_core((int)feckless_umode_arg(w, 7, &ok));
    uint32_t id = 0;
    const esp_err_t e = feckless_umode_app_spawn(feckless_umode_app_loaded(), a[0], a[3], a[2],
                                                 (int)a[4], core, &id);
    if (e != ESP_OK) {
        ESP_LOGE("box", "xTaskCreateStaticPinnedToCore: %s", esp_err_to_name(e));
        *ret = 0;
        return true;
    }
    box_rtos_named(w, id, a[1]);
    *ret = id;
    return true;
}

/* ---- for box_calls.c ------------------------------------------------------ */

/* tskNO_AFFINITY is the calling thread's core. A window holds interrupts off
 * on its core, and app_main runs away from the panel's (u-feckless-0031); a
 * thread FreeRTOS were free to put on core 0 would starve the panel there. */
int box_rtos_core(int asked)
{
    return asked == (int)tskNO_AFFINITY ? (int)xPortGetCoreID() : asked;
}

/* A new thread id: its name, and a notification state of its own. */
void box_rtos_named(win_t *w, uint32_t id, uint32_t name)
{
    if (!IS_ID(id)) return;
    thr_t *t = &s_thr[id];
    portENTER_CRITICAL(&s_mux);
    t->value = 0;
    t->pending = false;
    portEXIT_CRITICAL(&s_mux);
    t->name[0] = '\0';
    const char *s = name ? str(w, name, 256) : NULL;
    if (s) {
        strncpy(t->name, s, NAME_LEN - 1);
        t->name[NAME_LEN - 1] = '\0';
    }
}

#define RTOS_CALLS                                                                         \
    FN(xQueueGenericCreate), FN(xQueueCreateMutex), FN(xQueueCreateCountingSemaphore),     \
    FN(vQueueDelete), FNU(xQueueGenericSend), FNU(xQueueSemaphoreTake), FNU(xQueueReceive), \
    FNU(xQueuePeek), FNU(uxQueueMessagesWaiting), FN(xQueueGenericReset),                  \
    FNU(xQueueTakeMutexRecursive), FNU(xQueueGiveMutexRecursive),                          \
    FN(xStreamBufferGenericCreateStatic), FN(xStreamBufferGenericCreate),                  \
    FN(vStreamBufferDelete), FNU(xStreamBufferSend), FNU(xStreamBufferReceive),            \
    FNU(xStreamBufferBytesAvailable), FNU(xStreamBufferSpacesAvailable),                   \
    FNU(xStreamBufferIsEmpty), FNU(xStreamBufferIsFull), FN(xStreamBufferReset),           \
    FNU(ulTaskGenericNotifyTake), FNU(xTaskGenericNotifyWait), FN(xTaskGenericNotify),     \
    FNU(xPortEnterCriticalTimeout), FNU(vPortEnterCritical),                               \
    FNU(vPortExitCriticalMultiCore), FNU(vPortExitCritical), FNU(xPortInIsrContext),       \
    FNU(xTaskGetTickCount), FNU(xTaskGetCurrentTaskHandle), FN(pcTaskGetName),             \
    FN(uxTaskGetStackHighWaterMark), FN(xTaskGetStackStart), FN(uxTaskPriorityGet),        \
    FN(vTaskPrioritySet), FN(eTaskGetState), FNU(vTaskSuspend),                            \
    FN(xTaskCreateStaticPinnedToCore)

const feckless_umode_call_t box_rtos_calls[] = { RTOS_CALLS };
const uint32_t box_rtos_calls_n = sizeof box_rtos_calls / sizeof box_rtos_calls[0];

/* After the app is unloaded: every thread has ended, so nothing is inside
 * an object. */
void box_rtos_end(void)
{
    for (int i = 0; i < MAX_OBJ; i++) {
        if (s_obj[i].kind != OBJ_FREE) destroy(&s_obj[i]);
    }
    for (int i = 0; i < NTHREADS; i++) {
        if (s_thr[i].wake) vQueueDelete((QueueHandle_t)s_thr[i].wake);
        heap_caps_free(s_thr[i].wake_ctl);
    }
    memset(s_thr, 0, sizeof s_thr);
    if (s_crit) vQueueDelete((QueueHandle_t)s_crit);
    heap_caps_free(s_crit_ctl);
    s_crit = s_crit_ctl = NULL;
    s_warned_suspend = false;
}
