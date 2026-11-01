/*
 * feckless_redirect.h -- where a plain-HTTP request should go instead
 * (8004). Pure: no IDF, host-tested (test/redirecttest.c).
 *
 * The device certificate names <name>.local and no IP address
 * (feckless_cert.h), so https://192.168.1.20/ would warn about the name
 * as well as the signature. A request that reached port 80 by IP address
 * is sent to the name; one that reached it by a name -- the device's, a
 * router's DNS entry -- keeps it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether a Host header value (with or without ":port") is an address and
 * not a name: IPv4 in dotted form (digits and dots only), or an IPv6
 * literal in brackets. NULL or empty is an address too: there is no name
 * to keep.
 */
bool feckless_redirect_is_address(const char *host_header);

/*
 * Writes "https://<host>[:<port>]<uri>" to `out`: <host> is the Host
 * header's name when it is a plain name (letters, digits, '.' and '-', at
 * most 63 characters), otherwise "<name>.local" with <name> lower case;
 * ":port" only when `https_port` is not 443; <uri> the request's path and
 * query, or "/" when it is empty, does not start with '/', has a control
 * character or a space in it, or would not fit. Returns false, with `out`
 * empty, for a NULL argument, an empty `name`, or a buffer too small for
 * even "https://" and the host.
 */
bool feckless_redirect_target(const char *host_header, const char *uri,
                              const char *name, uint16_t https_port,
                              char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
