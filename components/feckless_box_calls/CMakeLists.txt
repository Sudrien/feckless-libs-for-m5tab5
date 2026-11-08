# SPDX-License-Identifier: MIT
# feckless_box_calls: what a launcher answers a boxed app with
# (u-feckless-0006). Linked by a launcher, beside feckless_umode.
idf_component_register(
    SRCS "src/box_calls.c"
    INCLUDE_DIRS "include"
    REQUIRES feckless_umode
    PRIV_REQUIRES feckless_abi feckless_drivers feckless_graphics_handler
                  esp_timer log freertos esp_rom
)
