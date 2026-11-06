/*
 * fetchfake.h -- the controls of fetchtest's fakes (9003): the in-memory
 * server behind fake esp_http_client.h, what to break, and what the
 * fakes saw.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One address the server answers. status 0 serves `body`; a 3xx sends
 * `location`; anything else is that status with no body. */
typedef struct {
    const char    *url;
    int            status;
    const char    *location;
    const uint8_t *body;
    int64_t        len;
    const char    *etag;        /* NULL: none */
    const char    *ctype;       /* NULL: none */
    bool           ranges;      /* honours Range, says Accept-Ranges: bytes */
    bool           no_length;   /* no Content-Length (chunked) */
} fake_route_t;

/* The server's addresses, replaced whole; the array must outlive use. */
void fake_routes(const fake_route_t *r, int n);

/* What to break: the next `n` connects fail; the first read that would
 * pass byte `at` of a body fails once (-1: none); each read sleeps `us`. */
void fake_fail_connects(int n);
void fake_break_at(int64_t at);
void fake_read_delay(int us);

/* What the fakes saw since fake_stats_reset(). */
void    fake_stats_reset(void);
int     fake_requests(void);            /* opens that reached the server */
int     fake_max_concurrent(void);      /* the most connections open at once */
int64_t fake_first_range_from(void);    /* the first request's Range start, -1 if none */
int64_t fake_bytes_served(void);
int     fake_lease_violations(void);    /* lease held twice, or across a network read */
int     fake_lease_holders(void);       /* tasks holding it now: 0 when idle */
