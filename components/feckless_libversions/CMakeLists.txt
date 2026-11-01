# SPDX-License-Identifier: MIT
# feckless_libversions (8012): the library and licence list Avoidant,
# Lothesome, Mahjong and Defeatist each kept a copy of. The generator is
# project_include.cmake (feckless_libversions()); this is the drawing,
# plain code with no hardware of its own, so an app's ELF links it
# (ELF_COMPONENTS) and its gfx calls are the ABI's.
idf_component_register(
    SRCS "src/feckless_libversions.c"
    INCLUDE_DIRS "include"
    REQUIRES feckless_graphics_handler
)
