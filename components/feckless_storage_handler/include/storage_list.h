/*
 * storage_list.h -- one folder's entries, filtered and sorted, for a
 * chooser to draw (9011).
 *
 * Every app had its own: Avoidant's menu, Defeatist's browser and its
 * remote `ls`, Spurious's dictionary scan, and Jinxed Writer was about to
 * write a fifth. Each opened the folder, skipped the dotfiles in its own
 * way, stat()ed each file for its size, and sorted folders first. What
 * differs between them is which files are wanted, and that is the
 * filter; the rest is here, once.
 *
 * READ IN ONE PASS. FatFs's f_readdir() fills in the size and the date
 * as it goes, and ESP-IDF's readdir() throws them away, so a chooser that
 * shows a size calls stat() per file -- which on FAT reads the folder
 * from the top for each name. Defeatist measured that at 9.5 s for 1192
 * tracks (its mediadir.h). This reads FILINFO itself.
 *
 * WHAT IS LEFT OUT before the filter sees it: storage_is_hidden()'s
 * (dotfiles, "System Volume Information", "$RECYCLE.BIN"), and entries
 * with the FAT hidden or system attribute. The filter decides the rest.
 *
 * THE VOLUME is held with storage_hold_brief() for the read and let go
 * after the close; each FatFs call takes the STORAGE_IO_BACKGROUND lease
 * for itself, so a listing never holds up a player's read by more than
 * one directory block. A volume that went away reads as an error.
 *
 * MEMORY: the entries and their names are on the heap, in PSRAM where
 * there is some, grown as needed, up to `max` entries. Nothing on the
 * stack but a few words. Reentrant: each call has its own directory
 * object, so two tasks may each read their own list.
 *
 * In the ABI's storage section, as the rest of the handler is, so an app
 * loaded by a launcher lists folders with the launcher's copy. So
 * storage_ent_t and storage_list_t are laid out for good: a field added
 * is a new section version.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One entry. The strings live in the list's arena: good until the list
 * is read again, cleared or freed. */
typedef struct {
    const char *name;       /* the name in the folder, as readdir() gives it */
    const char *label;      /* what to show instead, or NULL: the name */
    int64_t     size;       /* bytes; -1 for a folder or unknown */
    uint32_t    fat_time;   /* FatFs date << 16 | time; 0 unknown */
    bool        is_dir;
    bool        pinned;     /* added by the app; stays first, in order */
    uint8_t     kind;       /* the filter's: whatever the app wants to tag */
} storage_ent_t;

/* What the filter says of an entry. */
#define STORAGE_LIST_SKIP   (-1)

/*
 * Keep this entry? STORAGE_LIST_SKIP leaves it out; 0 to 255 keeps it
 * with that as its kind. `dir` is the folder being read, for a filter
 * that has to look inside the file (storage_join_path() it). Called with
 * the volume held, so it may open the file; it must not take the
 * STORAGE_IO lease itself across the whole call.
 *
 * NULL keeps every folder and every file.
 */
typedef int (*storage_list_filter_fn)(const char *dir, const storage_ent_t *e, void *ctx);

typedef enum {
    STORAGE_SORT_NAME = 0,  /* folders first, then files, each by name, case folded */
    STORAGE_SORT_NEWEST,    /* folders first, then files, newest first, then by name */
    STORAGE_SORT_NONE,      /* the folder's own order */
} storage_sort_t;

typedef struct {
    storage_ent_t *ents;
    int   n;
    int   max;              /* the most kept; 0 is STORAGE_LIST_MAX */
    bool  truncated;        /* more were wanted than max */

    /* private */
    int    cap;
    char  *arena;
    size_t used, size;
} storage_list_t;

#define STORAGE_LIST_MAX    (4096)

/* What storage_list_read() found. */
typedef enum {
    STORAGE_LIST_OK = 0,
    STORAGE_LIST_NO_DIR,    /* the volume is gone, or no such folder: the list is empty */
    STORAGE_LIST_IO,        /* a read failed part way: the list holds what came before */
    STORAGE_LIST_NO_MEM,    /* the list holds what fitted */
} storage_list_err_t;

/* A list starts zeroed: `storage_list_t l = { 0 };`, max set if wanted. */

/*
 * Empty the list and read the folder at `dir` (a VFS path: "/sd/notes")
 * into it, through `filter`, sorted by `sort`. Pinned entries -- added
 * with storage_list_add() before the read -- do not survive it; add
 * them after.
 */
storage_list_err_t storage_list_read(storage_list_t *l, const char *dir, storage_sort_t sort,
                                     storage_list_filter_fn filter, void *ctx);

/*
 * Empty the list and put in the volumes that are present, in
 * storage_id_t order: each a folder named by its mount path without the
 * slash ("sd", "usb"), labelled with storage_label().
 */
void storage_list_volumes(storage_list_t *l);

/*
 * Add an entry the folder does not have -- a row of the app's own: a
 * track in a cue sheet, Avoidant's "Boot app2". The strings are copied.
 * `pinned` puts it before everything storage_list_sort() sorts, in the
 * order added. False without memory, or at max.
 */
bool storage_list_add(storage_list_t *l, const char *name, const char *label, bool is_dir,
                      int64_t size, uint32_t fat_time, bool pinned, uint8_t kind);

/* Sort again, by another order. storage_list_read() has already sorted. */
void storage_list_sort(storage_list_t *l, storage_sort_t sort);

/* Index of the entry named `name` (as FAT matches: case folded), or -1. */
int storage_list_find(const storage_list_t *l, const char *name);

/* Empty, keeping the memory for the next read. */
void storage_list_clear(storage_list_t *l);

/* Empty and give the memory back. The list can be read into again. */
void storage_list_free(storage_list_t *l);

/*
 * A FAT timestamp in parts. False for 0 (unknown). The date and time are
 * the card's: local time, as the computer that wrote it had it.
 */
static inline bool storage_fat_time_parts(uint32_t t, int *year, int *mon, int *day,
                                          int *hour, int *min)
{
    if (!t) return false;
    *year = 1980 + (int)(t >> 25);
    *mon  = (int)((t >> 21) & 15);
    *day  = (int)((t >> 16) & 31);
    *hour = (int)((t >> 11) & 31);
    *min  = (int)((t >> 5) & 63);
    return true;
}

#ifdef __cplusplus
}
#endif
