/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0077: esp_http_server as box_httpd.c sees it. A test plays the
 * server's task: fake_httpd_open() a connection (a socketpair, so the
 * socket calls are real), fake_httpd_request() a request or a WebSocket
 * frame through the registered handlers, fake_httpd_run_work() the queued
 * work, fake_httpd_close(). What the handler answered is in the request's
 * fake_resp_t. Like IDF's, the server keeps the pointers set_status,
 * set_type and set_hdr are given, and reads them only when the response
 * goes -- so a copy freed too soon is a use after free ASan sees.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "feckless_https.h"
#include "fake_httpd.h"

#define HANDLERS 24
#define SESS 8
#define WORKS 8

typedef struct {
    char *uri;
    httpd_uri_t u;
} handler_t;

typedef struct fake_srv {
    httpd_config_t cfg;
    bool tls;
    handler_t h[HANDLERS];
    int fd[SESS], peer[SESS];
    bool ws[SESS];
    httpd_work_fn_t work[WORKS];
    void *work_arg[WORKS];
    int nwork;
    int closes_asked;
} fake_srv_t;

typedef struct {
    fake_srv_t *srv;
    int fd;
    const char *hdrs, *body, *query;
    size_t body_at;
    const char *status, *type;
    const char *hk[8], *hv[8];
    int nh;
    fake_resp_t *out;
} aux_t;

int fake_httpd_live, fake_https_redirects;
httpd_handle_t fake_httpd_last;             /* the last started */
char fake_ws_async[256];
int fake_ws_async_fd = -1;
pthread_mutex_t fake_httpd_mu = PTHREAD_MUTEX_INITIALIZER;

static esp_err_t start(httpd_handle_t *out, const httpd_config_t *c, bool tls)
{
    fake_srv_t *s = calloc(1, sizeof *s);
    s->cfg = *c;
    s->tls = tls;
    for (int i = 0; i < SESS; i++) s->fd[i] = s->peer[i] = -1;
    *out = fake_httpd_last = s;
    fake_httpd_live++;
    return ESP_OK;
}
esp_err_t httpd_start(httpd_handle_t *out, const httpd_config_t *c) { return start(out, c, false); }
esp_err_t feckless_https_start(httpd_handle_t *out, const httpd_config_t *c, uint16_t port)
{
    if (port == 1) return ESP_FAIL;         /* a port the test says is taken */
    return start(out, c, true);
}

esp_err_t httpd_stop(httpd_handle_t h)
{
    fake_srv_t *s = h;
    for (int i = 0; i < SESS; i++) if (s->fd[i] >= 0) fake_httpd_close(h, s->fd[i]);
    for (int i = 0; i < HANDLERS; i++) free(s->h[i].uri);
    free(s);
    fake_httpd_live--;
    return ESP_OK;
}
esp_err_t feckless_https_stop(httpd_handle_t h) { return httpd_stop(h); }

bool fake_httpd_is_tls(httpd_handle_t h) { return ((fake_srv_t *)h)->tls; }
const httpd_config_t *fake_httpd_cfg(httpd_handle_t h) { return &((fake_srv_t *)h)->cfg; }
int fake_httpd_closes_asked(httpd_handle_t h) { return ((fake_srv_t *)h)->closes_asked; }

esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u)
{
    fake_srv_t *s = h;
    for (int i = 0; i < HANDLERS; i++) {
        if (!s->h[i].uri) {
            s->h[i].uri = strdup(u->uri);   /* httpd copies the string */
            s->h[i].u = *u;
            s->h[i].u.uri = s->h[i].uri;
            return ESP_OK;
        }
    }
    return ESP_ERR_HTTPD_HANDLERS_FULL;
}
esp_err_t httpd_unregister_uri_handler(httpd_handle_t h, const char *uri, httpd_method_t m)
{
    fake_srv_t *s = h;
    for (int i = 0; i < HANDLERS; i++) {
        if (s->h[i].uri && !strcmp(s->h[i].uri, uri) && s->h[i].u.method == m) {
            free(s->h[i].uri);
            memset(&s->h[i], 0, sizeof s->h[i]);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

int fake_httpd_open(httpd_handle_t h)
{
    fake_srv_t *s = h;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    int slot = -1;
    for (int i = 0; i < SESS && slot < 0; i++) if (s->fd[i] < 0) slot = i;
    s->fd[slot] = sv[0];
    s->peer[slot] = sv[1];
    s->ws[slot] = false;
    if (s->cfg.open_fn && s->cfg.open_fn(h, sv[0]) != ESP_OK) {
        close(sv[0]);
        close(sv[1]);
        s->fd[slot] = s->peer[slot] = -1;
        return -1;
    }
    return sv[0];
}

void fake_httpd_close(httpd_handle_t h, int fd)
{
    fake_srv_t *s = h;
    for (int i = 0; i < SESS; i++) {
        if (s->fd[i] != fd) continue;
        if (s->cfg.close_fn) s->cfg.close_fn(h, fd);   /* it closes the socket */
        else close(fd);
        close(s->peer[i]);
        s->fd[i] = s->peer[i] = -1;
    }
}

int fake_httpd_peer(httpd_handle_t h, int fd)
{
    fake_srv_t *s = h;
    for (int i = 0; i < SESS; i++) if (s->fd[i] == fd) return s->peer[i];
    return -1;
}

static bool matches(fake_srv_t *s, const handler_t *k, int method, const char *uri, size_t n)
{
    if (!k->uri) return false;
    if ((int)k->u.method != method && !(k->u.is_websocket && method == 0)) return false;
    if (s->cfg.uri_match_fn) return s->cfg.uri_match_fn(k->uri, uri, n);
    return strlen(k->uri) == n && !strncmp(k->uri, uri, n);
}

esp_err_t fake_httpd_request(httpd_handle_t h, int fd, int method, const char *uri, const char *hdrs,
                             const char *body, fake_resp_t *out)
{
    fake_srv_t *s = h;
    memset(out, 0, sizeof *out);
    const char *q = strchr(uri, '?');
    const size_t n = q ? (size_t)(q - uri) : strlen(uri);
    handler_t *k = NULL;
    for (int i = 0; i < HANDLERS && !k; i++) if (matches(s, &s->h[i], method, uri, n)) k = &s->h[i];
    if (!k) { out->no_handler = true; return ESP_ERR_NOT_FOUND; }
    if (k->u.is_websocket && method == HTTP_GET)
        for (int i = 0; i < SESS; i++) if (s->fd[i] == fd) s->ws[i] = true;
    httpd_req_t *r = calloc(1, sizeof *r);
    aux_t *a = calloc(1, sizeof *a);
    r->handle = h;
    r->method = method;
    strncpy((char *)r->uri, uri, sizeof r->uri - 1);
    r->content_len = body ? strlen(body) : 0;
    r->user_ctx = k->u.user_ctx;
    r->aux = a;
    a->srv = s; a->fd = fd; a->hdrs = hdrs ? hdrs : ""; a->body = body ? body : ""; a->query = q ? q + 1 : NULL;
    a->out = out;
    out->ret = k->u.handler(r);
    free(a);
    free(r);
    return ESP_OK;
}

int fake_httpd_run_work(httpd_handle_t h)
{
    fake_srv_t *s = h;
    int n = 0;
    while (s->nwork) {
        httpd_work_fn_t fn = s->work[0];
        void *arg = s->work_arg[0];
        memmove(s->work, s->work + 1, sizeof s->work[0] * (WORKS - 1));
        memmove(s->work_arg, s->work_arg + 1, sizeof s->work_arg[0] * (WORKS - 1));
        s->nwork--;
        fn(arg);
        n++;
    }
    return n;
}

/* ---- the request ---- */

static aux_t *A(httpd_req_t *r) { return r->aux; }
int httpd_req_to_sockfd(httpd_req_t *r) { return r ? A(r)->fd : -1; }

int httpd_req_recv(httpd_req_t *r, char *buf, size_t len)
{
    aux_t *a = A(r);
    if (!strcmp(a->body, "BLOCK")) {        /* waits on the socket, as a slow peer's does */
        char c;
        const ssize_t got = recv(a->fd, &c, 1, 0);
        a->out->blocked_recv_got = (int)got;
        return got <= 0 ? HTTPD_SOCK_ERR_FAIL : 1;
    }
    const size_t left = strlen(a->body) - a->body_at;
    const size_t n = len < left ? len : left;
    memcpy(buf, a->body + a->body_at, n);
    a->body_at += n;
    return (int)n;
}

static const char *hdr(aux_t *a, const char *field, size_t *len)
{
    const size_t fl = strlen(field);
    for (const char *p = a->hdrs; *p;) {
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        if ((size_t)(eol - p) > fl + 1 && !strncasecmp(p, field, fl) && p[fl] == ':') {
            const char *v = p + fl + 1;
            while (*v == ' ') v++;
            *len = (size_t)(eol - v);
            return v;
        }
        p = *eol ? eol + 1 : eol;
    }
    return NULL;
}

size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *field)
{
    size_t n = 0;
    return hdr(A(r), field, &n) ? n : 0;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t size)
{
    size_t n = 0;
    const char *v = hdr(A(r), field, &n);
    if (!v) return ESP_ERR_NOT_FOUND;
    const size_t c = n < size - 1 ? n : size - 1;
    memcpy(val, v, c);
    val[c] = '\0';
    return n < size ? ESP_OK : ESP_ERR_HTTPD_RESULT_TRUNC;
}
size_t httpd_req_get_url_query_len(httpd_req_t *r) { return A(r)->query ? strlen(A(r)->query) : 0; }
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len)
{
    if (!A(r)->query) return ESP_ERR_NOT_FOUND;
    snprintf(buf, len, "%s", A(r)->query);
    return strlen(A(r)->query) < len ? ESP_OK : ESP_ERR_HTTPD_RESULT_TRUNC;
}
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t size)
{
    const size_t kl = strlen(key);
    for (const char *p = qry; p && *p;) {
        const char *amp = strchr(p, '&');
        const size_t pl = amp ? (size_t)(amp - p) : strlen(p);
        if (pl > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            const size_t vl = pl - kl - 1;
            const size_t c = vl < size - 1 ? vl : size - 1;
            memcpy(val, p + kl + 1, c);
            val[c] = '\0';
            return vl < size ? ESP_OK : ESP_ERR_HTTPD_RESULT_TRUNC;
        }
        p = amp ? amp + 1 : NULL;
    }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t httpd_req_get_cookie_val(httpd_req_t *r, const char *name, char *val, size_t *size)
{
    char ck[256];
    if (httpd_req_get_hdr_value_str(r, "Cookie", ck, sizeof ck) != ESP_OK) return ESP_ERR_NOT_FOUND;
    const char *p = strstr(ck, name);
    if (!p || p[strlen(name)] != '=') return ESP_ERR_NOT_FOUND;
    p += strlen(name) + 1;
    const size_t vl = strcspn(p, ";");
    if (vl >= *size) return ESP_ERR_HTTPD_RESULT_TRUNC;
    memcpy(val, p, vl);
    val[vl] = '\0';
    *size = vl + 1;
    return ESP_OK;
}
bool httpd_uri_match_wildcard(const char *t, const char *uri, size_t n)
{
    const size_t tl = strlen(t);
    if (tl && t[tl - 1] == '*') return n >= tl - 1 && !strncmp(t, uri, tl - 1);
    return tl == n && !strncmp(t, uri, n);
}

/* ---- the response ---- */

static void head(aux_t *a)
{
    fake_resp_t *o = a->out;
    if (o->sent) return;
    o->sent = true;
    snprintf(o->status, sizeof o->status, "%s", a->status ? a->status : "200 OK");
    snprintf(o->type, sizeof o->type, "%s", a->type ? a->type : "text/html");
    for (int i = 0; i < a->nh; i++)
        snprintf(o->hdrs + strlen(o->hdrs), sizeof o->hdrs - strlen(o->hdrs), "%s: %s\n", a->hk[i], a->hv[i]);
}
static void body(aux_t *a, const char *b, ssize_t n)
{
    if (n == HTTPD_RESP_USE_STRLEN) n = b ? (ssize_t)strlen(b) : 0;
    const size_t at = strlen(a->out->body);
    const size_t room = sizeof a->out->body - 1 - at;
    const size_t c = (size_t)n < room ? (size_t)n : room;
    memcpy(a->out->body + at, b, c);
    a->out->body[at + c] = '\0';
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *b, ssize_t n) { head(A(r)); body(A(r), b, n); A(r)->out->done = true; return ESP_OK; }
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *b, ssize_t n)
{
    head(A(r));
    if (!b || n == 0) A(r)->out->done = true;
    else { body(A(r), b, n); A(r)->out->chunks++; }
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) { A(r)->status = s; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) { A(r)->type = t; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v)
{
    aux_t *a = A(r);
    if (a->nh >= 8) return ESP_FAIL;
    a->hk[a->nh] = k;
    a->hv[a->nh++] = v;
    return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t e, const char *msg)
{
    A(r)->out->err = (int)e + 1;
    snprintf(A(r)->out->body, sizeof A(r)->out->body, "%s", msg ? msg : "");
    A(r)->out->done = true;
    return ESP_OK;
}
esp_err_t feckless_https_redirect(httpd_req_t *r)
{
    fake_https_redirects++;
    snprintf(A(r)->out->status, sizeof A(r)->out->status, "307 Temporary Redirect");
    A(r)->out->sent = A(r)->out->done = true;
    return ESP_OK;
}
bool feckless_https_fingerprint(char *out, size_t size)
{
    return snprintf(out, size, "AB:CD:EF") < (int)size;
}

