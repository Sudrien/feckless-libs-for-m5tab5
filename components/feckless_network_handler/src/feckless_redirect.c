/*
 * feckless_redirect.c -- see feckless_redirect.h. The host check and the
 * Location of Defeatist's h_redirect() (5121), with the IP case added.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_redirect.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define HOST_MAX 63

/* The Host header's name part: before any ":port", copied into `host`.
 * False if it is not a plain name or address. IPv6 in brackets is copied
 * whole ("[::1]") and is not a name. */
static bool host_part(const char *hdr, char *host, size_t cap, bool *bracket)
{
    *bracket = false;
    host[0] = '\0';
    if (!hdr || !hdr[0]) return false;
    size_t n = 0;
    if (hdr[0] == '[') {
        *bracket = true;
        return true;                /* an address; nothing else is wanted of it */
    }
    for (; hdr[n] && hdr[n] != ':'; n++) {
        const char c = hdr[n];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-')) return false;
        if (n + 1 >= cap) return false;
        host[n] = c;
    }
    host[n] = '\0';
    return n > 0;
}

static bool dotted_digits(const char *s)
{
    bool digit = false;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') digit = true;
        else if (*s != '.') return false;
    }
    return digit;
}

bool feckless_redirect_is_address(const char *host_header)
{
    char host[HOST_MAX + 1];
    bool bracket;
    if (!host_part(host_header, host, sizeof host, &bracket)) return true;
    return bracket || dotted_digits(host);
}

static bool uri_ok(const char *uri, size_t cap_left)
{
    if (!uri || uri[0] != '/') return false;
    size_t n = 0;
    for (; uri[n]; n++) {
        const unsigned char c = (unsigned char)uri[n];
        if (c <= ' ' || c == 0x7F) return false;    /* control, space: header injection */
    }
    return n < cap_left;
}

bool feckless_redirect_target(const char *host_header, const char *uri,
                              const char *name, uint16_t https_port,
                              char *out, size_t out_size)
{
    if (out && out_size) out[0] = '\0';
    if (!out || !out_size || !name || !name[0]) return false;

    char host[HOST_MAX + 1];
    bool bracket;
    const bool named = host_part(host_header, host, sizeof host, &bracket) &&
                       !bracket && !dotted_digits(host);
    if (!named) {
        /* name.local, lower case. */
        size_t n = 0;
        for (const char *p = name; *p && n + 7 < sizeof host; p++) {
            host[n++] = (char)tolower((unsigned char)*p);
        }
        memcpy(host + n, ".local", 7);
    }

    char head[HOST_MAX + 16];
    if (https_port && https_port != 443) snprintf(head, sizeof head, "https://%s:%u", host, (unsigned)https_port);
    else snprintf(head, sizeof head, "https://%s", host);
    const size_t hl = strlen(head);
    if (hl + 2 > out_size) return false;                    /* the host and at least "/" */

    const char *path = uri_ok(uri, out_size - hl) ? uri : "/";
    snprintf(out, out_size, "%s%s", head, path);
    return true;
}
