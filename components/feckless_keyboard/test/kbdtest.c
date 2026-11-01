/* SPDX-License-Identifier: MIT
 *
 * feckless_keyboard (8011): the staggered keyboard. The gfx calls are
 * recorded, not drawn, with the real gfx.h's GFX_GLYPH_H, and what was
 * drawn is the reference the taps are checked against:
 *
 *   - the stagger: each row's first key where an ANSI keyboard has it, in
 *     quarters of a key, at Spurious's width and at a landscape one;
 *   - drawing and tapping agree: every pixel of the band, in every state,
 *     taps the key drawn over it (its label, or shift / #+ / del), and the
 *     stagger's gaps tap nothing;
 *   - shift: off, once, lock, off; once is used up by a character and not
 *     by del, #+ or a symbol; lock is capitals only;
 *   - every printable ASCII character but space has a key;
 *   - kbd_edit(), and taps off the band.
 *
 *   make -C components/feckless_keyboard/test
 *   make -C components/feckless_keyboard/test preview   (render.c: PPMs)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "feckless_keyboard.h"

/* ---- recorded gfx ------------------------------------------------------- */

typedef struct { char op; int x, y, w, h, scale; uint16_t col; char s[8]; } call_t;
#define MAXCALLS 256
static call_t calls[MAXCALLS];
static int ncalls;

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (ncalls < MAXCALLS) calls[ncalls++] = (call_t){ 'r', x, y, w, h, 0, c, "" };
}

void gfx_draw_text(int x, int y, const char *s, int scale, int max_w, uint16_t c)
{
    (void)max_w;
    if (ncalls < MAXCALLS) {
        call_t k = { 't', x, y, 0, 0, scale, c, "" };
        snprintf(k.s, sizeof k.s, "%s", s);
        calls[ncalls++] = k;
    }
}

int gfx_text_w(const char *s, int scale)
{
    return (int)strlen(s) * GFX_GLYPH_W(scale);
}

/* ---- the drawn keys ----------------------------------------------------- */

/* A key as kbd_draw() drew it: its rectangle (gfx_fill_rect, inset two
 * pixels each side) widened back to its span, and the label after it. */
typedef struct { int x0, x1, y0, y1; uint16_t bg; char label[8]; } drawn_t;
static drawn_t keys[64];
static int nkeys;

static void draw(const kbd_t *k)
{
    ncalls = 0;
    kbd_draw(k);
    nkeys = 0;
    for (int i = 0; i + 1 < ncalls; i += 2) {
        if (calls[i].op != 'r' || calls[i + 1].op != 't') { nkeys = -1; return; }
        drawn_t *d = &keys[nkeys++];
        d->x0 = calls[i].x - 2; d->x1 = calls[i].x + calls[i].w + 2;
        d->y0 = calls[i].y - 2; d->y1 = calls[i].y + calls[i].h + 2;
        d->bg = calls[i].col;
        snprintf(d->label, sizeof d->label, "%s", calls[i + 1].s);
    }
}

/* What a tap on a drawn key should give. */
static int expect(const drawn_t *d)
{
    if (!strcmp(d->label, "shift") || !strcmp(d->label, "SHIFT") ||
        !strcmp(d->label, "#+") || !strcmp(d->label, "ab")) return KBD_STATE;
    if (!strcmp(d->label, "del")) return KBD_DEL;
    return (unsigned char)d->label[0];
}

static const drawn_t *drawn_at(int x, int y)
{
    for (int i = 0; i < nkeys; i++)
        if (x >= keys[i].x0 && x < keys[i].x1 && y >= keys[i].y0 && y < keys[i].y1) return &keys[i];
    return NULL;
}

static const drawn_t *drawn_label(const char *label)
{
    for (int i = 0; i < nkeys; i++)
        if (!strcmp(keys[i].label, label)) return &keys[i];
    return NULL;
}

/* ---- tests -------------------------------------------------------------- */

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); printf("\n"); } } while (0)

#define C_KEY  0x31C9
#define C_TEXT 0xEF7D
#define C_ON   0x7A05

/* Spurious's: 720 wide, 88 px rows, labels at scale 3. */
static kbd_t spurious(void)
{
    return (kbd_t){ .x = 0, .y = 840, .w = 720, .key_h = 88, .scale = 3,
                    .key = C_KEY, .text = C_TEXT, .on = C_ON };
}

/* A tap on a copy, so probing does not change the keyboard. */
static int hit(const kbd_t *k, int x, int y)
{
    kbd_t c = *k;
    return kbd_hit(&c, x, y);
}

