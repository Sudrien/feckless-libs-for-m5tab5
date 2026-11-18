/* SPDX-License-Identifier: MIT
 *
 * feckless_umode_run() on FreeRTOS. See feckless_umode_idf.h.
 */
#include "feckless_umode_idf.h"
#include "feckless_umode_load.h"     /* 0068: feckless_umode_app_where() */

#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
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

/*
 * u-feckless-0067: windows that ran long, in the log.
 *
 * A window holds its core from the app's other threads until its next
 * host call, so one that runs long is a UI that does not answer, touches
 * missed, an audio writer that comes back late. Which loop it was is the
 * question, and the ecall that ended it says.
 *
 * u-feckless-0068: that ecall is nearly always in a call stub, which says
 * only which call came next; the stub's ra says from where. Both go in the
 * line -- the call by name, the caller as its link address in the app's
 * ELF, for addr2line -- so it reads
 *
 *   ... the longest 32 ms on core 1, ending at a call to gfx_text_w
 *   from 0x0001a2b4 in the app
 *
 * Reported at most every LONG_REPORT_US, the longest since the last
 * report and how many there were, so a loop that runs long on every pass
 * is one line a second rather than a flood.
 */
#ifndef FECKLESS_UMODE_LONG_WINDOW_US
#define FECKLESS_UMODE_LONG_WINDOW_US   (30 * 1000)
#endif
#define LONG_REPORT_US                  (1000 * 1000)

static portMUX_TYPE s_long_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t  s_long_reported;
static uint32_t s_long_n, s_long_us, s_long_pc, s_long_ra;
static int      s_long_core;

static int64_t op_now_us(void) { return esp_timer_get_time(); }

static void long_report(uint32_t n, uint32_t worst, int core, uint32_t pc, uint32_t ra)
{
    char where[96];
    const char *call = NULL;
    uint32_t link = 0;
    switch (feckless_umode_app_where(pc, &link, &call)) {
    case FECKLESS_UMODE_WHERE_STUB: {
        uint32_t from = 0;
        if (feckless_umode_app_where(ra, &from, NULL) == FECKLESS_UMODE_WHERE_APP) {
            snprintf(where, sizeof where, "a call to %s from 0x%08" PRIx32 " in the app",
                     call ? call : "?", from);
        } else {
            snprintf(where, sizeof where, "a call to %s", call ? call : "?");
        }
        break;
    }
    case FECKLESS_UMODE_WHERE_APP:
        snprintf(where, sizeof where, "an ecall at 0x%08" PRIx32 " in the app", link);
        break;
    default:
        snprintf(where, sizeof where, "an ecall at pc 0x%08" PRIx32, pc);
        break;
    }
    ESP_LOGW("feckless_umode", "%" PRIu32 " window%s of %u ms or more without a host call; "
             "the longest %" PRIu32 " ms on core %d, ending at %s",
             n, n == 1 ? "" : "s", (unsigned)(FECKLESS_UMODE_LONG_WINDOW_US / 1000),
             worst / 1000, core, where);
}

static void op_long_window(const feckless_umode_window_t *w, uint32_t us, uint32_t pc)
{
    const int64_t now = esp_timer_get_time();
    uint32_t n = 0, worst = 0, at = 0, ra = 0;
    int core = 0;
    taskENTER_CRITICAL(&s_long_mux);
    s_long_n++;
    if (us > s_long_us) {
        s_long_us = us;
        s_long_pc = pc;
        s_long_ra = w->ctx.x[1];
        s_long_core = w->core;
    }
    if (now - s_long_reported >= LONG_REPORT_US) {
        n = s_long_n; worst = s_long_us; at = s_long_pc; ra = s_long_ra; core = s_long_core;
        s_long_n = s_long_us = 0;
        s_long_reported = now;
    }
    taskEXIT_CRITICAL(&s_long_mux);
    if (n) long_report(n, worst, core, at, ra);
}

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
        .now_us = op_now_us,
        .long_window = op_long_window,
        .long_us = FECKLESS_UMODE_LONG_WINDOW_US,
    };
    return feckless_umode_run(w, &ops);
}
