/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0077: the HTTP server for a boxed app, end to end, with real
 * threads (FAKE_REAL_LOCKS, FAKE_REAL_SEMS, as upcall_test).
 *
 * The test is three parties. The app: its main thread makes host calls on
 * a window of its own, and its callback loop -- a thread asking
 * feckless_box_upcall_next() for work -- runs the "handlers", C functions
 * here standing at box addresses, which make their host calls on the
 * loop's window. The launcher: box_httpd.c and box_upcall.c as they are.
 * The server's task: this file calling fake_httpd.c's open, request,
 * frame and work, as httpd's task would. Every wait has a watchdog.
 *
 *   make -C components/feckless_box_calls/test run-httpd
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "feckless_box_calls.h"
#include "feckless_umode_load.h"
#include "fake_httpd.h"
#include "../src/box_calls_priv.h"

extern bool fake_stopping;
extern uint32_t fake_stub_at[4];
extern const char *fake_stub_name[4];
extern httpd_handle_t fake_httpd_last;
extern int fake_httpd_live, fake_https_redirects;
extern char fake_ws_async[256];
extern int fake_ws_async_fd;

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define MEM (512 * 1024)
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

static uint32_t dcall(feckless_umode_window_t *w, const char *name, uint32_t a0, uint32_t a1, uint32_t a2,
                      uint32_t a3)
{
    const uint32_t i = idx_of(name);
    w->ctx.x[10] = a0; w->ctx.x[11] = a1; w->ctx.x[12] = a2; w->ctx.x[13] = a3;
    w->ctx.x[14] = 0; w->ctx.x[15] = 0;
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
static void *bx(uint32_t n)
{
    pthread_mutex_lock(&s_app);
    void *p = (void *)(uintptr_t)feckless_box_alloc(&box, n);
    pthread_mutex_unlock(&s_app);
    memset(p, 0, n);
    return p;
}
static char *bstr(const char *s) { char *p = bx((uint32_t)strlen(s) + 1); strcpy(p, s); return p; }
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

/* ---- the app's code, at box addresses ------------------------------------ */

#define AT(n)       (P(mem) + 0x40 + 0x10 * (n))
#define F_LOOP      AT(0)
#define F_OPEN      AT(1)
#define F_PAGE      AT(2)
#define F_POST      AT(3)
#define F_QUERY     AT(4)
#define F_WS        AT(5)
#define F_WORK      AT(6)
#define F_BLOCK     AT(7)
#define F_CLOSE     AT(8)
#define STUB_WILD   AT(14)                  /* the app's stubs for the launcher's functions */
#define STUB_REDIR  AT(15)

static feckless_umode_window_t lw;          /* the loop's window: handlers call on it */
static volatile int loop_ended, loop_calls;
static uint32_t s_tok;                      /* the TLS server's number */
static uint32_t s_saved_proxy;
static int s_open_fd = -1, s_open_peer_ok, s_open_refuse, s_closed_fd = -1;
static volatile int s_work_arg;

static httpd_req_t *R(uint32_t p) { return (httpd_req_t *)(uintptr_t)p; }

static uint32_t app_open(uint32_t tok, uint32_t fd)
{
    s_open_fd = (int)fd;
    char *sa = bx(64);
    uint32_t *sl = bx(4);
    *sl = 64;
    s_open_peer_ok = dcall(&lw, "lwip_getpeername", fd, P(sa), P(sl), 0) == 0;
    if (s_open_refuse) {
        dcall(&lw, "httpd_sess_trigger_close", tok, fd, 0, 0);
        return (uint32_t)ESP_FAIL;
    }
    return ESP_OK;
}

static uint32_t app_close(uint32_t tok, uint32_t fd) { (void)tok; s_closed_fd = (int)fd; return 0; }

static uint32_t app_page(uint32_t p)
{
    httpd_req_t *r = R(p);
    s_saved_proxy = p;
    if (r->method != HTTP_GET || strcmp(r->uri, "/") || r->user_ctx != (void *)0x1234 ||
        (uint32_t)(uintptr_t)r->handle != s_tok)
        return (uint32_t)ESP_FAIL;
    char *type = bstr("text/plain"), *k = bstr("X-A"), *v = bstr("1"), *st = bstr("201 Created");
    dcall(&lw, "httpd_resp_set_type", p, P(type), 0, 0);
    dcall(&lw, "httpd_resp_set_hdr", p, P(k), P(v), 0);
    dcall(&lw, "httpd_resp_set_status", p, P(st), 0, 0);
    strcpy(type, "XXXXXXXXXX");             /* the app reuses its buffers: the server must not see it */
    strcpy(st, "XXXXXXXXXXX");
    return dcall(&lw, "httpd_resp_send", p, P(bstr("hello")), (uint32_t)HTTPD_RESP_USE_STRLEN, 0);
}

static uint32_t app_post(uint32_t p)
{
    httpd_req_t *r = R(p);
    char *body = bx(64), *origin = bx(64), *ck = bx(32);
    size_t *cl = bx(sizeof(size_t));
    int got = 0;
    while (got < (int)r->content_len) {
        const int n = (int)dcall(&lw, "httpd_req_recv", p, P(body + got), (uint32_t)(r->content_len - (size_t)got), 0);
        if (n <= 0) return (uint32_t)ESP_FAIL;
        got += n;
    }
    if (dcall(&lw, "httpd_req_get_hdr_value_str", p, P(bstr("Origin")), P(origin), 64) != ESP_OK) return 1;
    if (dcall(&lw, "httpd_req_get_hdr_value_len", p, P(bstr("Origin")), 0, 0) != strlen(origin)) return 2;
    *cl = 32;
    if (dcall(&lw, "httpd_req_get_cookie_val", p, P(bstr("sid")), P(ck), P(cl)) != ESP_OK) return 3;
    if ((int)dcall(&lw, "httpd_req_to_sockfd", p, 0, 0, 0) != s_open_fd) return 4;
    char *line = bx(160);
    snprintf(line, 160, "%s|%s|%s", body, origin, ck);
    dcall(&lw, "httpd_resp_send_chunk", p, P(line), (uint32_t)HTTPD_RESP_USE_STRLEN, 0);
    dcall(&lw, "httpd_resp_send_chunk", p, P(bstr("|end")), 4, 0);
    return dcall(&lw, "httpd_resp_send_chunk", p, 0, 0, 0);
}

static uint32_t app_query(uint32_t p)
{
    const uint32_t n = dcall(&lw, "httpd_req_get_url_query_len", p, 0, 0, 0);
    char *q = bx(n + 1), *v = bx(16);
    if (dcall(&lw, "httpd_req_get_url_query_str", p, P(q), n + 1, 0) != ESP_OK) return 1;
    if (dcall(&lw, "httpd_query_key_value", P(q), P(bstr("y")), P(v), 16) != ESP_OK) return 2;
    if (dcall(&lw, "httpd_resp_send_err", p, HTTPD_404_NOT_FOUND, P(v), 0) != ESP_OK) return 3;
    return ESP_OK;
}

static uint32_t app_ws(uint32_t p)
{
    if (R(p)->method == HTTP_GET) return ESP_OK;           /* the handshake */
    httpd_ws_frame_t *f = bx(sizeof *f);
    if (dcall(&lw, "httpd_ws_recv_frame", p, P(f), 0, 0) != ESP_OK || !f->len) return 1;
    uint8_t *buf = bx((uint32_t)f->len + 8);
    f->payload = buf;
    if (dcall(&lw, "httpd_ws_recv_frame", p, P(f), (uint32_t)f->len, 0) != ESP_OK) return 2;
    if (dcall(&lw, "httpd_ws_recv_frame", p, P(f), MEM, 0) == ESP_OK) return 3;   /* more room than the box */
    httpd_ws_frame_t *o = bx(sizeof *o);
    char *msg = bx(32);
    snprintf(msg, 32, "pong:%.*s", (int)f->len, (char *)buf);
    o->type = HTTPD_WS_TYPE_TEXT;
    o->payload = (uint8_t *)msg;
    o->len = strlen(msg);
    return dcall(&lw, "httpd_ws_send_frame", p, P(o), 0, 0);
}

static uint32_t app_work(uint32_t arg)
{
    s_work_arg = (int)arg;
    size_t *n = bx(sizeof(size_t));
    int *fds = bx(8 * sizeof(int));
    *n = 8;
    if (dcall(&lw, "httpd_get_client_list", s_tok, P(n), P(fds), 0) != ESP_OK) return 1;
    httpd_ws_frame_t *o = bx(sizeof *o);
    o->type = HTTPD_WS_TYPE_TEXT;
    o->payload = (uint8_t *)bstr("tick");
    o->len = 4;
    for (size_t i = 0; i < *n; i++)
        if (dcall(&lw, "httpd_ws_get_fd_info", s_tok, (uint32_t)fds[i], 0, 0) == HTTPD_WS_CLIENT_WEBSOCKET)
            dcall(&lw, "httpd_ws_send_frame_async", s_tok, (uint32_t)fds[i], P(o), 0);
    return 0;
}

static uint32_t app_block(uint32_t p)
{
    char *b = bx(8);
    return (int)dcall(&lw, "httpd_req_recv", p, P(b), 8, 0) > 0 ? ESP_OK : (uint32_t)ESP_FAIL;
}

typedef struct { uint32_t fn, a[4]; } up_t;

static uint32_t run(const up_t *u)
{
    loop_calls++;
    if (u->fn == F_OPEN) return app_open(u->a[0], u->a[1]);
    if (u->fn == F_CLOSE) return app_close(u->a[0], u->a[1]);
    if (u->fn == F_PAGE) return app_page(u->a[0]);
    if (u->fn == F_POST) return app_post(u->a[0]);
    if (u->fn == F_QUERY) return app_query(u->a[0]);
    if (u->fn == F_WS) return app_ws(u->a[0]);
    if (u->fn == F_WORK) return app_work(u->a[0]);
    if (u->fn == F_BLOCK) return app_block(u->a[0]);
    printf("FAIL the loop was handed %#x\n", (unsigned)u->fn);
    failed++;
    return 0;
}

static void *loop_thread(void *arg)
{
    (void)arg;
    window(&lw);
    up_t *u = bx(sizeof *u);
    uint32_t ret = 0;
    while (dcall(&lw, "feckless_box_upcall_next", P(u), ret, 0, 0)) ret = run(u);
    loop_ended = 1;
    return NULL;
}

/* ---- the server's task -------------------------------------------------------- */

typedef struct {
    httpd_handle_t h;
    int fd, method;
    const char *uri, *hdrs, *body;
    fake_resp_t out;
    esp_err_t e;
    volatile int done;
} job_t;

static void *job_thread(void *arg)
{
    job_t *j = arg;
    j->e = fake_httpd_request(j->h, j->fd, j->method, j->uri, j->hdrs, j->body, &j->out);
    j->done = 1;
    return NULL;
}

/* A request, on a thread of its own, with a watchdog. */
static void request(job_t *j)
{
    pthread_t t;
    j->done = 0;
    pthread_create(&t, NULL, job_thread, j);
    for (int i = 0; i < 500 && !j->done; i++) sleep_ms(10);
    if (!j->done) {
        printf("FAIL %s: no answer in 5 s -- hung\nhttpd_test: FAILED (exiting rather than hanging)\n", j->uri);
        exit(1);
    }
    pthread_join(t, NULL);
}

static httpd_config_t *config(void)
{
    httpd_config_t *c = bx(sizeof *c);
    c->task_priority = 5;
    c->stack_size = 10240;
    c->max_open_sockets = 4;
    c->max_uri_handlers = 20;
    return c;
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
    fake_stub_at[0] = STUB_WILD;  fake_stub_name[0] = "httpd_uri_match_wildcard";
    fake_stub_at[1] = STUB_REDIR; fake_stub_name[1] = "feckless_https_redirect";

    feckless_umode_window_t w;
    window(&w);
    uint32_t *hout = bx(8);

    /* the blocking ones unlocked */
    CHECK(calls_tab[idx_of("httpd_stop")].flags & FECKLESS_UMODE_CALL_UNLOCKED);
    CHECK(calls_tab[idx_of("httpd_req_recv")].flags & FECKLESS_UMODE_CALL_UNLOCKED);
    CHECK(calls_tab[idx_of("httpd_ws_send_frame_async")].flags & FECKLESS_UMODE_CALL_UNLOCKED);
    CHECK(!(calls_tab[idx_of("httpd_start")].flags & FECKLESS_UMODE_CALL_UNLOCKED));
    CHECK(!(calls_tab[idx_of("httpd_resp_set_type")].flags & FECKLESS_UMODE_CALL_UNLOCKED));

    /* no loop announced: no server */
    httpd_config_t *c = config();
    CHECK(dcall(&w, "httpd_start", P(hout), P(c), 0, 0) == ESP_ERR_NOT_SUPPORTED && *hout == 0);
    CHECK(fake_httpd_live == 0);

    dcall(&w, "feckless_box_upcall_loop_is", F_LOOP, 0, 0, 0);
    pthread_t lt;
    pthread_create(&lt, NULL, loop_thread, NULL);

    /* configs a box cannot follow */
    c->if_name = (struct ifreq *)mem;
    CHECK(dcall(&w, "httpd_start", P(hout), P(c), 0, 0) == ESP_ERR_INVALID_ARG);
    c->if_name = NULL;
    c->uri_match_fn = (httpd_uri_match_func_t)(uintptr_t)AT(9);     /* the app's own */
    CHECK(dcall(&w, "httpd_start", P(hout), P(c), 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(fake_httpd_live == 0);

    /* the TLS server, as the remote starts it */
    c->uri_match_fn = (httpd_uri_match_func_t)(uintptr_t)STUB_WILD;
    c->open_fn = (httpd_open_func_t)(uintptr_t)F_OPEN;
    c->close_fn = (httpd_close_func_t)(uintptr_t)F_CLOSE;
    c->global_user_ctx = (void *)0x99;
    CHECK(dcall(&w, "feckless_https_start", P(hout), P(c), 1, 0) == (uint32_t)ESP_FAIL);   /* port taken */
    CHECK(fake_httpd_live == 0 && *hout == 0);
    CHECK(dcall(&w, "feckless_https_start", P(hout), P(c), 443, 0) == ESP_OK);
    s_tok = *hout;
    httpd_handle_t real = fake_httpd_last;
    CHECK(s_tok >= 0x48535600u && s_tok < 0x48535603u && fake_httpd_is_tls(real));
    const httpd_config_t *rc = fake_httpd_cfg(real);
    CHECK(rc->uri_match_fn == httpd_uri_match_wildcard);                 /* the launcher's own */
    CHECK(rc->open_fn && (uint32_t)(uintptr_t)rc->open_fn != F_OPEN);    /* a trampoline */
    CHECK(rc->close_fn && rc->global_user_ctx != (void *)0x99);
    CHECK(rc->stack_size == 10240 && rc->max_open_sockets == 4);

    /* handlers */
    httpd_uri_t *u = bx(sizeof *u);
    struct { const char *uri; int m; uint32_t fn; bool ws; } reg[] = {
        { "/", HTTP_GET, F_PAGE, false }, { "/post", HTTP_POST, F_POST, false },
        { "/q", HTTP_GET, F_QUERY, false }, { "/ws", HTTP_GET, F_WS, true }, { "/block", HTTP_POST, F_BLOCK, false },
    };
    for (size_t i = 0; i < sizeof reg / sizeof reg[0]; i++) {
        memset(u, 0, sizeof *u);
        u->uri = bstr(reg[i].uri);
        u->method = (httpd_method_t)reg[i].m;
        u->handler = (esp_err_t (*)(httpd_req_t *))(uintptr_t)reg[i].fn;
        u->user_ctx = i == 0 ? (void *)0x1234 : NULL;
        u->is_websocket = reg[i].ws;
        CHECK(dcall(&w, "httpd_register_uri_handler", s_tok, P(u), 0, 0) == ESP_OK);
    }
    u->handler = (esp_err_t (*)(httpd_req_t *))(uintptr_t)(P(mem) + MEM + 64);   /* not in the box */
    u->uri = bstr("/bad");
    CHECK(dcall(&w, "httpd_register_uri_handler", s_tok, P(u), 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(dcall(&w, "httpd_register_uri_handler", 0x1234, P(u), 0, 0) == ESP_ERR_INVALID_ARG);

    /* a connection: the app's open_fn, on its loop, judges the peer by its socket */
    const int fd = fake_httpd_open(real);
    CHECK(fd >= 0 && s_open_fd == fd && s_open_peer_ok);

    /* a page: the strings the server keeps are copies, held until it sends */
    job_t j = { .h = real, .fd = fd, .method = HTTP_GET, .uri = "/" };
    request(&j);
    CHECK(j.e == ESP_OK && j.out.ret == ESP_OK);
    CHECK(!strcmp(j.out.status, "201 Created") && !strcmp(j.out.type, "text/plain"));
    CHECK(!strcmp(j.out.hdrs, "X-A: 1\n") && !strcmp(j.out.body, "hello"));
    /* the request is gone once its handler returned */
    CHECK(dcall(&w, "httpd_resp_send", s_saved_proxy, P(bstr("late")), 4, 0) == ESP_ERR_INVALID_ARG);
    CHECK(dcall(&w, "httpd_req_to_sockfd", P(mem) + 0x200, 0, 0, 0) == (uint32_t)-1);

    /* a post: the body read in place, a header, a cookie, the socket, chunks */
    j = (job_t){ .h = real, .fd = fd, .method = HTTP_POST, .uri = "/post",
                 .hdrs = "Origin: https://tab5.local\nCookie: a=1; sid=s3cret\n", .body = "a=1&b=22" };
    request(&j);
    CHECK(j.out.ret == ESP_OK && j.out.done && j.out.chunks == 2);
    CHECK(!strcmp(j.out.body, "a=1&b=22|https://tab5.local|s3cret|end"));

    /* a query, and an error reply */
    j = (job_t){ .h = real, .fd = fd, .method = HTTP_GET, .uri = "/q?x=5&y=hi" };
    request(&j);
    CHECK(j.out.ret == ESP_OK && j.out.err == HTTPD_404_NOT_FOUND + 1 && !strcmp(j.out.body, "hi"));

    /* a WebSocket: the handshake, then a frame through the same handler */
    j = (job_t){ .h = real, .fd = fd, .method = HTTP_GET, .uri = "/ws" };
    request(&j);
    CHECK(j.out.ret == ESP_OK);
    j = (job_t){ .h = real, .fd = fd, .method = 0, .uri = "/ws", .body = "ping" };
    request(&j);
    CHECK(j.out.ret == ESP_OK && j.out.ws_sent == 1 && !strcmp(j.out.body, "pong:ping"));

    /* queued work: run by the loop on the server's turn; it sends to every WebSocket */
    CHECK(dcall(&w, "httpd_queue_work", s_tok, F_WORK, 77, 0) == ESP_OK);
    CHECK(dcall(&w, "httpd_queue_work", s_tok, P(mem) + MEM, 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(fake_httpd_run_work(real) == 1);
    CHECK(s_work_arg == 77 && fake_ws_async_fd == fd && !strcmp(fake_ws_async, "tick"));

    /* sockets: the box's getpeername takes a server's, not any number */
    CHECK(box_httpd_lends(fd) && !box_httpd_lends(fd + 100));

    /* the fingerprint into the app's buffer */
    char *fp = bx(96);
    CHECK(dcall(&w, "feckless_https_fingerprint", P(fp), 96, 0, 0) == 1 && !strcmp(fp, "AB:CD:EF"));
    CHECK(dcall(&w, "feckless_https_fingerprint", P(fp), 4, 0, 0) == 0);

    /* an open the app refuses: it asks for the close itself */
    s_open_refuse = 1;
    CHECK(fake_httpd_open(real) == -1 && fake_httpd_closes_asked(real) == 1);
    s_open_refuse = 0;

    /* port 80: the redirect is the launcher's function, no callback */
    httpd_config_t *c2 = config();
    c2->stack_size = 4096;
    c2->uri_match_fn = (httpd_uri_match_func_t)(uintptr_t)STUB_WILD;
    CHECK(dcall(&w, "httpd_start", P(hout), P(c2), 0, 0) == ESP_OK);
    const uint32_t tok2 = *hout;
    httpd_handle_t real2 = fake_httpd_last;
    CHECK(tok2 != s_tok && !fake_httpd_is_tls(real2) && fake_httpd_live == 2);
    memset(u, 0, sizeof *u);
    u->uri = bstr("/*");
    u->method = HTTP_GET;
    u->handler = (esp_err_t (*)(httpd_req_t *))(uintptr_t)STUB_REDIR;
    CHECK(dcall(&w, "httpd_register_uri_handler", tok2, P(u), 0, 0) == ESP_OK);
    const int fd2 = fake_httpd_open(real2);
    const int calls = loop_calls;
    j = (job_t){ .h = real2, .fd = fd2, .method = HTTP_GET, .uri = "/anything" };
    request(&j);
    CHECK(fake_https_redirects == 1 && !strncmp(j.out.status, "307", 3) && loop_calls == calls);
    /* stopped: its sessions closed, its number gone */
    CHECK(dcall(&w, "httpd_stop", tok2, 0, 0, 0) == ESP_OK && fake_httpd_live == 1);
    CHECK(dcall(&w, "httpd_stop", tok2, 0, 0, 0) == ESP_ERR_INVALID_ARG);

    /* the close_fn: the app's, then the socket closed for it */
    const int fd3 = fake_httpd_open(real);
    fake_httpd_close(real, fd3);
    CHECK(s_closed_fd == fd3);

    /* the stop while a handler waits on its socket: the server's task is let
     * go, but only after the call on its request has come back */
    printf("  pages, a post, a query, a WebSocket, queued work, the redirect: answered through the loop\n");
    j = (job_t){ .h = real, .fd = fd, .method = HTTP_POST, .uri = "/block", .body = "BLOCK" };
    pthread_t t;
    j.done = 0;
    pthread_create(&t, NULL, job_thread, &j);
    sleep_ms(100);
    fake_stopping = true;
    for (int i = 0; i < 500 && !j.done; i++) sleep_ms(10);
    if (!j.done) { printf("FAIL the stop did not end a handler blocked in recv\n"); return 1; }
    pthread_join(t, NULL);
    CHECK(j.out.ret == ESP_FAIL && j.out.blocked_recv_got == 0);       /* shut down, then let go */
    for (int i = 0; i < 500 && !loop_ended; i++) sleep_ms(10);
    CHECK(loop_ended);
    pthread_join(lt, NULL);
    CHECK(fake_httpd_open(real) == -1);                                /* open_fn refused while stopping */
    printf("  the stop ended a handler blocked in recv by shutting its socket; the loop ended\n");

    /* the app's end: its servers stopped */
    feckless_box_calls_end();
    CHECK(fake_httpd_live == 0);
    fake_stopping = false;

    if (failed) { printf("httpd_test: %d FAILED\n", failed); return 1; }
    printf("httpd_test: all passed (%d callbacks)\n", loop_calls);
    return 0;
}
