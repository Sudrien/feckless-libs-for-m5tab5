/*
 * storage_list.c -- see storage_list.h (9011).
 *
 * The device reads with FatFs's f_readdir(), for its size and date. A
 * host build (-DSTORAGE_LIST_HOST, test/) reads with POSIX opendir() and
 * stat() instead, with storage.h's calls supplied by the test; the
 * filtering, the arena and the sorting are the same code.
 *
 * SPDX-License-Identifier: MIT
 */
#include "storage_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "storage.h"

#ifdef STORAGE_LIST_HOST
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#define list_alloc(n)       malloc(n)
#define list_realloc(p, n)  realloc((p), (n))
#else
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ff.h"
#include "storage_io.h"
static const char *TAG = "storage_list";
#define CLS STORAGE_IO_BACKGROUND
/* PSRAM where there is some; internal RAM is the radio's, and only
 * a board without PSRAM gets these from it. */
#define PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
static void *list_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, PSRAM);
    return p ? p : malloc(n);
}
static void *list_realloc(void *o, size_t n)
{
    void *p = heap_caps_realloc(o, n, PSRAM);
    return p ? p : realloc(o, n);
}
#endif

/* ---- the arena: blocks that never move, so an entry's strings stay put */

typedef struct block {
    struct block *next;
    size_t used, size;
    char data[];
} block_t;

#define BLOCK_MIN   (16 * 1024)

static block_t *first_block(const storage_list_t *l) { return (block_t *)l->arena; }

static const char *keep(storage_list_t *l, const char *s)
{
    if (!s) return NULL;
    const size_t n = strlen(s) + 1;
    block_t *b = first_block(l);
    if (!b || b->size - b->used < n) {
        const size_t sz = n > BLOCK_MIN ? n : BLOCK_MIN;
        block_t *nb = list_alloc(sizeof(block_t) + sz);
        if (!nb) return NULL;
        nb->next = b;
        nb->used = 0;
        nb->size = sz;
        l->arena = (char *)nb;
        l->size += sz;
        b = nb;
    }
    char *p = b->data + b->used;
    memcpy(p, s, n);
    b->used += n;
    l->used += n;
    return p;
}

void storage_list_clear(storage_list_t *l)
{
    if (!l) return;
    l->n = 0;
    l->truncated = false;
    /* keep the newest block for the next read; the rest go */
    block_t *b = first_block(l);
    if (b) {
        block_t *o = b->next;
        while (o) {
            block_t *x = o->next;
            free(o);
            o = x;
        }
        b->next = NULL;
        b->used = 0;
        l->size = b->size;
    }
    l->used = 0;
}

void storage_list_free(storage_list_t *l)
{
    if (!l) return;
    storage_list_clear(l);
    free(l->arena);
    free(l->ents);
    const int max = l->max;
    memset(l, 0, sizeof *l);
    l->max = max;
}

static int limit(const storage_list_t *l)
{
    return l->max > 0 ? l->max : STORAGE_LIST_MAX;
}

/* Room for one more entry. False at max (truncated) or without memory. */
static bool room(storage_list_t *l)
{
    if (l->n >= limit(l)) {
        l->truncated = true;
        return false;
    }
    if (l->n < l->cap) return true;
    int c = l->cap ? l->cap * 2 : 64;
    if (c > limit(l)) c = limit(l);
    storage_ent_t *e = list_realloc(l->ents, (size_t)c * sizeof *e);
    if (!e) return false;
    l->ents = e;
    l->cap = c;
    return true;
}

bool storage_list_add(storage_list_t *l, const char *name, const char *label, bool is_dir,
                      int64_t size, uint32_t fat_time, bool pinned, uint8_t kind)
{
    if (!l || !name || !room(l)) return false;
    storage_ent_t *e = &l->ents[l->n];
    e->name = keep(l, name);
    e->label = label ? keep(l, label) : NULL;
    if (!e->name || (label && !e->label)) return false;
    e->size = is_dir ? -1 : size;
    e->fat_time = fat_time;
    e->is_dir = is_dir;
    e->pinned = pinned;
    e->kind = kind;
    l->n++;
    return true;
}

/* ---- sorting ------------------------------------------------------------ */

/* Pinned first in the order added -- qsort is not stable, so the index
 * is the tie-break, and is what makes NONE the folder's order. */
