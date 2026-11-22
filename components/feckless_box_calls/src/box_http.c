/* SPDX-License-Identifier: MIT
 *
 * esp_http_client for a boxed app (u-feckless-0073): the launcher's client,
 * a number in the app's hands, over the routes box_net.c answers.
 *
 * THE CLIENT
 * - esp_http_client_init() takes the app's config, copies what it can stand
 *   behind into the host -- the URL and the other strings, a server
 *   certificate -- and gives a number, TOKEN_HTTP + slot, HTTP_MAX at once.
 *   A config that asks for what a box cannot follow -- a client certificate
 *   or key, an interface, ALPN, the DS peripheral, async -- is refused (no
 *   number, and a line saying which). crt_bundle_attach set means the
 *   launcher's own bundle.
 * - THE EVENT HANDLER IS NEVER CALLED: it is the app's code, and a box does
 *   not call back into its app. The headers it would have been shown are
 *   kept by the client (CONFIG_ESP_HTTP_CLIENT_SAVE_RESPONSE_HEADERS) and
 *   read with esp_http_client_get_response_header() after fetch_headers;
 *   without that option it answers ESP_ERR_NOT_SUPPORTED. perform() runs,
 *   but its body goes to events, so it is for a request whose answer is
 *   its status -- read the body with open, fetch_headers and read.
 * - Strings the client hands back (get_header, get_response_header) are
 *   copied into one buffer per number in the box, HDR_MAX bytes, good until
 *   the next such call on that number or its cleanup. A post field is
 *   copied too: the client keeps only the pointer.
 *
 * WAITING, AND THE STOP
 * - open, write, fetch_headers, read, perform, close and cleanup are
 *   unlocked: they wait in lwIP, for as long as the app's timeout_ms says
 *   (and each one wait at most that). A box's stop must end a host call
 *   within FECKLESS_UMODE_STOP_WAIT_MS, so while any number has a call in
 *   flight a 100 ms esp_timer watches for the stop, and when it comes
 *   shuts down the socket of every request in flight
 *   (esp_http_client_get_socket(), lwip_shutdown()), which ends the wait.
 *   A DNS lookup inside open cannot be ended that way; it ends at lwIP's
 *   own timeout.
 * - cleanup and close on a number another of the app's threads is using
 *   shut its socket down and wait for that call to come back, so the host
 *   never frees a client under a call.
 *
 * In the network section; built where box_net.c is (the launcher links
 * feckless_network_handler), an empty table otherwise or with
 * FECKLESS_BOX_NO_NET.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("ethernet.h") && __has_include("lwip/sockets.h") && __has_include("esp_http_client.h") && \
    !defined(FECKLESS_BOX_NO_NET)

#include <stdlib.h>

#include "esp_http_client.h"
#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define HAVE_BUNDLE 1
#else
#define HAVE_BUNDLE 0
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "feckless_umode_box.h"
#include "feckless_umode_load.h"

static const char *TAG = "box_http";

#define TOKEN_HTTP   0x48545400u            /* "HTT\0" + slot */
#define HTTP_MAX     6                      /* clients at once */
#define HDR_MAX      2048                   /* a header value handed back, bytes: a CDN's Location */
#define URL_MAX      2048
#define STR_MAX      256                    /* host, user agent, common name, key */
#define CRED_MAX     128                    /* username, password */
#define PART_MAX     1024                   /* path, query */
#define CERT_MAX     (8 * 1024)
#define POST_MAX     (16 * 1024)
#define IO_MAX       (64 * 1024)            /* bytes a read or write moves at most: more is a short one */
#define BUF_MAX      (16 * 1024)            /* buffer_size, buffer_size_tx */
#define REAP_US      (100 * 1000)
#define DRAIN_MS     3000                   /* cleanup's wait for a call in flight */

typedef struct {
    bool used;
    bool dying;                             /* cleanup has begun: no new calls */
    int busy;                               /* calls in flight on this number */
    bool reaped;                            /* its socket shut down for the stop */
    esp_http_client_handle_t c;
    char *strs;                             /* the config's strings, one block */
    char *cert;
    char *post;
    uintptr_t vbuf;                         /* HDR_MAX in the box */
} hc_t;

