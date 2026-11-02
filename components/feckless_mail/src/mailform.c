/* SPDX-License-Identifier: MIT
 *
 * mailform.c -- see mailform.h.
 *
 * The band, top to bottom, each row row_h high with a gap of row_h / 8:
 *
 *   Server    [smtp.example.com          ]
 *   Port      [465                       ]      (dim: the usual one)
 *   Security  [  TLS  ][  STARTTLS  ]
 *   Login     [me@example.com            ]
 *   Password  [********          ][ Show ]
 *   From      [same as the login         ]
 *   To        [notes@example.com         ]
 *   the note, in red, at the text scale less one
 *   [ Cancel ]                  [ Save ]
 *   the keyboard, four rows       (osk)
 *            [   space   ]        (osk)
 */
#include "mailform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

static const char *const k_label[MAILFORM_ROWS] = {
    "Server", "Port", "Security", "Login", "Password", "From", "To",
};

static int gap(const mailform_t *f) { return f->row_h / 8; }
static int pitch(const mailform_t *f) { return f->row_h + gap(f); }
static int note_scale(const mailform_t *f) { return f->scale > 1 ? f->scale - 1 : 1; }
static int note_h(const mailform_t *f) { return 2 * (GFX_GLYPH_H(note_scale(f)) + 4) + gap(f); }
static int label_w(const mailform_t *f) { return f->w * 26 / 100; }
static int field_x(const mailform_t *f) { return f->x + label_w(f); }
static int field_w(const mailform_t *f) { return f->w - label_w(f); }
static int show_w(const mailform_t *f) { return field_w(f) / 4; }
static int row_y(const mailform_t *f, int r) { return f->y + r * pitch(f); }
static int buttons_y(const mailform_t *f) { return row_y(f, MAILFORM_ROWS) + note_h(f); }
static int kbd_y(const mailform_t *f) { return buttons_y(f) + pitch(f); }
static int space_y(const mailform_t *f) { return kbd_y(f) + KBD_ROWS * f->row_h + gap(f); }
static int button_w(const mailform_t *f) { return (f->w - gap(f)) / 2; }

int mailform_h(const mailform_t *f)
{
    int h = buttons_y(f) + f->row_h - f->y;
    if (f->osk) h = space_y(f) + f->row_h - f->y;
    return h;
}

