/*
 * blittest.c -- gfx_blit() at every angle, the CPU's turn and the PPA's
 * (u-feckless-0057).
 *
 * src/gfx.c is compiled as it ships, with CONFIG_FECKLESS_GFX_PPA on and
 * the panel replaced by an image this file keeps: every
 * esp_lcd_panel_draw_bitmap() is copied into it where it lands. The
 * expected picture is the forward map gfx.c documents and touch.c
 * inverts (90: px = pw-1-y, py = x; 180: both from the far end; 270:
 * px = y, py = ph-1-x).
 *
 *   ./blittest cpu   the PPA refuses its client: every turn is the CPU's
 *   ./blittest ppa   the PPA turns, as modelled below
 *
 * The model is the part this test cannot prove, only pin: it turns a
 * block COUNTER-clockwise, as ppa_types.h says, and it must reproduce
 * IDF's own SRM test vector (esp_driver_ppa/test_apps, "ppa_srm_basic_data_correctness")
 * before anything else runs. It refuses what IDF 6.1's
 * ppa_do_scale_rotate_mirror() refuses that gfx.c could get wrong: an
 * output buffer or size off the 128-byte line, a picture too small for
 * the block, a block outside its picture, a field past DMA2D's 14 bits.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"
#include "driver/ppa.h"
#include "brightness.h"
#include "gfx.h"

#define PW 720
#define PH 1280
#define LINE 128            /* the P4's L2 line: the driver's ext_mem_align */
#define FIELD_MAX 0x3FFFu   /* DMA2D_LL_DESC_2D_FIELD_MAX */

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the panel ---------------------------------------------------- */

static uint16_t s_panel[PW * PH];
static int s_draws;

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p, int x0, int y0, int x1, int y1, const void *data)
{
    (void)p;
    const uint16_t *d = data;
    const int w = x1 - x0;
    if (x0 < 0 || y0 < 0 || x1 > PW || y1 > PH || w <= 0 || y1 <= y0) {
        printf("FAIL draw_bitmap outside the glass: %d,%d..%d,%d\n", x0, y0, x1, y1);
        failures++;
        return ESP_ERR_INVALID_ARG;
    }
    for (int y = y0; y < y1; y++) memcpy(&s_panel[(size_t)y * PW + x0], &d[(size_t)(y - y0) * w], (size_t)w * 2);
    s_draws++;
    return ESP_OK;
}
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t p,
                                                     const esp_lcd_dpi_panel_event_callbacks_t *cbs, void *arg)
{ (void)p; (void)cbs; (void)arg; return ESP_OK; }
esp_err_t esp_lcd_dpi_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t n, void **fb)
{ (void)p; (void)n; (void)fb; return ESP_FAIL; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "ESP_ERR"; }

void *heap_caps_malloc(size_t sz, uint32_t caps) { (void)caps; return malloc(sz); }
void *heap_caps_calloc(size_t n, size_t sz, uint32_t caps) { (void)caps; return calloc(n, sz); }
void *heap_caps_aligned_alloc(size_t align, size_t sz, uint32_t caps)
{
    (void)caps;
    return aligned_alloc(align, (sz + align - 1) / align * align);
}
void heap_caps_free(void *p) { free(p); }

/* ---- the PPA, modelled -------------------------------------------- */

struct ppa_client_t { int unused; };
static struct ppa_client_t s_client;
static bool s_refuse_client;        /* "cpu" */
static int s_ops, s_refused, s_fail_next;

esp_err_t ppa_register_client(const ppa_client_config_t *cfg, ppa_client_handle_t *ret)
{
    if (s_refuse_client || !cfg || cfg->oper_type != PPA_OPERATION_SRM) return ESP_FAIL;
    *ret = &s_client;
    return ESP_OK;
}

