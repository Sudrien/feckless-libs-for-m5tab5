/* SPDX-License-Identifier: MIT
 *
 * choosertest.c -- feckless_chooser (9012) on the host, over the real
 * storage_list.c (built with STORAGE_LIST_HOST) and a real folder in
 * /tmp. The gfx calls are recorded, not drawn, so what was drawn is what
 * is checked:
 *
 *   - keys: up, down, pages, home, end, a letter, into a folder and back
 *     out onto it, never above root, Escape, and a key the chooser does
 *     not use coming back as the app's;
 *   - taps: a row opens, the path line goes up, the scroll bar pages and
 *     the selection follows into view;
 *   - a card going: the folder gone, the chooser up to the volumes;
 *   - the app's rows: pinned first, opened not entered, no path;
 *   - drawing: the selected row's band, folders' slash and colour, the
 *     size and date column dropped as the width narrows, the notes, the
 *     scroll bar only when needed.
 *
 *   make -C components/feckless_chooser/test
 *   make -C components/feckless_chooser/test preview   (render.c: PPMs)
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "gfx.h"
#include "storage.h"
#include "feckless_chooser.h"

/* ---- recorded gfx ------------------------------------------------------- */

typedef struct { char op; int x, y, w, h; uint16_t col; char s[96]; } call_t;
#define MAXCALLS 512
static call_t calls[MAXCALLS];
static int ncalls;

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (ncalls < MAXCALLS) calls[ncalls++] = (call_t){ 'r', x, y, w, h, c, "" };
}

static void text(char op, int x, int y, const char *s, int max_w, uint16_t c)
{
    if (ncalls < MAXCALLS) {
        call_t k = { op, x, y, max_w, 0, c, "" };
        snprintf(k.s, sizeof k.s, "%s", s);
        calls[ncalls++] = k;
    }
}

void gfx_draw_text(int x, int y, const char *s, int scale, int max_w, uint16_t c)
{
    (void)scale;
    text('t', x, y, s, max_w, c);
}

void gfx_draw_text_tail(int x, int y, const char *s, int scale, int max_w, uint16_t c)
{
    (void)scale;
    text('p', x, y, s, max_w, c);
}

int gfx_text_w(const char *s, int scale)
{
    return (int)strlen(s) * GFX_GLYPH_W(scale);
}

/* The text drawn starting with `s`, or NULL. */
static const call_t *drawn(const char *s)
{
    for (int i = 0; i < ncalls; i++)
        if ((calls[i].op == 't' || calls[i].op == 'p') && !strncmp(calls[i].s, s, strlen(s)))
            return &calls[i];
    return NULL;
}

static void draw(const chooser_t *c)
{
    ncalls = 0;
    chooser_draw(c);
}

/* ---- storage.c's, for the host ------------------------------------------ */

bool storage_join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    const int n = snprintf(out, out_len, "%s%s%s", dir,
                           dir[0] && dir[strlen(dir) - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < out_len;
}

bool storage_is_hidden(const char *name) { return !name || name[0] == '.'; }

static bool s_present[STORAGE_COUNT] = { true, false };
static uint32_t s_gen = 1;
bool storage_present(storage_id_t id) { return id < STORAGE_COUNT && s_present[id]; }
const char *storage_mount_path(storage_id_t id) { return id == STORAGE_SD ? "/sd" : "/usb"; }
const char *storage_label(storage_id_t id) { return id == STORAGE_SD ? "microSD" : "USB"; }
uint32_t storage_generation(void) { return s_gen; }

/* ---- the folder --------------------------------------------------------- */

static char root[64];
static int fails;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void mk(const char *name, size_t bytes, time_t t)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, name);
    FILE *f = fopen(p, "w");
    assert(f);
    for (size_t i = 0; i < bytes; i++) fputc('x', f);
    fclose(f);
    struct utimbuf u = { t, t };
    utime(p, &u);
}

static void mkd(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, name);
    assert(mkdir(p, 0755) == 0);
}

static const char *sel(const chooser_t *c)
{
    const storage_ent_t *e = chooser_sel(c);
    return e ? e->name : "(none)";
}

static int md_only(const char *dir, const storage_ent_t *e, void *ctx)
{
    (void)dir;
    (void)ctx;
    if (e->is_dir) return 0;
    const size_t n = strlen(e->name);
    return n > 3 && !strcmp(e->name + n - 3, ".md") ? 0 : STORAGE_LIST_SKIP;
}

static void boot_row(storage_list_t *l, const char *dir, void *ctx)
{
    (void)dir;
    (void)ctx;
    storage_list_add(l, "app2", "Boot app2: player", false, -1, 0, true, 9);
}

#define C_BG   1
#define C_ALT  2
#define C_SEL  3
#define C_TEXT 4
#define C_DIM  5
#define C_DIR  6
#define C_RULE 7

static chooser_t c;     /* not the stack: it holds a path */

