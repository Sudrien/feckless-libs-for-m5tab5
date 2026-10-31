/* Host test for feckless_image.h and image_ico.c. SPDX-License-Identifier: MIT */
#include "feckless_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- sniff / mime -------------------------------------------------- */
static void test_sniff(void)
{
    const uint8_t jpg[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 0 };
    const uint8_t png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0 };
    const uint8_t jxlc[] = { 0xFF, 0x0A, 0, 0 };
    uint8_t jxlb[16] = { 0, 0, 0, 0x0c, 'J', 'X', 'L', ' ', 0x0d, 0x0a, 0x87, 0x0a, 0, 0, 0, 0 };
    const uint8_t gif[] = "GIF89a\x01\x00\x01\x00\x00\x00\x00\x00";
    const uint8_t bmp[] = "BM\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    CHECK(img_sniff(jpg, sizeof jpg) == IMG_JPEG);
    CHECK(img_sniff(png, sizeof png) == IMG_PNG);
    CHECK(img_sniff(jxlc, sizeof jxlc) == IMG_JXL);
    CHECK(img_sniff(jxlb, sizeof jxlb) == IMG_JXL);
    CHECK(img_sniff(gif, sizeof gif - 1) == IMG_GIF);
    CHECK(img_sniff((const uint8_t *)"GIF87a\0\0\0\0\0\0\0\0", 14) == IMG_GIF);
    CHECK(img_sniff(bmp, sizeof bmp - 1) == IMG_NONE);
    CHECK(img_sniff(NULL, 100) == IMG_NONE);
    CHECK(img_sniff(jpg, 3) == IMG_NONE);          /* too short to be sure */
    CHECK(img_sniff(jpg, 0) == IMG_NONE);

    CHECK(strcmp(img_mime(IMG_JPEG), "image/jpeg") == 0);
    CHECK(strcmp(img_mime(IMG_PNG), "image/png") == 0);
    CHECK(strcmp(img_mime(IMG_JXL), "image/jxl") == 0);
    CHECK(strcmp(img_mime(IMG_GIF), "image/gif") == 0);
    CHECK(strcmp(img_mime(IMG_NONE), "application/octet-stream") == 0);
}

/* ---- JPEG SOF walk -------------------------------------------------- */
static size_t jpeg_with(uint8_t *b, uint8_t sof, int w, int h, bool app0_first)
{
    size_t n = 0;
    b[n++] = 0xFF; b[n++] = 0xD8;
    if (app0_first) {                              /* APP0, 16 bytes with the length */
        b[n++] = 0xFF; b[n++] = 0xE0; b[n++] = 0; b[n++] = 16;
        for (int i = 0; i < 14; i++) b[n++] = 0;
    }
    b[n++] = 0xFF; b[n++] = sof; b[n++] = 0; b[n++] = 17; b[n++] = 8;
    b[n++] = (uint8_t)(h >> 8); b[n++] = (uint8_t)h;
    b[n++] = (uint8_t)(w >> 8); b[n++] = (uint8_t)w;
    for (int i = 0; i < 10; i++) b[n++] = 0;
    return n;
}

static void test_sof(void)
{
    uint8_t b[128], sof; uint32_t w, h;
    size_t n = jpeg_with(b, 0xC0, 700, 500, true);
    CHECK(img_jpeg_sof(b, n, &sof, &w, &h) && sof == 0xC0 && w == 700 && h == 500);
    n = jpeg_with(b, 0xC1, 3000, 2000, false);
    CHECK(img_jpeg_sof(b, n, &sof, &w, &h) && sof == 0xC1 && w == 3000 && h == 2000);
    n = jpeg_with(b, 0xC2, 640, 480, true);                  /* progressive */
    CHECK(!img_jpeg_sof(b, n, &sof, &w, &h) && sof == 0xC2 && w == 640 && h == 480);
    n = jpeg_with(b, 0xC9, 10, 20, false);                   /* arithmetic */
    CHECK(!img_jpeg_sof(b, n, &sof, &w, &h) && sof == 0xC9);

    const uint8_t sos_first[] = { 0xFF, 0xD8, 0xFF, 0xDA, 0, 4, 0, 0, 0, 0 };
    CHECK(!img_jpeg_sof(sos_first, sizeof sos_first, &sof, &w, &h) && sof == 0);
    const uint8_t no_soi[] = { 0x89, 'P', 'N', 'G', 0, 0 };
    CHECK(!img_jpeg_sof(no_soi, sizeof no_soi, &sof, &w, &h) && sof == 0 && w == 0 && h == 0);
    CHECK(!img_jpeg_sof(b, 3, &sof, &w, &h));
    /* Cut off inside the SOF. */
    n = jpeg_with(b, 0xC0, 700, 500, false);
    CHECK(img_jpeg_sof(b, n, &sof, &w, &h));
    /* As moved from Defeatist: the marker says baseline, the size is not there to read. */
    CHECK(img_jpeg_sof(b, 8, &sof, &w, &h) && sof == 0xC0 && w == 0 && h == 0);
    /* A segment length under 2 is refused, not looped on. */
    const uint8_t bad[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 1, 0, 0, 0, 0 };
    CHECK(!img_jpeg_sof(bad, sizeof bad, &sof, &w, &h));
}

