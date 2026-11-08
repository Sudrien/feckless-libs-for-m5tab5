/* SPDX-License-Identifier: MIT
 *
 * What a launcher answers a boxed app with (u-feckless-0006): the table
 * feckless_umode_load() takes, for apps built with feckless_abi_box_elf().
 *
 *   uint32_t n;
 *   const feckless_umode_call_t *calls = feckless_box_calls_v1(&n);
 *   feckless_umode_load(image, size, calls, n, box_size, stack, &app);
 *
 * Every pointer from the app is checked against the box before it is read
 * or written, and nothing of the host's -- a panel, a bus, a string it keeps --
 * is handed to the app as a pointer: the app gets a number for it, or a copy
 * in the box. An import not in the table fails the load by name.
 */
#pragma once

#include <stdint.h>

#include "feckless_umode_calls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The table: the box's heap (feckless_umode_heap_calls), the version
 * symbols, libgcc's value helpers, the console, time, the log line, and the
 * panel, gfx, touch and backlight; and storage -- the storage handler, FatFs,
 * stdio and directories (u-feckless-0007). Built once; the same array every
 * call. */
const feckless_umode_call_t *feckless_box_calls_v1(uint32_t *n);

/* The app has ended: close the files and directories it left open, give
 * back any storage I/O lease and volume hold it took (the launcher's own
 * reads would wait on them), and forget the handles it was given. Call it
 * after feckless_umode_unload(). */
void feckless_box_calls_end(void);

/* The longest string the table reads from an app (a gfx paragraph). */
#define FECKLESS_BOX_STR_MAX  65536u

#ifdef __cplusplus
}
#endif
