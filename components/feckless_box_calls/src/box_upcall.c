/* SPDX-License-Identifier: MIT
 *
 * Callbacks into a boxed app (u-feckless-0075): the host asks, the app's
 * own thread calls.
 *
 * A box never calls into its app from a host task: the app's code runs in
 * windows on the app's threads, and nowhere else. But much of IDF is
 * callbacks -- an HTTP server's handlers, its open_fn and queued work, an
 * HID report, a download's progress -- and an app that registers one
 * expects it called. So the app keeps a thread for them:
 *
 * - feckless_box_libc's src/box_upcall.c, linked into every boxed ELF that
 *   allocates, has a constructor that tells the host where its loop is
 *   (feckless_box_upcall_loop_is). Nothing runs yet.
 * - A host call that registers something needing callbacks (httpd_start,
 *   box_httpd.c) calls box_upcall_start() on the app's thread, under the
 *   app's lock: the loop's thread is spawned then, once, with the stack
 *   and priority asked for (the largest stack asked by the first start;
 *   the thread does not grow).
 * - The loop asks feckless_box_upcall_next() for work: {fn, four words}.
 *   It calls fn with them, and hands back what fn returned on its next
 *   ask. next() is unlocked, and waits in 100 ms slices for work, ending
 *   the loop (0) when the app stops.
 * - A host task with something for the app calls box_upcall(): one at a
 *   time, in order of asking; it waits for its call's answer, however long
 *   the app's function takes (a handler that fetches from the net may take
 *   seconds), and gives up only when the app stops -- ESP_ERR_INVALID_STATE,
 *   in at most 100 ms. The app's function may itself make host calls,
 *   locked ones too: the asking task holds no lock while it waits.
 *
 * What a caller must not do: ask from the loop's own thread (its host
 * calls run on the loop's task, so the call could never be answered --
 * refused, ESP_ERR_INVALID_STATE, and logged), or hold the app's lock while
 * it waits (the app's function may need it -- so a host call that waits
 * for something which waits for a callback, httpd_stop() say, is UNLOCKED).
 *
 * One loop, so callbacks run one at a time, in order: what an HTTP server
 * does with its one task anyway.
 */
#include <string.h>

#include "box_calls_priv.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "feckless_umode_load.h"

static const char *TAG = "box_upcall";

#define SLICE_MS  100

/* What the loop is handed: feckless_box_libc's box_upcall_t, the same. */
typedef struct {
    uint32_t fn;
    uint32_t a[4];
} box_upcall_t;

static uint32_t s_loop;                     /* the app's loop, 0 if it said none */
static uint32_t s_thread;                   /* its thread's id, 0 not spawned */
static TaskHandle_t s_loop_task;            /* the host task it runs on, as seen in next() */
static SemaphoreHandle_t s_serial;          /* one call at a time */
static SemaphoreHandle_t s_work;            /* given: a call is posted */
static SemaphoreHandle_t s_done;            /* given: an answer is in */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Under s_mux. */
static bool s_posted;
static box_upcall_t s_post;
static uint32_t s_post_seq, s_seq;          /* the posted call's number; the last given */
static uint32_t s_running;                  /* the call the loop has, 0 none */
static uint32_t s_answered, s_ret;          /* the last call answered, and its answer */

static bool stopping(void) { return feckless_umode_app_stopping(feckless_umode_app_loaded()); }

/* (loop): the constructor's. A second is the same app's again: the last. */
H(c_feckless_box_upcall_loop_is)
{
    *ret = 0;
    if (!ok_ptr(w, a[0], 4)) {
        ESP_LOGE(TAG, "the app's callback loop is not in its box; no callbacks");
        return true;
    }
    s_loop = a[0];
    return true;
}

/* (box_upcall_t *u, last answer) -> 1, a call in *u; 0, end the loop.
 * Unlocked: waits for work for as long as the app runs. */
H(c_feckless_box_upcall_next)
{
    *ret = 0;
    if (!ok_ptr(w, a[0], sizeof(box_upcall_t)) || !s_work) return true;
    s_loop_task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_mux);
    const bool answer = s_running != 0;
    if (answer) {
        s_answered = s_running;
        s_ret = a[1];
        s_running = 0;
    }
    portEXIT_CRITICAL(&s_mux);
    if (answer) xSemaphoreGive(s_done);
    for (;;) {
        if (stopping()) return true;
        if (xSemaphoreTake(s_work, pdMS_TO_TICKS(SLICE_MS)) != pdTRUE) continue;
        box_upcall_t got;
        portENTER_CRITICAL(&s_mux);
        const bool have = s_posted;
        if (have) {
            got = s_post;
            s_running = s_post_seq;
            s_posted = false;
        }
        portEXIT_CRITICAL(&s_mux);
        if (!have) continue;                /* given for a call since withdrawn */
        memcpy((void *)(uintptr_t)a[0], &got, sizeof got);
        *ret = 1;
        return true;
    }
}