/* ---- integer scale --------------------------------------------------- */
static void test_scale(void)
{
    CHECK(img_int_scale(16, 16, 720, 720) == 45);
    CHECK(img_int_scale(16, 32, 720, 720) == 22);          /* the tighter axis */
    CHECK(img_int_scale(256, 256, 720, 720) == 2);
    CHECK(img_int_scale(257, 257, 2000, 2000) == 0);        /* a cover, not art */
    CHECK(img_int_scale(400, 400, 720, 720) == 0);          /* k would be 1 */
    CHECK(img_int_scale(0, 10, 720, 720) == 0);
    CHECK(img_int_scale(10, -1, 720, 720) == 0);
    CHECK(img_int_scale(16, 16, 20, 720) == 0);             /* box too narrow for 2x */
}

/* ---- ICO ------------------------------------------------------------- */
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static uint32_t get32be(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

/* Unwrap a PNG made of one IHDR, one IDAT of stored blocks, an IEND:
 * check every CRC and the Adler-32, and return the raw scanlines. */
static uint8_t *png_unwrap(const uint8_t *png, size_t len, int *w, int *h, size_t *raw_len)
{
    if (len < 8 + 25 + 12 || memcmp(png, "\x89PNG\r\n\x1a\n", 8)) return NULL;
    const uint8_t *p = png + 8;
    if (get32be(p) != 13 || memcmp(p + 4, "IHDR", 4)) return NULL;
    if (get32be(p + 21) != crc32_ref(p + 4, 17)) return NULL;
    *w = (int)get32be(p + 8); *h = (int)get32be(p + 12);
    if (p[16] != 8 || p[17] != 6) return NULL;
    p += 25;
    const uint32_t idat = get32be(p);
    if (memcmp(p + 4, "IDAT", 4)) return NULL;
    if (get32be(p + 8 + idat) != crc32_ref(p + 4, 4 + idat)) return NULL;
    const uint8_t *z = p + 8, *zend = p + 8 + idat;
    if (z[0] != 0x78) return NULL;
    z += 2;
    uint8_t *raw = malloc((size_t)(*h) * (1 + (size_t)(*w) * 4));
    size_t o = 0;
    for (;;) {
        const int final = z[0] & 1;
        const unsigned n = z[1] | (z[2] << 8), nn = z[3] | (z[4] << 8);
        if (((n ^ nn) & 0xFFFF) != 0xFFFF || (z[0] >> 1) != 0) { free(raw); return NULL; }
        memcpy(raw + o, z + 5, n); o += n; z += 5 + n;
        if (final) break;
    }
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < o; i++) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; }
    if (z + 4 != zend || get32be(z) != ((s2 << 16) | s1)) { free(raw); return NULL; }
    const uint8_t *e = p + 12 + idat;
    if (memcmp(e + 4, "IEND", 4) || (size_t)(e + 12 - png) != len) { free(raw); return NULL; }
    *raw_len = o;
    return raw;
}

/* One-entry .ico around `dib` (a BMP body without its file header). */
static size_t ico_wrap(uint8_t *out, int w, int h, int bpp, const uint8_t *dib, size_t dib_len)
{
    put16(out, 0); put16(out + 2, 1); put16(out + 4, 1);
    uint8_t *d = out + 6;
    d[0] = (uint8_t)w; d[1] = (uint8_t)h; d[2] = 0; d[3] = 0;
    put16(d + 4, 1); put16(d + 6, (unsigned)bpp);
    put32(d + 8, (uint32_t)dib_len); put32(d + 12, 22);
    memcpy(out + 22, dib, dib_len);
    return 22 + dib_len;
}

