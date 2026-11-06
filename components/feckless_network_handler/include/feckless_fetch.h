/*
 * feckless_fetch.h -- one file, downloaded to a volume, resumably. 9003.
 *
 * Defeatist's podcast downloader (its poddl.c, Defeatist 8020-8057) less
 * what was a podcast's, so other apps get what it learned on the board:
 * Jinxed Writer fetches its spelling dictionary with it. What it does:
 *
 *   - Redirects followed by hand, up to FECKLESS_FETCH_HOPS, with
 *     relative ones resolved against the host; https only if asked.
 *   - Written to a part file, renamed to `path` only when the last byte
 *     is in and the caller's verify says yes, so a file at `path` is
 *     always whole.
 *   - Resumable: the part file's `.id` beside it holds the first reply's
 *     length and validator (a strong ETag, else Last-Modified) and a map
 *     of which bytes are on the card. A start of the same part file asks
 *     for the rest with Range and If-Range and takes a 206 only when its
 *     Content-Range is exactly the rest of the same length; anything
 *     else starts over. After a pause, a failure or a reboot alike.
 *   - Up to FECKLESS_FETCH_CONNS_MAX connections, for a file of at least
 *     `split_min` from a server that says Accept-Ranges: bytes: 1 MB
 *     stripes, each connection taking the next stripe nobody has, on the
 *     same connection while the server keeps it. One stream is bounded
 *     by its TCP window a round trip; on a Tab5's Wi-Fi Defeatist
 *     measured 1.7-1.9 Mbit/s on one and 3.6 on three (Defeatist 8036,
 *     8043).
 *   - One writer task, the card leased (storage_io, BACKGROUND) a 16 KB
 *     slice at a time so playback reading the same card wins every
 *     wait, a segment's bytes gathered 64 KB at a time so a striped
 *     download seeks once in four chunks (Defeatist 8045).
 *   - A connection that fails mid-way waits 1, 3 and 10 s and asks
 *     again for the rest of its stripe; the others go on.
 *
 * ONE AT A TIME. A start while one runs is refused. Everything here is
 * safe from any task; the work is on tasks of its own. The part file's
 * folder is made if missing, and `path`'s too before the rename.
 *
 * What a caller adds is through one callback, on the download's task
 * with the card NOT leased (take storage_io yourself to touch it):
 *
 *   START      before the part file is looked at: move an old one into
 *              place, say. The return is ignored.
 *   HEADERS    the reply that will be written has come: ev->url is the
 *              address that answered, ev->ctype its Content-Type. The
 *              callback may rewrite ev->path (choose an extension). The
 *              return is ignored.
 *   VERIFY     the part file is whole, ev->size bytes, its .id still
 *              beside it. False refuses it: the download fails and the
 *              part file is removed, since asking for the same bytes
 *              again would only fail again.
 *   DONE       renamed to ev->path.
 *   CANCELLED  the part file and its .id are gone -- on the download's
 *              task after a cancel while running, on the caller's for a
 *              cancel of a paused or failed one.
 *
 * Sockets: a download holds up to `conns` at once. An app that counts
 * its lwIP sockets (CONFIG_LWIP_MAX_SOCKETS) counts these.
 *
 * Extra connections are helper tasks with stacks in PSRAM; without
 * CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM there are none, and every
 * download is one connection.
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

#define FECKLESS_FETCH_URL_MAX      (1024)
#define FECKLESS_FETCH_PATH_MAX     (512)
#define FECKLESS_FETCH_CONNS_MAX    (3)
#define FECKLESS_FETCH_SPLIT_MIN    (8 * 1024 * 1024)
#define FECKLESS_FETCH_HOPS         (6)     /* tracking prefixes stack */

typedef enum {
    FECKLESS_FETCH_IDLE,
    FECKLESS_FETCH_RUNNING,
    FECKLESS_FETCH_DONE,        /* whole, at the path */
    FECKLESS_FETCH_FAILED,      /* the part file is kept, for the next start */
    FECKLESS_FETCH_PAUSED,      /* stopped on request, the part file kept */
} feckless_fetch_state_t;

