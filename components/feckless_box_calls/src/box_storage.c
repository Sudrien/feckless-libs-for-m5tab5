/* SPDX-License-Identifier: MIT
 *
 * Storage for a boxed app (u-feckless-0007): the storage handler, FatFs,
 * stdio and directories, as Spurious calls them.
 *
 * Nothing the host keeps is in the box. A FatFs FIL holds a pointer to the
 * volume's FATFS and a cluster table; an app that could rewrite those could
 * aim FatFs anywhere. So the real FIL is the host's, found by the address of
 * the app's own FIL (which the app allocated and passes to every call), and
 * after each call only the fields FatFs's macros read -- f_size, f_tell,
 * f_eof, f_error -- are copied back into the app's. The fast-seek table the
 * app points cltbl at is mirrored: the host builds its own and tells the app
 * only the count FatFs reports in its first word.
 *
 * FILE and DIR are opaque to an app, so it gets a number for each; readdir()'s
 * struct dirent is copied into a buffer in the box. The storage handler's
 * strings are copied into the box too.
 *
 * feckless_box_calls_end() closes what the app left open and gives back the
 * I/O lease and the volume hold, which the launcher's own storage would
 * otherwise wait on.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "ff.h"
#include "storage.h"
#include "storage_io.h"

#include "box_calls_priv.h"

#define MAX_FIL   8
#define MAX_FILE  8
#define MAX_DIR   4
#define CLMT_MAX  16384u                    /* items; Spurious asks for at most this */
#define PATH_MAX_BOX 1024u
#define TOKEN_FILE 0x46494c00u              /* "FIL\0" + slot: a FILE * */
#define TOKEN_DIR  0x44495200u              /* "DIR\0" + slot: a DIR * */

/*
 * u-feckless-0015: reads into the box go through internal RAM.
 *
 * The box is in PSRAM, and so is the framebuffer the DPI peripheral scans
 * out continuously and cannot wait on. A large read handed the app's buffer
 * goes to the filesystem as a DMA straight into PSRAM -- stdio bypasses its
 * buffer for a request at least its size, FatFs reads whole sectors into the
 * caller's buffer -- and the card becomes a second bus master on that
 * memory: the panel underruns and goes cyan for a frame. On a Tab5, Spurious
 * loading a 54189-word dictionary in its box did exactly that. So reads are
 * cut into READ_SLICE pieces through one internal, DMA-capable staging
 * buffer and copied into the box by the CPU -- what feckless_storage_handler's
 * storage_io.c does for playback (PLAYBACK_SLICE, the same 8 KB), and for the
 * same reason. If the staging buffer cannot be had, reads go direct, as before.
 */
#define READ_SLICE (8 * 1024)

static uint8_t *s_stage;

