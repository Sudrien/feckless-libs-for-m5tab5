/*
 * fetchtest.c -- feckless_fetch.c on the host (9003), its tasks on
 * pthreads and the network an in-memory server (fake/fetchfake.c).
 * Every download's file is compared byte for byte with what was served,
 * and every run checks the lease rules: never taken twice by one task,
 * never held across a network read, never left held.
 *
 *   make -C test run-fetch
 *
 * FETCHTEST_LOG=1 prints the component's log.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fake/fetchfake.h"
#include "feckless_fetch.h"

static int  s_fail;
static char s_dir[256];

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* ---- helpers ------------------------------------------------------------- */

static uint8_t *body_of(int64_t len, uint32_t seed)
{
    uint8_t *b = malloc((size_t)len);
    uint32_t x = seed * 2654435761u + 1;
    for (int64_t i = 0; i < len; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[i] = (uint8_t)x; }
    return b;
}

static bool exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static bool same_as(const char *p, const uint8_t *want, int64_t len)
{
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    uint8_t *got = malloc((size_t)len + 1);
    const size_t n = fread(got, 1, (size_t)len + 1, f);
    fclose(f);
    const bool ok = (int64_t)n == len && memcmp(got, want, (size_t)len) == 0;
    free(got);
    return ok;
}

static void path_in(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", s_dir, name);
}

/* Until it is not running, or 60 s. */
static feckless_fetch_state_t wait_done(void)
{
    for (int i = 0; i < 30000; i++) {
        const feckless_fetch_state_t st = feckless_fetch_state(NULL, NULL, NULL, NULL, 0, NULL);
        if (st != FECKLESS_FETCH_RUNNING) return st;
        usleep(2000);
    }
    fprintf(stderr, "FAIL: still running after 60 s\n");
    s_fail++;
    exit(1);
}

/* Until `n` bytes are in, or it stops. */
static void wait_got(int64_t n)
{
    for (int i = 0; i < 30000; i++) {
        int64_t got = 0;
        if (feckless_fetch_state(NULL, &got, NULL, NULL, 0, NULL) != FECKLESS_FETCH_RUNNING || got >= n) return;
        usleep(1000);
    }
}

static bool why_is(const char *want)
{
    char why[64];
    feckless_fetch_why(why, sizeof(why));
    if (strcmp(why, want) == 0) return true;
    fprintf(stderr, "  why: \"%s\", wanted \"%s\"\n", why, want);
    return false;
}

/* The callback: records the events, can refuse VERIFY and can rename at
 * HEADERS. */
static struct {
    feckless_fetch_ev_kind_t kinds[16];
    int     n;
    bool    refuse;
    const char *rename_ext;
    int64_t verified_size;
    bool    id_beside_at_verify;
} s_ev;

static bool cb(feckless_fetch_ev_t *ev)
{
    if (s_ev.n < 16) s_ev.kinds[s_ev.n++] = ev->kind;
    if (ev->kind == FECKLESS_FETCH_EV_HEADERS && s_ev.rename_ext) {
        char p[FECKLESS_FETCH_PATH_MAX];
        snprintf(p, sizeof(p), "%s.%s", ev->path, s_ev.rename_ext);
        snprintf(ev->path, ev->path_size, "%s", p);
    }
    if (ev->kind == FECKLESS_FETCH_EV_VERIFY) {
        s_ev.verified_size = ev->size;
        char id[FECKLESS_FETCH_PATH_MAX + 8];
        snprintf(id, sizeof(id), "%s.id", ev->part);
        s_ev.id_beside_at_verify = exists(id);
        return !s_ev.refuse;
    }
    return true;
}

static void ev_reset(void) { memset(&s_ev, 0, sizeof(s_ev)); }

static bool start(const char *url, const char *path, int conns)
{
    const feckless_fetch_opts_t o = { .url = url, .path = path, .conns = conns, .cb = cb, .id = 7 };
    return feckless_fetch_start(&o);
}

static void after_each(const char *name)
{
    CHECK(fake_lease_holders() == 0);
    if (fake_lease_violations()) { fprintf(stderr, "FAIL %s: lease rules broken\n", name); s_fail++; }
    printf("%s\n", name);
}

/* ---- the cases ----------------------------------------------------------- */

