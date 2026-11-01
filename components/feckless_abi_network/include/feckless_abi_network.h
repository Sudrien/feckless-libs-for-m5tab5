/* SPDX-License-Identifier: MIT
 *
 * The feckless ABI's network section (6004): feckless_network_handler,
 * lwIP's sockets, esp_netif, the HTTP server and client, for an app's
 * ELF, from a launcher that links this component. sections/network.txt
 * in feckless_abi is the list.
 */
#pragma once

#include <stdint.h>
#include "feckless_abi.h"
#include "wifi.h"
#include "feckless_https.h"   /* 8004 */

#define FECKLESS_ABI_NETWORK_VERSION 1

/* Defined by a launcher that has the section; every app that takes a
 * name from it refers to it. A launcher without the section has no such
 * symbol, and esp_elf_relocate() refuses the app naming it. */
#define FECKLESS_ABI_NETWORK_SYMBOL feckless_abi_network_v1
extern const uint32_t FECKLESS_ABI_NETWORK_SYMBOL;

/* An app that uses the network calls this first thing in app_main(),
 * beside feckless_abi_require(). It does nothing at run time; it makes
 * the ELF refer to the section's symbol, which feckless_abi_elf() checks. */
static inline void feckless_abi_require_network(void)
{
    __asm__ volatile("" :: "r"(&FECKLESS_ABI_NETWORK_SYMBOL));
}
