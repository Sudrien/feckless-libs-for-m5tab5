/*
 * fetchfake.c -- fetchtest's fakes (9003): FreeRTOS on pthreads, an
 * esp_http_client over an in-memory server, the storage lease with its
 * rules checked, and the small rest. See fetchfake.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fetchfake.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage_io.h"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

/* ---- the small rest ---------------------------------------------------- */

void fake_log(char level, const char *tag, const char *fmt, ...)
{
    static int on = -1;
    if (on < 0) on = getenv("FETCHTEST_LOG") != NULL;
    if (!on) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%c %s: ", level, tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

void *heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
void  heap_caps_free(void *p) { free(p); }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 100000; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 90000; }
esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }

/* ---- tasks --------------------------------------------------------------- */

struct fake_task {
    TaskFunction_t  fn;
    void           *arg;
    pthread_mutex_t m;
    pthread_cond_t  c;
    uint32_t        notify;
};

static __thread struct fake_task *t_self;

static void *trampoline(void *p)
{
    struct fake_task *t = p;
    t_self = t;
    t->fn(t->arg);
    return NULL;
}

static struct fake_task *spawn(TaskFunction_t fn, void *arg)
{
    struct fake_task *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    pthread_mutex_init(&t->m, NULL);
    pthread_cond_init(&t->c, NULL);
    /* The handle is kept apart: a quick task can be gone, and t freed,
     * before pthread_create() returns here. */
    pthread_t th;
    if (pthread_create(&th, NULL, trampoline, t) != 0) { free(t); return NULL; }
    pthread_detach(th);
    return t;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                       UBaseType_t prio, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)prio;
    struct fake_task *t = spawn(fn, arg);
    if (out) *out = t;
    return t ? pdPASS : pdFALSE;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                               UBaseType_t prio, StackType_t *stk, StaticTask_t *tcb)
{
    (void)name; (void)stack; (void)prio; (void)stk; (void)tcb;
    return spawn(fn, arg);
}

void vTaskDelete(TaskHandle_t t)
{
    if (t == NULL || t == t_self) {
        struct fake_task *me = t_self;
        t_self = NULL;
        if (me) {
            pthread_mutex_destroy(&me->m);
            pthread_cond_destroy(&me->c);
            free(me);
        }
        pthread_exit(NULL);
    }
}

void vTaskDelay(TickType_t ticks) { usleep((useconds_t)ticks * 10); }     /* a hundred times fast */

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    (void)ticks;
    struct fake_task *t = t_self;
    pthread_mutex_lock(&t->m);
    while (t->notify == 0) pthread_cond_wait(&t->c, &t->m);
    const uint32_t v = t->notify;
    t->notify = clear ? 0 : v - 1;
    pthread_mutex_unlock(&t->m);
    return v;
}

BaseType_t xTaskNotifyGive(TaskHandle_t t)
{
    pthread_mutex_lock(&t->m);
    t->notify++;
    pthread_cond_signal(&t->c);
    pthread_mutex_unlock(&t->m);
    return pdPASS;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t) { (void)t; return 0; }

/* ---- queues and semaphores ------------------------------------------- */

struct fake_queue {
    pthread_mutex_t m;
    pthread_cond_t  ne, nf;
    unsigned        cap, size, head, count;
    uint8_t        *buf;
};

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size)
{
    struct fake_queue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->cap = len;
    q->size = size;
    q->buf = calloc(len ? len : 1, size ? size : 1);
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->ne, NULL);
    pthread_cond_init(&q->nf, NULL);
    return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    pthread_mutex_lock(&q->m);
    while (q->count == q->cap) {
        if (ticks == 0) { pthread_mutex_unlock(&q->m); return pdFALSE; }
        pthread_cond_wait(&q->nf, &q->m);
    }
    if (q->size && item) memcpy(q->buf + ((q->head + q->count) % q->cap) * q->size, item, q->size);
    q->count++;
    pthread_cond_signal(&q->ne);
    pthread_mutex_unlock(&q->m);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0) {
        if (ticks == 0) { pthread_mutex_unlock(&q->m); return pdFALSE; }
        pthread_cond_wait(&q->ne, &q->m);
    }
    if (q->size && item) memcpy(item, q->buf + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_cond_signal(&q->nf);
    pthread_mutex_unlock(&q->m);
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    pthread_mutex_lock(&q->m);
    q->head = q->count = 0;
    pthread_cond_broadcast(&q->nf);
    pthread_mutex_unlock(&q->m);
    return pdPASS;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    QueueHandle_t q = xQueueCreate(1, 0);
    if (q) xQueueSend(q, NULL, 0);
    return q;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xQueueCreate(1, 0); }
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial)
{
    QueueHandle_t q = xQueueCreate(max, 0);
    for (UBaseType_t i = 0; q && i < initial; i++) xQueueSend(q, NULL, 0);
    return q;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) { return xQueueReceive(s, NULL, ticks); }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { return xQueueSend(s, NULL, 0); }

