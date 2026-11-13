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
    INCLUDE_DIRS "include"
    # printf.c includes <printf/printf.h>
    PRIV_INCLUDE_DIRS "vendor"
)
# printf.c is eyalroz/printf as released; its warnings are its own.
set_source_files_properties("src/box_printf.c" PROPERTIES COMPILE_OPTIONS "-w")
