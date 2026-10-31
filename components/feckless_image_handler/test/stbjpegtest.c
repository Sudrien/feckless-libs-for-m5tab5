/*
 * stbjpegtest.c -- 5062's last JPEG decoder (moved from Defeatist by
 * 7005), on the flavours the other
 * two refuse.
 *
 * Fixtures in fixtures/stbjpeg, made with PIL from one synthetic 145x145
 * picture (the BBC logo's size): baseline 4:2:0, the same progressive,
 * the baseline file with its SOF0 marker rewritten to SOF1 (extended
 * sequential decodes identically at 8 bits), a progressive greyscale,
 * and a 1500x1000 progressive for the thinning.
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

static uint8_t *slurp(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "fixtures/stbjpeg/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  missing %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) exit(2);
    fclose(f);
    return b;
}

typedef struct { uint16_t *px; size_t size; int w, h; img_stb_err_t err; } pic_t;

static pic_t decode(const uint8_t *b, size_t len, int bw, int bh)
{
    pic_t p = {0};
    const char *why = NULL;
    p.err = img_stb_decode_rgb565(b, len, bw, bh, &p.px, &p.size, &p.w, &p.h, &why);
    return p;
}

static pic_t decode_file(const char *name, int bw, int bh)
{
    size_t len;
    uint8_t *b = slurp(name, &len);
    pic_t p = decode(b, len, bw, bh);
    free(b);
    return p;
}

static int chan_diff(uint16_t a, uint16_t b)
{
    const int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31)) * 8;
    const int dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) * 4;
    const int db = abs((a & 31) - (b & 31)) * 8;
    return dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
}

/* ---- Defeatist's arttest JPEGs (moved by 7006): a 64x48 four-quadrant
 * picture (red, green / blue, white) as baseline 4:2:0 and 4:4:4,
 * progressive, greyscale, and with EXIF ahead of the SOF. The middle of
 * each quadrant must be the colour drawn, the SOF walk must name the
 * marker, and every prefix of the file must be refused or partial. */
static uint16_t q565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void jpeg_case(const char *name, uint8_t sof_want, int grey)
{
    static const int want[4][3] = {
        { 220, 30, 30 }, { 30, 200, 40 }, { 40, 50, 210 }, { 255, 255, 255 },
    };
    char path[64];
    snprintf(path, sizeof path, "../art/%s", name);   /* fixtures/art, beside stbjpeg */
    size_t len;
    uint8_t *b = slurp(path, &len);
    CHECK(img_sniff(b, len) == IMG_JPEG, "%s: not sniffed as JPEG", name);

    uint8_t sof = 0;
    uint32_t w = 0, h = 0;
    const bool base = img_jpeg_sof(b, len, &sof, &w, &h);
    CHECK(sof == sof_want && w == 64 && h == 48, "%s: SOF %02x %ux%u, want %02x 64x48",
          name, sof, (unsigned)w, (unsigned)h, sof_want);
    CHECK(base == (sof_want == 0xC0 || sof_want == 0xC1), "%s: baseline says %d", name, base);

    pic_t p = decode(b, len, 0, 0);
    CHECK(p.err == IMG_STB_OK && p.w == 64 && p.h == 48, "%s: %s %dx%d", name, img_stb_err_name(p.err), p.w, p.h);
    if (p.px) {
        for (int q = 0; q < 4; q++) {
            const int x = (q & 1) ? 48 : 16, y = (q & 2) ? 36 : 12;
            int r = want[q][0], g = want[q][1], bl = want[q][2];
            if (grey) { const int l = (r * 299 + g * 587 + bl * 114) / 1000; r = g = bl = l; }
            const int d = chan_diff(p.px[y * 64 + x], q565(r, g, bl));
            CHECK(d <= 24, "%s: quadrant %d is %04x (off by %d)", name, q, p.px[y * 64 + x], d);
        }
    }
    free(p.px);

    for (size_t cut = 0; cut < len; cut += (cut < 64 ? 1 : 7)) {
        pic_t t = decode(b, cut, 0, 0);
        CHECK(t.err == IMG_STB_OK ? (t.px != NULL) : (t.px == NULL), "%s cut at %zu", name, cut);
        free(t.px);
        img_jpeg_sof(b, cut, &sof, &w, &h);
    }
    free(b);
}

