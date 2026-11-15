/* SPDX-License-Identifier: MIT
 *
 * Host test for box_storage.c as it is, playing Spurious's own calls
 * (sd_io_fatfs.c's open with fast seek, its chunked reads; stardict.c's
 * fopen/fgets/fread; main.c's dictionary scan) through the box's ext hook,
 * against a stand-in FatFs and storage handler over real files, and the
 * host's stdio and dirent.
 *
 *   make -C components/feckless_box_calls/test
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "feckless_box_calls.h"
#include "ff.h"
#include "storage.h"   /* u-feckless-0047: the info structs */
#include <utime.h>

extern int io_depth, hold_id, fat_open;
extern DWORD *last_cltbl;
extern uintptr_t box_lo, box_hi;
extern int reads_into_box, reads_total;

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define MEM (256 * 1024)
static uint8_t *mem;
static feckless_box_t box;
static feckless_umode_window_t w;
static feckless_umode_calls_t tab;
static const feckless_umode_call_t *calls_tab;
static uint32_t ncalls;
static char dir[64];

static uint32_t nr_of(const char *name)
{
    for (uint32_t i = 0; i < ncalls; i++)
        if (!strcmp(calls_tab[i].name, name)) return FECKLESS_SYS_EXT_BASE + i;
    printf("FAIL no call %s\n", name);
    failed++;
    return 0;
}
static uint32_t call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    w.ctx.x[10] = a0; w.ctx.x[11] = a1; w.ctx.x[12] = a2; w.ctx.x[13] = a3;
    uint32_t ret = 0xdeadbeef;
    CHECK(feckless_umode_calls_ext(&w, nr_of(name), &w.ctx.x[10], &ret));
    return ret;
}
static uint32_t call6(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    w.ctx.x[14] = a4; w.ctx.x[15] = a5;
    return call(name, a0, a1, a2, a3);
}
static uint32_t P(const void *p) { return (uint32_t)(uintptr_t)p; }
static void *box_alloc(uint32_t n) { void *p = (void *)(uintptr_t)feckless_box_alloc(&box, n); memset(p, 0, n); return p; }
static char *put_str(const char *s) { char *p = box_alloc((uint32_t)strlen(s) + 1); strcpy(p, s); return p; }

static void make_files(void)
{
    snprintf(dir, sizeof dir, "/tmp/boxst.%d", (int)getpid());
    mkdir(dir, 0700);
    char p[128];
    snprintf(p, sizeof p, "%s/words.dict", dir);
    FILE *f = fopen(p, "wb");
    for (int i = 0; i < 20000; i++) fputc('a' + i % 26, f);
    fclose(f);
    snprintf(p, sizeof p, "%s/words.ifo", dir);
    f = fopen(p, "w");
    fputs("StarDict's dict ifo file\nversion=2.4.2\nwordcount=42\n", f);
    fclose(f);
}

