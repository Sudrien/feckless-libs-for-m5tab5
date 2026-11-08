/* SPDX-License-Identifier: MIT
 *
 * mailsend.c -- the transport under mailsmtp.c: a TCP connection through
 * lwIP and Mbed TLS over it, with one deadline for the whole send.
 *
 * Mbed TLS directly rather than esp-tls, because STARTTLS upgrades a
 * connection that has already carried plain SMTP, which esp-tls has no
 * call for. The handshake runs over the socket through set_bio, so the
 * same fd serves the plain greeting and the encrypted rest.
 *
 * Mbed TLS 4 (IDF 6) takes randomness from PSA, which psa_crypto_init()
 * starts (idempotent). feckless_cert.c does the same.
 *
 * Every socket wait is bounded by what is left of timeout_ms: connect by
 * select(), sends and receives by SO_SNDTIMEO/SO_RCVTIMEO set to the
 * remainder before each call.
 */
#include "mailsend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/build_info.h"
#include "mbedtls/ssl.h"
#include "psa/crypto.h"

#include "mailsmtp.h"

static const char *TAG = "mailsend";

typedef struct {
    int      fd;
    int64_t  deadline_us;
    bool     tls;               /* reads and writes go through ssl */
    bool     ssl_ready;
    const char *host;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;
    smtp_t   smtp;
} conn_t;

/* ---- time --------------------------------------------------------------- */

static int64_t left_us(const conn_t *c)
{
    return c->deadline_us - esp_timer_get_time();
}

static bool expired(void *io)
{
    return left_us((conn_t *)io) <= 0;
}

/* Bound the next socket call by what is left. False when nothing is. */
static bool arm(conn_t *c)
{
    const int64_t l = left_us(c);
    if (l <= 0) return false;
    struct timeval tv = { .tv_sec = (time_t)(l / 1000000), .tv_usec = (suseconds_t)(l % 1000000) };
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return true;
}

/* ---- the plain socket, and as Mbed TLS's bio ---------------------------- */

static int raw_send(conn_t *c, const unsigned char *p, size_t n)
{
    if (!arm(c)) return -1;
    return (int)send(c->fd, p, n, 0);
}

static int raw_recv(conn_t *c, unsigned char *p, size_t n)
{
    if (!arm(c)) return -1;
    return (int)recv(c->fd, p, n, 0);
}

static int bio_send(void *ctx, const unsigned char *p, size_t n)
{
    const int r = raw_send(ctx, p, n);
    if (r >= 0) return r;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_TIMEOUT
                                                     : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int bio_recv(void *ctx, unsigned char *p, size_t n)
{
    const int r = raw_recv(ctx, p, n);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_TIMEOUT
                                                     : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

/* ---- smtp_io_t ---------------------------------------------------------- */

static int io_send(void *io, const char *p, size_t n)
{
    conn_t *c = io;
    if (!c->tls) return raw_send(c, (const unsigned char *)p, n);
    for (;;) {
        const int r = mbedtls_ssl_write(&c->ssl, (const unsigned char *)p, n);
        if (r >= 0) return r;
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) return -1;
        if (expired(c)) return -1;
    }
}

static int io_recv(void *io, char *p, size_t n)
{
    conn_t *c = io;
    if (!c->tls) return raw_recv(c, (unsigned char *)p, n);
    for (;;) {
        const int r = mbedtls_ssl_read(&c->ssl, (unsigned char *)p, n);
        if (r >= 0) return r;
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        /* TLS 1.3 tickets arrive as a "read" that gives no data. */
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            && r != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
#endif
            ) {
            return -1;
        }
        if (expired(c)) return -1;
    }
}

static int tls_start(void *io)
{
    conn_t *c = io;
    int r;
    /* Everything initialised before anything can fail, so the free in
     * mailsend() is always of initialised contexts. */
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    c->ssl_ready = true;
    if (psa_crypto_init() != PSA_SUCCESS) return -1;

    r = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) { ESP_LOGW(TAG, "ssl config: -0x%04x", -r); return -1; }
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    if (esp_crt_bundle_attach(&c->conf) != ESP_OK) { ESP_LOGW(TAG, "no CA bundle"); return -1; }
    r = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (r != 0) { ESP_LOGW(TAG, "ssl setup: -0x%04x", -r); return -1; }
    r = mbedtls_ssl_set_hostname(&c->ssl, c->host);
    if (r != 0) return -1;
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);

    while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGW(TAG, "handshake with %s: -0x%04x, verify 0x%lx", c->host, -r,
                     (unsigned long)mbedtls_ssl_get_verify_result(&c->ssl));
            return -1;
        }
        if (expired(c)) return -1;
    }
    c->tls = true;
    return 0;
}

/* ---- connect ------------------------------------------------------------ */

