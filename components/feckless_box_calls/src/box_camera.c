/* SPDX-License-Identifier: MIT
 *
 * The camera for a boxed app (u-feckless-0059): feckless_camera's six
 * calls, the ABI's camera section, as luma frames written into the box.
 *
 * Built only where the launcher links feckless_camera (as one that offers
 * the ABI's camera section does); elsewhere the table is empty, and an app
 * that imports these is refused at load by name, as before.
 * FECKLESS_BOX_NO_CAMERA leaves it out where the camera is linked, for a
 * launcher that keeps its camera to itself (and the host test, for the
 * other branch).
 *
 * - The bus is the host's: camera_init() takes the number tab5io_bus()
 *   gave the app, and the host's own bus goes to the driver.
 * - A frame is written by the host straight into the app's buffer, which
 *   is checked to lie in the box for the whole of the frame at the step
 *   asked -- the size is the host's, from the camera as it is started, not
 *   one the app gives. Not started, there is no size and nothing is
 *   written.
 * - feckless_camera is not thread-safe: one task owns the camera between
 *   start and stop. So one call at a time is inside it; a second, from
 *   another of the app's threads, is ESP_ERR_INVALID_STATE and touches
 *   nothing. Unlocked all the same: a grab waits for a frame, up to half a
 *   second, and a start for the sensor, and the app's other threads' host
 *   calls need not wait behind either.
 * - A camera the app left streaming is stopped when the app ends
 *   (box_camera_end()), so the next app, and the launcher, find it idle.
 * - u-feckless-0060: the driver is brought up, started and stopped on the
 *   board's core, core 0, whichever core the app's thread is on
 *   (on_board_core()). It has to be for a start: the CSI's DMA channel
 *   joins DW-GDMA's interrupt group, which the panel's DSI made on the
 *   core lcd_init() ran on, and ESP-IDF refuses to add to a group from
 *   another core (esp_intr_alloc_info(), ESP_ERR_INVALID_STATE). And it
 *   keeps the camera's own interrupts off the app's core, where a window
 *   holds them off (u-feckless-0031). A grab stays on the caller: it
 *   waits for a frame and copies it, and allocates nothing.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("camera.h") && !defined(FECKLESS_BOX_NO_CAMERA)

#include "camera.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tab5io.h"

/* u-feckless-0060: where a launcher brings the board up before it runs a
 * boxed app on the other core (feckless_umode_app_run_on()). */
#define BOARD_CORE   0
#define BOARD_STACK  (16 * 1024)

static const uint32_t s_camera_v1 = 1;      /* feckless_abi_camera_v1 */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy;                         /* a call is inside the camera */
static bool s_started;                      /* this app started it */

static bool enter(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool mine = !s_busy;
    if (mine) s_busy = true;
    portEXIT_CRITICAL(&s_mux);
    return mine;
}

static void leave(void)
{
    portENTER_CRITICAL(&s_mux);
    s_busy = false;
    portEXIT_CRITICAL(&s_mux);
}

/* u-feckless-0060: fn on the board's core -- here, if this is it; else on a
 * task of its own pinned there, at the caller's priority, while the caller
 * waits. ESP_ERR_NO_MEM if the task cannot be made: the camera is not
 * touched from the wrong core. */
typedef struct {
    esp_err_t (*fn)(void);
    esp_err_t e;
    SemaphoreHandle_t done;
} board_job_t;

static void board_task(void *arg)
{
    board_job_t *j = arg;
    j->e = j->fn();
    xSemaphoreGive(j->done);                /* j is the waiter's: not touched again */
    vTaskDelete(NULL);
}

static esp_err_t on_board_core(esp_err_t (*fn)(void))
{
    if (xPortGetCoreID() == BOARD_CORE) return fn();
    board_job_t j = { .fn = fn, .e = ESP_FAIL, .done = xSemaphoreCreateBinary() };
    if (!j.done) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(board_task, "box_camera", BOARD_STACK, &j, uxTaskPriorityGet(NULL), NULL,
                                BOARD_CORE) != pdPASS) {
        vSemaphoreDelete(j.done);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(j.done, portMAX_DELAY);
    vSemaphoreDelete(j.done);
    return j.e;
}

