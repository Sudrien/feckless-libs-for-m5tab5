/* SPDX-License-Identifier: MIT
 * Host test for feckless_libversions: libv_draw()'s rows, columns, cut
 * widths, paging and "and N more", against recorded gfx_draw_text calls. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "feckless_libversions.h"
#include "gfx.h"

typedef struct { int x, y, scale, max_w; uint16_t c; char s[64]; } call_t;
static call_t calls[256];
static int ncalls, fails;

void gfx_draw_text(int x, int y, const char *s, int scale, int max_w, uint16_t c)
{
    call_t *k = &calls[ncalls++];
    k->x = x; k->y = y; k->scale = scale; k->max_w = max_w; k->c = c;
    snprintf(k->s, sizeof k->s, "%s", s);
}

#define CHECK(c) do { if (!(c)) { fails++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const libv_t L[] = {
    { "Spurious", "abc1234", "MIT" }, { "ESP-IDF", "6.1.0", "Apache-2.0" },
    { "feckless_keyboard", "cd4b91b", "MIT" }, { "tinfl", "3.0.2 (vendored)", "Unlicense" },
    { "Ark Pixel font (TakWolf)", "0f1e2d3", "OFL-1.1" }, { NULL, NULL, NULL },
};
#define N (int)(sizeof L / sizeof L[0])

int main(void)
{
    libv_view_t v = { .x = 40, .y = 100, .w = 640, .h = 1000, .scale = 2, .text = 1, .dim = 2 };
    const int rh = GFX_GLYPH_H(2) + 10;

    /* everything fits: three calls a row, no "more" */
    ncalls = 0;
    CHECK(libv_draw(&v, L, N, 0) == N);
    CHECK(ncalls == 3 * N);
    for (int i = 0; i < N; i++) {
        call_t *a = &calls[3 * i], *b = a + 1, *c = a + 2;
        CHECK(a->y == 100 + i * rh && b->y == a->y && c->y == a->y);
        CHECK(a->x == 40 && b->x == 40 + 288 && c->x == 40 + 448);
        CHECK(a->max_w == 288 - 12 && b->max_w == 160 - 12 && c->max_w == 640 - 448);
        CHECK(a->c == 1 && b->c == 2 && c->c == 2 && a->scale == 2);
        CHECK(!strcmp(a->s, L[i].module ? L[i].module : ""));
    }

    /* three rows of room: two entries and "and 4 more" */
    v.h = 3 * rh + rh - 1;
    CHECK(libv_rows(&v) == 3);
    ncalls = 0;
    CHECK(libv_draw(&v, L, N, 0) == 2);
    CHECK(ncalls == 7 && !strcmp(calls[6].s, "and 4 more") && calls[6].y == 100 + 2 * rh);
    CHECK(calls[6].c == 2 && calls[6].max_w == 640);

    /* paging: 2, then 2, then the last 2 fit exactly */
    int first = 0, pages = 0, seen = 0;
    while (first < N) { int k = libv_draw(&v, L, N, first); CHECK(k > 0); first += k; seen += k; pages++; }
    CHECK(seen == N && pages == 3);

    /* explicit row_h, one row of room, out of range, nothing at all */
    v.row_h = 50; v.h = 50; ncalls = 0;
    CHECK(libv_draw(&v, L, N, 0) == 0 && ncalls == 1 && !strcmp(calls[0].s, "and 6 more"));
    v.h = 49; ncalls = 0;
    CHECK(libv_rows(&v) == 0 && libv_draw(&v, L, N, 0) == 0 && ncalls == 0);
    v.h = 500;
    CHECK(libv_draw(&v, L, N, N) == 0 && libv_draw(&v, L, N, -1) == 0 && libv_draw(&v, NULL, N, 0) == 0);
    /* a big count */
    static libv_t many[12345];
    for (int i = 0; i < 12345; i++) many[i] = L[0];
    ncalls = 0; v.h = 100;
    CHECK(libv_draw(&v, many, 12345, 0) == 1 && !strcmp(calls[3].s, "and 12344 more"));

    printf("libvtest: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
