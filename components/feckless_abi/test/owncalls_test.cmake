# SPDX-License-Identifier: MIT
#
# u-feckless-0054: abicheck.cmake and boxcheck.cmake with an embedder's own
# calls (feckless_abi_box_elf()'s OWN_CALLS), on a host, against the real
# exports.txt, sections/, feckless_abi.h, inapp.txt and boxcalls_names.txt:
#   cmake -P components/feckless_abi/test/owncalls_test.cmake
#
# readelf is stood in for by a script that prints a symbol table written
# here, so each case is an ELF's imports and nothing else.
cmake_minimum_required(VERSION 3.16)

set(_abi "${CMAKE_CURRENT_LIST_DIR}/..")
set(_libc "${CMAKE_CURRENT_LIST_DIR}/../../feckless_box_libc")
get_filename_component(_abi "${_abi}" ABSOLUTE)
get_filename_component(_libc "${_libc}" ABSOLUTE)

set(_fails 0)
set(_checks 0)
macro(expect _what)
    math(EXPR _checks "${_checks} + 1")
    if(NOT (${ARGN}))
        message(SEND_ERROR "FAIL: ${_what}")
        math(EXPR _fails "${_fails} + 1")
    endif()
endmacro()

# Scratch files go to TMPDIR, never into the tree.
if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
    set(_tmp "$ENV{TMPDIR}/feckless_abi_owncalls_test")
else()
    set(_tmp "/tmp/feckless_abi_owncalls_test")
endif()
file(REMOVE_RECURSE "${_tmp}")
file(MAKE_DIRECTORY "${_tmp}")

