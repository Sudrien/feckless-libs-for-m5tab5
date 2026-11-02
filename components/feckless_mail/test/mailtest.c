/* SPDX-License-Identifier: MIT
 *
 * Host test for feckless_mail: mailcfg's checks and its NVS round trip
 * against fake_nvs.c; and mailform driven by taps and keys through the real
 * gfx.c and feckless_keyboard, at Avoidant's portrait size with the
 * on-screen keyboard and Jinxed Writer's landscape one without, every
 * pixel of the band tapped once on each so nothing it draws is out of
 * bounds (ASan watches the framebuffer).
 */
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "mailcfg.h"
#include "mailform.h"

extern void *shim_last_big_alloc;
extern int fake_nvs_writes;
const unsigned char *fake_nvs_blob(size_t *len);

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const mailcfg_t k_good = {
    .host = "smtp.example.com", .port = 0, .security = MAILCFG_TLS,
    .user = "me@example.com", .pass = "abcd efgh ijkl mnop", .from = "", .to = "notes@example.org",
};

static void cfg_tests(void)
{
    const char *why;
    mailcfg_t c = k_good;
    CHECK(mailcfg_check(&c, &why) && why == NULL);
    CHECK(mailcfg_port(&c) == 465);
    c.security = MAILCFG_STARTTLS; CHECK(mailcfg_port(&c) == 587);
    c.port = 2525; CHECK(mailcfg_port(&c) == 2525);
    CHECK(!strcmp(mailcfg_from(&c), "me@example.com"));

    struct { const char *field, *val, *starts; } bad[] = {
        { "host", "", "Server" }, { "host", "smtp example.com", "Server" }, { "host", "localhost", "Server" },
        { "user", "", "Login" }, { "pass", "", "Password" },
        { "to", "", "To" }, { "to", "notes", "To" }, { "to", "a@b", "To" }, { "to", "a@@b.c", "To" },
        { "to", "<a@b.c>", "To" }, { "to", "a@b.c, d@e.f", "To" },
        { "to", "a@b.c\r\nRCPT TO:<x@y.z>", "A field" }, { "pass", "pw\n", "A field" },
        { "user", "me", "From" }, { "from", "nobody", "From" },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        c = k_good;
        char *f = !strcmp(bad[i].field, "host") ? c.host : !strcmp(bad[i].field, "user") ? c.user :
                  !strcmp(bad[i].field, "pass") ? c.pass : !strcmp(bad[i].field, "from") ? c.from : c.to;
        strcpy(f, bad[i].val);
        CHECK(!mailcfg_check(&c, &why) && why && !strncmp(why, bad[i].starts, strlen(bad[i].starts)));
    }
    c = k_good; strcpy(c.user, "me"); strcpy(c.from, "me@example.com");
    CHECK(mailcfg_check(&c, NULL));      /* a login that is not an address, with From */
    c = k_good; c.security = 7; CHECK(!mailcfg_check(&c, NULL));

    /* NVS */
    mailcfg_t out;
    CHECK(!mailcfg_load(NULL, &out) && !out.host[0]);
    c = k_good; c.to[0] = '\0';
    CHECK(mailcfg_save(NULL, &c) == ESP_ERR_INVALID_ARG && fake_nvs_writes == 0);
    c = k_good;
    CHECK(mailcfg_save(NULL, &c) == ESP_OK);
    CHECK(mailcfg_load(NULL, &out) && !memcmp(&out, &k_good, sizeof out));
    CHECK(mailcfg_load("feckless_mail", &out));
    CHECK(!mailcfg_load("other", &out));
    /* no stale bytes after a terminator in what is stored */
    size_t len;
    const unsigned char *b = fake_nvs_blob(&len);
    const char *host = (const char *)b + offsetof(mailcfg_t, host) + 1;   /* after the version byte */
    (void)host;
    int nonzero_tail = 0;
    const mailcfg_t *sc = (const mailcfg_t *)(b + ((size_t)&((struct { uint8_t v; mailcfg_t c; } *)0)->c));
    for (size_t i = strlen(sc->pass) + 1; i < sizeof sc->pass; i++) nonzero_tail += sc->pass[i] != 0;
    CHECK(nonzero_tail == 0);
    CHECK(mailcfg_clear(NULL) == ESP_OK && !mailcfg_load(NULL, &out));
    CHECK(mailcfg_clear(NULL) == ESP_OK);    /* none is fine */

    char d[32];
    mailcfg_domain("notes@example.org", d, sizeof d); CHECK(!strcmp(d, "@example.org"));
    mailcfg_domain("", d, sizeof d); CHECK(!strcmp(d, ""));
    mailcfg_domain(NULL, d, sizeof d); CHECK(!strcmp(d, ""));
    mailcfg_wipe(&c);
    for (size_t i = 0; i < sizeof c; i++) CHECK(((unsigned char *)&c)[i] == 0);
}

