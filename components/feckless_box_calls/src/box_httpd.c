/* SPDX-License-Identifier: MIT
 *
 * esp_http_server for a boxed app (u-feckless-0077), with WebSockets and
 * the launcher's TLS server (feckless_https).
 *
 * THE SERVER is the launcher's: its task, its sockets, its TLS. The app
 * holds a number for it, TOKEN_SRV + slot, SRV_MAX at once. httpd_start()
 * and feckless_https_start() copy the app's httpd_config_t, less what a
 * box cannot follow: an interface to bind to, a transport context, a
 * global context's free function (the app's own global_user_ctx is not
 * kept: the launcher's is how its callbacks find the server).
 *
 * THE APP'S CALLBACKS -- URI handlers, WebSocket frames, open_fn,
 * close_fn, queued work -- are the app's code, and a box does not call
 * into its app from a host task. The server's task asks the app's
 * callback loop to run each one (u-feckless-0075's box_upcall()) and waits
 * for it. So the first start spawns that loop, with a stack the size of
 * the server's (at least LOOP_STACK); handlers then run on it, one at a
 * time, as they would on httpd's one task.
 *
 * A REQUEST reaches the app as an httpd_req_t in its box: one per server,
 * made at start, filled for each request -- handle (the number), method,
 * uri, content_len, user_ctx (the app's own, from its httpd_uri_t) --
 * and good only while its handler runs. Every call that takes one is
 * matched by that address to the request the launcher holds; a pointer
 * that is not a server's, or a request whose handler has returned, is
 * ESP_ERR_INVALID_ARG (-1, 0). Not kept from the app's copy: aux, and
 * sess_ctx, which is NULL each time and not saved (per-session state is
 * the app's to keep, by socket).
 *
 * STRINGS the server keeps a pointer to until the response is sent --
 * set_status, set_type, set_hdr -- are copied, and the copies freed when
 * the handler returns. Bodies, chunks and frames are read in place in the
 * box; headers, queries and cookies are written into the app's buffers.
 *
 * WHAT IS RECOGNISED BY ITS STUB: a handler that is the app's stub for
 * feckless_https_redirect becomes the launcher's function itself, with no
 * callback; a uri_match_fn that is httpd_uri_match_wildcard's, likewise.
 * Any other uri_match_fn is refused (it would be a callback per handler
 * per request).
 *
 * WAITING: the calls that send or receive on a socket, and httpd_stop and
 * feckless_https_stop (which wait for the server's task, which may be
 * waiting for a callback that needs the app's lock), are unlocked. While
 * any is in flight, a 100 ms esp_timer watches for the app's stop and
 * shuts down the sockets in use, so the waits end inside the stop's 3 s.
 * A handler whose callback is abandoned to the stop is not left with its
 * request freed under a call: the server's task shuts the socket down and
 * waits for the call to come back first.
 *
 * box_httpd_lends(fd): the box's getsockname()/getpeername() (box_net.c)
 * accept a socket of one of the app's servers, as Defeatist's remote
 * judges each peer by both ends' addresses.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("esp_http_server.h") && __has_include("lwip/sockets.h") && __has_include("ethernet.h") && \
    !defined(FECKLESS_BOX_NO_NET)

#include <stdlib.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "feckless_umode_box.h"
#include "feckless_umode_load.h"

#if __has_include("feckless_https.h")
#include "feckless_https.h"
#define HAVE_HTTPS 1
#else
#define HAVE_HTTPS 0
#endif

static const char *TAG = "box_httpd";

#define TOKEN_SRV   0x48535600u             /* "HSV\0" + slot */
#define SRV_MAX     3
#define URIS        24                      /* handlers a server */
#define ARENA       16                      /* strings a response holds */
#define FLY         12                      /* socket calls in flight, for the stop */
#define CLIENTS     16
#define URI_STR     256
#define HDR_STR     512
#define LOOP_STACK  (12 * 1024)
#define REAP_US     (100 * 1000)
#define IO_MAX      (64 * 1024)

typedef struct srv srv_t;

typedef struct {
    srv_t *srv;
    bool used;
    uint32_t fn;                            /* the app's handler */
    uint32_t ctx;                           /* its user_ctx */
    char uri[URI_STR];
    int method;
} uri_ent_t;

struct srv {
    bool used;
    bool tls;
    bool closing;
    httpd_handle_t h;
    uint32_t open_fn, close_fn;             /* the app's, 0 none */
    uint32_t proxy;                         /* httpd_req_t in the box */
    httpd_req_t *cur;                       /* the request a handler has now */
    int busy;                               /* calls in flight on it */
    char *arena[ARENA];
    int narena;
    uri_ent_t uri[URIS];
};