static void small_one_stream(void)
{
    const int64_t len = 100 * 1024 + 17;
    uint8_t *b = body_of(len, 1);
    const fake_route_t r[] = { { .url = "https://h/small", .body = b, .len = len, .etag = "\"a\"", .ranges = true } };
    fake_routes(r, 1);
    char p[300], part[310];
    path_in(p, sizeof(p), "sub/dir/small.bin");      /* folders made */
    snprintf(part, sizeof(part), "%s.part", p);
    ev_reset();
    CHECK(start("https://h/small", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));
    CHECK(!exists(part));
    snprintf(part, sizeof(part), "%s.part.id", p);
    CHECK(!exists(part));
    CHECK(s_ev.n == 4 && s_ev.kinds[0] == FECKLESS_FETCH_EV_START && s_ev.kinds[1] == FECKLESS_FETCH_EV_HEADERS &&
          s_ev.kinds[2] == FECKLESS_FETCH_EV_VERIFY && s_ev.kinds[3] == FECKLESS_FETCH_EV_DONE);
    CHECK(s_ev.verified_size == len && s_ev.id_beside_at_verify);
    int64_t id = 0, got = 0, total = 0;
    char sp[300];
    feckless_fetch_state(&id, &got, &total, sp, sizeof(sp), NULL);
    CHECK(id == 7 && got == len && total == len && !strcmp(sp, p));
    CHECK(why_is(""));
    free(b);
    after_each("one stream, folders made, events in order");
}

static void striped(void)
{
    const int64_t len = 20 * 1024 * 1024 + 12345;   /* past FECKLESS_FETCH_SPLIT_MIN */
    uint8_t *b = body_of(len, 2);
    const fake_route_t r[] = { { .url = "https://h/big", .body = b, .len = len, .etag = "\"b\"", .ranges = true } };
    fake_routes(r, 1);
    char p[300];
    path_in(p, sizeof(p), "big.bin");
    fake_stats_reset();
    ev_reset();
    CHECK(start("https://h/big", p, 3));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));
    CHECK(fake_max_concurrent() >= 2);
    CHECK(fake_bytes_served() == len);              /* nothing asked for twice */
    printf("  %d requests, at most %d connections at once\n", fake_requests(), fake_max_concurrent());
    free(b);
    after_each("three connections in stripes");
}

static void no_ranges(void)
{
    const int64_t len = 9 * 1024 * 1024;
    uint8_t *b = body_of(len, 3);
    const fake_route_t r[] = { { .url = "https://h/nr", .body = b, .len = len } };
    fake_routes(r, 1);
    char p[300];
    path_in(p, sizeof(p), "nr.bin");
    fake_stats_reset();
    CHECK(start("https://h/nr", p, 3));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));
    CHECK(fake_max_concurrent() == 1 && fake_requests() == 1);
    free(b);
    after_each("no Accept-Ranges: one connection");
}

static void no_length(void)
{
    /* Defeatist's poddl.c read nothing here: a segment ending at -1 was
     * "whole" before a byte, and an empty file was renamed into place. */
    const int64_t len = 300 * 1024;
    uint8_t *b = body_of(len, 4);
    const fake_route_t r[] = { { .url = "https://h/chunked", .body = b, .len = len, .no_length = true } };
    fake_routes(r, 1);
    char p[300];
    path_in(p, sizeof(p), "chunked.bin");
    CHECK(start("https://h/chunked", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));
    int64_t total = 0;
    feckless_fetch_state(NULL, NULL, &total, NULL, 0, NULL);
    CHECK(total == -1);
    free(b);
    after_each("no Content-Length: read to the stream's end");
}