/* sd_io_fatfs.c's sd_file_open(), clmt_build() and sd_file_read(), as calls. */
static void test_fatfs(void)
{
    FIL *fp = box_alloc(sizeof(FIL));
    char *path = put_str("");
    char pbuf[128];
    snprintf(pbuf, sizeof pbuf, "%s/words.dict", dir);
    path = put_str(pbuf);

    CHECK(call("f_open", P(&box), P(path), FA_READ, 0) == FR_INVALID_PARAMETER);   /* FIL outside */
    CHECK(call("f_open", P(fp), P(&box), FA_READ, 0) == FR_INVALID_PARAMETER);    /* path outside */
    CHECK(call("f_open", P(fp), P(path), FA_READ, 0) == FR_OK);
    CHECK(fat_open == 1);
    CHECK(fp->obj.objsize == 20000);                                /* f_size() reads it */
    CHECK(fp->obj.fs == NULL);                                      /* the host's FATFS stays the host's */
    CHECK(call("f_open", P(fp), P(path), FA_READ, 0) == FR_INVALID_OBJECT);       /* already open */

    /* clmt_build(): 256 items, too few; then what FatFs asked for */
    DWORD *tbl = box_alloc(256 * 4);
    tbl[0] = 256;
    fp->cltbl = tbl;
    CHECK(call("f_lseek", P(fp), 0xffffffffu, 0xffffffffu, 0) == FR_NOT_ENOUGH_CORE);
    CHECK(tbl[0] == 300);                                           /* the app reads what is needed */
    CHECK(last_cltbl != tbl);                                       /* FatFs never saw the app's table */
    fp->cltbl = NULL;
    DWORD *tbl2 = box_alloc(300 * 4);
    tbl2[0] = 300;
    fp->cltbl = tbl2;
    CHECK(call("f_lseek", P(fp), 0xffffffffu, 0xffffffffu, 0) == FR_OK);
    CHECK(tbl2[0] == 211);
    CHECK(last_cltbl && last_cltbl != tbl2);
    CHECK(call("f_lseek", P(fp), 0, 0, 0) == FR_OK);
    CHECK(last_cltbl != NULL);                                      /* fast seek in use */

    /* a table too big for the box, or claiming past its end */
    DWORD *tb = box_alloc(16);
    tb[0] = 100000;
    fp->cltbl = tb;
    CHECK(call("f_lseek", P(fp), 0xffffffffu, 0xffffffffu, 0) == FR_INVALID_PARAMETER);
    fp->cltbl = tbl2;                                               /* the built table is gone with that */
    CHECK(call("f_lseek", P(fp), 5, 0, 0) == FR_OK && last_cltbl == NULL);
    fp->cltbl = NULL;

    /* sd_file_read(): seek, read in chunks; a 64-bit offset in a1:a2 */
    uint8_t *dst = box_alloc(5000);
    UINT *got = box_alloc(sizeof(UINT));
    CHECK(call("storage_io_acquire", 0, 0, 0, 0) == 0 && io_depth == 1);
    CHECK(call("f_lseek", P(fp), 26 * 100 + 3, 0, 0) == FR_OK && fp->fptr == 2603);
    CHECK(call("f_read", P(fp), P(dst), 4096, P(got)) == FR_OK && *got == 4096);
    CHECK(dst[0] == 'a' + 3 && dst[25] == 'a' + 2 && fp->fptr == 2603 + 4096);
    CHECK(call("f_lseek", P(fp), 1, 1, 0) == FR_OK && fp->fptr == 20000);     /* 4 GiB + 1, clamped */
    CHECK(call("f_read", P(fp), P(dst), 4096, P(got)) == FR_OK && *got == 0);
    CHECK(call("f_read", P(fp), P(dst), 1u << 20, P(got)) == FR_INVALID_PARAMETER);   /* past the box */
    CHECK(call("f_read", P(fp), P(dst), 16, P(&box)) == FR_INVALID_PARAMETER);
    CHECK(call("storage_io_release", 0, 0, 0, 0) == 0 && io_depth == 0);
    CHECK(call("storage_io_release", 0, 0, 0, 0) == 0 && io_depth == 0);     /* never the launcher's */

    /* u-feckless-0015: a read larger than a slice, across slice edges,
     * lands right, and FatFs was never handed the box. */
    uint8_t *big = box_alloc(20000);
    CHECK(call("f_lseek", P(fp), 5, 0, 0) == FR_OK);
    reads_total = 0;
    CHECK(call("f_read", P(fp), P(big), 19000, P(got)) == FR_OK && *got == 19000);
    for (int i = 0; i < 19000; i++) if (big[i] != 'a' + (5 + i) % 26) { CHECK(!"f_read data"); break; }
    CHECK(reads_total == 3);                                        /* 8 KB + 8 KB + 2616 */
    CHECK(call("f_lseek", P(fp), 19990, 0, 0) == FR_OK);
    CHECK(call("f_read", P(fp), P(big), 9000, P(got)) == FR_OK && *got == 10);   /* short: stops */
    CHECK(reads_into_box == 0);

    FIL *other = box_alloc(sizeof(FIL));
    CHECK(call("f_read", P(other), P(dst), 16, P(got)) == FR_INVALID_OBJECT); /* never opened */
    CHECK(call("f_close", P(fp), 0, 0, 0) == FR_OK && fat_open == 0);
    CHECK(call("f_close", P(fp), 0, 0, 0) == FR_INVALID_OBJECT);
    CHECK(call("f_read", P(fp), P(dst), 16, P(got)) == FR_INVALID_OBJECT);
}

