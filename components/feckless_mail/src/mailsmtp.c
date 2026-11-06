/* SPDX-License-Identifier: MIT
 *
 * mailsmtp.c -- the SMTP conversation. See mailsmtp.h.
 *
 *   220 greeting
 *   EHLO               250 capabilities
 *   [STARTTLS  220, the handshake, EHLO again]     MAILCFG_STARTTLS
 *   AUTH PLAIN <b64>   235      (or AUTH LOGIN, 334 user, 334 pass, 235)
 *   MAIL FROM:<a>      250
 *   RCPT TO:<b>        250/251
 *   DATA               354, headers, body, "."   250
 *   QUIT               221 (not waited on for success)
 *
 * The body goes out with every line ending made CRLF and every line that
 * starts with a dot given a second one (RFC 5321 4.5.2), so the text
 * cannot end the message early. The subject is sent as it is when it is
 * printable ASCII and as RFC 2047 encoded words when it is not; a CR or
 * LF in it is refused, as mailcfg_check() refuses them in the account.
 */
#include "mailsmtp.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- results ------------------------------------------------------------ */

static mailsend_err_t fail(smtp_t *s, mailsend_err_t e, const char *what)
{
    s->res->err = e;
    /* Ours, then the server's words, cut to fit. strlcpy/strlcat rather
     * than snprintf: the line can be 512 bytes and why is 128, and the
     * firmware's -Wformat-truncation stops the build on a %s that may
     * not fit, whatever the precision says (9002). Cutting is intended. */
    char *w = s->res->why;
    const size_t n = sizeof s->res->why;
    strlcpy(w, what, n);
    if (s->line[0]) {
        strlcat(w, ": ", n);
        strlcat(w, s->line, n);
    }
    return e;
}

static mailsend_err_t io_fail(smtp_t *s, const char *what)
{
    s->line[0] = '\0';
    if (s->io->expired(s->io->io)) return fail(s, MAILSEND_ERR_TIMEOUT, "The server took too long");
    return fail(s, MAILSEND_ERR_PROTOCOL, what);
}

/* ---- out ---------------------------------------------------------------- */

static bool flush_out(smtp_t *s)
{
    size_t off = 0;
    while (off < s->out_len) {
        const int n = s->io->send(s->io->io, s->out + off, s->out_len - off);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    s->out_len = 0;
    return true;
}

static bool put(smtp_t *s, const char *p, size_t n)
{
    while (n) {
        if (s->out_len == sizeof s->out && !flush_out(s)) return false;
        size_t k = sizeof s->out - s->out_len;
        if (k > n) k = n;
        memcpy(s->out + s->out_len, p, k);
        s->out_len += k;
        p += k;
        n -= k;
    }
    return true;
}

static bool puts_(smtp_t *s, const char *str) { return put(s, str, strlen(str)); }

/* ---- in ----------------------------------------------------------------- */

/* One reply line into s->line, without its CRLF. False on a dropped or
 * failed connection, or a line longer than the RFC allows. */
static bool get_line(smtp_t *s)
{
    for (;;) {
        char *nl = memchr(s->in, '\n', s->in_len);
        if (nl) {
            size_t n = (size_t)(nl - s->in);
            const size_t used = n + 1;
            if (n && s->in[n - 1] == '\r') n--;
            if (n > SMTP_LINE_MAX) return false;
            memcpy(s->line, s->in, n);
            s->line[n] = '\0';
            memmove(s->in, s->in + used, s->in_len - used);
            s->in_len -= used;
            return true;
        }
        if (s->in_len == sizeof s->in) return false;
        const int r = s->io->recv(s->io->io, s->in + s->in_len, sizeof s->in - s->in_len);
        if (r <= 0) return false;
        s->in_len += (size_t)r;
    }
}

static bool has_word(const char *line, const char *word)
{
    /* Capabilities are space-separated words after "250-" / "250 ". */
    const size_t wl = strlen(word);
    for (const char *p = line + 4; *p; ) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ' && *e != '=') e++;
        if ((size_t)(e - p) == wl && strncasecmp(p, word, wl) == 0) return true;
        p = e;
        while (*p && *p != ' ') p++;
    }
    return false;
}

/*
 * A whole reply, multi-line or not: the code, or -1 if the connection
 * failed or the lines made no sense. With `ehlo`, the capabilities are
 * read off the lines on the way.
 */
