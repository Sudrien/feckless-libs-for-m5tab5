/* SPDX-License-Identifier: MIT
 *
 * mailsmtp.h -- the SMTP conversation, apart from the sockets and the TLS
 * under it, so the host test can hold it against a scripted server
 * (test/smtptest.c). mailsend.c supplies the transport.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mailcfg.h"
#include "mailsend.h"

/* The transport. Each returns what send()/recv() would: bytes, 0 for a
 * closed connection (recv), <0 for an error; a timeout is an error, and
 * expired() then says so. starttls() upgrades the same connection in
 * place, 0 on success. */
typedef struct {
    int  (*send)(void *io, const char *p, size_t n);
    int  (*recv)(void *io, char *p, size_t n);
    int  (*starttls)(void *io);
    bool (*expired)(void *io);
    void *io;
} smtp_io_t;

/* The conversation's state. Heap or static, not a stack: about 1.3 KB. */
#define SMTP_LINE_MAX   (512)   /* RFC 5321's reply line is 512 with CRLF */
typedef struct {
    const smtp_io_t *io;
    char   in[SMTP_LINE_MAX * 2];
    size_t in_len;
    char   out[SMTP_LINE_MAX];
    size_t out_len;
    char   line[SMTP_LINE_MAX + 1];  /* the last reply line, for why */
    bool   cap_starttls, cap_plain, cap_login;
    mailsend_result_t *res;
} smtp_t;

typedef struct {
    const char *subject;
    const char *body;
    int64_t     date;       /* 0: no Date header */
    const char *ehlo_name;  /* what we call ourselves in EHLO */
} smtp_msg_t;

/* The whole dialogue, greeting to QUIT. The account has passed
 * mailcfg_check(). Fills *res (not NULL here) and returns res->err. */
mailsend_err_t smtp_dialog(smtp_t *s, const smtp_io_t *io, const mailcfg_t *c,
                           const smtp_msg_t *m, mailsend_result_t *res);

/* Exposed for the test. */
size_t smtp_b64(char *out, size_t out_size, const uint8_t *in, size_t n);
void   smtp_date(char *out, size_t out_size, int64_t epoch);
