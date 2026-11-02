/* SPDX-License-Identifier: MIT
 *
 * mailform.h -- the outgoing mail account as a form, drawn with gfx and
 * driven by the app: the fields of a mailcfg_t, a row each, then Cancel
 * and Save; under them, when asked for, feckless_keyboard and a space bar.
 *
 * Like feckless_keyboard it owns no hardware and no task. The app gives
 * it a rectangle, draws it into the framebuffer and blits, and hands it
 * taps (mailform_tap) and, from a USB keyboard, characters
 * (mailform_key). So it fits Avoidant's portrait screen with the
 * on-screen keyboard and Jinxed Writer's landscape one with a real one.
 *
 * It edits a copy; nothing is stored until the app does it on
 * MAILFORM_SAVE (mailcfg_save()), after which the form is done with.
 * mailform_done() clears the copy's password either way.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feckless_keyboard.h"
#include "mailcfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The rows, in order. */
enum {
    MAILFORM_HOST = 0,
    MAILFORM_PORT,
    MAILFORM_SECURITY,      /* a toggle, not typed */
    MAILFORM_USER,
    MAILFORM_PASS,
    MAILFORM_FROM,
    MAILFORM_TO,
    MAILFORM_ROWS,
};

/* What a tap or a key did. */
typedef enum {
    MAILFORM_NONE = 0,      /* nothing: no need to redraw */
    MAILFORM_REDRAW,        /* something changed: draw and blit */
    MAILFORM_CANCEL,        /* Cancel: leave without saving */
    MAILFORM_SAVE,          /* Save, and the account passed mailcfg_check() */
} mailform_result_t;

/* Keys besides characters for mailform_key(): a USB keyboard's. */
#define MAILFORM_KEY_DEL    (-1)    /* KBD_DEL, backspace */
#define MAILFORM_KEY_NEXT   (-10)   /* Tab or Down: the next row */
#define MAILFORM_KEY_PREV   (-11)   /* shift-Tab or Up */
#define MAILFORM_KEY_ENTER  (-12)   /* Enter: Save */
#define MAILFORM_KEY_ESC    (-13)   /* Escape: Cancel */

typedef struct {
    /* where: the band is w wide and mailform_h() high from (x, y) */
    int x, y, w;
    int row_h;              /* one row; the buttons and keyboard keys too */
    int scale;              /* gfx text scale */
    bool osk;               /* the on-screen keyboard and a space bar under the buttons */
    /* colours */
    uint16_t bg, field, key, text, dim, edge, go, bad, on;

    /* the copy being edited, and the form's state: set by mailform_begin() */
    mailcfg_t c;
    char port[6];           /* typed; "" is the security's usual port */
    int focus;              /* a MAILFORM_* row */
    bool reveal;            /* the password shown */
    const char *note;       /* mailcfg_check()'s complaint after a Save, or NULL */
    kbd_t kb;
} mailform_t;

/* Start from c (a loaded account, or NULL for an empty one), focus on
 * the first empty field. Set the geometry and colours first. */
void mailform_begin(mailform_t *f, const mailcfg_t *c);

/* The band's height: the rows, a line for the note, the buttons, and
 * with osk the keyboard and the space bar. */
int mailform_h(const mailform_t *f);

/* Everything into the framebuffer, the band's background first. */
void mailform_draw(const mailform_t *f);

/* A tap at (tx, ty). */
mailform_result_t mailform_tap(mailform_t *f, int tx, int ty);

/* A character (printable ASCII) or a MAILFORM_KEY_*. */
mailform_result_t mailform_key(mailform_t *f, int key);

/* Clear the copy, password and all. */
void mailform_done(mailform_t *f);

#ifdef __cplusplus
}
#endif