static void redirects(void)
{
    const int64_t len = 5000;
    uint8_t *b = body_of(len, 5);
    const fake_route_t r[] = {
        { .url = "https://a.example/start", .status = 302, .location = "/hop" },
        { .url = "https://a.example/hop",   .status = 301, .location = "//b.example/file" },
        { .url = "https://b.example/file",  .body = b, .len = len },
        { .url = "https://a.example/loop",  .status = 302, .location = "https://a.example/loop" },
        { .url = "https://a.example/down",  .status = 307, .location = "http://c.example/file" },
        { .url = "http://c.example/file",   .body = b, .len = len },
    };
    fake_routes(r, 6);
    char p[300];
    path_in(p, sizeof(p), "redir.bin");
    CHECK(start("https://a.example/start", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));

    path_in(p, sizeof(p), "loop.bin");
    CHECK(start("https://a.example/loop", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_FAILED);
    CHECK(why_is("too many redirects"));

    /* http at the far end: allowed, unless https_only. */
    path_in(p, sizeof(p), "down.bin");
    CHECK(start("https://a.example/down", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));
    path_in(p, sizeof(p), "down2.bin");
    feckless_fetch_opts_t o = { .url = "https://a.example/down", .path = p, .https_only = true };
    CHECK(feckless_fetch_start(&o));
    CHECK(wait_done() == FECKLESS_FETCH_FAILED);
    CHECK(why_is("not https"));
    CHECK(!exists(p));
    o.url = "http://c.example/file";
    CHECK(!feckless_fetch_start(&o));               /* refused before it starts */
    CHECK(why_is("not https"));
    free(b);
    after_each("redirects: relative, scheme-relative, a loop, https only");
}

static void pause_and_resume(int conns)
{
    const int64_t len = 12 * 1024 * 1024 + 999;
    uint8_t *b = body_of(len, 6);
    const fake_route_t r[] = { { .url = "https://h/pr", .body = b, .len = len, .etag = "\"p\"", .ranges = true } };
    fake_routes(r, 1);
    char p[300], part[310], id[320];
    path_in(p, sizeof(p), conns > 1 ? "pr3.bin" : "pr1.bin");
    snprintf(part, sizeof(part), "%s.part", p);
    snprintf(id, sizeof(id), "%s.part.id", p);
    fake_read_delay(40);
    CHECK(start("https://h/pr", p, conns));
    wait_got(4 * 1024 * 1024);
    feckless_fetch_pause();
    CHECK(wait_done() == FECKLESS_FETCH_PAUSED);
    CHECK(exists(part) && exists(id) && !exists(p));
    int64_t had = 0;
    feckless_fetch_state(NULL, &had, NULL, NULL, 0, NULL);
    fake_read_delay(0);
    fake_stats_reset();
    CHECK(start("https://h/pr", p, conns));         /* the same part file: a resume */
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b, len));
    CHECK(fake_first_range_from() > 0);             /* it asked for the rest */
    CHECK(fake_bytes_served() < len);               /* and not for all of it again */
    printf("  paused at %lld, then served %lld of %lld\n", (long long)had,
           (long long)fake_bytes_served(), (long long)len);
    CHECK(!exists(part) && !exists(id));
    free(b);
    after_each(conns > 1 ? "pause and resume, in stripes" : "pause and resume, one stream");
}

static void resume_changed(void)
{
    /* Paused, and the server's file changes: If-Range gets a 200, and it
     * starts over, rather than joining two files. */
    const int64_t len = 10 * 1024 * 1024;
    uint8_t *b1 = body_of(len, 7), *b2 = body_of(len + 50, 8);
    fake_route_t r[] = { { .url = "https://h/ch", .body = b1, .len = len, .etag = "\"one\"", .ranges = true } };
    fake_routes(r, 1);
    char p[300];
    path_in(p, sizeof(p), "changed.bin");
    fake_read_delay(40);
    CHECK(start("https://h/ch", p, 3));
    wait_got(3 * 1024 * 1024);
    feckless_fetch_pause();
    CHECK(wait_done() == FECKLESS_FETCH_PAUSED);
    fake_read_delay(0);
    r[0].body = b2; r[0].len = len + 50; r[0].etag = "\"two\"";
    CHECK(start("https://h/ch", p, 3));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(p, b2, len + 50));
    free(b1); free(b2);
    after_each("resume of a file that changed: starts over");
}

static void broken_read(void)
{
    const int64_t len = 16 * 1024 * 1024;
    uint8_t *b = body_of(len, 9);
    const fake_route_t r[] = { { .url = "https://h/br", .body = b, .len = len, .etag = "\"r\"", .ranges = true } };
    fake_routes(r, 1);
    char p[300];
    for (int conns = 1; conns <= 3; conns += 2) {
        path_in(p, sizeof(p), conns > 1 ? "broken3.bin" : "broken1.bin");
        fake_break_at(5 * 1024 * 1024 + 3);
        CHECK(start("https://h/br", p, conns));
        CHECK(wait_done() == FECKLESS_FETCH_DONE);
        CHECK(same_as(p, b, len));
    }
    free(b);
    after_each("a read that fails mid-way: tried again, whole");
}