static hc_t s_hc[HTTP_MAX];
static SemaphoreHandle_t s_lock;            /* the table; never held across a wait */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;   /* s_lock's making */
static esp_timer_handle_t s_reaper;
static int s_in_flight;

static bool stopping(void) { return feckless_umode_app_stopping(feckless_umode_app_loaded()); }

static void lock(void)
{
    if (!s_lock) {                          /* box_image.c's binary semaphore, made once */
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

/* The stop's watch: every request in flight has its socket shut down,
 * once. Not static: the host test runs it by hand. */
void box_http_reap(void *arg)
{
    (void)arg;
    if (!stopping()) return;
    lock();
    for (int i = 0; i < HTTP_MAX; i++) {
        hc_t *h = &s_hc[i];
        if (h->used && h->busy && !h->reaped) {
            const int fd = esp_http_client_get_socket(h->c);
            if (fd >= 0) lwip_shutdown(fd, SHUT_RDWR);
            h->reaped = true;
        }
    }
    unlock();
}

static void reaper_on(void)
{
    if (!s_reaper) {
        const esp_timer_create_args_t a = { .callback = box_http_reap, .name = "box_http" };
        if (esp_timer_create(&a, &s_reaper) != ESP_OK) s_reaper = NULL;
    }
    if (s_reaper) esp_timer_start_periodic(s_reaper, REAP_US);
}

static hc_t *slot_of(uint32_t tok)
{
    const uint32_t i = tok - TOKEN_HTTP;
    return i < HTTP_MAX && s_hc[i].used ? &s_hc[i] : NULL;
}

/* A number's client for one call, counted in flight; NULL if it is not one
 * the app holds, or is being cleaned up, or the app is stopping. */
static hc_t *enter(uint32_t tok)
{
    lock();
    hc_t *h = slot_of(tok);
    if (h && (h->dying || stopping())) h = NULL;
    if (h) {
        h->busy++;
        if (s_in_flight++ == 0) reaper_on();
    }
    unlock();
    return h;
}

static void leave(hc_t *h)
{
    lock();
    if (--h->busy == 0) h->reaped = false;
    if (--s_in_flight == 0 && s_reaper) esp_timer_stop(s_reaper);
    unlock();
}

/* A short call that does not wait: the table's lock is enough, since
 * cleanup takes it too. */
static esp_http_client_handle_t peek(uint32_t tok)
{
    hc_t *h = slot_of(tok);
    return h && !h->dying ? h->c : NULL;
}

/* ---- init --------------------------------------------------------------- */

/* A string of the app's into `*at`, advancing it; NULL stays NULL. False if
 * it is not a string within `max` in the box. */
static bool take(win_t *w, const char *p, uint32_t max, char **at, const char **out)
{
    *out = NULL;
    if (!p) return true;
    const char *s = str(w, (uint32_t)(uintptr_t)p, max);
    if (!s) return false;
    const size_t n = strlen(s) + 1;
    memcpy(*at, s, n);
    *out = *at;
    *at += n;
    return true;
}

static bool refuse(const char *what)
{
    ESP_LOGW(TAG, "esp_http_client_init: refused -- %s is not for a box", what);
    return false;
}

/* (config *) -> number, or 0 */
H(c_esp_http_client_init)
{
    *ret = 0;
    if (!ok_ptr(w, a[0], sizeof(esp_http_client_config_t))) return true;
    esp_http_client_config_t in;
    memcpy(&in, (const void *)(uintptr_t)a[0], sizeof in);

    if (in.client_cert_pem) return refuse("a client certificate"), true;
    if (in.client_key_pem || in.client_key) return refuse("a client key"), true;
    if (in.client_key_password) return refuse("a client key's password"), true;
    if (in.if_name) return refuse("an interface"), true;
    if (in.is_async) return refuse("async"), true;
#ifdef CONFIG_MBEDTLS_HARDWARE_ECDSA_SIGN
    if (in.use_ecdsa_peripheral) return refuse("the ECDSA peripheral"), true;
#endif
#if CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS
    if (in.alpn_protos) return refuse("ALPN"), true;
#endif
    /* u-feckless-0074: not ciphersuites_list or groups_list -- fields of
     * newer ESP-IDF only, which a field test cannot be written for. Like
     * every field not copied below, they reach the client as NULL: the
     * defaults, not the app's arrays. */
#if CONFIG_ESP_TLS_USE_DS_PERIPHERAL
    if (in.ds_data) return refuse("the DS peripheral"), true;
#endif
#if CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT
    if (in.transport) return refuse("a custom transport"), true;
#endif
#if !HAVE_BUNDLE
    if (in.crt_bundle_attach) return refuse("the certificate bundle (not in this launcher)"), true;
#endif

    int slot = -1;
    lock();
    for (int i = 0; i < HTTP_MAX; i++) {
        if (!s_hc[i].used) { slot = i; s_hc[i] = (hc_t){ .used = true, .dying = true }; break; }
    }
    unlock();
    if (slot < 0) {
        ESP_LOGW(TAG, "esp_http_client_init: all %d clients in use", HTTP_MAX);
        return true;
    }
    hc_t *h = &s_hc[slot];

    const size_t room = URL_MAX + 3 * STR_MAX + 2 * CRED_MAX + 2 * PART_MAX;
    h->strs = malloc(room);
    char *at = h->strs;
    esp_http_client_config_t c = { 0 };
    bool ok = h->strs &&
              take(w, in.url, URL_MAX, &at, &c.url) &&
              take(w, in.host, STR_MAX, &at, &c.host) &&
              take(w, in.username, CRED_MAX, &at, &c.username) &&
              take(w, in.password, CRED_MAX, &at, &c.password) &&
              take(w, in.path, PART_MAX, &at, &c.path) &&
              take(w, in.query, PART_MAX, &at, &c.query) &&
              take(w, in.user_agent, STR_MAX, &at, &c.user_agent) &&
              take(w, in.common_name, STR_MAX, &at, &c.common_name);
    if (ok && in.cert_pem) {
        /* cert_pem and cert_der share the field: cert_len bytes, or a string */
        const uint32_t n = in.cert_len ? (uint32_t)in.cert_len : 0;
        if (n) {
            ok = n <= CERT_MAX && ok_ptr(w, (uint32_t)(uintptr_t)in.cert_pem, n);
            if (ok && (h->cert = malloc(n))) memcpy(h->cert, in.cert_pem, n);
        } else {
            const char *s = str(w, (uint32_t)(uintptr_t)in.cert_pem, CERT_MAX);
            ok = s != NULL;
            if (ok && (h->cert = malloc(strlen(s) + 1))) strcpy(h->cert, s);
        }
        ok = ok && h->cert;
        c.cert_pem = h->cert;
        c.cert_len = in.cert_len;
    }
    if (!ok) {
        ESP_LOGW(TAG, "esp_http_client_init: a string of the config is not in the box, or too long");
        goto fail;
    }
    c.port = in.port;
    c.auth_type = in.auth_type;
    c.tls_version = in.tls_version;
    c.method = in.method;
    c.timeout_ms = in.timeout_ms;
    c.disable_auto_redirect = in.disable_auto_redirect;
    c.max_redirection_count = in.max_redirection_count;
    c.max_authorization_retries = in.max_authorization_retries;
    c.transport_type = in.transport_type;
    c.buffer_size = in.buffer_size > BUF_MAX ? BUF_MAX : in.buffer_size;
    c.buffer_size_tx = in.buffer_size_tx > BUF_MAX ? BUF_MAX : in.buffer_size_tx;
    c.use_global_ca_store = in.use_global_ca_store;
    c.skip_cert_common_name_check = in.skip_cert_common_name_check;
#if HAVE_BUNDLE
    c.crt_bundle_attach = in.crt_bundle_attach ? esp_crt_bundle_attach : NULL;
#endif
    c.addr_type = in.addr_type;
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    c.save_client_session = in.save_client_session;
#endif
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER
    c.tls_dyn_buf_strategy = in.tls_dyn_buf_strategy;
#endif
    c.keep_alive_enable = in.keep_alive_enable;
    c.keep_alive_idle = in.keep_alive_idle;
    c.keep_alive_interval = in.keep_alive_interval;
    c.keep_alive_count = in.keep_alive_count;
    /* event_handler and user_data: the app's, and not called (above) */

    h->vbuf = feckless_box_alloc(box_of_win(w), HDR_MAX);
    h->c = h->vbuf ? esp_http_client_init(&c) : NULL;
    if (!h->c) {
        ESP_LOGW(TAG, "esp_http_client_init: the client would not start");
        goto fail;
    }
    lock();
    h->dying = false;
    unlock();
    *ret = TOKEN_HTTP + (uint32_t)slot;
    return true;
fail:
    if (h->vbuf) feckless_box_free(box_of_win(w), h->vbuf);
    free(h->strs);
    free(h->cert);
    lock();
    *h = (hc_t){ 0 };
    unlock();
    return true;
}

/* ---- calls that do not wait --------------------------------------------- */

#define SHORT(body)                                                          \
    do {                                                                     \
        (void)w;                                                             \
        lock();                                                              \
        esp_http_client_handle_t c = peek(a[0]);                             \
        if (c) { body; } else { *ret = (uint32_t)ESP_ERR_INVALID_ARG; }      \
        unlock();                                                            \
    } while (0)

/* (h, url) */
H(c_esp_http_client_set_url)
{
    const char *u = str(w, a[1], URL_MAX);
    char *copy = u ? strdup(u) : NULL;
    if (!copy) { *ret = (uint32_t)ESP_ERR_INVALID_ARG; return true; }
    SHORT(*ret = (uint32_t)esp_http_client_set_url(c, copy));
    free(copy);                             /* the client keeps its own */
    return true;
}

/* (h, key, value) */
H(c_esp_http_client_set_header)
{
    const char *k = str(w, a[1], STR_MAX);
    const char *v = a[2] ? str(w, a[2], HDR_MAX) : NULL;
    if (!k || (a[2] && !v)) { *ret = (uint32_t)ESP_ERR_INVALID_ARG; return true; }
    char *kc = strdup(k), *vc = v ? strdup(v) : NULL;   /* off the host's stack, the app's threads aside */
    if (!kc || (v && !vc)) {
        *ret = (uint32_t)ESP_ERR_NO_MEM;
    } else {
        SHORT(*ret = (uint32_t)esp_http_client_set_header(c, kc, vc));
    }
    free(kc);
    free(vc);
    return true;
}

H(c_esp_http_client_set_method) { SHORT(*ret = (uint32_t)esp_http_client_set_method(c, (esp_http_client_method_t)a[1])); return true; }
H(c_esp_http_client_set_redirection) { SHORT(*ret = (uint32_t)esp_http_client_set_redirection(c)); return true; }
H(c_esp_http_client_set_timeout_ms) { SHORT(*ret = (uint32_t)esp_http_client_set_timeout_ms(c, (int)a[1])); return true; }
H(c_esp_http_client_get_status_code) { SHORT(*ret = (uint32_t)esp_http_client_get_status_code(c)); return true; }
H(c_esp_http_client_is_complete_data_received) { SHORT(*ret = esp_http_client_is_complete_data_received(c)); return true; }

H(c_esp_http_client_get_content_length)
{
    int64_t n = -1;
    lock();
    esp_http_client_handle_t c = peek(a[0]);
    if (c) n = esp_http_client_get_content_length(c);
    unlock();
    feckless_umode_ret64(w, (uint64_t)n, ret);
    return true;
}

/* (h, data, len): the client keeps the pointer, so the data is kept here */
H(c_esp_http_client_set_post_field)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const int len = (int)a[2];
    if (len < 0 || len > POST_MAX || (len && !ok_ptr(w, a[1], (uint32_t)len))) return true;
    char *copy = NULL;
    if (len) {
        copy = malloc((size_t)len);
        if (!copy) return true;
        memcpy(copy, (const void *)(uintptr_t)a[1], (size_t)len);
    }
    lock();
    hc_t *h = slot_of(a[0]);
    if (h && !h->dying) {
        *ret = (uint32_t)esp_http_client_set_post_field(h->c, copy, len);
        free(h->post);
        h->post = copy;
        copy = NULL;
    }
    unlock();
    free(copy);
    return true;
}

/* (h, url, len): the URL now, into the app's buffer */
H(c_esp_http_client_get_url)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const uint32_t len = a[2] > URL_MAX ? URL_MAX : a[2];
    if (!len || !ok_ptr(w, a[1], len)) return true;
    char *tmp = malloc(len);
    if (!tmp) return true;
    tmp[0] = '\0';
    SHORT(*ret = (uint32_t)esp_http_client_get_url(c, tmp, (int)len));
    tmp[len - 1] = '\0';
    memcpy((void *)(uintptr_t)a[1], tmp, strlen(tmp) + 1);
    free(tmp);
    return true;
}

