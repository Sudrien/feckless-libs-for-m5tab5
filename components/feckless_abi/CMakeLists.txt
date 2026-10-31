# feckless-abi-for-m5tab5
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
    # Not fatal: an app runs without them, worse.
    if(NOT CONFIG_CACHE_L2_CACHE_256KB)
        message(WARNING "feckless_abi: CONFIG_CACHE_L2_CACHE_256KB=y is recommended: an app runs from PSRAM")
    endif()
    if(DEFINED CONFIG_UAC_FREQ_NUM_MAX AND CONFIG_UAC_FREQ_NUM_MAX LESS 8)
        message(WARNING "feckless_abi: CONFIG_UAC_FREQ_NUM_MAX=8 is recommended: "
                "with 4, a microphone that lists five rates loses its fifth")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_exports}")
    file(STRINGS "${_exports}" _syms REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
    # As elf_loader's tool/symbols.py writes it: only the address
    # matters, and "int" for memcpy needs the warning off.
    set(_c "/* Generated from exports.txt by CMakeLists.txt. Do not edit. */\n")
    string(APPEND _c "#include <stddef.h>\n#include \"private/elf_symbol.h\"\n")
    string(APPEND _c "#include \"feckless_abi.h\"\n\n")
    string(APPEND _c "#pragma GCC diagnostic push\n")
    string(APPEND _c "#pragma GCC diagnostic ignored \"-Wbuiltin-declaration-mismatch\"\n")
    foreach(_s IN LISTS _syms)
        string(APPEND _c "extern int ${_s};\n")
    endforeach()
    string(APPEND _c "#pragma GCC diagnostic pop\n\n")
    string(APPEND _c "const struct esp_elfsym g_feckless_abi_elfsyms[] = {\n")
    # Not ESP_ELFSYM_EXPORT(FECKLESS_ABI_SYMBOL): it stringizes its
    # argument unexpanded, and registered the version as
    # "FECKLESS_ABI_SYMBOL", which no app asks for.
    string(APPEND _c "    { FECKLESS_ABI_NAME, (const void *)&FECKLESS_ABI_SYMBOL },\n")
    foreach(_s IN LISTS _syms)
        string(APPEND _c "    ESP_ELFSYM_EXPORT(${_s}),\n")
    endforeach()
    string(APPEND _c "    ESP_ELFSYM_END\n};\n")
    file(WRITE "${_table}.tmp" "${_c}")
    configure_file("${_table}.tmp" "${_table}" COPYONLY)
endif()