/* ---- the storage lease ------------------------------------------------- */

static pthread_mutex_t g_lease = PTHREAD_MUTEX_INITIALIZER;
static __thread int    t_leased;
static int             g_holders, g_violations;

void storage_io_acquire(storage_io_class_t cls)
{
    (void)cls;
    if (t_leased) {
        pthread_mutex_lock(&g_mu); g_violations++; pthread_mutex_unlock(&g_mu);
        fprintf(stderr, "fake: the lease taken twice by one task\n");
        t_leased++;
        return;
    }
    pthread_mutex_lock(&g_lease);
    t_leased = 1;
    pthread_mutex_lock(&g_mu); g_holders++; pthread_mutex_unlock(&g_mu);
}

void storage_io_release(void)
{
    if (t_leased > 1) { t_leased--; return; }
    if (!t_leased) {
        pthread_mutex_lock(&g_mu); g_violations++; pthread_mutex_unlock(&g_mu);
        fprintf(stderr, "fake: the lease released when not held\n");
        return;
    }
    t_leased = 0;
    pthread_mutex_lock(&g_mu); g_holders--; pthread_mutex_unlock(&g_mu);
    pthread_mutex_unlock(&g_lease);
}

FILE *storage_io_open(const char *path, const char *mode) { return fopen(path, mode); }
int   storage_io_close(FILE *f) { return f ? fclose(f) : 0; }

/* ---- the server and the client ----------------------------------------- */

static const fake_route_t *g_routes;
static int                 g_nroutes;
static int                 g_connect_fails;
static int64_t             g_break_at = -1;
static int                 g_delay_us;
static int                 g_requests, g_open_now, g_open_max;
static int64_t             g_first_from = -2, g_served;

void fake_routes(const fake_route_t *r, int n)
{
    pthread_mutex_lock(&g_mu); g_routes = r; g_nroutes = n; pthread_mutex_unlock(&g_mu);
}
void fake_fail_connects(int n) { pthread_mutex_lock(&g_mu); g_connect_fails = n; pthread_mutex_unlock(&g_mu); }
void fake_break_at(int64_t at) { pthread_mutex_lock(&g_mu); g_break_at = at; pthread_mutex_unlock(&g_mu); }
void fake_read_delay(int us)   { pthread_mutex_lock(&g_mu); g_delay_us = us; pthread_mutex_unlock(&g_mu); }

void fake_stats_reset(void)
{
    pthread_mutex_lock(&g_mu);
    g_requests = 0; g_open_max = g_open_now; g_first_from = -2; g_served = 0;
    pthread_mutex_unlock(&g_mu);
}
int     fake_requests(void)         { pthread_mutex_lock(&g_mu); int v = g_requests; pthread_mutex_unlock(&g_mu); return v; }
int     fake_max_concurrent(void)   { pthread_mutex_lock(&g_mu); int v = g_open_max; pthread_mutex_unlock(&g_mu); return v; }
int64_t fake_first_range_from(void) { pthread_mutex_lock(&g_mu); int64_t v = g_first_from < -1 ? -1 : g_first_from; pthread_mutex_unlock(&g_mu); return v; }
int64_t fake_bytes_served(void)     { pthread_mutex_lock(&g_mu); int64_t v = g_served; pthread_mutex_unlock(&g_mu); return v; }
int     fake_lease_violations(void) { pthread_mutex_lock(&g_mu); int v = g_violations; pthread_mutex_unlock(&g_mu); return v; }
int     fake_lease_holders(void)    { pthread_mutex_lock(&g_mu); int v = g_holders; pthread_mutex_unlock(&g_mu); return v; }