/* A header's value into the number's box buffer; *value that, or NULL. */
typedef esp_err_t (*hdr_fn)(esp_http_client_handle_t, const char *, char **);

static uint32_t header_of(win_t *w, const uint32_t a[7], hdr_fn fn)
{
    const char *k = str(w, a[1], STR_MAX);
    if (!k || !ok_ptr(w, a[2], sizeof(char *))) return (uint32_t)ESP_ERR_INVALID_ARG;
    char kc[STR_MAX];
    strcpy(kc, k);
    esp_err_t e = ESP_ERR_INVALID_ARG;
    char *got = NULL;
    lock();
    hc_t *h = slot_of(a[0]);
    if (h && !h->dying) {
        e = fn(h->c, kc, &got);
        char *dst = (char *)h->vbuf;
        if (e == ESP_OK && got) {
            const size_t n = strnlen(got, HDR_MAX - 1);
            memcpy(dst, got, n);
            dst[n] = '\0';
            *(char **)(uintptr_t)a[2] = dst;
        } else {
            *(char **)(uintptr_t)a[2] = NULL;
        }
    }
    unlock();
    return (uint32_t)e;
}

/* (h, key, char **value): the request's header, as the client's own */
H(c_esp_http_client_get_header) { *ret = header_of(w, a, esp_http_client_get_header); return true; }

