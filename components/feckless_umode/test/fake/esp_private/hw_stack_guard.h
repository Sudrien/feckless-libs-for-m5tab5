/*
 * Stand-in for ESP-IDF 6.1's esp_private/hw_stack_guard.h, for `make asm`
 * and `make compare` only. The macros below are IDF 6.1's own, copied from
 * components/esp_system/port/include/private/esp_private/hw_stack_guard.h
 * (SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD,
 * SPDX-License-Identifier: Apache-2.0), so a call with the wrong arguments
 * fails here as it does in an IDF build; u-feckless-0010 is why. The
 * register addresses are the ESP32-P4's, from IDF 6.1's
 * soc/esp32p4/register/hw_ver1/soc/assist_debug_reg.h, reg_base.h and
 * hal/esp32p4/include/hal/assist_debug_ll.h.
 */
#pragma once

#define DR_REG_ASSIST_DEBUG_BASE          0x3FF06000
#define ASSIST_DEBUG_CORE_0_INTR_ENA_REG  (DR_REG_ASSIST_DEBUG_BASE + 0x0)
#define ASSIST_DEBUG_CORE_0_INTR_RAW_REG  (DR_REG_ASSIST_DEBUG_BASE + 0x4)
#define ASSIST_DEBUG_CORE_0_INTR_CLR_REG  (DR_REG_ASSIST_DEBUG_BASE + 0xc)
#define ASSIST_DEBUG_CORE_0_SP_MIN_REG    (DR_REG_ASSIST_DEBUG_BASE + 0x38)
#define ASSIST_DEBUG_CORE_0_SP_MAX_REG    (DR_REG_ASSIST_DEBUG_BASE + 0x3c)
#define ASSIST_DEBUG_CORE_0_SP_PC_REG     (DR_REG_ASSIST_DEBUG_BASE + 0x40)
#define ASSIST_DEBUG_CORE_1_INTR_ENA_REG  (DR_REG_ASSIST_DEBUG_BASE + 0x80)
#define ASSIST_DEBUG_CORE_1_SP_MIN_REG    (DR_REG_ASSIST_DEBUG_BASE + 0xb8)
#define ASSIST_DEBUG_CORE_1_SP_MAX_REG    (DR_REG_ASSIST_DEBUG_BASE + 0xbc)
#define ASSIST_DEBUG_SP_SPILL_BITS        0x300     /* SP_SPILL_MIN_ENA | SP_SPILL_MAX_ENA: bits 8, 9 */
#define ASSIST_DEBUG_CORE_0_MONITOR_REG   ASSIST_DEBUG_CORE_0_INTR_ENA_REG
#define ASSIST_DEBUG_CORE_1_MONITOR_REG   ASSIST_DEBUG_CORE_1_INTR_ENA_REG

/* ---- from IDF 6.1, unchanged ---- */
#define ASSIST_DEBUG_CORE_0_MONITOR_REG_IMM  (ASSIST_DEBUG_CORE_0_MONITOR_REG >> 12)
#define ASSIST_DEBUG_CORE_0_SP_MIN_OFFSET    (ASSIST_DEBUG_CORE_0_SP_MIN_REG - DR_REG_ASSIST_DEBUG_BASE)
#define ASSIST_DEBUG_CORE_0_SP_MAX_OFFSET    (ASSIST_DEBUG_CORE_0_SP_MAX_REG - DR_REG_ASSIST_DEBUG_BASE)
#define ASSIST_DEBUG_CORE_0_SP_SPILL_OFFSET  (ASSIST_DEBUG_CORE_0_MONITOR_REG - DR_REG_ASSIST_DEBUG_BASE)

.macro ESP_HW_STACK_GUARD_SET_BOUNDS_CPU0 reg1 sp_min sp_max
lui     \reg1, ASSIST_DEBUG_CORE_0_MONITOR_REG_IMM
sw      \sp_min, ASSIST_DEBUG_CORE_0_SP_MIN_OFFSET(\reg1)
sw      \sp_max, ASSIST_DEBUG_CORE_0_SP_MAX_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_STOP_CPU0 reg1 reg2
lui     \reg1, ASSIST_DEBUG_CORE_0_MONITOR_REG_IMM
lw      \reg2, ASSIST_DEBUG_CORE_0_SP_SPILL_OFFSET(\reg1)
andi    \reg2, \reg2, ~ASSIST_DEBUG_SP_SPILL_BITS
sw      \reg2, ASSIST_DEBUG_CORE_0_SP_SPILL_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_START_CPU0 reg1 reg2
lui     \reg1, ASSIST_DEBUG_CORE_0_MONITOR_REG_IMM
lw      \reg2, ASSIST_DEBUG_CORE_0_SP_SPILL_OFFSET(\reg1)
ori     \reg2, \reg2, ASSIST_DEBUG_SP_SPILL_BITS
sw      \reg2, ASSIST_DEBUG_CORE_0_SP_SPILL_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_GET_BOUNDS_CPU0 reg1 sp_min sp_max
lui     \reg1, ASSIST_DEBUG_CORE_0_MONITOR_REG_IMM
lw      \sp_min, ASSIST_DEBUG_CORE_0_SP_MIN_OFFSET(\reg1)
lw      \sp_max, ASSIST_DEBUG_CORE_0_SP_MAX_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_QUERY_CPU0 reg1 reg2
lui     \reg1, ASSIST_DEBUG_CORE_0_MONITOR_REG_IMM
lw      \reg2, ASSIST_DEBUG_CORE_0_SP_SPILL_OFFSET(\reg1)
andi    \reg2, \reg2, ASSIST_DEBUG_SP_SPILL_BITS
.endm


