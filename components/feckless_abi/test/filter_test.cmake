# SPDX-License-Identifier: MIT
#
# 8003: feckless_abi_filter_section() and the real crypto.txt, on a host:
#   cmake -P components/feckless_abi/test/filter_test.cmake
#
# configure_file() and the directory property are not in script mode, so
# they are stood in for; the function's own logic is what runs.
cmake_minimum_required(VERSION 3.16)

set(_here "${CMAKE_CURRENT_LIST_DIR}")
function(set_property)
endfunction()
function(configure_file _from _to _opt)
    file(READ "${_from}" _c)
    file(WRITE "${_to}" "${_c}")
endfunction()
include("${_here}/../cmake/abitable.cmake")

set(_fails 0)
macro(expect _what)
    if(NOT (${ARGN}))
        message(SEND_ERROR "FAIL: ${_what}")
        math(EXPR _fails "${_fails} + 1")
    endif()
endmacro()

# Scratch files go to TMPDIR, never into the tree.
if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
    set(_tmp "$ENV{TMPDIR}/feckless_abi_filter_test")
else()
    set(_tmp "/tmp/feckless_abi_filter_test")
endif()
file(MAKE_DIRECTORY "${_tmp}")

# A small file: both branches, an @end, a stray comment with brackets.
file(WRITE "${_tmp}/t.txt"
"# a comment [with brackets]; and a semicolon
@symbol t_v1
common_a
@when k 3
three_a
three_b
@when k 4
four_a
@end
after_end
@when other 3
other_a
@end
")
feckless_abi_filter_section("${_tmp}/t.txt" "${_tmp}/k3.txt" k 3)
feckless_abi_filter_section("${_tmp}/t.txt" "${_tmp}/k4.txt" k 4)
file(STRINGS "${_tmp}/k3.txt" _k3)
file(STRINGS "${_tmp}/k4.txt" _k4)
string(REPLACE ";" "," _k3 "${_k3}")
string(REPLACE ";" "," _k4 "${_k4}")
expect("k=3 keeps common, its own, and what follows @end"
       _k3 STREQUAL "common_a,three_a,three_b,after_end")
expect("k=4 keeps common, its own, and what follows @end"
       _k4 STREQUAL "common_a,four_a,after_end")

# The real section: Mbed TLS 4's marker and names, and no 3.6 names.
set(_crypto "${_here}/../sections/crypto.txt")
feckless_abi_filter_section("${_crypto}" "${_tmp}/c4.txt" mbedtls 4)
file(STRINGS "${_tmp}/c4.txt" _c4)
expect("4 has its marker" feckless_abi_crypto_mbedtls4 IN_LIST _c4)
foreach(_n psa_hash_compute psa_crypto_init)
    expect("${_n} in 4" ${_n} IN_LIST _c4)
endforeach()
foreach(_n feckless_abi_crypto_mbedtls3 mbedtls_sha256)
    expect("${_n} gone" NOT ${_n} IN_LIST _c4)
endforeach()
# 8007: what version 1 had and the launcher owns now is gone from both.
foreach(_n mbedtls_pk_init mbedtls_x509write_crt_pem mbedtls_pk_get_type
           mbedtls_pk_setup mbedtls_ecp_gen_key mbedtls_pkcs5_pbkdf2_hmac_ext
           psa_generate_key mbedtls_pk_copy_from_psa psa_key_derivation_setup)
    expect("${_n} gone" NOT ${_n} IN_LIST _c4)
endforeach()
# The symbol line and comments never become names.
expect("no @symbol line in a table" NOT "@symbol feckless_abi_crypto_v2" IN_LIST _c3)
list(LENGTH _c3 _n3)
list(LENGTH _c4 _n4)
message(STATUS "crypto.txt: ${_n3} names for Mbed TLS 3, ${_n4} for 4")

# No name twice in a file.
list(REMOVE_DUPLICATES _c3)
list(LENGTH _c3 _u3)
expect("no duplicate in 3" _u3 EQUAL _n3)

if(_fails)
    message(FATAL_ERROR "${_fails} failed")
endif()
message(STATUS "filter_test: all passed")
