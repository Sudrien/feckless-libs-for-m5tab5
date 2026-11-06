/*
 * feckless_fetch.c -- see feckless_fetch.h. 9003.
 *
 * Lifted from Defeatist's poddl.c (Defeatist 8020-8057), which ran it on
 * the board. Its numbers are Defeatist's, written "Defeatist 8043", and
 * are not this repository's 8000 series. What was a podcast's -- folder
 * layout, the extension, pictures, the sidecar, the socket budget -- is
 * the caller's now, through the events in feckless_fetch.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_fetch.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/opt.h"           /* TCP_WND, in the timing line */
#include "sdkconfig.h"
#include "storage_io.h"

static const char *TAG = "feckless_fetch";

/*
 * 9004: THE STATICS IN PSRAM, where the app allows it. They are about
 * 16 KB -- addresses, paths, header buffers, the helpers' state -- and a
 * component's zero-initialised statics are internal RAM unless something
 * moves them, which on a Tab5 is the radio's DMA pool: Defeatist lost its
 * radio to 12 KB of .bss once (its 5178). EXT_RAM_BSS_ATTR is IDF's
 * .ext_ram.bss with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY and
 * nothing without it, so an app that has not set the option keeps them
 * internal, as before. None is a FreeRTOS object (those are on the heap:
 * the queues, the mutex, the helpers' TCBs), and none is touched with the
 * cache off.
 */
#define PSRAM_BSS EXT_RAM_BSS_ATTR

#define DL_STACK      8192          /* TLS and FatFs; internal: it writes the card */
#define DL_CHUNK      STORAGE_IO_CHUNK
#define DL_TIMEOUT_MS 15000
/* Chunks in flight between the readers and the card writer, in PSRAM:
 * four, and one more for each extra connection (Defeatist 8036, 8043). */
#define DL_NBUF       (4 + FECKLESS_FETCH_CONNS_MAX - 1)
#define DL_WR_STACK   6144
/* The most segments a part file's map holds (Defeatist 8042). More than
 * FECKLESS_FETCH_CONNS_MAX so that a map from a download of more
 * connections still reads. */
#define DL_SEGS_MAX   4
/* A segment's bytes gathered before a seek and a write (Defeatist 8045),
 * and how often the map is saved (Defeatist 8050). */
#define DL_STAGE      (64 * 1024)
#define DL_MAP_EVERY  (1 * 1024 * 1024)
#define DL_HELP_STACK 6144
#define DL_STRIPE     (1024 * 1024) /* Defeatist 8049: 1 MB, of 64 KB, 512 KB and 1 MB tried */
#define DL_HOPS       FECKLESS_FETCH_HOPS
#define DL_URL_MAX    FECKLESS_FETCH_URL_MAX
#define DL_PART_MAX   FECKLESS_FETCH_PATH_MAX
#define DL_RETRIES    3
#define DL_WHY_MAX    64

_Static_assert(FECKLESS_FETCH_CONNS_MAX <= DL_SEGS_MAX, "a segment a connection");

static SemaphoreHandle_t s_mu;
static feckless_fetch_state_t s_state;
static int64_t           s_id, s_got, s_total;
static uint32_t          s_gen;
static int64_t           s_step = -1;           /* last tenth of a MB the generation moved for */
enum { RUN = 0, STOP_PAUSE, STOP_CANCEL };
static int               s_stop;
static PSRAM_BSS char     s_url[DL_URL_MAX];
static PSRAM_BSS char     s_path[FECKLESS_FETCH_PATH_MAX];   /* the finished file */
static PSRAM_BSS char     s_partp[DL_PART_MAX];  /* its part file */
static int64_t           s_ready;               /* see feckless_fetch_live() */
static PSRAM_BSS char     s_why[DL_WHY_MAX];
/* The options, copied. */
static int               s_conns;
static int64_t           s_split_min;
static PSRAM_BSS char     s_ua[96];
static bool              s_https_only;
static feckless_fetch_cb_t s_cb;
static void             *s_arg;

/* What the response headers said; filled by the event handler. */
typedef struct {
    char location[DL_URL_MAX];
    char ctype[64];
    char etag[96];              /* what a resume is checked against */
    char lastmod[40];
    char crange[80];
    char ranges[16];            /* Accept-Ranges */
    char if_range[96];          /* sent with Range, never cleared by a hop */
} hdrs_t;

/* A string into a caller's buffer of `size`, cut to fit: no format, so
 * nothing for -Wformat-truncation to guess at (9002 is why). */
