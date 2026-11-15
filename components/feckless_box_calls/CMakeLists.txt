# SPDX-License-Identifier: MIT
# feckless_box_calls: what a launcher answers a boxed app with
# (u-feckless-0006). Linked by a launcher, beside feckless_umode.
idf_component_register(
    SRCS "src/box_calls.c" "src/box_storage.c" "src/box_audio.c"
         "src/box_rtos.c"   # u-feckless-0045
         "src/box_codec.c"  # u-feckless-0052
    INCLUDE_DIRS "include"
    REQUIRES feckless_umode
    PRIV_REQUIRES feckless_abi feckless_drivers feckless_graphics_handler
                  feckless_storage_handler fatfs vfs
                  esp_timer log freertos esp_rom heap
)

# u-feckless-0052: esp_audio_codec's simple decoder, answered in a box where
# the launcher links the codec (box_codec.c builds an empty table without
# it). The registry's component by either name, as the manager gives it or
# as a copy in the project names it.
idf_component_optional_requires(PRIVATE espressif__esp_audio_codec esp_audio_codec)
