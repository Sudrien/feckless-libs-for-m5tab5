/*
 * certtest.c -- feckless_cert.c on a host, against the real Mbed TLS
 * (3.6 or 4.0, whichever the Makefile was pointed at). Writes the key and
 * certificate it made to the directory in argv[1] and the fingerprint it
 * computed to <dir>/fp.txt, for certcheck.sh to put OpenSSL to. Exit
 * status: the number of checks that failed.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feckless_cert.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int urandom_rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    const size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}

static int failing_rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx; (void)buf; (void)len;
    return -0x1234;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: certtest <outdir>\n"); return 99; }
    char path[512];
    static char key[512], crt[1400];

    /* The ordinary case. Mixed case in the name: the SAN is lower case. */
    int ret = feckless_cert_make("Feckless-A1B2C3", urandom_rng, NULL,
                                 key, sizeof key, crt, sizeof crt);
    CHECK(ret == 0);
    CHECK(strstr(key, "-----BEGIN EC PRIVATE KEY-----") == key ||
          strstr(key, "-----BEGIN PRIVATE KEY-----") == key);
    CHECK(strstr(crt, "-----BEGIN CERTIFICATE-----") == crt);
    CHECK(strlen(key) < sizeof key && strlen(crt) < sizeof crt);

    unsigned char fp[32] = { 0 };
    CHECK(feckless_cert_fingerprint(crt, fp) == 0);
    unsigned char zero[32] = { 0 };
    CHECK(memcmp(fp, zero, 32) != 0);

    snprintf(path, sizeof path, "%s/key.pem", argv[1]);
    FILE *f = fopen(path, "w"); CHECK(f); if (f) { fputs(key, f); fclose(f); }
    snprintf(path, sizeof path, "%s/crt.pem", argv[1]);
    f = fopen(path, "w"); CHECK(f); if (f) { fputs(crt, f); fclose(f); }
    snprintf(path, sizeof path, "%s/fp.txt", argv[1]);
    f = fopen(path, "w");
    CHECK(f);
    if (f) {
        for (int i = 0; i < 32; i++) fprintf(f, "%02X%s", fp[i], i < 31 ? ":" : "\n");
        fclose(f);
    }

    /* Two certificates are two keys: no fixed seed. */
    static char key2[512], crt2[1400];
    CHECK(feckless_cert_make("Feckless-A1B2C3", urandom_rng, NULL,
                             key2, sizeof key2, crt2, sizeof crt2) == 0);
    CHECK(strcmp(key, key2) != 0 && strcmp(crt, crt2) != 0);

    /* A name longer than the buffers inside: cut, not overrun. */
    char longname[200];
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    CHECK(feckless_cert_make(longname, urandom_rng, NULL,
                             key2, sizeof key2, crt2, sizeof crt2) == 0);
    CHECK(strstr(crt2, "-----BEGIN CERTIFICATE-----") == crt2);

    /* Bad arguments. */
    CHECK(feckless_cert_make(NULL, urandom_rng, NULL, key, sizeof key, crt, sizeof crt) == -1);
    CHECK(feckless_cert_make("", urandom_rng, NULL, key, sizeof key, crt, sizeof crt) == -1);
    CHECK(feckless_cert_make("n", NULL, NULL, key, sizeof key, crt, sizeof crt) == -1);
    CHECK(feckless_cert_make("n", urandom_rng, NULL, NULL, 0, crt, sizeof crt) == -1);
    CHECK(feckless_cert_fingerprint(NULL, fp) == -1);
    CHECK(feckless_cert_fingerprint(crt, NULL) == -1);

    /* A buffer too small for the certificate fails and leaves both empty. */
    strcpy(key2, "x"); strcpy(crt2, "x");
    static char tiny[64];
    ret = feckless_cert_make("n", urandom_rng, NULL, key2, sizeof key2, tiny, sizeof tiny);
    CHECK(ret != 0);
    CHECK(key2[0] == '\0' && tiny[0] == '\0');
    ret = feckless_cert_make("n", urandom_rng, NULL, tiny, sizeof tiny, crt2, sizeof crt2);
    CHECK(ret != 0);
    CHECK(tiny[0] == '\0' && crt2[0] == '\0');

    /* A random source that fails: on 3.6 it is used for the key, on 4.0
     * for the serial, so either way this must not succeed, and must not
     * leave half a pair behind. */
    strcpy(key2, "x"); strcpy(crt2, "x");
    ret = feckless_cert_make("n", failing_rng, NULL, key2, sizeof key2, crt2, sizeof crt2);
    CHECK(ret != 0);
    CHECK(key2[0] == '\0' && crt2[0] == '\0');

    /* A fingerprint of something that is not a certificate. */
    CHECK(feckless_cert_fingerprint("not a certificate", fp) != 0);

    printf("certtest: %d checks, %d failed\n", checks, fails);
    return fails;
}
