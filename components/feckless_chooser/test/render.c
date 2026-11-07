/* SPDX-License-Identifier: MIT
 *
 * `make preview`: feckless_chooser drawn by the real gfx.c into its real
 * framebuffer (feckless_graphics_handler's test shim), over a folder made
 * in /tmp, as chooser_<name>.ppm: Jinxed Writer's landscape page at text
 * size 3 in its Dark theme, newest first, and Avoidant's portrait menu
 * with its "Boot app2" row, a run each. To look at; choosertest is
 * the check.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>

#include "gfx.h"
#include "storage.h"
#include "feckless_chooser.h"

extern void *shim_last_big_alloc;

bool storage_join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    const int n = snprintf(out, out_len, "%s/%s", dir, name);
    return n > 0 && (size_t)n < out_len;
}
bool storage_is_hidden(const char *name) { return name[0] == '.'; }
bool storage_present(storage_id_t id) { return id == STORAGE_SD; }
const char *storage_mount_path(storage_id_t id) { return id == STORAGE_SD ? "/sd" : "/usb"; }
const char *storage_label(storage_id_t id) { return id == STORAGE_SD ? "microSD" : "USB"; }
uint32_t storage_generation(void) { return 1; }

static char root[64];

static void mk(const char *name, size_t bytes, time_t t)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, name);
    if (strchr(name, '.') == NULL) { mkdir(p, 0755); return; }
    FILE *f = fopen(p, "w");
    if (!f) exit(1);
    for (size_t i = 0; i < bytes; i++) fputc('x', f);
    fclose(f);
    struct utimbuf u = { t, t };
    utime(p, &u);
}

static void save(const char *name, int w, int h)
{
    const uint16_t *fb = shim_last_big_alloc;
    char fn[64];
    snprintf(fn, sizeof fn, "chooser_%s.ppm", name);
    FILE *f = fopen(fn, "wb");
    if (!f) exit(1);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int p = 0; p < w * h; p++) {
        const uint16_t v = fb[p];
        const unsigned char rgb[3] = { (unsigned char)(((v >> 11) & 31) * 255 / 31),
                                       (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                       (unsigned char)((v & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", fn);
}

static int notes(const char *dir, const storage_ent_t *e, void *ctx)
{
    (void)dir; (void)ctx;
    const size_t n = strlen(e->name);
    return e->is_dir || (n > 3 && !strcmp(e->name + n - 3, ".md")) ? 0 : STORAGE_LIST_SKIP;
}

static int apps(const char *dir, const storage_ent_t *e, void *ctx)
{
    (void)dir; (void)ctx;
    const size_t n = strlen(e->name);
    if (e->is_dir) return 0;
    if (n > 4 && !strcmp(e->name + n - 4, ".elf")) return 1;
    return n > 4 && !strcmp(e->name + n - 4, ".bin") ? 0 : STORAGE_LIST_SKIP;
}

static void boot_row(storage_list_t *l, const char *dir, void *ctx)
{
    (void)dir; (void)ctx;
    storage_list_add(l, "app2", "Boot app2: Defeatist v0.9.1", false, -1, 0, true, 2);
}

static uint16_t app_colour(const storage_ent_t *e, void *ctx)
{
    (void)ctx;
    if (e->pinned) return RGB(235, 235, 235);
    return e->kind == 1 ? RGB(120, 210, 140) : 0;
}

static chooser_t c;

int main(int argc, char **argv)
{
    /* one screen a run: gfx_init() sizes the framebuffer once */
    const bool jinxed = argc < 2 || strcmp(argv[1], "avoidant") != 0;
    snprintf(root, sizeof root, "/tmp/chooserpreview.XXXXXX");
    if (!mkdtemp(root)) return 1;
    const time_t t0 = 1762300000 + 365 * 86400;
    static const char *days[] = { "2026-10-28.md", "2026-10-30.md", "2026-11-02.md",
                                  "2026-11-03.md", "2026-11-04.md", "2026-11-05.md",
                                  "2026-11-06.md", "2026-11-01.conflict.md", "drafts", "trash" };
    for (int i = 0; i < 10; i++) mk(days[i], (size_t)(900 + i * 2300), t0 + i * 86400 + i * 1234);
    static const char *bins[] = { "defeatist-v0.9.1.bin", "errant-moving-map.bin", "lothesome.elf",
                                  "spurious-v0.3.0.bin", "jinxed-writer.bin", "old" };
    char sub[96];
    snprintf(sub, sizeof sub, "%s/firmware", root);
    mkdir(sub, 0755);
    char save_root[64];
    snprintf(save_root, sizeof save_root, "%s", root);
    snprintf(root, sizeof root, "%s", sub);
    for (int i = 0; i < 6; i++) mk(bins[i], (size_t)(1500000 + i * 400000), t0);
    snprintf(root, sizeof root, "%s", save_root);

    if (jinxed) {
    /* Jinxed Writer: 1280x720, Dark, size 3 */
    const int W = 1280, H = 720;
    if (gfx_init(NULL, W, H) != ESP_OK) return 1;
    gfx_fill_rect(0, 0, W, H, RGB(18, 18, 18));
    c = (chooser_t){ .x = 0, .y = 0, .w = W, .h = H, .row_h = 3 * 12 + 24, .scale = 3,
                     .bg = RGB(18, 18, 18), .alt = RGB(18, 18, 18), .sel = RGB(44, 44, 52),
                     .text = RGB(170, 170, 170), .dim = RGB(110, 110, 110),
                     .folder = RGB(140, 160, 190), .rule = RGB(30, 30, 30),
                     .root = root, .sort = STORAGE_SORT_NEWEST, .filter = notes,
                     .show_path = true, .show_size = true, .show_time = true };
    chooser_begin(&c, NULL);
    chooser_key(&c, CHOOSER_KEY_DOWN);
    chooser_key(&c, CHOOSER_KEY_DOWN);
    chooser_key(&c, CHOOSER_KEY_DOWN);
    chooser_draw(&c);
    save("jinxed", W, H);
    chooser_done(&c);
    } else {
    /* Avoidant: 720x1280 portrait, its menu's colours, its row */
    const int PW = 720, PH = 1280;
    if (gfx_init(NULL, PW, PH) != ESP_OK) return 1;
    c = (chooser_t){ .x = 0, .y = 0, .w = PW, .h = PH, .row_h = 88, .scale = 3,
                     .bg = RGB(16, 18, 24), .alt = RGB(20, 22, 30), .sel = RGB(28, 52, 36),
                     .text = RGB(235, 235, 235), .dim = RGB(130, 130, 140),
                     .folder = RGB(120, 170, 255), .rule = RGB(40, 44, 56),
                     .root = NULL, .sort = STORAGE_SORT_NAME, .filter = apps,
                     .decorate = boot_row, .colour = app_colour,
                     .show_path = true, .show_size = true };
    chooser_begin(&c, sub);
    chooser_draw(&c);
    save("avoidant", PW, PH);
    chooser_done(&c);
    }

    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    return system(cmd) == 0 ? 0 : 1;
}
