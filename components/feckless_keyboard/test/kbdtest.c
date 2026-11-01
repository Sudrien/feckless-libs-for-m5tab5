/* SPDX-License-Identifier: MIT
 *
 * feckless_keyboard against the code it was lifted from. ORIGINAL below is
 * Spurious's (spurious-stardict-reader-for-m5tab5 at daa69a6, main.c): the
 * kb[] table, key(), draw_keyboard()'s three rows and the keyboard branch
 * of search()'s tap handling, copied as they are but for the band's origin
 * which was the screen's left edge and KB_Y. The gfx calls are recorded,
 * not drawn, with the real gfx.h's GFX_GLYPH_H.
 *
 *   make -C components/feckless_keyboard/test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "feckless_keyboard.h"

/* ---- recorded gfx ------------------------------------------------------- */

typedef struct { char op; int a, b, c, d, e; uint16_t col; char s[8]; } call_t;
static call_t calls[256];
static int ncalls;

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (ncalls < 256) calls[ncalls++] = (call_t){ 'r', x, y, w, h, 0, c, "" };
}

void gfx_draw_text(int x, int y, const char *s, int scale, int max_w, uint16_t c)
{
    if (ncalls < 256) {
        call_t k = { 't', x, y, scale, max_w, 0, c, "" };
        snprintf(k.s, sizeof k.s, "%s", s);
        calls[ncalls++] = k;
    }
}

int gfx_text_w(const char *s, int scale)
{
    return (int)strlen(s) * GFX_GLYPH_W(scale);
}

/* ---- ORIGINAL (Spurious daa69a6) ---------------------------------------- */

#define W           720
#define H           1280
#define SCALE       3
#define TEXT_H      GFX_GLYPH_H(SCALE)
#define KEY_H       88
#define KB_Y        (H - 4 * KEY_H)
#define C_TEXT      0xEF7D
#define C_KEY       0x31C9

static const char *const kb[3][10] = {
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "'" },
    { "z", "x", "c", "v", "b", "n", "m", "-", ".", "del" },
};

static void key(int x, int y, int w, int h, const char *label, uint16_t bg)
{
    gfx_fill_rect(x + 2, y + 2, w - 4, h - 4, bg);
    int tw = gfx_text_w(label, SCALE);
    gfx_draw_text(x + (w - tw) / 2, y + (h - TEXT_H) / 2, label, SCALE, w, C_TEXT);
}

/* draw_keyboard()'s three rows; its fourth is Spurious's own. */
static void orig_draw(void)
{
    int kw = W / 10;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 10; c++)
            key(c * kw, KB_Y + r * KEY_H, kw, KEY_H, kb[r][c], C_KEY);
}

/* search()'s keyboard branch: q as it leaves it, for a tap at (x, y)
 * that its own test, y >= KB_Y && y < KB_Y + 3 * KEY_H, has let in. */
static void orig_tap(char *q, size_t qsize, int x, int y)
{
    size_t l = strlen(q);
    int kw = W / 10;
    const char *k = kb[(y - KB_Y) / KEY_H][x / kw < 10 ? x / kw : 9];
    if (!strcmp(k, "del")) { if (l) q[l - 1] = 0; }
    else if (l + 1 < qsize) { q[l] = k[0]; q[l + 1] = 0; }
}

/* ---- tests -------------------------------------------------------------- */

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); printf("\n"); } } while (0)

static const kbd_t spurious = { .x = 0, .y = KB_Y, .w = W, .key_h = KEY_H,
                                .scale = SCALE, .key = C_KEY, .text = C_TEXT };

static void test_draw_same(void)
{
    call_t want[256];
    ncalls = 0;
    orig_draw();
    int nwant = ncalls;
    memcpy(want, calls, sizeof want);
    ncalls = 0;
    kbd_draw(&spurious);
    CHECK(ncalls == nwant, "kbd_draw made %d calls, the original %d", ncalls, nwant);
    CHECK(nwant == 60, "the original made %d calls, not 60", nwant);
    for (int i = 0; i < ncalls && i < nwant; i++)
        CHECK(!memcmp(&calls[i], &want[i], sizeof calls[i]),
              "call %d: %c(%d,%d,%d,%d) \"%s\" against %c(%d,%d,%d,%d) \"%s\"", i,
              calls[i].op, calls[i].a, calls[i].b, calls[i].c, calls[i].d, calls[i].s,
              want[i].op, want[i].a, want[i].b, want[i].c, want[i].d, want[i].s);
    CHECK(kbd_h(&spurious) == 3 * KEY_H, "kbd_h %d", kbd_h(&spurious));
}