/* stardict.c: the .ifo by fgets, a whole file by fseek/ftell/fread. */
static void test_stdio(void)
{
    char pbuf[128];
    snprintf(pbuf, sizeof pbuf, "%s/words.ifo", dir);
    char *path = put_str(pbuf), *r = put_str("r"), *rb = put_str("rb");
    const uint32_t f = call("fopen", P(path), P(r), 0, 0);
    CHECK(f != 0 && (f < w.lo || f >= w.hi));                       /* a number, not in the box */
    char *line = box_alloc(64);
    CHECK(call("fgets", P(line), 64, f, 0) == P(line) && !strncmp(line, "StarDict's dict ifo file", 24));
    CHECK(call("fgets", P(line), 64, f, 0) == P(line) && !strcmp(line, "version=2.4.2\n"));
    CHECK(call("fgets", P(line), 1u << 20, f, 0) == 0);             /* buffer past the box */
    CHECK(call("fgets", P(&box), 64, f, 0) == 0);
    CHECK(call("fgets", P(line), 64, f + 1, 0) == 0);               /* not a FILE */
    CHECK(call("fclose", f, 0, 0, 0) == 0);
    CHECK(call("fclose", f, 0, 0, 0) == (uint32_t)EOF);
    CHECK(call("fgets", P(line), 64, f, 0) == 0);                   /* closed */

    snprintf(pbuf, sizeof pbuf, "%s/words.dict", dir);
    path = put_str(pbuf);
    const uint32_t g = call("fopen", P(path), P(rb), 0, 0);
    CHECK(call("fseek", g, 0, SEEK_END, 0) == 0 && call("ftell", g, 0, 0, 0) == 20000);
    CHECK(call("fseek", g, (uint32_t)-10, SEEK_END, 0) == 0 && call("ftell", g, 0, 0, 0) == 19990);
    CHECK(call("fseek", g, 0, SEEK_SET, 0) == 0);
    uint8_t *b = box_alloc(20000);
    CHECK(call("fread", P(b), 1, 20000, g) == 20000 && b[19999] == 'a' + 19999 % 26);
    for (int i = 0; i < 20000; i++) if (b[i] != 'a' + i % 26) { CHECK(!"fread data"); break; }
    /* whole items only: 3-byte items over 20000 bytes are 6666 */
    CHECK(call("fseek", g, 0, SEEK_SET, 0) == 0);
    CHECK(call("fread", P(b), 3, 7000, g) == 6666 && call("ftell", g, 0, 0, 0) == 20000);
    /* size * n is 2^32: 0 in 32 bits. Rewound, so a read would happen,
     * into 16 bytes with a canary after them. */
    CHECK(call("fseek", g, 0, SEEK_SET, 0) == 0);
    uint8_t *small = box_alloc(16), *canary = box_alloc(64);
    memset(canary, 0x5a, 64);
    CHECK(call("fread", P(small), 0x10000, 0x10000, g) == 0);       /* size * n overflows */
    for (int i = 0; i < 64; i++) CHECK(canary[i] == 0x5a);
    CHECK(call("ftell", g, 0, 0, 0) == 0);                          /* nothing was read */
    CHECK(call("fread", P(b), 1, 1u << 20, g) == 0);                /* past the box */
    CHECK(call("fclose", g, 0, 0, 0) == 0);
    char *nope = put_str("/nonexistent/x");
    CHECK(call("fopen", P(nope), P(r), 0, 0) == 0);
}

