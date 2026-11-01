/* SPDX-License-Identifier: MIT
 *
 * feckless_libversions (8012): Avoidant's settings page's three columns,
 * made one copy. See the header.
 */
#include "feckless_libversions.h"

#include "gfx.h"

static int row_h(const libv_view_t *v)
{
    return v->row_h > 0 ? v->row_h : GFX_GLYPH_H(v->scale) + 10;
}

int libv_rows(const libv_view_t *v)
{
    const int rh = row_h(v);
    return v->h >= rh ? v->h / rh : 0;
}

/* "and N more", without snprintf. */
static void more_text(char *buf, int n)
{
    static const char head[] = "and ";
    char digits[12];
    int nd = 0, o = 0;
    do { digits[nd++] = (char)('0' + n % 10); n /= 10; } while (n > 0 && nd < 11);
    for (int i = 0; head[i]; i++) buf[o++] = head[i];
    while (nd > 0) buf[o++] = digits[--nd];
    for (const char *s = " more"; *s; s++) buf[o++] = *s;
    buf[o] = '\0';
}

int libv_draw(const libv_view_t *v, const libv_t *list, int n, int first)
{
    if (!list || first < 0 || first >= n) return 0;
    const int rh = row_h(v), rows = libv_rows(v), left = n - first;
    const int cx1 = v->x, cx2 = v->x + v->w * 45 / 100, cx3 = v->x + v->w * 70 / 100;
    const int shown = left <= rows ? left : (rows > 0 ? rows - 1 : 0);
    int y = v->y;
    for (int i = 0; i < shown; i++, y += rh) {
        const libv_t *e = &list[first + i];
        gfx_draw_text(cx1, y, e->module ? e->module : "", v->scale, cx2 - cx1 - 12, v->text);
        gfx_draw_text(cx2, y, e->checkout ? e->checkout : "", v->scale, cx3 - cx2 - 12, v->dim);
        gfx_draw_text(cx3, y, e->licence ? e->licence : "", v->scale, v->x + v->w - cx3, v->dim);
    }
    if (shown < left && rows > 0) {
        char more[24];
        more_text(more, left - shown);
        gfx_draw_text(cx1, y, more, v->scale, v->w, v->dim);
    }
    return shown;
}
