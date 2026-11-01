/* SPDX-License-Identifier: MIT
 *
 * See feckless_keyboard.h. Spurious's draw_keyboard(), key() and the
 * keyboard half of its tap handling, with the band's position and size,
 * the label scale and the colours made arguments.
 */
#include "feckless_keyboard.h"

#include <string.h>

#include "gfx.h"

static const char *const kb[KBD_ROWS][KBD_COLS] = {
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "'" },
    { "z", "x", "c", "v", "b", "n", "m", "-", ".", "del" },
};

static void key(const kbd_t *k, int x, int y, int w, int h, const char *label)
{
    gfx_fill_rect(x + 2, y + 2, w - 4, h - 4, k->key);
    int tw = gfx_text_w(label, k->scale);
    gfx_draw_text(x + (w - tw) / 2, y + (h - GFX_GLYPH_H(k->scale)) / 2, label,
                  k->scale, w, k->text);
}

int kbd_h(const kbd_t *k)
{
    return KBD_ROWS * k->key_h;
}

void kbd_draw(const kbd_t *k)
{
    int kw = k->w / KBD_COLS;
    for (int r = 0; r < KBD_ROWS; r++)
        for (int c = 0; c < KBD_COLS; c++)
            key(k, k->x + c * kw, k->y + r * k->key_h, kw, k->key_h, kb[r][c]);
}

int kbd_hit(const kbd_t *k, int tx, int ty)
{
    int kw = k->w / KBD_COLS;
    if (kw <= 0 || k->key_h <= 0) return KBD_NONE;
    if (tx < k->x || tx >= k->x + k->w || ty < k->y || ty >= k->y + kbd_h(k))
        return KBD_NONE;
    int c = (tx - k->x) / kw;
    const char *s = kb[(ty - k->y) / k->key_h][c < KBD_COLS ? c : KBD_COLS - 1];
    if (!strcmp(s, "del")) return KBD_DEL;
    return (unsigned char)s[0];
}

void kbd_edit(char *text, size_t size, int key)
{
    size_t l = strlen(text);
    if (key == KBD_DEL) {
        if (l) text[l - 1] = 0;
    } else if (key > 0 && l + 1 < size) {
        text[l] = (char)key;
        text[l + 1] = 0;
    }
}
