# SPDX-License-Identifier: MIT
# feckless_nvs_handler: the NVS partition, a value at a time (4003).
#
# nvs_flash is PRIV: the header takes and returns plain types, so an app
# that includes it -- an ELF on a launcher, through feckless_abi_nvs --
# compiles against nothing of NVS's.
idf_component_register(
    SRCS "src/nvs_handler.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash heap
)