static int reply(smtp_t *s, bool ehlo)
{
    if (ehlo) s->cap_starttls = s->cap_plain = s->cap_login = false;
    for (;;) {
        if (!get_line(s)) return -1;
        const char *l = s->line;
        if (strlen(l) < 3 || l[0] < '2' || l[0] > '5' || l[1] < '0' || l[1] > '9' ||
            l[2] < '0' || l[2] > '9' || (l[3] != '\0' && l[3] != ' ' && l[3] != '-')) {
            return -1;
        }
        const int code = (l[0] - '0') * 100 + (l[1] - '0') * 10 + (l[2] - '0');
        if (ehlo && code == 250 && l[3]) {
            if (strncasecmp(l + 4, "STARTTLS", 8) == 0 && (l[12] == '\0' || l[12] == ' ')) {
                s->cap_starttls = true;
            }
            if (strncasecmp(l + 4, "AUTH", 4) == 0 && (l[8] == ' ' || l[8] == '=')) {
                if (has_word(l + 5, "PLAIN")) s->cap_plain = true;
                if (has_word(l + 5, "LOGIN")) s->cap_login = true;
            }
        }
        if (l[3] != '-') {
            s->res->smtp_code = code;
            return code;
        }
    }
}

/* A command (CRLF added) and its reply's code, or -1. */
static int cmd(smtp_t *s, const char *a, const char *b, const char *c)
{
    if (!puts_(s, a) || (b && !puts_(s, b)) || (c && !puts_(s, c)) ||
        !puts_(s, "\r\n") || !flush_out(s)) {
        return -1;
    }
    return reply(s, false);
}

/* ---- encodings ---------------------------------------------------------- */

size_t smtp_b64(char *out, size_t out_size, const uint8_t *in, size_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t need = (n + 2) / 3 * 4;
    if (need + 1 > out_size) return 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t)in[i] << 16 |
                           (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) |
                           (i + 2 < n ? (uint32_t)in[i + 2] : 0);
        out[o++] = T[v >> 18 & 63];
        out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

void smtp_date(char *out, size_t out_size, int64_t epoch)
{
    static const char *const D[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, out_size, "%s, %02d %s %04d %02d:%02d:%02d +0000",
             D[tm.tm_wday % 7], tm.tm_mday, M[tm.tm_mon % 12], tm.tm_year + 1900,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static bool printable_ascii(const char *p)
{
    for (; *p; p++) if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) return false;
    return true;
}

/*
 * The Subject header. Printable ASCII goes as it is. Anything else is
 * encoded words, each at most 45 bytes of text (60 of base64, under the
 * 75 RFC 2047 allows) and never cutting a UTF-8 letter, folded one to a
 * line.
 */
static bool put_subject(smtp_t *s, const char *subj)
{
    if (!puts_(s, "Subject: ")) return false;
    if (printable_ascii(subj)) return puts_(s, subj) && puts_(s, "\r\n");
    const size_t n = strlen(subj);
    size_t i = 0;
    bool first = true;
    while (i < n) {
        size_t k = n - i < 45 ? n - i : 45;
        /* Back off to a letter's first byte, unless that leaves nothing. */
        while (k < n - i && k > 0 && ((unsigned char)subj[i + k] & 0xC0) == 0x80) k--;
        if (k == 0) k = n - i < 45 ? n - i : 45;
        char b[64];
        smtp_b64(b, sizeof b, (const uint8_t *)subj + i, k);
        if ((!first && !puts_(s, "\r\n ")) || !puts_(s, "=?UTF-8?B?") || !puts_(s, b) ||
            !puts_(s, "?=")) {
            return false;
        }
        first = false;
        i += k;
    }
    return puts_(s, "\r\n");
}

/* The body: CRLF line endings whatever came in, dot-stuffed, ending with
 * a line break so the terminating dot is on its own line. */
static bool put_body(smtp_t *s, const char *b)
{
    bool bol = true;
    for (; *b; b++) {
        if (*b == '\r') {
            if (b[1] == '\n') continue;     /* CRLF: the LF does it */
            if (!put(s, "\r\n", 2)) return false;
            bol = true;
            continue;
        }
        if (*b == '\n') {
            if (!put(s, "\r\n", 2)) return false;
            bol = true;
            continue;
        }
        if (bol && *b == '.' && !put(s, ".", 1)) return false;
        if (!put(s, b, 1)) return false;
        bol = false;
    }
    return bol || put(s, "\r\n", 2);
}

/* ---- an attachment (9009) ---------------------------------------------- */

static bool name_ok(const char *n)
{
    if (!n || !n[0] || strlen(n) > MAILSEND_NAME_MAX) return false;
    for (; *n; n++) {
        const unsigned char c = (unsigned char)*n;
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\' || c == '/') return false;
    }
    return true;
}

static bool type_ok(const char *t)
{
    if (!t) return true;
    const char *slash = strchr(t, '/');
    if (!slash || slash == t || !slash[1] || strchr(slash + 1, '/')) return false;
    for (; *t; t++) {
        const char c = *t;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '.' || c == '+' || c == '-')) {
            return false;
        }
    }
    return true;
}

