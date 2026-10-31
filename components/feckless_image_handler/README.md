# feckless_image_handler

Pictures for the Tab5's apps, from their bytes. Moved out of Defeatist
(its `artformat.h`, `icoimg.[ch]` and the integer-scale rule in
`albumart.c`) so that an app asks what a picture is the same way every
other app does. The software decoders came with 7005; the hardware JPEG engine, PNG
and drawing follow in later patches.

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

## Decoding (7005)

```c
#include "feckless_image_stb.h"     /* JPEG and GIF, through stb_image */
#include "feckless_image_jxl.h"     /* JPEG XL, through J40 */

uint16_t *px; size_t size; int w, h; const char *why;
img_stb_err_t e = img_stb_decode_rgb565(buf, len, box_w, box_h, &px, &size, &w, &h, &why);
img_jxl_err_t j = img_jxl_decode_rgb565(buf, len, box_w, box_h, bg565, &px, &size, &w, &h, &why);
```

Each reads the header first (`img_stb_info()`, `img_jxl_info()`) and says
what a decode may hold at once (`img_*_peak_bytes()`), so the caller can
check free memory before asking. Output is native RGB565, thinned by
whole steps to the smallest size that still covers the box, malloc'd
(PSRAM on the board) and the caller's to free. Both are the last
resorts, as they were in Defeatist: stb_image decodes SOF0-2 at any
size and sampling but holds a whole progressive picture's coefficients;
J40 is deep (about 11.5 KB of stack on a 64-bit host), so the caller runs
it on a task with a PSRAM stack, and it has limits that
`test/jxltest.c` pins. `stb_image.h` and `j40.h` are not in the
repository: `cmake/vendored.cmake` fetches them at configure time into
`vendor/`, pinned to a commit and checked against a SHA256.

## Test

    make -C components/feckless_image_handler/test

runs the sources as they are, under ASan and UBSan: `imagetest` (the
header and the icon), `stbjpegtest` (JPEG, with a corruption sweep),
`gifimgtest` and `jxltest`. The last three fetch the two headers into
`vendor/` the first time.
