/* SPDX-License-Identifier: MIT
 *
 * An app's ELF, loaded into a box and run in U-mode.
 *
 *   feckless_umode_app_t *app;
 *   err = feckless_umode_load(image, size, calls, n, 4 << 20, 64 << 10, &app);
 *   end = feckless_umode_app_run(app);         // constructors, then the entry
 *   feckless_umode_unload(app);
 *
 * Needs feckless_umode_guard_unroute() once beforehand, as any window does.
 * One app loaded at a time.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "feckless_umode_calls.h"
#include "feckless_umode_run.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct feckless_umode_app feckless_umode_app_t;

/* A box of box_size bytes in PSRAM, stack bytes of it the stack; the stubs
 * for calls[] written into it; the ELF relocated into it, every import
 * resolved to a stub or a data copy; the PMP granted the box (read, write,
 * execute) on every core and nothing else. Nothing of the app has run.
 * ESP_ERR_NO_MEM: the box, or the ELF does not fit in it. ESP_ERR_NOT_FOUND:
 * an import is not in calls[] (esp_elf has logged its name). ESP_ERR_INVALID_STATE:
 * another app is loaded, or the PMP would not take the grant. */
esp_err_t feckless_umode_load(const uint8_t *image, size_t size,
                              const feckless_umode_call_t *calls, uint32_t n,
                              uint32_t box_size, uint32_t stack,
                              feckless_umode_app_t **out);

/* Run each preinit and init function, then the entry (app_main), each in its
 * own window on the calling task, with ra on the exit stub so that returning
 * is SYS_EXIT 0. Stops at the first that does not end with EXIT. The calling
 * task's stack is the host's; the program's is in the box. */
feckless_umode_end_t feckless_umode_app_run(feckless_umode_app_t *app);

/* The window, for its counters and range. */
feckless_umode_window_t *feckless_umode_app_window(feckless_umode_app_t *app);

/* Revoke the grant, then free everything. The app's fini functions are not
 * run: esp_elf would run them in M-mode. */
void feckless_umode_unload(feckless_umode_app_t *app);

#ifdef __cplusplus
}
#endif
