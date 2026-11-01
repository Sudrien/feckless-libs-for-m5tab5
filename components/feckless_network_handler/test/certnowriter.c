/* SPDX-License-Identifier: MIT
 *
 * feckless_cert.c built without the X.509 writer is two stubs that say so
 * and leave the buffers empty -- and need no Mbed TLS to link. */
#include <stdio.h>
#include <string.h>
#include "feckless_cert.h"
int main(void)
{
    char k[8] = "x", c[8] = "x";
    unsigned char fp[32];
    int fails = 0;
    if (feckless_cert_make("n", NULL, NULL, k, sizeof k, c, sizeof c) != FECKLESS_CERT_ERR_NO_WRITER) fails++;
    if (k[0] || c[0]) fails++;
    if (feckless_cert_fingerprint("x", fp) != FECKLESS_CERT_ERR_NO_WRITER) fails++;
    printf("certnowriter: %d failed\n", fails);
    return fails;
}
