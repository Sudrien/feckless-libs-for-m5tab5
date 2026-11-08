/* SPDX-License-Identifier: MIT
 *
 * ESP-IDF's application region protection, not run (u-feckless-0013).
 *
 * esp_cpu_configure_region_protection() programs the PMP and locks its
 * entries; one of them gives user mode read and write over all of internal
 * SRAM (the host's stacks, its task control blocks, a window's saved
 * registers), others over the peripherals. A locked entry cannot be changed,
 * so with it run there is no box. Until ESP-IDF 939043ea83 (release/v6.1,
 * 2026-08-06) CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n kept the app from
 * running it; since, the app always does (bootloader_mem.c:
 * `#if !BOOTLOADER_BUILD || CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE`).
 *
 * So this component's link wraps it (CMakeLists.txt), and the app's call is
 * this: nothing, counted. The PMP stays in its reset state -- user mode gets
 * nothing, M-mode everything -- as the protected-mode sample, and IDF up to
 * the v6.1 tag, left it. Lost, as there: IDF's PMP and PMA entries that fault
 * M-mode accesses to unmapped gaps and to 0x0. The bootloader is a build of
 * its own, not linked with this, and must still be built with the option off.
 *
 * Called from call_start_cpu0 and call_start_cpu1, early: in IRAM, and the
 * count in .data with a non-zero start, so .bss being cleared after does not
 * hide it.
 */
#include <stdint.h>

#include "esp_attr.h"

#include "feckless_umode_pmp.h"

#define SKIP_BASE 0xfec10000u

DRAM_ATTR volatile uint32_t feckless_umode_idf_rp_calls = SKIP_BASE;

void IRAM_ATTR __wrap_esp_cpu_configure_region_protection(void)
{
    feckless_umode_idf_rp_calls++;
}

uint32_t feckless_umode_idf_region_protection_skipped(void)
{
    return feckless_umode_idf_rp_calls - SKIP_BASE;
}
