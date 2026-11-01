/* SPDX-License-Identifier: MIT
 *
 * feckless_keyboard (8010): an on-screen keyboard over gfx, for the Tab5's
 * apps. Spurious's (spurious-stardict-reader-for-m5tab5, main.c), lifted
 * as it was: three rows of ten keys -- q to p; a to l and an apostrophe;
 * z to m, '-', '.' and del -- lower case only, each key a rectangle with
 * its label centred, drawn with gfx_fill_rect() and gfx_draw_text().
 *
 * It owns no hardware and no task. The app's own UI code draws it into a
 * band of the framebuffer, blits as it blits everything else, and hands it
 * the taps it reads from touch: whatever writes the screen stays the one
 * thing that does. Anything else on the screen -- Spurious's row of Up,
 * Down, space, sounds and clear under it -- is the app's.
 *
 * Plain code, so it links into an app's ELF (ELF_COMPONENTS); the gfx
 * calls it makes are the ABI's.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define KBD_ROWS    3
#define KBD_COLS    10

/* What kbd_hit() returns besides a character. */
#define KBD_NONE    0       /* not on a key */
#define KBD_DEL     (-1)    /* del */

/* Where and how it is drawn. The band is w wide and KBD_ROWS * key_h
 * high from (x, y); each key is w / KBD_COLS wide. */
typedef struct {
    int x, y, w;
    int key_h;
    int scale;              /* gfx text scale of the labels */
    uint16_t key;           /* a key's colour */
    uint16_t text;          /* a label's colour */
} kbd_t;

/* The band's height. */
int kbd_h(const kbd_t *k);

/* Every key, into the framebuffer; the caller blits. */
void kbd_draw(const kbd_t *k);

/* The key under (tx, ty): its character, KBD_DEL, or KBD_NONE if that
 * is outside the band. A tap right of the tenth column, in the pixels
 * w / KBD_COLS leaves over, is the tenth column's key. */
int kbd_hit(const kbd_t *k, int tx, int ty);

/* A key applied to text of size bytes: KBD_DEL drops the last character,
 * a character is appended if one more fits with its terminator, KBD_NONE
 * does nothing. */
void kbd_edit(char *text, size_t size, int key);