static bool in(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/* The text a row edits, and how many bytes it holds with its terminator. */
static char *buf(mailform_t *f, int r, size_t *cap)
{
    switch (r) {
    case MAILFORM_HOST: *cap = sizeof f->c.host; return f->c.host;
    case MAILFORM_PORT: *cap = sizeof f->port;   return f->port;
    case MAILFORM_USER: *cap = sizeof f->c.user; return f->c.user;
    case MAILFORM_PASS: *cap = sizeof f->c.pass; return f->c.pass;
    case MAILFORM_FROM: *cap = sizeof f->c.from; return f->c.from;
    case MAILFORM_TO:   *cap = sizeof f->c.to;   return f->c.to;
    default:            *cap = 0;                return NULL;
    }
}

void mailform_begin(mailform_t *f, const mailcfg_t *c)
{
    if (c) f->c = *c;
    else memset(&f->c, 0, sizeof f->c);
    if (f->c.port) snprintf(f->port, sizeof f->port, "%u", (unsigned)f->c.port);
    else f->port[0] = '\0';
    f->reveal = false;
    f->note = NULL;
    f->focus = MAILFORM_HOST;
    /* the first empty one of those a person must fill */
    static const int need[] = { MAILFORM_HOST, MAILFORM_USER, MAILFORM_PASS, MAILFORM_TO };
    for (unsigned i = 0; i < sizeof need / sizeof need[0]; i++) {
        size_t cap;
        const char *s = buf(f, need[i], &cap);
        if (!s[0]) { f->focus = need[i]; break; }
    }
    f->kb = (kbd_t){ .x = f->x, .y = kbd_y(f), .w = f->w, .key_h = f->row_h, .scale = f->scale,
                     .key = f->key, .text = f->text, .on = f->on };
}

void mailform_done(mailform_t *f)
{
    mailcfg_wipe(&f->c);
    memset(f->port, 0, sizeof f->port);
}

static void button(const mailform_t *f, int x, int y, int w, const char *label, uint16_t bg)
{
    gfx_fill_rect(x, y, w, f->row_h, bg);
    const int tw = gfx_text_w(label, f->scale);
    const int th = GFX_GLYPH_H(f->scale);
    gfx_draw_text(x + (tw < w ? (w - tw) / 2 : 4), y + (f->row_h - th) / 2, label, f->scale, w - 8, f->text);
}

/* A field's box, its edge lit when focused, and its text from the end. */
static void box(const mailform_t *f, int y, int w, const char *s, bool masked, bool focus,
                const char *placeholder)
{
    const int x = field_x(f), b = f->row_h >= 48 ? 3 : 2;
    gfx_fill_rect(x, y, w, f->row_h, focus ? f->go : f->edge);
    gfx_fill_rect(x + b, y + b, w - 2 * b, f->row_h - 2 * b, f->field);
    char shown[MAILCFG_ADDR_MAX + 3];
    size_t n = strlen(s);
    if (n > sizeof shown - 2) n = sizeof shown - 2;
    if (masked) memset(shown, '*', n);
    else memcpy(shown, s, n);
    shown[n] = '\0';
    if (focus) { shown[n] = '_'; shown[n + 1] = '\0'; }
    const int tx = x + b + 10, tw = w - 2 * b - 20;
    const int ty = y + (f->row_h - GFX_GLYPH_H(f->scale)) / 2;
    if (!s[0] && placeholder) {
        gfx_draw_text(tx, ty, placeholder, f->scale, tw, f->dim);
        if (focus) gfx_fill_rect(tx, ty + GFX_GLYPH_H(f->scale) - 2, GFX_GLYPH_W(f->scale), 2, f->text);
    } else {
        gfx_draw_text_tail(tx, ty, shown, f->scale, tw, f->text);
    }
}

void mailform_draw(const mailform_t *f)
{
    gfx_fill_rect(f->x, f->y, f->w, mailform_h(f), f->bg);
    const int th = GFX_GLYPH_H(f->scale);
    for (int r = 0; r < MAILFORM_ROWS; r++) {
        const int y = row_y(f, r);
        gfx_draw_text(f->x, y + (f->row_h - th) / 2, k_label[r], f->scale, label_w(f) - 8, f->text);
        const bool focus = f->focus == r;
        switch (r) {
        case MAILFORM_HOST:
            box(f, y, field_w(f), f->c.host, false, focus, "smtp.example.com");
            break;
        case MAILFORM_PORT:
            box(f, y, field_w(f), f->port, false, focus,
                f->c.security == MAILCFG_STARTTLS ? "587" : "465");
            break;
        case MAILFORM_SECURITY: {
            const int bw = (field_w(f) - gap(f)) / 2;
            button(f, field_x(f), y, bw, "TLS", f->c.security == MAILCFG_TLS ? f->on : f->key);
            button(f, field_x(f) + bw + gap(f), y, bw, "STARTTLS",
                   f->c.security == MAILCFG_STARTTLS ? f->on : f->key);
            break;
        }
        case MAILFORM_USER:
            box(f, y, field_w(f), f->c.user, false, focus, "me@example.com");
            break;
        case MAILFORM_PASS:
            box(f, y, field_w(f) - show_w(f) - gap(f), f->c.pass, !f->reveal, focus, "app password");
            button(f, field_x(f) + field_w(f) - show_w(f), y, show_w(f), f->reveal ? "Hide" : "Show",
                   f->reveal ? f->on : f->key);
            break;
        case MAILFORM_FROM:
            box(f, y, field_w(f), f->c.from, false, focus, "same as the login");
            break;
        case MAILFORM_TO:
            box(f, y, field_w(f), f->c.to, false, focus, "notes@example.com");
            break;
        }
    }
    if (f->note) {
        const int ns = note_scale(f);
        gfx_draw_para(f->x, row_y(f, MAILFORM_ROWS), f->note, ns, f->w, GFX_GLYPH_H(ns) + 4, 2, f->bad);
    }
    button(f, f->x, buttons_y(f), button_w(f), "Cancel", f->key);
    button(f, f->x + f->w - button_w(f), buttons_y(f), button_w(f), "Save", f->go);
    if (f->osk) {
        kbd_draw(&f->kb);
        button(f, f->x + f->w / 4, space_y(f), f->w / 2, "space", f->key);
    }
}

/* One character into the focused row, within its size; the port digits only. */
static mailform_result_t type(mailform_t *f, int ch)
{
    size_t cap;
    char *s = buf(f, f->focus, &cap);
    if (!s) return MAILFORM_NONE;
    if (ch == MAILFORM_KEY_DEL) {
        if (!s[0]) return MAILFORM_NONE;
        kbd_edit(s, cap, KBD_DEL);
        return MAILFORM_REDRAW;
    }
    if (ch < 0x20 || ch > 0x7e) return MAILFORM_NONE;
    if (f->focus == MAILFORM_PORT && (ch < '0' || ch > '9')) return MAILFORM_NONE;
    if (strlen(s) + 1 >= cap) return MAILFORM_NONE;
    /* no spaces in a host, a port, a login or an address */
    if (ch == ' ' && f->focus != MAILFORM_PASS) return MAILFORM_NONE;
    kbd_edit(s, cap, ch);
    return MAILFORM_REDRAW;
}

static mailform_result_t save(mailform_t *f)
{
    long port = f->port[0] ? strtol(f->port, NULL, 10) : 0;
    if (port < 0 || port > 65535) {
        f->note = "Port: 1 to 65535, or empty for the usual one.";
        f->focus = MAILFORM_PORT;
        return MAILFORM_REDRAW;
    }
    f->c.port = (uint16_t)port;
    const char *why;
    if (mailcfg_check(&f->c, &why)) { f->note = NULL; return MAILFORM_SAVE; }
    f->note = why;
    /* the field it names */
    for (int r = 0; r < MAILFORM_ROWS; r++)
        if (!strncmp(why, k_label[r], strlen(k_label[r]))) { f->focus = r; break; }
    if (!strncmp(why, "Login", 5)) f->focus = MAILFORM_USER;
    return MAILFORM_REDRAW;
}

static void step(mailform_t *f, int d)
{
    do f->focus = (f->focus + d + MAILFORM_ROWS) % MAILFORM_ROWS;
    while (f->focus == MAILFORM_SECURITY);
}

mailform_result_t mailform_key(mailform_t *f, int key)
{
    switch (key) {
    case MAILFORM_KEY_NEXT:  step(f, 1);  return MAILFORM_REDRAW;
    case MAILFORM_KEY_PREV:  step(f, -1); return MAILFORM_REDRAW;
    case MAILFORM_KEY_ENTER: return save(f);
    case MAILFORM_KEY_ESC:   return MAILFORM_CANCEL;
    default:                 return type(f, key);
    }
}

mailform_result_t mailform_tap(mailform_t *f, int tx, int ty)
{
    if (f->osk) {
        const int k = kbd_hit(&f->kb, tx, ty);
        if (k == KBD_STATE) return MAILFORM_REDRAW;
        if (k == KBD_DEL) return type(f, MAILFORM_KEY_DEL);
        if (k > 0) {
            const mailform_result_t r = type(f, k);
            return r == MAILFORM_NONE ? MAILFORM_REDRAW : r;    /* a shift used up */
        }
        if (in(tx, ty, f->x + f->w / 4, space_y(f), f->w / 2, f->row_h)) return type(f, ' ');
    }
    if (in(tx, ty, f->x, buttons_y(f), button_w(f), f->row_h)) return MAILFORM_CANCEL;
    if (in(tx, ty, f->x + f->w - button_w(f), buttons_y(f), button_w(f), f->row_h)) return save(f);
    for (int r = 0; r < MAILFORM_ROWS; r++) {
        const int y = row_y(f, r);
        if (!in(tx, ty, field_x(f), y, field_w(f), f->row_h)) continue;
        if (r == MAILFORM_SECURITY) {
            const int bw = (field_w(f) - gap(f)) / 2;
            const uint8_t s = tx < field_x(f) + bw ? MAILCFG_TLS : MAILCFG_STARTTLS;
            if (s == f->c.security) return MAILFORM_NONE;
            f->c.security = s;
            return MAILFORM_REDRAW;
        }
        if (r == MAILFORM_PASS && tx >= field_x(f) + field_w(f) - show_w(f)) {
            f->reveal = !f->reveal;
            return MAILFORM_REDRAW;
        }
        if (f->focus == r) return MAILFORM_NONE;
        f->focus = r;
        return MAILFORM_REDRAW;
    }
    return MAILFORM_NONE;
}
