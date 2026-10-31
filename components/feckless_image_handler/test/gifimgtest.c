/*
 * gifimgtest.c -- GIF through stb_image (Defeatist's 6145), the first
 * frame; moved from its arttest.c by 7005.
 *
 * Fixtures in fixtures/art, from Defeatist's tools/gen_art_fixtures.py:
 * the same four-quadrant 64x48 picture as a GIF87a, interlaced, with a
 * transparent index (the white quadrant), and animated (the second frame
 * is black).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feckless_image.h"
#include "feckless_image_stb.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++;      \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__);   \
    printf("\n"); } } while (0)

#define W 64
#define H 48

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

static void gif_case(const char *name, int hole)
{
    static const int want[4][3] = {
        { 220, 30, 30 }, { 30, 200, 40 }, { 40, 50, 210 }, { 255, 255, 255 },
    };
    static const int at[4][2] = { { 16, 12 }, { 48, 12 }, { 16, 36 }, { 48, 36 } };
    size_t len;
    uint8_t *b = slurp(name, &len);
    CHECK(img_sniff(b, len) == IMG_GIF, "%s: not sniffed as GIF", name);
    int w = 0, h = 0;
    CHECK(img_stb_info(b, len, &w, &h) == IMG_STB_OK && w == W && h == H,
          "%s: info %dx%d", name, w, h);
    uint16_t *px = NULL;
    size_t size = 0;
    int ow = 0, oh = 0;
    const char *why = NULL;
    const img_stb_err_t e = img_stb_decode_rgb565(b, len, 0, 0, &px, &size, &ow, &oh, &why);
    CHECK(e == IMG_STB_OK && ow == W && oh == H, "%s: stb_image %s %dx%d (%s)",
          name, img_stb_err_name(e), ow, oh, why ? why : "-");
    if (px) {
        CHECK(size == (size_t)W * H * 2, "%s: size %zu", name, size);
        /* Palette colours are exact; a transparent one comes out black,
         * there being no alpha to carry it -- so not checked. */
        for (int q = 0; q < (hole ? 3 : 4); q++) {
            const uint16_t got = px[at[q][1] * W + at[q][0]];
            CHECK(chan_diff(got, rgb565(want[q][0], want[q][1], want[q][2])) <= 8,
                  "%s: quadrant %d is %04x", name, q, got);
        }
    }
    free(px);
    free(b);
}

int main(void)
{
    printf("gifimgtest\n");
    CHECK(img_sniff((const uint8_t *)"GIF89a........", 14) == IMG_GIF, "GIF89a");
    CHECK(img_sniff((const uint8_t *)"GIF87a........", 14) == IMG_GIF, "GIF87a");
    CHECK(img_sniff((const uint8_t *)"GIF88a........", 14) == IMG_NONE, "no such GIF");

    gif_case("gif.gif", 0);
    gif_case("gif_interlaced.gif", 0);
    gif_case("gif_trns.gif", 1);
    gif_case("gif_animated.gif", 0);    /* the second frame is black */
    CHECK(img_stb_gif_peak_bytes(600, 600) >= 600u * 600u * 13u, "GIF peak estimate");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
