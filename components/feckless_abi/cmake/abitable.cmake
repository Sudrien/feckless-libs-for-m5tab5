# SPDX-License-Identifier: MIT
#
# feckless_abi_write_table(<out.c> <exports file> <table> <version name> <version symbol>)
#
# Writes the C table esp_elf resolves an app's ELF against: every bare
# name in <exports file>, and <version symbol> registered as
# <version name> (a C string expression). Used by feckless_abi for the
# core table (exports.txt) and by each section's component for its own
# (sections/<name>.txt). Lines that are not a bare name are ignored, the
# "@symbol" line included.
#
# Rewritten only when its text changes, so an unchanged list does not
# rebuild the launcher.

function(feckless_abi_write_table _out _exports _table _vname _vsym)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_exports}")
    file(STRINGS "${_exports}" _syms REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
    # As elf_loader's tool/symbols.py writes it: only the address
    # matters, and "int" for memcpy needs the warning off.
    set(_c "/* Generated from ${_exports} by feckless_abi's cmake/abitable.cmake. Do not edit. */\n")
    string(APPEND _c "#include <stddef.h>\n#include <stdint.h>\n#include \"private/elf_symbol.h\"\n")
    string(APPEND _c "#include \"feckless_abi.h\"\n\n")
    string(APPEND _c "#pragma GCC diagnostic push\n")
    string(APPEND _c "#pragma GCC diagnostic ignored \"-Wbuiltin-declaration-mismatch\"\n")
    foreach(_s IN LISTS _syms)
        string(APPEND _c "extern int ${_s};\n")
    endforeach()
    string(APPEND _c "extern const uint32_t ${_vsym};\n")
    string(APPEND _c "#pragma GCC diagnostic pop\n\n")
    string(APPEND _c "const struct esp_elfsym ${_table}[] = {\n")
    # Not ESP_ELFSYM_EXPORT(<version symbol>): it stringizes its argument
    # unexpanded, and registered the core version as
    # "FECKLESS_ABI_SYMBOL", which no app asks for (feckless-abi 0025).
    string(APPEND _c "    { ${_vname}, (const void *)&${_vsym} },\n")
    foreach(_s IN LISTS _syms)
        string(APPEND _c "    ESP_ELFSYM_EXPORT(${_s}),\n")
    endforeach()
    string(APPEND _c "    ESP_ELFSYM_END\n};\n")
    file(WRITE "${_out}.tmp" "${_c}")
    configure_file("${_out}.tmp" "${_out}" COPYONLY)
endfunction()

# 8003: feckless_abi_filter_section(<section file> <out> <key> <value>)
#
# A section's names, for a launcher whose <key> is <value>: the bare
# names outside any "@when", and those inside "@when <key> <value>".
# A "@when" line starts a group, the next "@when" or an "@end" ends it;
# a "@when" for another key or value drops its names. Written one name a
# line, for feckless_abi_write_table(). The build check
# (abicheck.cmake) reads the section file whole, so an app built for
# either value passes it.
function(feckless_abi_filter_section _in _out _key _value)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_in}")
    file(STRINGS "${_in}" _lines REGEX "^(@when|@end|[A-Za-z_][A-Za-z0-9_]*$)")
    set(_active TRUE)
    set(_kept "")
    foreach(_l IN LISTS _lines)
        if(_l MATCHES "^@when +([A-Za-z0-9_]+) +([A-Za-z0-9_.]+) *$")
            if(CMAKE_MATCH_1 STREQUAL "${_key}" AND CMAKE_MATCH_2 STREQUAL "${_value}")
                set(_active TRUE)
            else()
                set(_active FALSE)
            endif()
        elseif(_l MATCHES "^@end *$")
            set(_active TRUE)
        elseif(_l MATCHES "^@")
            message(FATAL_ERROR "feckless_abi: ${_in}: cannot read \"${_l}\"")
        elseif(_active)
            string(APPEND _kept "${_l}\n")
        endif()
    endforeach()
    file(WRITE "${_out}.tmp" "${_kept}")
    configure_file("${_out}.tmp" "${_out}" COPYONLY)
endfunction()