static void failures(void)
{
    const int64_t len = 4000;
    uint8_t *b = body_of(len, 10);
    const fake_route_t r[] = {
        { .url = "https://h/ok", .body = b, .len = len },
        { .url = "https://h/gone", .status = 404 },
    };
    fake_routes(r, 2);
    char p[300], part[310];

    path_in(p, sizeof(p), "gone.bin");
    CHECK(start("https://h/gone", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_FAILED);
    CHECK(why_is("the server said 404"));

    path_in(p, sizeof(p), "noconn.bin");
    fake_fail_connects(1000);
    CHECK(start("https://h/ok", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_FAILED);
    CHECK(why_is("could not connect"));
    fake_fail_connects(0);

    /* VERIFY refuses: failed, nothing at the path, the part file gone. */
    path_in(p, sizeof(p), "refused.bin");
    snprintf(part, sizeof(part), "%s.part", p);
    ev_reset();
    s_ev.refuse = true;
    CHECK(start("https://h/ok", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_FAILED);
    CHECK(why_is("the file is not what was expected"));
    CHECK(!exists(p) && !exists(part));
    ev_reset();

    /* Refused while one runs, and for bad options. */
    fake_read_delay(2000);
    path_in(p, sizeof(p), "busy.bin");
    CHECK(start("https://h/ok", p, 1));
    CHECK(!start("https://h/ok", p, 1));
    CHECK(why_is("one is running"));
    fake_read_delay(0);
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(!start(NULL, p, 1));
    free(b);
    after_each("404, no connection, refused by VERIFY, refused to start");
}

static void cancel(void)
{
    const int64_t len = 9 * 1024 * 1024;
    uint8_t *b = body_of(len, 11);
    const fake_route_t r[] = { { .url = "https://h/c", .body = b, .len = len, .etag = "\"c\"", .ranges = true } };
    fake_routes(r, 1);
    char p[300], part[310], id[320];
    path_in(p, sizeof(p), "cancel.bin");
    snprintf(part, sizeof(part), "%s.part", p);
    snprintf(id, sizeof(id), "%s.part.id", p);

    fake_read_delay(40);
    ev_reset();
    CHECK(start("https://h/c", p, 3));
    wait_got(2 * 1024 * 1024);
    feckless_fetch_cancel();
    CHECK(wait_done() == FECKLESS_FETCH_IDLE);
    CHECK(!exists(part) && !exists(id) && !exists(p));
    CHECK(s_ev.n > 0 && s_ev.kinds[s_ev.n - 1] == FECKLESS_FETCH_EV_CANCELLED);

    /* And a cancel of a paused one, on this task. */
    ev_reset();
    CHECK(start("https://h/c", p, 1));
    wait_got(1024 * 1024);
    feckless_fetch_pause();
    CHECK(wait_done() == FECKLESS_FETCH_PAUSED);
    CHECK(exists(part));
    int64_t size = 0;
    CHECK(feckless_fetch_part(part, &size) && size > 0);
    feckless_fetch_cancel();
    CHECK(feckless_fetch_state(NULL, NULL, NULL, NULL, 0, NULL) == FECKLESS_FETCH_IDLE);
    CHECK(!exists(part) && !exists(id));
    CHECK(s_ev.kinds[s_ev.n - 1] == FECKLESS_FETCH_EV_CANCELLED);
    fake_read_delay(0);

    /* And a discard of one left from before, with nothing running. */
    FILE *f = fopen(part, "w"); fputs("x", f); fclose(f);
    f = fopen(id, "w"); fputs("1\n\n", f); fclose(f);
    feckless_fetch_discard(part);
    CHECK(!exists(part) && !exists(id));
    free(b);
    after_each("cancel while running, of a paused one, and a discard");
}

static void rename_at_headers(void)
{
    const int64_t len = 2048;
    uint8_t *b = body_of(len, 12);
    const fake_route_t r[] = { { .url = "https://h/ep", .body = b, .len = len, .ctype = "audio/mpeg" } };
    fake_routes(r, 1);
    char p[300], named[310];
    path_in(p, sizeof(p), "episode");
    snprintf(named, sizeof(named), "%s.mp3", p);
    ev_reset();
    s_ev.rename_ext = "mp3";
    CHECK(start("https://h/ep", p, 1));
    CHECK(wait_done() == FECKLESS_FETCH_DONE);
    CHECK(same_as(named, b, len) && !exists(p));
    char sp[310];
    feckless_fetch_state(NULL, NULL, NULL, sp, sizeof(sp), NULL);
    CHECK(!strcmp(sp, named));
    CHECK(feckless_fetch_live(named, NULL, 0, NULL, NULL, NULL));
    ev_reset();
    free(b);
    after_each("the name chosen at HEADERS");
}

int main(void)
{
    snprintf(s_dir, sizeof(s_dir), "%s/fetchtest.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(s_dir)) { perror("mkdtemp"); return 1; }
    srand(1);

    small_one_stream();
    striped();
    no_ranges();
    no_length();
    redirects();
    pause_and_resume(1);
    pause_and_resume(3);
    resume_changed();
    broken_read();
    failures();
    cancel();
    rename_at_headers();

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", s_dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", s_dir);
    if (s_fail) { fprintf(stderr, "fetchtest: %d failed\n", s_fail); return 1; }
    printf("fetchtest: all passed\n");
    return 0;
}
