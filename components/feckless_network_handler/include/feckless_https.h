/*
 * feckless_https.h -- the device's HTTPS server identity, owned by the
 * launcher (8004).
 *
 * A launcher, or the shim of an app's standalone .bin, makes one
 * self-signed certificate for the device (feckless_cert.h), keeps it in
 * NVS, and starts HTTPS servers with it. An app asks for a server and is
 * never given the key: it supplies the same httpd_config_t it would for
 * httpd_start(), its handlers and its open_fn, and the port. So the
 * fingerprint a browser was told to accept is the device's, across apps
 * and across firmware updates, and one browser exception covers every app
 * on the device; and the key can stay out of an app's reach when apps are
 * sandboxed, which is the plan -- today an ELF app shares the launcher's
 * address space and could read it.
 *
 * The three calls are in the ABI's network section; set_name() is the
 * launcher's alone.
 *
 * The identity is made the first time a server starts (a second or so on
 * the P4; the caller waits) and kept in the "feckless_https" NVS
 * namespace. Erasing NVS makes a new one, and every browser warns again.
 * NVS must be up: the launcher brings it up before an app runs.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start an HTTPS server on `port` with the device certificate. `cfg` is
 * copied: HTTPD_DEFAULT_CONFIG() with what the app changes, including
 * open_fn, which runs on the server's task as in httpd_start(). Like
 * httpd_ssl_start(), it takes the port from `port`, not from
 * cfg->server_port. ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_SUPPORTED
 * when this Mbed TLS has no X.509 writer (feckless_cert.h), ESP_FAIL when
 * the identity could not be made, or httpd_ssl_start()'s own error.
 */
esp_err_t feckless_https_start(httpd_handle_t *out, const httpd_config_t *cfg, uint16_t port);

/* Stop a server feckless_https_start() made. */
esp_err_t feckless_https_stop(httpd_handle_t handle);

/*
 * A handler for the app's own plain-HTTP server (port 80), to register
 * on the wildcard URI (httpd_uri_match_wildcard) for GET: it answers with a 307
 * to https:// on the port feckless_https_start() last used. A request that
 * reached the device by IP address goes to "<name>.local", the name the
 * certificate carries; one that came by a name keeps it
 * (feckless_redirect.h). Nothing else is served in the clear. The app
 * keeps the server, so it can stand down for a setup portal that needs
 * port 80, and its open_fn, so it can refuse what is off the network.
 *
 * It sends a browser to a name that has to resolve, so the first
 * successful feckless_https_start() also starts an mDNS responder for it
 * ("<name>.local", and an _https._tcp service on the port). That is the
 * station interface and the IDF's predefined ones; the USB Ethernet
 * interface is not registered with it (8004), so over the cable the name
 * resolves only where the network's own DNS knows it. Call the app's
 * server start before its redirect.
 */
esp_err_t feckless_https_redirect(httpd_req_t *req);

/*
 * The certificate's SHA-256 as "AB:CD:...", 95 characters and a NUL, into
 * `out` (at least FECKLESS_HTTPS_FP_SIZE). False until an identity exists
 * -- a server has started once since boot -- and for a buffer too small.
 */
#define FECKLESS_HTTPS_FP_SIZE 96
bool feckless_https_fingerprint(char *out, size_t out_size);

/*
 * Launcher only, not in the ABI: the name the certificate is made for,
 * before the first server starts (it is "feckless-" and the last three
 * bytes of the chip's MAC in hex if never set). Made fit to be a DNS
 * label (feckless_dns_label()); a name that leaves nothing is ignored.
 * The name is kept with the certificate: a name given after the identity
 * exists changes nothing until NVS is erased.
 */
void feckless_https_set_name(const char *name);

#ifdef __cplusplus
}
#endif