#define ASSIST_DEBUG_CORE_1_MONITOR_REG_IMM  (ASSIST_DEBUG_CORE_1_MONITOR_REG >> 12)
#define ASSIST_DEBUG_CORE_1_SP_MIN_OFFSET    (ASSIST_DEBUG_CORE_1_SP_MIN_REG - DR_REG_ASSIST_DEBUG_BASE)
#define ASSIST_DEBUG_CORE_1_SP_MAX_OFFSET    (ASSIST_DEBUG_CORE_1_SP_MAX_REG - DR_REG_ASSIST_DEBUG_BASE)
#define ASSIST_DEBUG_CORE_1_SP_SPILL_OFFSET  (ASSIST_DEBUG_CORE_1_MONITOR_REG - DR_REG_ASSIST_DEBUG_BASE)

.macro ESP_HW_STACK_GUARD_SET_BOUNDS_CPU1 reg1 sp_min sp_max
lui     \reg1, ASSIST_DEBUG_CORE_1_MONITOR_REG_IMM
sw      \sp_min, ASSIST_DEBUG_CORE_1_SP_MIN_OFFSET(\reg1)
sw      \sp_max, ASSIST_DEBUG_CORE_1_SP_MAX_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_STOP_CPU1 reg1 reg2
lui     \reg1, ASSIST_DEBUG_CORE_1_MONITOR_REG_IMM
lw      \reg2, ASSIST_DEBUG_CORE_1_SP_SPILL_OFFSET(\reg1)
andi    \reg2, \reg2, ~ASSIST_DEBUG_SP_SPILL_BITS
sw      \reg2, ASSIST_DEBUG_CORE_1_SP_SPILL_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_START_CPU1 reg1 reg2
lui     \reg1, ASSIST_DEBUG_CORE_1_MONITOR_REG_IMM
lw      \reg2, ASSIST_DEBUG_CORE_1_SP_SPILL_OFFSET(\reg1)
ori     \reg2, \reg2, ASSIST_DEBUG_SP_SPILL_BITS
sw      \reg2, ASSIST_DEBUG_CORE_1_SP_SPILL_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_GET_BOUNDS_CPU1 reg1 sp_min sp_max
lui     \reg1, ASSIST_DEBUG_CORE_1_MONITOR_REG_IMM
lw      \sp_min, ASSIST_DEBUG_CORE_1_SP_MIN_OFFSET(\reg1)
lw      \sp_max, ASSIST_DEBUG_CORE_1_SP_MAX_OFFSET(\reg1)
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_QUERY_CPU1 reg1 reg2
lui     \reg1, ASSIST_DEBUG_CORE_1_MONITOR_REG_IMM
lw      \reg2, ASSIST_DEBUG_CORE_1_SP_SPILL_OFFSET(\reg1)
andi    \reg2, \reg2, ASSIST_DEBUG_SP_SPILL_BITS
.endm

.macro ESP_HW_STACK_GUARD_SET_BOUNDS_CUR_CORE reg1 sp_min sp_max
/* Check the current core ID */
csrr \reg1, mhartid
beqz \reg1, 1f
/* Core 1 */
ESP_HW_STACK_GUARD_SET_BOUNDS_CPU1 \reg1 \sp_min \sp_max
j 2f
1:
/* Core 0 */
ESP_HW_STACK_GUARD_SET_BOUNDS_CPU0 \reg1 \sp_min \sp_max
2:
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_START_CUR_CORE reg1 reg2
/* Check the current core ID */
csrr \reg1, mhartid
beqz \reg1, 1f
/* Core 1 */
ESP_HW_STACK_GUARD_MONITOR_START_CPU1 \reg1 \reg2
j 2f
1:
/* Core 0 */
ESP_HW_STACK_GUARD_MONITOR_START_CPU0 \reg1 \reg2
2:
.endm

.macro ESP_HW_STACK_GUARD_MONITOR_STOP_CUR_CORE reg1 reg2
/* Check the current core ID */
csrr \reg1, mhartid
beqz \reg1, 1f
/* Core 1 */
ESP_HW_STACK_GUARD_MONITOR_STOP_CPU1 \reg1 \reg2
j 2f
1:
/* Core 0 */
ESP_HW_STACK_GUARD_MONITOR_STOP_CPU0 \reg1 \reg2
2:
.endm