#define C_BG    RGB(16, 18, 24)
static mailform_t form(int x, int y, int w, int row_h, bool osk)
{
    return (mailform_t){ .x = x, .y = y, .w = w, .row_h = row_h, .scale = 3, .osk = osk,
        .bg = C_BG, .field = RGB(26, 29, 38), .key = RGB(52, 58, 76), .text = RGB(235, 235, 235),
        .dim = RGB(140, 145, 160), .edge = RGB(44, 48, 60), .go = RGB(40, 130, 70),
        .bad = RGB(220, 90, 80), .on = RGB(120, 96, 40) };
}

static void dump(const char *name, int W, int H)
{
    char fn[64];
    snprintf(fn, sizeof fn, "mail_%s.ppm", name);
    FILE *f = fopen(fn, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    const uint16_t *fb = shim_last_big_alloc;
    for (int p = 0; p < W * H; p++) {
        uint16_t v = fb[p];
        unsigned char rgb[3] = { (unsigned char)(((v >> 11) & 31) * 255 / 31),
                                 (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                 (unsigned char)((v & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void keys(mailform_t *f, const char *s)
{
    for (; *s; s++) mailform_key(f, *s);
}

static void form_tests(bool preview)
{
    /* portrait, Avoidant: 720 x 1280, the band under a 140-px header */
    mailform_t f = form(0, 160, 720, 72, true);
    CHECK(160 + mailform_h(&f) <= 1280);
    mailform_begin(&f, NULL);
    CHECK(f.focus == MAILFORM_HOST);
    mailform_draw(&f);
    if (preview) dump("portrait_empty", 720, 1280);

    /* typing by keys, as a USB keyboard would */
    keys(&f, "smtp.example.com");
    CHECK(!strcmp(f.c.host, "smtp.example.com"));
    CHECK(mailform_key(&f, ' ') == MAILFORM_NONE);          /* no space in a host */
    CHECK(mailform_key(&f, MAILFORM_KEY_NEXT) == MAILFORM_REDRAW && f.focus == MAILFORM_PORT);
    keys(&f, "5x8a7"); CHECK(!strcmp(f.port, "587"));        /* digits only */
    keys(&f, "999"); CHECK(!strcmp(f.port, "58799"));        /* five fit */
    CHECK(mailform_key(&f, '1') == MAILFORM_NONE);
    mailform_key(&f, MAILFORM_KEY_NEXT); CHECK(f.focus == MAILFORM_USER);   /* Security skipped */
    mailform_key(&f, MAILFORM_KEY_PREV); CHECK(f.focus == MAILFORM_PORT);
    for (int i = 0; i < 5; i++) mailform_key(&f, MAILFORM_KEY_DEL);
    keys(&f, "70000");
    CHECK(mailform_key(&f, MAILFORM_KEY_ENTER) == MAILFORM_REDRAW && f.note && f.focus == MAILFORM_PORT);
    for (int i = 0; i < 5; i++) mailform_key(&f, MAILFORM_KEY_DEL);
    CHECK(f.port[0] == '\0' && mailform_key(&f, MAILFORM_KEY_DEL) == MAILFORM_NONE);

    /* taps: a field, Security, Show */
    const int RH = 72, pitch = RH + RH / 8;
    CHECK(mailform_tap(&f, 400, 160 + MAILFORM_USER * pitch + RH / 2) == MAILFORM_REDRAW && f.focus == MAILFORM_USER);
    keys(&f, "me@example.com");
    CHECK(mailform_tap(&f, 700, 160 + MAILFORM_SECURITY * pitch + RH / 2) == MAILFORM_REDRAW &&
          f.c.security == MAILCFG_STARTTLS);
    CHECK(mailform_tap(&f, 700, 160 + MAILFORM_SECURITY * pitch + RH / 2) == MAILFORM_NONE);
    mailform_tap(&f, 400, 160 + MAILFORM_PASS * pitch + RH / 2);
    keys(&f, "abcd efgh ijkl mnop");
    CHECK(!strcmp(f.c.pass, "abcd efgh ijkl mnop"));        /* spaces in a password */
    CHECK(mailform_tap(&f, 700, 160 + MAILFORM_PASS * pitch + RH / 2) == MAILFORM_REDRAW && f.reveal);

    /* Save with To missing: the note, and To focused */
    const int by = 160 + MAILFORM_ROWS * pitch + (2 * (GFX_GLYPH_H(2) + 4) + RH / 8);
    CHECK(mailform_tap(&f, 600, by + RH / 2) == MAILFORM_REDRAW && f.note && f.focus == MAILFORM_TO);
    mailform_draw(&f);
    if (preview) dump("portrait_note", 720, 1280);

    /* the on-screen keyboard: "n" on the z-row, then space bar ignored for To */
    const int ky = by + pitch, kw = 720 / 11;
    int nx = -1;
    for (int x = 0; x < 720 && nx < 0; x++) { kbd_t k = f.kb; if (kbd_hit(&k, x, ky + 3 * RH + RH / 2) == 'n') nx = x; }
    (void)kw;
    CHECK(nx >= 0 && f.kb.y == ky);
    CHECK(mailform_tap(&f, nx + 4, ky + 3 * RH + RH / 2) == MAILFORM_REDRAW);
    CHECK(!strcmp(f.c.to, "n"));
    mailform_tap(&f, 360, ky + 4 * RH + RH / 8 + RH / 2);
    CHECK(!strcmp(f.c.to, "n"));
    mailform_key(&f, MAILFORM_KEY_DEL);
    keys(&f, "notes@example.org");
    CHECK(mailform_tap(&f, 100, by + RH / 2) == MAILFORM_CANCEL);
    CHECK(mailform_key(&f, MAILFORM_KEY_ENTER) == MAILFORM_SAVE && f.note == NULL);
    CHECK(f.c.port == 0 && f.c.security == MAILCFG_STARTTLS);
    mailform_draw(&f);
    if (preview) dump("portrait_filled", 720, 1280);

    /* every pixel of the band, from a copy each time */
    mailform_t g = f;
    for (int y = f.y - 5; y < f.y + mailform_h(&f) + 5; y++)
        for (int x = -5; x < 725; x++) {
            g = f;
            mailform_tap(&g, x, y);
            CHECK(strlen(g.c.to) < sizeof g.c.to && strlen(g.port) < sizeof g.port);
        }
    mailform_draw(&g);

    /* a full field takes no more */
    mailform_tap(&f, 400, 160 + MAILFORM_TO * pitch + RH / 2);
    for (int i = 0; i < 200; i++) mailform_key(&f, 'a');
    CHECK(strlen(f.c.to) == MAILCFG_ADDR_MAX);
    mailform_draw(&f);
    mailform_done(&f);
    CHECK(f.c.pass[0] == 0 && f.c.to[0] == 0);

    /* loaded: focus on the first empty one of those needed */
    mailcfg_t half = k_good; half.pass[0] = '\0';
    mailform_begin(&f, &half);
    CHECK(f.focus == MAILFORM_PASS);
    half.port = 2525; mailform_begin(&f, &half); CHECK(!strcmp(f.port, "2525"));
}

static void landscape(bool preview)
{
    /* landscape, Jinxed Writer: 1280 x 720, no on-screen keyboard */
    gfx_set_rotation(1);
    if (gfx_w() != 1280) { printf("note: no landscape in this gfx; skipped\n"); gfx_set_rotation(0); return; }
    mailform_t f = form(40, 70, 1200, 60, false);
    CHECK(70 + mailform_h(&f) <= 720);
    mailcfg_t c = k_good; c.to[0] = '\0';
    mailform_begin(&f, &c);
    gfx_fill_rect(0, 0, 1280, 720, C_BG);
    gfx_draw_text(40, 20, "Email", 3, 600, RGB(235, 235, 235));
    mailform_key(&f, MAILFORM_KEY_ENTER);
    mailform_draw(&f);
    for (int y = 0; y < 720; y++)
        for (int x = 0; x < 1280; x++) { mailform_t g = f; mailform_tap(&g, x, y); }
    if (preview) {
        /* the shadow framebuffer is laid out as drawn, 1280 x 720 */
        dump("landscape", 1280, 720);
    }
    mailform_done(&f);
    gfx_set_rotation(0);
}

int main(int argc, char **argv)
{
    const bool preview = argc > 1 && !strcmp(argv[1], "preview");
    if (gfx_init(NULL, 720, 1280) != ESP_OK) return 1;
    cfg_tests();
    form_tests(preview);
    landscape(preview);
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("mailtest: all passed\n");
    return 0;
}