/* ---- sessions, work, WebSockets ---- */

void *httpd_get_global_user_ctx(httpd_handle_t h) { return ((fake_srv_t *)h)->cfg.global_user_ctx; }
esp_err_t httpd_sess_trigger_close(httpd_handle_t h, int fd) { (void)fd; ((fake_srv_t *)h)->closes_asked++; return ESP_OK; }
esp_err_t httpd_get_client_list(httpd_handle_t h, size_t *fds, int *client_fds)
{
    fake_srv_t *s = h;
    const size_t max = *fds;
    *fds = 0;
    for (int i = 0; i < SESS; i++) {
        if (s->fd[i] < 0) continue;
        if (*fds >= max) return ESP_ERR_INVALID_ARG;
        client_fds[(*fds)++] = s->fd[i];
    }
    return ESP_OK;
}
esp_err_t httpd_queue_work(httpd_handle_t h, httpd_work_fn_t fn, void *arg)
{
    fake_srv_t *s = h;
    if (s->nwork >= WORKS) return ESP_FAIL;
    s->work[s->nwork] = fn;
    s->work_arg[s->nwork++] = arg;
    return ESP_OK;
}
esp_err_t httpd_ws_recv_frame(httpd_req_t *r, httpd_ws_frame_t *f, size_t max)
{
    aux_t *a = A(r);
    f->type = HTTPD_WS_TYPE_TEXT;
    f->final = true;
    f->len = strlen(a->body);
    if (max == 0) return ESP_OK;
    if (max < f->len) return ESP_ERR_INVALID_SIZE;
    memcpy(f->payload, a->body, f->len);
    return ESP_OK;
}
esp_err_t httpd_ws_send_frame(httpd_req_t *r, httpd_ws_frame_t *f)
{
    body(A(r), (const char *)f->payload, (ssize_t)f->len);
    A(r)->out->ws_sent++;
    return ESP_OK;
}
esp_err_t httpd_ws_send_frame_async(httpd_handle_t h, int fd, httpd_ws_frame_t *f)
{
    (void)h;
    pthread_mutex_lock(&fake_httpd_mu);
    fake_ws_async_fd = fd;
    const size_t c = f->len < sizeof fake_ws_async - 1 ? f->len : sizeof fake_ws_async - 1;
    memcpy(fake_ws_async, f->payload, c);
    fake_ws_async[c] = '\0';
    pthread_mutex_unlock(&fake_httpd_mu);
    return ESP_OK;
}
httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t h, int fd)
{
    fake_srv_t *s = h;
    for (int i = 0; i < SESS; i++) if (s->fd[i] == fd) return s->ws[i] ? HTTPD_WS_CLIENT_WEBSOCKET : HTTPD_WS_CLIENT_HTTP;
    return HTTPD_WS_CLIENT_INVALID;
}
