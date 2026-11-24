# SPDX-License-Identifier: MIT
# feckless_box_calls: what a launcher answers a boxed app with
# (u-feckless-0006). Linked by a launcher, beside feckless_umode.
idf_component_register(
    SRCS "src/box_calls.c" "src/box_storage.c" "src/box_audio.c"
         "src/box_rtos.c"   # u-feckless-0045
         "src/box_codec.c"  # u-feckless-0052
         "src/box_image.c"  # u-feckless-0065
         "src/box_nvs.c"    # u-feckless-0053
         "src/box_camera.c" # u-feckless-0059
         "src/box_drivers.c" # u-feckless-0062
         "src/box_net.c"     # u-feckless-0071
         "src/box_http.c"    # u-feckless-0073
         "src/box_upcall.c"  # u-feckless-0075
         "src/box_netif.c"   # u-feckless-0076
         "src/box_httpd.c"   # u-feckless-0077
         "src/box_crypto.c"  # u-feckless-0078
         "src/box_fetch.c"   # u-feckless-0081
         "src/box_flash.c"   # u-feckless-0082
         "src/box_wifi.c"    # u-feckless-0083
         "src/box_hid.c"     # u-feckless-0086
    INCLUDE_DIRS "include"
    REQUIRES feckless_umode
    PRIV_REQUIRES feckless_abi feckless_drivers feckless_graphics_handler
                  feckless_storage_handler feckless_nvs_handler nvs_flash fatfs vfs
                  esp_timer log freertos esp_rom heap
                  esp_driver_uart esp_driver_gpio esp_driver_i2c   # u-feckless-0062
                  esp_driver_jpeg esp_mm   # u-feckless-0065: the JPEG engine, where the chip has one
                  lwip                     # u-feckless-0071
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

# u-feckless-0071: the routes and lwIP's sockets, answered in a box where the
# launcher links feckless_network_handler (box_net.c builds an empty table
# without it), as one that offers the ABI's network section does.
idf_component_optional_requires(PRIVATE feckless_network_handler)

# u-feckless-0073: esp_http_client, answered where box_net.c is and the
# launcher links the client and its certificate bundle (box_http.c builds an
# empty table without them).
idf_component_optional_requires(PRIVATE esp_http_client mbedtls)

# u-feckless-0076, 0077: esp_netif's handles and the HTTP server, where the
# launcher has them (box_netif.c and box_httpd.c build empty tables without).
idf_component_optional_requires(PRIVATE esp_netif esp_http_server)
