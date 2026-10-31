/*
 * pngimgtest.c -- PNG through pngle to an RGB565 buffer
 * (src/image_png.c, 7006). The cases are Defeatist's arttest PNG ones,
 * moved: RGB, RGBA, palette, palette with tRNS, grey, grey with alpha,
 * 16 bits a channel, Adam7; plus the thinning, the blend over a
 * background, the refusals, and every file cut short.
 *
 * Fixtures in fixtures/art: Defeatist's (the 64x48 four-quadrant picture,
 * red/green over blue/white, from its tools/gen_art_fixtures.py) and two
 * from gen_png_fixtures.py.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feckless_image.h"
#include "feckless_image_png.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++;      \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__);   \
    printf("\n"); } } while (0)

#define W 64
#define H 48
#define BG 0x0841       /* the colour behind a transparent pixel */

static uint8_t *slurp(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "fixtures/art/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  missing %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len ? *len : 1);
    if (fread(b, 1, *len, f) != *len) exit(2);
    fclose(f);
    return b;
}

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static int chan_diff(uint16_t a, uint16_t b)
{
    const int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31)) * 8;
    const int dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) * 4;
    const int db = abs((a & 31) - (b & 31)) * 8;
    return dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
}

typedef struct { uint16_t *px; size_t size; int w, h; img_png_err_t err; const char *why; } pic_t;

static pic_t decode(const uint8_t *b, size_t len, int bw, int bh)
{
    pic_t p = {0};
    p.err = img_png_decode_rgb565(b, len, bw, bh, BG, &p.px, &p.size, &p.w, &p.h, &p.why);
    return p;
}

/* The middle of each quadrant against the colours drawn. `grey`: the
 * fixture is greyscale, so compare luma. `hole`: the white quadrant is
 * transparent and must come out as the background. */
static void quadrants(const char *what, const pic_t *p, int tol, int grey, int hole)
{
    static const int want[4][3] = {
        { 220, 30, 30 }, { 30, 200, 40 }, { 40, 50, 210 }, { 255, 255, 255 },
    };
    for (int q = 0; q < 4; q++) {
        const int x = (q & 1) ? 3 * p->w / 4 : p->w / 4, y = (q & 2) ? 3 * p->h / 4 : p->h / 4;
        const uint16_t got = p->px[y * p->w + x];
        if (q == 3 && hole) {
            CHECK(got == BG, "%s: transparent quadrant is %04x, want the background", what, got);
            continue;
        }
        int r = want[q][0], g = want[q][1], b = want[q][2];
        if (grey) { const int l = (r * 299 + g * 587 + b * 114) / 1000; r = g = b = l; }
        const int d = chan_diff(got, rgb565(r, g, b));
        CHECK(d <= tol, "%s: quadrant %d is %04x, want ~%04x (off by %d)",
              what, q, got, rgb565(r, g, b), d);
    }
}

static void png_case(const char *name, int grey, int hole)
{
    size_t len;
    uint8_t *b = slurp(name, &len);
    CHECK(img_sniff(b, len) == IMG_PNG, "%s: not sniffed as PNG", name);
    int iw = 0, ih = 0;
    CHECK(img_png_info(b, len, &iw, &ih) == IMG_PNG_OK && iw == W && ih == H,
          "%s: info %dx%d", name, iw, ih);
    pic_t p = decode(b, len, 0, 0);
    CHECK(p.err == IMG_PNG_OK && p.w == W && p.h == H && p.size == (size_t)W * H * 2,
          "%s: %s %dx%d (%s)", name, img_png_err_name(p.err), p.w, p.h, p.why ? p.why : "-");
    if (p.px) quadrants(name, &p, 8, grey, hole);
    free(p.px);

    /* Cut short at every length up to the whole file: no crash, and an
     * honest answer -- an error, or a picture decoded only in part. */
    for (size_t cut = 0; cut < len; cut++) {
        pic_t t = decode(b, cut, 0, 0);
        if (t.err == IMG_PNG_OK) {
            CHECK(t.w == W && t.h == H && t.px && t.size == (size_t)W * H * 2,
                  "%s cut at %zu: %dx%d", name, cut, t.w, t.h);
        } else {
            CHECK(!t.px, "%s cut at %zu: a refusal left pixels", name, cut);
        }
        free(t.px);
    }
    free(b);
}

