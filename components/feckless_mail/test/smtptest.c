/* SPDX-License-Identifier: MIT
 *
 * Host test for mailsmtp.c, the SMTP conversation under mailsend(): a
 * scripted server, step by step -- what the client must say, what the
 * server answers, where the TLS handshake comes -- and the client's
 * every line checked against it. Each script runs twice, once with the
 * server's replies whole and once a byte at a time, so a reply split
 * across reads is a case and not an accident.
 *
 * Checked: SMTPS with AUTH PLAIN; STARTTLS with AUTH LOGIN only; no
 * STARTTLS offered (no password sent); data before the handshake
 * refused; login refused; recipient refused; multi-line replies; the body
 * made CRLF and dot-stuffed; a UTF-8 subject as encoded words that never
 * cut a letter; a CR in the subject refused before anything is sent; a
 * timeout; a dropped connection; base64 against RFC 4648's vectors; the
 * Date header.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mailsmtp.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- the scripted server ------------------------------------------------ */

typedef enum { S, C, DATA, TLS, TLS_FAIL, TIMEOUT, DROP, END } kind_t;
typedef struct { kind_t k; const char *s; } step_t;

typedef struct {
    const step_t *st;
    int  i;
    bool bytewise;
    const char *pending;    /* the rest of a server reply being handed out */
    char got[8192];         /* what the client sent, not yet matched */
    size_t got_len;
    char data[4096];        /* the DATA block, for the checks */
    bool timed_out;
    bool tls;
    bool script_broken;
    char sent_all[16384];   /* everything the client sent, for "no password" */
    size_t sent_all_len;
} fake_t;

static int f_send(void *io, const char *p, size_t n)
{
    fake_t *f = io;
    if (f->got_len + n > sizeof f->got || f->sent_all_len + n > sizeof f->sent_all) return -1;
    memcpy(f->got + f->got_len, p, n);
    f->got_len += n;
    memcpy(f->sent_all + f->sent_all_len, p, n);
    f->sent_all_len += n;
    return (int)n;
}

/* Match C and DATA steps against what the client has sent; false if a
 * step cannot be matched yet or does not match. */
static bool consume(fake_t *f)
{
    for (;;) {
        const step_t *s = &f->st[f->i];
        if (s->k == C) {
            char *nl = memchr(f->got, '\n', f->got_len);
            if (!nl) return false;
            const size_t n = (size_t)(nl - f->got) + 1;
            char line[1024];
            memcpy(line, f->got, n);
            line[n] = '\0';
            char want[1024];
            snprintf(want, sizeof want, "%s\r\n", s->s);
            if (strcmp(line, want) != 0) {
                printf("  step %d: client said [%.*s], script wants [%s]\n", f->i,
                       (int)n - 2, line, s->s);
                f->script_broken = true;
                return false;
            }
            memmove(f->got, f->got + n, f->got_len - n);
            f->got_len -= n;
            f->i++;
        } else if (s->k == DATA) {
            char *end = NULL;
            for (size_t j = 0; j + 5 <= f->got_len; j++) {
                if (!memcmp(f->got + j, "\r\n.\r\n", 5)) { end = f->got + j + 5; break; }
            }
            if (!end) return false;
            const size_t n = (size_t)(end - f->got);
            memcpy(f->data, f->got, n < sizeof f->data - 1 ? n : sizeof f->data - 1);
            f->data[n < sizeof f->data - 1 ? n : sizeof f->data - 1] = '\0';
            memmove(f->got, end, f->got_len - n);
            f->got_len -= n;
            f->i++;
        } else {
            return true;
        }
    }
}

static int f_recv(void *io, char *p, size_t n)
{
    fake_t *f = io;
    if (!f->pending) {
        consume(f);
        const step_t *s = &f->st[f->i];
        if (s->k == TIMEOUT) { f->timed_out = true; return -1; }
        if (s->k == DROP) return 0;
        if (s->k != S) { f->script_broken = f->script_broken || s->k != END; return -1; }
        f->pending = s->s;
        f->i++;
    }
    size_t k = strlen(f->pending);
    if (f->bytewise && k > 1) k = 1;
    if (k > n) k = n;
    memcpy(p, f->pending, k);
    f->pending += k;
    if (!*f->pending) f->pending = NULL;
    return (int)k;
}