static void test_stagger(int x, int w)
{
    kbd_t k = spurious();
    k.x = x;
    k.w = w;
    draw(&k);
    CHECK(nkeys == 43, "%d keys drawn at width %d", nkeys, w);
    /* first key of each row, in quarters of a key, as ANSI has them */
    static const struct { const char *label; int q; } first[] = {
        { "1", 0 }, { "q", 2 }, { "#+", 0 }, { "a", 3 }, { "shift", 0 }, { "z", 5 }, { "del", 41 },
    };
    for (unsigned i = 0; i < sizeof first / sizeof first[0]; i++) {
        const drawn_t *d = drawn_label(first[i].label);
        int want = x + (int)(((long)first[i].q * w) / 44);
        CHECK(d && d->x0 == want, "width %d: \"%s\" at %d, want %d", w, first[i].label, d ? d->x0 : -1, want);
    }
    /* q is half a key right of 1, a a quarter right of q, z half right of a */
    const drawn_t *one = drawn_label("1"), *q = drawn_label("q"), *a = drawn_label("a"), *z = drawn_label("z");
    int u = w / KBD_UNITS;
    CHECK(one && q && a && z, "the four rows' first letters");
    if (one && q && a && z) {
        CHECK(abs((q->x0 - one->x0) - u / 2) <= 1, "q - 1 = %d, half a key %d", q->x0 - one->x0, u / 2);
        CHECK(abs((a->x0 - q->x0) - u / 4) <= 1, "a - q = %d, a quarter %d", a->x0 - q->x0, u / 4);
        CHECK(abs((z->x0 - a->x0) - u / 2) <= 1, "z - a = %d, half a key %d", z->x0 - a->x0, u / 2);
    }
    /* no key past the band, none overlapping in its row */
    for (int i = 0; i < nkeys; i++) {
        CHECK(keys[i].x0 >= x && keys[i].x1 <= x + w, "\"%s\" outside the band", keys[i].label);
        for (int j = i + 1; j < nkeys; j++)
            if (keys[i].y0 == keys[j].y0)
                CHECK(keys[i].x1 <= keys[j].x0 || keys[j].x1 <= keys[i].x0,
                      "\"%s\" and \"%s\" overlap", keys[i].label, keys[j].label);
    }
}

/* Every pixel of the band, in each state: the drawn key's tap, or nothing. */
static void test_every_pixel(void)
{
    static const struct { uint8_t shift; bool sym; } st[] = {
        { KBD_SHIFT_OFF, false }, { KBD_SHIFT_ONCE, false }, { KBD_SHIFT_LOCK, false }, { KBD_SHIFT_OFF, true },
    };
    long taps = 0, gaps = 0;
    for (unsigned s = 0; s < 4; s++) {
        kbd_t k = spurious();
        k.shift = st[s].shift;
        k.symbols = st[s].sym;
        draw(&k);
        for (int y = k.y; y < k.y + kbd_h(&k); y++)
            for (int x = k.x; x < k.x + k.w; x++) {
                const drawn_t *d = drawn_at(x, y);
                int want = d ? expect(d) : KBD_NONE;
                int got = hit(&k, x, y);
                if (got != want) {
                    CHECK(0, "state %u, (%d,%d): tap %d, drawn \"%s\"", s, x, y, got, d ? d->label : "(gap)");
                    return;
                }
                taps++;
                gaps += !d;
            }
    }
    printf("  %ld taps the same as the keys drawn under them (%ld in the stagger's gaps)\n", taps, gaps);
}

static void test_shift(void)
{
    kbd_t k = spurious();
    draw(&k);
    const drawn_t *sh = drawn_label("shift"), *qk = drawn_label("q"), *one = drawn_label("1"),
                  *del = drawn_label("del"), *sym = drawn_label("#+");
    CHECK(sh && qk && one && del && sym, "keys found");
    if (!(sh && qk && one && del && sym)) return;
    int shx = sh->x0 + 5, shy = sh->y0 + 5, qx = qk->x0 + 5, qy = qk->y0 + 5;
    int ox = one->x0 + 5, oy = one->y0 + 5, dx = del->x0 + 5, dy = del->y0 + 5;
    int yx = sym->x0 + 5, yy = sym->y0 + 5;

    CHECK(kbd_hit(&k, qx, qy) == 'q', "q");
    CHECK(kbd_hit(&k, shx, shy) == KBD_STATE && k.shift == KBD_SHIFT_ONCE, "shift once");
    draw(&k);
    CHECK(drawn_label("Q") && drawn_label("!") && drawn_label("\"") && drawn_label("<"), "once: labels shifted");
    CHECK(drawn_label("shift") && drawn_label("shift")->bg == C_ON, "once: shift lit");
    CHECK(kbd_hit(&k, dx, dy) == KBD_DEL && k.shift == KBD_SHIFT_ONCE, "del keeps a shift for one key");
    CHECK(kbd_hit(&k, ox, oy) == '!' && k.shift == KBD_SHIFT_OFF, "once: 1 is ! and the shift is used");
    CHECK(kbd_hit(&k, qx, qy) == 'q', "then q again");

    kbd_hit(&k, shx, shy);
    CHECK(kbd_hit(&k, shx, shy) == KBD_STATE && k.shift == KBD_SHIFT_LOCK, "shift twice locks");
    draw(&k);
    CHECK(drawn_label("SHIFT") && drawn_label("Q") && drawn_label("1") && drawn_label("'"),
          "lock: SHIFT, capitals, digits and ' as they are");
    CHECK(kbd_hit(&k, qx, qy) == 'Q' && kbd_hit(&k, qx, qy) == 'Q', "lock stays: Q Q");
    CHECK(kbd_hit(&k, ox, oy) == '1', "lock is capitals only: 1");
    CHECK(kbd_hit(&k, shx, shy) == KBD_STATE && k.shift == KBD_SHIFT_OFF, "a third time, off");

    CHECK(kbd_hit(&k, yx, yy) == KBD_STATE && k.symbols, "#+ on");
    draw(&k);
    CHECK(drawn_label("ab") && drawn_label("ab")->bg == C_ON && drawn_label("=") && drawn_label("`"),
          "symbol page: ab lit, = and `");
    CHECK(kbd_hit(&k, qx, qy) == '=', "q's place is =");
    kbd_hit(&k, shx, shy);
    CHECK(kbd_hit(&k, qx, qy) == '=' && k.shift == KBD_SHIFT_ONCE, "symbols ignore a shift, and keep it");
    CHECK(kbd_hit(&k, ox, oy) == '1', "the symbol page's number row is digits");
    yx = drawn_label("ab")->x0 + 5;
    CHECK(kbd_hit(&k, yx, yy) == KBD_STATE && !k.symbols, "ab: back");
    CHECK(kbd_hit(&k, qx, qy) == 'Q', "and the kept shift is for that");
}

