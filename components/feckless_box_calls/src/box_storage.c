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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "esp_heap_caps.h"
#include "ff.h"
#include "storage.h"
#include "storage_io.h"

#include "box_calls_priv.h"

#define MAX_FIL   8
#define MAX_FILE  16                        /* u-feckless-0047: was 8; Defeatist keeps more open */
#define MAX_DIR   4
#define MAX_FFDIR 4                         /* u-feckless-0047: FatFs's own directories */
#define CLMT_MAX  16384u                    /* items; Spurious asks for at most this */
#define PATH_MAX_BOX 1024u
#define TOKEN_FILE 0x46494c00u              /* "FIL\0" + slot: a FILE * */
#define TOKEN_DIR  0x44495200u              /* "DIR\0" + slot: a DIR * */
#define TOKEN_FD   0x46440000u              /* "FD\0\0" + slot: fileno() of a FILE, u-feckless-0050 */

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
static bool s_file_io[MAX_FILE];            /* u-feckless-0047: storage_io_open()'s, closed by its close */
static struct { uint32_t app; FF_DIR dir; } *s_ffdir[MAX_FFDIR];   /* by the app's FF_DIR, as a FIL is */
static uint32_t s_label[STORAGE_COUNT];     /* the box copy of each label */
static int s_brief[STORAGE_COUNT];          /* the app's storage_hold_brief()s not released */
static bool s_held_bg;
static uint32_t s_readers;
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

/* u-feckless-0050: a host call's int result for the app, its errno set
 * from the host's when it failed; and a refusal before any call, with the
 * errno it stands for (EFAULT a pointer, EBADF a number not given). */
static uint32_t rc(win_t *w, int r)
{
    if (r != 0) box_set_errno(w, errno);
    return (uint32_t)r;
}
static uint32_t refuse(win_t *w, int e, uint32_t r)
{
    box_set_errno(w, e);
    return r;
}

