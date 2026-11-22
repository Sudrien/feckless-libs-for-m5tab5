/* SPDX-License-Identifier: MIT
 *
 * feckless_fetch for a boxed app (u-feckless-0081): the launcher's
 * downloader, its tasks, its writer's lease on the card, its part files;
 * the app's callback run by the app's callback loop.
 *
 * - feckless_fetch_start() copies the app's options into the launcher --
 *   url, path, part, user agent, the numbers -- and, if the app gave a
 *   callback, puts the launcher's own in its place: each event is copied
 *   into one block in the app's box (the event, the url and Content-Type
 *   it names, the part file, and a path buffer the app may rewrite), and
 *   the callback loop (u-feckless-0075) runs the app's callback on it
 *   while the download's task waits. A HEADERS callback's new path is
 *   copied back. One event at a time, as feckless_fetch delivers them.
 * - CANCELLED of a paused or failed download comes on the task that
 *   called feckless_fetch_cancel() -- which may be the loop itself, when
 *   an HTTP handler cancels (Defeatist's remote does). A callback asked
 *   from the loop's own task could never run, so that one is delivered a
 *   moment later from a short task of the launcher's; its return is
 *   ignored anyway.
 * - When the app stops mid-callback the event goes unanswered: true, so
 *   a VERIFY the app never got to judge does not throw away a whole
 *   file. At the app's end a download it started is paused -- its part
 *   file kept, to resume at the next start -- and no callback is made.
 * - Everything but start is unlocked: discard, part and live touch the
 *   card, which waits for the storage lease, which an app thread may hold
 *   while it waits for the app's lock (u-feckless-0072). start allocates
 *   the event block and spawns the loop under the lock, then lets the
 *   lock go for feckless_fetch_start() itself.
 *
 * In the network section; built where feckless_fetch.h is (the launcher
 * links feckless_network_handler).
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("feckless_fetch.h") && !defined(FECKLESS_BOX_NO_NET)

#include <stdlib.h>

#include "esp_log.h"
#include "feckless_fetch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "feckless_umode_box.h"
#include "feckless_umode_load.h"

static const char *TAG = "box_fetch";

#define UA_MAX     128
#define CTYPE_MAX  128
#define LOOP_STACK (12 * 1024)
#define LATER_STACK 3072

/* The event as the app sees it, in its box. */
typedef struct {
    feckless_fetch_ev_t ev;
    char url[FECKLESS_FETCH_URL_MAX];
    char ctype[CTYPE_MAX];
    char part[FECKLESS_FETCH_PATH_MAX];
    char path[FECKLESS_FETCH_PATH_MAX];
} box_ev_t;

static uint32_t s_cb;                       /* the app's callback, 0 none */
static uint32_t s_arg;                      /* its arg */
static uint32_t s_block;                    /* box_ev_t in the box, kept for the app's life */
static bool s_mine;                         /* this app started the download running now */
static SemaphoreHandle_t s_one;             /* one event at a time */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void one_lock(void)
{
    if (!s_one) {
        SemaphoreHandle_t m = xSemaphoreCreateBinary();
        if (m) xSemaphoreGive(m);
        bool mine = false;
        portENTER_CRITICAL(&s_mux);
        if (!s_one) { s_one = m; mine = true; }
        portEXIT_CRITICAL(&s_mux);
        if (!mine && m) vSemaphoreDelete(m);
    }
    xSemaphoreTake(s_one, portMAX_DELAY);
}
static void one_unlock(void) { xSemaphoreGive(s_one); }

