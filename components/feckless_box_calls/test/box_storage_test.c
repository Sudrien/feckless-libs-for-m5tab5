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

extern int io_depth, hold_id, fat_open;
extern DWORD *last_cltbl;

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
    feckless_box_calls_end();
    CHECK(fat_open == 0 && io_depth == 0 && hold_id == 2);          /* hold released: STORAGE_COUNT */
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
    make_files();
    nr_of("feckless_abi_storage_v2");
    test_fatfs();
    test_stdio();
    test_dirs();
    test_storage_handler();
    test_end();
    char cmd[160];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {}
    printf(failed ? "%d FAILED\n" : "box_storage: all passed\n", failed);
    return failed != 0;
}
