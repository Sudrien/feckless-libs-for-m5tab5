# feckless-libs-for-m5tab5: feckless_drivers
#
# usb_host_uac is REQUIRED BY NAME and not listed in idf_component.yml:
# the player vendors it with a descriptor-parser fix the registry does not
# have (its 5026), and a project's own components/ wins over anything a
# manifest would pull. An application without its own copy adds
# espressif/usb_host_uac to its manifest. `usb` is espressif/usb, brought in by usb_host_uac's manifest.
#
# esp_lcd_touch is PUBLIC because touch.h hands out its types.
#
# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/tab5io.c"
         "src/usbhost.c" "src/uac.c" "src/hid.c" "src/battery.c"
         "src/rtc8130.c" "src/touch.c"
         "src/audio_out.c" "src/polyrsp.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_driver_i2c esp_driver_gpio esp_lcd_touch usb
    PRIV_REQUIRES esp_timer esp_driver_i2s usb_host_uac
             esp_lcd_touch_gt911 esp_lcd_touch_st7123
)

# 3006: usb_host_uac is required by name. The one beside this component
# in feckless-libs has the Tab5's fixes; say so when another is in use
# (the registry's, or a project's own copy), rather than fail.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(_uac_dir usb_host_uac COMPONENT_DIR)
    set(_uac_url "")
    if(EXISTS "${_uac_dir}/idf_component.yml")
        file(STRINGS "${_uac_dir}/idf_component.yml" _uac_url REGEX "^url:")
    endif()
    if(NOT _uac_url MATCHES "feckless-libs-for-m5tab5")
        message(WARNING "feckless_drivers: usb_host_uac is ${_uac_dir}, not "
                "feckless-libs' (components/usb_host_uac), which has the Tab5's "
                "fixes. Name that one in the project's manifest.")
    endif()
endif()