static int f_starttls(void *io)
{
    fake_t *f = io;
    consume(f);
    if (f->st[f->i].k == TLS) { f->i++; f->tls = true; return 0; }
    if (f->st[f->i].k == TLS_FAIL) { f->i++; return -1; }
    f->script_broken = true;
    return -1;
}

static bool f_expired(void *io) { return ((fake_t *)io)->timed_out; }

static smtp_t s_smtp;   /* not on the stack: about 1.5 KB */

/* Run a script both ways. `fk` keeps the bytewise run's state. */
static mailsend_err_t run(const step_t *st, const mailcfg_t *c, const smtp_msg_t *m,
                          mailsend_result_t *res, fake_t *fk)
{
    mailsend_err_t first = MAILSEND_OK;
    for (int pass = 0; pass < 2; pass++) {
        memset(fk, 0, sizeof *fk);
        fk->st = st;
        fk->bytewise = pass == 1;
        const smtp_io_t io = { f_send, f_recv, f_starttls, f_expired, fk };
        const mailsend_err_t e = smtp_dialog(&s_smtp, &io, c, m, res);
        if (pass == 0) first = e;
        else CHECK(e == first);
        consume(fk);
        CHECK(!fk->script_broken);
    }
    return first;
}

/* ---- scripts ------------------------------------------------------------ */

static const mailcfg_t k_tls = {
    .host = "smtp.example.com", .port = 0, .security = MAILCFG_TLS,
    .user = "me@example.com", .pass = "app pass", .from = "", .to = "notes@example.org",
};

/* b64("\0me@example.com\0app pass") */
#define PLAIN "AG1lQGV4YW1wbGUuY29tAGFwcCBwYXNz"

static const smtp_msg_t k_msg = {
    .subject = "Test", .body = "line one\n.dot\r\nline three\r..x", .date = 0,
    .ehlo_name = "feckless.local",
};

static void test_tls_plain(void)
{
    static const step_t st[] = {
        { S, "220 smtp.example.com ESMTP ready\r\n" },
        { C, "EHLO feckless.local" },
        { S, "250-smtp.example.com\r\n250-SIZE 35882577\r\n250-AUTH LOGIN PLAIN XOAUTH2\r\n250 8BITMIME\r\n" },
        { C, "AUTH PLAIN " PLAIN },
        { S, "235 2.7.0 Accepted\r\n" },
        { C, "MAIL FROM:<me@example.com>" },
        { S, "250 OK\r\n" },
        { C, "RCPT TO:<notes@example.org>" },
        { S, "250 OK\r\n" },
        { C, "DATA" },
        { S, "354 Go ahead\r\n" },
        { DATA, NULL },
        { S, "250 2.0.0 OK queued\r\n" },
        { C, "QUIT" },
        { S, "221 bye\r\n" },
        { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &k_msg, &res, fk) == MAILSEND_OK);
    CHECK(res.smtp_code == 221 || res.smtp_code == 250);
    CHECK(strstr(fk->data, "From: <me@example.com>\r\n"));
    CHECK(strstr(fk->data, "To: <notes@example.org>\r\n"));
    CHECK(strstr(fk->data, "Subject: Test\r\n"));
    CHECK(strstr(fk->data, "Content-Type: text/plain; charset=UTF-8\r\n"));
    CHECK(!strstr(fk->data, "Date:"));
    /* CRLF everywhere, the dots doubled at line starts only. */
    CHECK(strstr(fk->data, "\r\n\r\nline one\r\n..dot\r\nline three\r\n...x\r\n.\r\n"));
    free(fk);
}