typedef struct {
    storage_ent_t e;
    int i;
} ranked_t;

static int cmp_common(const ranked_t *x, const ranked_t *y, int *out)
{
    if (x->e.pinned != y->e.pinned) { *out = x->e.pinned ? -1 : 1; return 1; }
    if (x->e.pinned) { *out = x->i - y->i; return 1; }
    if (x->e.is_dir != y->e.is_dir) { *out = x->e.is_dir ? -1 : 1; return 1; }
    return 0;
}

static int by_name_then_i(const ranked_t *x, const ranked_t *y)
{
    const int c = strcasecmp(x->e.name, y->e.name);
    if (c) return c;
    /* two names FAT would call one only differ in a listing that was
     * added to by hand; settle them by bytes, then by order */
    const int b = strcmp(x->e.name, y->e.name);
    return b ? b : x->i - y->i;
}

static int cmp_name(const void *a, const void *b)
{
    int r;
    if (cmp_common(a, b, &r)) return r;
    return by_name_then_i(a, b);
}

static int cmp_newest(const void *a, const void *b)
{
    const ranked_t *x = a, *y = b;
    int r;
    if (cmp_common(x, y, &r)) return r;
    /* folders by name: a folder's FAT date is when it was made, which is
     * not what "recent" means to anyone looking at it */
    if (!x->e.is_dir && x->e.fat_time != y->e.fat_time)
        return x->e.fat_time > y->e.fat_time ? -1 : 1;
    return by_name_then_i(x, y);
}

static int cmp_none(const void *a, const void *b)
{
    const ranked_t *x = a, *y = b;
    if (x->e.pinned != y->e.pinned) return x->e.pinned ? -1 : 1;
    return x->i - y->i;
}

void storage_list_sort(storage_list_t *l, storage_sort_t sort)
{
    if (!l || l->n < 2) return;
    ranked_t *r = list_alloc((size_t)l->n * sizeof *r);
    if (!r) return;     /* left in the order it has: nothing lost */
    for (int i = 0; i < l->n; i++) {
        r[i].e = l->ents[i];
        r[i].i = i;
    }
    qsort(r, (size_t)l->n, sizeof *r,
          sort == STORAGE_SORT_NEWEST ? cmp_newest : sort == STORAGE_SORT_NONE ? cmp_none : cmp_name);
    for (int i = 0; i < l->n; i++) l->ents[i] = r[i].e;
    free(r);
}

int storage_list_find(const storage_list_t *l, const char *name)
{
    if (!l || !name) return -1;
    for (int i = 0; i < l->n; i++)
        if (!strcasecmp(l->ents[i].name, name)) return i;
    return -1;
}

void storage_list_volumes(storage_list_t *l)
{
    if (!l) return;
    storage_list_clear(l);
    for (int i = 0; i < STORAGE_COUNT; i++) {
        const storage_id_t id = (storage_id_t)i;
        if (!storage_present(id)) continue;
        const char *mp = storage_mount_path(id);
        if (!mp) continue;
        storage_list_add(l, mp[0] == '/' ? mp + 1 : mp, storage_label(id), true, -1, 0, false, 0);
    }
}

/* ---- reading ------------------------------------------------------------ */

/* The filter, and keeping what it keeps. False to stop: no memory. */
static bool offer(storage_list_t *l, const char *dir, storage_ent_t *e,
                  storage_list_filter_fn filter, void *ctx, storage_list_err_t *err)
{
    if (storage_is_hidden(e->name)) return true;
    int kind = 0;
    if (filter) {
        kind = filter(dir, e, ctx);
        if (kind < 0) return true;
        if (kind > 255) kind = 255;
    }
    if (storage_list_add(l, e->name, NULL, e->is_dir, e->size, e->fat_time, false, (uint8_t)kind))
        return true;
    if (l->truncated) return false;     /* at max: stop, not an error */
    *err = STORAGE_LIST_NO_MEM;
    return false;
}

#ifdef STORAGE_LIST_HOST

static uint32_t host_fat_time(time_t t)
{
    struct tm tm;
    if (!localtime_r(&t, &tm) || tm.tm_year < 80) return 0;
    return (uint32_t)(tm.tm_year - 80) << 25 | (uint32_t)(tm.tm_mon + 1) << 21 |
           (uint32_t)tm.tm_mday << 16 | (uint32_t)tm.tm_hour << 11 |
           (uint32_t)tm.tm_min << 5 | (uint32_t)(tm.tm_sec / 2);
}