static srv_t s_srv[SRV_MAX];
static SemaphoreHandle_t s_lock;            /* the table; never held across a wait */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_fly[FLY];                      /* sockets in use by calls; -1 free */
static bool s_fly_init;
static int s_fly_n;
static esp_timer_handle_t s_reaper;

static bool stopping(void) { return feckless_umode_app_stopping(feckless_umode_app_loaded()); }

static void lock(void)
{
    if (!s_lock) {
        SemaphoreHandle_t m = xSemaphoreCreateBinary();
        if (m) xSemaphoreGive(m);
        bool mine = false;
        portENTER_CRITICAL(&s_mux);
        if (!s_lock) { s_lock = m; mine = true; }
        portEXIT_CRITICAL(&s_mux);
        if (!mine && m) vSemaphoreDelete(m);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
}
static void unlock(void) { xSemaphoreGive(s_lock); }

static void sleep_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }

/* ---- the stop's watch ------------------------------------------------- */

/* Not static: the host test runs it by hand. */
void box_httpd_reap(void *arg)
{
    (void)arg;
    if (!stopping()) return;
    portENTER_CRITICAL(&s_mux);
    int fds[FLY];
    for (int i = 0; i < FLY; i++) { fds[i] = s_fly[i]; }
    portEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < FLY; i++) if (fds[i] >= 0) lwip_shutdown(fds[i], SHUT_RDWR);
}

static int fly_enter(int fd)
{
    int slot = -1;
    bool first = false;
    portENTER_CRITICAL(&s_mux);
    if (!s_fly_init) { for (int i = 0; i < FLY; i++) s_fly[i] = -1; s_fly_init = true; }
    for (int i = 0; i < FLY && slot < 0; i++) if (s_fly[i] < 0) { s_fly[i] = fd; slot = i; }
    if (slot >= 0) first = s_fly_n++ == 0;
    portEXIT_CRITICAL(&s_mux);
    if (first) {
        if (!s_reaper) {
            const esp_timer_create_args_t a = { .callback = box_httpd_reap, .name = "box_httpd" };
            if (esp_timer_create(&a, &s_reaper) != ESP_OK) s_reaper = NULL;
        }
        if (s_reaper) esp_timer_start_periodic(s_reaper, REAP_US);
    }
    return slot;
}

static void fly_leave(int slot)
{
    if (slot < 0) return;
    bool last;
    portENTER_CRITICAL(&s_mux);
    s_fly[slot] = -1;
    last = --s_fly_n == 0;
    portEXIT_CRITICAL(&s_mux);
    if (last && s_reaper) esp_timer_stop(s_reaper);
}

/* ---- numbers and requests ------------------------------------------------ */

static srv_t *srv_of(uint32_t tok)
{
    const uint32_t i = tok - TOKEN_SRV;
    return i < SRV_MAX && s_srv[i].used && !s_srv[i].closing ? &s_srv[i] : NULL;
}

static uint32_t tok_of(const srv_t *s) { return TOKEN_SRV + (uint32_t)(s - s_srv); }

/* The request the app's copy at `p` stands for, counted in flight; NULL if
 * none. Leave with req_leave(). */
static httpd_req_t *req_enter(uint32_t p, srv_t **out)
{
    httpd_req_t *r = NULL;
    lock();
    for (int i = 0; i < SRV_MAX && !r; i++) {
        srv_t *s = &s_srv[i];
        if (s->used && s->proxy && s->proxy == p && s->cur) {
            r = s->cur;
            s->busy++;
            *out = s;
        }
    }
    unlock();
    return r;
}

static void req_leave(srv_t *s)
{
    lock();
    s->busy--;
    unlock();
}

/* A string kept until the handler returns; NULL if no room. */
static const char *keep(srv_t *s, const char *str)
{
    if (s->narena >= ARENA) return NULL;
    char *c = strdup(str);
    if (c) s->arena[s->narena++] = c;
    return c;
}

static bool is_stub(uint32_t fn, const char *name)
{
    const char *c = NULL;
    return fn && feckless_umode_app_where(fn, NULL, &c) == FECKLESS_UMODE_WHERE_STUB && c && !strcmp(c, name);
}

/* ---- what the server's task runs ------------------------------------------- */

