# SPDX-License-Identifier: MIT
#
# An app's ELF against the ABI (feckless_abi_elf() runs this):
#
#   cmake -DREADELF=<readelf> -DELF=<app.elf> -DEXPORTS=<exports.txt>
#         -DHEADER=<feckless_abi.h> -P cmake/abicheck.cmake
#
# Every undefined symbol must be in exports.txt, or the launcher has no
# entry for it and esp_elf_relocate() fails on the board. And the ELF
# must refer to this version's FECKLESS_ABI_SYMBOL, or a launcher of
# another version would load it.
#
# A failing ELF is deleted: the check runs after the link, so without
# that the ELF of a failed build is left beside the log, and copies as
# well as a good one -- then fails on the board instead.

cmake_policy(SET CMP0057 NEW)   # IN_LIST, in script mode

file(STRINGS "${HEADER}" _v REGEX "^#define FECKLESS_ABI_SYMBOL ")
string(REGEX REPLACE "^#define FECKLESS_ABI_SYMBOL +([A-Za-z0-9_]+).*$" "\\1" _version_sym "${_v}")

# readelf exits non-zero on these ("no .dynamic section in the dynamic
# segment": project_elf() strips it, and esp_elf does not want it), but
# the symbol table it prints is whole. A missing table is the failure.
execute_process(COMMAND "${READELF}" -s --wide "${ELF}"
                OUTPUT_VARIABLE _out ERROR_QUIET)
if(NOT _out MATCHES "Symbol table")
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_abi: no symbol table from ${READELF} in ${ELF}")
endif()
string(REPLACE "\n" ";" _lines "${_out}")
set(_need "")
foreach(_l IN LISTS _lines)
    # WEAK too: esp_elf resolves a weak reference like any other, and
    # fails on one it cannot.
    if(_l MATCHES "(GLOBAL|WEAK) +DEFAULT +UND +([A-Za-z_][A-Za-z0-9_]*)$")
        list(APPEND _need "${CMAKE_MATCH_2}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _need)

# And every symbol a relocation names. elf_loader 2.0.0 looks each one up
# in the launcher's table, even one the ELF defines itself, so a defined
# global reached through the PLT or GOT fails on the board as surely as
# an import outside the ABI. feckless_abi_elf() links with -Bsymbolic so
# there are none; this is what says so.
execute_process(COMMAND "${READELF}" -r --wide "${ELF}"
                OUTPUT_VARIABLE _rout ERROR_QUIET)
string(REPLACE "\n" ";" _rlines "${_rout}")
foreach(_l IN LISTS _rlines)
    if(_l MATCHES "R_[A-Z0-9_]+ +[0-9a-f]+ +([A-Za-z_][A-Za-z0-9_]*) [+-]")
        list(APPEND _need "${CMAKE_MATCH_1}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _need)

file(STRINGS "${EXPORTS}" _have REGEX "^[A-Za-z_][A-Za-z0-9_]*$")

set(_missing "")
set(_versioned FALSE)
foreach(_s IN LISTS _need)
    if(_s STREQUAL _version_sym)
        set(_versioned TRUE)
    elseif(_s MATCHES "^feckless_abi_v[0-9]+$" OR NOT _s IN_LIST _have)
        list(APPEND _missing "${_s}")
    endif()
endforeach()

if(_missing)
    list(SORT _missing)
    string(REPLACE ";" "\n  " _missing "${_missing}")
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_abi: ${ELF} needs symbols outside the ABI "
            "(${_version_sym}). Link them into the ELF (ELF_COMPONENTS), or "
            "they are a new ABI version:\n  ${_missing}")
endif()
if(NOT _versioned)
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_abi: ${ELF} does not refer to ${_version_sym}. "
            "Call feckless_abi_require() first thing in app_main().")
endif()
list(LENGTH _need _n)
message(STATUS "feckless_abi: ${_n} symbols, all in ${_version_sym}")
