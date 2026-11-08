/* SPDX-License-Identifier: MIT
 *
 * Taking the PMP over, on every core. The plan is umode_plan.c's; this file
 * writes it with feckless_umode_pmp_program() and reads it back.
 *
 * Needs the PMP in its reset state: CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n
 * keeps ESP-IDF from locking entries, and a locked entry ignores both its
 * configuration and its address. The read-back is how a locked or coarser-
 * than-4-byte entry shows up here, as an error, instead of as a grant that
 * is not the one asked for.
 */
#include "feckless_umode.h"

#include <stdbool.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_ipc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "soc/assist_debug_reg.h"
#include "soc/clic_reg.h"
#include "soc/soc_caps.h"

#include "feckless_umode_pmp.h"

static const char *TAG = "feckless_umode";

/* umode.S carries its own copy of the CLIC threshold, as it cannot include
 * soc/clic_reg.h. */
_Static_assert(FUMODE_CLIC_THRESH_REG == CLIC_INT_THRESH_REG,
               "FUMODE_CLIC_THRESH_REG no longer matches IDF's CLIC_INT_THRESH_REG");
_Static_assert(FUMODE_CLIC_THRESH_MASK == (CLIC_CPU_INT_THRESH_V << CLIC_CPU_INT_THRESH_S),
               "FUMODE_CLIC_THRESH_MASK is not a full CLIC threshold mask");

/* umode.S reaches the current core's assist_debug block by adding
 * mhartid << 7 to the CORE_0 address, so the blocks must be 0x80 apart for
 * each of the five registers it reads. */
#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
#define STRIDE (ASSIST_DEBUG_CORE_1_INTR_ENA_REG - ASSIST_DEBUG_CORE_0_INTR_ENA_REG)
_Static_assert(STRIDE == 0x80, "FUMODE_CORE_OFF in umode.S shifts mhartid by 7");
#define STRIDE_OK(reg) (ASSIST_DEBUG_CORE_1_##reg - ASSIST_DEBUG_CORE_0_##reg == STRIDE)
_Static_assert(STRIDE_OK(INTR_RAW_REG), "assist_debug INTR_RAW is not one stride apart per core");
_Static_assert(STRIDE_OK(INTR_CLR_REG), "assist_debug INTR_CLR is not one stride apart per core");
_Static_assert(STRIDE_OK(SP_MIN_REG), "assist_debug SP_MIN is not one stride apart per core");
_Static_assert(STRIDE_OK(SP_MAX_REG), "assist_debug SP_MAX is not one stride apart per core");
_Static_assert(STRIDE_OK(SP_PC_REG), "assist_debug SP_PC is not one stride apart per core");
_Static_assert(SOC_CPU_CORES_NUM == 2, "the assist_debug stride arithmetic assumes two cores");
#endif

typedef struct {
    uint32_t addr[16];
    uint32_t cfg[4];
    volatile uint32_t done;                 /* bit per core that ran it */
    volatile uint32_t bad;                  /* bit per core whose read-back differed */
} job_t;

/* Per-hart CSRs, so the write and the read-back must be on one hart: with
 * interrupts masked this task cannot be moved to the other core between them. */
static void IRAM_ATTR apply_here(void *arg)
{
    job_t *j = arg;
    const unsigned prev = portSET_INTERRUPT_MASK_FROM_ISR();
    const uint32_t me = (uint32_t)esp_cpu_get_core_id();

    feckless_umode_pmp_program(j->addr, j->cfg);

    bool same = true;
    for (uint32_t i = 0; i < 16; i++) {
        same = same && feckless_umode_read_pmpaddr(i) == j->addr[i];
    }
    for (uint32_t w = 0; w < 4; w++) {
        same = same && feckless_umode_read_pmpcfg(w) == j->cfg[w];
    }
    if (!same) {
        j->bad |= 1u << me;
    }
    j->done |= 1u << me;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(prev);
}

esp_err_t feckless_umode_pmp_apply(const feckless_umode_grant_t *grants, int n)
{
    static job_t job;
    ESP_RETURN_ON_FALSE(feckless_umode_pmp_plan(grants, n, job.addr, job.cfg) == FECKLESS_UMODE_OK,
                        ESP_ERR_INVALID_ARG, TAG, "the grants are not a valid PMP plan");
    job.done = 0;
    job.bad = 0;

    apply_here(&job);
    /* Every core not done yet. This is also the case where the task was
     * moved before apply_here ran, so the current core is not assumed. */
    for (int core = 0; core < SOC_CPU_CORES_NUM; core++) {
        if (!(job.done & (1u << core))) {
            ESP_RETURN_ON_ERROR(esp_ipc_call_blocking((uint32_t)core, apply_here, &job), TAG,
                                "could not reach core %d", core);
        }
    }
    const uint32_t all = (1u << SOC_CPU_CORES_NUM) - 1;
    ESP_RETURN_ON_FALSE(job.done == all, ESP_ERR_INVALID_STATE, TAG, "not every core was programmed");
    if (job.bad) {
        ESP_LOGE(TAG, "PMP read back differently on cores 0x%x: an entry is locked "
                      "(CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE must be off) or the "
                      "granularity is coarser than 4 bytes", (unsigned)job.bad);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