/* main.c's scan: opendir, readdir's d_name, storage_join_path. */
static void test_dirs(void)
{
    char *path = put_str(dir);
    const uint32_t d = call("opendir", P(path), 0, 0, 0);
    CHECK(d != 0 && (d < w.lo || d >= w.hi));
    int seen = 0, ifo = 0;
    uint32_t e;
    char *joined = box_alloc(256);
    while ((e = call("readdir", d, 0, 0, 0)) != 0) {
        CHECK(e >= w.lo && e + sizeof(struct dirent) <= w.hi);      /* in the box */
        const struct dirent *de = (const struct dirent *)(uintptr_t)e;
        seen++;
        if (!strcmp(de->d_name, "words.ifo")) {
            ifo++;
            CHECK(call("storage_join_path", P(joined), 256, P(path), P(de->d_name)) == 1);
            CHECK(!strcmp(joined + strlen(dir), "/words.ifo"));
        }
    }
    CHECK(seen >= 4 && ifo == 1);                                   /* . .. and the two files */
    CHECK(call("closedir", d, 0, 0, 0) == 0);
    CHECK(call("readdir", d, 0, 0, 0) == 0);
    CHECK(call("closedir", d, 0, 0, 0) == (uint32_t)-1);
    CHECK(call("storage_join_path", P(joined), 4, P(path), P(path)) == 0);    /* does not fit */
    CHECK(call("storage_join_path", P(joined), 1u << 20, P(path), P(path)) == 0);
    CHECK(call("opendir", P(&box), 0, 0, 0) == 0);
}

static void test_storage_handler(void)
{
    const uint32_t m = call("storage_mount_path", 0, 0, 0, 0);
    CHECK(m >= w.lo && m < w.hi && !strcmp((char *)(uintptr_t)m, "/sd"));   /* a copy in the box */
    CHECK(call("storage_mount_path", 0, 0, 0, 0) == m);             /* the same copy */
    feckless_box_free(&box, m);                                     /* the app frees it ... */
    const uint32_t m2 = call("storage_mount_path", 0, 0, 0, 0);
    CHECK(m2 && !strcmp((char *)(uintptr_t)m2, "/sd"));             /* ... and gets it back */
    CHECK(call("storage_mount_path", 9, 0, 0, 0) == 0);
    CHECK(call("storage_present", 0, 0, 0, 0) == 1 && call("storage_present", 1, 0, 0, 0) == 0);
    CHECK(call("storage_present", 99, 0, 0, 0) == 0);
    CHECK(call("storage_ff_drive", 1, 0, 0, 0) == 1 && call("storage_ff_drive", 5, 0, 0, 0) == (uint32_t)-1);
    char *sdp = put_str("/sd/dict/x.ifo");
    CHECK(call("storage_of_path", P(sdp), 0, 0, 0) == 0);
    CHECK(call("storage_of_path", P(&box), 0, 0, 0) == 2);          /* STORAGE_COUNT */
    CHECK(call("storage_generation", 0, 0, 0, 0) == 7);
    call("storage_hold", 0, 0, 0, 0);
    CHECK(hold_id == 0);
}

/* u-feckless-0047: writing, through the staging buffer; the paths. */
extern int io_open, io_cls, io_into_box, brief[2], bg, readers_mask, ffdir_open;
extern BYTE chmod_attr, chmod_mask;
extern char hidden_path[300], report_phase[64];

