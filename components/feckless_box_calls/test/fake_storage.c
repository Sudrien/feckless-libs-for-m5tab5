/* SPDX-License-Identifier: MIT
 * Stand-ins for FatFs and the storage handler, over real host files. The
 * fake FatFs asks for 300 table items for fast seek, as a fragmented file
 * would, and records the table it was given. */
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "storage.h"
#include "storage_io.h"

int io_depth, hold_id = -1, fat_open;
/* u-feckless-0015: every f_read's destination, against the box's range. */
uintptr_t box_lo, box_hi;
int reads_into_box, reads_total;
DWORD *last_cltbl;
const char *mount_tmp = "/tmp";

FRESULT f_open(FIL *fp, const char *path, BYTE mode)
{
    (void)mode;
    FILE *f = fopen(path, "rb");
    if (!f) return FR_NO_FILE;
    memset(fp, 0, sizeof *fp);
    fseek(f, 0, SEEK_END);
    fp->obj.objsize = (FSIZE_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    fp->obj.fs = (void *)0x4ff00000;       /* a host pointer the app must never get */
    fp->host_file = f;
    fp->flag = 1;
    fat_open++;
    return FR_OK;
}
FRESULT f_read(FIL *fp, void *b, UINT n, UINT *got)
{
    reads_total++;
    if ((uintptr_t)b < box_hi && (uintptr_t)b + n > box_lo) reads_into_box++;
    last_cltbl = fp->cltbl;
    fseek(fp->host_file, (long)fp->fptr, SEEK_SET);
    *got = (UINT)fread(b, 1, n, fp->host_file);
    fp->fptr += *got;
    return FR_OK;
}
FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
    last_cltbl = fp->cltbl;
    if (ofs == CREATE_LINKMAP) {
        if (!fp->cltbl) return FR_INVALID_PARAMETER;
        if (fp->cltbl[0] < 300) { fp->cltbl[0] = 300; return FR_NOT_ENOUGH_CORE; }
        fp->cltbl[0] = 211;                 /* used */
        return FR_OK;
    }
    fp->fptr = ofs > fp->obj.objsize ? fp->obj.objsize : ofs;
    return FR_OK;
}
FRESULT f_close(FIL *fp) { fclose(fp->host_file); fat_open--; return FR_OK; }

bool storage_present(storage_id_t id) { return id == STORAGE_SD; }
const char *storage_mount_path(storage_id_t id) { return id == STORAGE_SD ? "/sd" : "/usb"; }
int storage_ff_drive(storage_id_t id) { return (int)id; }
storage_id_t storage_of_path(const char *p) { return !strncmp(p, "/sd", 3) ? STORAGE_SD : STORAGE_COUNT; }
bool storage_join_path(char *out, size_t n, const char *d, const char *f)
{
    return (size_t)snprintf(out, n, "%s/%s", d, f) < n;
}
uint32_t storage_generation(void) { return 7; }
void storage_hold(storage_id_t id) { hold_id = (int)id; }
void storage_io_acquire(storage_io_class_t c) { (void)c; io_depth++; }
void storage_io_release(void) { io_depth--; }

/* u-feckless-0047: the rest, over the host's own files and directories. */
#include <dirent.h>
int io_open, io_cls = -1, io_into_box, brief[2], bg = -1, readers_mask, ffdir_open;
BYTE chmod_attr, chmod_mask;
char hidden_path[300], report_phase[64];
FILE *storage_io_open(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f) io_open++;
    return f;
}
int storage_io_close(FILE *f) { io_open--; return fclose(f); }
size_t storage_io_fread(void *dst, size_t len, FILE *f, storage_io_class_t cls)
{
    io_cls = (int)cls;
    if ((uintptr_t)dst < box_hi && (uintptr_t)dst + len > box_lo) io_into_box++;
    return fread(dst, 1, len, f);
}
bool storage_io_read_at(FILE *f, int64_t off, void *dst, size_t len, storage_io_class_t cls)
{
    if (fseek(f, (long)off, SEEK_SET) != 0) return false;
    return storage_io_fread(dst, len, f, cls) == len;
}
uint32_t storage_io_phase_ms(void) { return 1500; }
void storage_io_report(const char *phase) { snprintf(report_phase, sizeof report_phase, "%s", phase); }
const char *storage_label(storage_id_t id) { return id == STORAGE_SD ? "SD card" : "USB drive"; }
bool storage_is_hidden(const char *name) { return name[0] == '.'; }
void storage_mark_hidden(const char *path) { snprintf(hidden_path, sizeof hidden_path, "%s", path); }
bool storage_usb_power(bool on) { return on; }
bool storage_usb_busy(void) { return false; }
bool storage_usb_powered(void) { return true; }
void storage_sd_info(storage_sd_info_t *o) { memset(o, 0, sizeof *o); o->present = true; strcpy(o->type, "SDHC/SDXC"); o->capacity_mb = 30436; }
void storage_usb_info(storage_usb_info_t *o) { memset(o, 0, sizeof *o); o->vid = 0x0781; }
void storage_hold_background(storage_id_t id) { bg = (int)id; }
void storage_hold_readers(uint32_t m) { readers_mask = (int)m; }
void storage_hold_brief(storage_id_t id) { brief[id]++; }
void storage_release_brief(storage_id_t id) { brief[id]--; }
FRESULT f_opendir(FF_DIR *dp, const char *path)
{
    DIR *d = opendir(path);
    if (!d) return FR_NO_PATH;
    memset(dp, 0, sizeof *dp);
    dp->obj.fs = (void *)0x4ff00000;
    dp->host_dir = d;
    ffdir_open++;
    return FR_OK;
}
FRESULT f_readdir(FF_DIR *dp, FILINFO *fno)
{
    if (!fno) { rewinddir(dp->host_dir); return FR_OK; }
    const struct dirent *e = readdir(dp->host_dir);
    memset(fno, 0, sizeof *fno);
    if (e) snprintf(fno->fname, sizeof fno->fname, "%s", e->d_name);
    return FR_OK;
}
FRESULT f_closedir(FF_DIR *dp) { closedir(dp->host_dir); ffdir_open--; return FR_OK; }
FRESULT f_chmod(const char *path, BYTE attr, BYTE mask) { (void)path; chmod_attr = attr; chmod_mask = mask; return FR_OK; }
