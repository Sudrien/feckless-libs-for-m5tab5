/* SPDX-License-Identifier: MIT
 *
 * feckless_chooser.c -- see feckless_chooser.h (9012).
 */
#include "feckless_chooser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "gfx.h"
#include "storage.h"

/* ---- geometry ----------------------------------------------------------- */

static int path_h(const chooser_t *c) { return c->show_path ? c->row_h : 0; }
static int list_y(const chooser_t *c) { return c->y + path_h(c); }
static int pad(const chooser_t *c) { return GFX_GLYPH_W(c->scale); }
static int text_dy(const chooser_t *c) { return (c->row_h - GFX_GLYPH_H(c->scale)) / 2; }

int chooser_rows(const chooser_t *c)
{
    const int r = c->row_h > 0 ? (c->h - path_h(c)) / c->row_h : 0;
    return r > 0 ? r : 1;
}

/* The scroll bar's strip: a pad wide at the right, when it is needed. */
static bool scrolls(const chooser_t *c) { return c->list.n > chooser_rows(c); }
static int bar_w(const chooser_t *c) { return scrolls(c) ? pad(c) : 0; }

/* ---- the selection ------------------------------------------------------ */

/* Keep cur in the list and in view. */
static void clamp(chooser_t *c)
{
    const int n = c->list.n, rows = chooser_rows(c);
    if (c->cur >= n) c->cur = n - 1;
    if (c->cur < 0) c->cur = 0;
    if (c->top > c->cur) c->top = c->cur;
    if (c->top < c->cur - rows + 1) c->top = c->cur - rows + 1;
    if (c->top > n - rows) c->top = n - rows;
    if (c->top < 0) c->top = 0;
}

const storage_ent_t *chooser_sel(const chooser_t *c)
{
    return c->list.n ? &c->list.ents[c->cur] : NULL;
}

bool chooser_select(chooser_t *c, const char *name)
{
    const int i = storage_list_find(&c->list, name);
    if (i < 0) return false;
    c->cur = i;
    clamp(c);
    return true;
}

/* ---- folders ------------------------------------------------------------ */

static const char *root(const chooser_t *c) { return c->root ? c->root : ""; }

/* Is dir root, or inside it? */
static bool within(const chooser_t *c, const char *dir)
{
    const char *r = root(c);
    const size_t n = strlen(r);
    if (!n) return true;
    return !strncmp(dir, r, n) && (dir[n] == '\0' || dir[n] == '/');
}

static bool at_top(const chooser_t *c) { return !strcmp(c->dir, root(c)); }

/* The folder above c->dir, into c->dir; the name left behind into
 * `from` (NULL to not want it). False at the top. */
static bool parent(chooser_t *c, char *from, size_t n)
{
    if (at_top(c)) return false;
    char *slash = strrchr(c->dir, '/');
    if (from) snprintf(from, n, "%s", slash ? slash + 1 : c->dir);
    if (!slash || slash == c->dir) c->dir[0] = '\0';    /* "/sd" -> the volumes */
    else *slash = '\0';
    if (!within(c, c->dir)) snprintf(c->dir, sizeof c->dir, "%s", root(c));
    return true;
}

/* Read c->dir. At the volumes, the volumes. */
static void load(chooser_t *c)
{
    c->list.max = c->max;
    c->gen = storage_generation();
    if (!c->dir[0]) {
        storage_list_volumes(&c->list);
        c->err = STORAGE_LIST_OK;
    } else {
        c->err = storage_list_read(&c->list, c->dir, c->sort, c->filter, c->ctx);
    }
    if (c->decorate) {
        c->decorate(&c->list, c->dir, c->ctx);
        storage_list_sort(&c->list, c->dir[0] ? c->sort : STORAGE_SORT_NONE);
    }
}

/* Read c->dir, going up while it cannot be read. */
static void read_up(chooser_t *c)
{
    for (;;) {
        load(c);
        if (c->err != STORAGE_LIST_NO_DIR || !parent(c, NULL, 0)) break;
    }
}

void chooser_begin(chooser_t *c, const char *dir)
{
    snprintf(c->dir, sizeof c->dir, "%s", dir && within(c, dir) ? dir : root(c));
    /* "/sd/notes/" and "/sd/notes" are one folder */
    size_t n = strlen(c->dir);
    while (n > 1 && c->dir[n - 1] == '/') c->dir[--n] = '\0';
    c->cur = c->top = 0;
    read_up(c);
    clamp(c);
}

void chooser_reload(chooser_t *c)
{
    char keep[CHOOSER_PATH_MAX] = "";
    const storage_ent_t *e = chooser_sel(c);
    if (e) snprintf(keep, sizeof keep, "%s", e->name);
    const int was = c->cur;
    read_up(c);
    if (!keep[0] || !chooser_select(c, keep)) {
        c->cur = was;       /* gone: the row that took its place */
        clamp(c);
    }
}

chooser_result_t chooser_poll(chooser_t *c)
{
    if (storage_generation() == c->gen) return CHOOSER_NONE;
    chooser_reload(c);
    return CHOOSER_REDRAW;
}