static esp_err_t on_request(httpd_req_t *req)
{
    uri_ent_t *e = req->user_ctx;
    srv_t *s = e->srv;
    const uint32_t fn = e->fn, ctx = e->ctx;
    lock();
    const bool ok = s->used && !s->closing && s->proxy && !s->cur;
    if (ok) {
        httpd_req_t *p = (httpd_req_t *)(uintptr_t)s->proxy;
        memset(p, 0, sizeof *p);
        p->handle = (httpd_handle_t)(uintptr_t)tok_of(s);
        p->method = req->method;
        /* u-feckless-0079: memcpy, not strncpy -- GCC's stringop-truncation
         * sees two arrays of one size; p is zeroed above, so it ends. */
        memcpy((char *)p->uri, req->uri, strnlen(req->uri, sizeof p->uri - 1));
        p->content_len = req->content_len;
        p->user_ctx = (void *)(uintptr_t)ctx;
        s->cur = req;
        s->busy = 0;
    }
    unlock();
    if (!ok) return ESP_FAIL;

    uint32_t r = (uint32_t)ESP_FAIL;
    const esp_err_t e2 = box_upcall(fn, (const uint32_t[4]){ s->proxy, 0, 0, 0 }, &r);

    lock();
    s->cur = NULL;                          /* no call takes it from now */
    if (s->busy) {                          /* abandoned to the stop, with a call in flight */
        const int fd = httpd_req_to_sockfd(req);
        if (fd >= 0) lwip_shutdown(fd, SHUT_RDWR);
        for (int ms = 0; s->busy; ms += 10) {
            unlock();
            if (ms && ms % 1000 == 0) ESP_LOGW(TAG, "a request's call has not come back after %d ms", ms);
            sleep_ms(10);
            lock();
        }
    }
    for (int i = 0; i < s->narena; i++) free(s->arena[i]);
    s->narena = 0;
    unlock();
    return e2 == ESP_OK ? (esp_err_t)r : ESP_FAIL;
}

static srv_t *srv_of_handle(httpd_handle_t h) { return httpd_get_global_user_ctx(h); }

/* u-feckless-0080: httpd_stop() hands the global context to its free
 * function, or to free() when there is none -- and ours is a slot of
 * s_srv, not the heap's. Nothing to free. */
static void keep_srv(void *ctx) { (void)ctx; }

static esp_err_t on_open(httpd_handle_t h, int fd)
{
    srv_t *s = srv_of_handle(h);
    if (!s || !s->open_fn) return ESP_OK;
    uint32_t r = (uint32_t)ESP_FAIL;
    if (box_upcall(s->open_fn, (const uint32_t[4]){ tok_of(s), (uint32_t)fd, 0, 0 }, &r) != ESP_OK) return ESP_FAIL;
    return (esp_err_t)r;
}

/* close_fn, when set, must close the socket itself: the app's cannot (it
 * is not one the box gave it), so it is closed here after. */
static void on_close(httpd_handle_t h, int fd)
{
    srv_t *s = srv_of_handle(h);
    if (s && s->close_fn) {
        uint32_t r;
        (void)box_upcall(s->close_fn, (const uint32_t[4]){ tok_of(s), (uint32_t)fd, 0, 0 }, &r);
    }
    lwip_close(fd);
}

typedef struct {
    uint32_t fn, arg;
} work_t;

static void on_work(void *arg)
{
    work_t *w = arg;
    uint32_t r;
    (void)box_upcall(w->fn, (const uint32_t[4]){ w->arg, 0, 0, 0 }, &r);
    free(w);
}

/* ---- starting and stopping -------------------------------------------------- */

static bool refuse(const char *what)
{
    ESP_LOGW(TAG, "server: refused -- %s is not for a box", what);
    return false;
}

/* The app's config at a[...] as the launcher's, into *c; false if it asks
 * for what a box cannot do. */
static bool config_of(win_t *w, uint32_t p, httpd_config_t *c, srv_t *s)
{
    if (!ok_ptr(w, p, sizeof *c)) return false;
    memcpy(c, (const void *)(uintptr_t)p, sizeof *c);
    if (c->if_name) return refuse("binding to an interface");
    if (c->global_transport_ctx || c->global_transport_ctx_free_fn) return refuse("a transport context");
    if (c->uri_match_fn) {
        if (!is_stub((uint32_t)(uintptr_t)c->uri_match_fn, "httpd_uri_match_wildcard"))
            return refuse("a uri_match_fn of the app's own");
        c->uri_match_fn = httpd_uri_match_wildcard;
    }
    if (c->global_user_ctx_free_fn) ESP_LOGW(TAG, "server: the app's global_user_ctx and its free function are not kept");
    s->open_fn = (uint32_t)(uintptr_t)c->open_fn;
    s->close_fn = (uint32_t)(uintptr_t)c->close_fn;
    if (s->open_fn && !ok_ptr(w, s->open_fn, 4)) return false;
    if (s->close_fn && !ok_ptr(w, s->close_fn, 4)) return false;
    c->open_fn = s->open_fn ? on_open : NULL;
    c->close_fn = s->close_fn ? on_close : NULL;
    c->global_user_ctx = s;
    c->global_user_ctx_free_fn = keep_srv;  /* not NULL: then httpd_stop() calls free() on it */
    return true;
}