static void test_ico(void)
{
    /* 2x2, 32-bit BGRA bottom-up, height stored doubled, alpha in use. */
    uint8_t dib[40 + 16 + 8];
    memset(dib, 0, sizeof dib);
    put32(dib, 40); put32(dib + 4, 2); put32(dib + 8, 4);
    put16(dib + 12, 1); put16(dib + 14, 32);
    const uint8_t px[16] = {
        /* bottom row (y=1): blue, green */  255, 0, 0, 255,   0, 255, 0, 128,
        /* top row    (y=0): red, white  */   0, 0, 255, 255,  255, 255, 255, 0 };
    memcpy(dib + 40, px, 16);
    /* the AND mask (8 bytes: 2 rows of 4) is all zero */
    uint8_t ico[256];
    size_t n = ico_wrap(ico, 2, 2, 32, dib, sizeof dib);

    CHECK(img_ico_is_ico(ico, n));
    uint8_t *png = NULL; size_t plen = 0; img_ico_info_t info = {0};
    CHECK(img_ico_to_png(ico, n, &png, &plen, &info));
    CHECK(info.w == 2 && info.h == 2 && info.bpp == 32 && !info.was_png);
    int w = 0, h = 0; size_t rlen = 0;
    uint8_t *raw = png ? png_unwrap(png, plen, &w, &h, &rlen) : NULL;
    CHECK(raw && w == 2 && h == 2 && rlen == 2 * (1 + 8));
    if (raw) {
        /* RGBA, top row first: red, transparent white; then blue, half green. */
        const uint8_t want[18] = { 0, 255, 0, 0, 255,  255, 255, 255, 0,
                                   0, 0, 0, 255, 255,  0, 255, 0, 128 };
        CHECK(memcmp(raw, want, sizeof want) == 0);
    }
    free(raw); free(png);

    /* 24-bit with an AND mask: the masked pixel is transparent. */
    uint8_t d24[40 + 8 + 8];
    memset(d24, 0, sizeof d24);
    put32(d24, 40); put32(d24 + 4, 2); put32(d24 + 8, 4);
    put16(d24 + 12, 1); put16(d24 + 14, 24);
    /* rows padded to 8 bytes; y=1 (bottom) first, 2 px of BGR */
    const uint8_t row1[8] = { 10, 20, 30,  40, 50, 60,  0, 0 };
    const uint8_t row0[8] = { 70, 80, 90,  1, 2, 3,  0, 0 };
    memcpy(d24 + 40, row1, 8); memcpy(d24 + 48, row0, 8);
    /* AND mask: two rows of 4 bytes, bit 7 = leftmost; top row masks x=1.
     * (Too short here on purpose: have_and is false, so all opaque.) */
    n = ico_wrap(ico, 2, 2, 24, d24, 40 + 16);
    CHECK(img_ico_to_png(ico, n, &png, &plen, &info) && info.bpp == 24);
    raw = png ? png_unwrap(png, plen, &w, &h, &rlen) : NULL;
    CHECK(raw && raw[1] == 90 && raw[2] == 80 && raw[3] == 70 && raw[4] == 255);
    free(raw); free(png);

    /* An embedded PNG is copied out unchanged. */
    uint8_t emb[64] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    for (int i = 8; i < 40; i++) emb[i] = (uint8_t)i;
    n = ico_wrap(ico, 16, 16, 32, emb, 40);
    CHECK(img_ico_to_png(ico, n, &png, &plen, &info));
    CHECK(info.was_png && info.w == 16 && plen == 40 && memcmp(png, emb, 40) == 0);
    free(png);

    /* Refusals: no allocation survives a false return. */
    png = (uint8_t *)1; plen = 7;
    CHECK(!img_ico_to_png(ico, 10, &png, &plen, &info) && png == NULL && plen == 0);
    uint8_t cut[256]; memcpy(cut, ico, n);
    put32(cut + 6 + 12, 4000);                               /* offset past the end */
    CHECK(!img_ico_to_png(cut, n, &png, &plen, &info));
    memcpy(cut, ico, n); put32(cut + 6 + 8, 100000);         /* size past the end */
    CHECK(!img_ico_to_png(cut, n, &png, &plen, &info));
    uint8_t not_ico[32] = { 0 }; put16(not_ico + 2, 2);      /* a cursor, type 2 */
    CHECK(!img_ico_is_ico(not_ico, sizeof not_ico) && !img_ico_is_ico(NULL, 100));
    CHECK(!img_ico_to_png(ico, n, NULL, &plen, &info));

    /* Over IMG_ICO_MAX_DIM on a side. */
    memset(dib, 0, sizeof dib);
    put32(dib, 40); put32(dib + 4, IMG_ICO_MAX_DIM + 1); put32(dib + 8, 4);
    put16(dib + 12, 1); put16(dib + 14, 32);
    n = ico_wrap(ico, 0, 2, 32, dib, sizeof dib);
    CHECK(!img_ico_to_png(ico, n, &png, &plen, &info));
}

int main(void)
{
    test_sniff();
    test_sof();
    test_scale();
    test_ico();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
