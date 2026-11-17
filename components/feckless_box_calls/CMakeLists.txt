# SPDX-License-Identifier: MIT
# feckless_box_calls: what a launcher answers a boxed app with
# (u-feckless-0006). Linked by a launcher, beside feckless_umode.
idf_component_register(
    SRCS "src/box_calls.c" "src/box_storage.c" "src/box_audio.c"
         "src/box_rtos.c"   # u-feckless-0045
         "src/box_codec.c"  # u-feckless-0052
         "src/box_nvs.c"    # u-feckless-0053
         "src/box_camera.c" # u-feckless-0059
         "src/box_drivers.c" # u-feckless-0062
    INCLUDE_DIRS "include"
    REQUIRES feckless_umode
    PRIV_REQUIRES feckless_abi feckless_drivers feckless_graphics_handler
                  feckless_storage_handler feckless_nvs_handler nvs_flash fatfs vfs
                  esp_timer log freertos esp_rom heap
                  esp_driver_uart esp_driver_gpio esp_driver_i2c   # u-feckless-0062
)

# u-feckless-0052: esp_audio_codec's simple decoder, answered in a box where
# the launcher links the codec (box_codec.c builds an empty table without
# it). The registry's component by either name, as the manager gives it or
# as a copy in the project names it.
idf_component_optional_requires(PRIVATE espressif__esp_audio_codec esp_audio_codec)

# u-feckless-0059: feckless_camera's frames, answered in a box where the
# launcher links the camera (box_camera.c builds an empty table without it),
# as one that offers the ABI's camera section does.
idf_component_optional_requires(PRIVATE feckless_camera)
