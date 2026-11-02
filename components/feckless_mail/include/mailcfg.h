/* SPDX-License-Identifier: MIT
 *
 * mailcfg.h -- the outgoing mail account, kept in NVS, for every app on
 * the Tab5 to share: the SMTP server, its port and kind of TLS, the
 * login, an app password, and who the mail is from and to.
 *
 * Kept the way feckless_network_handler's wifistore keeps the saved
 * networks, and for its reasons (wifistore.h): in NVS, never on the
 * card, which leaves the device; and with no scheme of its own on top --
 * a device that can log in to the server can be read by whoever holds
 * it, so covering the device is CONFIG_NVS_ENCRYPTION with flash
 * encryption, a build's decision. One blob under one key, so the
 * namespace can be shared with an app's own keys.
 *
 * Through feckless_nvs_handler, so it links into an app's ELF and runs on
 * a launcher's NVS (the ABI's nvs section). Plain code, no task: every
 * call reads or writes NVS and returns.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The namespace one launcher and its apps share, unless told otherwise. */
#define MAILCFG_NS          "feckless_mail"

#define MAILCFG_HOST_MAX    (63)
#define MAILCFG_USER_MAX    (95)
#define MAILCFG_PASS_MAX    (63)
#define MAILCFG_ADDR_MAX    (95)

/* How the connection is secured. Both are TLS; plain SMTP is not offered:
 * the password would cross the network as base64. */
typedef enum {
    MAILCFG_TLS = 0,        /* TLS from the first byte ("SMTPS"), usually 465 */
    MAILCFG_STARTTLS,       /* plain, then STARTTLS before the login, usually 587 */
} mailcfg_security_t;

typedef struct {
    char     host[MAILCFG_HOST_MAX + 1];    /* smtp.example.com */
    uint16_t port;                          /* 0: the security's usual one */
    uint8_t  security;                      /* mailcfg_security_t */
    char     user[MAILCFG_USER_MAX + 1];    /* the login; often the address */
    char     pass[MAILCFG_PASS_MAX + 1];    /* an app password, as the provider issues it */
    char     from[MAILCFG_ADDR_MAX + 1];    /* "" sends from `user` */
    char     to[MAILCFG_ADDR_MAX + 1];      /* where the mail goes */
} mailcfg_t;

/* The port to connect to: `port`, or 465 / 587 for the security. */
uint16_t mailcfg_port(const mailcfg_t *c);

/* The From address: `from`, or `user` when from is empty. */
const char *mailcfg_from(const mailcfg_t *c);

/*
 * Whether c is complete enough to send: a host, a login, a password, an
 * address for To and one for From (each with one '@' and a dot after it),
 * and no control characters anywhere -- a CR or LF in a header or an SMTP
 * command would be a second line. NULL and true if so; otherwise false,
 * and *why (if given) says which field and how, for showing to a person.
 */
bool mailcfg_check(const mailcfg_t *c, const char **why);

/*
 * The account in namespace `ns` (NULL or "": MAILCFG_NS) into *out.
 * True when there is one and it passes mailcfg_check(); false leaves *out
 * zeroed but for what was stored, so a settings form can start from a
 * half-finished one. Never logs the password.
 */
bool mailcfg_load(const char *ns, mailcfg_t *out);

/* Store c, which must pass mailcfg_check(): ESP_ERR_INVALID_ARG if not. */
esp_err_t mailcfg_save(const char *ns, const mailcfg_t *c);

/* Forget it. ESP_OK if there was none. */
esp_err_t mailcfg_clear(const char *ns);

/* "@example.com" from an address, for a status line that should not show
 * the whole of it (Jinxed Writer's `sent to @example.com`). "" for none. */
void mailcfg_domain(const char *addr, char *out, size_t out_size);

/* Clear the password and anything else in c. For the caller's copies. */
void mailcfg_wipe(mailcfg_t *c);

#ifdef __cplusplus
}
#endif