static void copy_out(char *dst, size_t size, const char *src)
{
    const size_t n = strnlen(src, size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void lock(void)
{
    /* Made on the first call: feckless_fetch_start() is the first, and
     * an app starts nothing on two tasks at once before it. */
    if (!s_mu) s_mu = xSemaphoreCreateMutex();
    xSemaphoreTake(s_mu, portMAX_DELAY);
}
static void unlock(void) { xSemaphoreGive(s_mu); }

static void set_why(const char *why)
{
    lock();
    snprintf(s_why, sizeof(s_why), "%s", why);
    unlock();
}

static esp_err_t on_event(esp_http_client_event_t *ev)
{
    hdrs_t *h = ev->user_data;
    if (ev->event_id == HTTP_EVENT_ON_HEADER && h && ev->header_key && ev->header_value) {
        if (!strcasecmp(ev->header_key, "Location"))
            snprintf(h->location, sizeof(h->location), "%s", ev->header_value);
        else if (!strcasecmp(ev->header_key, "Content-Type"))
            snprintf(h->ctype, sizeof(h->ctype), "%.*s", (int)sizeof(h->ctype) - 1, ev->header_value);
        else if (!strcasecmp(ev->header_key, "ETag"))
            snprintf(h->etag, sizeof(h->etag), "%.*s", (int)sizeof(h->etag) - 1, ev->header_value);
        else if (!strcasecmp(ev->header_key, "Last-Modified"))
            snprintf(h->lastmod, sizeof(h->lastmod), "%.*s", (int)sizeof(h->lastmod) - 1, ev->header_value);
        else if (!strcasecmp(ev->header_key, "Accept-Ranges"))
            snprintf(h->ranges, sizeof(h->ranges), "%.*s", (int)sizeof(h->ranges) - 1, ev->header_value);
        else if (!strcasecmp(ev->header_key, "Content-Range"))
            snprintf(h->crange, sizeof(h->crange), "%.*s", (int)sizeof(h->crange) - 1, ev->header_value);
    }
    return ESP_OK;
}

static void finish(feckless_fetch_state_t st)
{
    lock();
    s_state = st;
    s_gen++;
    unlock();
}

/* The callback, if there is one. */
static bool event(feckless_fetch_ev_kind_t kind, const char *url, const char *ctype,
                  const char *part, char *path, size_t path_size, int64_t size)
{
    if (!s_cb) return true;
    feckless_fetch_ev_t ev = {
        .kind = kind, .id = s_id, .url = url, .ctype = ctype ? ctype : "",
        .part = part, .path = path, .path_size = path_size, .size = size, .arg = s_arg,
    };
    return s_cb(&ev);
}

/* Every folder down to the one `file` is in. A level that is there
 * already, or the mount point, fails harmlessly; the fopen() after says
 * what matters. Under the lease. */
static void make_dirs_for(const char *file)
{
    char p[DL_PART_MAX];
    const char *slash = strrchr(file, '/');
    if (!slash || slash == file || (size_t)(slash - file) >= sizeof(p)) return;
    for (const char *q = file + 1; q <= slash; q++) {
        if (*q != '/') continue;
        memcpy(p, file, (size_t)(q - file));
        p[q - file] = '\0';
        if (mkdir(p, 0777) != 0 && errno != EEXIST) ESP_LOGD(TAG, "mkdir %s: errno %d", p, errno);
    }
}

/*
 * THE PART FILE'S IDENTITY (Defeatist 8041). A resume asked for "the
 * rest" and took any 206 as the rest of the same file; some hosts insert
 * ads per request, so the file a server has today need not be the one
 * whose first half is on the card. <part>.id holds the first reply's
 * total and its validator -- a strong ETag, else Last-Modified -- and a
 * resume sends If-Range with it and checks Content-Range.
 *
 * THE MAP (Defeatist 8042). The .id also says which bytes of the part
 * file are there, as segments: `s <start> <end> <done>`, end exclusive
 * (-1 when the length is not known), done the bytes from start on the
 * card; and (Defeatist 8046) `p <front> <stripe>` in stripes: every byte
 * before `front` in none of the segments is on the card. A two-line .id
 * is one segment, done from the part file's size.
 *
 * Written to <part>.id.tmp and renamed over, so a reboot during the
 * write leaves the old map or the new one; rename() does not replace, so
 * the old one goes first, and a read that finds no .id takes the .tmp.
 */
typedef struct { int64_t start, end, done; } seg_t;

static bool id_path(const char *part, char *out, size_t cap)
{
    return snprintf(out, cap, "%s.id", part) < (int)cap;
}

static bool id_tmp_path(const char *part, char *out, size_t cap)
{
    return snprintf(out, cap, "%s.id.tmp", part) < (int)cap;
}

/* The number of segments read (0: a .id with no map), or -1 for no .id. */
static int id_read(const char *part, char *val, size_t vcap, int64_t *total,
                   seg_t *seg, int nmax, int64_t *front, int64_t *stripe)
{
    static PSRAM_BSS char p[DL_PART_MAX + 8];
    val[0] = '\0';
    *total = -1;
    *front = -1;
    *stripe = 0;
    if (!id_path(part, p, sizeof(p))) return -1;
    FILE *f = fopen(p, "r");
    if (!f && id_tmp_path(part, p, sizeof(p))) f = fopen(p, "r");
    if (!f) return -1;
    char line[80];
    int n = -1;
    if (fgets(line, sizeof(line), f)) {
        *total = strtoll(line, NULL, 10);
        n = 0;
        if (fgets(val, (int)vcap, f)) val[strcspn(val, "\r\n")] = '\0';
        while (fgets(line, sizeof(line), f)) {
            long long a, b, d;
            if (sscanf(line, "p %lld %lld", &a, &b) == 2) { *front = a; *stripe = b; continue; }
            if (n >= nmax || sscanf(line, "s %lld %lld %lld", &a, &b, &d) != 3) break;
            seg[n].start = a; seg[n].end = b; seg[n].done = d;
            n++;
        }
    }
    fclose(f);
    return n;
}

static void id_write(const char *part, const char *val, int64_t total, const seg_t *seg, int nseg,
                     int64_t front, int64_t stripe)
{
    static PSRAM_BSS char p[DL_PART_MAX + 8], t[DL_PART_MAX + 8];
    if (!id_path(part, p, sizeof(p)) || !id_tmp_path(part, t, sizeof(t))) return;
    FILE *f = fopen(t, "w");
    if (!f) { ESP_LOGW(TAG, "could not write %s", t); return; }
    bool ok = fprintf(f, "%lld\n%s\n", (long long)total, val) > 0;
    for (int i = 0; i < nseg && ok; i++)
        ok = fprintf(f, "s %lld %lld %lld\n", (long long)seg[i].start,
                     (long long)seg[i].end, (long long)seg[i].done) > 0;
    if (ok && front >= 0) ok = fprintf(f, "p %lld %lld\n", (long long)front, (long long)stripe) > 0;
    if (fflush(f) != 0) ok = false;
    if (ok) fsync(fileno(f));
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(t); ESP_LOGW(TAG, "could not write %s", t); return; }
    remove(p);
    if (rename(t, p) != 0) ESP_LOGW(TAG, "could not rename %s", t);
}

static void remove_id(const char *part)
{
    static PSRAM_BSS char p[DL_PART_MAX + 8];
    if (id_path(part, p, sizeof(p))) remove(p);
    if (id_tmp_path(part, p, sizeof(p))) remove(p);
}

/* The bytes from the file's start with no gap (Defeatist 8050): the
 * lowest point a segment not yet whole has reached, or the front when
 * all in hand are. */
static int64_t ready_of(const seg_t *seg, int nseg, int64_t front)
{
    if (nseg == 1) return seg[0].start + seg[0].done;
    int64_t r = front >= 0 ? front : INT64_MAX;
    for (int i = 0; i < nseg; i++)
        if (seg[i].end < 0 || seg[i].done < seg[i].end - seg[i].start)
            if (seg[i].start + seg[i].done < r) r = seg[i].start + seg[i].done;
    return r == INT64_MAX ? 0 : r;
}

static void set_ready(int64_t r)
{
    lock();
    if (r > s_ready) s_ready = r;
    unlock();
}

static void remove_part(const char *part)
{
    remove(part);
    remove_id(part);
}

/* The validator a later If-Range can use: a strong ETag, else the date.
 * A weak ETag (W/"...") is not allowed in If-Range. */
static const char *validator(const hdrs_t *h)
{
    if (h->etag[0] && strncmp(h->etag, "W/", 2) != 0) return h->etag;
    return h->lastmod;
}

static bool is_http(const char *u) { return !strncasecmp(u, "http://", 7); }

/* Open `url`, following redirects; the client, positioned at the body,
 * with its status in *status, or NULL. `url` is rewritten with the
 * address that answered. */
static esp_http_client_handle_t open_followed(char *url, hdrs_t *h, int *status, int64_t from, int64_t to)
{
    *status = 0;
    for (int hop = 0; hop <= DL_HOPS; hop++) {
        h->location[0] = h->ctype[0] = '\0';
        h->etag[0] = h->lastmod[0] = h->crange[0] = h->ranges[0] = '\0';
        if (s_https_only && is_http(url)) {
            ESP_LOGW(TAG, "not https: %.80s", url);
            set_why("not https");
            return NULL;
        }
        esp_http_client_config_t cfg = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = DL_TIMEOUT_MS,
            .buffer_size = 4096,
            .buffer_size_tx = 2048,
            .user_agent = s_ua[0] ? s_ua : NULL,
            .disable_auto_redirect = true,
            .event_handler = on_event,
            .user_data = h,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) { set_why("out of memory"); return NULL; }
        if (from > 0 || to >= 0) {                  /* the rest of a part file, or a segment */
            char range[48];
            if (to >= from)
                snprintf(range, sizeof(range), "bytes=%lld-%lld", (long long)from, (long long)to);
            else
                snprintf(range, sizeof(range), "bytes=%lld-", (long long)from);
            esp_http_client_set_header(c, "Range", range);
            /* And only if it is still the same file -- else the server
             * sends all of it, 200, and this starts over. */
            if (h->if_range[0]) esp_http_client_set_header(c, "If-Range", h->if_range);
        }
        if (esp_http_client_open(c, 0) != ESP_OK) {
            ESP_LOGW(TAG, "no connection to %.80s", url);
            set_why("could not connect");
            esp_http_client_cleanup(c);
            return NULL;
        }
        esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        const bool redirect = *status == 301 || *status == 302 || *status == 303 ||
                              *status == 307 || *status == 308;
        if (!redirect) return c;
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        if (!h->location[0]) { set_why("a redirect to nowhere"); return NULL; }
        if (!strncasecmp(h->location, "http://", 7) || !strncasecmp(h->location, "https://", 8)) {
            if (strlen(h->location) >= DL_URL_MAX) { set_why("a redirect too long"); return NULL; }
            snprintf(url, DL_URL_MAX, "%s", h->location);
        } else if (h->location[0] == '/' && h->location[1] == '/') {
            /* Scheme-relative: this address's scheme, that host. */
            const char *sep = strstr(url, "://");
            const size_t keep = sep ? (size_t)(sep - url) + 1 : 0;    /* "https:" */
            if (!keep || keep + strlen(h->location) + 1 > DL_URL_MAX) { set_why("a redirect not followed"); return NULL; }
            memmove(url + keep, h->location, strlen(h->location) + 1);
        } else if (h->location[0] == '/') {
            /* An absolute path on the same host. */
            char *p = strstr(url, "://");
            p = p ? strchr(p + 3, '/') : NULL;
            const size_t keep = p ? (size_t)(p - url) : strlen(url);
            if (keep + strlen(h->location) + 1 > DL_URL_MAX) { set_why("a redirect too long"); return NULL; }
            memmove(url + keep, h->location, strlen(h->location) + 1);
        } else {
            ESP_LOGW(TAG, "a redirect to \"%.60s\" not followed", h->location);
            set_why("a redirect not followed");
            return NULL;
        }
        ESP_LOGI(TAG, "%d to %.100s", *status, url);
    }
    ESP_LOGW(TAG, "more than %d redirects", DL_HOPS);
    set_why("too many redirects");
    return NULL;
}