static void test_writes(void)
{
    char pbuf[128];
    snprintf(pbuf, sizeof pbuf, "%s/out.bin", dir);
    char *path = put_str(pbuf), *wb = put_str("wb"), *rb = put_str("rb");
    const uint32_t f = call("fopen", P(path), P(wb), 0, 0);
    CHECK(f != 0);
    uint8_t *b = box_alloc(20000);
    for (int i = 0; i < 20000; i++) b[i] = (uint8_t)(i * 7);
    CHECK(call("fwrite", P(b), 1, 20000, f) == 20000);              /* three slices */
    CHECK(call("fwrite", P(b), 4, 3, f) == 3);
    CHECK(call("fwrite", P(b), 0x10000, 0x10000, f) == 0);          /* size * n overflows */
    CHECK(call("fwrite", P(b), 1, 1u << 20, f) == 0);               /* past the box */
    CHECK(call("fwrite", 0x1000, 1, 16, f) == 0);
    CHECK(call("fflush", f, 0, 0, 0) == 0 && call("fflush", 0, 0, 0, 0) == 0);
    CHECK(call("fflush", f + 1, 0, 0, 0) == (uint32_t)EOF);
    CHECK(call("fclose", f, 0, 0, 0) == 0);
    struct stat *st = box_alloc(sizeof(struct stat));
    CHECK(call("stat", P(path), P(st), 0, 0) == 0 && st->st_size == 20012);
    CHECK(call("stat", P(path), 0x1000, 0, 0) == (uint32_t)-1);
    const uint32_t g = call("fopen", P(path), P(rb), 0, 0);
    uint8_t *back = box_alloc(20012);
    CHECK(call("fread", P(back), 1, 20012, g) == 20012 && !memcmp(back, b, 20000) && !memcmp(back + 20000, b, 12));
    call("rewind", g, 0, 0, 0);
    CHECK(call("ftell", g, 0, 0, 0) == 0);
    CHECK(call("fclose", g, 0, 0, 0) == 0);

    snprintf(pbuf, sizeof pbuf, "%s/moved.bin", dir);
    char *to = put_str(pbuf);
    CHECK(call("rename", P(path), P(to), 0, 0) == 0 && call("stat", P(path), P(st), 0, 0) == (uint32_t)-1);
    struct utimbuf *ut = box_alloc(sizeof(struct utimbuf));
    ut->actime = ut->modtime = 1000000000;
    CHECK(call("utime", P(to), P(ut), 0, 0) == 0);
    CHECK(call("stat", P(to), P(st), 0, 0) == 0 && st->st_mtime == 1000000000);
    CHECK(call("utime", P(to), 0x1000, 0, 0) == (uint32_t)-1);
    CHECK(call("remove", P(to), 0, 0, 0) == 0 && call("remove", P(to), 0, 0, 0) == (uint32_t)-1);
    snprintf(pbuf, sizeof pbuf, "%s/sub", dir);
    char *sub = put_str(pbuf);
    CHECK(call("mkdir", P(sub), 0777, 0, 0) == 0 && call("mkdir", P(sub), 0777, 0, 0) == (uint32_t)-1);
    CHECK(call("rmdir", P(sub), 0, 0, 0) == 0);
    CHECK(call("rename", P(sub), 0x1000, 0, 0) == (uint32_t)-1 && call("remove", 0x1000, 0, 0, 0) == (uint32_t)-1);

    /* rewinddir */
    const uint32_t d = call("opendir", P(put_str(dir)), 0, 0, 0);
    int n1 = 0, n2 = 0;
    while (call("readdir", d, 0, 0, 0)) n1++;
    call("rewinddir", d, 0, 0, 0);
    while (call("readdir", d, 0, 0, 0)) n2++;
    CHECK(n1 > 0 && n1 == n2);
    call("closedir", d, 0, 0, 0);
}

/* storage_io's files: a FILE number, reads through the staging buffer */
static void test_storage_io(void)
{
    char pbuf[128];
    snprintf(pbuf, sizeof pbuf, "%s/words.dict", dir);
    char *path = put_str(pbuf), *rb = put_str("rb");
    const uint32_t f = call("storage_io_open", P(path), P(rb), 0, 0);
    CHECK(f != 0 && io_open == 1);
    uint8_t *b = box_alloc(20000);
    io_into_box = 0;
    CHECK(call("storage_io_fread", P(b), 20000, f, 1) == 20000 && io_cls == 1 && io_into_box == 0);
    CHECK(b[19999] == 'a' + 19999 % 26);
    CHECK(call("storage_io_fread", P(b), 16, f, 0) == 0);            /* at the end */
    memset(b, 0, 20000);
    CHECK(call6("storage_io_read_at", f, 9000, 0, P(b), 11000, 0) == 1 && io_into_box == 0);
    CHECK(b[0] == 'a' + 9000 % 26 && b[10999] == 'a' + 19999 % 26);
    CHECK(call6("storage_io_read_at", f, 19990, 0, P(b), 11, 0) == 0);    /* one short */
    CHECK(call6("storage_io_read_at", f, 0, 0x80000000u, P(b), 4, 0) == 0); /* a negative offset */
    CHECK(call6("storage_io_read_at", f, 0, 0, 0x1000, 4, 0) == 0);
    CHECK(call6("storage_io_read_at", f, 5, 0, P(b), 0, 0) == 1);           /* nothing asked */
    CHECK(call("storage_io_close", f, 0, 0, 0) == 0 && io_open == 0);
    CHECK(call("storage_io_close", f, 0, 0, 0) == (uint32_t)EOF);
    const uint32_t g = call("storage_io_open", P(path), P(rb), 0, 0);
    CHECK(call("fclose", g, 0, 0, 0) == 0 && io_open == 0);              /* fclose gives the pool slot back too */
    CHECK(call("storage_io_phase_ms", 0, 0, 0, 0) == 1500);
    call("storage_io_report", P(put_str("open")), 0, 0, 0);
    CHECK(!strcmp(report_phase, "open"));
    call("storage_io_report", 0x1000, 0, 0, 0);
    CHECK(!strcmp(report_phase, "app"));
}

