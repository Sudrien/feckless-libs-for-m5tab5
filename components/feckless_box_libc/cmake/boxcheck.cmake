# SPDX-License-Identifier: MIT
#
# A boxed app's ELF, after feckless_abi's own check (abicheck.cmake):
#
#   cmake -DREADELF=<readelf> -DELF=<app.elf> -DINAPP=<inapp.txt>
#         -DANSWERS=<boxcalls_names.txt> -P cmake/boxcheck.cmake
#
# It must refer to feckless_abi_box_v1 (feckless_abi_require_box()), and it
# must not import any name in inapp.txt: those are linked into the app, and
# the box has no host call for them. A failing ELF is deleted, as abicheck
# does, so a failed build leaves nothing to copy.

cmake_policy(SET CMP0057 NEW)   # IN_LIST, in script mode

execute_process(COMMAND "${READELF}" -s --wide "${ELF}" OUTPUT_VARIABLE _out ERROR_QUIET)
if(NOT _out MATCHES "Symbol table")
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_box_libc: no symbol table in ${ELF}")
endif()
string(REPLACE "\n" ";" _lines "${_out}")
set(_und "")
foreach(_l IN LISTS _lines)
    if(_l MATCHES "(GLOBAL|WEAK) +DEFAULT +UND +([A-Za-z_][A-Za-z0-9_]*)$")
        list(APPEND _und "${CMAKE_MATCH_2}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _und)
file(STRINGS "${INAPP}" _inapp REGEX "^[A-Za-z_][A-Za-z0-9_]*$")

if(NOT "feckless_abi_box_v1" IN_LIST _und)
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_box_libc: ${ELF} does not refer to feckless_abi_box_v1. "
            "Call feckless_abi_require_box() in app_main(), beside feckless_abi_require().")
endif()
set(_bad "")
foreach(_s IN LISTS _und)
    if(_s IN_LIST _inapp)
        list(APPEND _bad "${_s}")
    endif()
endforeach()
if(_bad)
    string(REPLACE ";" "\n  " _bad "${_bad}")
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_box_libc: ${ELF} imports names that must be inside a "
            "boxed app (inapp.txt); the link did not take them from feckless_box_libc:\n  ${_bad}")
endif()
# u-feckless-0026: and every import must be one the box's table answers
# (boxcalls_names.txt): the ABI check passes a name the launcher exports, but
# the box answers only its own table, so a name outside it failed the load,
# on the board ("Can't find common sqrt"), not the build.
file(STRINGS "${ANSWERS}" _answers REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
set(_unanswered "")
foreach(_s IN LISTS _und)
    if(NOT _s IN_LIST _answers)
        list(APPEND _unanswered "${_s}")
    endif()
endforeach()
if(_unanswered)
    string(REPLACE ";" "\n  " _unanswered "${_unanswered}")
    file(REMOVE "${ELF}")
    message(FATAL_ERROR "feckless_box_libc: ${ELF} imports names the box does not answer "
            "(feckless_box_calls' table, boxcalls_names.txt); its load would fail on them:\n  ${_unanswered}")
endif()
list(LENGTH _und _n)
message(STATUS "feckless_box_libc: boxed, ${_n} imports, none of them in-app names, all answered by the box")
