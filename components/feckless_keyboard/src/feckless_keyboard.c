/* SPDX-License-Identifier: MIT
 *
 * See feckless_keyboard.h. Each key is a span of the band in quarters of
 * a key (QUARTERS across), so the stagger is exact at any width; drawing
 * and hit-testing both go through key_x(), so they cannot disagree.
 */
#include "feckless_keyboard.h"

#include <string.h>

#include "gfx.h"

#define QUARTERS    (KBD_UNITS * 4)

enum { K_CHAR, K_SHIFT, K_SYM, K_DEL };

typedef struct {
    uint8_t q0, q1;         /* from, to, in quarters of a key */
    uint8_t kind;
    char c;                 /* typed */
    char sc;                /* with shift */
    char yc;                /* on the symbol page */
} kkey_t;

#define ROW0_N  11
#define ROW1_N  10
#define ROW2_N  11
#define ROW3_N  11

/* The number row: from the left edge. Its shifted symbols are a US
 * keyboard's; the symbol page leaves it as it is. */
static const kkey_t row0[ROW0_N] = {
    {  0,  4, K_CHAR, '1', '!', '1' }, {  4,  8, K_CHAR, '2', '@', '2' },
    {  8, 12, K_CHAR, '3', '#', '3' }, { 12, 16, K_CHAR, '4', '$', '4' },
    { 16, 20, K_CHAR, '5', '%', '5' }, { 20, 24, K_CHAR, '6', '^', '6' },
    { 24, 28, K_CHAR, '7', '&', '7' }, { 28, 32, K_CHAR, '8', '*', '8' },
    { 32, 36, K_CHAR, '9', '(', '9' }, { 36, 40, K_CHAR, '0', ')', '0' },
    { 40, 44, K_CHAR, '-', '_', '-' },
};
/* q to p, half a key in. */
static const kkey_t row1[ROW1_N] = {
    {  2,  6, K_CHAR, 'q', 'Q', '=' }, {  6, 10, K_CHAR, 'w', 'W', '+' },
    { 10, 14, K_CHAR, 'e', 'E', '[' }, { 14, 18, K_CHAR, 'r', 'R', ']' },
    { 18, 22, K_CHAR, 't', 'T', '{' }, { 22, 26, K_CHAR, 'y', 'Y', '}' },
    { 26, 30, K_CHAR, 'u', 'U', '\\' }, { 30, 34, K_CHAR, 'i', 'I', '|' },
    { 34, 38, K_CHAR, 'o', 'O', ';' }, { 38, 42, K_CHAR, 'p', 'P', ':' },
};
/* a to l, a quarter further; #+ in the stagger's space before it. */
static const kkey_t row2[ROW2_N] = {
    {  0,  3, K_SYM, 0, 0, 0 },
    {  3,  7, K_CHAR, 'a', 'A', '`' }, {  7, 11, K_CHAR, 's', 'S', '~' },
    { 11, 15, K_CHAR, 'd', 'D', '/' }, { 15, 19, K_CHAR, 'f', 'F', '?' },
    { 19, 23, K_CHAR, 'g', 'G', '@' }, { 23, 27, K_CHAR, 'h', 'H', '#' },
    { 27, 31, K_CHAR, 'j', 'J', '$' }, { 31, 35, K_CHAR, 'k', 'K', '%' },
    { 35, 39, K_CHAR, 'l', 'L', '&' }, { 39, 43, K_CHAR, '\'', '"', '\'' },
};
/* z to m, half a key further; shift in the space before it, del after. */
static const kkey_t row3[ROW3_N] = {
    {  0,  5, K_SHIFT, 0, 0, 0 },
    {  5,  9, K_CHAR, 'z', 'Z', '!' }, {  9, 13, K_CHAR, 'x', 'X', '*' },
    { 13, 17, K_CHAR, 'c', 'C', '(' }, { 17, 21, K_CHAR, 'v', 'V', ')' },
    { 21, 25, K_CHAR, 'b', 'B', '_' }, { 25, 29, K_CHAR, 'n', 'N', '<' },
    { 29, 33, K_CHAR, 'm', 'M', '>' }, { 33, 37, K_CHAR, ',', '<', ',' },
    { 37, 41, K_CHAR, '.', '>', '.' }, { 41, 44, K_DEL, 0, 0, 0 },
};