int main(void)
{
    printf("stbjpegtest\n");

    jpeg_case("base420.jpg", 0xC0, 0);
    jpeg_case("base444.jpg", 0xC0, 0);
    jpeg_case("prog.jpg", 0xC2, 0);
    jpeg_case("gray.jpg", 0xC0, 1);
    jpeg_case("exif.jpg", 0xC0, 0);

    pic_t base = decode_file("base145.jpg", 0, 0);
    CHECK(base.err == IMG_STB_OK && base.w == 145 && base.h == 145,
          "baseline: %s %dx%d", img_stb_err_name(base.err), base.w, base.h);
    CHECK(base.size == 145u * 145u * 2u, "size %zu", base.size);

    /* The white square at 20..60 and the red disc centred at 105. */
    if (base.px) {
        const uint16_t white = base.px[40 * 145 + 40];
        CHECK(chan_diff(white, 0xFFFF) <= 8, "white is %04x", white);
        const uint16_t red = base.px[105 * 145 + 105];
        CHECK(((red >> 11) & 31) >= 23 && ((red >> 5) & 63) < 8,
              "red is %04x (RGB565 channel order)", red);
    }

    /* Progressive: the same picture as the baseline, to within what two
     * lossy encodes of it differ by. */
    pic_t prog = decode_file("prog145.jpg", 0, 0);
    CHECK(prog.err == IMG_STB_OK && prog.w == 145 && prog.h == 145,
          "progressive: %s %dx%d", img_stb_err_name(prog.err), prog.w, prog.h);
    if (prog.px && base.px) {
        long sum = 0; int worst = 0;
        for (int i = 0; i < 145 * 145; i++) {
            const int d = chan_diff(prog.px[i], base.px[i]);
            sum += d; if (d > worst) worst = d;
        }
        CHECK(sum / (145 * 145) <= 4, "progressive differs from baseline by %ld mean", sum / (145 * 145));
    }

    /* SOF1: TJpgDec's JDR_FMT3. Same entropy data, so identical pixels. */
    pic_t sof1 = decode_file("sof1_145.jpg", 0, 0);
    CHECK(sof1.err == IMG_STB_OK && sof1.w == 145, "SOF1: %s", img_stb_err_name(sof1.err));
    if (sof1.px && base.px) {
        CHECK(memcmp(sof1.px, base.px, base.size) == 0, "SOF1 decodes differently from SOF0");
    }

    pic_t gray = decode_file("prog145_gray.jpg", 0, 0);
    CHECK(gray.err == IMG_STB_OK && gray.w == 145, "grey progressive: %s", img_stb_err_name(gray.err));
    if (gray.px) {
        const uint16_t g = gray.px[40 * 145 + 40];
        CHECK(chan_diff(g, 0xFFFF) <= 8, "grey white is %04x", g);
    }

    /* Thinned to the smallest whole step that still covers the box. */
    pic_t big = decode_file("prog1500x1000.jpg", 720, 400);
    CHECK(big.err == IMG_STB_OK && big.w == 750 && big.h == 500,
          "1500x1000 into 720x400: %dx%d, want 750x500 (step 2)", big.w, big.h);
    pic_t big1 = decode_file("prog1500x1000.jpg", 720, 720);
    CHECK(big1.w == 1500 && big1.h == 1000, "a box it cannot halve: %dx%d", big1.w, big1.h);
    CHECK(img_stb_peak_bytes(1000, 1000) >= 10u * 1000u * 1000u, "peak estimate");

    /* Refusals, without crashing. */
    {
        size_t len; uint8_t *b = slurp("prog145.jpg", &len);
        pic_t t = decode(b, len / 2, 0, 0);          /* truncated */
        CHECK(t.err == IMG_STB_OK || t.err == IMG_STB_FAILED,
              "truncated: %s", img_stb_err_name(t.err));
        free(t.px);
        pic_t j = decode((const uint8_t *)"not a jpeg at all", 17, 0, 0);
        CHECK(j.err == IMG_STB_NOT_JPEG && !j.px, "junk: %s", img_stb_err_name(j.err));
        pic_t z = decode(b, 0, 0, 0);
        CHECK(z.err == IMG_STB_NOT_JPEG, "empty: %s", img_stb_err_name(z.err));

        /* 5000x5000 in the header: over IMG_STB_MAX_PIXELS, refused
         * before anything is allocated for it. */
        for (size_t i = 0; i + 8 < len; i++) {
            if (b[i] == 0xFF && b[i + 1] == 0xC2) {
                b[i + 5] = 0x13; b[i + 6] = 0x88; b[i + 7] = 0x13; b[i + 8] = 0x88;
                break;
            }
        }
        int w, h;
        CHECK(img_stb_info(b, len, &w, &h) == IMG_STB_TOO_BIG && w == 5000,
              "5000x5000 header: %dx%d", w, h);
        pic_t tb = decode(b, len, 0, 0);
        CHECK(tb.err == IMG_STB_TOO_BIG && !tb.px, "5000x5000 decode: %s", img_stb_err_name(tb.err));
        free(b);
    }

    /* Damaged files: 3000 copies of each fixture with up to 8 bytes
     * changed. Any answer is fine except a crash, which ASan and UBSan
     * turn into a failure. The input is the internet's. */
    {
        const char *names[] = { "prog145.jpg", "base145.jpg", "prog145_gray.jpg" };
        unsigned seed = 5062;
        int decoded = 0;
        for (int f = 0; f < 3; f++) {
            size_t len; uint8_t *orig = slurp(names[f], &len);
            uint8_t *b = malloc(len);
            for (int n = 0; n < 3000; n++) {
                memcpy(b, orig, len);
                const int flips = 1 + (int)((seed = seed * 1103515245u + 12345u) >> 16) % 8;
                for (int k = 0; k < flips; k++) {
                    seed = seed * 1103515245u + 12345u;
                    const size_t at = (seed >> 8) % len;
                    seed = seed * 1103515245u + 12345u;
                    b[at] = (uint8_t)(seed >> 16);
                }
                pic_t d = decode(b, len, 0, 0);
                if (d.err == IMG_STB_OK) {
                    decoded++;
                    CHECK(d.px && d.w > 0 && d.h > 0 &&
                          d.size == (size_t)d.w * d.h * 2, "a damaged decode is malformed");
                } else {
                    CHECK(!d.px, "a refusal left pixels");
                }
                free(d.px);
            }
            free(b); free(orig);
        }
        printf("  9000 damaged files, %d still decoded, none crashed\n", decoded);
    }

    free(base.px); free(prog.px); free(sof1.px); free(gray.px); free(big.px); free(big1.px);
    printf("%d checks, %d failures\n", checks, failures);
    printf(failures ? "FAILURES\n" : "all passed\n");
    return failures ? 1 : 0;
}
