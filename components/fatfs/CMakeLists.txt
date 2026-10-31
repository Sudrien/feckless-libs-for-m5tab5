# feckless-libs-for-m5tab5: fatfs
#
# ESP-IDF's own fatfs component, with exFAT. IDF compiles exFAT out of
# FatFs (ffconf.h, FF_FS_EXFAT 0) and has no option for it, so this
# component takes fatfs's name -- a component added later overrides one
# of the same name, and the component manager adds this after IDF's --
# and builds IDF's fatfs from a patched copy:
#
#   1. copy $IDF_PATH/components/fatfs, whichever IDF is building, into
#      the build directory, and run tools/patch_fatfs.py over the copy;
#   2. build it with IDF's own CMakeLists.txt, read from the copy, with
#      COMPONENT_DIR pointing at the copy.
#
# So it follows the installed IDF as the per-project copies did
# (cmake/exfat.cmake, tools/enable_exfat.sh), without anything having to
# exist before the first configure. Nothing in the IDF tree is touched.
# The copy is made again when the IDF, or patch_fatfs.py, changes.
#
# CONFIG_FECKLESS_FATFS_EXFAT=n builds IDF's fatfs unpatched: FAT32 only.
#
# SPDX-License-Identifier: MIT

set(_feckless_idf_fatfs "${IDF_PATH}/components/fatfs")
if(NOT IDF_PATH)
    set(_feckless_idf_fatfs "$ENV{IDF_PATH}/components/fatfs")
endif()
if(NOT EXISTS "${_feckless_idf_fatfs}/CMakeLists.txt")
    message(FATAL_ERROR "fatfs (feckless): no fatfs component at ${_feckless_idf_fatfs}")
endif()

if(CMAKE_BUILD_EARLY_EXPANSION OR NOT CONFIG_FECKLESS_FATFS_EXFAT)
    # The requirements are IDF's either way, and so is everything else
    # when exFAT is off.
    set(COMPONENT_DIR "${_feckless_idf_fatfs}")
    include("${_feckless_idf_fatfs}/CMakeLists.txt")
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        message(STATUS "fatfs (feckless): CONFIG_FECKLESS_FATFS_EXFAT is off; IDF's FatFs, FAT32 only")
    endif()
    return()
endif()

set(_feckless_patch "${CMAKE_CURRENT_LIST_DIR}/tools/patch_fatfs.py")
set(_feckless_copy "${CMAKE_BINARY_DIR}/feckless_fatfs")
file(SHA256 "${_feckless_patch}" _feckless_patch_sha)
file(SHA256 "${_feckless_idf_fatfs}/src/ff.c" _feckless_ff_sha)
file(SHA256 "${_feckless_idf_fatfs}/CMakeLists.txt" _feckless_cm_sha)
set(_feckless_stamp_want "${_feckless_idf_fatfs}\n${_feckless_cm_sha}\n${_feckless_ff_sha}\n${_feckless_patch_sha}\n")
set(_feckless_stamp "${_feckless_copy}/.feckless_fatfs")
set(_feckless_stamp_have "")
if(EXISTS "${_feckless_stamp}")
    file(READ "${_feckless_stamp}" _feckless_stamp_have)
endif()

if(NOT _feckless_stamp_have STREQUAL _feckless_stamp_want)
    file(REMOVE_RECURSE "${_feckless_copy}")
    file(COPY "${_feckless_idf_fatfs}/" DESTINATION "${_feckless_copy}"
         NO_SOURCE_PERMISSIONS)
    idf_build_get_property(_feckless_python PYTHON)
    if(NOT _feckless_python)
        set(_feckless_python python3)
    endif()
    execute_process(
        COMMAND "${_feckless_python}" "${_feckless_patch}" "${_feckless_copy}"
        RESULT_VARIABLE _feckless_rc
        OUTPUT_VARIABLE _feckless_out
        ERROR_VARIABLE _feckless_err)
    if(NOT _feckless_rc EQUAL 0)
        file(REMOVE_RECURSE "${_feckless_copy}")
        message(FATAL_ERROR
            "fatfs (feckless): tools/patch_fatfs.py failed on this IDF's FatFs:\n"
            "${_feckless_out}${_feckless_err}"
            "Set CONFIG_FECKLESS_FATFS_EXFAT=n to build without exFAT.")
    endif()
    file(WRITE "${_feckless_stamp}" "${_feckless_stamp_want}")
    message(STATUS "fatfs (feckless): exFAT, from ${_feckless_idf_fatfs}")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${_feckless_patch}" "${_feckless_idf_fatfs}/src/ff.c"
             "${_feckless_idf_fatfs}/CMakeLists.txt")

# IDF's CMakeLists.txt, from the copy: its sources resolve against
# COMPONENT_DIR, its include directories against the file being read.
set(COMPONENT_DIR "${_feckless_copy}")
include("${_feckless_copy}/CMakeLists.txt")
