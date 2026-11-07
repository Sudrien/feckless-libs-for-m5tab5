# feckless-libs-for-m5tab5: feckless_storage_handler
#
# usb_host_msc and fatfs are REQUIRED BY NAME and not listed in
# idf_component.yml: the player vendors both (usb_host_msc with fixes the
# registry does not have, fatfs patched for exFAT), and a project's own
# components/ wins over anything a manifest would pull. An application
# without its own copies adds espressif/usb_host_msc to its manifest; IDF
# carries fatfs. See README.md.
#
# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/storage.c" "src/storage_io.c" "src/storage_list.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_timer fatfs sdmmc esp_driver_sdmmc usb usb_host_msc
)

# 3004: usb_host_msc is required by name. The one beside this component
# in feckless-libs has the Tab5's fixes; say so when another is in use
# (the registry's, or a project's own copy), rather than fail -- it
# builds, it only does worse on the Tab5.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(_msc_dir usb_host_msc COMPONENT_DIR)
    set(_msc_url "")
    if(EXISTS "${_msc_dir}/idf_component.yml")
        file(STRINGS "${_msc_dir}/idf_component.yml" _msc_url REGEX "^url:")
    endif()
    if(NOT _msc_url MATCHES "feckless-libs-for-m5tab5")
        message(WARNING "feckless_storage_handler: usb_host_msc is ${_msc_dir}, not "
                "feckless-libs' (components/usb_host_msc), which has the Tab5's "
                "DMA-sized transfers. Name that one in the project's manifest.")
    endif()
endif()