static mailsend_err_t open_tcp(conn_t *c, const char *host, uint16_t port, mailsend_result_t *res)
{
    char ps[6];
    snprintf(ps, sizeof ps, "%u", (unsigned)port);
    const struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *ai = NULL;
    if (getaddrinfo(host, ps, &hints, &ai) != 0 || !ai) {
        snprintf(res->why, sizeof res->why, "%.60s did not resolve", host);
        return MAILSEND_ERR_DNS;
    }
    mailsend_err_t e = MAILSEND_ERR_CONNECT;
    for (struct addrinfo *a = ai; a; a = a->ai_next) {
        const int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        /* Non-blocking for the connect, so select() can bound it. */
        const int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int r = connect(fd, a->ai_addr, a->ai_addrlen);
        if (r != 0 && errno == EINPROGRESS) {
            const int64_t l = left_us(c);
            struct timeval tv = { .tv_sec = (time_t)(l > 0 ? l / 1000000 : 0),
                                  .tv_usec = (suseconds_t)(l > 0 ? l % 1000000 : 0) };
            fd_set w;
            FD_ZERO(&w);
            FD_SET(fd, &w);
            r = -1;
            if (select(fd + 1, NULL, &w, NULL, &tv) == 1) {
                int err = 0;
                socklen_t el = sizeof err;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0) r = 0;
            } else if (expired(c)) {
                e = MAILSEND_ERR_TIMEOUT;
            }
        }
        if (r == 0) {
            fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
            c->fd = fd;
            e = MAILSEND_OK;
            break;
        }
        close(fd);
        if (e == MAILSEND_ERR_TIMEOUT) break;
    }
    freeaddrinfo(ai);
    if (e == MAILSEND_ERR_CONNECT) {
        snprintf(res->why, sizeof res->why, "Could not connect to %.60s port %u", host, (unsigned)port);
    } else if (e == MAILSEND_ERR_TIMEOUT) {
        snprintf(res->why, sizeof res->why, "Connecting to %.60s took too long", host);
    }
    return e;
}

/* ---- the call ----------------------------------------------------------- */

mailsend_err_t mailsend(const mailcfg_t *cfg, const char *subject, const char *body,
                        int64_t date, uint32_t timeout_ms, mailsend_result_t *res)
{
    return mailsend_with(cfg, subject, body, NULL, 0, date, timeout_ms, res);
}

mailsend_err_t mailsend_with(const mailcfg_t *cfg, const char *subject, const char *body,
                             const mailsend_attach_t *att, size_t n_att,
                             int64_t date, uint32_t timeout_ms, mailsend_result_t *res)
{
    mailsend_result_t local;
    if (!res) res = &local;
    memset(res, 0, sizeof *res);

    const char *why = NULL;
    if (!cfg || !mailcfg_check(cfg, &why)) {
        res->err = MAILSEND_ERR_CONFIG;
        snprintf(res->why, sizeof res->why, "%s", why ? why : "No account");
        return res->err;
    }

    /* Big (the TLS contexts and the conversation's buffers): heap, PSRAM
     * if there is any. */
    conn_t *c = heap_caps_calloc(1, sizeof *c, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c) c = calloc(1, sizeof *c);
    if (!c) {
        res->err = MAILSEND_ERR_NOMEM;
        snprintf(res->why, sizeof res->why, "Out of memory");
        return res->err;
    }
    c->fd = -1;
    c->host = cfg->host;
    c->deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    mailsend_err_t e = open_tcp(c, cfg->host, mailcfg_port(cfg), res);
    if (e == MAILSEND_OK && cfg->security == MAILCFG_TLS && tls_start(c) != 0) {
        e = expired(c) ? MAILSEND_ERR_TIMEOUT : MAILSEND_ERR_TLS;
        snprintf(res->why, sizeof res->why, "%s",
                 e == MAILSEND_ERR_TIMEOUT ? "The server took too long"
                                           : "The TLS handshake failed (certificate or protocol)");
    }
    if (e == MAILSEND_OK) {
        res->err = MAILSEND_OK;
        const smtp_io_t io = { .send = io_send, .recv = io_recv, .starttls = tls_start,
                               .expired = expired, .io = c };
        const smtp_msg_t m = { .subject = subject, .body = body, .date = date,
                               .ehlo_name = "feckless.local", .att = att, .n_att = n_att };
        e = smtp_dialog(&c->smtp, &io, cfg, &m, res);
    }
    res->err = e;

    if (c->tls) mbedtls_ssl_close_notify(&c->ssl);
    if (c->ssl_ready) {
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
    }
    if (c->fd >= 0) close(c->fd);
    memset(&c->smtp, 0, sizeof c->smtp);    /* the AUTH line passed through it */
    free(c);

    if (e == MAILSEND_OK) ESP_LOGI(TAG, "sent to %s", cfg->to);
    else ESP_LOGW(TAG, "not sent (%d): %s", (int)e, res->why);
    return e;
}