static void copy(char *dst, size_t cap, const char *src)
{
    const size_t n = src ? strnlen(src, cap - 1) : 0;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

/* The app's callback on a copy of ev; true unless it said false. */
static bool deliver(feckless_fetch_ev_t *ev)
{
    portENTER_CRITICAL(&s_mux);
    const uint32_t cb = s_cb, arg = s_arg, block = s_block;
    portEXIT_CRITICAL(&s_mux);
    if (!cb || !block) return true;
    one_lock();
    box_ev_t *b = (box_ev_t *)(uintptr_t)block;
    memset(&b->ev, 0, sizeof b->ev);
    copy(b->url, sizeof b->url, ev->url);
    copy(b->ctype, sizeof b->ctype, ev->ctype);
    copy(b->part, sizeof b->part, ev->part);
    copy(b->path, sizeof b->path, ev->path);
    b->ev.kind = ev->kind;
    b->ev.id = ev->id;
    b->ev.url = ev->url ? b->url : NULL;
    b->ev.ctype = ev->ctype ? b->ctype : NULL;
    b->ev.part = ev->part ? b->part : NULL;
    b->ev.path = ev->path ? b->path : NULL;
    b->ev.path_size = ev->path ? sizeof b->path : 0;
    b->ev.size = ev->size;
    b->ev.arg = (void *)(uintptr_t)arg;
    uint32_t r = 1;
    const esp_err_t e = box_upcall(cb, (const uint32_t[4]){ block, 0, 0, 0 }, &r);
    if (e == ESP_OK && ev->kind == FECKLESS_FETCH_EV_HEADERS && ev->path && ev->path_size) {
        /* the app may have written into the path buffer, or pointed path
         * at a string of its own: either, if it lies in the box */
        const char *np = b->ev.path;
        if (np == b->path) {
            b->path[sizeof b->path - 1] = '\0';
            copy(ev->path, ev->path_size, b->path);
        } else if (np) {
            const uint32_t at = (uint32_t)(uintptr_t)np;
            if (at >= block && at < block + sizeof *b) copy(ev->path, ev->path_size, np);
            else ESP_LOGW(TAG, "HEADERS: the app's new path is not its event's; kept the old");
        }
    }
    one_unlock();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "event %d: the app did not answer (%s); taken as yes", (int)ev->kind, esp_err_to_name(e));
        return true;
    }
    return (r & 0xff) != 0;                 /* a bool in a0 */
}

/* CANCELLED asked from the loop's own task: delivered from this one. */
typedef struct {
    feckless_fetch_ev_t ev;
    char part[FECKLESS_FETCH_PATH_MAX];
    char path[FECKLESS_FETCH_PATH_MAX];
} later_t;

static void later_task(void *arg)
{
    later_t *l = arg;
    (void)deliver(&l->ev);
    free(l);
    vTaskDelete(NULL);
}

static bool on_event(feckless_fetch_ev_t *ev)
{
    if (box_upcall_on_loop()) {
        later_t *l = calloc(1, sizeof *l);
        if (!l) return true;
        l->ev = *ev;
        copy(l->part, sizeof l->part, ev->part);
        copy(l->path, sizeof l->path, ev->path);
        l->ev.part = ev->part ? l->part : NULL;
        l->ev.path = ev->path ? l->path : NULL;
        l->ev.path_size = ev->path ? sizeof l->path : 0;
        l->ev.url = l->ev.ctype = NULL;     /* CANCELLED names neither that matters */
        if (xTaskCreate(later_task, "box_fetch", LATER_STACK, l, 4, NULL) != pdPASS) free(l);
        return true;
    }
    return deliver(ev);
}