static esp_err_t refuse(const char *why)
{
    s_refused++;
    printf("  model refuses: %s\n", why);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t c, const ppa_srm_oper_config_t *op)
{
    if (c != &s_client || !op) return ESP_ERR_INVALID_ARG;
    s_ops++;
    const ppa_in_pic_blk_config_t *in = &op->in;
    const ppa_out_pic_blk_config_t *out = &op->out;
    if (in->srm_cm != PPA_SRM_COLOR_MODE_RGB565 || out->srm_cm != PPA_SRM_COLOR_MODE_RGB565) return refuse("colour mode");
    if (op->scale_x != 1.0f || op->scale_y != 1.0f || op->mirror_x || op->mirror_y) return refuse("scale or mirror");
    if (op->mode != PPA_TRANS_MODE_BLOCKING) return refuse("not blocking");
    if (in->pic_w > FIELD_MAX || in->pic_h > FIELD_MAX || out->pic_w > FIELD_MAX || out->pic_h > FIELD_MAX)
        return refuse("a field past 14 bits");
    if (!in->block_w || !in->block_h || in->block_offset_x >= in->pic_w ||
        in->block_w > in->pic_w - in->block_offset_x || in->block_offset_y >= in->pic_h ||
        in->block_h > in->pic_h - in->block_offset_y) return refuse("block outside the input picture");
    if (((uintptr_t)out->buffer & (LINE - 1)) || (out->buffer_size & (LINE - 1))) return refuse("output off the line");
    if ((uint64_t)out->pic_w * out->pic_h * 2 > out->buffer_size) return refuse("output picture past its buffer");
    const bool swaps = op->rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || op->rotation_angle == PPA_SRM_ROTATION_ANGLE_270;
    const uint32_t nw = swaps ? in->block_h : in->block_w, nh = swaps ? in->block_w : in->block_h;
    if (out->block_offset_x >= out->pic_w || nw > out->pic_w - out->block_offset_x ||
        out->block_offset_y >= out->pic_h || nh > out->pic_h - out->block_offset_y)
        return refuse("turned block does not fit the output picture");

    uint16_t *o = out->buffer;
    if (s_fail_next) {
        /* A transfer that failed: what it left is not to be sent. */
        s_fail_next = 0;
        memset(o, 0xA5, out->buffer_size);
        return ESP_FAIL;
    }
    const uint16_t *i = in->buffer;
    const uint32_t bw = in->block_w, bh = in->block_h;
    for (uint32_t y = 0; y < bh; y++) {
        for (uint32_t x = 0; x < bw; x++) {
            uint32_t ox, oy;                        /* counter-clockwise */
            switch (op->rotation_angle) {
            case PPA_SRM_ROTATION_ANGLE_90:  ox = y;          oy = bw - 1 - x; break;
            case PPA_SRM_ROTATION_ANGLE_180: ox = bw - 1 - x; oy = bh - 1 - y; break;
            case PPA_SRM_ROTATION_ANGLE_270: ox = bh - 1 - y; oy = x;          break;
            default:                         ox = x;          oy = y;          break;
            }
            o[(size_t)(oy + out->block_offset_y) * out->pic_w + ox + out->block_offset_x] =
                i[(size_t)(y + in->block_offset_y) * in->pic_w + x + in->block_offset_x];
        }
    }
    return ESP_OK;
}

/* IDF 6.1's esp_driver_ppa/test_apps/main/test_ppa.cpp, the 4x4 RGB565
 * block at (1,1), PPA_SRM_ROTATION_ANGLE_90, into (1,0): its expected
 * output, word for word. */
