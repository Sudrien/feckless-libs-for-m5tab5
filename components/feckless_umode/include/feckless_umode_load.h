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
 * task's stack is the host's; the program's is in the box.
 *
 * u-feckless-0023: and the app's threads. When app_main returns, this waits
 * for every thread to end, as an IDF app_main may return and leave its tasks
 * running. When any of them -- app_main or a thread -- ends other than by
 * EXIT, the app is stopping: every other thread is ended at its next host
 * call (KILLED), and this returns that first end. A thread that makes no
 * host call for FECKLESS_UMODE_STOP_WAIT_MS cannot be stopped (its window
 * holds interrupts off); the app is then stuck, and unload refuses it. */
#define FECKLESS_UMODE_STOP_WAIT_MS 3000   /* > the longest a host call may wait */
feckless_umode_end_t feckless_umode_app_run(feckless_umode_app_t *app);

/* u-feckless-0022: the app loaded now, or NULL. One at a time. */
feckless_umode_app_t *feckless_umode_app_loaded(void);

/* u-feckless-0022: grant the app [lo, hi) as well as its box, on every core,
 * from now until it is unloaded: FECKLESS_UMODE_R, or _R | _W -- never _X,
 * so nothing outside the box runs. lo and hi on the PMP's granule (128 bytes
 * on the P4). For memory a host call hands the app to use in place, such as
 * the framebuffer (feckless_box_calls' gfx_fb()). The same grant again is
 * ESP_OK. ESP_ERR_INVALID_ARG: bad perm or range, off the granule, or
 * overlapping a grant already made. ESP_ERR_NO_MEM: no PMP entries left.
 * ESP_ERR_INVALID_STATE: not the loaded app. The window's range checks are
 * unchanged: a host call still takes pointers into the box only. */
esp_err_t feckless_umode_app_grant(feckless_umode_app_t *app, uint32_t lo, uint32_t hi, uint32_t perm);

/* u-feckless-0023: a thread for the app -- xTaskCreatePinnedToCore() as
 * the app sees it (feckless_box_calls). fn runs in a window of its own,
 * with arg in a0, ra on the exit stub (so returning ends the thread), a
 * stack of stack_bytes from the box's heap, its own errno block (tp), on a
 * host task of its own at prio (clamped to the RTOS's range) pinned to core
 * (0, 1, or tskNO_AFFINITY). *id (may be NULL) is 1 + its slot.
 * ESP_ERR_INVALID_ARG: fn not in the box, a stack outside 1 KB..1 MB, a
 * bad core. ESP_ERR_NO_MEM: no slot (FECKLESS_UMODE_MAX_THREADS), no room
 * in the box, or no host task. ESP_ERR_INVALID_STATE: not the loaded app,
 * or it is stopping. */
#define FECKLESS_UMODE_MAX_THREADS 8
esp_err_t feckless_umode_app_spawn(feckless_umode_app_t *app, uint32_t fn, uint32_t arg,
                                   uint32_t stack_bytes, int prio, int core, uint32_t *id);

/* u-feckless-0031: feckless_umode_app_run(), on a host task of its own
 * pinned to `core`, with the calling task waiting for it. A window holds
 * interrupts off on its core, so an app's long computations between host
 * calls delay every interrupt there -- the panel's DMA restart among them,
 * which IDF's DPI driver needs every frame, or the panel shows the cyan of
 * an underrun. Run the app on the core the drivers' interrupts are NOT on:
 * the launcher brought the board up on core 0 (app_main's), so core 1. The
 * app's own driver inits, run there, are answered by the launcher's
 * already-initialised drivers. Falls back to running here if the task
 * cannot be made. */
#define FECKLESS_UMODE_APP_STACK 16384     /* the host task: the run loop and the host calls */
feckless_umode_end_t feckless_umode_app_run_on(feckless_umode_app_t *app, int core);

/* The window, for its counters and range. */
feckless_umode_window_t *feckless_umode_app_window(feckless_umode_app_t *app);

/* Revoke the grant, then free everything. The app's fini functions are not
 * run: esp_elf would run them in M-mode. u-feckless-0023: not if a thread of
 * the app is still running (see feckless_umode_app_run()): that is logged,
 * the app stays loaded, and only a reset recovers it. */
void feckless_umode_unload(feckless_umode_app_t *app);

#ifdef __cplusplus
}
#endif