/*
 * The boundary: "=_feckless_" and a number, which base64 can never hold
 * ("=_" is not in its alphabet) and which is checked against the body,
 * the next number tried until the body does not hold it either.
 */
static void boundary(char *b, size_t size, const char *body)
{
    for (unsigned n = 0;; n++) {
        snprintf(b, size, "=_feckless_%08x", 0x5eed0000u + n);
        if (!strstr(body, b)) return;
    }
}

/* The parts after the headers: the body inline, then the file. */
static bool put_mixed(smtp_t *s, const char *body, const mailsend_attach_t *a, const char *b)
{
    const char *type = a->type ? a->type : "application/octet-stream";
    const bool text = strncmp(type, "text/", 5) == 0;
    bool ok = puts_(s, "\r\n--") && puts_(s, b) && puts_(s, "\r\n"
                  "Content-Type: text/plain; charset=UTF-8\r\n"
                  "Content-Transfer-Encoding: 8bit\r\n\r\n") &&
              put_body(s, body) &&
              puts_(s, "--") && puts_(s, b) && puts_(s, "\r\nContent-Type: ") && puts_(s, type) &&
              (!text || puts_(s, "; charset=UTF-8")) &&
              puts_(s, "; name=\"") && puts_(s, a->name) && puts_(s, "\"\r\n"
                  "Content-Disposition: attachment; filename=\"") && puts_(s, a->name) &&
              puts_(s, "\"\r\nContent-Transfer-Encoding: base64\r\n\r\n");
    /* 57 bytes a line: 76 of base64, RFC 2045's limit. No line of it can
     * start with a dot, so none needs stuffing. */
    const uint8_t *d = a->data;
    for (size_t i = 0; ok && i < a->len; i += 57) {
        char line[80];
        const size_t k = a->len - i < 57 ? a->len - i : 57;
        const size_t n = smtp_b64(line, sizeof line, d + i, k);
        ok = put(s, line, n) && put(s, "\r\n", 2);
    }
    return ok && puts_(s, "--") && puts_(s, b) && puts_(s, "--\r\n");
}

/* ---- the dialogue ------------------------------------------------------- */

static mailsend_err_t login(smtp_t *s, const mailcfg_t *c)
{
    char b[(MAILCFG_USER_MAX + MAILCFG_PASS_MAX + 2 + 2) / 3 * 4 + 1];
    int r;
    if (s->cap_plain) {
        /* "\0user\0pass", base64. */
        uint8_t raw[MAILCFG_USER_MAX + MAILCFG_PASS_MAX + 2];
        const size_t ul = strlen(c->user), pl = strlen(c->pass);
        raw[0] = 0;
        memcpy(raw + 1, c->user, ul);
        raw[1 + ul] = 0;
        memcpy(raw + 2 + ul, c->pass, pl);
        smtp_b64(b, sizeof b, raw, ul + pl + 2);
        memset(raw, 0, sizeof raw);
        r = cmd(s, "AUTH PLAIN ", b, NULL);
    } else if (s->cap_login) {
        r = cmd(s, "AUTH LOGIN", NULL, NULL);
        if (r == 334) {
            smtp_b64(b, sizeof b, (const uint8_t *)c->user, strlen(c->user));
            r = cmd(s, b, NULL, NULL);
        }
        if (r == 334) {
            smtp_b64(b, sizeof b, (const uint8_t *)c->pass, strlen(c->pass));
            r = cmd(s, b, NULL, NULL);
        }
    } else {
        s->line[0] = '\0';
        return fail(s, MAILSEND_ERR_AUTH, "The server offers no login this can use (PLAIN or LOGIN)");
    }
    memset(b, 0, sizeof b);
    if (r < 0) return io_fail(s, "The connection dropped during the login");
    if (r != 235) return fail(s, MAILSEND_ERR_AUTH, "Login refused");
    return MAILSEND_OK;
}

static int ehlo(smtp_t *s, const char *name)
{
    if (!puts_(s, "EHLO ") || !puts_(s, name) || !puts_(s, "\r\n") || !flush_out(s)) return -1;
    return reply(s, true);
}

