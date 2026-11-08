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
