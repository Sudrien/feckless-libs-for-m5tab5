/* SPDX-License-Identifier: MIT
 *
 * Programming the grants into the PMP of every core. Separate from
 * feckless_umode.h because that one has no ESP-IDF in it.
 */
#pragma once

#include "esp_err.h"

#include "feckless_umode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Plan the grants (feckless_umode_pmp_plan()), write the plan to the PMP of
 * every core, and read it back. Call once at start-up, before any U-mode
 * context runs. ESP_ERR_INVALID_ARG: the grants are not a plan.
 * ESP_ERR_INVALID_STATE: a core read back something else (an entry is
 * locked, or the PMP is coarser than 4 bytes), or a core was not reached. */
esp_err_t feckless_umode_pmp_apply(const feckless_umode_grant_t *grants, int n);

/* How many times ESP-IDF's startup called esp_cpu_configure_region_protection()
 * and this component's wrap skipped it (u-feckless-0013): one per core when
 * the wrap is in the link, 0 if not. */
uint32_t feckless_umode_idf_region_protection_skipped(void);

#ifdef __cplusplus
}
#endif
