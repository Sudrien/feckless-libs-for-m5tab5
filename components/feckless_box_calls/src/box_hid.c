/* SPDX-License-Identifier: MIT
 *
 * A USB headset's or keyboard's media keys for a boxed app (u-feckless-0086):
 * hid_init() and hid_present().
 *
 * hid.c calls its callback on its polling task, which owns the URB and must
 * not block (hid.h). A box never runs app code there, and waiting for the
 * app's callback loop from there could stall the port. So the box's answer
 * to the app's hid_init(cb) is hid_init() with this file's own callback,
 * which only queues the button; a short task of the launcher's delivers
 * each in order through the callback loop (u-feckless-0075) to the app's
 * cb, by its box address. A full queue drops the press, as a person
 * pressing faster than an app can take them would want.
 *
 * Ordering is the trusted build's: hid_init() must come before the port
 * starts, and in a box the app starts it (box_audio.c's usbhost_start), so
 * an app that calls hid_init() before usbhost_start() gets its devices.
 * hid.c registers once and stays; at the app's end its cb is forgotten and
 * presses are dropped until the next app's hid_init().
 */
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid.h"

#include "box_calls_priv.h"

#define PRESSES     8
#define HID_STACK   2560
#define LOOP_STACK  (12 * 1024)                 /* box_httpd.c's, box_fetch.c's */

static uint32_t s_cb;                           /* the app's, by box address */
static uint32_t s_gen;
static bool s_registered;                       /* hid_init() done: once, for good */
static esp_err_t s_init_err;
static QueueHandle_t s_q;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct { uint32_t button, gen; } press_t;

/* hid.c's polling task: queue, never wait. */
static void on_button(hid_button_t button)
{
    if (!s_q) return;
    portENTER_CRITICAL(&s_mux);
    const press_t p = { (uint32_t)button, s_gen };
    const bool live = s_cb != 0;
    portEXIT_CRITICAL(&s_mux);
    if (live) (void)xQueueSend(s_q, &p, 0);
}

static void deliver(void *arg)
{
    (void)arg;
    press_t p;
    while (xQueueReceive(s_q, &p, portMAX_DELAY) == pdTRUE) {
        portENTER_CRITICAL(&s_mux);
        const uint32_t cb = p.gen == s_gen ? s_cb : 0;
        portEXIT_CRITICAL(&s_mux);
        if (!cb) continue;
        const uint32_t a[4] = { p.button, 0, 0, 0 };
        uint32_t r = 0;
        (void)box_upcall(cb, a, &r);            /* the app stopping: INVALID_STATE, nothing to say */
    }
    vTaskDelete(NULL);
}

/* (cb): locked -- the callback loop is spawned here if it is not yet. */
H(c_hid_init)
{
    if (!a[0] || !ok_ptr(w, a[0], 4)) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    if (box_upcall_start(w, LOOP_STACK, 5) != ESP_OK) {
        *ret = (uint32_t)ESP_ERR_NO_MEM;
        return true;
    }
    if (!s_q) {
        QueueHandle_t q = xQueueCreate(PRESSES, sizeof(press_t));
        if (!q) {
            *ret = (uint32_t)ESP_ERR_NO_MEM;
            return true;
        }
        s_q = q;
        if (xTaskCreatePinnedToCore(deliver, "box_hid", HID_STACK, NULL, 4, NULL, tskNO_AFFINITY) != pdPASS) {
            vQueueDelete(q);
            s_q = NULL;
            *ret = (uint32_t)ESP_ERR_NO_MEM;
            return true;
        }
    }
    if (!s_registered) {
        s_init_err = hid_init(on_button);
        s_registered = s_init_err == ESP_OK;
    }
    if (s_registered) {
        portENTER_CRITICAL(&s_mux);
        s_cb = a[0];
        portEXIT_CRITICAL(&s_mux);
    }
    *ret = (uint32_t)(s_registered ? ESP_OK : s_init_err);
    return true;
}

H(c_hid_present)
{
    (void)w; (void)a;
    *ret = hid_present() ? 1 : 0;
    return true;
}

const feckless_umode_call_t box_hid_calls[] = {
    FN(hid_init), FNU(hid_present),
};
const uint32_t box_hid_calls_n = sizeof box_hid_calls / sizeof box_hid_calls[0];

void box_hid_end(void)
{
    portENTER_CRITICAL(&s_mux);
    s_cb = 0;
    s_gen++;
    portEXIT_CRITICAL(&s_mux);
}