/* (opts *) -> bool */
H(c_feckless_fetch_start)
{
    *ret = 0;
    if (!ok_ptr(w, a[0], sizeof(feckless_fetch_opts_t))) return true;
    feckless_fetch_opts_t in;
    memcpy(&in, (const void *)(uintptr_t)a[0], sizeof in);
    const char *u = in.url ? str(w, (uint32_t)(uintptr_t)in.url, FECKLESS_FETCH_URL_MAX) : NULL;
    const char *p = in.path ? str(w, (uint32_t)(uintptr_t)in.path, FECKLESS_FETCH_PATH_MAX) : NULL;
    const char *pt = in.part ? str(w, (uint32_t)(uintptr_t)in.part, FECKLESS_FETCH_PATH_MAX) : NULL;
    const char *ag = in.user_agent ? str(w, (uint32_t)(uintptr_t)in.user_agent, UA_MAX) : NULL;
    if ((in.url && !u) || (in.path && !p) || (in.part && !pt) || (in.user_agent && !ag)) {
        ESP_LOGW(TAG, "start: a string of the options is not in the box, or too long");
        return true;
    }
    const uint32_t cb = (uint32_t)(uintptr_t)in.cb;
    if (cb) {
        if (!ok_ptr(w, cb, 4)) return true;
        if (box_upcall_start(w, LOOP_STACK, 5) != ESP_OK) return true;
        if (!s_block) s_block = feckless_box_alloc(box_of_win(w), sizeof(box_ev_t));
        if (!s_block) {
            ESP_LOGW(TAG, "start: no room in the box for the event");
            return true;
        }
    }
    /* the strings copied out of the box before the lock goes: another
     * thread of the app may change them meanwhile */
    typedef struct {
        char url[FECKLESS_FETCH_URL_MAX], path[FECKLESS_FETCH_PATH_MAX], part[FECKLESS_FETCH_PATH_MAX], ua[UA_MAX];
    } strs_t;
    strs_t *t = malloc(sizeof *t);
    if (!t) return true;
    copy(t->url, sizeof t->url, u);
    copy(t->path, sizeof t->path, p);
    copy(t->part, sizeof t->part, pt);
    copy(t->ua, sizeof t->ua, ag);
    feckless_fetch_opts_t o = in;
    o.url = u ? t->url : NULL;
    o.path = p ? t->path : NULL;
    o.part = pt ? t->part : NULL;
    o.user_agent = ag ? t->ua : NULL;
    o.cb = cb ? on_event : NULL;
    o.arg = NULL;
    portENTER_CRITICAL(&s_mux);
    const uint32_t was_cb = s_cb, was_arg = s_arg;
    s_cb = cb;
    s_arg = (uint32_t)(uintptr_t)in.arg;
    portEXIT_CRITICAL(&s_mux);
    /* START comes on the download's task at once, and its callback may
     * need the app's lock: not held across the start (which copies the
     * options before it returns) */
    feckless_umode_app_t *app = feckless_umode_app_loaded();
    feckless_umode_app_unlock(app);
    const bool ok = feckless_fetch_start(&o);
    feckless_umode_app_lock(app);
    free(t);
    if (ok) {
        s_mine = true;
    } else {                                /* refused: one already running keeps its callback */
        portENTER_CRITICAL(&s_mux);
        s_cb = was_cb;
        s_arg = was_arg;
        portEXIT_CRITICAL(&s_mux);
    }
    *ret = ok;
    return true;
}

H(c_feckless_fetch_pause) { (void)w; (void)a; feckless_fetch_pause(); *ret = 0; return true; }
H(c_feckless_fetch_cancel) { (void)w; (void)a; feckless_fetch_cancel(); *ret = 0; return true; }

/* (id *, got *, total *, path, path_size, gen *) */
H(c_feckless_fetch_state)
{
    *ret = (uint32_t)FECKLESS_FETCH_IDLE;
    if ((a[0] && !ok_ptr(w, a[0], 8)) || (a[1] && !ok_ptr(w, a[1], 8)) || (a[2] && !ok_ptr(w, a[2], 8)) ||
        (a[3] && a[4] && !ok_ptr(w, a[3], a[4])) || (a[5] && !ok_ptr(w, a[5], 4)))
        return true;
    int64_t id = 0, got = 0, total = 0;
    uint32_t gen = 0;
    char path[FECKLESS_FETCH_PATH_MAX];
    const size_t room = a[3] && a[4] ? (a[4] < sizeof path ? a[4] : sizeof path) : 0;
    const feckless_fetch_state_t st = feckless_fetch_state(&id, &got, &total, room ? path : NULL, room, &gen);
    if (a[0]) memcpy((void *)(uintptr_t)a[0], &id, 8);
    if (a[1]) memcpy((void *)(uintptr_t)a[1], &got, 8);
    if (a[2]) memcpy((void *)(uintptr_t)a[2], &total, 8);
    if (a[5]) memcpy((void *)(uintptr_t)a[5], &gen, 4);
    if (room) {
        path[room - 1] = '\0';
        memcpy((void *)(uintptr_t)a[3], path, strlen(path) + 1);
    }
    *ret = (uint32_t)st;
    return true;
}