/*
 * THE WRITER (Defeatist 8036). The readers hand it chunks through a
 * queue; it writes them and keeps the map, since only it knows what is
 * on the card. A chunk says its segment and its place in the file; the
 * writer seeks only when that is not where the last one ended. A write
 * that fails sets err and drops the rest; the readers see err and stop.
 * len WR_CLAIM is no chunk but a segment taking a new stripe, [off,
 * end): what it has for the old one is flushed first (Defeatist 8046).
 */
typedef struct { uint8_t *p; int len; int seg; int64_t off; int64_t end; } wr_item_t;
#define WR_CLAIM (-2)
static struct {
    QueueHandle_t     full, free_;
    SemaphoreHandle_t done;
    FILE             *f;
    volatile bool     err;
    volatile int      err_no;
    volatile int64_t  put;                  /* bytes written */
    volatile int64_t  t_lease, t_write;
    int64_t           pos;                  /* where the file is; -1 unknown */
    seg_t             seg[DL_SEGS_MAX];
    int               nseg;
    int64_t           total, since_map;
    const char       *part;
    char              val[96];
    int64_t           front, stripe;
    /* Each segment's gathered bytes, and where they go; NULL when PSRAM
     * could not be had, and then every chunk is written as it comes. */
    uint8_t          *stage;
    int               st_len[DL_SEGS_MAX];
    int64_t           st_off[DL_SEGS_MAX];
} s_wr PSRAM_BSS;

/*
 * Chunk writes (Defeatist 8045): a segment's chunks gathered in its
 * stage and written DL_STAGE at a time, one seek for four chunks, since
 * FAT seeks by walking the cluster chain. The lease is still taken a
 * 16 KB slice at a time, so playback waits no longer; the FILE keeps its
 * place between slices, since nothing else writes it.
 */
static bool wr_out(int seg, const uint8_t *p, int len, int64_t off)
{
    for (int done = 0; done < len; ) {
        const int n = len - done < DL_CHUNK ? len - done : DL_CHUNK;
        const int64_t t0 = esp_timer_get_time();
        storage_io_acquire(STORAGE_IO_BACKGROUND);
        const int64_t t1 = esp_timer_get_time();
        /* fseek() takes a long, so past 2 GB it cannot be asked; such a
         * file is kept to one segment, which never seeks. */
        const int64_t at_off = off + done;
        bool at = at_off == s_wr.pos;
        if (!at && at_off <= LONG_MAX) at = fseek(s_wr.f, (long)at_off, SEEK_SET) == 0;
        const size_t put = at ? fwrite(p + done, 1, (size_t)n, s_wr.f) : 0;
        if (put == (size_t)n) {
            s_wr.pos = at_off + n;
            s_wr.seg[seg].done += n;
            s_wr.since_map += n;
            if (s_wr.since_map >= DL_MAP_EVERY) {
                s_wr.since_map = 0;
                if (fflush(s_wr.f) == 0) {
                    fsync(fileno(s_wr.f));
                    id_write(s_wr.part, s_wr.val, s_wr.total, s_wr.seg, s_wr.nseg,
                             s_wr.front, s_wr.stripe);
                    set_ready(ready_of(s_wr.seg, s_wr.nseg, s_wr.front));
                }
            }
        } else {
            s_wr.pos = -1;
        }
        storage_io_release();
        const int64_t t2 = esp_timer_get_time();
        s_wr.t_lease += t1 - t0;
        s_wr.t_write += t2 - t1;
        if (put != (size_t)n) { s_wr.err_no = errno; s_wr.err = true; return false; }
        s_wr.put += n;
        done += n;
    }
    return true;
}

static void wr_flush(int seg)
{
    if (s_wr.st_len[seg] > 0 && !s_wr.err)
        wr_out(seg, s_wr.stage + (size_t)seg * DL_STAGE, s_wr.st_len[seg], s_wr.st_off[seg]);
    s_wr.st_len[seg] = 0;
}