typedef esp_err_t (*start_fn)(httpd_handle_t *, const httpd_config_t *, uint16_t);

static esp_err_t plain_start(httpd_handle_t *h, const httpd_config_t *c, uint16_t port)
{
    (void)port;
    return httpd_start(h, c);
}

/* (handle *out, config *, port, tls) */
static uint32_t start(win_t *w, uint32_t out, uint32_t cfg, uint16_t port, bool tls)
{
    if (!ok_ptr(w, out, sizeof(uint32_t))) return (uint32_t)ESP_ERR_INVALID_ARG;
    *(uint32_t *)(uintptr_t)out = 0;
#if !HAVE_HTTPS
    if (tls) return (uint32_t)ESP_ERR_NOT_SUPPORTED;
#endif
    int slot = -1;
    lock();
    for (int i = 0; i < SRV_MAX && slot < 0; i++)
        if (!s_srv[i].used) { slot = i; memset(&s_srv[i], 0, sizeof s_srv[i]); s_srv[i].used = s_srv[i].closing = true; }
    unlock();
    if (slot < 0) {
        ESP_LOGW(TAG, "server: all %d in use", SRV_MAX);
        return (uint32_t)ESP_ERR_NO_MEM;
    }
    srv_t *s = &s_srv[slot];
    s->tls = tls;
    httpd_config_t c;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (!config_of(w, cfg, &c, s)) goto fail;
    e = box_upcall_start(w, c.stack_size > LOOP_STACK ? (uint32_t)c.stack_size : LOOP_STACK,
                         (int)c.task_priority);
    if (e != ESP_OK) goto fail;
    s->proxy = feckless_box_alloc(box_of_win(w), sizeof(httpd_req_t));
    if (!s->proxy) { e = ESP_ERR_NO_MEM; goto fail; }
    for (int i = 0; i < URIS; i++) s->uri[i].srv = s;
    lock();
    s->closing = false;                     /* callbacks may come as soon as it starts */
    unlock();
#if HAVE_HTTPS
    const start_fn fn = tls ? feckless_https_start : plain_start;
#else
    const start_fn fn = plain_start;
#endif
    e = fn(&s->h, &c, port);
    if (e != ESP_OK) goto fail;
    *(uint32_t *)(uintptr_t)out = tok_of(s);
    return ESP_OK;
fail:
    if (s->proxy) feckless_box_free(box_of_win(w), s->proxy);
    lock();
    memset(s, 0, sizeof *s);
    unlock();
    return (uint32_t)e;
}

H(c_httpd_start) { *ret = start(w, a[0], a[1], 0, false); return true; }
H(c_feckless_https_start) { *ret = start(w, a[0], a[1], (uint16_t)a[2], true); return true; }

/* (handle): waits for the server's task, which may be in a callback */
static uint32_t stop(win_t *w, uint32_t tok)
{
    lock();
    srv_t *s = srv_of(tok);
    if (s) s->closing = true;
    unlock();
    if (!s) return (uint32_t)ESP_ERR_INVALID_ARG;
#if HAVE_HTTPS
    const esp_err_t e = s->tls ? feckless_https_stop(s->h) : httpd_stop(s->h);
#else
    const esp_err_t e = httpd_stop(s->h);
#endif
    feckless_umode_app_t *app = feckless_umode_app_loaded();
    feckless_umode_app_lock(app);
    if (s->proxy) feckless_box_free(box_of_win(w), s->proxy);
    feckless_umode_app_unlock(app);
    lock();
    memset(s, 0, sizeof *s);
    unlock();
    return (uint32_t)e;
}

H(c_httpd_stop) { *ret = stop(w, a[0]); return true; }
H(c_feckless_https_stop) { *ret = stop(w, a[0]); return true; }

/* (handle, httpd_uri_t *) */
H(c_httpd_register_uri_handler)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!ok_ptr(w, a[1], sizeof(httpd_uri_t))) return true;
    httpd_uri_t u;
    memcpy(&u, (const void *)(uintptr_t)a[1], sizeof u);
    const char *path = str(w, (uint32_t)(uintptr_t)u.uri, URI_STR);
    if (!path) return true;
#if CONFIG_HTTPD_WS_SUPPORT
    const char *sub = u.supported_subprotocol ? str(w, (uint32_t)(uintptr_t)u.supported_subprotocol, URI_STR) : NULL;
    if (u.supported_subprotocol && !sub) return true;
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
    if (u.ws_pre_handshake_cb) return refuse("a WebSocket pre-handshake callback"), true;
#endif
#if CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
    if (u.ws_post_handshake_cb) return refuse("a WebSocket post-handshake callback"), true;
