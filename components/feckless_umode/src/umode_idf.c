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
#if SOC_CPU_HAS_PIE
#include "riscv/csr_pie.h"
#endif

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

#define FECKLESS_STR_(x) #x
#define FECKLESS_STR(x) FECKLESS_STR_(x)

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

#if SOC_CPU_HAS_PIE
/* u-feckless-0034. esp.movx.r.sar reads the PIE unit's shift register into
 * an integer register: a PIE instruction, so with the unit off it takes the
 * RTOS's lazy-enable trap here, in M-mode in the host task -- which makes
 * this task the unit's owner and restores its PIE registers if it had any
 * -- and it changes no PIE register. ESP-IDF's own save routine uses it
 * (portasm.S, pie_save_regs), so it assembles wherever ESP-IDF does. */
static void op_pie_on(void)
{
    /* a1 and a0, here and below, because those are the registers ESP-IDF
     * gives these instructions: not every integer register is an operand
     * they take (zero is refused by the assembler). */
    register uint32_t sar __asm__("a1");
    __asm__ volatile("esp.movx.r.sar a1" : "=r"(sar));
    (void)sar;
}

/* The unit's state register; its low two bits are zero when it is off, as
 * portasm.S's pie_disable reads it. */
static bool op_pie_off(void)
{
    uint32_t st;
    __asm__ volatile("csrr %0, " FECKLESS_STR(CSR_PIE_STATE_REG) : "=r"(st));
    return (st & 3u) == 0;
}

/* Every PIE register to zero: q0-q7, the two accumulators, the unaligned
 * load state and the three small ones. A task's first use of the unit
 * restores nothing, so without this the program would read what the last
 * owner computed -- the launcher's own vector code, or another app's. */
static void op_pie_clear(void)
{
    static const uint8_t zero[16] __attribute__((aligned(16))) = { 0 };
    register const uint8_t *z __asm__("a0") = zero;
    register uint32_t nil __asm__("a1") = 0;
    __asm__ volatile(
        "esp.zero.q q0\n esp.zero.q q1\n esp.zero.q q2\n esp.zero.q q3\n"
        "esp.zero.q q4\n esp.zero.q q5\n esp.zero.q q6\n esp.zero.q q7\n"
        "esp.zero.qacc\n esp.zero.xacc\n"
        "esp.ld.ua.state.ip a0, 0\n"
        "esp.movx.w.sar a1\n esp.movx.w.sar.bytes a1\n esp.movx.w.fft.bit.width a1\n"
        : "+r"(z) : "r"(nil), "m"(zero));
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
#if SOC_CPU_HAS_PIE
        .pie_on = op_pie_on,
        .pie_off = op_pie_off,
        .pie_clear = op_pie_clear,
#endif
    };
    return feckless_umode_run(w, &ops);
}