H(c_fopen)
{
    const char *path = str(w, a[0], PATH_MAX_BOX);
    const char *mode = str(w, a[1], 8);
    *ret = 0;
    if (!path || !mode) {
        refuse(w, EFAULT, 0);
        return true;
    }
    int i = 0;
    while (i < MAX_FILE && s_file[i]) i++;
    if (i == MAX_FILE) {
        refuse(w, EMFILE, 0);
        return true;
    }
    s_file[i] = fopen(path, mode);
    if (s_file[i]) {
        *ret = TOKEN_FILE + (uint32_t)i;
    } else {
        box_set_errno(w, errno);
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
    FILE *f = file_of(a[0]);
    *ret = f ? rc(w, fseek(f, (long)(int32_t)a[1], (int)a[2])) : refuse(w, EBADF, (uint32_t)-1);
    return true;
}

H(c_ftell)
{
    FILE *f = file_of(a[0]);
    long p = f ? ftell(f) : -1;
    if (!f) {
        box_set_errno(w, EBADF);
    } else if (p < 0) {
        box_set_errno(w, errno);
    } else if (p > INT32_MAX) {
        p = -1;                             /* does not fit the app's long */
        box_set_errno(w, EOVERFLOW);
    }
    *ret = (uint32_t)(int32_t)p;
    return true;
}

H(c_fclose)
{
    FILE *f = file_of(a[0]);
    if (!f) {
        *ret = refuse(w, EBADF, (uint32_t)EOF);
        return true;
    }
    const uint32_t i = a[0] - TOKEN_FILE;
    s_file[i] = NULL;
    *ret = rc(w, s_file_io[i] ? storage_io_close(f) : fclose(f));
    s_file_io[i] = false;
    return true;
}

/* ---- u-feckless-0047: writing, and the rest of stdio and the paths -------
 * A write reaches the card from internal RAM, as a read does (0015): stdio
 * hands a request at least its buffer's size straight to FatFs, which
 * would DMA it out of the box in PSRAM while the panel scans the
 * framebuffer there. So it is copied into the staging buffer a slice at a
 * time and written from that.
 *
 * Paths are anything the app names, for writing as for reading: the box
 * keeps the app out of the host's memory, not out of the card's files. */

H(c_fwrite)
{
    FILE *f = file_of(a[3]);
    const uint64_t len = (uint64_t)a[1] * a[2];
    *ret = 0;
    if (!f || len == 0 || len > UINT32_MAX || !ok_ptr(w, a[0], (uint32_t)len)) {
        return true;
    }
    uint8_t *const st = stage();
    if (!st) {
        *ret = (uint32_t)fwrite((const void *)(uintptr_t)a[0], a[1], a[2], f);
        return true;
    }
    uint32_t done = 0;
    while (done < (uint32_t)len) {
        const uint32_t n = (uint32_t)len - done > READ_SLICE ? READ_SLICE : (uint32_t)len - done;
        memcpy(st, (const uint8_t *)(uintptr_t)a[0] + done, n);
        const size_t put = fwrite(st, 1, n, f);
        done += (uint32_t)put;
        if (put < n) {
            break;
        }
    }
    *ret = done / a[1];
    return true;
}

/* fflush(NULL) flushes the app's own files, not every stream the host has. */
H(c_fflush)
{
    if (a[0] == 0) {
        int r = 0;
        for (int i = 0; i < MAX_FILE; i++) {
            if (s_file[i] && fflush(s_file[i]) != 0) r = EOF;
        }
        *ret = rc(w, r);
        return true;
    }
    FILE *f = file_of(a[0]);
    *ret = f ? rc(w, fflush(f)) : refuse(w, EBADF, (uint32_t)EOF);
    return true;
}

H(c_rewind)
{
    (void)w;
    FILE *f = file_of(a[0]);
    if (f) rewind(f);
    *ret = 0;
    return true;
}

H(c_remove)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    *ret = p ? rc(w, remove(p)) : refuse(w, EFAULT, (uint32_t)-1);
    return true;
}
H(c_rename)
{
    const char *from = str(w, a[0], PATH_MAX_BOX), *to = str(w, a[1], PATH_MAX_BOX);
    *ret = (from && to) ? rc(w, rename(from, to)) : refuse(w, EFAULT, (uint32_t)-1);
    return true;
}
H(c_mkdir)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    *ret = p ? rc(w, mkdir(p, (mode_t)a[1])) : refuse(w, EFAULT, (uint32_t)-1);
    return true;
}
H(c_rmdir)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    *ret = p ? rc(w, rmdir(p)) : refuse(w, EFAULT, (uint32_t)-1);
    return true;
}
/* (path, struct stat *): one picolibc on both sides, so one layout */
H(c_stat)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    struct stat st;
    if (!p || !ok_ptr(w, a[1], sizeof st)) {
        *ret = refuse(w, EFAULT, (uint32_t)-1);
        return true;
    }
    const int r = stat(p, &st);
    if (r == 0) memcpy((void *)(uintptr_t)a[1], &st, sizeof st);
    *ret = rc(w, r);
    return true;
}
/* (path, const struct utimbuf * or NULL for now) */
H(c_utime)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    struct utimbuf t;
    if (!p || (a[1] && !ok_ptr(w, a[1], sizeof t))) {
        *ret = refuse(w, EFAULT, (uint32_t)-1);
        return true;
    }
    if (a[1]) memcpy(&t, (const void *)(uintptr_t)a[1], sizeof t);
    *ret = rc(w, utime(p, a[1] ? &t : NULL));
    return true;
}

/* ---- u-feckless-0050: the rest of stdio a writer uses ---------------------
 * Jinxed Writer's log (its doclog.c) is fflush()ed and fsync(fileno())ed
 * after every append, checked with ferror(), and its export put in place
 * with unlink() and rename(). fileno() gives a number of its own, TOKEN_FD
 * + the FILE's slot, which only fsync() takes: no other fd call is a box's,
 * and a FILE number is not an fd. */