/* FatFs's directories: the host's FF_DIR, found by the app's */
static void test_ffdir(void)
{
    FF_DIR *dp = box_alloc(sizeof(FF_DIR));
    FILINFO *fi = box_alloc(sizeof(FILINFO));
    CHECK(call("f_opendir", P(dp), P(put_str(dir)), 0, 0) == FR_OK && ffdir_open == 1);
    CHECK(dp->obj.fs == NULL);                                       /* nothing of the host's copied back */
    CHECK(call("f_opendir", P(dp), P(put_str(dir)), 0, 0) == FR_INVALID_OBJECT);
    int n = 0, dict = 0;
    while (call("f_readdir", P(dp), P(fi), 0, 0) == FR_OK && fi->fname[0]) {
        n++;
        if (!strcmp(fi->fname, "words.dict")) dict++;
    }
    CHECK(n >= 2 && dict == 1);
    CHECK(call("f_readdir", P(dp), 0, 0, 0) == FR_OK);              /* rewind */
    CHECK(call("f_readdir", P(dp), P(fi), 0, 0) == FR_OK && fi->fname[0]);
    CHECK(call("f_readdir", P(dp), 0x1000, 0, 0) == FR_INVALID_PARAMETER);
    CHECK(call("f_readdir", P(fi), P(fi), 0, 0) == FR_INVALID_OBJECT);
    CHECK(call("f_closedir", P(dp), 0, 0, 0) == FR_OK && ffdir_open == 0);
    CHECK(call("f_closedir", P(dp), 0, 0, 0) == FR_INVALID_OBJECT);
    CHECK(call("f_opendir", P(dp), P(put_str("/nonexistent")), 0, 0) == FR_NO_PATH);
    CHECK(call("f_opendir", 0x1000, P(put_str(dir)), 0, 0) == FR_INVALID_PARAMETER);
    CHECK(call("f_chmod", P(put_str("/sd/x")), 0x02, 0x02, 0) == FR_OK && chmod_attr == 2 && chmod_mask == 2);
    CHECK(call("f_chmod", 0x1000, 2, 2, 0) == FR_INVALID_PARAMETER);
}