static void test_starttls_login(void)
{
    mailcfg_t c = k_tls;
    c.security = MAILCFG_STARTTLS;
    static const step_t st[] = {
        { S, "220 mx ESMTP\r\n" },
        { C, "EHLO feckless.local" },
        { S, "250-mx\r\n250-STARTTLS\r\n250 PIPELINING\r\n" },
        { C, "STARTTLS" },
        { S, "220 2.0.0 Ready to start TLS\r\n" },
        { TLS, NULL },
        { C, "EHLO feckless.local" },
        { S, "250-mx\r\n250 AUTH=LOGIN\r\n" },
        { C, "AUTH LOGIN" },
        { S, "334 VXNlcm5hbWU6\r\n" },
        { C, "bWVAZXhhbXBsZS5jb20=" },          /* me@example.com */
        { S, "334 UGFzc3dvcmQ6\r\n" },
        { C, "YXBwIHBhc3M=" },                  /* app pass */
        { S, "235 ok\r\n" },
        { C, "MAIL FROM:<me@example.com>" },
        { S, "250 ok\r\n" },
        { C, "RCPT TO:<notes@example.org>" },
        { S, "251 will forward\r\n" },
        { C, "DATA" },
        { S, "354 end with .\r\n" },
        { DATA, NULL },
        { S, "250 queued\r\n" },
        { C, "QUIT" },
        { DROP, NULL },                         /* QUIT unanswered: still sent */
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    const smtp_msg_t m = { .subject = "Notes", .body = "hi", .date = 1793580000,
                           .ehlo_name = "feckless.local" };
    CHECK(run(st, &c, &m, &res, fk) == MAILSEND_OK);
    CHECK(fk->tls);
    CHECK(strstr(fk->data, "Date: Mon, 02 Nov 2026 "));
    CHECK(strstr(fk->data, "\r\n\r\nhi\r\n.\r\n"));
    free(fk);
}

static void test_no_starttls(void)
{
    mailcfg_t c = k_tls;
    c.security = MAILCFG_STARTTLS;
    static const step_t st[] = {
        { S, "220 mx\r\n" },
        { C, "EHLO feckless.local" },
        { S, "250-mx\r\n250 AUTH PLAIN LOGIN\r\n" },
        { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &c, &k_msg, &res, fk) == MAILSEND_ERR_TLS);
    CHECK(strstr(res.why, "STARTTLS"));
    CHECK(!memmem(fk->sent_all, fk->sent_all_len, "AUTH", 4));     /* no password went */
    free(fk);
}

static void test_injection_before_tls(void)
{
    mailcfg_t c = k_tls;
    c.security = MAILCFG_STARTTLS;
    static const step_t st[] = {
        { S, "220 mx\r\n" },
        { C, "EHLO feckless.local" },
        { S, "250-mx\r\n250 STARTTLS\r\n" },
        { C, "STARTTLS" },
        { S, "220 go\r\n250 injected\r\n" },    /* arrives in the same read */
        { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    /* Whole: the extra line is buffered and caught. Bytewise it is still
     * on the wire when the handshake would start, so the fake's TLS step
     * is missing and that run fails differently; check the whole run. */
    memset(fk, 0, sizeof *fk);
    fk->st = st;
    const smtp_io_t io = { f_send, f_recv, f_starttls, f_expired, fk };
    CHECK(smtp_dialog(&s_smtp, &io, &c, &k_msg, &res) == MAILSEND_ERR_PROTOCOL);
    CHECK(!fk->tls);
    free(fk);
}

static void test_tls_fail(void)
{
    mailcfg_t c = k_tls;
    c.security = MAILCFG_STARTTLS;
    static const step_t st[] = {
        { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { S, "250 STARTTLS\r\n" },
        { C, "STARTTLS" }, { S, "220 go\r\n" }, { TLS_FAIL, NULL }, { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &c, &k_msg, &res, fk) == MAILSEND_ERR_TLS);
    free(fk);
}

static void test_auth_refused(void)
{
    static const step_t st[] = {
        { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { S, "250 AUTH PLAIN\r\n" },
        { C, "AUTH PLAIN " PLAIN },
        { S, "535-5.7.8 Username and Password not accepted.\r\n535 5.7.8 Learn more\r\n" },
        { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &k_msg, &res, fk) == MAILSEND_ERR_AUTH);
    CHECK(res.smtp_code == 535);
    CHECK(strstr(res.why, "Login refused: 535 5.7.8"));
    CHECK(!strstr(res.why, "app pass"));
    free(fk);
}

static void test_no_auth_method(void)
{
    static const step_t st[] = {
        { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { S, "250-mx\r\n250 AUTH XOAUTH2 CRAM-MD5\r\n" },
        { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &k_msg, &res, fk) == MAILSEND_ERR_AUTH);
    free(fk);
}

static void test_rcpt_refused(void)
{
    static const step_t st[] = {
        { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { S, "250 AUTH PLAIN\r\n" },
        { C, "AUTH PLAIN " PLAIN }, { S, "235 ok\r\n" },
        { C, "MAIL FROM:<me@example.com>" }, { S, "250 ok\r\n" },
        { C, "RCPT TO:<notes@example.org>" }, { S, "550 5.1.1 No such user\r\n" },
        { END, NULL },
    };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &k_msg, &res, fk) == MAILSEND_ERR_REJECTED);
    CHECK(res.smtp_code == 550);
    CHECK(strstr(res.why, "recipient"));
    free(fk);
}

static void test_timeout_and_drop(void)
{
    static const step_t t[] = { { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { TIMEOUT, NULL } };
    static const step_t d[] = { { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { DROP, NULL } };
    static const step_t g[] = { { S, "hello there\r\n" }, { END, NULL } };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(t, &k_tls, &k_msg, &res, fk) == MAILSEND_ERR_TIMEOUT);
    CHECK(run(d, &k_tls, &k_msg, &res, fk) == MAILSEND_ERR_PROTOCOL);
    CHECK(run(g, &k_tls, &k_msg, &res, fk) == MAILSEND_ERR_PROTOCOL);
    free(fk);
}

static void test_subject(void)
{
    static const step_t st[] = {
        { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { S, "250 AUTH PLAIN\r\n" },
        { C, "AUTH PLAIN " PLAIN }, { S, "235 ok\r\n" },
        { C, "MAIL FROM:<me@example.com>" }, { S, "250 ok\r\n" },
        { C, "RCPT TO:<notes@example.org>" }, { S, "250 ok\r\n" },
        { C, "DATA" }, { S, "354 go\r\n" }, { DATA, NULL }, { S, "250 ok\r\n" },
        { C, "QUIT" }, { S, "221 bye\r\n" }, { END, NULL },
    };
    /* 30 é, two bytes each: 60 bytes, so two words. Byte 45 is the
     * second half of a letter, so the first word must stop at 44. */
    char subj[128] = "";
    for (int i = 0; i < 30; i++) strcat(subj, "\xc3\xa9");
    const smtp_msg_t m = { .subject = subj, .body = "b", .ehlo_name = "feckless.local" };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &m, &res, fk) == MAILSEND_OK);
    char want[64];
    smtp_b64(want, sizeof want, (const uint8_t *)subj, 45);
    CHECK(!strstr(fk->data, want));
    smtp_b64(want, sizeof want, (const uint8_t *)subj, 44);
    char line[128];
    snprintf(line, sizeof line, "Subject: =?UTF-8?B?%s?=\r\n =?UTF-8?B?", want);
    CHECK(strstr(fk->data, line));
    free(fk);

    /* A CR in the subject: refused before a byte is sent. */
    const smtp_msg_t bad = { .subject = "a\rRCPT TO:<x@y.z>", .body = "b", .ehlo_name = "e" };
    static const step_t none[] = { { END, NULL } };
    fk = calloc(1, sizeof *fk);
    fk->st = none;
    const smtp_io_t io = { f_send, f_recv, f_starttls, f_expired, fk };
    CHECK(smtp_dialog(&s_smtp, &io, &k_tls, &bad, &res) == MAILSEND_ERR_CONFIG);
    CHECK(fk->sent_all_len == 0);
    free(fk);
}

/* 9009: a file with the message. */
static size_t unb64(uint8_t *out, const char *in)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t v = 0;
    int bits = 0;
    size_t n = 0;
    for (; *in && *in != '-'; in++) {
        const char *p = strchr(T, *in);
        if (!p || !*in) continue;               /* CRLF, '=' */
        v = v << 6 | (uint32_t)(p - T);
        bits += 6;
        if (bits >= 8) { bits -= 8; out[n++] = (uint8_t)(v >> bits); }
    }
    return n;
}

static void test_attach(void)
{
    static const step_t st[] = {
        { S, "220 mx\r\n" }, { C, "EHLO feckless.local" }, { S, "250 AUTH PLAIN\r\n" },
        { C, "AUTH PLAIN " PLAIN }, { S, "235 ok\r\n" },
        { C, "MAIL FROM:<me@example.com>" }, { S, "250 ok\r\n" },
        { C, "RCPT TO:<notes@example.org>" }, { S, "250 ok\r\n" },
        { C, "DATA" }, { S, "354 go\r\n" }, { DATA, NULL }, { S, "250 ok\r\n" },
        { C, "QUIT" }, { S, "221 bye\r\n" }, { END, NULL },
    };
    /* 200 bytes, every value, so four lines of base64 and a short one;
     * and a body that holds the first boundary, and a dot to stuff. */
    uint8_t file[200];
    for (int i = 0; i < 200; i++) file[i] = (uint8_t)(i * 37 + 11);
    const mailsend_attach_t att = { .name = "2026-11-06.md", .type = "text/markdown",
                                    .data = file, .len = sizeof file };
    const smtp_msg_t m = { .subject = "Notes", .body = "# notes\n.dot\n--=_feckless_5eed0000\n",
                           .ehlo_name = "feckless.local", .att = &att };
    mailsend_result_t res;
    fake_t *fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &m, &res, fk) == MAILSEND_OK);
    const char *d = fk->data;
    CHECK(strstr(d, "MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"=_feckless_5eed0001\"\r\n\r\n--=_feckless_5eed0001\r\n"));
    CHECK(!strstr(d, "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n--"));
    CHECK(strstr(d, "Content-Transfer-Encoding: 8bit\r\n\r\n# notes\r\n..dot\r\n--=_feckless_5eed0000\r\n--=_feckless_5eed0001\r\n"));
    CHECK(strstr(d, "Content-Type: text/markdown; charset=UTF-8; name=\"2026-11-06.md\"\r\n"
                    "Content-Disposition: attachment; filename=\"2026-11-06.md\"\r\n"
                    "Content-Transfer-Encoding: base64\r\n\r\n"));
    CHECK(strstr(d, "\r\n--=_feckless_5eed0001--\r\n.\r\n"));
    /* The base64: lines of 76, and back to the same bytes. */
    const char *b64 = strstr(d, "base64\r\n\r\n");
    CHECK(b64);
    if (b64) {
        b64 += strlen("base64\r\n\r\n");
        const char *e = strchr(b64, '\r');
        CHECK(e && e - b64 == 76);
        uint8_t back[256];
        CHECK(unb64(back, b64) == sizeof file && !memcmp(back, file, sizeof file));
    }
    free(fk);

    /* No type: octet-stream, no charset. Empty data: no base64 lines. */
    const mailsend_attach_t bin = { .name = "x.bin", .data = NULL, .len = 0 };
    const smtp_msg_t mb = { .subject = "s", .body = "b", .ehlo_name = "feckless.local", .att = &bin };
    fk = calloc(1, sizeof *fk);
    CHECK(run(st, &k_tls, &mb, &res, fk) == MAILSEND_OK);
    CHECK(strstr(fk->data, "Content-Type: application/octet-stream; name=\"x.bin\"\r\n"));
    CHECK(strstr(fk->data, "base64\r\n\r\n--=_feckless_5eed0000--\r\n"));
    free(fk);

    /* Names and types that would break the headers: refused, nothing sent. */
    static const mailsend_attach_t bad[] = {
        { .name = "a\"b.md", .type = "text/plain" },
        { .name = "a\r\nBcc: x@y.z", .type = "text/plain" },
        { .name = "../x", .type = "text/plain" },
        { .name = "", .type = "text/plain" },
        { .name = "ok.md", .type = "text" },
        { .name = "ok.md", .type = "text/plain; x=1" },
        { .name = "ok.md", .type = "text/plain", .data = NULL, .len = 3 },
    };
    static const step_t none[] = { { END, NULL } };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const smtp_msg_t mx = { .subject = "s", .body = "b", .ehlo_name = "e", .att = &bad[i] };
        fk = calloc(1, sizeof *fk);
        fk->st = none;
        const smtp_io_t io = { f_send, f_recv, f_starttls, f_expired, fk };
        CHECK(smtp_dialog(&s_smtp, &io, &k_tls, &mx, &res) == MAILSEND_ERR_CONFIG);
        CHECK(fk->sent_all_len == 0);
        free(fk);
    }
}

static void test_b64(void)
{
    static const struct { const char *in, *out; } v[] = {
        { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" },
        { "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" },
    };
    char b[32];
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        smtp_b64(b, sizeof b, (const uint8_t *)v[i].in, strlen(v[i].in));
        CHECK(!strcmp(b, v[i].out));
    }
    CHECK(smtp_b64(b, 4, (const uint8_t *)"foo", 3) == 0);   /* no room for the NUL */
    char d[48];
    smtp_date(d, sizeof d, 0);
    CHECK(!strcmp(d, "Thu, 01 Jan 1970 00:00:00 +0000"));
}

int main(void)
{
    test_b64();
    test_tls_plain();
    test_starttls_login();
    test_no_starttls();
    test_injection_before_tls();
    test_tls_fail();
    test_auth_refused();
    test_no_auth_method();
    test_rcpt_refused();
    test_timeout_and_drop();
    test_subject();
    test_attach();
    if (fails) { printf("smtptest: %d failed\n", fails); return 1; }
    printf("smtptest: ok\n");
    return 0;
}