struct esp_http_client {
    char                  url[1100];
    http_event_handle_cb  handler;
    void                 *user_data;
    char                  range[64], if_range[128];
    bool                  open;
    int                   status;
    const fake_route_t   *route;
    int64_t               pos, end, clen;
};

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    struct esp_http_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    snprintf(c->url, sizeof(c->url), "%s", cfg->url);
    c->handler = cfg->event_handler;
    c->user_data = cfg->user_data;
    return c;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value)
{
    if (!strcasecmp(key, "Range"))         snprintf(c->range, sizeof(c->range), "%s", value);
    else if (!strcasecmp(key, "If-Range")) snprintf(c->if_range, sizeof(c->if_range), "%s", value);
    return ESP_OK;
}

static void header(esp_http_client_handle_t c, const char *k, const char *v)
{
    if (!c->handler) return;
    esp_http_client_event_t ev = {
        .event_id = HTTP_EVENT_ON_HEADER, .client = c, .user_data = c->user_data,
        .header_key = (char *)k, .header_value = (char *)v,
    };
    c->handler(&ev);
}

esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len)
{
    (void)write_len;
    pthread_mutex_lock(&g_mu);
    if (g_connect_fails > 0) { g_connect_fails--; pthread_mutex_unlock(&g_mu); return ESP_FAIL; }
    const fake_route_t *r = NULL;
    for (int i = 0; i < g_nroutes; i++)
        if (!strcmp(g_routes[i].url, c->url)) r = &g_routes[i];
    long long a = -1, b = -1;
    const bool has_range = c->range[0] && sscanf(c->range, "bytes=%lld-%lld", &a, &b) >= 1;
    g_requests++;
    if (g_first_from == -2) g_first_from = has_range ? a : -1;
    if (!c->open) { c->open = true; g_open_now++; if (g_open_now > g_open_max) g_open_max = g_open_now; }
    pthread_mutex_unlock(&g_mu);

    c->route = r;
    c->pos = c->end = 0;
    c->clen = 0;
    if (!r) { c->status = 404; return ESP_OK; }
    if (r->status >= 300 && r->status < 400) {
        c->status = r->status;
        header(c, "Location", r->location);
        return ESP_OK;
    }
    if (r->status) { c->status = r->status; return ESP_OK; }

    if (r->ctype) header(c, "Content-Type", r->ctype);
    if (r->etag) header(c, "ETag", r->etag);
    if (r->ranges) header(c, "Accept-Ranges", "bytes");
    const bool same = !c->if_range[0] || (r->etag && !strcmp(c->if_range, r->etag));
    if (r->ranges && has_range && same) {
        if (a >= r->len) { c->status = 416; return ESP_OK; }
        c->status = 206;
        c->pos = a;
        c->end = (b >= 0 && b + 1 < r->len) ? b + 1 : r->len;
        char cr[80];
        snprintf(cr, sizeof(cr), "bytes %lld-%lld/%lld", (long long)c->pos, (long long)(c->end - 1),
                 (long long)r->len);
        header(c, "Content-Range", cr);
    } else {
        c->status = 200;
        c->pos = 0;
        c->end = r->len;
    }
    c->clen = r->no_length ? -1 : c->end - c->pos;
    return ESP_OK;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) { return c->clen; }
int     esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->status; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t c) { return c->clen; }

int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len)
{
    if (t_leased) {
        pthread_mutex_lock(&g_mu); g_violations++; pthread_mutex_unlock(&g_mu);
        fprintf(stderr, "fake: a network read with the lease held\n");
    }
    pthread_mutex_lock(&g_mu);
    const int delay = g_delay_us;
    pthread_mutex_unlock(&g_mu);
    if (delay) usleep((useconds_t)delay);
    if (!c->route || c->status >= 300 || c->pos >= c->end) return 0;
    int64_t n = 1 + rand() % 8192;
    if (n > len) n = len;
    if (n > c->end - c->pos) n = c->end - c->pos;
    pthread_mutex_lock(&g_mu);
    if (g_break_at >= 0 && c->pos <= g_break_at && g_break_at < c->pos + n) {
        g_break_at = -1;
        pthread_mutex_unlock(&g_mu);
        return -1;
    }
    g_served += n;
    pthread_mutex_unlock(&g_mu);
    memcpy(buf, c->route->body + c->pos, (size_t)n);
    c->pos += n;
    return (int)n;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{
    return c->route && c->status < 300 && c->pos >= c->end;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t c)
{
    pthread_mutex_lock(&g_mu);
    if (c->open) { c->open = false; g_open_now--; }
    pthread_mutex_unlock(&g_mu);
    return ESP_OK;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    esp_http_client_close(c);
    free(c);
    return ESP_OK;
}
