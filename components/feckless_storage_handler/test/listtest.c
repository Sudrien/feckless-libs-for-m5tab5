/*
 * listtest.c -- storage_list.c on the host (9011), against a real folder
 * made in /tmp: what is left out, what the filter sees and keeps, the
 * three orders, pinned rows, max, the volumes, and an arena that has to
 * grow while names are held.
 *
 * storage.c is the device's; the few calls storage_list.c makes of it
 * are here. storage_join_path() and storage_is_hidden() are copies of
 * storage.c's, which the comparison at the end of `make` checks.
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "storage.h"
#include "storage_list.h"

/* ---- storage.c's, for the host ---- */

bool storage_join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    if (!dir || !name) return false;

    const size_t dn = strlen(dir);
    const size_t nn = strlen(name);
    const size_t sep = (dn && dir[dn - 1] == '/') ? 0 : 1;

    if (dn + sep + nn + 1 > out_len) return false;

    memcpy(out, dir, dn);
    if (sep) out[dn] = '/';
    memcpy(out + dn + sep, name, nn + 1);
    return true;
}

bool storage_is_hidden(const char *name)
{
    return !name || name[0] == '.' ||
           strcasecmp(name, "System Volume Information") == 0 ||   /* 5272 */
           strcasecmp(name, "$RECYCLE.BIN") == 0;
}

static bool s_present[STORAGE_COUNT];
bool storage_present(storage_id_t id) { return id < STORAGE_COUNT && s_present[id]; }
const char *storage_mount_path(storage_id_t id) { return id == STORAGE_SD ? "/sd" : "/usb"; }
const char *storage_label(storage_id_t id) { return id == STORAGE_SD ? "microSD" : "USB"; }

/* ---- the folder ---- */

static char root[64];
static int fails;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void mk(const char *name, size_t bytes, time_t mtime)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, name);
    FILE *f = fopen(p, "w");
    assert(f);
    for (size_t i = 0; i < bytes; i++) fputc('x', f);
    fclose(f);
    struct utimbuf t = { mtime, mtime };
    utime(p, &t);
}

static void mkd(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, name);
    assert(mkdir(p, 0755) == 0);
}

static const char *names(const storage_list_t *l)
{
    static char out[2048];
    out[0] = '\0';
    for (int i = 0; i < l->n; i++) {
        if (i) strcat(out, " ");
        strcat(out, l->ents[i].name);
        if (l->ents[i].is_dir) strcat(out, "/");
    }
    return out;
}

/* .md and .txt kept, .md as kind 1; folders kept; the rest not.
 * Counts what it saw, and checks the folder it was given. */
typedef struct { int seen; const char *want_dir; } fctx_t;

static int notes(const char *dir, const storage_ent_t *e, void *ctx)
{
    fctx_t *c = ctx;
    c->seen++;
    if (strcmp(dir, c->want_dir) != 0) fails++;
    if (e->is_dir) return 0;
    const size_t n = strlen(e->name);
    if (n > 3 && !strcasecmp(e->name + n - 3, ".md")) return 1;
    if (n > 4 && !strcasecmp(e->name + n - 4, ".txt")) return 2;
    return STORAGE_LIST_SKIP;
}