#if CONFIG_ESP_HTTP_CLIENT_SAVE_RESPONSE_HEADERS
/* (h, key, char **value): the last response's -- what the event handler
 * would have been shown */
H(c_esp_http_client_get_response_header) { *ret = header_of(w, a, esp_http_client_get_response_header); return true; }
#else
H(c_esp_http_client_get_response_header)
{
    if (ok_ptr(w, a[2], sizeof(char *))) *(char **)(uintptr_t)a[2] = NULL;
    *ret = (uint32_t)ESP_ERR_NOT_SUPPORTED;
    return true;
}
#endif

/* An app calling it itself has no TLS config of the host's to give it; as
 * a config's crt_bundle_attach it is honoured (init). */
H(c_esp_crt_bundle_attach) { (void)w; (void)a; *ret = (uint32_t)ESP_ERR_NOT_SUPPORTED; return true; }

/* ---- calls that wait ------------------------------------------------------ */

/* (h, write_len) */
H(c_esp_http_client_open)
{
    (void)w;
    hc_t *h = enter(a[0]);
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!h) return true;
    *ret = (uint32_t)esp_http_client_open(h->c, (int)a[1]);
    leave(h);
    return true;
}

/* (h, buffer, len) -> bytes or -1 */
H(c_esp_http_client_write)
{
    *ret = (uint32_t)-1;
    const uint32_t len = a[2] > IO_MAX ? IO_MAX : a[2];    /* a short write, as the client may give */
    if ((int32_t)a[2] < 0 || (len && !ok_ptr(w, a[1], len))) return true;
    hc_t *h = enter(a[0]);
    if (!h) return true;
    *ret = (uint32_t)esp_http_client_write(h->c, (const char *)(uintptr_t)a[1], (int)len);
    leave(h);
    return true;
}

