/* SPDX-License-Identifier: MIT
 *
 * The feckless ABI's crypto section (8003): Mbed TLS's keys, X.509 writer
 * and hashing, for an app's ELF, from a launcher that links this
 * component. sections/crypto.txt in feckless_abi is the list.
 *
 * The names differ between Mbed TLS 3.6 (IDF 5.x) and 4.0 (IDF 6.x), and
 * the structures they take are laid out by the Mbed TLS that compiled
 * them, so an app is built for the launcher's major: the section has a
 * marker symbol for each, defined only by a launcher of that major, and
 * feckless_abi_require_crypto() refers to the one the app was compiled
 * against (MBEDTLS_VERSION_MAJOR). An app built for the other fails in
 * esp_elf_relocate(), naming it.
 */
#pragma once

#include <stdint.h>

#include "mbedtls/build_info.h"

#include "feckless_abi.h"

#define FECKLESS_ABI_CRYPTO_VERSION 1

/* Defined by a launcher that has the section; every app that takes a
 * name from it refers to it. A launcher without the section has no such
 * symbol, and esp_elf_relocate() refuses the app naming it. */
#define FECKLESS_ABI_CRYPTO_SYMBOL feckless_abi_crypto_v1
extern const uint32_t FECKLESS_ABI_CRYPTO_SYMBOL;

#if MBEDTLS_VERSION_MAJOR >= 4
extern const uint32_t feckless_abi_crypto_mbedtls4;
#define FECKLESS_ABI_CRYPTO_MBEDTLS_SYMBOL feckless_abi_crypto_mbedtls4
#else
extern const uint32_t feckless_abi_crypto_mbedtls3;
#define FECKLESS_ABI_CRYPTO_MBEDTLS_SYMBOL feckless_abi_crypto_mbedtls3
#endif

/* An app that uses the section calls this first thing in app_main(),
 * beside feckless_abi_require(). It does nothing at run time; it makes
 * the ELF refer to the section's symbol and to its Mbed TLS major's,
 * which feckless_abi_elf() checks and a launcher of another major or
 * without the section refuses. */
static inline void feckless_abi_require_crypto(void)
{
    __asm__ volatile("" :: "r"(&FECKLESS_ABI_CRYPTO_SYMBOL),
                           "r"(&FECKLESS_ABI_CRYPTO_MBEDTLS_SYMBOL));
}
