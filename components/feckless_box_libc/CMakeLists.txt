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
