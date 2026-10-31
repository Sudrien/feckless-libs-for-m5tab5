/* fittest.c -- image_fit.c: sizes, blocks, strides, and no write outside cw x ch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feckless_image_fit.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void size(int iw, int ih, int bw, int bh, int ecw, int ech, int ek)
{
    int cw, ch, k;
    img_fit_size(iw, ih, bw, bh, &cw, &ch, &k);
    CHECK(cw == ecw && ch == ech && k == ek, "%dx%d in %dx%d: %dx%d k%d (want %dx%d k%d)",
          iw, ih, bw, bh, cw, ch, k, ecw, ech, ek);
}

/* a picture whose pixel is x + 1000*y, with a padded stride */
static img_pic_t make(int w, int h, int stride)
{
    img_pic_t p; memset(&p, 0, sizeof p);
    p.px = malloc((size_t)stride * h * 2);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < stride; x++) p.px[(size_t)y * stride + x] = x < w ? (uint16_t)(x + 100 * y) : 0xDEAD;
    p.w = w; p.h = h; p.stride = stride;
    return p;
}

int main(void)
{
    printf("fittest\n");
    size(1920, 1920, 720, 720, 720, 720, 0);
    size(1920, 1080, 720, 720, 720, 405, 0);
    size(1080, 1920, 720, 720, 405, 720, 0);
    size(370, 370, 720, 720, 720, 720, 0);            /* k would be 1: fractional, enlarged to fit */
    size(64, 64, 720, 720, 704, 704, 11);             /* small: whole pixels */
    size(200, 200, 720, 720, 600, 600, 3);
    size(300, 300, 720, 720, 720, 720, 0);            /* over IMG_INT_SCALE_MAX: a cover, fractional */
    size(0, 5, 720, 720, 1, 1, 0);
    size(5, 5, 0, 0, 1, 1, 0);
    size(10000, 1, 720, 720, 720, 1, 0);
    size(1, 10000, 720, 720, 1, 720, 0);

    /* Identity through a padded stride: rows copied, padding never read. */
    {
        img_pic_t p = make(20, 10, 32);
        uint16_t *d = malloc(21 * 11 * 2); memset(d, 0xAA, 21 * 11 * 2);
        img_fit_draw(&p, d, 21, 20, 10, 0);
        int bad = 0;
        for (int y = 0; y < 10; y++) for (int x = 0; x < 20; x++) if (d[y * 21 + x] != (uint16_t)(x + 100 * y)) bad++;
        CHECK(!bad, "identity: %d wrong", bad);
        CHECK(d[20] == 0xAAAA && d[10 * 21] == 0xAAAA, "nothing written outside cw x ch");
        free(d); free(p.px);
    }
    /* A whole-number scale is exact blocks: 3x of 5 wide is 3,3,3 each. */
    {
        img_pic_t p = make(5, 4, 16);
        uint16_t *d = calloc(15 * 12, 2);
        img_fit_draw(&p, d, 15, 15, 12, 3);
        int bad = 0;
        for (int y = 0; y < 12; y++) for (int x = 0; x < 15; x++) if (d[y * 15 + x] != (uint16_t)((x / 3) + 100 * (y / 3))) bad++;
        CHECK(!bad, "3x blocks: %d wrong", bad);
        free(d); free(p.px);
    }
    /* Down and up: every output reads inside the source, ends included. */
    {
        int dims[][4] = { {1920, 1080, 720, 405}, {7, 3, 31, 13}, {100, 100, 7, 7}, {3, 3, 720, 720}, {1, 1, 9, 9} };
        for (unsigned i = 0; i < sizeof dims / sizeof dims[0]; i++) {
            const int iw = dims[i][0], ih = dims[i][1], cw = dims[i][2], ch = dims[i][3];
            img_pic_t p = make(iw, ih, iw + 8);
            uint16_t *d = malloc((size_t)cw * ch * 2);
            img_fit_draw(&p, d, (size_t)cw, cw, ch, 0);
            int bad = 0;
            for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
                const uint16_t v = d[(size_t)y * cw + x];
                const int sx = v % 100, sy = v / 100;
                if (iw <= 100 ? (sx >= iw || sy >= ih) : 0) bad++;
                if (iw <= 100 && v == 0xDEAD) bad++;   /* big ones wrap 16 bits and could be 0xDEAD */
            }
            CHECK(!bad, "%dx%d -> %dx%d: %d reads outside the picture", iw, ih, cw, ch, bad);
            CHECK(d[0] == 0, "%dx%d -> %dx%d: top-left is the source's", iw, ih, cw, ch);
            free(d); free(p.px);
        }
    }
    /* Nothing to draw. */
    { img_pic_t p; memset(&p, 0, sizeof p); uint16_t d = 7; img_fit_draw(&p, &d, 1, 1, 1, 0); img_fit_draw(NULL, &d, 1, 1, 1, 0); CHECK(d == 7, "no pixels, no write"); }

    return failures ? 1 : (printf("%d checks, %d failures\n", checks, failures), 0);
}
