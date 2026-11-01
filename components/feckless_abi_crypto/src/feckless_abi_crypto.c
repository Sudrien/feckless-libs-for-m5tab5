/* SPDX-License-Identifier: MIT
 *
 * See feckless_abi_crypto.h. The table, g_feckless_abi_crypto_elfsyms, is
 * generated from feckless_abi's sections/crypto.txt for this launcher's
 * Mbed TLS major; feckless_abi_run() finds it through a weak reference and
 * registers it beside the core table.
 */
#include "feckless_abi_crypto.h"

const uint32_t FECKLESS_ABI_CRYPTO_SYMBOL = FECKLESS_ABI_CRYPTO_VERSION;

/* Only this major's: the table lists only the one that is defined. */
#if MBEDTLS_VERSION_MAJOR >= 4
const uint32_t feckless_abi_crypto_mbedtls4 = 4;
#else
const uint32_t feckless_abi_crypto_mbedtls3 = 3;
#endif