/* (h) -> int64: the content length, 0 chunked, or < 0 */
H(c_esp_http_client_fetch_headers)
{
    int64_t n = ESP_FAIL;
    hc_t *h = enter(a[0]);
    if (h) {
        n = esp_http_client_fetch_headers(h->c);
        leave(h);
    }
    feckless_umode_ret64(w, (uint64_t)n, ret);
    return true;
}

/* (h, buffer, len) -> bytes, 0 at the end, or < 0; in place in the box.
 * Only the part read into is checked: a picture's fetch asks for what is
 * left of a 512 KB buffer and gets IO_MAX of it, as a read may. */
H(c_esp_http_client_read)
{
    *ret = (uint32_t)-1;
    const uint32_t len = a[2] > IO_MAX ? IO_MAX : a[2];    /* a short read, as the client may give */
    if ((int32_t)a[2] < 0 || !ok_ptr(w, a[1], len)) return true;
    hc_t *h = enter(a[0]);
    if (!h) return true;
    *ret = (uint32_t)esp_http_client_read(h->c, (char *)(uintptr_t)a[1], (int)len);
    leave(h);
    return true;
}

H(c_esp_http_client_perform)
{
    (void)w;
    hc_t *h = enter(a[0]);
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!h) return true;
    *ret = (uint32_t)esp_http_client_perform(h->c);
    leave(h);
    return true;
}

