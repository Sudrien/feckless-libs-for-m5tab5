# SPDX-License-Identifier: MIT
# feckless_umode: RISC-V user mode on the ESP32-P4 under ESP-IDF's FreeRTOS,
# and the PMP grants that say what it may touch (u-feckless-0002).
#
# ESP-IDF locks the PMP entries it programs, and one of them gives U-mode
# read and write over all of internal DRAM; a locked entry cannot be changed.
# With this option off IDF never programs them and the PMP is left in its
# reset state, which denies U-mode everything.
if(CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE)
    message(FATAL_ERROR
        "feckless_umode: CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=y. ESP-IDF "
        "would lock a PMP entry that gives U-mode read/write over internal DRAM, "
        "and nothing can undo it. Set CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n "
        "(and rm sdkconfig).")
endif()

idf_component_register(
    SRCS "src/umode.S" "src/umode_plan.c" "src/umode_pmp.c"
    INCLUDE_DIRS "include"
    # esp_system: esp_ipc.h, and esp_private/hw_stack_guard.h, which umode.S
    # includes. soc: the assist_debug and CLIC register definitions.
    PRIV_REQUIRES esp_system esp_hw_support soc freertos log
)