typedef enum {
    FECKLESS_FETCH_EV_START,
    FECKLESS_FETCH_EV_HEADERS,
    FECKLESS_FETCH_EV_VERIFY,
    FECKLESS_FETCH_EV_DONE,
    FECKLESS_FETCH_EV_CANCELLED,
} feckless_fetch_ev_kind_t;

typedef struct {
    feckless_fetch_ev_kind_t kind;
    int64_t      id;            /* the caller's, from the options */
    const char  *url;           /* HEADERS: the address that answered */
    const char  *ctype;         /* HEADERS: its Content-Type, or "" */
    const char  *part;          /* the part file */
    char        *path;          /* the finished file; HEADERS may rewrite it */
    size_t       path_size;
    int64_t      size;          /* VERIFY, DONE: the file's bytes */
    void        *arg;           /* the options' */
} feckless_fetch_ev_t;

typedef bool (*feckless_fetch_cb_t)(feckless_fetch_ev_t *ev);

typedef struct {
    const char  *url;
    const char  *path;          /* the finished file */
    const char  *part;          /* its part file; NULL: path + ".part" */
    int          conns;         /* 0 or 1: one; at most FECKLESS_FETCH_CONNS_MAX */
    int64_t      split_min;     /* 0: FECKLESS_FETCH_SPLIT_MIN */
    const char  *user_agent;    /* NULL: esp_http_client's own */
    bool         https_only;    /* refuse http://, at the start or a redirect */
    feckless_fetch_cb_t cb;     /* NULL: none */
    void        *arg;
    int64_t      id;            /* the caller's, reported back */
} feckless_fetch_opts_t;

/* Start. The options are copied; their strings may go once this
 * returns. False if one is running, the options are unusable (no url,
 * no path, too long, http:// with https_only) or the task could not
 * start -- feckless_fetch_why() says which. */
bool feckless_fetch_start(const feckless_fetch_opts_t *opts);

/* Stop at the next chunk and keep the part file (pause), or stop and
 * remove it (cancel). Cancel on a paused or failed download removes its
 * part file at once. */
void feckless_fetch_pause(void);
void feckless_fetch_cancel(void);

/* The current or last download: its id, bytes so far and in all (-1 if
 * the server did not say), the finished file's path, and a generation
 * that moves with every change worth redrawing (every 0.1 MB). Any
 * pointer may be NULL. No filesystem. */
feckless_fetch_state_t feckless_fetch_state(int64_t *id, int64_t *got, int64_t *total,
                                            char *path, size_t path_size, uint32_t *gen);

/* Why the last start was refused or the last download failed, in a few
 * plain words ("could not connect", "the server said 404"), or "".
 * No filesystem. */
void feckless_fetch_why(char *out, size_t out_size);

/*
 * PLAYING WHILE IT DOWNLOADS (Defeatist 8050). Whether `path` -- a
 * finished file's path, as feckless_fetch_state() gives it -- is the
 * current or last download, and if so its part file, the bytes from its
 * start that are on the card (written, flushed and fsync()ed, with no
 * gap before them), its total (-1 if not known) and its state. A reader
 * may read the part file up to `ready` and no further. `ready` grows a
 * megabyte at a time while it runs. Once DONE the part file is gone and
 * `path` is the whole file. No filesystem.
 */
bool feckless_fetch_live(const char *path, char *part, size_t part_size,
                         int64_t *ready, int64_t *total, feckless_fetch_state_t *st);

/* Whether a part file is there, and its size. Touches the filesystem;
 * takes the lease. */
bool feckless_fetch_part(const char *part, int64_t *size);

/* Remove a part file and its .id, for a download not running -- after a
 * reboot, when there is nothing for feckless_fetch_cancel() to stop.
 * Nothing while one runs. Touches the filesystem; takes the lease. */
void feckless_fetch_discard(const char *part);

#ifdef __cplusplus
}
#endif