static storage_list_err_t read_dir(storage_list_t *l, const char *dir,
                                   storage_list_filter_fn filter, void *ctx)
{
    DIR *d = opendir(dir);
    if (!d) return STORAGE_LIST_NO_DIR;
    storage_list_err_t err = STORAGE_LIST_OK;
    struct dirent *de;
    char full[1024];
    while ((de = readdir(d)) != NULL) {
        storage_ent_t e = { .name = de->d_name, .size = -1 };
        struct stat st;
        if (storage_join_path(full, sizeof full, dir, de->d_name) && stat(full, &st) == 0) {
            e.is_dir = S_ISDIR(st.st_mode);
            if (!e.is_dir) e.size = (int64_t)st.st_size;
            e.fat_time = host_fat_time(st.st_mtime);
        }
        if (!offer(l, dir, &e, filter, ctx, &err)) break;
    }
    closedir(d);
    return err;
}

#else

typedef struct {
    FF_DIR  dir;
    FILINFO fi;
    char    path[8 + 512];
} ffwork_t;

static storage_list_err_t read_dir(storage_list_t *l, const char *dir,
                                   storage_list_filter_fn filter, void *ctx)
{
    const storage_id_t id = storage_of_path(dir);
    if (id == STORAGE_COUNT) return STORAGE_LIST_NO_DIR;

    storage_hold_brief(id);
    storage_list_err_t err = STORAGE_LIST_NO_DIR;
    ffwork_t *w = NULL;
    const int drv = storage_present(id) ? storage_ff_drive(id) : -1;
    if (drv < 0) goto out;

    /* FatFs's own path: the drive, then the path below the mount */
    w = list_alloc(sizeof *w);
    if (!w) { err = STORAGE_LIST_NO_MEM; goto out; }
    const char *rel = dir + strlen(storage_mount_path(id));
    if (*rel == '\0') rel = "/";
    const int n = snprintf(w->path, sizeof w->path, "%d:%s", drv, rel);
    if (n < 0 || (size_t)n >= sizeof w->path) goto out;

    storage_io_acquire(CLS);
    FRESULT r = f_opendir(&w->dir, w->path);
    storage_io_release();
    if (r != FR_OK) {
        ESP_LOGW(TAG, "f_opendir(%s): %d", w->path, (int)r);
        goto out;
    }
    err = STORAGE_LIST_OK;
    for (;;) {
        storage_io_acquire(CLS);
        r = f_readdir(&w->dir, &w->fi);
        storage_io_release();
        if (r != FR_OK) {
            /* an error is never the end: a chooser that showed half a
             * folder as all of it would be lying */
            ESP_LOGW(TAG, "f_readdir(%s): %d", w->path, (int)r);
            err = STORAGE_LIST_IO;
            break;
        }
        if (w->fi.fname[0] == '\0') break;
        if (w->fi.fattrib & (AM_HID | AM_SYS)) continue;
        storage_ent_t e = {
            .name = w->fi.fname,
            .is_dir = (w->fi.fattrib & AM_DIR) != 0,
            .size = (w->fi.fattrib & AM_DIR) ? -1 : (int64_t)w->fi.fsize,
            .fat_time = (uint32_t)w->fi.fdate << 16 | w->fi.ftime,
        };
        if (!offer(l, dir, &e, filter, ctx, &err)) break;
    }
    storage_io_acquire(CLS);
    f_closedir(&w->dir);
    storage_io_release();
out:
    free(w);
    storage_release_brief(id);
    return err;
}

#endif

storage_list_err_t storage_list_read(storage_list_t *l, const char *dir, storage_sort_t sort,
                                     storage_list_filter_fn filter, void *ctx)
{
    if (!l) return STORAGE_LIST_NO_DIR;
    storage_list_clear(l);
    if (!dir) return STORAGE_LIST_NO_DIR;
    const storage_list_err_t err = read_dir(l, dir, filter, ctx);
    if (err == STORAGE_LIST_NO_DIR) storage_list_clear(l);
    else storage_list_sort(l, sort);
    return err;
}
