/* SPDX-License-Identifier: MIT
 *
 * mailsend.h -- send one plain-text message with the account mailcfg.h
 * keeps: SMTP over TLS, from the first byte (MAILCFG_TLS, "SMTPS") or
 * after STARTTLS (MAILCFG_STARTTLS), logged in with AUTH PLAIN or LOGIN.
 *
 * The server's certificate is checked against IDF's CA bundle
 * (esp_crt_bundle) and the host name. STARTTLS is required, not offered:
 * a server that does not advertise it gets no password, and the send
 * fails with MAILSEND_ERR_TLS. There is no plain-SMTP path, as there is
 * no plain-SMTP setting.
 *
 * Blocking, on the caller's task, which needs the network up (wifi.h) and
 * room on its stack for a TLS handshake: MAILSEND_MIN_STACK. Not from a
 * UI task -- a send is seconds, and a slow server is up to timeout_ms.
 *
 * Not in the ABI: an ELF app that calls it fails feckless_abi's check on
 * the Mbed TLS names, which the network section does not export. A
 * standalone firmware (Jinxed Writer) links it directly. An app that does
 * not call it is unaffected -- its object is not pulled into the ELF.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mailcfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the calling task's stack needs, at least: the handshake's ECDHE
 * and certificate checks run on it. */
#define MAILSEND_MIN_STACK  (12 * 1024)

/* A sensible whole-send cap (Jinxed's spec, section 3). */
#define MAILSEND_TIMEOUT_MS (45000)

typedef enum {
    MAILSEND_OK = 0,
    MAILSEND_ERR_CONFIG,        /* the account fails mailcfg_check(), or the message is unusable */
    MAILSEND_ERR_NOMEM,
    MAILSEND_ERR_DNS,           /* the host name did not resolve */
    MAILSEND_ERR_CONNECT,       /* no TCP connection */
    MAILSEND_ERR_TLS,           /* handshake, certificate, or no STARTTLS offered */
    MAILSEND_ERR_AUTH,          /* the login was refused */
    MAILSEND_ERR_REJECTED,      /* the server refused the sender, recipient or message */
    MAILSEND_ERR_PROTOCOL,      /* an answer that made no sense, or the connection dropped */
    MAILSEND_ERR_TIMEOUT,       /* timeout_ms ran out */
} mailsend_err_t;

typedef struct {
    mailsend_err_t err;
    int  smtp_code;             /* the last reply's code, 0 if none */
    /* A sentence for a person: what failed, and the server's own words
     * where it gave some. Never the password. */
    char why[128];
} mailsend_result_t;

/*
 * Send `body` (UTF-8, any line endings) with `subject` (UTF-8, one line)
 * from mailcfg_from(c) to c->to, giving up after timeout_ms in all.
 * `date` is the Date header's time as a Unix epoch, or 0 to leave the
 * header for the server to add (it will). *res is filled either way and
 * may be NULL. Returns res->err.
 */
mailsend_err_t mailsend(const mailcfg_t *c, const char *subject, const char *body,
                        int64_t date, uint32_t timeout_ms, mailsend_result_t *res);

#ifdef __cplusplus
}
#endif
