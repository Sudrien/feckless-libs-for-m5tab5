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
#include <string.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_ipc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "soc/assist_debug_reg.h"
#include "soc/clic_reg.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#include "feckless_umode_pmp.h"

static const char *TAG = "feckless_umode";

/* umode.S carries its own copy of the CLIC threshold, as it cannot include
 * soc/clic_reg.h. */
_Static_assert(FUMODE_CLIC_THRESH_REG == CLIC_INT_THRESH_REG,
               "FUMODE_CLIC_THRESH_REG no longer matches IDF's CLIC_INT_THRESH_REG");
_Static_assert(FUMODE_CLIC_THRESH_MASK == ((uint32_t)CLIC_CPU_INT_THRESH_V << CLIC_CPU_INT_THRESH_S),
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

#define NCORE SOC_CPU_CORES_NUM

typedef struct {
    uint32_t addr[16];
    uint32_t cfg[4];
    volatile uint32_t done;                 /* bit per core that ran it */
    volatile uint32_t bad;                  /* bit per core whose read-back differed */
    /* u-feckless-0011: what each core had, what it read back, and its PMP
     * granularity, for the log. */
    uint32_t before_addr[NCORE][16], before_cfg[NCORE][4];
    uint32_t after_addr[NCORE][16], after_cfg[NCORE][4];
    uint32_t gran_raw[NCORE];               /* pmpaddr15 after writing all ones, entry off; 0 if not probed */
} job_t;

static uint8_t cfg_byte(const uint32_t cfg[4], int i)
{
    return (uint8_t)(cfg[i / 4] >> ((i % 4) * 8));
}

static void read_all(uint32_t addr[16], uint32_t cfg[4])
{
    for (uint32_t i = 0; i < 16; i++) {
        addr[i] = feckless_umode_read_pmpaddr(i);
    }
    for (uint32_t w = 0; w < 4; w++) {
        cfg[w] = feckless_umode_read_pmpcfg(w);
    }
}

/* Per-hart CSRs, so the write and the read-back must be on one hart: with
 * interrupts masked this task cannot be moved to the other core between them. */
static void IRAM_ATTR apply_here(void *arg)
{
    job_t *j = arg;
    const unsigned prev = portSET_INTERRUPT_MASK_FROM_ISR();
    const uint32_t me = (uint32_t)esp_cpu_get_core_id();

    read_all(j->before_addr[me], j->before_cfg[me]);

    /* The granularity, as the privileged spec has it found: entry 15 off,
     * all ones into its address; bits below G read back as zero, and the
     * granule is 2^(G+2) bytes. Only if entry 15 is not locked. */
    j->gran_raw[me] = 0;
    if (!(cfg_byte(j->before_cfg[me], 15) & 0x80)) {
        uint32_t a[16], c[4];
        memcpy(a, j->before_addr[me], sizeof a);
        memcpy(c, j->before_cfg[me], sizeof c);
        a[15] = 0xffffffffu;
        c[3] &= 0x00ffffffu;
        feckless_umode_pmp_program(a, c);
        j->gran_raw[me] = feckless_umode_read_pmpaddr(15);
    }

    feckless_umode_pmp_program(j->addr, j->cfg);
    read_all(j->after_addr[me], j->after_cfg[me]);

    bool same = true;
    for (uint32_t i = 0; i < 16; i++) {
        same = same && j->after_addr[me][i] == j->addr[i];
    }
    for (uint32_t w = 0; w < 4; w++) {
        same = same && j->after_cfg[me][w] == j->cfg[w];
    }
    if (!same) {
        j->bad |= 1u << me;
    }
    j->done |= 1u << me;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(prev);
}

/* 2^(G+2) bytes, or 0 if not probed. */
__attribute__((unused)) static uint32_t granule(uint32_t raw)
{
    return raw ? 1u << (__builtin_ctz(raw) + 2) : 0;
}

static void log_core(const job_t *j, int c)
{
    ESP_LOGE(TAG, "core %d: PMP granularity %u bytes (pmpaddr15 read 0x%08x after all ones)%s", c,
             (unsigned)granule(j->gran_raw[c]), (unsigned)j->gran_raw[c],
             j->gran_raw[c] ? "" : " -- not probed, entry 15 locked");
    for (int i = 0; i < 16; i++) {
        const uint8_t b = cfg_byte(j->before_cfg[c], i);
        if (b & 0x80) {
            ESP_LOGE(TAG, "core %d: entry %2d was locked before: cfg 0x%02x addr 0x%08x (<<2: 0x%08x)", c, i,
                     b, (unsigned)j->before_addr[c][i], (unsigned)(j->before_addr[c][i] << 2));
        }
    }
    for (int i = 0; i < 16; i++) {
        const uint8_t want = cfg_byte(j->cfg, i), got = cfg_byte(j->after_cfg[c], i);
        if (want != got || j->addr[i] != j->after_addr[c][i]) {
            ESP_LOGE(TAG, "core %d: entry %2d wrote cfg 0x%02x addr 0x%08x, read cfg 0x%02x addr 0x%08x", c, i,
                     want, (unsigned)j->addr[i], got, (unsigned)j->after_addr[c][i]);
        }
    }
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
    for (int core = 0; core < NCORE; core++) {
        if (!(job.done & (1u << core))) {
            ESP_RETURN_ON_ERROR(esp_ipc_call_blocking((uint32_t)core, apply_here, &job), TAG,
                                "could not reach core %d", core);
        }
    }
    const uint32_t all = (1u << NCORE) - 1;
    ESP_RETURN_ON_FALSE(job.done == all, ESP_ERR_INVALID_STATE, TAG, "not every core was programmed");
    for (int c = 0; c < NCORE; c++) {
        ESP_LOGI(TAG, "core %d: PMP granularity %u bytes", c, (unsigned)granule(job.gran_raw[c]));
    }
    if (job.bad) {
#if CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE
        ESP_LOGE(TAG, "built with CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=y");
#else
        ESP_LOGE(TAG, "built with CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE off");
#endif
        for (int c = 0; c < NCORE; c++) {
            if (job.bad & (1u << c)) {
                log_core(&job, c);
            }
        }
        /* u-feckless-0012: entries locked before this firmware programmed any
         * are the bootloader's. esp_cpu_configure_region_protection() is called
         * from bootloader_init_mem(), in the second-stage bootloader and in the
         * app alike, and a lock holds until reset: the app's own sdkconfig being
         * right is not enough if the bootloader on the flash was built otherwise. */
        int locked = 0;
        for (int i = 0; i < 16; i++) {
            locked += (cfg_byte(job.before_cfg[0], i) & 0x80) != 0;
        }
        if (locked) {
            ESP_LOGE(TAG, "%d PMP entries were locked before this firmware set any: the second-stage "
                          "bootloader on the flash was built with CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=y. "
                          "Rebuild it with the option off (check build/bootloader/config/sdkconfig.h) and "
                          "flash it too -- idf.py flash, or bootloader-flash; app-flash keeps the old one",
                     locked);
        } else {
            ESP_LOGE(TAG, "PMP read back differently on cores 0x%x with no entry locked: the "
                          "granularity is coarser than a grant's alignment", (unsigned)job.bad);
        }
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