static esp_err_t init_here(void) { return camera_init(tab5io_bus()); }

/* (bus): the app's number for the bus, as tab5io_bus() gave it */
H(c_camera_init)
{
    (void)w;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (a[0] == BOX_TOKEN_BUS) {
        e = ESP_ERR_INVALID_STATE;
        if (enter()) {
            e = on_board_core(init_here);
            leave();
        }
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_camera_present) { (void)w; (void)a; *ret = camera_present(); return true; }

H(c_camera_start)
{
    (void)w; (void)a;
    esp_err_t e = ESP_ERR_INVALID_STATE;
    if (enter()) {
        e = on_board_core(camera_start);
        if (e == ESP_OK) s_started = true;
        leave();
    }
    *ret = (uint32_t)e;
    return true;
}

/* (step, int *w, int *h): either may be NULL */
H(c_camera_luma_size)
{
    *ret = 0;
    if ((a[1] && !ok_ptr(w, a[1], sizeof(int))) || (a[2] && !ok_ptr(w, a[2], sizeof(int)))) return true;
    int fw = 0, fh = 0;
    camera_luma_size((int)a[0], &fw, &fh);
    if (a[1]) memcpy((void *)(uintptr_t)a[1], &fw, sizeof fw);
    if (a[2]) memcpy((void *)(uintptr_t)a[2], &fh, sizeof fh);
    return true;
}

/* (uint8_t *dst, step, int *w, int *h): w and h may be NULL */
H(c_camera_grab_luma)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    const bool outs = (!a[2] || ok_ptr(w, a[2], sizeof(int))) && (!a[3] || ok_ptr(w, a[3], sizeof(int)));
    if (outs) {
        e = ESP_ERR_INVALID_STATE;
        if (enter()) {
            /* The frame's size at this step, as the grab will write it:
             * nothing changes it but a start or a stop, and none is
             * inside the camera while this call is. */
            int fw = 0, fh = 0;
            camera_luma_size((int)a[1], &fw, &fh);
            const uint64_t need = fw > 0 && fh > 0 ? (uint64_t)fw * (uint64_t)fh : 0;
            if (!need) {
                e = ESP_ERR_INVALID_STATE;          /* not started: no frame to size */
            } else if (need > UINT32_MAX || !ok_ptr(w, a[0], (uint32_t)need)) {
                e = ESP_ERR_INVALID_ARG;
            } else {
                int gw = 0, gh = 0;
                e = camera_grab_luma((uint8_t *)(uintptr_t)a[0], (int)a[1], &gw, &gh);
                if (e == ESP_OK) {
                    if (a[2]) memcpy((void *)(uintptr_t)a[2], &gw, sizeof gw);
                    if (a[3]) memcpy((void *)(uintptr_t)a[3], &gh, sizeof gh);
                }
            }
            leave();
        }
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_camera_stop)
{
    (void)w; (void)a;
    esp_err_t e = ESP_ERR_INVALID_STATE;
    if (enter()) {
        e = on_board_core(camera_stop);
        if (e != ESP_ERR_NO_MEM) s_started = false;     /* not stopped: box_camera_end() will */
        leave();
    }
    *ret = (uint32_t)e;
    return true;
}

const feckless_umode_call_t box_camera_calls[] = {
    { "feckless_abi_camera_v1", NULL, &s_camera_v1, sizeof s_camera_v1, 0 },
    FNU(camera_init), FNU(camera_present), FNU(camera_start), FNU(camera_luma_size),
    FNU(camera_grab_luma), FNU(camera_stop),
};
const uint32_t box_camera_calls_n = sizeof box_camera_calls / sizeof box_camera_calls[0];

/* After the app is unloaded: no thread is inside the camera. */
void box_camera_end(void)
{
    if (s_started) on_board_core(camera_stop);
    s_started = false;
    s_busy = false;
}

#else   /* no feckless_camera in this launcher */

const feckless_umode_call_t box_camera_calls[1] = { { NULL, NULL, NULL, 0, 0 } };
const uint32_t box_camera_calls_n = 0;
void box_camera_end(void) {}

#endif
