/* SPDX-License-Identifier: MIT
 *
 * The feckless ABI's storage section (3008): the storage handler, FatFs
 * and libc's file I/O, for an app's ELF, from a launcher that links this
 * component. sections/storage.txt in feckless_abi is the list.
 */
#pragma once

#include <stdint.h>
#include "feckless_abi.h"

#define FECKLESS_ABI_STORAGE_VERSION 1

/* Defined by a launcher that has the section; every app that takes a
 * name from it refers to it. A launcher without the section has no such
 * symbol, and esp_elf_relocate() refuses the app naming it. */
#define FECKLESS_ABI_STORAGE_SYMBOL feckless_abi_storage_v1
extern const uint32_t FECKLESS_ABI_STORAGE_SYMBOL;

/* An app that uses storage calls this first thing in app_main(), beside
 * feckless_abi_require(). It does nothing at run time; it makes the ELF
 * refer to the section's symbol, which feckless_abi_elf() checks. */
static inline void feckless_abi_require_storage(void)
{
    __asm__ volatile("" :: "r"(&FECKLESS_ABI_STORAGE_SYMBOL));
}
