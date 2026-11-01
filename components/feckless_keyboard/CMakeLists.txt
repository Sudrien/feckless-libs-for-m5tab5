# SPDX-License-Identifier: MIT
# feckless_keyboard (8010): an on-screen keyboard over gfx -- Spurious's,
# lifted; 8011: four staggered rows, shift and a symbol page. Plain code
# with no hardware of its own, so an app links it into its ELF
# (ELF_COMPONENTS) and its gfx calls are the ABI's.
idf_component_register(
    SRCS "src/feckless_keyboard.c"
    INCLUDE_DIRS "include"
    REQUIRES feckless_graphics_handler
)