H(c_ferror)
{
    (void)w;
    FILE *f = file_of(a[0]);
    *ret = f ? (uint32_t)(ferror(f) != 0) : 1u;
    return true;
}
H(c_feof)
{
    (void)w;
    FILE *f = file_of(a[0]);
    *ret = f ? (uint32_t)(feof(f) != 0) : 0u;
    return true;
}
H(c_clearerr)
{
    (void)w;
    FILE *f = file_of(a[0]);
    if (f) clearerr(f);
    *ret = 0;
    return true;
}
H(c_fileno)
{
    *ret = file_of(a[0]) ? TOKEN_FD + (a[0] - TOKEN_FILE) : refuse(w, EBADF, (uint32_t)-1);
    return true;
}
H(c_fsync)
{
    const uint32_t i = a[0] - TOKEN_FD;
    FILE *f = (a[0] >= TOKEN_FD && i < MAX_FILE) ? s_file[i] : NULL;
    *ret = f ? rc(w, fsync(fileno(f))) : refuse(w, EBADF, (uint32_t)-1);
    return true;
}
H(c_unlink)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    *ret = p ? rc(w, unlink(p)) : refuse(w, EFAULT, (uint32_t)-1);
    return true;
}

/* ---- u-feckless-0047: storage_io's files --------------------------------
 * storage_io_open() is fopen() with a large DMA-capable buffer from its
 * pool; the app gets a FILE number like fopen()'s, and either close
 * gives the pool slot back. The reads go through the staging buffer, a
 * slice at a time, each under storage_io's lease for the class asked. */

H(c_storage_io_open)
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
    s_file[i] = storage_io_open(path, mode);
    if (s_file[i]) {
        s_file_io[i] = true;
        *ret = TOKEN_FILE + (uint32_t)i;
    }
    return true;
}
H(c_storage_io_close) { return c_fclose(w, a, ret); }

/* Into the box's dst from the stream's position, through the staging
 * buffer; the bytes read. */
static uint32_t io_read(FILE *f, uint8_t *dst, uint32_t len, storage_io_class_t cls)
{
    uint8_t *const st = stage();
    if (!st) {
        return (uint32_t)storage_io_fread(dst, len, f, cls);
    }
    uint32_t done = 0;
    while (done < len) {
        const uint32_t want = len - done > READ_SLICE ? READ_SLICE : len - done;
        const size_t g = storage_io_fread(st, want, f, cls);
        memcpy(dst + done, st, g);
        done += (uint32_t)g;
        if (g < want) {
            break;
        }
    }
    return done;
}

/* (dst, len, f, class) */
H(c_storage_io_fread)
{
    FILE *f = file_of(a[2]);
    *ret = 0;
    if (!f || a[1] == 0 || !ok_ptr(w, a[0], a[1])) {
        return true;
    }
    *ret = io_read(f, (uint8_t *)(uintptr_t)a[0], a[1], (storage_io_class_t)a[3]);
    return true;
}

