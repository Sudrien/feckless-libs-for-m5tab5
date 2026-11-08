/* SPDX-License-Identifier: MIT
 *
 * The feckless ABI's crypto section (8003; hashing only since 8007):
 * a SHA-256 for an app's ELF, from a launcher that links this component.
 * sections/crypto.txt in feckless_abi is the list.
 *
 * It is Mbed TLS 4.0 (IDF 6): hashes through PSA. The section has a
 * marker symbol for that major, defined only by a launcher of it, and
 * feckless_abi_require_crypto() refers to it, so an app built for another
 * fails in esp_elf_relocate(), naming it.
 */
#pragma once

#include <stdint.h>

#include "mbedtls/build_info.h"

#include "feckless_abi.h"

#define FECKLESS_ABI_CRYPTO_VERSION 2

/* Defined by a launcher that has the section; every app that takes a
 * name from it refers to it. A launcher without the section has no such
 * symbol, and esp_elf_relocate() refuses the app naming it. */
#define FECKLESS_ABI_CRYPTO_SYMBOL feckless_abi_crypto_v2
extern const uint32_t FECKLESS_ABI_CRYPTO_SYMBOL;

extern const uint32_t feckless_abi_crypto_mbedtls4;
#define FECKLESS_ABI_CRYPTO_MBEDTLS_SYMBOL feckless_abi_crypto_mbedtls4

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