static void wr_task(void *arg)
{
    (void)arg;
    wr_item_t it;
    for (int i = 0; i < DL_SEGS_MAX; i++) s_wr.st_len[i] = 0;
    for (;;) {
        xQueueReceive(s_wr.full, &it, portMAX_DELAY);
        if (it.len == WR_CLAIM) {                   /* the old stripe is whole */
            if (s_wr.stage) wr_flush(it.seg);
            s_wr.seg[it.seg].start = it.off;
            s_wr.seg[it.seg].end = it.end;
            s_wr.seg[it.seg].done = 0;
            if (it.end > s_wr.front) s_wr.front = it.end;
            continue;
        }
        if (it.len <= 0) break;
        if (s_wr.err) {
            /* dropped: the readers see err and stop */
        } else if (!s_wr.stage) {
            wr_out(it.seg, it.p, it.len, it.off);
        } else {
            const int k = it.seg;
            /* A segment's chunks come in order, so a gap means a reader
             * started over somewhere else: what is gathered goes first. */
            if (s_wr.st_len[k] > 0 && s_wr.st_off[k] + s_wr.st_len[k] != it.off) wr_flush(k);
            if (s_wr.st_len[k] == 0) s_wr.st_off[k] = it.off;
            memcpy(s_wr.stage + (size_t)k * DL_STAGE + s_wr.st_len[k], it.p, (size_t)it.len);
            s_wr.st_len[k] += it.len;
            xQueueSend(s_wr.free_, &it, portMAX_DELAY);     /* the buffer is free once copied */
            if (s_wr.st_len[k] + DL_CHUNK > DL_STAGE) wr_flush(k);
            continue;
        }
        xQueueSend(s_wr.free_, &it, portMAX_DELAY);
    }
    if (s_wr.stage)
        for (int k = 0; k < DL_SEGS_MAX; k++) wr_flush(k);    /* the rest, before the end */
    ESP_LOGD(TAG, "writer: stack %u bytes never used of %u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)DL_WR_STACK);
    xSemaphoreGive(s_wr.done);
    vTaskDelete(NULL);
}

/*
 * THE READERS (Defeatist 8043). A download in segments has a reader a
 * segment: dl_task for the first not yet whole, on the connection that
 * asked for the file, and up to FECKLESS_FETCH_CONNS_MAX - 1 helpers for
 * the others, each on its own connection. Every reader takes a free
 * buffer, reads at most to its segment's end, and hands the chunk to the
 * writer. s_rcv[k] is the next byte segment k's reader wants, written
 * only by that reader. A failure that the retries do not mend sets
 * s_abort and the rest stop at their next chunk; the part file and its
 * map stay.
 */
enum { SRV_DONE = 0, SRV_RERR, SRV_WERR, SRV_STOP, SRV_NOOPEN };

static PSRAM_BSS seg_t  s_plan[DL_SEGS_MAX];
static int             s_nplan;
static PSRAM_BSS int64_t s_rcv[DL_SEGS_MAX];
static volatile bool   s_abort;
/* The first byte no reader has taken, and the stripe size; under lock().
 * s_front is the total for a map in thirds (nothing to take). */
static int64_t         s_front, s_stripe;
static int             s_conn_new, s_conn_reuse;   /* requests on new and kept connections */

typedef struct {
    int64_t t_net, t_wait, t_mark, got_mark, w_net, w_wait, wl_mark, ww_mark;
} dl_timing_t;

/* Bytes added, from any reader. */
static int add_progress(int n)
{
    lock();
    s_got += n;
    const int64_t step = s_got * 10 >> 20;
    if (step != s_step) { s_step = step; s_gen++; }
    const int stop = s_stop;
    unlock();
    return stop;
}

/* Read segment k from c until its end, the stream's end, an error or a
 * stop. `tm` is dl_task's timing, NULL for a helper. */
static int serve(esp_http_client_handle_t c, int k, dl_timing_t *tm)
{
    for (;;) {
        if (s_abort) return SRV_STOP;
        wr_item_t it;
        const int64_t t0 = esp_timer_get_time();
        xQueueReceive(s_wr.free_, &it, portMAX_DELAY);
        const int64_t t1 = esp_timer_get_time();
        if (tm) { tm->t_wait += t1 - t0; tm->w_wait += t1 - t0; }
        if (s_wr.err) { xQueueSend(s_wr.free_, &it, 0); return SRV_WERR; }
        const int64_t off = s_rcv[k];
        int want = DL_CHUNK;
        if (s_plan[k].end >= 0) {                   /* no further than the segment */
            const int64_t left = s_plan[k].end - off;
            if (left <= 0) { xQueueSend(s_wr.free_, &it, 0); return SRV_DONE; }
            if (left < want) want = (int)left;
        }
        const int n = esp_http_client_read(c, (char *)it.p, want);
        const int64_t t2 = esp_timer_get_time();
        if (tm) { tm->t_net += t2 - t1; tm->w_net += t2 - t1; }
        if (n <= 0) {
            xQueueSend(s_wr.free_, &it, 0);
            if (n < 0) ESP_LOGW(TAG, "segment %d: read failed at %lld", k + 1, (long long)off);
            return n < 0 ? SRV_RERR : SRV_DONE;
        }
        it.len = n;
        it.seg = k;
        it.off = off;
        s_rcv[k] = off + n;
        xQueueSend(s_wr.full, &it, portMAX_DELAY);
        const int stop = add_progress(n);
        if (tm && t2 - tm->t_mark >= 5000000) {
            const int64_t span = t2 - tm->t_mark;
            const int64_t wl = s_wr.t_lease, ww = s_wr.t_write;
            lock();
            const int64_t got = s_got;
            unlock();
            ESP_LOGI(TAG, "timing 5s: %lld kbit/s, net %d%%, wait %d%%, lease %d%%, write %d%%, "
                     "internal free %u min %u, TCP_WND %d, %d connection(s)",
                     (long long)((got - tm->got_mark) * 8000 / span),
                     (int)(tm->w_net * 100 / span), (int)(tm->w_wait * 100 / span),
                     (int)((wl - tm->wl_mark) * 100 / span), (int)((ww - tm->ww_mark) * 100 / span),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (int)TCP_WND, s_nplan > 1 ? s_nplan : 1);
            tm->t_mark = t2; tm->got_mark = got;
            tm->wl_mark = wl; tm->ww_mark = ww;
            tm->w_net = tm->w_wait = 0;
        }
        if (stop != RUN) return SRV_STOP;
    }
}

/* Whether a reply is exactly segment k's rest. */
static bool range_ok(const hdrs_t *h, int status, int k)
{
    const int64_t from = s_rcv[k], to = s_plan[k].end - 1;
    long long a = -1, b = -1, t = -1;
    const int n = sscanf(h->crange, "bytes %lld-%lld/%lld", &a, &b, &t);
    if (status == 206 && n >= 2 && a == from && b == to && (n < 3 || s_wr.total <= 0 || t == s_wr.total))
        return true;
    ESP_LOGW(TAG, "segment %d: HTTP %d, Content-Range \"%s\" for %lld-%lld; not this way",
             k + 1, status, h->crange, (long long)from, (long long)to);
    return false;
}

/* Open segment k's rest on a connection of its own. NULL if the server
 * will not give exactly that. */
static esp_http_client_handle_t open_segment(char *url, hdrs_t *h, int k)
{
    int status = 0;
    snprintf(h->if_range, sizeof(h->if_range), "%s", s_wr.val);
    esp_http_client_handle_t c = open_followed(url, h, &status, s_rcv[k], s_plan[k].end - 1);
    if (!c) return NULL;
    if (range_ok(h, status, k)) {
        lock(); s_conn_new++; unlock();
        return c;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return NULL;
}

/* Ask for segment k's rest on a connection whose last reply has been
 * read to its end: HTTP/1.1 keeps it open, and esp_http_client_open()
 * sends on it without connecting again. False if the server closed it or
 * answered otherwise; the caller then makes a new one. */
static bool reopen_segment(esp_http_client_handle_t c, hdrs_t *h, int k)
{
    char range[48];
    snprintf(range, sizeof(range), "bytes=%lld-%lld", (long long)s_rcv[k], (long long)(s_plan[k].end - 1));
    esp_http_client_set_header(c, "Range", range);
    if (h->if_range[0]) esp_http_client_set_header(c, "If-Range", h->if_range);
    h->location[0] = h->ctype[0] = h->etag[0] = h->lastmod[0] = h->crange[0] = h->ranges[0] = '\0';
    if (esp_http_client_open(c, 0) != ESP_OK) return false;
    esp_http_client_fetch_headers(c);
    if (!range_ok(h, esp_http_client_get_status_code(c), k)) return false;
    lock(); s_conn_reuse++; unlock();
    return true;
}

/* Segment k takes the next stripe, if there is one, and tells the writer
 * -- in the queue, after the old stripe's last chunk. */
static bool claim(int k)
{
    lock();
    const int64_t total = s_total;
    const bool any = s_stripe > 0 && total > 0 && s_front < total;
    int64_t start = 0, end = 0;
    if (any) {
        start = s_front;
        end = start + s_stripe < total ? start + s_stripe : total;
        s_front = end;
    }
    unlock();
    if (!any) return false;
    s_plan[k].start = start;
    s_plan[k].end = end;
    s_plan[k].done = 0;
    s_rcv[k] = start;
    const wr_item_t m = { NULL, WR_CLAIM, k, start, end };
    xQueueSend(s_wr.full, &m, portMAX_DELAY);
    return true;
}

/*
 * A FAILED CONNECTION TRIES AGAIN (Defeatist 8057). The connection that
 * failed waits and asks again for the rest of its stripe, on a new
 * connection, up to DL_RETRIES times, 1 s, 3 s and 10 s apart; the
 * others go on meanwhile. A retry that gets any further starts the count
 * again. A pause or a cancel ends the wait at once.
 *
 * False when the retries are spent, or a stop or another's failure came
 * during the wait; true to try again.
 */
static bool retry_wait(int *fails, int k)
{
    static const int wait_ms[DL_RETRIES] = { 1000, 3000, 10000 };
    if (*fails >= DL_RETRIES) return false;
    const int ms = wait_ms[*fails];
    (*fails)++;
    ESP_LOGW(TAG, "segment %d: trying again at %lld in %d s (%d of %d)", k + 1,
             (long long)s_rcv[k], ms / 1000, *fails, DL_RETRIES);
    for (int t = 0; t < ms; t += 100) {
        lock();
        const int stop = s_stop;
        unlock();
        if (stop != RUN || s_abort) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

/* One reader's whole share: its segment, then each stripe it can claim,
 * on one connection while the server keeps it. `c` is a connection
 * already open on the segment (dl_task's first), or NULL. A 200 read only
 * part way cannot be asked on again; is_complete_data_received() says. */
static int run_slot(int k, char *url, hdrs_t *h, esp_http_client_handle_t c, dl_timing_t *tm)
{
    int res = SRV_DONE;
    int fails = 0;
    bool ready = c != NULL;                         /* c already answers segment k */
    for (;;) {
        if (s_plan[k].end >= 0 && s_rcv[k] >= s_plan[k].end) {
            if (!claim(k)) break;
            ready = false;
        }
        if (!ready && !(c && reopen_segment(c, h, k))) {
            if (c) {
                esp_http_client_close(c);
                esp_http_client_cleanup(c);
            }
            c = open_segment(url, h, k);
            if (!c) {
                if (fails > 0 && retry_wait(&fails, k)) continue;   /* still down */
                res = SRV_NOOPEN;
                break;
            }
        }
        ready = false;
        const int64_t before = s_rcv[k];
        res = serve(c, k, tm);
        const bool short_reply = res == SRV_DONE && s_plan[k].end >= 0 && s_rcv[k] < s_plan[k].end;
        if (short_reply)
            ESP_LOGW(TAG, "segment %d: reply ended at %lld of %lld", k + 1,
                     (long long)s_rcv[k], (long long)s_plan[k].end);
        if (res == SRV_RERR || short_reply) {
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            c = NULL;
            if (s_rcv[k] > before) fails = 0;       /* it got further: a fresh count */
            if (retry_wait(&fails, k)) { res = SRV_DONE; continue; }
            res = SRV_RERR;
            break;
        }
        if (res != SRV_DONE) break;
        if (s_plan[k].end < 0) break;               /* no length: the stream's end is the end */
        if (s_rcv[k] > before) fails = 0;
        if (!esp_http_client_is_complete_data_received(c)) {
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            c = NULL;
        }
    }
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    return res;
}

/*
 * The helpers: created once and never deleted -- a task made per
 * download would take its stack from whichever heap at that moment.
 * Stacks in PSRAM (sockets and TLS, never the card or flash), TCBs
 * internal. Without PSRAM stacks there are none.
 */
#define DL_HELPERS (FECKLESS_FETCH_CONNS_MAX - 1)
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM && DL_HELPERS > 0
#define DL_CAN_HELP 1
#else
#define DL_CAN_HELP 0
#endif
static StaticTask_t *s_help_tcb[DL_HELPERS > 0 ? DL_HELPERS : 1];
static struct {
    TaskHandle_t  task;
    StackType_t  *stack;
    int           seg;
    volatile int  res;
    char          url[DL_URL_MAX];
    hdrs_t        h;
} s_help[DL_HELPERS > 0 ? DL_HELPERS : 1] PSRAM_BSS;
static SemaphoreHandle_t s_help_done;

static void help_task(void *arg)
{
    const int j = (int)(intptr_t)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int k = s_help[j].seg;
        s_help[j].res = run_slot(k, s_help[j].url, &s_help[j].h, NULL, NULL);
        if (s_help[j].res == SRV_RERR || s_help[j].res == SRV_WERR) s_abort = true;
        /* SRV_NOOPEN: not served; dl_task takes it after */
        xSemaphoreGive(s_help_done);
    }
}

/* `n` helpers ready, creating any not yet made. */
static bool helpers_ready(int n)
{
#if DL_CAN_HELP
    if (n <= 0) return false;
    if (!s_help_done) s_help_done = xSemaphoreCreateCounting(DL_HELPERS, 0);
    if (!s_help_done) return false;
    for (int j = 0; j < n && j < DL_HELPERS; j++) {
        if (s_help[j].task) continue;
        if (!s_help[j].stack)
            s_help[j].stack = heap_caps_malloc(DL_HELP_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_help[j].stack) return false;
        if (!s_help_tcb[j])
            s_help_tcb[j] = heap_caps_calloc(1, sizeof(*s_help_tcb[j]), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_help_tcb[j]) return false;
        s_help[j].task = xTaskCreateStatic(help_task, "fetch_h", DL_HELP_STACK, (void *)(intptr_t)j,
                                           3, s_help[j].stack, s_help_tcb[j]);
        if (!s_help[j].task) return false;
    }
    return true;
#else
    (void)n;
    return false;
#endif
}

static void dl_task(void *arg)
{
    (void)arg;
    static PSRAM_BSS char url[DL_URL_MAX], part[DL_PART_MAX], path[FECKLESS_FETCH_PATH_MAX];
    static PSRAM_BSS hdrs_t h;
    lock();
    snprintf(url, sizeof(url), "%s", s_url);
    snprintf(part, sizeof(part), "%s", s_partp);
    snprintf(path, sizeof(path), "%s", s_path);
    const int conns = s_conns;
    const int64_t split_min = s_split_min;
    unlock();
    memset(&h, 0, sizeof(h));

    feckless_fetch_state_t st = FECKLESS_FETCH_FAILED;
    uint8_t *buf = heap_caps_malloc((size_t)DL_NBUF * DL_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_http_client_handle_t c = NULL;
    FILE *f = NULL;
    int status = 0;
    if (!buf) { set_why("out of memory"); goto out; }

    event(FECKLESS_FETCH_EV_START, url, NULL, part, path, sizeof(path), 0);
    storage_io_acquire(STORAGE_IO_BACKGROUND);
    make_dirs_for(part);
    storage_io_release();

    /*
     * A part file already there is the start of this download -- paused,
     * cut off by a reboot or a dropped link -- and the rest is asked for
     * with Range. One segment written in order resumes from the file's
     * size, which is never past what was written; more than one, from the
     * map. The size is stat()'s off_t: a part past 2 GB here starts over
     * rather than resuming at a wrong place.
     */
    static PSRAM_BSS seg_t seg[DL_SEGS_MAX];
    int nseg = 0;
    int64_t id_total = -1;
    int64_t front = -1, stripe = 0;
    {
        struct stat pst;
        int64_t psize = 0;
        storage_io_acquire(STORAGE_IO_BACKGROUND);
        if (stat(part, &pst) == 0 && S_ISREG(pst.st_mode) && pst.st_size > 0) psize = (int64_t)pst.st_size;
        const int n = psize > 0 ? id_read(part, h.if_range, sizeof(h.if_range), &id_total, seg, DL_SEGS_MAX,
                                          &front, &stripe) : -1;
        storage_io_release();
        if (psize > 0 && n < 0) ESP_LOGI(TAG, "part file has no .id; checking Content-Range only");
        if (psize > 0 && n <= 1) {
            seg[0].start = 0;
            seg[0].end = id_total > 0 ? id_total : -1;
            seg[0].done = psize;
            nseg = 1;
        } else if (n > 1) {
            nseg = n;
            if (front < 0) front = id_total;        /* a map in thirds: nothing past them */
        }
        if (nseg == 1) front = -1;
    }
    int k0 = 0;                                     /* this connection's segment */
    while (k0 < nseg && seg[k0].end >= 0 && seg[k0].done >= seg[k0].end - seg[k0].start) k0++;
    if (nseg > 1 && k0 == nseg && stripe > 0 && id_total > 0 && front < id_total) {
        /* Every stripe in hand is whole; the first one's slot takes the
         * next, and the rest are claimed as the readers go. */
        k0 = 0;
        seg[0].start = front;
        seg[0].end = front + stripe < id_total ? front + stripe : id_total;
        seg[0].done = 0;
        front = seg[0].end;
    }
    if (nseg > 0 && k0 == nseg) {
        /* Whole, but not renamed: a reboot between the two. Rare enough
         * that the file is asked for again rather than finished blind. */
        ESP_LOGI(TAG, "part file's map says it is whole; starting over");
        nseg = 0;
        k0 = 0;
        h.if_range[0] = '\0';
    }
    int64_t from = nseg ? seg[k0].start + seg[k0].done : 0;
    int64_t to = (nseg > 1 && seg[k0].end > 0) ? seg[k0].end - 1 : -1;
    for (int attempt = 0; attempt < 2; attempt++) {
        c = open_followed(url, &h, &status, from, to);
        if (!c) goto out;
        /* A 206 is the rest of the same file only if it says so. */
        const char *bad = NULL;
        if (status == 206 && (from > 0 || to >= 0)) {
            long long a = -1, b = -1, t = -1;
            const int n = sscanf(h.crange, "bytes %lld-%lld/%lld", &a, &b, &t);
            if (n < 2) bad = "no Content-Range";
            else if (a != from) bad = "Content-Range starts elsewhere";
            else if (to >= 0 && b != to) bad = "Content-Range ends elsewhere";
            else if (n == 3 && id_total > 0 && t != id_total) bad = "the file's size changed";
            else if (n == 3 && id_total <= 0) id_total = t;
            if (bad) ESP_LOGW(TAG, "resume refused, %s (%s, from %lld, saved total %lld)",
                              bad, h.crange, (long long)from, (long long)id_total);
        }
        if ((status == 416 && nseg > 0) || bad) {
            if (!bad) ESP_LOGI(TAG, "416 for the rest from %lld; starting over", (long long)from);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            c = NULL;
            lock(); snprintf(url, sizeof(url), "%s", s_url); unlock();
            nseg = 0; k0 = 0; from = 0; to = -1;
            id_total = -1;
            front = -1; stripe = 0;
            h.if_range[0] = '\0';
            continue;
        }
        break;
    }
    if (status == 200) {
        if (nseg > 0) ESP_LOGI(TAG, "200 for the rest from %lld: %s; starting over", (long long)from,
                               h.if_range[0] ? "the file changed" : "no ranges");
        nseg = 0; k0 = 0; from = 0; to = -1;
        front = -1; stripe = 0;
    } else if (status != 206 || nseg == 0) {
        ESP_LOGW(TAG, "HTTP %d", status);
        char why[32];
        snprintf(why, sizeof(why), "the server said %d", status);
        set_why(why);
        goto out;
    }

    /* The caller may choose the name now, from what answered. */
    event(FECKLESS_FETCH_EV_HEADERS, url, h.ctype, part, path, sizeof(path), 0);
    const bool fresh = nseg == 0;
    if (fresh) {
        const int64_t clen = esp_http_client_get_content_length(c);
        id_total = clen > 0 ? clen : -1;
        seg[0].start = 0;
        seg[0].end = id_total;
        seg[0].done = 0;
        nseg = 1;
        /* In stripes, when the server does ranges, the file is big enough
         * to be worth two more handshakes, and small enough to seek in.
         * This connection's 200 is the first stripe, read only to its end. */
        if (conns > 1 && id_total >= split_min && id_total <= LONG_MAX &&
            !strcasecmp(h.ranges, "bytes") && helpers_ready(conns - 1)) {
            nseg = conns;
            stripe = DL_STRIPE;
            for (int i = 0; i < nseg; i++) {
                seg[i].start = stripe * i;
                seg[i].end = stripe * (i + 1) < id_total ? stripe * (i + 1) : id_total;
                seg[i].done = 0;
            }
            front = stripe * nseg < id_total ? stripe * nseg : id_total;
        } else if (conns > 1 && id_total >= split_min) {
            ESP_LOGI(TAG, "one connection: Accept-Ranges \"%s\"%s%s", h.ranges,
                     id_total > LONG_MAX ? ", and past 2 GB" : "",
                     DL_CAN_HELP ? "" : ", and no helpers in this build");
        }
    } else if (seg[0].end < 0 && id_total > 0 && nseg == 1) {
        seg[0].end = id_total;                      /* a part file with no .id */
    }
    int64_t have = 0;                               /* on the card already */
    for (int i = 0; i < nseg; i++) have += seg[i].done;
    if (front >= 0) {                               /* and the whole stripes before front */
        int64_t inseg = 0;
        for (int i = 0; i < nseg; i++) inseg += seg[i].end - seg[i].start;
        have += front - inseg;
    }
    lock();
    snprintf(s_path, sizeof(s_path), "%s", path);
    s_total = id_total;
    s_got = have;
    s_ready = ready_of(seg, nseg, front);           /* what an earlier run left, synced */
    unlock();

    /* One segment appends; a map writes in place. */
    storage_io_acquire(STORAGE_IO_BACKGROUND);
    f = storage_io_open(part, fresh ? "wb" : nseg == 1 ? "ab" : "r+b");
    if (f) {
        snprintf(s_wr.val, sizeof(s_wr.val), "%s", fresh || !h.if_range[0] ? validator(&h) : h.if_range);
        id_write(part, s_wr.val, id_total, seg, nseg, front, stripe);
    }
    storage_io_release();
    if (!f) {
        ESP_LOGW(TAG, "could not open %s", part);
        set_why("could not write the card");
        goto out;
    }
    ESP_LOGI(TAG, "to %s (%lld bytes%s, from %lld, segment %d of %d, %s)", path, (long long)id_total,
             id_total < 0 ? ", length not given" : "", (long long)from, k0 + 1, nseg,
             stripe > 0 ? "in stripes" : nseg > 1 ? "in thirds" : "one stream");

    int64_t got = have;
    bool ok = true;
    int stop = RUN;

    /* The writer, its queues and buffers. */
    if (!s_wr.full) {
        s_wr.full  = xQueueCreate(DL_NBUF + 1, sizeof(wr_item_t));
        s_wr.free_ = xQueueCreate(DL_NBUF, sizeof(wr_item_t));
        s_wr.done  = xSemaphoreCreateBinary();
    }
    if (!s_wr.full || !s_wr.free_ || !s_wr.done) {
        ESP_LOGW(TAG, "no memory for the writer's queues");
        set_why("out of memory");
        ok = false;
        goto closed;
    }
    xQueueReset(s_wr.full);
    xQueueReset(s_wr.free_);
    xSemaphoreTake(s_wr.done, 0);
    for (int i = 0; i < DL_NBUF; i++) {
        wr_item_t it = { buf + (size_t)i * DL_CHUNK, 0, 0, 0, 0 };
        xQueueSend(s_wr.free_, &it, 0);
    }
    s_wr.f = f;
    s_wr.err = false;
    s_wr.err_no = 0;
    s_wr.put = 0;
    s_wr.t_lease = s_wr.t_write = 0;
    /* The map the writer keeps, and where the file is: at its end for
     * "ab" and "wb", unknown for "r+b" so the first chunk seeks. */
    memcpy(s_wr.seg, seg, sizeof(seg));
    s_wr.nseg = nseg;
    s_wr.total = id_total;
    s_wr.since_map = 0;
    s_wr.part = part;
    s_wr.pos = nseg == 1 ? from : -1;
    s_wr.front = front;
    s_wr.stripe = stripe;
    s_wr.stage = heap_caps_malloc((size_t)DL_SEGS_MAX * DL_STAGE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_wr.stage) ESP_LOGW(TAG, "no PSRAM for the write stages; writing each chunk as it comes");
    if (xTaskCreate(wr_task, "fetch_wr", DL_WR_STACK, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "could not start the writer");
        set_why("out of memory");
        ok = false;
        goto closed;
    }

    memcpy(s_plan, seg, sizeof(seg));
    s_nplan = nseg;
    lock();
    s_front = front;
    s_stripe = stripe;
    s_conn_new = 1;                                 /* dl_task's own */
    s_conn_reuse = 0;
    unlock();
    for (int i = 0; i < nseg; i++) s_rcv[i] = seg[i].start + seg[i].done;
    if (nseg == 1) s_rcv[0] = from;                 /* the size, not the map */
    s_abort = false;
    const int64_t t_start = esp_timer_get_time();
    dl_timing_t tm = { .t_mark = t_start, .got_mark = got };

    /* A helper for each other segment not yet whole; in stripes, a slot
     * whose stripe is whole claims the next. */
    int helping = 0;
    const int want_help = nseg - 1 < conns - 1 ? nseg - 1 : conns - 1;
    if (nseg > 1 && want_help > 0 && helpers_ready(want_help)) {
        for (int i = 0; i < nseg && helping < DL_HELPERS && helping < conns - 1; i++) {
            if (i == k0 || (s_rcv[i] >= seg[i].end && stripe == 0)) continue;
            snprintf(s_help[helping].url, sizeof(s_help[helping].url), "%s", url);
            memset(&s_help[helping].h, 0, sizeof(s_help[helping].h));
            s_help[helping].seg = i;
            s_help[helping].res = SRV_DONE;
            xTaskNotifyGive(s_help[helping].task);
            helping++;
        }
    }

    int res = run_slot(k0, url, &h, c, &tm);        /* its stripe, and the next */
    c = NULL;                                       /* run_slot() closed it */
    if (res == SRV_RERR || res == SRV_WERR) s_abort = true;
    for (int j = 0; j < helping; j++) xSemaphoreTake(s_help_done, portMAX_DELAY);
    for (int j = 0; j < helping && res == SRV_DONE; j++)
        if (s_help[j].res != SRV_DONE && s_help[j].res != SRV_NOOPEN) res = s_help[j].res;

    /* A segment nobody finished -- its helper could not get it, or the map
     * has more segments than there are helpers -- is dl_task's, one after
     * another, each on a connection of its own; then the stripes still to
     * claim after them. */
    static PSRAM_BSS hdrs_t h2;
    for (int i = 0; i < nseg && res == SRV_DONE && nseg > 1; i++) {
        if (s_rcv[i] >= s_plan[i].end) continue;
        memset(&h2, 0, sizeof(h2));
        res = run_slot(i, url, &h2, NULL, &tm);
        if (res == SRV_NOOPEN) res = SRV_RERR;
    }
    if (res == SRV_RERR) { ok = false; set_why("the connection broke"); }
    if (res == SRV_WERR) {
        ok = false;
        set_why("could not write the card");
        ESP_LOGW(TAG, "write failed after %lld bytes (errno %d)",
                 (long long)(have + s_wr.put), s_wr.err_no);
    }
    lock();
    stop = s_stop;
    unlock();
    if (res != SRV_STOP && stop != RUN) stop = RUN;     /* asked after the last chunk: done is done */
    const int64_t t_net = tm.t_net, t_wait = tm.t_wait;
    /* The end marker, then wait for every chunk before it to land. */
    {
        const wr_item_t end = { NULL, 0, 0, 0, 0 };
        xQueueSend(s_wr.full, &end, portMAX_DELAY);
        xSemaphoreTake(s_wr.done, portMAX_DELAY);
    }
    if (s_wr.err) {
        if (ok) ESP_LOGW(TAG, "write failed after %lld bytes (errno %d)",
                         (long long)(have + s_wr.put), s_wr.err_no);
        set_why("could not write the card");
        ok = false;
    }
    /* What the part file holds, by the writer's map, saved once more so a
     * pause or a failure resumes from all of it. */
    got = 0;
    for (int i = 0; i < s_wr.nseg; i++) got += s_wr.seg[i].done;
    if (s_wr.front >= 0) {
        int64_t inseg = 0;
        for (int i = 0; i < s_wr.nseg; i++) inseg += s_wr.seg[i].end - s_wr.seg[i].start;
        got += s_wr.front - inseg;
    }
    if (stop != STOP_CANCEL) {
        storage_io_acquire(STORAGE_IO_BACKGROUND);
        if (fflush(f) == 0) fsync(fileno(f));
        id_write(part, s_wr.val, s_wr.total, s_wr.seg, s_wr.nseg, s_wr.front, s_wr.stripe);
        storage_io_release();
        set_ready(ready_of(s_wr.seg, s_wr.nseg, s_wr.front));
    }
    {
        const int64_t span = esp_timer_get_time() - t_start;
        if (span > 0)
            ESP_LOGI(TAG, "timing total: %lld bytes in %lld ms = %lld kbit/s; net %d%%, wait %d%%, "
                     "lease %d%%, write %d%%; %s, %d request(s) on new connections, %d on kept ones",
                     (long long)(got - have), (long long)(span / 1000),
                     (long long)((got - have) * 8000 / span),
                     (int)(t_net * 100 / span), (int)(t_wait * 100 / span),
                     (int)(s_wr.t_lease * 100 / span), (int)(s_wr.t_write * 100 / span),
                     s_wr.stripe > 0 ? "stripes" : s_wr.nseg > 1 ? "thirds" : "one stream",
                     s_conn_new, s_conn_reuse);
    }
closed:
    storage_io_acquire(STORAGE_IO_BACKGROUND);
    if (storage_io_close(f) != 0) { ok = false; set_why("could not write the card"); }
    f = NULL;
    if (stop == STOP_PAUSE) {
        storage_io_release();
        st = FECKLESS_FETCH_PAUSED;
        ESP_LOGI(TAG, "paused at %lld bytes", (long long)got);
        goto out;
    }
    if (stop == STOP_CANCEL) {
        remove_part(part);
        storage_io_release();
        st = FECKLESS_FETCH_IDLE;
        ESP_LOGI(TAG, "cancelled at %lld bytes", (long long)got);
        event(FECKLESS_FETCH_EV_CANCELLED, url, NULL, part, path, sizeof(path), got);
        goto out;
    }
    storage_io_release();
    if (ok && s_total > 0 && got != s_total) {
        ESP_LOGW(TAG, "ended at %lld of %lld bytes", (long long)got, (long long)s_total);
        set_why("cut short");
        ok = false;
    }
    if (ok && !event(FECKLESS_FETCH_EV_VERIFY, url, h.ctype, part, path, sizeof(path), got)) {
        /* Refused: asking for the same bytes again would only fail again. */
        ESP_LOGW(TAG, "the caller refused %s", part);
        set_why("the file is not what was expected");
        storage_io_acquire(STORAGE_IO_BACKGROUND);
        remove_part(part);
        storage_io_release();
        ok = false;
    }
    if (ok) {
        storage_io_acquire(STORAGE_IO_BACKGROUND);
        make_dirs_for(path);
        remove(path);                  /* a stale one, if any; rename will not replace */
        if (rename(part, path) != 0) {
            ESP_LOGW(TAG, "could not rename %s (errno %d)", part, errno);
            set_why("could not write the card");
            ok = false;
        }
        if (ok) remove_id(part);
        storage_io_release();
    }
    /* A part that failed is kept -- a dropped link is the usual reason,
     * and the next start takes up from it. Only a cancel removes it. */
    if (ok) {
        st = FECKLESS_FETCH_DONE;
        set_why("");
        ESP_LOGI(TAG, "done: %lld bytes", (long long)got);
        lock();
        snprintf(s_path, sizeof(s_path), "%s", path);
        unlock();
        event(FECKLESS_FETCH_EV_DONE, url, h.ctype, part, path, sizeof(path), got);
    }

out:
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    heap_caps_free(buf);
    heap_caps_free(s_wr.stage);
    s_wr.stage = NULL;
    ESP_LOGD(TAG, "stack: %u bytes never used of %u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)DL_STACK);
    finish(st);
    vTaskDelete(NULL);
}

bool feckless_fetch_start(const feckless_fetch_opts_t *o)
{
    const char *why = NULL;
    if (!o || !o->url || !o->url[0] || !o->path || !o->path[0]) why = "no url or path";
    else if (strlen(o->url) >= DL_URL_MAX) why = "the url is too long";
    else if (strlen(o->path) >= FECKLESS_FETCH_PATH_MAX) why = "the path is too long";
    else if (o->part && strlen(o->part) >= DL_PART_MAX) why = "the path is too long";
    else if (!o->part && strlen(o->path) + 5 >= DL_PART_MAX) why = "the path is too long";
    else if (o->https_only && is_http(o->url)) why = "not https";
    lock();
    if (!why && s_state == FECKLESS_FETCH_RUNNING) why = "one is running";
    if (why) {
        snprintf(s_why, sizeof(s_why), "%s", why);
        unlock();
        ESP_LOGW(TAG, "not started: %s", why);
        return false;
    }
    snprintf(s_url, sizeof(s_url), "%s", o->url);
    snprintf(s_path, sizeof(s_path), "%s", o->path);
    if (o->part) snprintf(s_partp, sizeof(s_partp), "%s", o->part);
    else         snprintf(s_partp, sizeof(s_partp), "%s.part", o->path);
    s_conns = o->conns < 1 ? 1 : o->conns > FECKLESS_FETCH_CONNS_MAX ? FECKLESS_FETCH_CONNS_MAX : o->conns;
    s_split_min = o->split_min > 0 ? o->split_min : FECKLESS_FETCH_SPLIT_MIN;
    snprintf(s_ua, sizeof(s_ua), "%s", o->user_agent ? o->user_agent : "");
    s_https_only = o->https_only;
    s_cb = o->cb;
    s_arg = o->arg;
    s_ready = 0;
    s_id = o->id;
    s_got = 0;
    s_total = -1;
    s_step = -1;
    s_stop = RUN;
    s_why[0] = '\0';
    s_state = FECKLESS_FETCH_RUNNING;
    s_gen++;
    unlock();
    if (xTaskCreate(dl_task, "fetch", DL_STACK, NULL, 3, NULL) != pdPASS) {
        set_why("out of memory");
        finish(FECKLESS_FETCH_FAILED);
        return false;
    }
    ESP_LOGI(TAG, "%lld: downloading %.100s", (long long)o->id, o->url);
    return true;
}

void feckless_fetch_pause(void)
{
    lock();
    if (s_state == FECKLESS_FETCH_RUNNING) s_stop = STOP_PAUSE;
    unlock();
}

void feckless_fetch_cancel(void)
{
    static PSRAM_BSS char part[DL_PART_MAX], path[FECKLESS_FETCH_PATH_MAX];
    lock();
    const feckless_fetch_state_t st = s_state;
    if (st == FECKLESS_FETCH_RUNNING) s_stop = STOP_CANCEL;
    if (st == FECKLESS_FETCH_PAUSED || st == FECKLESS_FETCH_FAILED) {
        snprintf(part, sizeof(part), "%s", s_partp);
        snprintf(path, sizeof(path), "%s", s_path);
        s_state = FECKLESS_FETCH_IDLE;
        s_gen++;
    }
    unlock();
    if (st == FECKLESS_FETCH_PAUSED || st == FECKLESS_FETCH_FAILED) {
        storage_io_acquire(STORAGE_IO_BACKGROUND);
        if (part[0]) remove_part(part);
        storage_io_release();
        ESP_LOGI(TAG, "cancelled; part file removed");
        event(FECKLESS_FETCH_EV_CANCELLED, NULL, NULL, part, path, sizeof(path), 0);
    }
}

feckless_fetch_state_t feckless_fetch_state(int64_t *id, int64_t *got, int64_t *total,
                                            char *path, size_t path_size, uint32_t *gen)
{
    lock();
    const feckless_fetch_state_t st = s_state;
    if (id) *id = s_id;
    if (got) *got = s_got;
    if (total) *total = s_total;
    if (path && path_size) copy_out(path, path_size, s_path);
    if (gen) *gen = s_gen;
    unlock();
    return st;
}

void feckless_fetch_why(char *out, size_t out_size)
{
    if (!out || !out_size) return;
    lock();
    copy_out(out, out_size, s_why);
    unlock();
}

bool feckless_fetch_live(const char *path, char *part, size_t part_size,
                         int64_t *ready, int64_t *total, feckless_fetch_state_t *st)
{
    if (!path || !path[0]) return false;
    lock();
    const bool mine = s_path[0] && !strcmp(path, s_path);
    if (mine) {
        if (part && part_size) copy_out(part, part_size, s_partp);
        if (ready) *ready = s_ready;
        if (total) *total = s_total;
        if (st) *st = s_state;
    }
    unlock();
    return mine;
}

bool feckless_fetch_part(const char *part, int64_t *size)
{
    if (!part) return false;
    struct stat st;
    storage_io_acquire(STORAGE_IO_BACKGROUND);
    const bool there = stat(part, &st) == 0 && S_ISREG(st.st_mode);
    storage_io_release();
    if (there && size) *size = (int64_t)st.st_size;
    return there;
}

void feckless_fetch_discard(const char *part)
{
    lock();
    const bool running = s_state == FECKLESS_FETCH_RUNNING;
    unlock();
    if (running || !part) return;
    storage_io_acquire(STORAGE_IO_BACKGROUND);
    remove_part(part);
    storage_io_release();
    lock();
    if (s_state != FECKLESS_FETCH_RUNNING) { s_state = FECKLESS_FETCH_IDLE; s_gen++; }
    unlock();
    ESP_LOGI(TAG, "part file discarded");
}