void chooser_done(chooser_t *c)
{
    storage_list_free(&c->list);
}

bool chooser_sel_path(const chooser_t *c, char *out, size_t n)
{
    const storage_ent_t *e = chooser_sel(c);
    if (!e || e->pinned) return false;
    if (!c->dir[0]) {                       /* a volume */
        const int k = snprintf(out, n, "/%s", e->name);
        return k > 0 && (size_t)k < n;
    }
    return storage_join_path(out, n, c->dir, e->name);
}

/* Open the selection: into a folder, or CHOOSER_OPEN. */
static chooser_result_t open_sel(chooser_t *c)
{
    const storage_ent_t *e = chooser_sel(c);
    if (!e) return CHOOSER_NONE;
    if (e->pinned || !e->is_dir) return CHOOSER_OPEN;
    char p[CHOOSER_PATH_MAX];
    if (!chooser_sel_path(c, p, sizeof p)) return CHOOSER_NONE;    /* too deep to name */
    snprintf(c->dir, sizeof c->dir, "%s", p);
    c->cur = c->top = 0;
    read_up(c);
    clamp(c);
    return CHOOSER_REDRAW;
}

static chooser_result_t go_up(chooser_t *c)
{
    char from[CHOOSER_PATH_MAX];
    if (!parent(c, from, sizeof from)) return CHOOSER_NONE;
    c->cur = c->top = 0;
    read_up(c);
    chooser_select(c, from);        /* back on the folder just left */
    clamp(c);
    return CHOOSER_REDRAW;
}

/* ---- input -------------------------------------------------------------- */

static const char *shown(const chooser_t *c, const storage_ent_t *e)
{
    (void)c;
    return e->label ? e->label : e->name;
}

/* The next row after cur whose text starts with ch, round the end. */
static bool jump(chooser_t *c, int ch)
{
    const int n = c->list.n;
    for (int k = 1; k <= n; k++) {
        const int i = (c->cur + k) % n;
        if (tolower((unsigned char)shown(c, &c->list.ents[i])[0]) == tolower(ch)) {
            c->cur = i;
            clamp(c);
            return true;
        }
    }
    return false;
}

static chooser_result_t move(chooser_t *c, int to)
{
    const int was = c->cur, top = c->top;
    c->cur = to;
    clamp(c);
    return c->cur != was || c->top != top ? CHOOSER_REDRAW : CHOOSER_NONE;
}

chooser_result_t chooser_key(chooser_t *c, int key)
{
    const int page = chooser_rows(c) > 1 ? chooser_rows(c) - 1 : 1;
    switch (key) {
    case CHOOSER_KEY_UP:    return move(c, c->cur - 1);
    case CHOOSER_KEY_DOWN:  return move(c, c->cur + 1);
    case CHOOSER_KEY_PGUP:  return move(c, c->cur - page);
    case CHOOSER_KEY_PGDN:  return move(c, c->cur + page);
    case CHOOSER_KEY_HOME:  return move(c, 0);
    case CHOOSER_KEY_END:   return move(c, c->list.n - 1);
    case CHOOSER_KEY_ENTER: return open_sel(c);
    case CHOOSER_KEY_BACK:  return go_up(c);
    case CHOOSER_KEY_ESC:   return CHOOSER_CANCEL;
    default:
        if (key > ' ' && key < 0x7F) return jump(c, key) ? CHOOSER_REDRAW : CHOOSER_NONE;
        return CHOOSER_KEY;
    }
}

chooser_result_t chooser_tap(chooser_t *c, int tx, int ty)
{
    if (tx < c->x || tx >= c->x + c->w || ty < c->y || ty >= c->y + c->h) return CHOOSER_NONE;
    if (ty < list_y(c)) return go_up(c);
    const int rows = chooser_rows(c);
    const int r = (ty - list_y(c)) / c->row_h;
    if (r >= rows) return CHOOSER_NONE;
    if (scrolls(c) && tx >= c->x + c->w - bar_w(c)) {
        /* above the thumb a page up, below it a page down */
        const int track = rows * c->row_h;
        const int thumb_y = list_y(c) + (int)((int64_t)track * c->top / c->list.n);
        const int page = rows > 1 ? rows - 1 : 1;
        if (ty < thumb_y) {
            c->top -= page;
            if (c->top < 0) c->top = 0;
        } else {
            c->top += page;
            if (c->top > c->list.n - rows) c->top = c->list.n - rows;
        }
        /* the selection follows into view, not the other way */
        if (c->cur < c->top) c->cur = c->top;
        if (c->cur > c->top + rows - 1) c->cur = c->top + rows - 1;
        return CHOOSER_REDRAW;
    }
    const int i = c->top + r;
    if (i >= c->list.n) return CHOOSER_NONE;
    c->cur = i;
    return open_sel(c);
}

/* ---- drawing ------------------------------------------------------------ */

