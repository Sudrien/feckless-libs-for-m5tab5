/* SPDX-License-Identifier: MIT
 *
 * feckless_umode_run() on FreeRTOS. See feckless_umode_idf.h.
 */
#include "feckless_umode_idf.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_rom_sys.h"
#include "sdkconfig.h"
#include "soc/interrupts.h"
#include "soc/soc.h"         /* ETS_INVALID_INUM, in IDF 6.1 */
#include "soc/soc_caps.h"

void feckless_umode_guard_unroute(void)
{
#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
    /* The interrupt matrix is global, so one core can pull the source from
     * another's routing. */
    for (int core = 0; core < SOC_CPU_CORES_NUM; core++) {
        esp_rom_route_intr_matrix(core, ETS_ASSIST_DEBUG_INTR_SOURCE, ETS_INVALID_INUM);
    }
#endif
}

static void op_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void op_yield(void) { taskYIELD(); }
static uint32_t op_core_id(void) { return (uint32_t)xPortGetCoreID(); }
static void op_write(const char *buf, uint32_t len) { fwrite(buf, 1, len, stdout); }

#if SOC_CPU_HAS_FPU
/* u-feckless-0028: reading fcsr is a float-state access, so with the FPU off
 * it takes the RTOS's lazy-enable trap here, in M-mode in the host task --
 * and it changes no float register, which may hold the program's values. */
static void op_fpu_on(void)
{
    uint32_t fcsr;
    __asm__ volatile("csrr %0, fcsr" : "=r"(fcsr));
    (void)fcsr;
}
static bool op_fpu_off(void)
{
    uint32_t ms;
    __asm__ volatile("csrr %0, mstatus" : "=r"(ms));
    return ((ms >> 13) & 3u) == 0;              /* mstatus.FS == Off */
}
#endif

feckless_umode_end_t feckless_umode_run_default(feckless_umode_window_t *w)
{
    static const feckless_umode_ops_t ops = {
        .enter = feckless_umode_enter,
        .delay_ms = op_delay_ms,
        .yield = op_yield,
        .core_id = op_core_id,
        .write = op_write,
#if SOC_CPU_HAS_FPU
        .fpu_on = op_fpu_on,
        .fpu_off = op_fpu_off,
#endif
    };
    return feckless_umode_run(w, &ops);
}
