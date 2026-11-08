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
    SRCS "src/umode.S" "src/umode_plan.c" "src/umode_run.c" "src/umode_box.c" "src/umode_calls.c"
         "src/umode_pmp.c" "src/umode_idf.c" "src/umode_load.c"
         "src/umode_wrap.c"
    INCLUDE_DIRS "include"
    # esp_system: esp_ipc.h, and esp_private/hw_stack_guard.h, which umode.S
    # includes. soc: the assist_debug and CLIC register definitions.
    # elf_loader (u-feckless-0004): the loader, named as feckless_abi names it.
    PRIV_REQUIRES esp_system esp_hw_support soc freertos log heap elf_loader
)

# u-feckless-0004: esp_elf's allocations go into the box while one is being
# loaded (umode_load.c). Every other allocation passes straight through, so
# the trusted path is as it was.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_elf_malloc" "-Wl,--wrap=esp_elf_free")

# u-feckless-0013: and ESP-IDF's application region protection is not run.
# Since IDF 939043ea83 (release/v6.1) the app always locks IDF's PMP layout,
# whatever CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE says, and that layout
# gives user mode internal SRAM and the peripherals (src/umode_wrap.c).
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_cpu_configure_region_protection")