static void test_model_against_idf(void)
{
    static const uint16_t in_buf[16] = {
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0x8080, 0x8080, 0x8080,
        0xFFFF, 0x8F80, 0x8F80, 0x8F80,
        0xFFFF, 0xFF80, 0xFF80, 0xFF80,
    };
    static const uint16_t expected[16] = {
        0x0000, 0x8080, 0x8F80, 0xFF80,
        0x0000, 0x8080, 0x8F80, 0xFF80,
        0x0000, 0x8080, 0x8F80, 0xFF80,
        0x0000, 0x0000, 0x0000, 0x0000,
    };
    uint16_t *out = aligned_alloc(LINE, LINE);
    memset(out, 0, LINE);
    ppa_srm_oper_config_t op = {
        .in = { .buffer = in_buf, .pic_w = 4, .pic_h = 4, .block_w = 3, .block_h = 3,
                .block_offset_x = 1, .block_offset_y = 1, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = out, .buffer_size = LINE, .pic_w = 4, .pic_h = 4,
                 .block_offset_x = 1, .block_offset_y = 0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90, .scale_x = 1.0f, .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    const int ops = s_ops;
    CHECK(ppa_do_scale_rotate_mirror(&s_client, &op) == ESP_OK, "the model takes IDF's vector");
    CHECK(memcmp(out, expected, sizeof expected) == 0, "the model turns IDF's vector as IDF expects");
    s_ops = ops;
    free(out);
}

/* ---- the picture ----------------------------------------------------- */

static uint16_t pat(int x, int y) { return (uint16_t)(((uint32_t)x * 2654435761u ^ (uint32_t)y * 40503u) >> 7); }

/* Every logical pixel distinct enough that a transposition, a flip or a
 * band in the wrong place shows. */
static void paint(void)
{
    uint16_t *fb = gfx_fb();
    const int w = gfx_w(), h = gfx_h();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) fb[(size_t)y * w + x] = pat(x, y);
}

/* Where logical (x, y) is on the glass at `rot`. */
static void to_glass(int rot, int x, int y, int *px, int *py)
{
    switch (rot) {
    case GFX_ROT_90:  *px = PW - 1 - y; *py = x;          break;
    case GFX_ROT_180: *px = PW - 1 - x; *py = PH - 1 - y; break;
    case GFX_ROT_270: *px = y;          *py = PH - 1 - x; break;
    default:          *px = x;          *py = y;          break;
    }
}

#define UNTOUCHED 0xBEEF

/* Logical rows [y0, y1) blitted at `rot`: each lands where the map says,
 * dimmed by `filter`, and nothing else on the glass changes. */
static int check_blit(int rot, int y0, int y1, int filter, const char *what)
{
    gfx_set_rotation(rot);
    gfx_set_filter(filter);
    paint();
    for (size_t i = 0; i < (size_t)PW * PH; i++) s_panel[i] = UNTOUCHED;
    gfx_blit(y0, y1);
    const int w = gfx_w(), h = gfx_h();
    int wrong = 0, stray = 0;
    static uint8_t hit[PW * PH];
    memset(hit, 0, sizeof hit);
    for (int y = y0; y < y1 && y < h; y++) {
        for (int x = 0; x < w; x++) {
            int px, py;
            to_glass(rot, x, y, &px, &py);
            hit[(size_t)py * PW + px] = 1;
            uint16_t want = pat(x, y);
            if (filter < BRIGHTNESS_FILTER_FULL) want = brightness_dim565(want, filter);
            if (s_panel[(size_t)py * PW + px] != want) wrong++;
        }
    }
    for (size_t i = 0; i < (size_t)PW * PH; i++)
        if (!hit[i] && s_panel[i] != UNTOUCHED) stray++;
    CHECK(wrong == 0 && stray == 0, "%s: rot%d rows %d..%d filter %d: %d pixels wrong, %d outside the band",
          what, rot * 90, y0, y1, filter, wrong, stray);
    gfx_set_filter(BRIGHTNESS_FILTER_FULL);
    return wrong + stray;
}

int main(int argc, char **argv)
{
    const bool ppa = argc > 1 && strcmp(argv[1], "ppa") == 0;
    s_refuse_client = !ppa;
    test_model_against_idf();
    if (gfx_init(NULL, PW, PH) != ESP_OK) { printf("gfx_init failed\n"); return 1; }

    for (int rot = GFX_ROT_0; rot <= GFX_ROT_270; rot++) {
        const int ops = s_ops;
        const int h = (rot & 1) ? PW : PH;
        check_blit(rot, 0, h, BRIGHTNESS_FILTER_FULL, "whole");
        check_blit(rot, 100, 137, BRIGHTNESS_FILTER_FULL, "an odd band");
        check_blit(rot, h - 5, h, BRIGHTNESS_FILTER_FULL, "the last rows");
        check_blit(rot, 0, 1, BRIGHTNESS_FILTER_FULL, "one row");
        if (rot == GFX_ROT_0 || !ppa) CHECK(s_ops == ops, "rot%d: the PPA not used (%d turns)", rot * 90, s_ops - ops);
        else CHECK(s_ops > ops, "rot%d: the PPA turned (%d turns)", rot * 90, s_ops - ops);
    }
    CHECK(s_refused == 0, "the model refused %d of gfx's turns", s_refused);

    /* A fade: dimmed, so the CPU's, whatever the PPA could do. */
    {
        const int ops = s_ops;
        check_blit(GFX_ROT_90, 0, PW, BRIGHTNESS_FILTER_FULL / 3, "dimmed");
        check_blit(GFX_ROT_180, 0, PH, BRIGHTNESS_FILTER_FULL / 2, "dimmed");
        CHECK(s_ops == ops, "a dimmed band is not the PPA's (%d turns)", s_ops - ops);
    }

    /* The PPA fails a turn: that band from the CPU, and every band after. */
    if (ppa) {
        s_fail_next = 1;
        const int ops = s_ops;
        check_blit(GFX_ROT_90, 0, PW, BRIGHTNESS_FILTER_FULL, "after a failed turn");
        CHECK(s_ops == ops + 1, "one try, then the CPU (%d turns)", s_ops - ops);
        check_blit(GFX_ROT_270, 0, PW, BRIGHTNESS_FILTER_FULL, "given up");
        CHECK(s_ops == ops + 1, "not tried again (%d turns)", s_ops - ops);
    }

    printf("blittest %s: %d checks, %d failures, %d PPA turns, %d draws\n",
           ppa ? "ppa" : "cpu", checks, failures, s_ops, s_draws);
    return failures ? 1 : 0;
}