/* (out, size) */
H(c_feckless_fetch_why)
{
    *ret = 0;
    if (!a[1] || !ok_ptr(w, a[0], a[1])) return true;
    char why[128];
    feckless_fetch_why(why, sizeof why);
    copy((char *)(uintptr_t)a[0], a[1], why);
    return true;
}

/* (path, part, part_size, int64 *ready, int64 *total, state *) -> bool */
H(c_feckless_fetch_live)
{
    *ret = 0;
    const char *p = str(w, a[0], FECKLESS_FETCH_PATH_MAX);
    if (!p || (a[1] && a[2] && !ok_ptr(w, a[1], a[2])) || (a[3] && !ok_ptr(w, a[3], 8)) ||
        (a[4] && !ok_ptr(w, a[4], 8)) || (a[5] && !ok_ptr(w, a[5], sizeof(feckless_fetch_state_t))))
        return true;
    char path[FECKLESS_FETCH_PATH_MAX], part[FECKLESS_FETCH_PATH_MAX];
    copy(path, sizeof path, p);
    const size_t room = a[1] && a[2] ? (a[2] < sizeof part ? a[2] : sizeof part) : 0;
    int64_t ready = 0, total = 0;
    feckless_fetch_state_t st = FECKLESS_FETCH_IDLE;
    const bool r = feckless_fetch_live(path, room ? part : NULL, room, &ready, &total, &st);
    if (room) {
        part[room - 1] = '\0';
        memcpy((void *)(uintptr_t)a[1], part, strlen(part) + 1);
    }
    if (a[3]) memcpy((void *)(uintptr_t)a[3], &ready, 8);
    if (a[4]) memcpy((void *)(uintptr_t)a[4], &total, 8);
    if (a[5]) memcpy((void *)(uintptr_t)a[5], &st, sizeof st);
    *ret = r;
    return true;
}

/* (part, int64 *size) -> bool */
H(c_feckless_fetch_part)
{
    *ret = 0;
    const char *p = str(w, a[0], FECKLESS_FETCH_PATH_MAX);
    if (!p || (a[1] && !ok_ptr(w, a[1], 8))) return true;
    char part[FECKLESS_FETCH_PATH_MAX];
    copy(part, sizeof part, p);
    int64_t size = 0;
    *ret = feckless_fetch_part(part, &size);
    if (a[1]) memcpy((void *)(uintptr_t)a[1], &size, 8);
    return true;
}

/* (part) */
H(c_feckless_fetch_discard)
{
    *ret = 0;
    const char *p = str(w, a[0], FECKLESS_FETCH_PATH_MAX);
    if (!p) return true;
    char part[FECKLESS_FETCH_PATH_MAX];
    copy(part, sizeof part, p);
    feckless_fetch_discard(part);
    return true;
}

const feckless_umode_call_t box_fetch_calls[] = {
    FN(feckless_fetch_start), FNU(feckless_fetch_pause), FNU(feckless_fetch_cancel),
    FNU(feckless_fetch_state), FNU(feckless_fetch_why), FNU(feckless_fetch_live),
    FNU(feckless_fetch_part), FNU(feckless_fetch_discard),
};
const uint32_t box_fetch_calls_n = sizeof box_fetch_calls / sizeof box_fetch_calls[0];

/* The app has ended: no more callbacks; a download it left running is
 * paused, its part file kept for the next start. */
void box_fetch_end(void)
{
    portENTER_CRITICAL(&s_mux);
    s_cb = 0;
    s_arg = 0;
    s_block = 0;                            /* went with the box */
    portEXIT_CRITICAL(&s_mux);
    if (s_mine && feckless_fetch_state(NULL, NULL, NULL, NULL, 0, NULL) == FECKLESS_FETCH_RUNNING) {
        ESP_LOGI(TAG, "the app ended with a download running: paused, its part file kept");
        feckless_fetch_pause();
    }
    s_mine = false;
}

#else

const feckless_umode_call_t box_fetch_calls[1];
const uint32_t box_fetch_calls_n = 0;
void box_fetch_end(void) {}

#endif
