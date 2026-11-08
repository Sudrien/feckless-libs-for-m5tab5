/* SPDX-License-Identifier: MIT
 *
 * For an app built to run in a box (feckless_abi_box_elf(), u-feckless-0005).
 *
 * Call feckless_abi_require_box() in app_main() beside feckless_abi_require().
 * It ties the ELF to the box: a launcher that would run it trusted, with no
 * box, has no feckless_abi_box_v1 and refuses it by that name before any of
 * it runs; a launcher that boxes apps looks for the name to know this one is
 * for the box.
 */
#pragma once

#include <stdint.h>

#define FECKLESS_ABI_BOX_VERSION 1
#define FECKLESS_ABI_BOX_SYMBOL  feckless_abi_box_v1

extern const uint32_t FECKLESS_ABI_BOX_SYMBOL;

static inline void feckless_abi_require_box(void)
{
    __asm__ volatile("" :: "r"(&FECKLESS_ABI_BOX_SYMBOL));
}