# The stand-in readelf: -s prints <elf>.syms, -r prints <elf>.rels if any.
file(WRITE "${_tmp}/readelf"
"#!/bin/sh
if [ \"$1\" = \"-s\" ]; then cat \"$3.syms\"; else cat \"$3.rels\" 2>/dev/null; fi
")
file(CHMOD "${_tmp}/readelf" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

# An "ELF" importing these names.
function(make_elf _elf)
    set(_t "Symbol table '.dynsym' contains entries:\n   Num:    Value  Size Type    Bind   Vis      Ndx Name\n")
    set(_i 1)
    foreach(_n IN LISTS ARGN)
        string(APPEND _t "    ${_i}: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND ${_n}\n")
        math(EXPR _i "${_i} + 1")
    endforeach()
    file(WRITE "${_elf}" "elf")
    file(WRITE "${_elf}.syms" "${_t}")
endfunction()

# abicheck; result in <out>_rc (0 or not) and <out>_err.
function(abicheck _out _elf _extra)
    execute_process(COMMAND ${CMAKE_COMMAND}
        -DREADELF=${_tmp}/readelf -DELF=${_elf}
        -DEXPORTS=${_abi}/exports.txt -DHEADER=${_abi}/include/feckless_abi.h
        -DSECTIONS=${_abi}/sections "-DEXTRA_SECTIONS=${_extra}"
        -P ${_abi}/cmake/abicheck.cmake
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
    # CMake wraps a FATAL_ERROR's text; one line, one space, to match.
    string(REGEX REPLACE "[ \n]+" " " _all "${_o}${_e}")
    string(STRIP "${_all}" _all)
    set(${_out}_rc "${_rc}" PARENT_SCOPE)
    set(${_out}_err "${_all}" PARENT_SCOPE)
endfunction()
function(boxcheck _out _elf _answers)
    execute_process(COMMAND ${CMAKE_COMMAND}
        -DREADELF=${_tmp}/readelf -DELF=${_elf}
        -DINAPP=${_libc}/inapp.txt "-DANSWERS=${_answers}"
        -P ${_libc}/cmake/boxcheck.cmake
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
    # CMake wraps a FATAL_ERROR's text; one line, one space, to match.
    string(REGEX REPLACE "[ \n]+" " " _all "${_o}${_e}")
    string(STRIP "${_all}" _all)
    set(${_out}_rc "${_rc}" PARENT_SCOPE)
    set(${_out}_err "${_all}" PARENT_SCOPE)
endfunction()

# Jinxed's own calls, as its .bin would declare them.
file(WRITE "${_tmp}/jinxed_calls.txt"
"# an embedder's own calls
@symbol jinxed_calls_v1
jinxed_key_next
jinxed_send_start
")
file(WRITE "${_tmp}/nosym.txt" "jinxed_key_next\n")
file(WRITE "${_tmp}/storage.txt" "@symbol x_v1\njinxed_key_next\n")
set(_own "${_tmp}/jinxed_calls.txt")
set(_base feckless_abi_v1 feckless_abi_box_v1 fopen feckless_abi_storage_v2)

# 1. Own calls, their symbol referred to, the file given: passes.
make_elf(${_tmp}/a.elf ${_base} jinxed_key_next jinxed_send_start jinxed_calls_v1)
abicheck(r ${_tmp}/a.elf "${_own}")
expect("abicheck passes own calls given and referred to: ${r_err}" r_rc EQUAL 0)
expect("abicheck counts the own section" r_err MATCHES "feckless_abi_storage_v2 [+] jinxed_calls_v1$")
expect("a passing ELF is kept" EXISTS ${_tmp}/a.elf)

# 2. The same ELF without the file: its own names are outside the ABI.
abicheck(r ${_tmp}/a.elf "")
expect("abicheck refuses own names with no OWN_CALLS" NOT r_rc EQUAL 0)
expect("and names them" r_err MATCHES "jinxed_key_next")
expect("and deletes the ELF" NOT EXISTS ${_tmp}/a.elf)

# 3. Own names taken, the symbol not referred to.
make_elf(${_tmp}/b.elf ${_base} jinxed_key_next)
abicheck(r ${_tmp}/b.elf "${_own}")
expect("abicheck requires the own symbol" NOT r_rc EQUAL 0)
expect("and says which" r_err MATCHES "its own calls, jinxed_calls.*jinxed_calls_v1")
expect("and deletes the ELF" NOT EXISTS ${_tmp}/b.elf)

# 4. No own name taken: the symbol is not required, and the file is harmless.
make_elf(${_tmp}/c.elf ${_base})
abicheck(r ${_tmp}/c.elf "${_own}")
expect("abicheck passes an ELF that takes no own name: ${r_err}" r_rc EQUAL 0)
expect("and does not count the own section" NOT r_err MATCHES "jinxed_calls_v1")

# 5. A file with no @symbol, a file named as a feckless section, no file.
make_elf(${_tmp}/d.elf ${_base} jinxed_key_next jinxed_calls_v1)
abicheck(r ${_tmp}/d.elf "${_tmp}/nosym.txt")
expect("an own-calls file must have @symbol" NOT r_rc EQUAL 0 AND r_err MATCHES "no \"@symbol")
make_elf(${_tmp}/d.elf ${_base} jinxed_key_next jinxed_calls_v1)
abicheck(r ${_tmp}/d.elf "${_tmp}/storage.txt")
expect("an own-calls file may not be named as a section" NOT r_rc EQUAL 0 AND r_err MATCHES "name of a feckless section")
make_elf(${_tmp}/d.elf ${_base} jinxed_key_next jinxed_calls_v1)
abicheck(r ${_tmp}/d.elf "${_tmp}/missing.txt")
expect("a missing own-calls file is an error" NOT r_rc EQUAL 0 AND r_err MATCHES "no own-calls file")

# 6. Two files, '|'-separated.
file(WRITE "${_tmp}/more_calls.txt" "@symbol more_calls_v1\nmore_thing\n")
make_elf(${_tmp}/e.elf ${_base} jinxed_key_next jinxed_calls_v1 more_thing more_calls_v1)
abicheck(r ${_tmp}/e.elf "${_own}|${_tmp}/more_calls.txt")
expect("abicheck takes two own-calls files: ${r_err}" r_rc EQUAL 0)
make_elf(${_tmp}/e.elf ${_base} jinxed_key_next jinxed_calls_v1 more_thing)
abicheck(r ${_tmp}/e.elf "${_own}|${_tmp}/more_calls.txt")
expect("each own section's symbol is required" NOT r_rc EQUAL 0 AND r_err MATCHES "more_calls_v1")

# 7. A feckless section still needs its own symbol with own calls given.
make_elf(${_tmp}/f.elf feckless_abi_v1 fopen jinxed_key_next jinxed_calls_v1)
abicheck(r ${_tmp}/f.elf "${_own}")
expect("storage still requires feckless_abi_storage_v2" NOT r_rc EQUAL 0 AND r_err MATCHES "storage section")

# boxcheck
set(_answers "${_libc}/boxcalls_names.txt")
make_elf(${_tmp}/g.elf ${_base} jinxed_key_next jinxed_send_start jinxed_calls_v1)
boxcheck(r ${_tmp}/g.elf "${_answers}|${_own}")
expect("boxcheck answers own calls and their symbol: ${r_err}" r_rc EQUAL 0)
boxcheck(r ${_tmp}/g.elf "${_answers}")
expect("boxcheck refuses own calls not given" NOT r_rc EQUAL 0 AND r_err MATCHES "jinxed_key_next")
expect("and deletes the ELF" NOT EXISTS ${_tmp}/g.elf)
make_elf(${_tmp}/h.elf ${_base} jinxed_key_next jinxed_calls_v1 not_anyones)
boxcheck(r ${_tmp}/h.elf "${_answers}|${_own}")
expect("boxcheck still refuses a name nobody answers" NOT r_rc EQUAL 0 AND r_err MATCHES "not_anyones")
expect("and not the own ones" NOT r_err MATCHES "jinxed_key_next")
make_elf(${_tmp}/i.elf ${_base})
boxcheck(r ${_tmp}/i.elf "${_answers}")
expect("boxcheck unchanged with one answers file: ${r_err}" r_rc EQUAL 0)
make_elf(${_tmp}/j.elf ${_base} strlen)
boxcheck(r ${_tmp}/j.elf "${_answers}|${_own}")
expect("an in-app name is still refused" NOT r_rc EQUAL 0 AND r_err MATCHES "inapp")

file(REMOVE_RECURSE "${_tmp}")
if(_fails)
    message(FATAL_ERROR "owncalls_test: ${_fails} of ${_checks} checks failed")
endif()
message(STATUS "owncalls_test: all ${_checks} checks passed")
