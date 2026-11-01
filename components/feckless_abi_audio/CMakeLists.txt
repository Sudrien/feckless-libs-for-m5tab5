# feckless-libs-for-m5tab5: feckless_abi_audio
#
# The feckless ABI's audio section (8008). A launcher that links this
# gives its apps Espressif's esp_audio_codec decoders: the table esp_elf
# resolves them against, generated from feckless_abi's sections/audio.txt,
# and feckless_abi_audio_v1, which every such app refers to. An app names
# this component too, for the codec's headers; its ELF links none of it,
# and esp_audio_codec stays out of its ELF_COMPONENTS (the archives are
# not position-independent: sections/audio.txt says why).
#
# WHOLE_ARCHIVE: nothing in a launcher refers to the table but
# feckless_abi_run()'s weak reference, which pulls nothing out of an
# archive by itself. The table's references pull in the codec.
#
# SPDX-License-Identifier: MIT

set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_audio_syms.c")

idf_component_register(
    SRCS "src/feckless_abi_audio.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_abi esp_audio_codec
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
        message(FATAL_ERROR "feckless_abi_audio is ${_own}, feckless_abi is ${_abiv}: "
                "name every feckless component at the same tag")
    endif()

    include("${_abi_dir}/cmake/abitable.cmake")
    feckless_abi_write_table("${_table}" "${_abi_dir}/sections/audio.txt"
                             g_feckless_abi_audio_elfsyms
                             "\"feckless_abi_audio_v1\"" feckless_abi_audio_v1)
endif()