/* (f, int64_t off in a1/a2, dst, len, class): all of it, or false */
H(c_storage_io_read_at)
{
    FILE *f = file_of(a[0]);
    const int64_t off = (int64_t)((uint64_t)a[2] << 32 | a[1]);
    *ret = false;
    if (!f || off < 0 || !ok_ptr(w, a[3], a[4])) {
        return true;
    }
    if (a[4] == 0) {
        *ret = true;
        return true;
    }
    uint8_t *const st = stage();
    uint8_t *const dst = (uint8_t *)(uintptr_t)a[3];
    const storage_io_class_t cls = (storage_io_class_t)a[5];
    if (!st) {
        *ret = storage_io_read_at(f, off, dst, a[4], cls);
        return true;
    }
    /* The first slice where asked, the rest on from it. */
    const uint32_t first = a[4] > READ_SLICE ? READ_SLICE : a[4];
    if (!storage_io_read_at(f, off, st, first, cls)) {
        return true;
    }
    memcpy(dst, st, first);
    *ret = first == a[4] || io_read(f, dst + first, a[4] - first, cls) == a[4] - first;
    return true;
}
H(c_storage_io_phase_ms) { (void)w; (void)a; *ret = storage_io_phase_ms(); return true; }
H(c_storage_io_report)
{
    const char *phase = str(w, a[0], 64);
    storage_io_report(phase ? phase : "app");
    *ret = 0;
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

H(c_rewinddir)                              /* u-feckless-0047 */
{
    (void)w;
    const uint32_t i = a[0] - TOKEN_DIR;
    if (a[0] >= TOKEN_DIR && i < MAX_DIR && s_dir[i]) rewinddir(s_dir[i]);
    *ret = 0;
    return true;
}

/* ---- u-feckless-0047: FatFs's directories ---------------------------------
 * As a FIL: the real FF_DIR is the host's (it points at the volume's FATFS),
 * found by the address of the app's, and nothing of it is copied back. A
 * FILINFO holds no pointer and is copied into the box whole. */

static int ffdir_of(uint32_t app)
{
    for (int i = 0; i < MAX_FFDIR; i++) {
        if (s_ffdir[i] && s_ffdir[i]->app == app) return i;
    }
    return -1;
}

H(c_f_opendir)
{
    const char *path = str(w, a[1], PATH_MAX_BOX);
    if (!ok_ptr(w, a[0], sizeof(FF_DIR)) || !path) {
        *ret = FR_INVALID_PARAMETER;
        return true;
    }
    if (ffdir_of(a[0]) >= 0) {
        *ret = FR_INVALID_OBJECT;           /* that FF_DIR is open: close it first */
        return true;
    }
    int i = 0;
    while (i < MAX_FFDIR && s_ffdir[i]) i++;
    if (i == MAX_FFDIR) {
        *ret = FR_TOO_MANY_OPEN_FILES;
        return true;
    }
    s_ffdir[i] = calloc(1, sizeof *s_ffdir[i]);
    if (!s_ffdir[i]) {
        *ret = FR_NOT_ENOUGH_CORE;
        return true;
    }
    const FRESULT r = f_opendir(&s_ffdir[i]->dir, path);
    if (r != FR_OK) {
        free(s_ffdir[i]);
        s_ffdir[i] = NULL;
    } else {
        s_ffdir[i]->app = a[0];
    }
    *ret = r;
    return true;
}
/* (dp, fno or NULL to rewind) */
H(c_f_readdir)
{
    const int i = ffdir_of(a[0]);
    if (i < 0) {
        *ret = FR_INVALID_OBJECT;
        return true;
    }
    if (!a[1]) {
        *ret = f_readdir(&s_ffdir[i]->dir, NULL);
        return true;
    }
    if (!ok_ptr(w, a[1], sizeof(FILINFO))) {
        *ret = FR_INVALID_PARAMETER;
        return true;
    }
    FILINFO fi;
    const FRESULT r = f_readdir(&s_ffdir[i]->dir, &fi);
    if (r == FR_OK) memcpy((void *)(uintptr_t)a[1], &fi, sizeof fi);
    *ret = r;
    return true;
}
static void ffdir_close(int i)
{
    f_closedir(&s_ffdir[i]->dir);
    free(s_ffdir[i]);
    s_ffdir[i] = NULL;
}
H(c_f_closedir)
{
    (void)w;
    const int i = ffdir_of(a[0]);
    if (i < 0) {
        *ret = FR_INVALID_OBJECT;
        return true;
    }
    ffdir_close(i);
    *ret = FR_OK;
    return true;
}
/* (path, attr, mask) */
H(c_f_chmod)
{
    const char *path = str(w, a[0], PATH_MAX_BOX);
    *ret = path ? f_chmod(path, (BYTE)a[1], (BYTE)a[2]) : FR_INVALID_PARAMETER;
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

/* ---- u-feckless-0047: the rest of the storage handler -------------------- */

H(c_storage_label)
{
    *ret = 0;
    if (a[0] >= STORAGE_COUNT) {
        return true;
    }
    const char *l = storage_label((storage_id_t)a[0]);
    if (!l) {
        return true;
    }
    const uint32_t len = (uint32_t)strlen(l) + 1;
    const uint32_t p = box_buf(w, &s_label[a[0]], len);
    if (p) {
        memcpy((void *)(uintptr_t)p, l, len);
        *ret = p;
    }
    return true;
}
H(c_storage_is_hidden)
{
    const char *n = str(w, a[0], PATH_MAX_BOX);
    *ret = n && storage_is_hidden(n);
    return true;
}
H(c_storage_mark_hidden)
{
    const char *p = str(w, a[0], PATH_MAX_BOX);
    if (p) storage_mark_hidden(p);
    *ret = 0;
    return true;
}
H(c_storage_sd_info)
{
    storage_sd_info_t i;
    if (ok_ptr(w, a[0], sizeof i)) {
        storage_sd_info(&i);
        memcpy((void *)(uintptr_t)a[0], &i, sizeof i);
    }
    *ret = 0;
    return true;
}
H(c_storage_usb_info)
{
    storage_usb_info_t i;
    if (ok_ptr(w, a[0], sizeof i)) {
        storage_usb_info(&i);
        memcpy((void *)(uintptr_t)a[0], &i, sizeof i);
    }
    *ret = 0;
    return true;
}
H(c_storage_usb_busy) { (void)w; (void)a; *ret = storage_usb_busy(); return true; }
H(c_storage_usb_power) { (void)w; *ret = storage_usb_power(a[0] != 0); return true; }
H(c_storage_usb_powered) { (void)w; (void)a; *ret = storage_usb_powered(); return true; }
/* The holds are given back at the end if the app left them taken, as
 * storage_hold()'s is. */
H(c_storage_hold_background)
{
    (void)w;
    storage_hold_background(a[0] < STORAGE_COUNT ? (storage_id_t)a[0] : STORAGE_COUNT);
    s_held_bg = a[0] < STORAGE_COUNT;
    *ret = 0;
    return true;
}
H(c_storage_hold_readers)
{
    (void)w;
    s_readers = a[0] & ((1u << STORAGE_COUNT) - 1u);
    storage_hold_readers(s_readers);
    *ret = 0;
    return true;
}
H(c_storage_hold_brief)
{
    (void)w;
    if (a[0] < STORAGE_COUNT) {
        storage_hold_brief((storage_id_t)a[0]);
        s_brief[a[0]]++;
    }
    *ret = 0;
    return true;
}
/* never one the app did not take: the count is per volume, and shared */
H(c_storage_release_brief)
{
    (void)w;
    if (a[0] < STORAGE_COUNT && s_brief[a[0]] > 0) {
        s_brief[a[0]]--;
        storage_release_brief((storage_id_t)a[0]);
    }
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
    /* u-feckless-0047 */
    FN(fwrite), FN(fflush), FN(rewind), FN(remove), FN(rename), FN(mkdir), FN(rmdir), FN(stat),
    FN(utime), FN(rewinddir), FN(f_opendir), FN(f_readdir), FN(f_closedir), FN(f_chmod),
    FN(storage_io_open), FN(storage_io_close), FN(storage_io_fread), FN(storage_io_read_at),
    FN(storage_io_phase_ms), FN(storage_io_report),
    FN(storage_label), FN(storage_is_hidden), FN(storage_mark_hidden), FN(storage_sd_info),
    FN(storage_usb_info), FN(storage_usb_busy), FN(storage_usb_power), FN(storage_usb_powered),
    FN(storage_hold_background), FN(storage_hold_readers), FN(storage_hold_brief),
    FN(storage_release_brief),
    /* u-feckless-0050 */
    FN(ferror), FN(feof), FN(clearerr), FN(fileno), FN(fsync), FN(unlink),
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
            if (s_file_io[i]) storage_io_close(s_file[i]);
            else fclose(s_file[i]);
            s_file[i] = NULL;
        }
        s_file_io[i] = false;
    }
    for (int i = 0; i < MAX_FFDIR; i++) {
        if (s_ffdir[i]) ffdir_close(i);
    }
    memset(s_label, 0, sizeof s_label);
    for (int v = 0; v < STORAGE_COUNT; v++) {
        while (s_brief[v] > 0) {
            s_brief[v]--;
            storage_release_brief((storage_id_t)v);
        }
    }
    if (s_held_bg) {
        storage_hold_background(STORAGE_COUNT);
        s_held_bg = false;
    }
    if (s_readers) {
        storage_hold_readers(0);
        s_readers = 0;
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
