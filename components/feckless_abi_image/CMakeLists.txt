# feckless-libs-for-m5tab5: feckless_abi_image
#
# The feckless ABI's image section (7010). A launcher that links this
# gives its apps the P4's hardware JPEG decoder: the table esp_elf
# resolves them against, generated from feckless_abi's sections/image.txt,
# and feckless_abi_image_v1, which every such app refers to. An app names
# this component too, for the header; its ELF links none of it.
#
# WHOLE_ARCHIVE: nothing in a launcher refers to the table but
# feckless_abi_run()'s weak reference, which pulls nothing out of an
# archive by itself.
#
# SPDX-License-Identifier: MIT

set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_image_syms.c")

idf_component_register(
    SRCS "src/feckless_abi_image.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_abi esp_driver_jpeg
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
        message(FATAL_ERROR "feckless_abi_image is ${_own}, feckless_abi is ${_abiv}: "
                "name every feckless component at the same tag")
    endif()

    include("${_abi_dir}/cmake/abitable.cmake")
    feckless_abi_write_table("${_table}" "${_abi_dir}/sections/image.txt"
                             g_feckless_abi_image_elfsyms
                             "\"feckless_abi_image_v1\"" feckless_abi_image_v1)
endif()
