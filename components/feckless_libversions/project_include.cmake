# SPDX-License-Identifier: MIT
#
# feckless_libversions(<target> [NAME <name>] [LICENSE <spdx>])
#
# Writes libversions.h -- the libraries this build was made from, each
# with its checkout and licence -- and puts it on <target>'s include path.
# Call it after project():
#
#   project(myapp)
#   idf_component_get_property(main_lib main COMPONENT_LIB)
#   feckless_libversions(${main_lib} NAME "My App" LICENSE MIT)
#
# NAME is the app's own first row (default: the project's name), at the
# project's git commit; LICENSE its licence (default MIT). The rest is in
# cmake/libversions.cmake, which also runs on its own:
#   cmake -DROOT=<project dir> -P cmake/libversions.cmake

set(FECKLESS_LIBVERSIONS_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(feckless_libversions _target)
    cmake_parse_arguments(_fl "" "NAME;LICENSE" "" ${ARGN})
    set(LV_NAME "${_fl_NAME}")
    set(LV_LICENSE "${_fl_LICENSE}")
    if(NOT LV_NAME)
        set(LV_NAME "${CMAKE_PROJECT_NAME}")
    endif()
    if(NOT LV_LICENSE)
        set(LV_LICENSE "MIT")
    endif()
    set(ROOT "${CMAKE_SOURCE_DIR}")
    include("${FECKLESS_LIBVERSIONS_DIR}/cmake/libversions.cmake")
    target_include_directories(${_target} PRIVATE "${LIBVERSIONS_DIR}")
endfunction()