#endif
#endif
    const uint32_t fn = (uint32_t)(uintptr_t)u.handler;
    lock();
    srv_t *s = srv_of(a[0]);
    if (!s) { unlock(); return true; }
    httpd_uri_t h = u;
    h.uri = path;
#if CONFIG_HTTPD_WS_SUPPORT
    h.supported_subprotocol = sub;          /* httpd copies both */
#endif
#if HAVE_HTTPS
    if (is_stub(fn, "feckless_https_redirect")) {
        h.handler = feckless_https_redirect;
        h.user_ctx = NULL;
        *ret = (uint32_t)httpd_register_uri_handler(s->h, &h);
        unlock();
        return true;
    }
#endif
    if (!ok_ptr(w, fn, 4)) { unlock(); return true; }
    uri_ent_t *e = NULL;
    for (int i = 0; i < URIS && !e; i++) if (!s->uri[i].used) e = &s->uri[i];
    if (!e) {
        unlock();
        ESP_LOGW(TAG, "server: more than %d handlers", URIS);
        *ret = (uint32_t)ESP_ERR_HTTPD_HANDLERS_FULL;
        return true;
    }
    e->used = true;
    e->fn = fn;
    e->ctx = (uint32_t)(uintptr_t)u.user_ctx;
    e->method = u.method;
    const size_t pl = strnlen(path, sizeof e->uri - 1);
    memcpy(e->uri, path, pl);
    e->uri[pl] = '\0';
    h.handler = on_request;
    h.user_ctx = e;
    const esp_err_t r = httpd_register_uri_handler(s->h, &h);
    if (r != ESP_OK) e->used = false;
    unlock();
    *ret = (uint32_t)r;
    return true;
}

/* (handle, uri, method) */
H(c_httpd_unregister_uri_handler)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *path = str(w, a[1], URI_STR);
    if (!path) return true;
    lock();
    srv_t *s = srv_of(a[0]);
    if (s) {
        *ret = (uint32_t)httpd_unregister_uri_handler(s->h, path, (httpd_method_t)a[2]);
        for (int i = 0; i < URIS; i++) {
            uri_ent_t *e = &s->uri[i];
            if (*ret == ESP_OK && e->used && e->method == (int)a[2] && !strcmp(e->uri, path)) e->used = false;
        }
    }
    unlock();
    return true;
}

/* ---- the response ------------------------------------------------------------- */

/* A string the server keeps until the response goes; the app's at p. */
static uint32_t set_kept(win_t *w, uint32_t req, uint32_t p, int which, uint32_t p2)
{
    const char *v = str(w, p, HDR_STR);
    const char *v2 = which == 2 ? str(w, p2, HDR_STR) : NULL;
    if (!v || (which == 2 && !v2)) return (uint32_t)ESP_ERR_INVALID_ARG;
    srv_t *s;
    httpd_req_t *r = req_enter(req, &s);
    if (!r) return (uint32_t)ESP_ERR_INVALID_ARG;
    lock();
    const char *k = keep(s, v), *k2 = which == 2 ? keep(s, v2) : NULL;
    unlock();
    esp_err_t e = ESP_ERR_NO_MEM;
    if (k && (which != 2 || k2)) {
        e = which == 0 ? httpd_resp_set_status(r, k) : which == 1 ? httpd_resp_set_type(r, k)
                                                                  : httpd_resp_set_hdr(r, k, k2);
    } else {
        ESP_LOGW(TAG, "a response with more than %d status, type and header strings", ARENA);
    }
    req_leave(s);
    return (uint32_t)e;
}

H(c_httpd_resp_set_status) { *ret = set_kept(w, a[0], a[1], 0, 0); return true; }
H(c_httpd_resp_set_type) { *ret = set_kept(w, a[0], a[1], 1, 0); return true; }
H(c_httpd_resp_set_hdr) { *ret = set_kept(w, a[0], a[1], 2, a[2]); return true; }

/* (req, buf, len or HTTPD_RESP_USE_STRLEN) */
static uint32_t send_body(win_t *w, const uint32_t a[7], bool chunk)
{
    ssize_t len = (ssize_t)(int32_t)a[2];
    if (a[1] && len == HTTPD_RESP_USE_STRLEN) {
        const int n = feckless_umode_strlen(w, a[1], IO_MAX + 1);
        if (n < 0 || n > IO_MAX) return (uint32_t)ESP_ERR_INVALID_ARG;
        len = n;
    }
    if (len < 0 || len > IO_MAX || (len && !ok_ptr(w, a[1], (uint32_t)len))) return (uint32_t)ESP_ERR_INVALID_ARG;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return (uint32_t)ESP_ERR_INVALID_ARG;
    const int f = fly_enter(httpd_req_to_sockfd(r));
    const char *b = a[1] ? (const char *)(uintptr_t)a[1] : NULL;
    const esp_err_t e = chunk ? httpd_resp_send_chunk(r, b, len) : httpd_resp_send(r, b, len);
    fly_leave(f);
    req_leave(s);
    return (uint32_t)e;
}