static uint8_t *stage(void)
{
    if (!s_stage) {
        s_stage = heap_caps_aligned_alloc(64, READ_SLICE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    return s_stage;
}

typedef struct {
    uint32_t app;                           /* the app's FIL: the key */
    FIL fil;
    DWORD *tbl;                             /* the host's cluster table */
    uint32_t tbl_items;
    uint32_t tbl_app;                       /* the app's table it was built for */
    bool tbl_ok;
} fil_slot_t;

static fil_slot_t *s_fil[MAX_FIL];
static FILE *s_file[MAX_FILE];
static DIR *s_dir[MAX_DIR];
static uint32_t s_dirent[MAX_DIR];          /* the box buffer readdir() fills, per slot */
static uint32_t s_mount[STORAGE_COUNT];     /* the box copy of each mount path */
static int s_io_depth;                      /* the app's storage_io_acquire()s not released */
static bool s_held;

/* ---- FatFs --------------------------------------------------------------- */

static fil_slot_t *fil_of(uint32_t app)
{
    for (int i = 0; i < MAX_FIL; i++) {
        if (s_fil[i] && s_fil[i]->app == app) {
            return s_fil[i];
        }
    }
    return NULL;
}

/* The fields FatFs's macros read, into the app's FIL. */
static void fil_out(const fil_slot_t *s)
{
    FIL *a = (FIL *)(uintptr_t)s->app;
    a->obj.objsize = s->fil.obj.objsize;
    a->fptr = s->fil.fptr;
    a->flag = s->fil.flag;
    a->err = s->fil.err;
}

#if FF_USE_FASTSEEK
/* Point the host's FIL at the host's table if the app has one built. */
static void fil_cltbl(win_t *w, fil_slot_t *s)
{
    const uint32_t app_tbl = (uint32_t)(uintptr_t)((FIL *)(uintptr_t)s->app)->cltbl;
    s->fil.cltbl = (app_tbl && app_tbl == s->tbl_app && s->tbl_ok) ? s->tbl : NULL;
    (void)w;
}
#endif

H(c_f_open)
{
    const char *path = str(w, a[1], PATH_MAX_BOX);
    if (!ok_ptr(w, a[0], sizeof(FIL)) || !path) {
        *ret = FR_INVALID_PARAMETER;
        return true;
    }
    if (fil_of(a[0])) {
        *ret = FR_INVALID_OBJECT;           /* that FIL is open: close it first */
        return true;
    }
    int i = 0;
    while (i < MAX_FIL && s_fil[i]) i++;
    if (i == MAX_FIL) {
        *ret = FR_TOO_MANY_OPEN_FILES;
        return true;
    }
    fil_slot_t *s = calloc(1, sizeof *s);
    if (!s) {
        *ret = FR_NOT_ENOUGH_CORE;
        return true;
    }
    s->app = a[0];
    const FRESULT r = f_open(&s->fil, path, (BYTE)a[2]);
    if (r != FR_OK) {
        free(s);
        *ret = r;
        return true;
    }
    s_fil[i] = s;
#if FF_USE_FASTSEEK   /* u-feckless-0024: the one use 0007 left unguarded */
    ((FIL *)(uintptr_t)s->app)->cltbl = NULL;
#endif
    fil_out(s);
    *ret = FR_OK;
    return true;
}

H(c_f_read)
{
    fil_slot_t *s = fil_of(a[0]);
    if (!s) {
        *ret = FR_INVALID_OBJECT;
        return true;
    }
    if (!ok_ptr(w, a[1], a[2]) || !ok_ptr(w, a[3], sizeof(UINT))) {
        *ret = FR_INVALID_PARAMETER;
        return true;
    }
#if FF_USE_FASTSEEK
    fil_cltbl(w, s);
#endif
    UINT got = 0;
    uint8_t *const st = stage();
    if (!st) {
        *ret = f_read(&s->fil, (void *)(uintptr_t)a[1], (UINT)a[2], &got);
    } else {
        /* In slices through internal RAM; a short slice or an error ends it,
         * as one f_read() would have. */
        FRESULT r = FR_OK;
        while (got < (UINT)a[2]) {
            const UINT want = (UINT)a[2] - got > READ_SLICE ? READ_SLICE : (UINT)a[2] - got;
            UINT g = 0;
            r = f_read(&s->fil, st, want, &g);
            memcpy((uint8_t *)(uintptr_t)a[1] + got, st, g);
            got += g;
            if (r != FR_OK || g < want) {
                break;
            }
        }
        *ret = r;
    }
    memcpy((void *)(uintptr_t)a[3], &got, sizeof got);
    fil_out(s);
    return true;
}

/* f_lseek(fp, ofs): a 64-bit FSIZE_t arrives in a1 (low) and a2 (high). */
H(c_f_lseek)
{
    (void)w;              /* only fast seek reads it */
    fil_slot_t *s = fil_of(a[0]);
    if (!s) {
        *ret = FR_INVALID_OBJECT;
        return true;
    }
    FSIZE_t ofs = (FSIZE_t)a[1];
    if (sizeof(FSIZE_t) > 4) {
        ofs = (FSIZE_t)((uint64_t)a[2] << 32 | a[1]);
    }
#if FF_USE_FASTSEEK
    if (ofs == CREATE_LINKMAP) {
        const uint32_t app_tbl = (uint32_t)(uintptr_t)((FIL *)(uintptr_t)s->app)->cltbl;
        s->tbl_ok = false;
        s->tbl_app = 0;
        uint32_t items;
        if (!app_tbl || !ok_ptr(w, app_tbl, 4)) {
            *ret = FR_INVALID_PARAMETER;
            return true;
        }
        memcpy(&items, (void *)(uintptr_t)app_tbl, 4);
        if (items < 4 || items > CLMT_MAX || !ok_ptr(w, app_tbl, items * 4)) {
            *ret = FR_INVALID_PARAMETER;
            return true;
        }
        if (items != s->tbl_items) {
            DWORD *t = realloc(s->tbl, items * sizeof(DWORD));
            if (!t) {
                *ret = FR_NOT_ENOUGH_CORE;
                return true;
            }
            s->tbl = t;
            s->tbl_items = items;
        }
        s->tbl[0] = items;
        s->fil.cltbl = s->tbl;
        const FRESULT r = f_lseek(&s->fil, CREATE_LINKMAP);
        /* The first word is what the app reads: used, or needed. */
        const uint32_t first = (uint32_t)s->tbl[0];
        memcpy((void *)(uintptr_t)app_tbl, &first, 4);
        s->tbl_ok = r == FR_OK;
        s->tbl_app = s->tbl_ok ? app_tbl : 0;
        if (!s->tbl_ok) {
            s->fil.cltbl = NULL;
        }
        fil_out(s);
        *ret = r;
        return true;
    }
    fil_cltbl(w, s);
#endif
    *ret = f_lseek(&s->fil, ofs);
    fil_out(s);
    return true;
}

static void fil_close(int i)
{
    f_close(&s_fil[i]->fil);
    free(s_fil[i]->tbl);
    free(s_fil[i]);
    s_fil[i] = NULL;
}

H(c_f_close)
{
    (void)w;
    for (int i = 0; i < MAX_FIL; i++) {
        if (s_fil[i] && s_fil[i]->app == a[0]) {
            fil_close(i);
            *ret = FR_OK;
            return true;
        }
    }
    *ret = FR_INVALID_OBJECT;
    return true;
}

/* ---- stdio ---------------------------------------------------------------- */

static FILE *file_of(uint32_t t)
{
    const uint32_t i = t - TOKEN_FILE;
    return (t >= TOKEN_FILE && i < MAX_FILE) ? s_file[i] : NULL;
}

H(c_fopen)
{
    const char *path = str(w, a[0], PATH_MAX_BOX);
    const char *mode = str(w, a[1], 8);
    *ret = 0;
    if (!path || !mode) {
        return true;
    }
    int i = 0;
    while (i < MAX_FILE && s_file[i]) i++;
    if (i == MAX_FILE) {
        return true;
    }
    s_file[i] = fopen(path, mode);
    if (s_file[i]) {
        *ret = TOKEN_FILE + (uint32_t)i;
    }
    return true;
}

H(c_fread)
{
    FILE *f = file_of(a[3]);
    const uint64_t len = (uint64_t)a[1] * a[2];
    *ret = 0;
    if (!f || len == 0 || len > UINT32_MAX || !ok_ptr(w, a[0], (uint32_t)len)) {
        return true;
    }
    uint8_t *const st = stage();
    if (!st) {
        *ret = (uint32_t)fread((void *)(uintptr_t)a[0], a[1], a[2], f);
        return true;
    }
    /* In slices through internal RAM, then the count of whole items, as
     * fread() gives it. */
    uint32_t done = 0;
    while (done < (uint32_t)len) {
        const uint32_t want = (uint32_t)len - done > READ_SLICE ? READ_SLICE : (uint32_t)len - done;
        const size_t g = fread(st, 1, want, f);
        memcpy((uint8_t *)(uintptr_t)a[0] + done, st, g);
        done += (uint32_t)g;
        if (g < want) {
            break;
        }
    }
    *ret = done / a[1];
    return true;
}

H(c_fgets)
{
    FILE *f = file_of(a[2]);
    const int n = (int)a[1];
    *ret = 0;
    if (!f || n <= 0 || !ok_ptr(w, a[0], (uint32_t)n)) {
        return true;
    }
    *ret = fgets((char *)(uintptr_t)a[0], n, f) ? a[0] : 0;
    return true;
}

/* long is 32 bits in the app. */
H(c_fseek)
{
    (void)w;
    FILE *f = file_of(a[0]);
    *ret = f ? (uint32_t)fseek(f, (long)(int32_t)a[1], (int)a[2]) : (uint32_t)-1;
    return true;
}

H(c_ftell)
{
    (void)w;
    FILE *f = file_of(a[0]);
    long p = f ? ftell(f) : -1;
    if (p > INT32_MAX) {
        p = -1;                             /* does not fit the app's long */
    }
    *ret = (uint32_t)(int32_t)p;
    return true;
}

H(c_fclose)
{
    (void)w;
    FILE *f = file_of(a[0]);
    if (!f) {
        *ret = (uint32_t)EOF;
        return true;
    }
    s_file[a[0] - TOKEN_FILE] = NULL;
    *ret = (uint32_t)fclose(f);
    return true;
}

/* ---- directories ---------------------------------------------------------- */

/* A buffer in the box of len bytes at *slot, (re)allocated if the app has
 * freed it. 0 if the box is full. */
static uint32_t box_buf(win_t *w, uint32_t *slot, uint32_t len)
{
    feckless_box_t *b = box_of_win(w);
    if (*slot && feckless_box_len(b, *slot) == len) {
        return *slot;
    }
    *slot = (uint32_t)feckless_box_alloc(b, len);
    return *slot;
}

H(c_opendir)
{
    const char *path = str(w, a[0], PATH_MAX_BOX);
    *ret = 0;
    if (!path) {
        return true;
    }
    int i = 0;
    while (i < MAX_DIR && s_dir[i]) i++;
    if (i == MAX_DIR || !box_buf(w, &s_dirent[i], sizeof(struct dirent))) {
        return true;
    }
    s_dir[i] = opendir(path);
    if (s_dir[i]) {
        *ret = TOKEN_DIR + (uint32_t)i;
    }
    return true;
}

H(c_readdir)
{
    const uint32_t i = a[0] - TOKEN_DIR;
    *ret = 0;
    if (a[0] < TOKEN_DIR || i >= MAX_DIR || !s_dir[i]) {
        return true;
    }
    const struct dirent *e = readdir(s_dir[i]);
    if (!e || !box_buf(w, &s_dirent[i], sizeof(struct dirent))) {
        return true;
    }
    memcpy((void *)(uintptr_t)s_dirent[i], e, sizeof(struct dirent));
    *ret = s_dirent[i];
    return true;
}

H(c_closedir)
{
    (void)w;
    const uint32_t i = a[0] - TOKEN_DIR;
    if (a[0] < TOKEN_DIR || i >= MAX_DIR || !s_dir[i]) {
        *ret = (uint32_t)-1;
        return true;
    }
    *ret = (uint32_t)closedir(s_dir[i]);
    s_dir[i] = NULL;                        /* the dirent buffer stays the slot's */
    return true;
}

/* ---- the storage handler -------------------------------------------------- */

H(c_storage_present) { (void)w; *ret = a[0] < STORAGE_COUNT && storage_present((storage_id_t)a[0]); return true; }
H(c_storage_ff_drive) { (void)w; *ret = a[0] < STORAGE_COUNT ? (uint32_t)storage_ff_drive((storage_id_t)a[0]) : (uint32_t)-1; return true; }
H(c_storage_generation) { (void)w; (void)a; *ret = storage_generation(); return true; }
H(c_storage_of_path)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    *ret = p ? (uint32_t)storage_of_path(p) : (uint32_t)STORAGE_COUNT;
    return true;
}
/* The host's string, copied into the box: the app may read it, not the host's. */
H(c_storage_mount_path)
{
    *ret = 0;
    if (a[0] >= STORAGE_COUNT) {
        return true;
    }
    const char *m = storage_mount_path((storage_id_t)a[0]);
    if (!m) {
        return true;
    }
    const uint32_t len = (uint32_t)strlen(m) + 1;
    const uint32_t p = box_buf(w, &s_mount[a[0]], len);
    if (p) {
        memcpy((void *)(uintptr_t)p, m, len);
        *ret = p;
    }
    return true;
}
H(c_storage_join_path)
{
    const char *dir = str(w, a[2], PATH_MAX_BOX);
    const char *name = str(w, a[3], PATH_MAX_BOX);
    *ret = 0;
    if (!dir || !name || a[1] == 0 || a[1] > PATH_MAX_BOX || !ok_ptr(w, a[0], a[1])) {
        return true;
    }
    char out[PATH_MAX_BOX];
    if (storage_join_path(out, a[1], dir, name)) {
        memcpy((void *)(uintptr_t)a[0], out, strlen(out) + 1);
        *ret = 1;
    }
    return true;
}
H(c_storage_io_acquire)
{
    (void)w;
    storage_io_acquire((storage_io_class_t)a[0]);
    s_io_depth++;
    *ret = 0;
    return true;
}
H(c_storage_io_release)
{
    (void)w;
    (void)a;
    if (s_io_depth > 0) {                   /* never the launcher's own lease */
        s_io_depth--;
        storage_io_release();
    }
    *ret = 0;
    return true;
}
H(c_storage_hold)
{
    (void)w;
    storage_hold(a[0] < STORAGE_COUNT ? (storage_id_t)a[0] : STORAGE_COUNT);
    s_held = a[0] < STORAGE_COUNT;
    *ret = 0;
    return true;
}

static const uint32_t s_storage_v2 = 2;

const feckless_umode_call_t box_storage_calls[] = {
    { "feckless_abi_storage_v2", NULL, &s_storage_v2, sizeof s_storage_v2, 0 },
    FN(f_open), FN(f_read), FN(f_lseek), FN(f_close),
    FN(fopen), FN(fread), FN(fgets), FN(fseek), FN(ftell), FN(fclose),
    FN(opendir), FN(readdir), FN(closedir),
    FN(storage_present), FN(storage_ff_drive), FN(storage_generation), FN(storage_of_path),
    FN(storage_mount_path), FN(storage_join_path), FN(storage_io_acquire), FN(storage_io_release),
    FN(storage_hold),
};
const uint32_t box_storage_calls_n = sizeof box_storage_calls / sizeof box_storage_calls[0];

void box_storage_end(void)
{
    /* s_stage is kept: the next app reads through it too. */
    for (int i = 0; i < MAX_FIL; i++) {
        if (s_fil[i]) fil_close(i);
    }
    for (int i = 0; i < MAX_FILE; i++) {
        if (s_file[i]) {
            fclose(s_file[i]);
            s_file[i] = NULL;
        }
    }
    for (int i = 0; i < MAX_DIR; i++) {
        if (s_dir[i]) {
            closedir(s_dir[i]);
            s_dir[i] = NULL;
        }
        s_dirent[i] = 0;                    /* the box goes with the app */
    }
    memset(s_mount, 0, sizeof s_mount);
    while (s_io_depth > 0) {
        s_io_depth--;
        storage_io_release();
    }
    if (s_held) {
        storage_hold(STORAGE_COUNT);
        s_held = false;
    }
}