int main(void)
{
    printf("pngimgtest\n");

    png_case("rgb.png", 0, 0);
    png_case("rgba.png", 0, 1);
    png_case("palette.png", 0, 0);
    png_case("palette_trns.png", 0, 1);
    png_case("gray.png", 1, 0);
    png_case("gray_alpha.png", 1, 1);
    png_case("rgb16.png", 0, 0);
    png_case("interlaced.png", 0, 0);     /* Adam7: later passes overwrite the coarse ones */

    /* Blend: an opaque red pixel, and red at alpha 128 over BG. */
    {
        size_t len;
        uint8_t *b = slurp("blend.png", &len);
        pic_t p = decode(b, len, 0, 0);
        CHECK(p.err == IMG_PNG_OK && p.w == 2 && p.h == 1, "blend: %s %dx%d", img_png_err_name(p.err), p.w, p.h);
        if (p.px) {
            CHECK(chan_diff(p.px[0], rgb565(255, 0, 0)) <= 8, "opaque is %04x", p.px[0]);
            /* BG 0x0841 is (8, 8, 8)-ish; half of 255 plus half of that. */
            const uint16_t want = rgb565((255 * 128 + 8 * 127) / 255, (0 * 128 + 8 * 127) / 255,
                                         (0 * 128 + 8 * 127) / 255);
            CHECK(chan_diff(p.px[1], want) <= 8, "half-transparent is %04x, want ~%04x", p.px[1], want);
        }
        free(p.px);
        /* A different background shows through the same way. */
        uint16_t *px = NULL; size_t sz; int w, h;
        CHECK(img_png_decode_rgb565(b, len, 0, 0, 0xFFFF, &px, &sz, &w, &h, NULL) == IMG_PNG_OK, "white bg");
        if (px) CHECK(((px[1] >> 5) & 63) > 24, "red over white keeps green up: %04x", px[1]);
        free(px);
        free(b);
    }

    /* Thinned like the other two: the largest whole step that covers. */
    {
        size_t len;
        uint8_t *b = slurp("big1500x1000.png", &len);
        pic_t p = decode(b, len, 720, 400);
        CHECK(p.err == IMG_PNG_OK && p.w == 750 && p.h == 500,
              "1500x1000 into 720x400: %dx%d, want 750x500 (step 2)", p.w, p.h);
        if (p.px) quadrants("thinned", &p, 8, 0, 0);
        free(p.px);
        pic_t p1 = decode(b, len, 720, 720);
        CHECK(p1.err == IMG_PNG_OK && p1.w == 1500 && p1.h == 1000,
              "a box it cannot halve: %dx%d", p1.w, p1.h);
        free(p1.px);
        pic_t p3 = decode(b, len, 300, 200);
        CHECK(p3.err == IMG_PNG_OK && p3.w == 300 && p3.h == 200, "into 300x200: %dx%d, want 300x200 (step 5)", p3.w, p3.h);
        if (p3.px) quadrants("step 5", &p3, 8, 0, 0);
        free(p3.px);
        CHECK(img_png_peak_bytes(1000, 1000) >= 2u * 1000u * 1000u, "peak estimate");
        free(b);
    }
    {   /* The thinned Adam7 file: later passes still win. */
        size_t len;
        uint8_t *b = slurp("interlaced.png", &len);
        pic_t p = decode(b, len, 20, 15);          /* 64/3 = 21 >= 20, 48/3 = 16 >= 15 */
        CHECK(p.err == IMG_PNG_OK && p.w == 21 && p.h == 16, "interlaced step 3: %dx%d", p.w, p.h);
        if (p.px) {
            static const int want[4][3] = { { 220, 30, 30 }, { 30, 200, 40 }, { 40, 50, 210 }, { 255, 255, 255 } };
            for (int q = 0; q < 4; q++) {
                const int x = (q & 1) ? 3 * p.w / 4 : p.w / 4, y = (q & 2) ? 3 * p.h / 4 : p.h / 4;
                CHECK(chan_diff(p.px[y * p.w + x], rgb565(want[q][0], want[q][1], want[q][2])) <= 8,
                      "interlaced thinned: quadrant %d is %04x", q, p.px[y * p.w + x]);
            }
        }
        free(p.px);
        free(b);
    }

    /* Refusals, without crashing. */
    {
        size_t len;
        uint8_t *b = slurp("rgb.png", &len);
        pic_t j = decode((const uint8_t *)"not a png at all, no", 20, 0, 0);
        CHECK(j.err == IMG_PNG_NOT_PNG && !j.px, "junk: %s", img_png_err_name(j.err));
        pic_t s = decode((const uint8_t *)"\x89PNG\r\n\x1a\nnot a chunk at all", 26, 0, 0);
        CHECK(s.err == IMG_PNG_NOT_PNG && !s.px, "junk after the signature: %s", img_png_err_name(s.err));
        pic_t z = decode(b, 0, 0, 0);
        CHECK(z.err == IMG_PNG_NOT_PNG, "empty: %s", img_png_err_name(z.err));
        CHECK(img_png_decode_rgb565(NULL, 100, 0, 0, BG, &z.px, &z.size, &z.w, &z.h, NULL) == IMG_PNG_NOT_PNG, "NULL");

        /* 5000x5000 in the IHDR: over IMG_PNG_MAX_PIXELS, refused before
         * pngle or an allocation. */
        b[16] = 0; b[17] = 0; b[18] = 0x13; b[19] = 0x88;
        b[20] = 0; b[21] = 0; b[22] = 0x13; b[23] = 0x88;
        int w = 0, h = 0;
        CHECK(img_png_info(b, len, &w, &h) == IMG_PNG_TOO_BIG && w == 5000 && h == 5000, "5000x5000 header: %dx%d", w, h);
        pic_t tb = decode(b, len, 0, 0);
        CHECK(tb.err == IMG_PNG_TOO_BIG && !tb.px, "5000x5000 decode: %s", img_png_err_name(tb.err));
        /* 0 x 0 */
        memset(b + 16, 0, 8);
        CHECK(img_png_info(b, len, &w, &h) == IMG_PNG_NOT_PNG, "0x0 header");
        free(b);
    }

    /* Damaged files: 2000 copies of each of four fixtures with up to 8
     * bytes changed. Any answer is fine except a crash; whatever is
     * returned is well formed. The input is the internet's. */
    {
        const char *names[] = { "rgb.png", "interlaced.png", "palette_trns.png", "rgb16.png" };
        unsigned seed = 7006;
        int decoded = 0;
        for (int f = 0; f < 4; f++) {
            size_t len; uint8_t *orig = slurp(names[f], &len);
            uint8_t *b = malloc(len);
            for (int n = 0; n < 2000; n++) {
                memcpy(b, orig, len);
                const int flips = 1 + (int)((seed = seed * 1103515245u + 12345u) >> 16) % 8;
                for (int k = 0; k < flips; k++) {
                    seed = seed * 1103515245u + 12345u;
                    const size_t at = (seed >> 8) % len;
                    seed = seed * 1103515245u + 12345u;
                    b[at] = (uint8_t)(seed >> 16);
                }
                pic_t d = decode(b, len, 0, 0);
                if (d.err == IMG_PNG_OK) {
                    decoded++;
                    CHECK(d.px && d.w > 0 && d.h > 0 && d.size == (size_t)d.w * d.h * 2,
                          "a damaged decode is malformed");
                } else {
                    CHECK(!d.px, "a refusal left pixels");
                }
                free(d.px);
            }
            free(b); free(orig);
        }
        printf("  8000 damaged files, %d still decoded, none crashed\n", decoded);
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