/* Every printable ASCII character but space, somewhere. */
static void test_coverage(void)
{
    bool have[128] = { false };
    static const struct { uint8_t shift; bool sym; } st[] = {
        { KBD_SHIFT_OFF, false }, { KBD_SHIFT_ONCE, false }, { KBD_SHIFT_LOCK, false }, { KBD_SHIFT_OFF, true },
    };
    for (unsigned s = 0; s < 4; s++) {
        kbd_t k = spurious();
        k.shift = st[s].shift;
        k.symbols = st[s].sym;
        draw(&k);
        for (int i = 0; i < nkeys; i++) {
            int c = hit(&k, (keys[i].x0 + keys[i].x1) / 2, (keys[i].y0 + keys[i].y1) / 2);
            if (c > 0 && c < 128) have[c] = true;
        }
    }
    int missing = 0;
    for (int c = 0x21; c < 0x7f; c++)
        if (!have[c]) { printf("  no key for '%c'\n", c); missing++; }
    CHECK(!missing, "%d printable characters have no key", missing);
}

static void test_outside(void)
{
    kbd_t k = spurious();
    CHECK(hit(&k, 360, k.y - 1) == KBD_NONE, "above the band");
    CHECK(hit(&k, 360, k.y + kbd_h(&k)) == KBD_NONE, "below the band");
    CHECK(hit(&k, -1, k.y + 10) == KBD_NONE, "left of the band");
    CHECK(hit(&k, k.w, k.y + 10) == KBD_NONE, "right of the band");
    CHECK(kbd_h(&k) == 4 * 88, "kbd_h %d", kbd_h(&k));
    kbd_t narrow = k;
    narrow.w = 40;  /* narrower than a quarter-key grid */
    CHECK(hit(&narrow, 1, k.y + 1) == KBD_NONE, "a band too narrow for keys");
    kbd_t s1 = k;
    s1.scale = 1;   /* the modifiers' smaller labels stop at 1 */
    draw(&s1);
    for (int i = 0; i < ncalls; i++)
        if (calls[i].op == 't') CHECK(calls[i].scale >= 1, "label scale %d", calls[i].scale);
}

static void test_edit(void)
{
    char t[4] = "";
    kbd_edit(t, sizeof t, KBD_DEL);
    CHECK(!strcmp(t, ""), "del on nothing");
    kbd_edit(t, sizeof t, 'a');
    kbd_edit(t, sizeof t, KBD_STATE);
    kbd_edit(t, sizeof t, 'b');
    kbd_edit(t, sizeof t, 'c');
    CHECK(!strcmp(t, "abc"), "three in four bytes, KBD_STATE nothing: \"%s\"", t);
    kbd_edit(t, sizeof t, 'd');
    CHECK(!strcmp(t, "abc"), "a fourth does not fit: \"%s\"", t);
    kbd_edit(t, sizeof t, KBD_NONE);
    kbd_edit(t, sizeof t, KBD_DEL);
    CHECK(!strcmp(t, "ab"), "del: \"%s\"", t);
}

int main(void)
{
    test_stagger(0, 720);       /* Spurious, portrait */
    test_stagger(100, 1280);    /* landscape, moved */
    test_stagger(0, 725);       /* not a multiple of 44 */
    test_every_pixel();
    test_shift();
    test_coverage();
    test_outside();
    test_edit();
    if (failures) {
        printf("kbdtest: %d failed\n", failures);
        return 1;
    }
    printf("kbdtest: all passed\n");
    return 0;
}
