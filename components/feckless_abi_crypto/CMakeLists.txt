# feckless-libs-for-m5tab5: feckless_abi_crypto
#
# The feckless ABI's crypto section (8003; hashing only since 8007). A
# launcher that links this gives its apps a SHA-256: the table
# esp_elf resolves them against, generated from feckless_abi's
# sections/crypto.txt for this launcher's Mbed TLS major (3 on IDF 5.x,
# 4 on IDF 6.x), and feckless_abi_crypto_v2, which every such app refers
# to. An app names this component too, for the header; its ELF links none
# of it.
#
# WHOLE_ARCHIVE: nothing in a launcher refers to the table but
# feckless_abi_run()'s weak reference, which pulls nothing out of an
# archive by itself. The names in the table are what keeps each Mbed TLS
# function in the launcher's link.
#
# SPDX-License-Identifier: MIT

set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_crypto_syms.c")

idf_component_register(
    SRCS "src/feckless_abi_crypto.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_abi mbedtls
    PRIV_REQUIRES elf_loader
    WHOLE_ARCHIVE
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(_abi_dir feckless_abi COMPONENT_DIR)

    # One tag, as feckless_abi checks for its own siblings.
    set(_versions "")
    foreach(_dir "${CMAKE_CURRENT_LIST_DIR}" "${_abi_dir}")
        file(STRINGS "${_dir}/idf_component.yml" _v REGEX "^version:")
        string(REGEX REPLACE "^version:[ \t]*\"?([^\" \t]*)\"?.*" "\\1" _v "${_v}")
        list(APPEND _versions "${_v}")
    endforeach()
    list(GET _versions 0 _own)
    list(GET _versions 1 _abiv)
    if(NOT _own STREQUAL _abiv)
        message(FATAL_ERROR "feckless_abi_crypto is ${_own}, feckless_abi is ${_abiv}: "
                "name every feckless component at the same tag")
    endif()

    # Which Mbed TLS this launcher has: the version in its own header,
    # and failing that, the IDF's (6 moved to Mbed TLS 4).
    idf_component_get_property(_mbedtls_dir mbedtls COMPONENT_DIR)
    set(_tls_major "")
    set(_bi "${_mbedtls_dir}/mbedtls/include/mbedtls/build_info.h")
    if(EXISTS "${_bi}")
        file(STRINGS "${_bi}" _mv REGEX "^#define MBEDTLS_VERSION_MAJOR ")
        string(REGEX REPLACE "^#define MBEDTLS_VERSION_MAJOR +([0-9]+).*$" "\\1" _tls_major "${_mv}")
    endif()
    if(NOT _tls_major MATCHES "^[0-9]+$")
        if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
            set(_tls_major 4)
        else()
            set(_tls_major 3)
        endif()
        message(WARNING "feckless_abi_crypto: no MBEDTLS_VERSION_MAJOR in ${_bi}; "
                "taking Mbed TLS ${_tls_major} from IDF ${IDF_VERSION_MAJOR}")
    endif()
    if(NOT _tls_major EQUAL 3 AND NOT _tls_major EQUAL 4)
        message(FATAL_ERROR "feckless_abi_crypto: Mbed TLS ${_tls_major}: "
                "sections/crypto.txt has names for 3 and 4")
    endif()

    include("${_abi_dir}/cmake/abitable.cmake")
    set(_names "${CMAKE_CURRENT_BINARY_DIR}/crypto_names.txt")
    feckless_abi_filter_section("${_abi_dir}/sections/crypto.txt" "${_names}"
                                mbedtls ${_tls_major})
    feckless_abi_write_table("${_table}" "${_names}"
                             g_feckless_abi_crypto_elfsyms
                             "\"feckless_abi_crypto_v2\"" feckless_abi_crypto_v2)
endif()
