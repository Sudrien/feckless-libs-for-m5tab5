/* Stand-in for FatFs's ff.h: the fields and codes box_storage.c uses, the
 * values FatFs has. FSIZE_t is 64 bits, as with exFAT. */
#pragma once
#include <stdint.h>
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef uint64_t FSIZE_t;
#define FF_USE_FASTSEEK 1
#define CREATE_LINKMAP ((FSIZE_t)0 - 1)
typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH, FR_INVALID_NAME,
    FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED,
    FR_NO_FILESYSTEM, FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER
} FRESULT;
typedef struct { void *fs; FSIZE_t objsize; } FFOBJID;
typedef struct {
    FFOBJID obj;
    BYTE flag;
    BYTE err;
    FSIZE_t fptr;
    DWORD *cltbl;
    void *host_file;    /* the fake's */
    BYTE buf[512];
} FIL;
#define FA_READ 1
FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_lseek(FIL *fp, FSIZE_t ofs);
FRESULT f_close(FIL *fp);
