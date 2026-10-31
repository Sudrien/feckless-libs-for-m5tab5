/*
 * decodetest.c -- the ORDER of img_decode() (image_decode.c), with fake
 * decoders behind img_ops_t: hardware, then TJpgDec, then stb_image for a
 * baseline JPEG; stb_image alone for a progressive one; the memory
 * checks; the idle-priority bracket; what a refusal does next.
 *
 * The real decoders are image_esp.c's and are tested on the board. What
 * is tested here is the logic Defeatist's albumart_draw() could not be
 * compiled on a host to test.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feckless_image.h"
#include "feckless_image_decode.h"
#include "feckless_image_stb.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++;      \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__);   \
    printf("\n"); } } while (0)

/* ---- the fakes ------------------------------------------------------ */
static struct {
    size_t free_ps, largest;
    int hw_ready;
    img_err_t hw_info_r, hw_r, tjpgd_r, stb_r, gif_r, png_r, jxl_r;
    int hw_stride_off;              /* make the hardware's stride wrong */
    uint32_t info_w, info_h;
    int mcu_w, mcu_h;
    /* calls */
    int n_hw_ready, n_hw_info, n_hw, n_tjpgd, n_stb, n_gif, n_png, n_jxl, begin, end;
    unsigned long token;
    uint32_t got_pad_w, got_pad_h, got_tj_w, got_tj_h;
    uint16_t got_bg;
    int got_box_w, got_box_h;
} F;

static void reset(void)
{
    memset(&F, 0, sizeof F);
    F.free_ps = 64u << 20;
    F.largest = 32u << 20;
    F.hw_ready = 1;
    F.info_w = 700; F.info_h = 500;
    F.mcu_w = 16; F.mcu_h = 16;
    F.token = 0x5eed;
}

static size_t f_free(void) { return F.free_ps; }
static size_t f_largest(void) { return F.largest; }
static int f_ready(void) { F.n_hw_ready++; return F.hw_ready; }
static img_err_t f_info(const uint8_t *in, size_t len, img_hw_info_t *i)
{
    (void)in; (void)len; F.n_hw_info++;
    if (F.hw_info_r != IMG_OK) return F.hw_info_r;
    i->width = F.info_w; i->height = F.info_h; i->mcu_w = F.mcu_w; i->mcu_h = F.mcu_h;
    return IMG_OK;
}
static img_err_t make(img_pic_t *p, size_t px, int w, int h, int stride, const char *dec)
{
    p->px = malloc(px * 2);
    p->size = px * 2; p->w = w; p->h = h; p->stride = stride; p->decoder = dec;
    return IMG_OK;
}
static img_err_t f_hw(const uint8_t *in, size_t len, const img_hw_info_t *i,
                      uint32_t pw, uint32_t ph, img_pic_t *p)
{
    (void)in; (void)len; (void)i; F.n_hw++;
    F.got_pad_w = pw; F.got_pad_h = ph;
    if (F.hw_r != IMG_OK) return F.hw_r;
    return make(p, (size_t)pw * ph, 0, 0, (int)pw + F.hw_stride_off, "x");
}
static unsigned long f_begin(void) { F.begin++; return F.token; }
static void f_end(unsigned long t) { F.end++; CHECK(t == F.token, "soft_end got the token soft_begin gave"); }
static img_err_t f_tj(const uint8_t *in, size_t len, uint32_t w, uint32_t h, int bw, int bh, img_pic_t *p)
{
    (void)in; (void)len; F.n_tjpgd++; F.got_tj_w = w; F.got_tj_h = h; F.got_box_w = bw; F.got_box_h = bh;
    if (F.tjpgd_r != IMG_OK) return F.tjpgd_r;
    return make(p, (size_t)(w / 4) * (h / 4), (int)(w / 4), (int)(h / 4), (int)(w / 4), "tjpgd");
}
static img_err_t f_stb(const uint8_t *in, size_t len, int bw, int bh, img_pic_t *p)
{
    (void)in; (void)len; (void)bw; (void)bh; F.n_stb++;
    if (F.stb_r != IMG_OK) return F.stb_r;
    return make(p, 100, 10, 10, 10, "stb");
}
static img_err_t f_gif(const uint8_t *in, size_t len, int bw, int bh, img_pic_t *p)
{
    (void)in; (void)len; (void)bw; (void)bh; F.n_gif++;
    if (F.gif_r != IMG_OK) return F.gif_r;
    return make(p, 64 * 48, 64, 48, 64, "stb");
}
static img_err_t f_png(const uint8_t *in, size_t len, int bw, int bh, uint16_t bg, img_pic_t *p)
{
    (void)in; (void)len; (void)bw; (void)bh; F.n_png++; F.got_bg = bg;
    if (F.png_r != IMG_OK) return F.png_r;
    return make(p, 64 * 48, 64, 48, 64, "pngle");
}
static img_err_t f_jxl(const uint8_t *in, size_t len, int bw, int bh, uint16_t bg, img_pic_t *p)
{
    (void)in; (void)len; (void)bw; (void)bh; F.n_jxl++; F.got_bg = bg;
    if (F.jxl_r != IMG_OK) return F.jxl_r;
    return make(p, 320 * 240, 320, 240, 320, "j40");
}

