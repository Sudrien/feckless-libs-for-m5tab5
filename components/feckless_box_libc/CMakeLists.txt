# SPDX-License-Identifier: MIT
# feckless_box_libc: the part of libc a boxed app links into its own ELF
# (u-feckless-0005). Built into the app, never into a launcher. See
# project_include.cmake for feckless_abi_box_elf().
idf_component_register(
    SRCS "src/box_errno.c" "src/box_strto.c" "src/box_printf.c" "src/box_log.c"
         "src/box_math.c" "src/box_heap.c"   # u-feckless-0020
         "src/box_task.c"                    # u-feckless-0023
         "src/box_fpconv.c"                  # u-feckless-0027
         "src/box_softdouble.c"              # u-feckless-0036
         "src/box_div64.c"                   # u-feckless-0038
         "src/box_strtod.c" "src/box_string.c" "src/box_scanf.c" "src/box_stdio.c"   # u-feckless-0048
         "src/box_upcall.c"                  # u-feckless-0075
    INCLUDE_DIRS "include"
    # printf.c includes <printf/printf.h>; newlib_math includes "fdlibm.h"
    PRIV_INCLUDE_DIRS "vendor" "newlib_math/common"
)

# u-feckless-0041: double libm from Espressif's newlib, unchanged
# (newlib_math/README.md). Added here, not in SRCS above, to keep their
# list in one place; their warnings are newlib's.
set(_newlib_math
    math/e_acos math/e_asin math/e_atan2 math/e_exp math/e_fmod math/e_log
    math/e_log10 math/e_pow math/e_rem_pio2 math/e_sinh math/e_sqrt
    math/k_cos math/k_rem_pio2 math/k_sin math/k_tan
    math/s_asinh math/s_atan math/s_ceil math/s_cos math/s_fabs math/s_floor
    math/s_ldexp math/s_sin math/s_tan
    math/w_acos math/w_asin math/w_atan2 math/w_exp math/w_fmod math/w_log
    math/w_log10 math/w_pow math/w_sinh math/w_sqrt
    common/math_err common/s_copysign common/s_expm1 common/s_finite
    common/s_log1p common/s_lround common/s_nan common/s_round common/s_scalbn)
foreach(_f ${_newlib_math})
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/newlib_math/${_f}.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/newlib_math/${_f}.c"
        PROPERTIES COMPILE_OPTIONS "-w;-fno-builtin")
endforeach()
# printf.c is eyalroz/printf as released; its warnings are its own.
set_source_files_properties("src/box_printf.c" PROPERTIES COMPILE_OPTIONS "-w")
# u-feckless-0058: box_string.c defines stpcpy, which GCC would otherwise
# turn its own loop -- or a strcpy and a strlen -- back into a call to.
set_source_files_properties("src/box_string.c" PROPERTIES COMPILE_OPTIONS "-fno-builtin-stpcpy")

# u-feckless-0063: nothing of this component in a firmware.
#
# Built into the app, never into a launcher -- but an app whose one manifest
# serves its ELF and the .bin that runs it (a standalone launcher, as
# Spurious's, Lothesome's and Errant's umode branches have) gets this
# component in the firmware's build as well: the component manager makes it
# a requirement of main there too, and ESP-IDF links every component of a
# build into the executable. Its stdio and strerror then meet the
# toolchain's as soon as the firmware uses them: "multiple definition of
# `stderr'", `stdin', `stdout', `fgetc', `getc', `fputc', `putc',
# `strerror' (Errant's u-errant-0007).
#
# So, at the end of the top-level CMakeLists.txt, in a build that has not
# called feckless_abi_box_elf(): the library's sources become empty.c. It
# stays in the build, its include directory with it (feckless_box.h), and
# links nothing. feckless_abi_box_elf() marks its build, which is left as it
# was.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    function(_feckless_box_libc_not_in_firmware lib empty)
        get_property(_elf GLOBAL PROPERTY FECKLESS_BOX_ELF)
        if(_elf)
            return()
        endif()
        if(NOT TARGET "${lib}")
            message(FATAL_ERROR "feckless_box_libc: no target '${lib}' to empty")
        endif()
        set_property(TARGET ${lib} PROPERTY SOURCES "${empty}")
        message(STATUS "feckless_box_libc: not an ELF's build, so nothing of it is linked")
    endfunction()
    # A deferred call's arguments are read when it runs, in the top-level
    # directory, where COMPONENT_LIB is not this component's: written out
    # now, by EVAL.
    cmake_language(EVAL CODE "
        cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]]
            CALL _feckless_box_libc_not_in_firmware [[${COMPONENT_LIB}]] [[${CMAKE_CURRENT_LIST_DIR}/empty.c]])")
endif()