int main(void)
{
    snprintf(root, sizeof root, "/tmp/listtest.XXXXXX");
    assert(mkdtemp(root));

    const time_t base = 1762300000;        /* November 2025 */
    mk("b.md", 10, base + 300);
    mk("A.md", 2000, base + 100);
    mk("c.TXT", 0, base + 200);
    mk("song.mp3", 5, base);
    mk(".h0", 5, base);                     /* a dotfile: never offered */
    mk("._b.md", 5, base);                  /* macOS's sidecar */
    mkd("zeta");
    mkd("Alpha");
    mkd("System Volume Information");

    /* by name, with the filter */
    storage_list_t l = { 0 };
    fctx_t c = { 0, root };
    CHECK(storage_list_read(&l, root, STORAGE_SORT_NAME, notes, &c) == STORAGE_LIST_OK);
    CHECK(!strcmp(names(&l), "Alpha/ zeta/ A.md b.md c.TXT"));
    CHECK(c.seen == 6);                     /* not . .. .h0 ._b.md SVI */
    int a = storage_list_find(&l, "a.MD");
    CHECK(a == 2 && l.ents[a].kind == 1 && l.ents[a].size == 2000);
    CHECK(l.ents[4].kind == 2 && l.ents[4].size == 0);
    CHECK(l.ents[0].size == -1 && l.ents[0].is_dir);
    int y, mo, d, h, mi;
    CHECK(storage_fat_time_parts(l.ents[a].fat_time, &y, &mo, &d, &h, &mi) && y == 2025 && mo == 11);
    CHECK(!storage_fat_time_parts(0, &y, &mo, &d, &h, &mi));

    /* newest first: files by date, folders still first and by name */
    storage_list_sort(&l, STORAGE_SORT_NEWEST);
    CHECK(!strcmp(names(&l), "Alpha/ zeta/ b.md c.TXT A.md"));

    /* no filter: every file and folder, the mp3 too */
    CHECK(storage_list_read(&l, root, STORAGE_SORT_NAME, NULL, NULL) == STORAGE_LIST_OK);
    CHECK(!strcmp(names(&l), "Alpha/ zeta/ A.md b.md c.TXT song.mp3"));

    /* the folder's own order holds the same entries */
    CHECK(storage_list_read(&l, root, STORAGE_SORT_NONE, NULL, NULL) == STORAGE_LIST_OK);
    CHECK(l.n == 6);

    /* pinned rows: first, in the order added, through every sort */
    CHECK(storage_list_read(&l, root, STORAGE_SORT_NAME, NULL, NULL) == STORAGE_LIST_OK);
    CHECK(storage_list_add(&l, "zz-boot", "Boot app2: x", false, -1, 0, true, 7));
    CHECK(storage_list_add(&l, "aa-link", NULL, true, -1, 0, true, 0));
    CHECK(storage_list_add(&l, "Track 03", "Track 03", false, 0, 0, false, 3));
    storage_list_sort(&l, STORAGE_SORT_NAME);
    CHECK(!strcmp(names(&l), "zz-boot aa-link/ Alpha/ zeta/ A.md b.md c.TXT song.mp3 Track 03"));
    CHECK(!strcmp(l.ents[0].label, "Boot app2: x") && l.ents[0].kind == 7);
    storage_list_sort(&l, STORAGE_SORT_NEWEST);
    CHECK(!strncmp(names(&l), "zz-boot aa-link/ ", 17));
    storage_list_sort(&l, STORAGE_SORT_NONE);
    CHECK(!strncmp(names(&l), "zz-boot aa-link/ ", 17));

    /* max: the first that fit, and it says so */
    storage_list_t m = { .max = 3 };
    CHECK(storage_list_read(&m, root, STORAGE_SORT_NAME, NULL, NULL) == STORAGE_LIST_OK);
    CHECK(m.n == 3 && m.truncated);
    CHECK(!storage_list_add(&m, "more", NULL, false, 1, 0, false, 0));
    storage_list_free(&m);
    CHECK(m.max == 3 && m.n == 0 && !m.ents);

    /* a folder that is not there */
    char gone[96];
    snprintf(gone, sizeof gone, "%s/nope", root);
    CHECK(storage_list_read(&l, gone, STORAGE_SORT_NAME, NULL, NULL) == STORAGE_LIST_NO_DIR);
    CHECK(l.n == 0);

    /* the volumes: only those present, labelled */
    s_present[STORAGE_SD] = false;
    s_present[STORAGE_USB] = true;
    storage_list_volumes(&l);
    CHECK(!strcmp(names(&l), "usb/") && !strcmp(l.ents[0].label, "USB"));
    s_present[STORAGE_SD] = true;
    storage_list_volumes(&l);
    CHECK(!strcmp(names(&l), "sd/ usb/") && !strcmp(l.ents[0].label, "microSD"));

    /* an arena that grows past its first block with every name held:
     * 600 names of 200 bytes is 120 KB, several blocks; ASan says
     * whether an earlier name moved */
    storage_list_clear(&l);
    char nm[256];
    for (int i = 0; i < 600; i++) {
        snprintf(nm, sizeof nm, "%04d-%0195d", i, 0);
        CHECK(storage_list_add(&l, nm, nm, false, i, (uint32_t)i, false, 0));
    }
    storage_list_sort(&l, STORAGE_SORT_NEWEST);
    CHECK(l.n == 600 && !strncmp(l.ents[0].name, "0599-", 5) && !strncmp(l.ents[599].label, "0000-", 5));
    for (int i = 0; i < 600; i++) CHECK(strlen(l.ents[i].name) == 200);

    /* cleared and read again into the same memory */
    CHECK(storage_list_read(&l, root, STORAGE_SORT_NAME, notes, &c) == STORAGE_LIST_OK);
    CHECK(l.n == 5);
    storage_list_free(&l);

    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);

    if (fails) {
        fprintf(stderr, "listtest: %d failed\n", fails);
        return 1;
    }
    puts("listtest: ok");
    return 0;
}