static const img_ops_t ALL = {
    .psram_free = f_free, .psram_largest = f_largest,
    .hw_ready = f_ready, .hw_info = f_info, .hw_decode = f_hw,
    .soft_begin = f_begin, .soft_end = f_end,
    .tjpgd = f_tj, .stb = f_stb, .gif = f_gif, .png = f_png, .jxl = f_jxl,
};

/* ---- inputs --------------------------------------------------------- */
static size_t jpeg_with(uint8_t *b, uint8_t sof, int w, int h)
{
    size_t n = 0;
    b[n++] = 0xFF; b[n++] = 0xD8;
    b[n++] = 0xFF; b[n++] = sof; b[n++] = 0; b[n++] = 17; b[n++] = 8;
    b[n++] = (uint8_t)(h >> 8); b[n++] = (uint8_t)h;
    b[n++] = (uint8_t)(w >> 8); b[n++] = (uint8_t)w;
    for (int i = 0; i < 10; i++) b[n++] = 0;
    return n;
}

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

static img_err_t run(const img_ops_t *o, const uint8_t *b, size_t n, img_pic_t *p)
{
    return img_decode(o, b, n, 720, 720, 0x0841, p);
}

int main(void)
{
    printf("decodetest\n");
    uint8_t jb[64], jp[64], ja[64];
    const size_t jn = jpeg_with(jb, 0xC0, 700, 500);       /* baseline */
    const size_t pn = jpeg_with(jp, 0xC2, 700, 500);       /* progressive */
    const size_t an = jpeg_with(ja, 0xC9, 700, 500);       /* arithmetic */
    img_pic_t p;

    /* The arithmetic. */
    CHECK(img_pad_up(3000, 16) == 3008 && img_pad_up(3008, 16) == 3008 && img_pad_up(145, 8) == 152, "pad");
    CHECK(img_tjpgd_first_scale(3000, 3000, 720, 720) == 2, "3000 into 720 starts at 1/4");
    CHECK(img_tjpgd_first_scale(1000, 1000, 720, 720) == 1, "1000 into 720 starts at 1/2");
    CHECK(img_tjpgd_first_scale(145, 145, 720, 720) == 1, "a small one still starts at 1/2");
    CHECK(img_tjpgd_first_scale(9000, 9000, 720, 720) == 3, "never past 1/8");
    CHECK(img_tjpgd_first_scale(3000, 700, 720, 720) == 1, "the short side decides");

    /* 1. Hardware, the happy path: stride is the padded width, the size
     * is the picture's, and the software bracket is not touched. */
    reset();
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK, "hw path");
    CHECK(p.decoder && !strcmp(p.decoder, "hardware") && p.w == 700 && p.h == 500, "hw: %s %dx%d", p.decoder, p.w, p.h);
    CHECK(p.stride == 704 && F.got_pad_w == 704 && F.got_pad_h == 512, "hw pads to the MCU grid: stride %d, %ux%u", p.stride, F.got_pad_w, F.got_pad_h);
    CHECK(p.size == 704u * 512u * 2u && p.skipped == NULL, "hw size");
    CHECK(F.n_tjpgd == 0 && F.n_stb == 0 && F.begin == 0 && F.end == 0, "hw: no software, no priority drop");
    CHECK(p.sof == 0xC0 && p.sof_w == 700 && p.sof_h == 500, "sof recorded");
    free(p.px);

    /* 4:4:4 pads to 8. */
    reset(); F.mcu_w = 8; F.mcu_h = 8; F.info_w = 145; F.info_h = 145;
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK && p.stride == 152 && p.w == 145, "145 at 8 pads to 152");
    free(p.px);

    /* 2. No engine: TJpgDec, at idle priority, with the header's size. */
    reset(); F.hw_ready = 0;
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK && !strcmp(p.decoder, "tjpgd"), "no engine -> tjpgd");
    CHECK(p.skipped && strstr(p.skipped, "no hardware"), "skipped says why: %s", p.skipped ? p.skipped : "-");
    CHECK(F.n_hw == 0 && F.begin == 1 && F.end == 1, "bracketed once");
    CHECK(F.got_tj_w == 700 && F.got_tj_h == 500 && F.got_box_w == 720, "tjpgd got the sof size and the box");
    free(p.px);

    /* 3. The padded result does not fit in one block: TJpgDec, and the
     * hardware is never asked to decode. */
    reset(); F.largest = 704u * 512u * 2u - 1;
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK && !strcmp(p.decoder, "tjpgd") && F.n_hw == 0, "no block -> tjpgd");
    free(p.px);
    reset(); F.largest = 704u * 512u * 2u;
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK && !strcmp(p.decoder, "hardware"), "exactly one block is enough");
    free(p.px);

    /* 4. The hardware refuses the picture (size not divisible by 8): TJpgDec. */
    reset(); F.hw_r = IMG_ERR_FAILED;
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK && !strcmp(p.decoder, "tjpgd") && F.n_hw == 1, "hw refuses -> tjpgd");
    CHECK(p.skipped && strstr(p.skipped, "refused"), "skipped: %s", p.skipped ? p.skipped : "-");
    free(p.px);
    reset(); F.hw_info_r = IMG_ERR_FAILED;
    CHECK(run(&ALL, jb, jn, &p) == IMG_OK && !strcmp(p.decoder, "tjpgd") && F.n_hw == 0, "hw cannot read the header -> tjpgd");
    free(p.px);

    /* 5. TJpgDec refuses the FORMAT: stb_image, the last decoder. */
    reset(); F.hw_ready = 0; F.tjpgd_r = IMG_ERR_FAILED;
    {
        size_t gn;
        uint8_t *real = slurp("../stbjpeg/prog145.jpg", &gn);       /* a real JPEG for img_stb_info */
        uint8_t base[64]; const size_t bn = jpeg_with(base, 0xC0, 145, 145);
        (void)real;
        /* stb needs a real header to size: use a baseline fixture. */
        size_t fn; uint8_t *fx = slurp("../stbjpeg/base145.jpg", &fn);
        CHECK(run(&ALL, fx, fn, &p) == IMG_OK && !strcmp(p.decoder, "stb"), "tjpgd FAILED -> stb");
        CHECK(F.n_tjpgd == 1 && F.n_stb == 1 && F.begin == 1 && F.end == 1, "one bracket spans both software decoders");
        CHECK(p.skipped && strstr(p.skipped, "TJpgDec"), "skipped: %s", p.skipped ? p.skipped : "-");
        free(p.px);

        /* 6. TJpgDec is out of MEMORY: that is the answer; stb is not tried. */
        reset(); F.hw_ready = 0; F.tjpgd_r = IMG_ERR_NO_MEM;
        CHECK(run(&ALL, fx, fn, &p) == IMG_ERR_NO_MEM && F.n_stb == 0 && !p.px, "tjpgd NO_MEM stays NO_MEM");
        CHECK(F.begin == 1 && F.end == 1, "and the bracket still closes");

        /* stb has no memory either: refused before it starts, with a reason. */
        reset(); F.hw_ready = 0; F.tjpgd_r = IMG_ERR_FAILED; F.free_ps = 1024;
        CHECK(run(&ALL, fx, fn, &p) == IMG_ERR_NO_MEM && F.n_stb == 0 && p.why, "stb is asked about memory first");
        reset(); F.hw_ready = 0; F.tjpgd_r = IMG_ERR_FAILED; F.stb_r = IMG_ERR_FAILED;
        CHECK(run(&ALL, fx, fn, &p) == IMG_ERR_FAILED && !p.px, "every decoder refused -> FAILED");
        free(fx); free(real); (void)base; (void)bn;
    }

    /* 7. Progressive: stb_image alone. Neither the hardware nor TJpgDec is asked. */
    {
        size_t fn; uint8_t *fx = slurp("../stbjpeg/prog145.jpg", &fn);
        reset();
        /* the fixture's own SOF is C2 */
        CHECK(run(&ALL, fx, fn, &p) == IMG_OK && !strcmp(p.decoder, "stb"), "progressive -> stb");
        CHECK(F.n_hw_ready == 0 && F.n_hw == 0 && F.n_tjpgd == 0 && F.begin == 1 && F.end == 1, "and nothing else");
        CHECK(p.sof == 0xC2, "sof 0xC2");
        free(p.px); free(fx);
    }
    reset();
    CHECK(run(&ALL, jp, pn, &p) == IMG_ERR_UNSUPPORTED && !p.px, "a progressive header stb cannot size is UNSUPPORTED");

    /* 8. Another SOF: refused, named, nothing tried. */
    reset();
    CHECK(run(&ALL, ja, an, &p) == IMG_ERR_UNSUPPORTED && p.sof == 0xC9 && p.why && F.n_stb + F.n_hw + F.n_tjpgd == 0, "arithmetic JPEG refused");

    /* 9. The hardware writes a buffer that is not the size its header
     * implied: an error, not a sheared picture, and the buffer is freed
     * (ASan checks that). */
    reset(); F.hw_stride_off = 8;
    CHECK(run(&ALL, jb, jn, &p) == IMG_ERR_FAILED && !p.px && p.why, "wrong stride -> FAILED");

    /* 10. GIF, PNG, JXL: their decoder, the memory check, bg passed on. */
    {
        size_t gn; uint8_t *g = slurp("gif.gif", &gn);
        reset();
        CHECK(run(&ALL, g, gn, &p) == IMG_OK && F.n_gif == 1 && F.begin == 1 && F.end == 1 && F.n_stb == 0, "gif");
        free(p.px);
        reset(); F.free_ps = 100;
        CHECK(run(&ALL, g, gn, &p) == IMG_ERR_NO_MEM && F.n_gif == 0, "gif memory check");
        free(g);

        size_t jn2; uint8_t *j = slurp("lossy.jxl", &jn2);
        reset();
        CHECK(run(&ALL, j, jn2, &p) == IMG_OK && F.n_jxl == 1 && F.got_bg == 0x0841 && p.w == 320, "jxl, with bg");
        free(p.px);
        reset(); F.free_ps = 320u * 240u * 13u + IMG_JXL_STACK - 1;
        CHECK(run(&ALL, j, jn2, &p) == IMG_ERR_NO_MEM && F.n_jxl == 0, "jxl memory check includes its stack");
        free(j);

        size_t pn2; uint8_t *pg = slurp("rgb.png", &pn2);
        reset();
        CHECK(run(&ALL, pg, pn2, &p) == IMG_OK && F.n_png == 1 && F.got_bg == 0x0841 && F.begin == 1 && F.end == 1, "png, bracketed, with bg");
        free(p.px);
        reset(); F.png_r = IMG_ERR_FAILED;
        CHECK(run(&ALL, pg, pn2, &p) == IMG_ERR_FAILED && !p.px, "png refused");
        free(pg);
    }

    /* 11. Junk, and a table with nothing in it. */
    reset();
    CHECK(run(&ALL, (const uint8_t *)"BM........", 10, &p) == IMG_ERR_BAD, "BMP is not a picture here");
    CHECK(run(&ALL, NULL, 0, &p) == IMG_ERR_BAD, "NULL");
    CHECK(img_decode(NULL, jb, jn, 0, 0, 0, &p) == IMG_ERR_UNSUPPORTED, "no table");
    {
        static const img_ops_t none = {0};
        size_t gn; uint8_t *g = slurp("gif.gif", &gn);
        size_t pn2; uint8_t *pg = slurp("rgb.png", &pn2);
        CHECK(img_decode(&none, g, gn, 0, 0, 0, &p) == IMG_ERR_UNSUPPORTED, "empty table: gif");
        CHECK(img_decode(&none, pg, pn2, 0, 0, 0, &p) == IMG_ERR_UNSUPPORTED, "empty table: png");
        CHECK(img_decode(&none, jb, jn, 0, 0, 0, &p) == IMG_ERR_FAILED || p.px == NULL, "empty table: baseline jpeg finds nobody");
        free(g); free(pg);
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
