# SPDX-License-Identifier: MIT
#
# feckless_libversions(<target> [NAME <name>] [LICENSE <spdx>] [NO_IDF]
#                      [EXTRA "<module>|<checkout>|<licence>" ...])
#
# Writes libversions.h and libversions.tsv -- the libraries this build was
# made from, each with its checkout and licence -- and puts the header on
# <target>'s include path. Call it after project(), in the build that
# compiles the code including it (an app's elf/ for an ELF):
#
#   project(myapp)
#   idf_component_get_property(main_lib main COMPONENT_LIB)
#   feckless_libversions(${main_lib} NAME "My App" LICENSE MIT
#       EXTRA "World map (Protomaps)|${_date}|ODbL-1.0")
#
# NAME is the app's own first row (default: the project's name), at the
# project's git commit; LICENSE its licence (default MIT). NO_IDF leaves
# ESP-IDF out. EXTRA rows -- what only the app knows: a fetched blob, a
# vendored file with no manifest, rendered glyphs -- come last, in order.
# Sets LIBVERSIONS_DIR and LIBVERSIONS_TSV in the caller's scope.
#
# The rest is in cmake/libversions.cmake, which also runs on its own:
#   cmake -DROOT=<project dir> -P cmake/libversions.cmake

set(FECKLESS_LIBVERSIONS_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(feckless_libversions _target)
    cmake_parse_arguments(_fl "NO_IDF" "NAME;LICENSE" "EXTRA" ${ARGN})
    set(LV_NAME "${_fl_NAME}")
    set(LV_LICENSE "${_fl_LICENSE}")
    set(LV_NO_IDF "${_fl_NO_IDF}")
    set(LV_EXTRA "${_fl_EXTRA}")
    if(NOT LV_NAME)
        set(LV_NAME "${CMAKE_PROJECT_NAME}")
    endif()
    set(ROOT "${CMAKE_SOURCE_DIR}")
    include("${FECKLESS_LIBVERSIONS_DIR}/cmake/libversions.cmake")
    target_include_directories(${_target} PRIVATE "${LIBVERSIONS_DIR}")
    set(LIBVERSIONS_DIR "${LIBVERSIONS_DIR}" PARENT_SCOPE)
    set(LIBVERSIONS_TSV "${LIBVERSIONS_TSV}" PARENT_SCOPE)
endfunction()
