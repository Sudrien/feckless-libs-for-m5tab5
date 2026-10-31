# SPDX-License-Identifier: MIT
# feckless_image_handler: pictures, from their bytes (7004).
#
# Only the pure half so far: what kind of picture, how big, how to fit
# it, and a favicon as a PNG. The one IDF call is the allocator, and
# it is heap's.
idf_component_register(
    SRCS "src/image_ico.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES heap
)
