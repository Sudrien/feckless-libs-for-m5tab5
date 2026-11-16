/* SPDX-License-Identifier: MIT
 * Shared by box_calls.c and box_storage.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feckless_box_calls.h"

typedef feckless_umode_window_t win_t;
#define H(name) static bool name(win_t *w, const uint32_t a[7], uint32_t *ret)
#define FN(n) { #n, c_##n, NULL, 0, 0 }
/* u-feckless-0023: shares no state between the app's threads, may block:
 * called without the app's lock (FECKLESS_UMODE_CALL_UNLOCKED). */
#define FNU(n) { #n, c_##n, NULL, 0, FECKLESS_UMODE_CALL_UNLOCKED }

static inline bool ok_ptr(const win_t *w, uint32_t p, uint32_t len)
{
    return feckless_umode_range_ok(w, (uintptr_t)p, len);
}

/* A string the app passed, or NULL: terminated within max and the box. */
static inline const char *str(const win_t *w, uint32_t p, uint32_t max)
{
    return feckless_umode_strlen(w, (uintptr_t)p, max) >= 0 ? (const char *)(uintptr_t)p : NULL;
}

/* u-feckless-0050: the app's errno, which feckless_box_libc's __errno()
 * finds at the calling thread's tp (u-feckless-0023, 0042): set where the
 * host call failed, so an app reads it as it would after its own libc's. A
 * tp outside the box is left alone. The two sides are one picolibc, so one
 * numbering. */
static inline void box_set_errno(win_t *w, int e)
{
    if (feckless_umode_range_ok(w, (uintptr_t)w->ctx.x[4], sizeof(int))) {
        *(int *)(uintptr_t)w->ctx.x[4] = e;
    }
}

static inline feckless_box_t *box_of_win(win_t *w)
{
    return feckless_umode_calls_of(w)->box;
}

/* What the app is given in place of the host's handles. */
#define BOX_TOKEN_PANEL  0x50414e31u    /* "PAN1" */
#define BOX_TOKEN_BUS    0x42555331u    /* "BUS1" */
#define BOX_TOKEN_EXP1   0x45585031u    /* "EXP1", u-feckless-0021 */
#define BOX_TOKEN_EXP2   0x45585032u    /* "EXP2" */

extern const feckless_umode_call_t box_storage_calls[];
extern const uint32_t box_storage_calls_n;
void box_storage_end(void);

/* u-feckless-0021: audio, USB audio and the USB host (box_audio.c). */
extern const feckless_umode_call_t box_audio_calls[];
extern const uint32_t box_audio_calls_n;
void box_audio_end(void);

/* u-feckless-0045: FreeRTOS's locks, queues, stream buffers, notifications,
 * critical sections and task queries (box_rtos.c). box_rtos_core(): the
 * core a thread asked for, tskNO_AFFINITY made the caller's. box_rtos_named():
 * a thread spawned, with the app's name for it (a box string, or 0). */
extern const feckless_umode_call_t box_rtos_calls[];
extern const uint32_t box_rtos_calls_n;
void box_rtos_end(void);
int box_rtos_core(int asked);
void box_rtos_named(win_t *w, uint32_t id, uint32_t name);

/* u-feckless-0052: esp_audio_codec's simple decoder (box_codec.c); an
 * empty table where the launcher does not link the codec. */
extern const feckless_umode_call_t box_codec_calls[];
extern const uint32_t box_codec_calls_n;
void box_codec_end(void);

/* u-feckless-0053: NVS, in the namespaces the launcher named (box_nvs.c). */
extern const feckless_umode_call_t box_nvs_calls[];
extern const uint32_t box_nvs_calls_n;
void box_nvs_end(void);

/* u-feckless-0059: feckless_camera's frames (box_camera.c); an empty table
 * where the launcher does not link the camera. */
extern const feckless_umode_call_t box_camera_calls[];
extern const uint32_t box_camera_calls_n;
void box_camera_end(void);
