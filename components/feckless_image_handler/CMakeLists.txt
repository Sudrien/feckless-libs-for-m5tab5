# SPDX-License-Identifier: MIT
# feckless_image_handler: pictures, from their bytes (7004), and the
# software decoders (7005): stb_image for JPEG and GIF, J40 for JPEG XL,
# and pngle for PNG (7006); img_decode() (7007) and its ESP backends (7008:
# the P4 hardware JPEG engine, TJpgDec through esp_jpeg, big-stack runners).
#
# stb_image.h, j40.h, pngle and miniz are fetched at configure time, pinned and
# hash-checked (cmake/vendored.cmake), into vendor/, which is gitignored.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/vendored.cmake")
endif()

idf_component_register(
    SRCS "src/image_ico.c" "src/image_stb.c" "src/image_jxl.c" "src/image_png.c"
         "src/image_decode.c" "src/image_esp.c"
         "vendor/pngle.c" "vendor/miniz.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor"
    PRIV_REQUIRES heap log freertos esp_driver_jpeg espressif__esp_jpeg
)

# -fwrapv: a damaged file can overflow signed arithmetic in stb_image's
# IDCT and in J40 (the host test's corruption sweeps found it under
# UBSan). Defined as wrapping, it is garbage pixels rather than undefined
# behaviour. The warnings are upstream's: both files are fetched
# unmodified, and IDF's set is wider than theirs.
set_source_files_properties("src/image_stb.c" PROPERTIES COMPILE_OPTIONS
    "-fwrapv;-Wno-unused-function;-Wno-sign-compare;-Wno-unused-but-set-variable;-Wno-type-limits;-Wno-shift-negative-value;-Wno-implicit-fallthrough")

# J40: on RISC-V newlib int32_t is long, not int. J40 prints int32_t with
# %d and passes an int * where it declares int32_t *: the same 32 bits
# either way, and invisible on the host, where int32_t is int. GCC 14
# makes the pointer mismatch an error by default. And -Os: J40 is one
# 130 KB translation unit of macros that -O2 inlines into itself; -Os
# takes it to about 60%, and a JPEG XL cover is rare and decoded off the
# UI's path.
set_source_files_properties("src/image_jxl.c" PROPERTIES COMPILE_OPTIONS
    "-fwrapv;-Os;-Wno-unused-function;-Wno-sign-compare;-Wno-unused-but-set-variable;-Wno-type-limits;-Wno-implicit-fallthrough;-Wno-unused-variable;-Wno-maybe-uninitialized;-Wno-missing-field-initializers;-Wno-format;-Wno-incompatible-pointer-types;-Wno-error")

# pngle.c and miniz.c are fetched unmodified, so upstream's warnings stay
# upstream's. miniz.h is a one-line shim that #includes miniz.c with
# MINIZ_HEADER_FILE_ONLY defined, so miniz.c is both the header and the
# implementation, by design; compiling it once as a source gives the
# implementation exactly once. -fwrapv for the reason stb has it.
set_source_files_properties("vendor/pngle.c" PROPERTIES COMPILE_OPTIONS
    "-fwrapv;-Wno-sign-compare;-Wno-unused-function;-Wno-unused-parameter")
set_source_files_properties("vendor/miniz.c" PROPERTIES COMPILE_OPTIONS
    "-fwrapv;-Wno-unused-function;-Wno-unused-parameter;-Wno-sign-compare;-Wno-attributes")
