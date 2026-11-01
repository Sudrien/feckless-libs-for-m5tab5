/* SPDX-License-Identifier: MIT
 *
 * feckless_keyboard: an on-screen keyboard over gfx, for the Tab5's apps.
 * 8010 lifted Spurious's three rows of ten as they were; 8011 makes it a
 * keyboard. Four rows, staggered as a real (ANSI) one is, in quarters of a
 * key: the number row from the left edge, q-p half a key in, a-l a
 * quarter further, z-m half a key further again. Eleven keys across.
 *
 *    1  2  3  4  5  6  7  8  9  0  -
 *      q  w  e  r  t  y  u  i  o  p
 *   #+  a  s  d  f  g  h  j  k  l  '
 *   shift z  x  c  v  b  n  m  ,  .  del
 *
 * shift is a phone's: once for the next key (the number row's symbols, a
 * capital, " < >), again to lock -- capitals only, as caps lock is -- and
 * again for off. #+ turns the three letter rows into the symbols the
 * other page has no key for, and back. With both, every printable ASCII
 * character but space has a key; space, enter and the like are the app's.
 *
 * It owns no hardware and no task. The app's own UI code draws it into a
 * band of the framebuffer, blits as it blits everything else, and hands it
 * the taps it reads from touch: whatever writes the screen stays the one
 * thing that does.
 *
 * Plain code, so it links into an app's ELF (ELF_COMPONENTS); the gfx
 * calls it makes are the ABI's.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KBD_ROWS    4
#define KBD_UNITS   11      /* keys across: the band is KBD_UNITS keys wide */

/* What kbd_hit() returns besides a character. */
#define KBD_NONE    0       /* not on a key */
#define KBD_DEL     (-1)    /* del */
#define KBD_STATE   (-2)    /* shift or #+: the keys' labels changed */

/* shift's three states. */
enum { KBD_SHIFT_OFF, KBD_SHIFT_ONCE, KBD_SHIFT_LOCK };

/* Where and how it is drawn, and its state. The band is w wide and
 * KBD_ROWS * key_h high from (x, y); a key is w / KBD_UNITS wide. Zero
 * the state (shift, symbols) to start, and leave it to kbd_hit(). */
typedef struct {
    int x, y, w;
    int key_h;
    int scale;              /* gfx text scale of a character's label */
    uint16_t key;           /* a key's colour */
    uint16_t text;          /* a label's colour */
    uint16_t on;            /* shift or #+ while it is on */
    uint8_t shift;          /* KBD_SHIFT_* */
    bool symbols;           /* the symbol page */
} kbd_t;

/* The band's height. */
int kbd_h(const kbd_t *k);

/* Every key, labelled as the state has them, into the framebuffer; the
 * band's gaps (the stagger's ends) are not drawn. The caller blits. */
void kbd_draw(const kbd_t *k);

/* A tap at (tx, ty): the character it types, KBD_DEL, KBD_STATE for
 * shift or #+, or KBD_NONE off the keys. A character uses up a shift
 * that was for one key. Redraw after anything but KBD_NONE: the labels
 * can have changed. */
int kbd_hit(kbd_t *k, int tx, int ty);

/* A key applied to text of size bytes: KBD_DEL drops the last character,
 * a character is appended if one more fits with its terminator, and
 * KBD_NONE or KBD_STATE does nothing. */
void kbd_edit(char *text, size_t size, int key);
