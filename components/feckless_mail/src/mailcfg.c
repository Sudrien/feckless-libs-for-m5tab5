/* SPDX-License-Identifier: MIT
 *
 * mailcfg.c -- see mailcfg.h.
 */
#include "mailcfg.h"

#include <stdio.h>
#include <string.h>

#include "nvs_handler.h"

/* nvs.h's, without its header: ESP_ERR_NVS_BASE + 0x02. */
#ifndef ESP_ERR_NVS_NOT_FOUND
#define ESP_ERR_NVS_NOT_FOUND   (0x1100 + 0x02)
#endif

#define KEY         "acct"
#define VERSION     (1)

/* What is written: a version byte ahead of the account, so a later layout
 * can tell this one apart and read it instead of failing. */
typedef struct {
    uint8_t   version;
    mailcfg_t c;
} blob_t;

static const char *ns_or_default(const char *ns)
{
    return ns && ns[0] ? ns : MAILCFG_NS;
}

uint16_t mailcfg_port(const mailcfg_t *c)
{
    if (c->port) return c->port;
    return c->security == MAILCFG_STARTTLS ? 587 : 465;
}

const char *mailcfg_from(const mailcfg_t *c)
{
    return c->from[0] ? c->from : c->user;
}

static bool printable(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7f) return false;
    return true;
}

/* One '@' with something before it, and a dot after it with something
 * either side; no spaces, angle brackets or commas, which would make an
 * envelope address into something else. */
static bool address(const char *s)
{
    const char *at = strchr(s, '@');
    if (!at || at == s || strchr(at + 1, '@')) return false;
    const char *dot = strrchr(at, '.');
    if (!dot || dot == at + 1 || !dot[1]) return false;
    for (; *s; s++)
        if (*s == ' ' || *s == '<' || *s == '>' || *s == ',' || *s == '"') return false;
    return true;
}

bool mailcfg_check(const mailcfg_t *c, const char **why)
{
    const char *w = NULL;
    if (!printable(c->host) || !printable(c->user) || !printable(c->pass) ||
        !printable(c->from) || !printable(c->to))
        w = "A field has a control character in it.";
    else if (!c->host[0] || strchr(c->host, ' ') || !strchr(c->host, '.'))
        w = "Server: a name such as smtp.example.com.";
    else if (c->security > MAILCFG_STARTTLS)
        w = "Security: TLS or STARTTLS.";
    else if (!c->user[0])
        w = "Login: the account's user name, often its address.";
    else if (!c->pass[0])
        w = "Password: the app password the provider gave.";
    else if (!address(mailcfg_from(c)))
        w = c->from[0] ? "From: an address such as me@example.com."
                       : "From: an address, since the login is not one.";
    else if (!address(c->to))
        w = "To: an address such as notes@example.com.";
    if (why) *why = w;
    return w == NULL;
}

bool mailcfg_load(const char *ns, mailcfg_t *out)
{
    blob_t b;
    memset(&b, 0, sizeof b);
    memset(out, 0, sizeof *out);
    size_t len = sizeof b;
    if (nvsh_get_blob(ns_or_default(ns), KEY, &b, &len) != ESP_OK ||
        len != sizeof b || b.version != VERSION) {
        mailcfg_wipe(&b.c);
        return false;
    }
    /* terminators, whatever was stored */
    b.c.host[MAILCFG_HOST_MAX] = b.c.user[MAILCFG_USER_MAX] = b.c.pass[MAILCFG_PASS_MAX] = '\0';
    b.c.from[MAILCFG_ADDR_MAX] = b.c.to[MAILCFG_ADDR_MAX] = '\0';
    *out = b.c;
    mailcfg_wipe(&b.c);
    return mailcfg_check(out, NULL);
}

esp_err_t mailcfg_save(const char *ns, const mailcfg_t *c)
{
    if (!mailcfg_check(c, NULL)) return ESP_ERR_INVALID_ARG;
    blob_t b;
    memset(&b, 0, sizeof b);    /* padding and the strings' tails: no stale bytes */
    b.version = VERSION;
    memcpy(b.c.host, c->host, strlen(c->host));
    b.c.port = c->port;
    b.c.security = c->security;
    memcpy(b.c.user, c->user, strlen(c->user));
    memcpy(b.c.pass, c->pass, strlen(c->pass));
    memcpy(b.c.from, c->from, strlen(c->from));
    memcpy(b.c.to, c->to, strlen(c->to));
    const esp_err_t err = nvsh_set_blob(ns_or_default(ns), KEY, &b, sizeof b);
    mailcfg_wipe(&b.c);
    return err;
}

esp_err_t mailcfg_clear(const char *ns)
{
    const esp_err_t err = nvsh_erase_key(ns_or_default(ns), KEY);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

void mailcfg_domain(const char *addr, char *out, size_t out_size)
{
    if (!out_size) return;
    const char *at = addr ? strrchr(addr, '@') : NULL;
    snprintf(out, out_size, "%s", at ? at : "");
}

void mailcfg_wipe(mailcfg_t *c)
{
    volatile unsigned char *p = (volatile unsigned char *)c;
    for (size_t i = 0; i < sizeof *c; i++) p[i] = 0;
}