/* Every pixel of the band, as a tap on an empty, a short and a full query. */
static void test_every_pixel(void)
{
    static const char *starts[] = { "", "ab", "0123456789012345678901234567890123456789012345678901234567890" };
    long n = 0;
    for (int y = KB_Y; y < KB_Y + 3 * KEY_H; y++)
        for (int x = 0; x < W; x++)
            for (size_t s = 0; s < 3; s++) {
                char a[64], b[64];
                snprintf(a, sizeof a, "%s", starts[s]);
                snprintf(b, sizeof b, "%s", starts[s]);
                orig_tap(a, sizeof a, x, y);
                kbd_edit(b, sizeof b, kbd_hit(&spurious, x, y));
                if (strcmp(a, b)) {
                    CHECK(0, "tap (%d,%d) on \"%s\": original \"%s\", kbd \"%s\"", x, y, starts[s], a, b);
                    return;
                }
                n++;
            }
    printf("  %ld taps the same as the original\n", n);
}

static void test_outside(void)
{
    CHECK(kbd_hit(&spurious, 360, KB_Y - 1) == KBD_NONE, "above the band");
    CHECK(kbd_hit(&spurious, 360, KB_Y + 3 * KEY_H) == KBD_NONE, "below the band (Spurious's fourth row)");
    CHECK(kbd_hit(&spurious, -1, KB_Y + 10) == KBD_NONE, "left of the band");
    CHECK(kbd_hit(&spurious, W, KB_Y + 10) == KBD_NONE, "right of the band");
    kbd_t zero = spurious;
    zero.w = 5;     /* narrower than ten columns */
    CHECK(kbd_hit(&zero, 1, KB_Y + 1) == KBD_NONE, "a band too narrow for keys");
}

/* A band elsewhere, with leftover pixels: w = 725 leaves five past the
 * tenth column, which belong to it as they did to Spurious's. */
static void test_moved_band(void)
{
    kbd_t k = { .x = 100, .y = 40, .w = 725, .key_h = 50, .scale = 2, .key = 1, .text = 2 };
    int kw = 72;
    CHECK(kbd_hit(&k, 100, 40) == 'q', "top left is q");
    CHECK(kbd_hit(&k, 100 + 9 * kw, 40) == 'p', "tenth column is p");
    CHECK(kbd_hit(&k, 100 + 724, 40 + 49) == 'p', "a leftover pixel is the tenth column");
    CHECK(kbd_hit(&k, 100 + 724, 40 + 149) == KBD_DEL, "bottom right is del");
    CHECK(kbd_hit(&k, 100 + 9 * kw, 40 + 50) == '\'', "row two's tenth is the apostrophe");
    CHECK(kbd_hit(&k, 99, 40) == KBD_NONE, "one left of the band");
    ncalls = 0;
    kbd_draw(&k);
    CHECK(ncalls == 60, "%d calls", ncalls);
    CHECK(calls[0].op == 'r' && calls[0].a == 102 && calls[0].b == 42 && calls[0].c == kw - 4 && calls[0].d == 46,
          "first key's rectangle %d,%d %dx%d", calls[0].a, calls[0].b, calls[0].c, calls[0].d);
    CHECK(calls[59].op == 't' && !strcmp(calls[59].s, "del") && calls[59].c == 2,
          "last label \"%s\" at scale %d", calls[59].s, calls[59].c);
}

static void test_edit(void)
{
    char t[4] = "";
    kbd_edit(t, sizeof t, KBD_DEL);
    CHECK(!strcmp(t, ""), "del on nothing");
    kbd_edit(t, sizeof t, 'a');
    kbd_edit(t, sizeof t, 'b');
    kbd_edit(t, sizeof t, 'c');
    CHECK(!strcmp(t, "abc"), "three in four bytes: \"%s\"", t);
    kbd_edit(t, sizeof t, 'd');
    CHECK(!strcmp(t, "abc"), "a fourth does not fit: \"%s\"", t);
    kbd_edit(t, sizeof t, KBD_NONE);
    CHECK(!strcmp(t, "abc"), "KBD_NONE changes nothing: \"%s\"", t);
    kbd_edit(t, sizeof t, KBD_DEL);
    CHECK(!strcmp(t, "ab"), "del: \"%s\"", t);
}

int main(void)
{
    test_draw_same();
    test_every_pixel();
    test_outside();
    test_moved_band();
    test_edit();
    if (failures) {
        printf("kbdtest: %d failed\n", failures);
        return 1;
    }
    printf("kbdtest: all passed\n");
    return 0;
}