const feckless_umode_call_t box_upcall_calls[] = {
    FN(feckless_box_upcall_loop_is),
    FNU(feckless_box_upcall_next),
};
const uint32_t box_upcall_calls_n = sizeof box_upcall_calls / sizeof box_upcall_calls[0];

bool box_upcall_ready(void) { return s_thread != 0; }

/* u-feckless-0081: the caller is the loop's own host task -- a callback
 * asked for now could never be answered, so the caller defers it. */
bool box_upcall_on_loop(void) { return s_loop_task && xTaskGetCurrentTaskHandle() == s_loop_task; }

esp_err_t box_upcall_start(win_t *w, uint32_t stack, int prio)
{
    (void)w;
    if (s_thread) return ESP_OK;
    if (!s_loop) {
        ESP_LOGE(TAG, "this app has no callback loop (its ELF links no feckless_box_libc "
                      "allocator, or predates u-feckless-0075); no callbacks");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_serial) s_serial = xSemaphoreCreateBinary();
    if (!s_work) s_work = xSemaphoreCreateBinary();
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_serial || !s_work || !s_done) return ESP_ERR_NO_MEM;
    xSemaphoreGive(s_serial);
    uint32_t id = 0;
    const esp_err_t e = feckless_umode_app_spawn(feckless_umode_app_loaded(), s_loop, 0, stack, prio,
                                                 box_rtos_core((int)tskNO_AFFINITY), &id);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "the callback loop's thread: %s", esp_err_to_name(e));
        return e;
    }
    s_thread = id;
    ESP_LOGI(TAG, "callbacks into the app on thread %u (%u bytes of stack, priority %d)",
             (unsigned)id, (unsigned)stack, prio);
    return ESP_OK;
}

/* Withdraw a call not yet taken; true if it was. */
static bool withdraw(uint32_t seq)
{
    portENTER_CRITICAL(&s_mux);
    const bool was = s_posted && s_post_seq == seq;
    if (was) s_posted = false;
    portEXIT_CRITICAL(&s_mux);
    return was;
}

esp_err_t box_upcall(uint32_t fn, const uint32_t args[4], uint32_t *ret)
{
    if (!s_thread) return ESP_ERR_NOT_SUPPORTED;
    if (s_loop_task && xTaskGetCurrentTaskHandle() == s_loop_task) {
        ESP_LOGE(TAG, "a callback asked for from the callback loop's own thread: refused");
        return ESP_ERR_INVALID_STATE;
    }
    while (xSemaphoreTake(s_serial, pdMS_TO_TICKS(SLICE_MS)) != pdTRUE) {
        if (stopping()) return ESP_ERR_INVALID_STATE;
    }
    if (stopping()) {
        xSemaphoreGive(s_serial);
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_mux);
    const uint32_t seq = ++s_seq ? s_seq : ++s_seq;     /* never 0: s_running's none */
    s_post.fn = fn;
    for (int i = 0; i < 4; i++) s_post.a[i] = args ? args[i] : 0;
    s_post_seq = seq;
    s_posted = true;
    portEXIT_CRITICAL(&s_mux);
    xSemaphoreGive(s_work);

    esp_err_t e = ESP_OK;
    for (;;) {
        const bool given = xSemaphoreTake(s_done, pdMS_TO_TICKS(SLICE_MS)) == pdTRUE;
        portENTER_CRITICAL(&s_mux);
        const bool mine = s_answered == seq;
        const uint32_t r = s_ret;
        portEXIT_CRITICAL(&s_mux);
        if (mine) {
            if (ret) *ret = r;
            break;
        }
        (void)given;                        /* an older answer's give: keep waiting */
        if (stopping()) {
            (void)withdraw(seq);
            e = ESP_ERR_INVALID_STATE;
            break;
        }
    }
    xSemaphoreGive(s_serial);
    return e;
}

void box_upcall_end(void)
{
    /* The app's threads are gone; the next app says where its loop is. The
     * semaphores stay, emptied. */
    s_loop = 0;
    s_thread = 0;
    s_loop_task = NULL;
    portENTER_CRITICAL(&s_mux);
    s_posted = false;
    s_running = 0;
    s_answered = 0;
    portEXIT_CRITICAL(&s_mux);
    if (s_work) xSemaphoreTake(s_work, 0);
    if (s_done) xSemaphoreTake(s_done, 0);
    if (s_serial) {
        xSemaphoreTake(s_serial, 0);
        xSemaphoreGive(s_serial);
    }
}