H(c_httpd_resp_send) { *ret = send_body(w, a, false); return true; }
H(c_httpd_resp_send_chunk) { *ret = send_body(w, a, true); return true; }

/* (req, code, msg) */
H(c_httpd_resp_send_err)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *m = a[2] ? str(w, a[2], HDR_STR) : NULL;
    if (a[2] && !m) return true;
    char *mc = m ? strdup(m) : NULL;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (r) {
        const int f = fly_enter(httpd_req_to_sockfd(r));
        *ret = (uint32_t)httpd_resp_send_err(r, (httpd_err_code_t)a[1], mc);
        fly_leave(f);
        req_leave(s);
    }
    free(mc);
    return true;
}

#if HAVE_HTTPS
/* (req): the launcher's redirect, called by the app itself */
H(c_feckless_https_redirect)
{
    (void)w;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!r) return true;
    const int f = fly_enter(httpd_req_to_sockfd(r));
    *ret = (uint32_t)feckless_https_redirect(r);
    fly_leave(f);
    req_leave(s);
    return true;
}

/* (out, size) */
H(c_feckless_https_fingerprint)
{
    *ret = 0;
    if (!a[1] || !ok_ptr(w, a[0], a[1])) return true;
    char fp[FECKLESS_HTTPS_FP_SIZE];
    const bool ok = feckless_https_fingerprint(fp, sizeof fp);
    if (!ok) return true;
    const size_t n = strnlen(fp, sizeof fp - 1);
    if (n + 1 > a[1]) return true;
    memcpy((void *)(uintptr_t)a[0], fp, n + 1);
    *ret = 1;
    return true;
}
#else
H(c_feckless_https_redirect) { (void)w; (void)a; *ret = (uint32_t)ESP_ERR_NOT_SUPPORTED; return true; }
H(c_feckless_https_fingerprint) { (void)w; (void)a; *ret = 0; return true; }
#endif

/* ---- the request ---------------------------------------------------------------- */

/* (req, buf, len) -> bytes, 0 at the end, or < 0 */
H(c_httpd_req_recv)
{
    *ret = (uint32_t)HTTPD_SOCK_ERR_INVALID;
    const uint32_t len = a[2] > IO_MAX ? IO_MAX : a[2];
    if (!len || !ok_ptr(w, a[1], len)) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    const int f = fly_enter(httpd_req_to_sockfd(r));
    *ret = (uint32_t)httpd_req_recv(r, (char *)(uintptr_t)a[1], len);
    fly_leave(f);
    req_leave(s);
    return true;
}

H(c_httpd_req_to_sockfd)
{
    (void)w;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    *ret = (uint32_t)-1;
    if (!r) return true;
    *ret = (uint32_t)httpd_req_to_sockfd(r);
    req_leave(s);
    return true;
}

/* (req, field) */
H(c_httpd_req_get_hdr_value_len)
{
    *ret = 0;
    const char *f = str(w, a[1], HDR_STR);
    if (!f) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    *ret = (uint32_t)httpd_req_get_hdr_value_len(r, f);
    req_leave(s);
    return true;
}

/* (req, field, val, size) */
H(c_httpd_req_get_hdr_value_str)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *f = str(w, a[1], HDR_STR);
    if (!f || !a[3] || !ok_ptr(w, a[2], a[3])) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    *ret = (uint32_t)httpd_req_get_hdr_value_str(r, f, (char *)(uintptr_t)a[2], a[3]);
    req_leave(s);
    return true;
}

H(c_httpd_req_get_url_query_len)
{
    (void)w;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    *ret = 0;
    if (!r) return true;
    *ret = (uint32_t)httpd_req_get_url_query_len(r);
    req_leave(s);
    return true;
}

/* (req, buf, len) */
H(c_httpd_req_get_url_query_str)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!a[2] || !ok_ptr(w, a[1], a[2])) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    *ret = (uint32_t)httpd_req_get_url_query_str(r, (char *)(uintptr_t)a[1], a[2]);
    req_leave(s);
    return true;
}

/* (req, name, val, size_t *size) */
H(c_httpd_req_get_cookie_val)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *n = str(w, a[1], HDR_STR);
    if (!n || !ok_ptr(w, a[3], sizeof(size_t))) return true;
    size_t size = *(size_t *)(uintptr_t)a[3];
    if (!size || !ok_ptr(w, a[2], (uint32_t)size)) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    *ret = (uint32_t)httpd_req_get_cookie_val(r, n, (char *)(uintptr_t)a[2], &size);
    *(size_t *)(uintptr_t)a[3] = size;
    req_leave(s);
    return true;
}