mailsend_err_t smtp_dialog(smtp_t *s, const smtp_io_t *io, const mailcfg_t *c,
                           const smtp_msg_t *m, mailsend_result_t *res)
{
    memset(s, 0, sizeof *s);
    s->io = io;
    s->res = res;
    memset(res, 0, sizeof *res);

    if (!m->subject || strchr(m->subject, '\r') || strchr(m->subject, '\n') || !m->body) {
        return fail(s, MAILSEND_ERR_CONFIG, "The subject must be one line");
    }
    if (m->att && (!name_ok(m->att->name) || !type_ok(m->att->type) || (m->att->len && !m->att->data))) {
        return fail(s, MAILSEND_ERR_CONFIG, "The attachment's name or type cannot go in a message");
    }

    int r = reply(s, false);
    if (r < 0) return io_fail(s, "No greeting from the server");
    if (r != 220) return fail(s, MAILSEND_ERR_PROTOCOL, "The server would not talk");

    r = ehlo(s, m->ehlo_name);
    if (r < 0) return io_fail(s, "The connection dropped after EHLO");
    if (r != 250) return fail(s, MAILSEND_ERR_PROTOCOL, "EHLO refused");

    if (c->security == MAILCFG_STARTTLS) {
        if (!s->cap_starttls) {
            s->line[0] = '\0';
            return fail(s, MAILSEND_ERR_TLS, "The server does not offer STARTTLS; not sending the password in the clear");
        }
        r = cmd(s, "STARTTLS", NULL, NULL);
        if (r < 0) return io_fail(s, "The connection dropped at STARTTLS");
        if (r != 220) return fail(s, MAILSEND_ERR_TLS, "STARTTLS refused");
        /* Anything the server sent before the handshake is not to be
         * trusted after it (RFC 3207 4.2). */
        if (s->in_len) {
            s->line[0] = '\0';
            return fail(s, MAILSEND_ERR_PROTOCOL, "The server sent data before the TLS handshake");
        }
        if (io->starttls(io->io) != 0) {
            s->line[0] = '\0';
            if (io->expired(io->io)) return fail(s, MAILSEND_ERR_TIMEOUT, "The server took too long");
            return fail(s, MAILSEND_ERR_TLS, "The TLS handshake failed (certificate or protocol)");
        }
        r = ehlo(s, m->ehlo_name);
        if (r < 0) return io_fail(s, "The connection dropped after STARTTLS");
        if (r != 250) return fail(s, MAILSEND_ERR_PROTOCOL, "EHLO refused after STARTTLS");
    }

    mailsend_err_t e = login(s, c);
    if (e != MAILSEND_OK) return e;

    r = cmd(s, "MAIL FROM:<", mailcfg_from(c), ">");
    if (r < 0) return io_fail(s, "The connection dropped at MAIL FROM");
    if (r != 250) return fail(s, MAILSEND_ERR_REJECTED, "The sender was refused");

    r = cmd(s, "RCPT TO:<", c->to, ">");
    if (r < 0) return io_fail(s, "The connection dropped at RCPT TO");
    if (r != 250 && r != 251) return fail(s, MAILSEND_ERR_REJECTED, "The recipient was refused");

    r = cmd(s, "DATA", NULL, NULL);
    if (r < 0) return io_fail(s, "The connection dropped at DATA");
    if (r != 354) return fail(s, MAILSEND_ERR_REJECTED, "The server would not take a message");

    bool ok = true;
    if (m->date > 0) {
        char d[48];
        smtp_date(d, sizeof d, m->date);
        ok = puts_(s, "Date: ") && puts_(s, d) && puts_(s, "\r\n");
    }
    ok = ok && puts_(s, "From: <") && puts_(s, mailcfg_from(c)) && puts_(s, ">\r\n") &&
         puts_(s, "To: <") && puts_(s, c->to) && puts_(s, ">\r\n") &&
         put_subject(s, m->subject) && puts_(s, "MIME-Version: 1.0\r\n");
    if (m->att) {
        char b[32];
        boundary(b, sizeof b, m->body);
        ok = ok && puts_(s, "Content-Type: multipart/mixed; boundary=\"") && puts_(s, b) &&
             puts_(s, "\"\r\n") && put_mixed(s, m->body, m->att, b);
    } else {
        ok = ok && puts_(s, "Content-Type: text/plain; charset=UTF-8\r\n"
                             "Content-Transfer-Encoding: 8bit\r\n\r\n") &&
             put_body(s, m->body);
    }
    ok = ok && puts_(s, ".\r\n") && flush_out(s);
    if (!ok) return io_fail(s, "The connection dropped while sending the message");
    r = reply(s, false);
    if (r < 0) return io_fail(s, "The connection dropped after the message");
    if (r != 250) return fail(s, MAILSEND_ERR_REJECTED, "The message was refused");

    /* Sent. QUIT is a courtesy; its answer changes nothing. */
    if (puts_(s, "QUIT\r\n") && flush_out(s)) (void)reply(s, false);
    res->err = MAILSEND_OK;
    snprintf(res->why, sizeof res->why, "Sent");
    return MAILSEND_OK;
}
