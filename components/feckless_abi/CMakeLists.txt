# feckless-libs-for-m5tab5: feckless_abi
#
# The host's side: the feckless libraries, linked, and the table esp_elf
# resolves an app's ELF against, generated from exports.txt. The table
# names every symbol, which is also what keeps each one in the host's
# link: nothing in a launcher calls gfx_fill_star(), only an app might.
#
# The app's side is feckless_abi_elf(), in project_include.cmake: an app
# compiles against these headers and links none of it.
#
# SPDX-License-Identifier: MIT

set(_exports "${CMAKE_CURRENT_LIST_DIR}/exports.txt")
set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_syms.c")

idf_component_register(
    SRCS "src/feckless_abi.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_drivers feckless_graphics_handler
    PRIV_REQUIRES elf_loader esp_timer esp_app_format
                  # 6002: the drivers, partitions and MAC exports.txt names
                  esp_driver_gpio esp_driver_uart esp_driver_i2c
                  esp_partition app_update efuse
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # The configuration both sides must share (README.md, "Configuration").
    # An app compiles the launcher's tick rate into every pdMS_TO_TICKS(),
    # and esp_elf can only run it from PSRAM; these fail the build, on
    # either side, rather than the board.
    if(NOT CONFIG_FREERTOS_HZ EQUAL 1000)
        message(FATAL_ERROR "feckless_abi: CONFIG_FREERTOS_HZ must be 1000 (is ${CONFIG_FREERTOS_HZ})")
    endif()
    if(NOT CONFIG_SPIRAM OR NOT CONFIG_ELF_LOADER_LOAD_PSRAM)
        message(FATAL_ERROR "feckless_abi: needs CONFIG_SPIRAM=y and CONFIG_ELF_LOADER_LOAD_PSRAM=y")
    endif()
    if(CONFIG_ESP_SYSTEM_MEMPROT_FEATURE)
        message(FATAL_ERROR "feckless_abi: needs CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n")
    endif()
    # 9017: the C library is picolibc, IDF 6's default. An app calls the
    # launcher's libc through exports.txt, and a FILE, a ctype table or the
    # errno it reads are the library's layout: newlib's and picolibc's are
    # not the same, so the two sides must be the same library.
    if(NOT CONFIG_LIBC_PICOLIBC)
        message(FATAL_ERROR "feckless_abi: needs CONFIG_LIBC_PICOLIBC=y, IDF 6's default; "
                "the feckless ABI is picolibc's, not newlib's (CONFIG_LIBC_NEWLIB)")
    endif()
    # picolibc's errno is a thread-local variable unless the build names a
    # function for it, and a thread-local cannot be in an app's ELF (ld:
    # "R_RISCV_TPREL_HI20 against `errno' can not be used when making a
    # shared object"). IDF's own build adds this definition from
    # release/v6.1 on, not at the v6.1 tag, so it is added here, for every
    # component, ELF_COMPONENTS and the app's main included: `errno` then
    # calls __errno(), which the ABI exports. The same definition twice is
    # not a redefinition. IDF's libc keeps one variable behind both.
    idf_build_set_property(COMPILE_DEFINITIONS "__PICOLIBC_ERRNO_FUNCTION=__errno" APPEND)
    # Not fatal: an app runs without them, worse.
    if(NOT CONFIG_CACHE_L2_CACHE_256KB)
        message(WARNING "feckless_abi: CONFIG_CACHE_L2_CACHE_256KB=y is recommended: an app runs from PSRAM")
    endif()
    if(DEFINED CONFIG_UAC_FREQ_NUM_MAX AND CONFIG_UAC_FREQ_NUM_MAX LESS 8)
        message(WARNING "feckless_abi: CONFIG_UAC_FREQ_NUM_MAX=8 is recommended: "
                "with 4, a microphone that lists five rates loses its fifth")
    endif()

    # One tag for the repository (README.md at its root): the ABI is
    # these siblings at its own version. The app's manifest names each
    # one, so it can name them at different tags; fail that here rather
    # than as a launcher whose table and libraries disagree.
    function(_feckless_version _dir _out)
        file(STRINGS "${_dir}/idf_component.yml" _v REGEX "^version:")
        string(REGEX REPLACE "^version:[ \t]*\"?([^\" \t]*)\"?.*" "\\1" _v "${_v}")
        set(${_out} "${_v}" PARENT_SCOPE)
    endfunction()
    _feckless_version("${CMAKE_CURRENT_LIST_DIR}" _own)
    foreach(_sib feckless_drivers feckless_graphics_handler)
        idf_component_get_property(_sibdir ${_sib} COMPONENT_DIR)
        _feckless_version("${_sibdir}" _sibv)
        if(NOT _sibv STREQUAL _own)
            message(FATAL_ERROR "feckless_abi: ${_sib} is ${_sibv}, feckless_abi is ${_own}: "
                    "name every feckless component at the same tag")
        endif()
    endforeach()

    # The core table: exports.txt and feckless_abi_v1 (3007: the writer
    # is cmake/abitable.cmake, which each section's component uses too).
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/abitable.cmake")
    feckless_abi_write_table("${_table}" "${_exports}" g_feckless_abi_elfsyms
                             FECKLESS_ABI_NAME FECKLESS_ABI_SYMBOL)
endif()
