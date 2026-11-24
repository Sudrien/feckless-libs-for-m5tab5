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

#include <stdbool.h>
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

/* u-feckless-0053: the NVS namespaces the next app may use, at most four
 * of at most 15 characters, copied; feckless_box_calls_end() forgets them.
 * Call it before feckless_umode_load(). Named none (the default), the app's
 * gets find nothing and its sets and erases are ESP_ERR_NOT_ALLOWED, so
 * NVS -- the launcher's Wi-Fi credentials, other apps' settings -- is out
 * of its reach. false, and nothing changed, for a bad list. */
bool feckless_box_calls_nvs(const char *const *ns, uint32_t n);

/* u-feckless-0055: a launcher's test of each path the app would write --
 * fopen and storage_io_open in a mode that writes, remove, unlink, rename
 * (each end), mkdir, rmdir, utime, storage_mark_hidden, and FatFs's f_open
 * with a write or create bit and f_chmod (their paths are FatFs's, "0:/").
 * Given the host's copy of the path, which is what the call then uses;
 * false refuses it with EACCES (FR_DENIED for FatFs) and nothing done.
 * Set before feckless_umode_load(); NULL, the default and what
 * feckless_box_calls_end() leaves, allows all. Reads are not filtered. */
typedef bool (*feckless_box_write_filter_t)(const char *path);
void feckless_box_calls_write_filter(feckless_box_write_filter_t f);

/*
 * u-feckless-0087: what the app wanted done once it ended. A boxed app
 * cannot power the board down or switch the boot partition, so it ends
 * (esp_restart(), answered by ending it), and before that it says which of
 * those it meant with feckless_box_end_wish(), an import in the box
 * section: Defeatist's power off and its Exit to Launcher end alike
 * otherwise, and the launcher had to guess. Taken once: the launcher reads
 * it after the app has ended; NONE if the app said nothing.
 */
typedef enum {
    FECKLESS_BOX_WISH_NONE = 0,
    FECKLESS_BOX_WISH_POWER_OFF,
    FECKLESS_BOX_WISH_LAUNCHER,             /* back to the launcher's own menu */
    FECKLESS_BOX_WISH_RESTART,              /* the app again */
} feckless_box_wish_t;
feckless_box_wish_t feckless_box_calls_wish(void);

/* The longest string the table reads from an app (a gfx paragraph). */
#define FECKLESS_BOX_STR_MAX  65536u

#ifdef __cplusplus
}
#endif
