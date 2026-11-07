# SPDX-License-Identifier: MIT
# feckless_chooser (9012): a file chooser over gfx and storage_list.h,
# driven by the app's keys and taps. Plain code with no hardware or task
# of its own, so an app links it into its ELF (ELF_COMPONENTS): its gfx
# calls are the core list's, its storage calls the ABI's storage section.
idf_component_register(
    SRCS "src/feckless_chooser.c"
    INCLUDE_DIRS "include"
    REQUIRES feckless_graphics_handler feckless_storage_handler
)