static const struct { const kkey_t *k; int n; } rows[KBD_ROWS] = {
    { row0, ROW0_N }, { row1, ROW1_N }, { row2, ROW2_N }, { row3, ROW3_N },
};

static int key_x(const kbd_t *k, int q)
{
    return k->x + (int)(((long)q * k->w) / QUARTERS);
}

static bool is_letter(char c)
{
    return c >= 'a' && c <= 'z';
}

/* What a key types now. */
static char typed(const kbd_t *k, const kkey_t *e)
{
    if (k->symbols) return e->yc;
    if (k->shift == KBD_SHIFT_ONCE) return e->sc;
    if (k->shift == KBD_SHIFT_LOCK && is_letter(e->c)) return e->sc;
    return e->c;
}

static void draw_key(const kbd_t *k, const kkey_t *e, int y)
{
    int x0 = key_x(k, e->q0), x1 = key_x(k, e->q1), w = x1 - x0, h = k->key_h;
    char buf[2] = { 0, 0 };
    const char *label = buf;
    uint16_t bg = k->key;
    int scale = k->scale;
    switch (e->kind) {
    case K_CHAR:
        buf[0] = typed(k, e);
        break;
    case K_SHIFT:
        label = k->shift == KBD_SHIFT_LOCK ? "SHIFT" : "shift";
        if (k->shift != KBD_SHIFT_OFF) bg = k->on;
        scale = scale > 1 ? scale - 1 : 1;
        break;
    case K_SYM:
        label = k->symbols ? "ab" : "#+";
        if (k->symbols) bg = k->on;
        scale = scale > 1 ? scale - 1 : 1;
        break;
    case K_DEL:
        label = "del";
        scale = scale > 1 ? scale - 1 : 1;
        break;
    }
    gfx_fill_rect(x0 + 2, y + 2, w - 4, h - 4, bg);
    int tw = gfx_text_w(label, scale);
    gfx_draw_text(x0 + (w - tw) / 2, y + (h - GFX_GLYPH_H(scale)) / 2, label, scale, w,
                  k->text);
}

int kbd_h(const kbd_t *k)
{
    return KBD_ROWS * k->key_h;
}

void kbd_draw(const kbd_t *k)
{
    for (int r = 0; r < KBD_ROWS; r++)
        for (int i = 0; i < rows[r].n; i++)
            draw_key(k, &rows[r].k[i], k->y + r * k->key_h);
}

int kbd_hit(kbd_t *k, int tx, int ty)
{
    if (k->w < QUARTERS || k->key_h <= 0) return KBD_NONE;
    if (ty < k->y || ty >= k->y + kbd_h(k)) return KBD_NONE;
    int r = (ty - k->y) / k->key_h;
    for (int i = 0; i < rows[r].n; i++) {
        const kkey_t *e = &rows[r].k[i];
        if (tx < key_x(k, e->q0) || tx >= key_x(k, e->q1)) continue;
        switch (e->kind) {
        case K_SHIFT:
            k->shift = k->shift == KBD_SHIFT_OFF  ? KBD_SHIFT_ONCE
                     : k->shift == KBD_SHIFT_ONCE ? KBD_SHIFT_LOCK
                                                  : KBD_SHIFT_OFF;
            return KBD_STATE;
        case K_SYM:
            k->symbols = !k->symbols;
            return KBD_STATE;
        case K_DEL:
            return KBD_DEL;
        default: {
            char c = typed(k, e);
            if (k->shift == KBD_SHIFT_ONCE && !k->symbols) k->shift = KBD_SHIFT_OFF;
            return (unsigned char)c;
        }
        }
    }
    return KBD_NONE;
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