static void fmt_size(char *out, size_t n, int64_t b)
{
    if (b < 0) out[0] = '\0';
    else if (b < 1024) snprintf(out, n, "%d B", (int)b);
    else if (b < 1024 * 1024) snprintf(out, n, "%d KB", (int)((b + 1023) / 1024));
    else {
        /* tenths, without a float: 1.5 MB, 12.0 GB */
        const int64_t unit = b < 1024LL * 1024 * 1024 ? 1024LL * 1024 : 1024LL * 1024 * 1024;
        const int64_t t = (b * 10 + unit / 2) / unit;
        snprintf(out, n, "%d.%d %s", (int)(t / 10), (int)(t % 10), unit == 1024LL * 1024 ? "MB" : "GB");
    }
}

static void fmt_time(char *out, size_t n, uint32_t t)
{
    int y, mo, d, h, mi;
    if (!storage_fat_time_parts(t, &y, &mo, &d, &h, &mi)) out[0] = '\0';
    else snprintf(out, n, "%04d-%02d-%02d %02d:%02d", y % 10000, mo % 100, d % 100, h % 100, mi % 100);
}

static void row(const chooser_t *c, int i, int y, int text_w)
{
    const storage_ent_t *e = &c->list.ents[i];
    const bool sel = i == c->cur;
    uint16_t bg = sel ? c->sel : ((i & 1) ? c->alt : c->bg);
    if (bg != c->bg) gfx_fill_rect(c->x, y, text_w, c->row_h, bg);
    gfx_fill_rect(c->x, y + c->row_h - 1, text_w, 1, c->rule);

    uint16_t col = c->colour ? c->colour(e, c->ctx) : 0;
    if (!col) col = e->is_dir ? c->folder : c->text;
    const int ty = y + text_dy(c), p = pad(c);

    /* the right column: as much of size and date as leaves the name
     * at least half the row */
    char sz[24] = "", tm[24] = "", right[56] = "";
    if (c->show_size && !e->is_dir) fmt_size(sz, sizeof sz, e->size);
    if (c->show_time && !e->is_dir) fmt_time(tm, sizeof tm, e->fat_time);
    if (sz[0] && tm[0]) snprintf(right, sizeof right, "%s   %s", sz, tm);
    else snprintf(right, sizeof right, "%s%s", sz, tm);
    int rw = right[0] ? gfx_text_w(right, c->scale) : 0;
    if (rw && rw + 3 * p > text_w / 2 && sz[0] && tm[0]) {
        snprintf(right, sizeof right, "%s", sz);        /* the date goes first */
        rw = gfx_text_w(right, c->scale);
    }
    if (rw && rw + 3 * p > text_w / 2) rw = 0;
    if (rw) gfx_draw_text(c->x + text_w - p - rw, ty, right, c->scale, rw, c->dim);

    /* the name: a folder with its slash, a volume by its label */
    char name[CHOOSER_PATH_MAX + 2];
    if (e->is_dir && !e->label && c->dir[0]) snprintf(name, sizeof name, "%s/", e->name);
    else snprintf(name, sizeof name, "%s", shown(c, e));
    const int nw = text_w - 2 * p - (rw ? rw + 2 * p : 0);
    gfx_draw_text(c->x + p, ty, name, c->scale, nw > 0 ? nw : 0, col);
}

void chooser_draw(const chooser_t *c)
{
    gfx_fill_rect(c->x, c->y, c->w, c->h, c->bg);
    const int p = pad(c);

    if (c->show_path) {
        const char *s = c->dir[0] ? c->dir : (c->top_label ? c->top_label : "microSD and USB");
        gfx_draw_text_tail(c->x + p, c->y + text_dy(c), s, c->scale, c->w - 2 * p, c->dim);
        gfx_fill_rect(c->x, c->y + c->row_h - 1, c->w, 1, c->rule);
    }

    const int rows = chooser_rows(c), y0 = list_y(c), text_w = c->w - bar_w(c);
    int r = 0;
    for (; r < rows && c->top + r < c->list.n; r++) row(c, c->top + r, y0 + r * c->row_h, text_w);

    /* what is wrong, or that there is more than was kept, in the next row */
    const char *note = NULL;
    if (!c->list.n) note = c->empty ? c->empty : "Nothing here.";
    else if (c->err == STORAGE_LIST_IO) note = "Not all of this folder could be read.";
    else if (c->err == STORAGE_LIST_NO_MEM) note = "Not all of this folder fits in memory.";
    else if (c->list.truncated) note = "More here than can be listed.";
    if (note && r < rows && (!c->list.n || c->top + r == c->list.n))
        gfx_draw_text(c->x + p, y0 + r * c->row_h + text_dy(c), note, c->scale, text_w - 2 * p, c->dim);

    if (scrolls(c)) {
        const int track = rows * c->row_h, bx = c->x + c->w - bar_w(c) + bar_w(c) / 3;
        const int bw = bar_w(c) / 3 > 2 ? bar_w(c) / 3 : 2;
        int th = (int)((int64_t)track * rows / c->list.n);
        if (th < c->row_h / 2) th = c->row_h / 2;
        int ty = y0 + (int)((int64_t)track * c->top / c->list.n);
        if (ty + th > y0 + track) ty = y0 + track - th;
        gfx_fill_rect(bx, y0, bw, track, c->alt != c->bg ? c->alt : c->rule);
        gfx_fill_rect(bx, ty, bw, th, c->dim);
    }
}
