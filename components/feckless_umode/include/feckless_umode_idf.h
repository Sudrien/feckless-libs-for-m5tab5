/* SPDX-License-Identifier: MIT
 *
 * feckless_umode_run() on FreeRTOS, and the one start-up step a window
 * needs from ESP-IDF.
 */
#pragma once

#include "feckless_umode_run.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Take every core out of the interrupt routing of the stack guard's source.
 *
 * ETS_ASSIST_DEBUG_INTR_SOURCE is one source in the interrupt matrix, and
 * both cores' monitors drive it, so a violation on one core's monitor is
 * delivered to the other as well -- where a window's CLIC threshold means
 * nothing, and ESP-IDF panics for a fault that is the program's. Call once at
 * start-up, before the first window. Nothing happens when
 * CONFIG_ESP_SYSTEM_HW_STACK_GUARD is off.
 *
 * THE PRICE: ESP-IDF no longer panics on a stack overflow in M-mode, on
 * either core. The monitors still run and latch; a window still reads its own
 * core's latch on the way out, so a program's overflow is still caught and
 * still its own. An overflow in M-mode goes unreported.
 */
void feckless_umode_guard_unroute(void);

/* feckless_umode_run() with feckless_umode_enter, vTaskDelay, taskYIELD,
 * xPortGetCoreID and fwrite to stdout. */
feckless_umode_end_t feckless_umode_run_default(feckless_umode_window_t *w);

#ifdef __cplusplus
}
#endif
