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

/* u-feckless-0065: the hardware JPEG decoder, the ABI's image section
 * (box_image.c); an empty table where the chip has no engine. */
extern const feckless_umode_call_t box_image_calls[];
extern const uint32_t box_image_calls_n;
void box_image_end(void);

/* u-feckless-0053: NVS, in the namespaces the launcher named (box_nvs.c). */
extern const feckless_umode_call_t box_nvs_calls[];
extern const uint32_t box_nvs_calls_n;
void box_nvs_end(void);

/* u-feckless-0059: feckless_camera's frames (box_camera.c); an empty table
 * where the launcher does not link the camera. */
extern const feckless_umode_call_t box_camera_calls[];
extern const uint32_t box_camera_calls_n;
void box_camera_end(void);

/* u-feckless-0062: a UART, I2C devices and GPIO (box_drivers.c) */
extern const feckless_umode_call_t box_drivers_calls[];
extern const uint32_t box_drivers_calls_n;
void box_drivers_end(void);

/* u-feckless-0071: the routes' state and lwIP's sockets (box_net.c); an
 * empty table where the launcher does not link feckless_network_handler. */
extern const feckless_umode_call_t box_net_calls[];
extern const uint32_t box_net_calls_n;
void box_net_end(void);

/* u-feckless-0073: esp_http_client, the launcher's clients by number
 * (box_http.c); an empty table where box_net.c's is, or the launcher does
 * not link esp_http_client. box_http_reap(): the stop's watch. */
extern const feckless_umode_call_t box_http_calls[];
extern const uint32_t box_http_calls_n;
void box_http_end(void);
void box_http_reap(void *arg);

/* u-feckless-0075: callbacks into the app (box_upcall.c). box_upcall_start():
 * from a host call on the app's thread, under its lock -- spawns the app's
 * callback loop once (ESP_ERR_NOT_SUPPORTED: the app has none). box_upcall():
 * from a host task, holding no lock of the app's -- fn(args) run by the
 * loop, its return in *ret; waits as long as fn takes; ESP_ERR_INVALID_STATE
 * when the app stops (or asked from the loop's own thread),
 * ESP_ERR_NOT_SUPPORTED with no loop running. */
#include "esp_err.h"
extern const feckless_umode_call_t box_upcall_calls[];
extern const uint32_t box_upcall_calls_n;
esp_err_t box_upcall_start(win_t *w, uint32_t stack, int prio);
bool box_upcall_ready(void);
bool box_upcall_on_loop(void);
esp_err_t box_upcall(uint32_t fn, const uint32_t args[4], uint32_t *ret);
void box_upcall_end(void);

/* u-feckless-0076: esp_netif's handles by number (box_netif.c). */
extern const feckless_umode_call_t box_netif_calls[];
extern const uint32_t box_netif_calls_n;
void box_netif_end(void);

/* u-feckless-0077: esp_http_server and feckless_https (box_httpd.c).
 * box_httpd_lends(): fd is a session of one of the app's servers. */
extern const feckless_umode_call_t box_httpd_calls[];
extern const uint32_t box_httpd_calls_n;
bool box_httpd_lends(int fd);
void box_httpd_end(void);
void box_httpd_reap(void *arg);

/* u-feckless-0078: psa_crypto_init, psa_hash_compute (box_crypto.c). */
extern const feckless_umode_call_t box_crypto_calls[];
extern const uint32_t box_crypto_calls_n;

/* u-feckless-0081: feckless_fetch, the launcher's downloader (box_fetch.c). */
extern const feckless_umode_call_t box_fetch_calls[];
extern const uint32_t box_fetch_calls_n;
void box_fetch_end(void);

/* u-feckless-0083: the radio's control and the app's network hooks (box_wifi.c). */
extern const feckless_umode_call_t box_wifi_calls[];
extern const uint32_t box_wifi_calls_n;
void box_wifi_end(void);

/* u-feckless-0082: flash work on a task with an internal stack (box_flash.c),
 * for host tasks whose stacks are in PSRAM. box_flash_run(): fn(arg) there,
 * waited for; false only if the task could not be made. box_flash_call(): a
 * whole handler run there. */
typedef void (*box_flash_fn_t)(void *arg);
void box_flash_init(void);
bool box_flash_run(box_flash_fn_t fn, void *arg);
bool box_flash_call(feckless_umode_call_fn fn, win_t *w, const uint32_t a[7], uint32_t *ret);
/* A table entry whose handler runs on the flash task: FLASHU(name) is
 * FNU(name) through box_flash_call(). */
#define FLASH_WRAP(n) \
    static bool f_##n(win_t *w, const uint32_t a[7], uint32_t *ret) { return box_flash_call(c_##n, w, a, ret); }
#define FLASHU(n) { #n, f_##n, NULL, 0, FECKLESS_UMODE_CALL_UNLOCKED }