/* (qry, key, val, size): the app's strings only */
H(c_httpd_query_key_value)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *q = str(w, a[0], FECKLESS_BOX_STR_MAX);
    const char *k = str(w, a[1], HDR_STR);
    if (!q || !k || !a[3] || !ok_ptr(w, a[2], a[3])) return true;
    *ret = (uint32_t)httpd_query_key_value(q, k, (char *)(uintptr_t)a[2], a[3]);
    return true;
}

/* (template, uri, len) */
H(c_httpd_uri_match_wildcard)
{
    *ret = 0;
    const char *t = str(w, a[0], URI_STR);
    if (!t || !ok_ptr(w, a[1], a[2])) return true;
    *ret = httpd_uri_match_wildcard(t, (const char *)(uintptr_t)a[1], a[2]);
    return true;
}

/* ---- sessions, work, WebSockets --------------------------------------------------- */

/* (handle, size_t *fds, int *client_fds) */
H(c_httpd_get_client_list)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!ok_ptr(w, a[1], sizeof(size_t))) return true;
    const size_t cap = *(size_t *)(uintptr_t)a[1];
    if (!cap || !ok_ptr(w, a[2], (uint32_t)(cap * sizeof(int)))) return true;
    lock();
    srv_t *s = srv_of(a[0]);
    httpd_handle_t h = s ? s->h : NULL;
    unlock();
    if (!h) return true;
    int fds[CLIENTS];
    size_t n = CLIENTS;
    const esp_err_t e = httpd_get_client_list(h, &n, fds);
    if (e != ESP_OK) { *ret = (uint32_t)e; return true; }
    if (n > cap) return true;               /* as httpd's own: not enough room */
    memcpy((void *)(uintptr_t)a[2], fds, n * sizeof(int));
    *(size_t *)(uintptr_t)a[1] = n;
    *ret = ESP_OK;
    return true;
}

/* (handle, work, arg): work is the app's, run by its callback loop on the
 * server's task's turn */
H(c_httpd_queue_work)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!ok_ptr(w, a[1], 4)) return true;
    lock();
    srv_t *s = srv_of(a[0]);
    httpd_handle_t h = s ? s->h : NULL;
    unlock();
    if (!h) return true;
    work_t *k = malloc(sizeof *k);
    if (!k) { *ret = (uint32_t)ESP_ERR_NO_MEM; return true; }
    k->fn = a[1];
    k->arg = a[2];
    const esp_err_t e = httpd_queue_work(h, on_work, k);
    if (e != ESP_OK) free(k);
    *ret = (uint32_t)e;
    return true;
}

/* (handle, fd) */
H(c_httpd_sess_trigger_close)
{
    (void)w;
    lock();
    srv_t *s = srv_of(a[0]);
    httpd_handle_t h = s ? s->h : NULL;
    unlock();
    *ret = h ? (uint32_t)httpd_sess_trigger_close(h, (int)a[1]) : (uint32_t)ESP_ERR_INVALID_ARG;
    return true;
}

#if CONFIG_HTTPD_WS_SUPPORT
H(c_httpd_ws_get_fd_info)
{
    (void)w;
    lock();
    srv_t *s = srv_of(a[0]);
    httpd_handle_t h = s ? s->h : NULL;
    unlock();
    *ret = h ? (uint32_t)httpd_ws_get_fd_info(h, (int)a[1]) : (uint32_t)HTTPD_WS_CLIENT_INVALID;
    return true;
}

/* A frame of the app's, copied; its payload checked to lie in the box. */
static bool frame_of(win_t *w, uint32_t p, httpd_ws_frame_t *f, size_t room)
{
    if (!ok_ptr(w, p, sizeof *f)) return false;
    memcpy(f, (const void *)(uintptr_t)p, sizeof *f);
    if (room > IO_MAX) return false;
    return !room || ok_ptr(w, (uint32_t)(uintptr_t)f->payload, (uint32_t)room);
}

/* (req, frame, max_len): max_len 0 asks only the length */
H(c_httpd_ws_recv_frame)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    httpd_ws_frame_t f;
    if (!frame_of(w, a[1], &f, a[2])) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    const int fl = fly_enter(httpd_req_to_sockfd(r));
    *ret = (uint32_t)httpd_ws_recv_frame(r, &f, a[2]);
    fly_leave(fl);
    req_leave(s);
    httpd_ws_frame_t *out = (httpd_ws_frame_t *)(uintptr_t)a[1];
    out->final = f.final;
    out->fragmented = f.fragmented;
    out->type = f.type;
    out->len = f.len;
    out->left_len = f.left_len;
    return true;
}

