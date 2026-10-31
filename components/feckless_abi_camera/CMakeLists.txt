# feckless-libs-for-m5tab5: feckless_abi_camera
#
# The feckless ABI's camera section (4005). A launcher that links this
# gives its apps feckless_camera's frames: the table esp_elf resolves them against,
# generated from feckless_abi's sections/camera.txt, and
# feckless_abi_camera_v1, which every such app refers to. An app names
# this component too, for the headers; its ELF links none of it.
#
# WHOLE_ARCHIVE: nothing in a launcher refers to the table but
# feckless_abi_run()'s weak reference, which pulls nothing out of an
# archive by itself.
#
# SPDX-License-Identifier: MIT

set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_camera_syms.c")

idf_component_register(
    SRCS "src/feckless_abi_camera.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_abi feckless_camera
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
        message(FATAL_ERROR "feckless_abi_camera is ${_own}, feckless_abi is ${_abiv}: "
                "name every feckless component at the same tag")
    endif()

    # Not fatal: the section links without them, and camera_init() then
    # finds no sensor, which an app takes as "no camera". The launcher
    # is the build whose configuration the camera runs under.
    if(NOT CONFIG_CAMERA_SC202CS OR NOT CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER)
        message(WARNING "feckless_abi_camera: CONFIG_CAMERA_SC202CS=y and "
                "CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y are needed for "
                "camera_init() to find the Tab5's sensor (feckless_camera's README)")
    endif()

    include("${_abi_dir}/cmake/abitable.cmake")
    feckless_abi_write_table("${_table}" "${_abi_dir}/sections/camera.txt"
                             g_feckless_abi_camera_elfsyms
                             "\"feckless_abi_camera_v1\"" feckless_abi_camera_v1)
endif()
