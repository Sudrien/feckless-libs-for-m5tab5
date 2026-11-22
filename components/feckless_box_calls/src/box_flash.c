/* SPDX-License-Identifier: MIT
 *
 * Flash work on a task with an internal stack (u-feckless-0082).
 *
 * With CONFIG_FECKLESS_UMODE_HOST_STACK_PSRAM a boxed app's host tasks
 * have their stacks in PSRAM, and a task like that must not touch flash:
 * a flash operation turns the cache off, PSRAM with it, and ESP-IDF
 * asserts (esp_task_stack_is_sane_cache_disabled). NVS reads and writes
 * are flash operations, and so is the first feckless_https_start(), which
 * makes the device's certificate and keeps it in NVS. Those host calls
 * hand their work to this one task, created at the first need with an
 * internal stack, and wait for it: one piece of work at a time, in turn.
 * Without the Kconfig option the work runs where it is asked, as before.
 */
#include "box_calls_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_FECKLESS_UMODE_HOST_STACK_PSRAM

#include "esp_heap_caps.h"
#include "esp_log.h"

#define FLASH_STACK 8192                    /* nvs, and the certificate's keygen and X.509 writer */
#define FLASH_PRIO  5

typedef struct {
    box_flash_fn_t fn;
    void *arg;
} job_t;

static bool s_made;                         /* the task and its semaphores exist */
static SemaphoreHandle_t s_turn;            /* one caller at a time */
static SemaphoreHandle_t s_go, s_done;
static job_t s_job;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void flash_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_go, portMAX_DELAY);
        s_job.fn(s_job.arg);
        xSemaphoreGive(s_done);
    }
}

static bool make(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool have = s_made;
    portEXIT_CRITICAL(&s_mux);
    if (have) return true;
    SemaphoreHandle_t turn = xSemaphoreCreateBinary(), go = xSemaphoreCreateBinary(),
                      done = xSemaphoreCreateBinary();
    if (!turn || !go || !done) goto fail;
    xSemaphoreGive(turn);
    /* Published before the task exists: on the other core it runs at once
     * and takes s_go before xTaskCreatePinnedToCore() has returned. */
    s_turn = turn;
    s_go = go;
    s_done = done;
    TaskHandle_t t = NULL;
    /* internal RAM by default: xTaskCreatePinnedToCore()'s stack is never PSRAM */
    if (xTaskCreatePinnedToCore(flash_task, "box_flash", FLASH_STACK, NULL, FLASH_PRIO, &t, tskNO_AFFINITY) !=
        pdPASS)
        goto fail;
    (void)t;
    portENTER_CRITICAL(&s_mux);
    s_made = true;
    portEXIT_CRITICAL(&s_mux);
    return true;
fail:
    ESP_LOGE("box_flash", "no task for flash work (internal RAM?)");
    s_turn = s_go = s_done = NULL;
    if (turn) vSemaphoreDelete(turn);
    if (go) vSemaphoreDelete(go);
    if (done) vSemaphoreDelete(done);
    return false;
}

/* Made at a time no host call is in flight: from feckless_box_calls_v1(),
 * so the first NVS read does not race another to make it. */
void box_flash_init(void) { (void)make(); }

bool box_flash_run(box_flash_fn_t fn, void *arg)
{
    if (!make()) return false;
    xSemaphoreTake(s_turn, portMAX_DELAY);
    s_job = (job_t){ fn, arg };
    xSemaphoreGive(s_go);
    xSemaphoreTake(s_done, portMAX_DELAY);
    xSemaphoreGive(s_turn);
    return true;
}

#else

void box_flash_init(void) {}

bool box_flash_run(box_flash_fn_t fn, void *arg)
{
    fn(arg);
    return true;
}

#endif

/* A host call's handler run whole on the flash task. */
typedef struct {
    feckless_umode_call_fn fn;
    win_t *w;
    const uint32_t *a;
    uint32_t *ret;
    bool r;
} call_t;

static void run_call(void *arg)
{
    call_t *c = arg;
    c->r = c->fn(c->w, c->a, c->ret);
}

bool box_flash_call(feckless_umode_call_fn fn, win_t *w, const uint32_t a[7], uint32_t *ret)
{
    call_t c = { fn, w, a, ret, true };
    if (!box_flash_run(run_call, &c)) {
        *ret = (uint32_t)ESP_ERR_NO_MEM;
        return true;
    }
    return c.r;
}
