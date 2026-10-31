# feckless-storage-handler-for-m5tab5
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
    SRCS "src/storage.c" "src/storage_io.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_timer fatfs sdmmc esp_driver_sdmmc usb usb_host_msc
)