static void test_more_handler(void)
{
    const uint32_t l = call("storage_label", 1, 0, 0, 0);
    CHECK(l >= w.lo && l < w.hi && !strcmp((char *)(uintptr_t)l, "USB drive"));
    CHECK(call("storage_label", 7, 0, 0, 0) == 0);
    CHECK(call("storage_is_hidden", P(put_str(".settings")), 0, 0, 0) == 1);
    CHECK(call("storage_is_hidden", P(put_str("a.mp3")), 0, 0, 0) == 0 && call("storage_is_hidden", 0x1000, 0, 0, 0) == 0);
    call("storage_mark_hidden", P(put_str("/sd/.x")), 0, 0, 0);
    CHECK(!strcmp(hidden_path, "/sd/.x"));
    storage_sd_info_t *si = box_alloc(sizeof *si);
    call("storage_sd_info", P(si), 0, 0, 0);
    CHECK(si->present && !strcmp(si->type, "SDHC/SDXC") && si->capacity_mb == 30436);
    storage_usb_info_t *ui = box_alloc(sizeof *ui);
    call("storage_usb_info", P(ui), 0, 0, 0);
    CHECK(ui->vid == 0x0781);
    call("storage_usb_info", 0x1000, 0, 0, 0);                      /* refused, quietly */
    CHECK(call("storage_usb_busy", 0, 0, 0, 0) == 0 && call("storage_usb_powered", 0, 0, 0, 0) == 1);
    CHECK(call("storage_usb_power", 1, 0, 0, 0) == 1 && call("storage_usb_power", 0, 0, 0, 0) == 0);
    call("storage_hold_brief", 0, 0, 0, 0);
    call("storage_hold_brief", 0, 0, 0, 0);
    call("storage_release_brief", 0, 0, 0, 0);
    call("storage_release_brief", 1, 0, 0, 0);                      /* not taken: not given */
    call("storage_hold_brief", 9, 0, 0, 0);
    CHECK(brief[0] == 1 && brief[1] == 0);
    call("storage_hold_background", 1, 0, 0, 0);
    call("storage_hold_readers", 0xff, 0, 0, 0);
    CHECK(bg == 1 && readers_mask == 3);
}

/* The app ends with everything open: the host closes and releases it all. */
static void test_end(void)
{
    char pbuf[128];
    snprintf(pbuf, sizeof pbuf, "%s/words.dict", dir);
    char *path = put_str(pbuf), *r = put_str("r");
    FIL *fp = box_alloc(sizeof(FIL));
    CHECK(call("f_open", P(fp), P(path), FA_READ, 0) == FR_OK && fat_open == 1);
    CHECK(call("fopen", P(path), P(r), 0, 0) != 0);
    CHECK(call("opendir", P(put_str(dir)), 0, 0, 0) != 0);
    call("storage_io_acquire", 0, 0, 0, 0);
    call("storage_io_acquire", 0, 0, 0, 0);
    call("storage_hold", 0, 0, 0, 0);
    CHECK(call("storage_io_open", P(path), P(r), 0, 0) != 0 && io_open == 1);   /* u-feckless-0047 */
    FF_DIR *dp = box_alloc(sizeof(FF_DIR));
    CHECK(call("f_opendir", P(dp), P(put_str(dir)), 0, 0) == FR_OK && ffdir_open == 1);
    feckless_box_calls_end();
    CHECK(fat_open == 0 && io_depth == 0 && hold_id == 2);          /* hold released: STORAGE_COUNT */
    CHECK(io_open == 0 && ffdir_open == 0 && brief[0] == 0 && bg == 2 && readers_mask == 0);
    CHECK(call("f_read", P(fp), 0, 0, 0) == FR_INVALID_OBJECT);
    /* every slot free again */
    for (int i = 0; i < 8; i++) {
        FIL *f = box_alloc(sizeof(FIL));
        CHECK(call("f_open", P(f), P(path), FA_READ, 0) == FR_OK);
    }
    FIL *ninth = box_alloc(sizeof(FIL));
    CHECK(call("f_open", P(ninth), P(path), FA_READ, 0) == FR_TOO_MANY_OPEN_FILES);
    feckless_box_calls_end();
    CHECK(fat_open == 0);
}

int main(void)
{
    mem = mmap(NULL, MEM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED) { printf("no memory below 4 GB\n"); return 1; }
    feckless_box_init(&box, (uintptr_t)mem, MEM, 4096);
    calls_tab = feckless_box_calls_v1(&ncalls);
    tab = (feckless_umode_calls_t){ calls_tab, ncalls, &box };
    w.lo = (uintptr_t)mem;
    w.hi = (uintptr_t)mem + MEM;
    w.ext_arg = &tab;
    box_lo = w.lo;
    box_hi = w.hi;
    make_files();
    nr_of("feckless_abi_storage_v2");
    test_fatfs();
    test_stdio();
    test_dirs();
    test_storage_handler();
    test_writes();
    test_storage_io();
    test_ffdir();
    test_more_handler();
    test_end();
    char cmd[160];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {}
    printf(failed ? "%d FAILED\n" : "box_storage: all passed\n", failed);
    return failed != 0;
}