static void setup(int w)
{
    chooser_done(&c);
    memset(&c, 0, sizeof c);
    c = (chooser_t){ .x = 0, .y = 100, .w = w, .h = 7 * 60, .row_h = 60, .scale = 3,
                     .bg = C_BG, .alt = C_BG, .sel = C_SEL, .text = C_TEXT, .dim = C_DIM,
                     .folder = C_DIR, .rule = C_RULE, .root = root, .sort = STORAGE_SORT_NAME,
                     .filter = md_only, .show_path = true, .show_size = true, .show_time = true };
}

int main(void)
{
    snprintf(root, sizeof root, "/tmp/choosertest.XXXXXX");
    assert(mkdtemp(root));
    const time_t t0 = 1762300000;
    mkd("drafts");
    mkd("old");
    for (int i = 0; i < 12; i++) {
        char n[32];
        snprintf(n, sizeof n, "2026-11-%02d.md", i + 1);
        mk(n, (size_t)(i * 1000), t0 + i * 86400);
    }
    mk("notes.txt", 3, t0);
    mk("drafts/b.md", 2, t0);
    mk("drafts/a.md", 2, t0);

    /* 7 rows high, one the path's: 6 rows of 14 entries */
    setup(1280);
    chooser_begin(&c, NULL);
    CHECK(!strcmp(c.dir, root));
    CHECK(c.list.n == 14 && chooser_rows(&c) == 6);
    CHECK(!strcmp(sel(&c), "drafts"));

    /* keys */
    CHECK(chooser_key(&c, CHOOSER_KEY_UP) == CHOOSER_NONE);         /* at the top already */
    CHECK(chooser_key(&c, CHOOSER_KEY_DOWN) == CHOOSER_REDRAW && !strcmp(sel(&c), "old"));
    CHECK(chooser_key(&c, CHOOSER_KEY_PGDN) == CHOOSER_REDRAW && c.cur == 6 && c.top == 1);
    CHECK(chooser_key(&c, CHOOSER_KEY_END) == CHOOSER_REDRAW && c.cur == 13 && c.top == 8);
    CHECK(chooser_key(&c, CHOOSER_KEY_DOWN) == CHOOSER_NONE);
    CHECK(chooser_key(&c, CHOOSER_KEY_HOME) == CHOOSER_REDRAW && c.cur == 0 && c.top == 0);
    CHECK(chooser_key(&c, 'o') == CHOOSER_REDRAW && !strcmp(sel(&c), "old"));
    CHECK(chooser_key(&c, '2') == CHOOSER_REDRAW && !strcmp(sel(&c), "2026-11-01.md"));
    CHECK(chooser_key(&c, '2') == CHOOSER_REDRAW && !strcmp(sel(&c), "2026-11-02.md"));
    CHECK(chooser_key(&c, 'z') == CHOOSER_NONE);
    CHECK(chooser_key(&c, -99) == CHOOSER_KEY);                     /* the app's: F2, Delete */
    CHECK(chooser_key(&c, CHOOSER_KEY_ESC) == CHOOSER_CANCEL);

    /* a file: OPEN, and its path */
    char p[CHOOSER_PATH_MAX], want[CHOOSER_PATH_MAX];
    CHECK(chooser_key(&c, CHOOSER_KEY_ENTER) == CHOOSER_OPEN);
    snprintf(want, sizeof want, "%s/2026-11-02.md", root);
    CHECK(chooser_sel_path(&c, p, sizeof p) && !strcmp(p, want));

    /* into a folder, and back out onto it; never above root */
    chooser_select(&c, "drafts");
    CHECK(chooser_key(&c, CHOOSER_KEY_ENTER) == CHOOSER_REDRAW);
    snprintf(want, sizeof want, "%s/drafts", root);
    CHECK(!strcmp(c.dir, want) && c.list.n == 2 && !strcmp(sel(&c), "a.md"));
    CHECK(chooser_key(&c, CHOOSER_KEY_BACK) == CHOOSER_REDRAW);
    CHECK(!strcmp(c.dir, root) && !strcmp(sel(&c), "drafts"));
    CHECK(chooser_key(&c, CHOOSER_KEY_BACK) == CHOOSER_NONE && !strcmp(c.dir, root));

    /* begin in a folder below root, with a trailing slash; one outside
     * root is root */
    snprintf(want, sizeof want, "%s/drafts/", root);
    chooser_begin(&c, want);
    CHECK(c.list.n == 2 && c.dir[strlen(c.dir) - 1] != '/');
    chooser_begin(&c, "/etc");
    CHECK(!strcmp(c.dir, root));

    /* taps: the path line goes up, a row opens, the scroll bar pages */
    snprintf(want, sizeof want, "%s/drafts", root);
    chooser_begin(&c, want);
    CHECK(chooser_tap(&c, 50, 100 + 10) == CHOOSER_REDRAW && !strcmp(c.dir, root));
    CHECK(chooser_tap(&c, 50, 100 + 60 + 60 + 10) == CHOOSER_REDRAW);    /* row 1: old/ */
    snprintf(want, sizeof want, "%s/old", root);
    CHECK(!strcmp(c.dir, want) && c.list.n == 0);
    draw(&c);
    CHECK(drawn("Nothing here."));
    CHECK(chooser_key(&c, CHOOSER_KEY_BACK) == CHOOSER_REDRAW && !strcmp(sel(&c), "old"));
    CHECK(chooser_tap(&c, 50, 100 + 60 + 2 * 60 + 10) == CHOOSER_OPEN && !strcmp(sel(&c), "2026-11-01.md"));
    CHECK(chooser_tap(&c, 1279, 100 + 6 * 60) == CHOOSER_REDRAW && c.top == 5 && c.cur == 5);
    CHECK(chooser_tap(&c, 1279, 100 + 6 * 60) == CHOOSER_REDRAW && c.top == 8 && c.cur == 8);
    CHECK(chooser_tap(&c, 1279, 100 + 61) == CHOOSER_REDRAW && c.top == 3 && c.cur == 8);
    CHECK(chooser_tap(&c, 5000, 5000) == CHOOSER_NONE);

    /* drawing: path, the band, folders, sizes and dates, the bar */
    chooser_key(&c, CHOOSER_KEY_HOME);
    draw(&c);
    const call_t *k = drawn(root);
    CHECK(k && k->op == 'p' && k->col == C_DIM);
    k = drawn("drafts/");
    CHECK(k && k->col == C_DIR);
    CHECK(drawn("old/") && !drawn("2026-11-05.md"));            /* in view only */
    int band = 0, bar = 0;
    for (int i = 0; i < ncalls; i++) {
        if (calls[i].op == 'r' && calls[i].col == C_SEL && calls[i].y == 160 && calls[i].h == 60) band++;
        if (calls[i].op == 'r' && calls[i].col == C_DIM && calls[i].x > 1250) bar++;
    }
    CHECK(band == 1 && bar == 1);
    chooser_select(&c, "2026-11-03.md");
    draw(&c);
    k = drawn("2 KB   2025-11-");
    CHECK(k && k->col == C_DIM);

    /* narrower: the date goes, then the size, and the name keeps half */
    setup(640);
    chooser_begin(&c, NULL);
    chooser_select(&c, "2026-11-03.md");
    draw(&c);
    CHECK(drawn("2 KB") && !drawn("2 KB   "));
    setup(200);
    chooser_begin(&c, NULL);
    chooser_select(&c, "2026-11-03.md");
    draw(&c);
    CHECK(!drawn("2 KB") && drawn("2026-11-03.md"));

    /* no scroll bar when it all fits */
    setup(1280);
    snprintf(want, sizeof want, "%s/drafts", root);
    chooser_begin(&c, want);
    draw(&c);
    bar = 0;
    for (int i = 0; i < ncalls; i++)
        if (calls[i].op == 'r' && calls[i].col == C_DIM) bar++;
    CHECK(bar == 0);

    /* the app's row: pinned first, opened, no path; kept through reloads */
    setup(1280);
    c.decorate = boot_row;
    chooser_begin(&c, NULL);
    CHECK(!strcmp(sel(&c), "app2") && c.list.n == 15);
    CHECK(chooser_key(&c, CHOOSER_KEY_ENTER) == CHOOSER_OPEN && chooser_sel(&c)->kind == 9);
    CHECK(!chooser_sel_path(&c, p, sizeof p));
    draw(&c);
    CHECK(drawn("Boot app2: player"));
    chooser_key(&c, CHOOSER_KEY_END);
    chooser_reload(&c);
    CHECK(c.list.n == 15 && !strcmp(c.list.ents[0].name, "app2") && c.cur == 14);

    /* a volume going: the folder is gone with it, the chooser climbs to
     * the volumes, and the volumes listed are those present */
    setup(1280);
    c.root = NULL;
    chooser_begin(&c, "/sd/jinxed");       /* no /sd here: up to the volumes */
    CHECK(c.dir[0] == '\0' && c.list.n == 1 && !strcmp(c.list.ents[0].label, "microSD"));
    draw(&c);
    CHECK(drawn("microSD and USB") && drawn("microSD"));
    CHECK(chooser_poll(&c) == CHOOSER_NONE);
    s_present[STORAGE_USB] = true;
    s_gen++;
    CHECK(chooser_poll(&c) == CHOOSER_REDRAW && c.list.n == 2 && !strcmp(sel(&c), "sd"));
    CHECK(chooser_sel_path(&c, p, sizeof p) && !strcmp(p, "/sd"));
    CHECK(chooser_key(&c, CHOOSER_KEY_BACK) == CHOOSER_NONE);    /* the top */

    /* in a real folder whose files are deleted underneath: reload keeps
     * the row's place */
    setup(1280);
    chooser_begin(&c, NULL);
    chooser_select(&c, "2026-11-12.md");
    snprintf(p, sizeof p, "%s/2026-11-12.md", root);
    unlink(p);
    s_gen++;
    CHECK(chooser_poll(&c) == CHOOSER_REDRAW && c.list.n == 13 && !strcmp(sel(&c), "2026-11-11.md"));

    chooser_done(&c);
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);

    if (fails) {
        fprintf(stderr, "choosertest: %d failed\n", fails);
        return 1;
    }
    puts("choosertest: ok");
    return 0;
}