/* Shut a number's socket down and wait for its other calls to come back;
 * with the table locked on return. False if it is not the app's. */
static hc_t *drain(uint32_t tok, bool mark_dying)
{
    lock();
    hc_t *h = slot_of(tok);
    if (!h || h->dying) {
        unlock();
        return NULL;
    }
    if (mark_dying) h->dying = true;
    if (h->busy) {
        const int fd = esp_http_client_get_socket(h->c);
        if (fd >= 0) lwip_shutdown(fd, SHUT_RDWR);
        for (int ms = 0; h->busy && ms < DRAIN_MS; ms += 10) {
            unlock();
            vTaskDelay(pdMS_TO_TICKS(10));
            lock();
        }
    }
    return h;
}

H(c_esp_http_client_close)
{
    (void)w;
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    hc_t *h = drain(a[0], false);
    if (!h) return true;
    h->busy++;                              /* close may wait in lwIP: in flight */
    if (s_in_flight++ == 0) reaper_on();
    unlock();
    *ret = (uint32_t)esp_http_client_close(h->c);
    leave(h);
    return true;
}

H(c_esp_http_client_cleanup)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    hc_t *h = drain(a[0], true);
    if (!h) return true;
    if (h->busy) {                          /* never came back: leave it rather than free it under a call */
        ESP_LOGE(TAG, "cleanup: a call on this client did not come back in %d ms; left", DRAIN_MS);
        unlock();
        return true;
    }
    esp_http_client_handle_t c = h->c;
    unlock();
    *ret = (uint32_t)esp_http_client_cleanup(c);
    feckless_umode_app_t *app = feckless_umode_app_loaded();
    feckless_umode_app_lock(app);
    if (h->vbuf) feckless_box_free(box_of_win(w), h->vbuf);
    feckless_umode_app_unlock(app);
    free(h->strs);
    free(h->cert);
    free(h->post);
    lock();
    *h = (hc_t){ 0 };
    unlock();
    return true;
}

const feckless_umode_call_t box_http_calls[] = {
    FN(esp_http_client_init), FN(esp_http_client_set_url), FN(esp_http_client_set_header),
    FN(esp_http_client_set_method), FN(esp_http_client_set_post_field), FN(esp_http_client_set_redirection),
    FN(esp_http_client_set_timeout_ms), FN(esp_http_client_get_status_code),
    FN(esp_http_client_get_content_length), FN(esp_http_client_is_complete_data_received),
    FN(esp_http_client_get_url), FN(esp_http_client_get_header), FN(esp_http_client_get_response_header),
    FN(esp_crt_bundle_attach),
    FNU(esp_http_client_open), FNU(esp_http_client_write), FNU(esp_http_client_fetch_headers),
    FNU(esp_http_client_read), FNU(esp_http_client_perform), FNU(esp_http_client_close),
    FNU(esp_http_client_cleanup),
};
const uint32_t box_http_calls_n = sizeof box_http_calls / sizeof box_http_calls[0];

/* The app's clients cleaned up (its threads are gone; nothing is in flight). */
void box_http_end(void)
{
    if (s_reaper) {
        esp_timer_stop(s_reaper);
        esp_timer_delete(s_reaper);
        s_reaper = NULL;
    }
    for (int i = 0; i < HTTP_MAX; i++) {
        hc_t *h = &s_hc[i];
        if (!h->used) continue;
        if (h->c && !h->busy) esp_http_client_cleanup(h->c);
        free(h->strs);
        free(h->cert);
        free(h->post);
        *h = (hc_t){ 0 };
    }
    s_in_flight = 0;
}

#else  /* no network in this launcher */

const feckless_umode_call_t box_http_calls[1];
const uint32_t box_http_calls_n = 0;
void box_http_end(void) {}

#endif
