#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "gfx.h"
#include "font5x7.h"

static uint16_t *s_fb[GFX_BANDS];   /* horizontal bands, top to bottom */

/* Base of row y within its band buffer (y must be in range). */
static inline uint16_t *gfx_row(int y)
{
    return s_fb[y / GFX_BAND_H] + (y % GFX_BAND_H) * GFX_W;
}

bool gfx_init(void)
{
    /* DMA-capable so esp_lcd can push each band directly */
    for (int b = 0; b < GFX_BANDS; b++) {
        s_fb[b] = heap_caps_malloc(GFX_W * GFX_BAND_H * sizeof(uint16_t),
                                   MALLOC_CAP_DMA);
        if (!s_fb[b]) return false;
    }
    return true;
}

uint16_t *gfx_band(int b) { return s_fb[b]; }

void gfx_clear(uint16_t c)
{
    for (int b = 0; b < GFX_BANDS; b++) {
        if (((c >> 8) & 0xFF) == (c & 0xFF)) {
            memset(s_fb[b], c & 0xFF, GFX_W * GFX_BAND_H * 2);
        } else {
            for (int i = 0; i < GFX_W * GFX_BAND_H; i++) s_fb[b][i] = c;
        }
    }
}

void gfx_pixel(int x, int y, uint16_t c)
{
    if ((unsigned)x < GFX_W && (unsigned)y < GFX_H) gfx_row(y)[x] = c;
}

void gfx_hline(int x0, int x1, int y, uint16_t c)
{
    if ((unsigned)y >= GFX_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    uint16_t *p = gfx_row(y) + x0;
    for (int x = x0; x <= x1; x++) *p++ = c;
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_circle(int cx, int cy, int r, uint16_t c)
{
    int x = -r, y = 0, err = 2 - 2 * r;
    do {
        gfx_pixel(cx - x, cy + y, c);
        gfx_pixel(cx - y, cy - x, c);
        gfx_pixel(cx + x, cy - y, c);
        gfx_pixel(cx + y, cy + x, c);
        int e2 = err;
        if (e2 <= y) { err += ++y * 2 + 1; if (-x == y && e2 <= x) e2 = 0; }
        if (e2 > x) err += ++x * 2 + 1;
    } while (x < 0);
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= r * r) half++;
        gfx_hline(cx - half, cx + half, cy + dy, c);
    }
}

static void swap_pt(int *ax, int *ay, int *bx, int *by)
{
    int t = *ax; *ax = *bx; *bx = t;
    t = *ay; *ay = *by; *by = t;
}

void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    /* sort by y */
    if (y0 > y1) swap_pt(&x0, &y0, &x1, &y1);
    if (y1 > y2) swap_pt(&x1, &y1, &x2, &y2);
    if (y0 > y1) swap_pt(&x0, &y0, &x1, &y1);

    if (y0 == y2) { /* degenerate: single line */
        int a = x0, b = x0;
        if (x1 < a) a = x1;
        if (x1 > b) b = x1;
        if (x2 < a) a = x2;
        if (x2 > b) b = x2;
        gfx_hline(a, b, y0, c);
        return;
    }
    for (int y = y0; y <= y2; y++) {
        /* long edge x at this scanline */
        int xa = x0 + (int)((int64_t)(x2 - x0) * (y - y0) / (y2 - y0));
        int xb;
        if (y < y1 || y1 == y2) {
            xb = (y1 == y0) ? x1
                 : x0 + (int)((int64_t)(x1 - x0) * (y - y0) / (y1 - y0));
        } else {
            xb = (y2 == y1) ? x1
                 : x1 + (int)((int64_t)(x2 - x1) * (y - y1) / (y2 - y1));
        }
        gfx_hline(xa, xb, y, c);
    }
}

void gfx_text(int x, int y, const char *s, uint16_t c, int scale)
{
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const uint8_t *g = font5x7[ch - 0x20];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    if (scale == 1) {
                        gfx_pixel(x + col, y + row, c);
                    } else {
                        for (int dy = 0; dy < scale; dy++)
                            gfx_hline(x + col * scale,
                                      x + col * scale + scale - 1,
                                      y + row * scale + dy, c);
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

int gfx_text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? (n * 6 - 1) * scale : 0;
}

void gfx_text_center(int cx, int y, const char *s, uint16_t c, int scale)
{
    gfx_text(cx - gfx_text_width(s, scale) / 2, y, s, c, scale);
}
