/* SPDX-License-Identifier: MIT
 *
 * `make preview`: feckless_keyboard drawn by the real gfx.c into its real
 * framebuffer (feckless_graphics_handler's test shim), in its four states
 * -- lower case, shift once, shift locked, the symbol page -- at
 * Spurious's size and colours, as kb_<state>.ppm. To look at, after a
 * change to the layout or the labels; kbdtest is the check.
 */
#include <stdio.h>
#include "gfx.h"
#include "feckless_keyboard.h"

#define W 720
#define H 1280
#define KEY_H 88
#define C_BG    RGB(16, 18, 24)
#define C_KEY   RGB(52, 58, 76)
#define C_TEXT  RGB(235, 235, 235)
#define C_ON    RGB(120, 96, 40)

extern void *shim_last_big_alloc;

int main(void)
{
    if (gfx_init(NULL, W, H) != ESP_OK) return 1;
    const uint16_t *fb = shim_last_big_alloc;
    static const struct { const char *name; uint8_t shift; bool sym; } st[] = {
        { "lower", KBD_SHIFT_OFF, false }, { "shift", KBD_SHIFT_ONCE, false },
        { "caps", KBD_SHIFT_LOCK, false }, { "symbols", KBD_SHIFT_OFF, true },
    };
    for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++) {
        kbd_t k = { .x = 0, .y = 0, .w = W, .key_h = KEY_H, .scale = 3, .key = C_KEY,
                    .text = C_TEXT, .on = C_ON, .shift = st[i].shift, .symbols = st[i].sym };
        int h = kbd_h(&k);
        gfx_fill_rect(0, 0, W, h, C_BG);
        kbd_draw(&k);
        char fn[32];
        snprintf(fn, sizeof fn, "kb_%s.ppm", st[i].name);
        FILE *f = fopen(fn, "wb");
        if (!f) return 1;
        fprintf(f, "P6\n%d %d\n255\n", W, h);
        for (int p = 0; p < W * h; p++) {
            uint16_t v = fb[p];
            unsigned char rgb[3] = { (unsigned char)(((v >> 11) & 31) * 255 / 31),
                                     (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                     (unsigned char)((v & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("%s\n", fn);
    }
    return 0;
}
