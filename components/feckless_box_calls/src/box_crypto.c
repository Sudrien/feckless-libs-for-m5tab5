/* SPDX-License-Identifier: MIT
 *
 * The crypto section for a boxed app (u-feckless-0078): psa_crypto_init
 * and psa_hash_compute, the launcher's PSA run on the app's bytes.
 *
 * Defeatist's remote keeps a SHA-256 of each browser it allowed and of the
 * token a browser presents (approve.c); without a hash no browser is ever
 * allowed, and the remote serves only its pairing page. A hash is a pure
 * function: the input is read in place in the box (at most IN_MAX), the
 * digest written into the app's buffer, its length through the app's
 * size_t. Any algorithm PSA hashes is answered; nothing else of PSA is.
 * And the section's two marks, feckless_abi_crypto_v2 and
 * feckless_abi_crypto_mbedtls4, as data, as box_net.c answers the
 * network's.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("psa/crypto.h") && !defined(FECKLESS_BOX_NO_CRYPTO)

#include "psa/crypto.h"

#define IN_MAX   (64 * 1024)
#define HASH_MAX 64                         /* SHA-512's */

static const uint32_t s_crypto_v2 = 2;      /* feckless_abi_crypto_v2 */
static const uint32_t s_mbedtls4 = 4;       /* feckless_abi_crypto_mbedtls4 */

H(c_psa_crypto_init) { (void)w; (void)a; *ret = (uint32_t)psa_crypto_init(); return true; }

/* (alg, input, input_length, hash, hash_size, size_t *hash_length) */
H(c_psa_hash_compute)
{
    *ret = (uint32_t)PSA_ERROR_INVALID_ARGUMENT;
    const uint32_t lenp = a[5];
    if (a[2] > IN_MAX || (a[2] && !ok_ptr(w, a[1], a[2])) || !ok_ptr(w, a[3], a[4]) ||
        !ok_ptr(w, lenp, sizeof(size_t)))
        return true;
    uint8_t out[HASH_MAX];
    size_t n = 0;
    (void)psa_crypto_init();                /* once is enough; again is harmless */
    const psa_status_t s = psa_hash_compute((psa_algorithm_t)a[0], (const uint8_t *)(uintptr_t)a[1], a[2], out,
                                            a[4] < sizeof out ? a[4] : sizeof out, &n);
    if (s == PSA_SUCCESS) memcpy((void *)(uintptr_t)a[3], out, n);
    *(size_t *)(uintptr_t)lenp = s == PSA_SUCCESS ? n : 0;
    *ret = (uint32_t)s;
    return true;
}

const feckless_umode_call_t box_crypto_calls[] = {
    { "feckless_abi_crypto_v2", NULL, &s_crypto_v2, sizeof s_crypto_v2, 0 },
    { "feckless_abi_crypto_mbedtls4", NULL, &s_mbedtls4, sizeof s_mbedtls4, 0 },
    FN(psa_crypto_init), FN(psa_hash_compute),
};
const uint32_t box_crypto_calls_n = sizeof box_crypto_calls / sizeof box_crypto_calls[0];

#else

const feckless_umode_call_t box_crypto_calls[1];
const uint32_t box_crypto_calls_n = 0;

#endif