/* (req, frame) */
H(c_httpd_ws_send_frame)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    httpd_ws_frame_t f;
    if (!ok_ptr(w, a[1], sizeof f)) return true;
    if (!frame_of(w, a[1], &f, ((const httpd_ws_frame_t *)(uintptr_t)a[1])->len)) return true;
    srv_t *s;
    httpd_req_t *r = req_enter(a[0], &s);
    if (!r) return true;
    const int fl = fly_enter(httpd_req_to_sockfd(r));
    *ret = (uint32_t)httpd_ws_send_frame(r, &f);
    fly_leave(fl);
    req_leave(s);
    return true;
}

/* (handle, fd, frame): from any of the app's threads */
H(c_httpd_ws_send_frame_async)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    httpd_ws_frame_t f;
    if (!ok_ptr(w, a[2], sizeof f)) return true;
    if (!frame_of(w, a[2], &f, ((const httpd_ws_frame_t *)(uintptr_t)a[2])->len)) return true;
    lock();
    srv_t *s = srv_of(a[0]);
    httpd_handle_t h = s ? s->h : NULL;
    unlock();
    if (!h) return true;
    const int fl = fly_enter((int)a[1]);
    *ret = (uint32_t)httpd_ws_send_frame_async(h, (int)a[1], &f);
    fly_leave(fl);
    return true;
}
#endif

bool box_httpd_lends(int fd)
{
    if (fd < 0) return false;
    for (int i = 0; i < SRV_MAX; i++) {
        lock();
        srv_t *s = s_srv[i].used && !s_srv[i].closing ? &s_srv[i] : NULL;
        httpd_handle_t h = s ? s->h : NULL;
        unlock();
        if (!h) continue;
        int fds[CLIENTS];
        size_t n = CLIENTS;
        if (httpd_get_client_list(h, &n, fds) != ESP_OK) continue;
        for (size_t k = 0; k < n; k++) if (fds[k] == fd) return true;
    }
    return false;
}

const feckless_umode_call_t box_httpd_calls[] = {
    FN(httpd_start), FN(feckless_https_start), FNU(httpd_stop), FNU(feckless_https_stop),
    FN(httpd_register_uri_handler), FN(httpd_unregister_uri_handler),
    FN(httpd_resp_set_status), FN(httpd_resp_set_type), FN(httpd_resp_set_hdr),
    FNU(httpd_resp_send), FNU(httpd_resp_send_chunk), FNU(httpd_resp_send_err),
    FNU(feckless_https_redirect), FN(feckless_https_fingerprint),
    FNU(httpd_req_recv), FN(httpd_req_to_sockfd), FN(httpd_req_get_hdr_value_len),
    FN(httpd_req_get_hdr_value_str), FN(httpd_req_get_url_query_len), FN(httpd_req_get_url_query_str),
    FN(httpd_req_get_cookie_val), FN(httpd_query_key_value), FN(httpd_uri_match_wildcard),
    FN(httpd_get_client_list), FN(httpd_queue_work), FN(httpd_sess_trigger_close),
#if CONFIG_HTTPD_WS_SUPPORT
    FN(httpd_ws_get_fd_info), FNU(httpd_ws_recv_frame), FNU(httpd_ws_send_frame), FNU(httpd_ws_send_frame_async),
#endif
};
const uint32_t box_httpd_calls_n = sizeof box_httpd_calls / sizeof box_httpd_calls[0];

/* The app's servers stopped (its threads are gone: no callback is made). */
void box_httpd_end(void)
{
    for (int i = 0; i < SRV_MAX; i++) {
        srv_t *s = &s_srv[i];
        if (!s->used || !s->h) { memset(s, 0, sizeof *s); continue; }
        lock();
        s->closing = true;
        unlock();
#if HAVE_HTTPS
        if (s->tls) feckless_https_stop(s->h); else httpd_stop(s->h);
#else
        httpd_stop(s->h);
#endif
        for (int k = 0; k < s->narena; k++) free(s->arena[k]);
        memset(s, 0, sizeof *s);            /* the proxy went with the box */
    }
    if (s_reaper) {
        esp_timer_stop(s_reaper);
        esp_timer_delete(s_reaper);
        s_reaper = NULL;
    }
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < FLY; i++) s_fly[i] = -1;
    s_fly_init = true;
    s_fly_n = 0;
    portEXIT_CRITICAL(&s_mux);
}

#else

const feckless_umode_call_t box_httpd_calls[1];
const uint32_t box_httpd_calls_n = 0;
bool box_httpd_lends(int fd) { (void)fd; return false; }
void box_httpd_end(void) {}

#endif
