# SPDX-License-Identifier: MIT
# feckless_mail (8016): the outgoing mail account in NVS, shared by a
# launcher and its apps (mailcfg), and a form to set it with gfx and
# feckless_keyboard (mailform). Plain code with no hardware or task of its
# own, so an app links it into its ELF (ELF_COMPONENTS): its NVS calls are
# the ABI's nvs section, its gfx calls the core list's.
idf_component_register(
    SRCS "src/mailcfg.c" "src/mailform.c"
    INCLUDE_DIRS "include"
    REQUIRES feckless_nvs_handler feckless_graphics_handler feckless_keyboard
)
