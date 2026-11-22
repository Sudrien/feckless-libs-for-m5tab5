#pragma once
/* u-feckless-0078: PSA's hash, as box_crypto.c calls it; fake_net.c's is a
 * stand-in digest (not SHA-256), enough to see the bytes go both ways. */
#include <stddef.h>
#include <stdint.h>
typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
#define PSA_SUCCESS ((psa_status_t)0)
#define PSA_ERROR_INVALID_ARGUMENT ((psa_status_t)-135)
#define PSA_ERROR_BUFFER_TOO_SMALL ((psa_status_t)-138)
#define PSA_ALG_SHA_256 ((psa_algorithm_t)0x02000009)
psa_status_t psa_crypto_init(void);
psa_status_t psa_hash_compute(psa_algorithm_t alg, const uint8_t *input, size_t input_length, uint8_t *hash,
                              size_t hash_size, size_t *hash_length);
