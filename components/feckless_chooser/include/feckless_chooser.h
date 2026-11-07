/* SPDX-License-Identifier: MIT
 *
 * feckless_chooser.h -- a file chooser drawn with gfx and driven by the
 * app (9012): a folder's entries from storage_list.h, a row each, moved
 * through by keys or taps, into folders and back out.
 *
 * Like mailform and feckless_keyboard it owns no hardware and no task.
 * The app gives it a rectangle and its colours, draws it into the
 * framebuffer and blits, and hands it taps (chooser_tap), keys
 * (chooser_key) and, now and then, chooser_poll() to notice a card come
 * or go. So it fits Avoidant's portrait menu, worked by taps, and Jinxed
 * Writer's landscape one, worked by a USB keyboard, and draws in either's
 * theme at either's text size.
 *
 * What it does not do: decide which files are wanted (the filter, as
 * storage_list_read() takes it), draw a header or buttons around itself,
 * or act on a file. Choosing a file is CHOOSER_OPEN and the app does the
 * rest. Keys it has no use for come back as CHOOSER_KEY, so the app can
 * give Delete or F2 a meaning of its own.
 *
 * The rows, top to bottom, each row_h high:
 *
 *   /sd/jinxed                                    the path (show_path)
 *   [ Boot app2: player             ]             pinned rows, the app's
 *   drafts/                                       folders
 *   2026-11-06.md          4 KB   2026-11-06 14:05   files: size, date
 *   ...                                         | the scroll bar, when
 *                                                 there is more than fits
 *
 * The highlighted row is the selection; a tap opens what it lands on.
 *
 * chooser_t holds the list and a path: a few hundred bytes. Keep it
 * static or on the heap, never on a task's stack.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "storage_list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHOOSER_PATH_MAX    (512)

/* What a tap, a key or a poll did. */
typedef enum {
    CHOOSER_NONE = 0,       /* nothing: no need to redraw */
    CHOOSER_REDRAW,         /* something changed: draw and blit */
    CHOOSER_OPEN,           /* a file or a pinned row was chosen: chooser_sel() */
    CHOOSER_CANCEL,         /* Escape */
    CHOOSER_KEY,            /* a key the chooser does not use: the app's */
} chooser_result_t;

/* Keys besides characters for chooser_key(). A printable character
 * moves to the next row starting with it. Distinct from mailform's. */
#define CHOOSER_KEY_UP      (-20)
#define CHOOSER_KEY_DOWN    (-21)
#define CHOOSER_KEY_PGUP    (-22)
#define CHOOSER_KEY_PGDN    (-23)
#define CHOOSER_KEY_HOME    (-24)
#define CHOOSER_KEY_END     (-25)
#define CHOOSER_KEY_ENTER   (-26)   /* open the selection: a folder, or a file */
#define CHOOSER_KEY_BACK    (-27)   /* up a folder: Backspace, or Left */
#define CHOOSER_KEY_ESC     (-28)   /* CHOOSER_CANCEL */

typedef struct {
    /* where: a w by h rectangle at (x, y) */
    int x, y, w, h;
    int row_h;              /* one row; text is centred in it */
    int scale;              /* gfx text scale */
    /* colours: background, alternate rows (== bg for none), the
     * selection's band, names, sizes and dates and the path, folders,
     * the rule under each row and the scroll bar's track */
    uint16_t bg, alt, sel, text, dim, folder, rule;

    /* what is listed */
    const char *root;       /* NULL or "": the volumes; else a folder never left upward */
    const char *top_label;  /* the path line at the volumes; NULL: "microSD and USB" */
    const char *empty;      /* when nothing is listed; NULL: "Nothing here." */
    storage_sort_t sort;
    int max;                /* the most entries kept; 0: STORAGE_LIST_MAX */
    storage_list_filter_fn filter;      /* storage_list_read()'s; NULL keeps all */
    /* After each read, before it is drawn: put in rows of the app's own
     * (storage_list_add(), pinned or not). The list is already sorted;
     * a pinned row goes first wherever it was added. NULL: none. */
    void (*decorate)(storage_list_t *l, const char *dir, void *ctx);
    /* A row's text colour; NULL, or 0 returned: folder for a folder, text
     * for anything else. */
    uint16_t (*colour)(const storage_ent_t *e, void *ctx);
    void *ctx;              /* filter's, decorate's and colour's */
    bool show_path, show_size, show_time;

    /* the chooser's own: set by chooser_begin() */
    storage_list_t list;
    char dir[CHOOSER_PATH_MAX];     /* "" at the volumes */
    int cur, top;
    uint32_t gen;
    storage_list_err_t err;
} chooser_t;

/*
 * Read `dir` and select its first row. NULL, or a folder outside root,
 * is root. A folder that cannot be read goes up until one can. Set the
 * geometry, colours and what is listed first.
 */
void chooser_begin(chooser_t *c, const char *dir);

/* Everything into the rectangle, its background first. */
void chooser_draw(const chooser_t *c);

/*
 * A tap at (tx, ty). On a row: a folder opens, a file or a pinned row is
 * CHOOSER_OPEN, selected. On the path line: up a folder. On the scroll
 * bar: a page up or down, by which side of the thumb. Elsewhere: none.
 */
chooser_result_t chooser_tap(chooser_t *c, int tx, int ty);

/* A character (printable ASCII) or a CHOOSER_KEY_*. Anything else is
 * CHOOSER_KEY, with nothing changed. */
chooser_result_t chooser_key(chooser_t *c, int key);

/*
 * Read again if a volume came or went since the last read
 * (storage_generation()): CHOOSER_REDRAW then, keeping the selection by
 * name, going up if the folder went with its volume. Cheap otherwise;
 * call it while waiting for input.
 */
chooser_result_t chooser_poll(chooser_t *c);

/* Read again now, keeping the selection by name -- after the app
 * renamed, deleted or wrote something here. */
void chooser_reload(chooser_t *c);

/* The selected entry, or NULL when nothing is listed. */
const storage_ent_t *chooser_sel(const chooser_t *c);

/* Select the entry named `name`, scrolled into view. False if none. */
bool chooser_select(chooser_t *c, const char *name);

/* The selection's full path: the folder and its name. False when
 * nothing is selected, it is a pinned row, or it would not fit. */
bool chooser_sel_path(const chooser_t *c, char *out, size_t n);

/* How many rows of entries fit (under the path line, if shown). */
int chooser_rows(const chooser_t *c);

/* Give the list's memory back. chooser_begin() can start it again. */
void chooser_done(chooser_t *c);

#ifdef __cplusplus
}
#endif
