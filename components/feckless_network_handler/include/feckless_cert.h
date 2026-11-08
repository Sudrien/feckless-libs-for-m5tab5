/*
 * feckless_cert.h -- a self-signed TLS certificate for this one device,
 * made on the device (8004; Defeatist's certgen.h before it).
 *
 * Pure Mbed TLS: no NVS, no ESP-IDF, so it builds and runs on a host
 * against the same Mbed TLS the firmware links (test/certtest.c). Where
 * the result is kept, and who gets to see it, is feckless_https.h's.
 *
 * WHY ON THE DEVICE AND NOT AT BUILD TIME. A key compiled into the
 * firmware is in the firmware image, and the firmware is built from a
 * public repository: every device running a build would share a private
 * key anyone can read. A key generated here never leaves the device.
 *
 * WHAT IT IS AND IS NOT. No browser trusts it -- nothing a browser
 * already trusts signed it -- so the first visit warns once. What it
 * buys is that the connection is encrypted from then on (a Wi-Fi
 * password typed into a page cannot be read by someone listening), and
 * that the certificate can be recognised by its SHA-256 fingerprint.
 *
 * Mbed TLS 4.0 (IDF 6): the key and the hash come from PSA. It needs the X.509 writer: on IDF 6
 * CONFIG_MBEDTLS_X509_CREATE_C. Without it feckless_cert_make() returns
 * FECKLESS_CERT_ERR_NO_WRITER and builds nothing, so a project that only
 * includes this header is not asked to enable it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not an Mbed TLS error: the writer is not built into this Mbed TLS. */
#define FECKLESS_CERT_ERR_NO_WRITER  (-0x7F01)

/* The caller's random source: fill `buf` with `len` random bytes, 0 on
 * success. The firmware passes the hardware RNG. */
typedef int (*feckless_cert_rng_t)(void *ctx, unsigned char *buf, size_t len);

/*
 * An ECDSA P-256 key and a certificate for it, both PEM, NUL-terminated.
 *
 *   subject = issuer = CN=<name>, O=Feckless
 *   subjectAltName   = DNS:<lower-case name>.local
 *   valid            2026-01-01 .. 2049-12-31, so a clock that has not
 *                    been set yet, or one set wrong, never makes it
 *                    "not yet valid" or "expired"
 *   serial           16 random bytes, top bit clear
 *   basicConstraints CA:FALSE, keyUsage digitalSignature
 *
 * No IP address in it, deliberately: DHCP can move the address, and a
 * certificate that followed it would change its fingerprint -- the one
 * thing a person is asked to recognise. A browser's one-time exception
 * covers the name mismatch along with the self-signature.
 *
 * Returns 0, a negative Mbed TLS error, FECKLESS_CERT_ERR_NO_WRITER, or
 * -1 for a NULL or empty argument. On failure both buffers are empty.
 */
int feckless_cert_make(const char *name, feckless_cert_rng_t rng, void *rng_ctx,
                       char *key_pem, size_t key_cap,
                       char *crt_pem, size_t crt_cap);

/*
 * SHA-256 of the certificate's DER, which is what browsers call its
 * fingerprint. 0, a negative Mbed TLS error, or FECKLESS_CERT_ERR_NO_WRITER
 * (built without the writer: this file made nothing to hash).
 */
int feckless_cert_fingerprint(const char *crt_pem, unsigned char out[32]);

#ifdef __cplusplus
}
#endif
