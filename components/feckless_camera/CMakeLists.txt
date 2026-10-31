# SPDX-License-Identifier: MIT
# feckless_camera: the Tab5's camera as luma frames.
#
# driver/i2c_master.h is PUBLIC because camera.h takes the bus handle.
idf_component_register(
    SRCS "src/camera.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_video
)
