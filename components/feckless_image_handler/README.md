# feckless_image_handler

Pictures for the Tab5's apps, from their bytes. Moved out of Defeatist
(its `artformat.h`, `icoimg.[ch]` and the integer-scale rule in
`albumart.c`) so that an app asks what a picture is the same way every
other app does. Decoding and drawing follow in later patches; this is
the part with no ESP-IDF in it.

```c
#include "feckless_image.h"

img_format_t f = img_sniff(buf, len);            /* IMG_JPEG, IMG_PNG, IMG_JXL, IMG_GIF, IMG_NONE */
const char *mime = img_mime(f);

uint8_t sof; uint32_t w, h;
bool baseline = img_jpeg_sof(buf, len, &sof, &w, &h);   /* progressive: false, sof = 0xC2 */

int k = img_int_scale(w, h, box_w, box_h);       /* 0, or the whole-number scale for small art */

uint8_t *png; size_t png_len; img_ico_info_t info;
if (img_ico_is_ico(buf, len) && img_ico_to_png(buf, len, &png, &png_len, &info)) { /* ... free(png) */ }
```

Magic bytes decide, never a MIME type or a file name. `img_ico_to_png()`
takes the largest entry of a `.ico`: a PNG entry is copied out, a DIB is
converted to RGBA and written as a PNG whose deflate stream is stored
blocks (a 256x256 icon is 257 KB of PSRAM for the moment it takes to
draw). Every offset and size is checked against the bytes in hand;
images over `IMG_ICO_MAX_DIM` a side are refused.

## Test

    make -C components/feckless_image_handler/test

runs the header and `image_ico.c` as they are, under ASan and UBSan.
