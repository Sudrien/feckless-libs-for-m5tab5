/* Stand-in for ESP-IDF's: the register names umode.S uses and the macros it
 * calls, as nops. It makes the assembler check the SYNTAX of the guard path
 * and the register arithmetic around it, not what IDF's macros do. */
#pragma once
#define ASSIST_DEBUG_CORE_0_INTR_ENA_REG  0x3ff0e000
#define ASSIST_DEBUG_CORE_1_INTR_ENA_REG  0x3ff0e080
#define ASSIST_DEBUG_CORE_0_INTR_RAW_REG  0x3ff0e004
#define ASSIST_DEBUG_CORE_0_INTR_CLR_REG  0x3ff0e008
#define ASSIST_DEBUG_CORE_0_SP_MIN_REG    0x3ff0e010
#define ASSIST_DEBUG_CORE_0_SP_MAX_REG    0x3ff0e014
#define ASSIST_DEBUG_CORE_0_SP_PC_REG     0x3ff0e018
#define ASSIST_DEBUG_SP_SPILL_BITS        0x3
  .macro ESP_HW_STACK_GUARD_MONITOR_STOP_CUR_CORE a, b
  nop
  .endm
  .macro ESP_HW_STACK_GUARD_MONITOR_START_CUR_CORE a, b
  nop
  .endm
  .macro ESP_HW_STACK_GUARD_SET_BOUNDS_CUR_CORE a
  nop
  .endm
