/* Tiny software renderer over a full-screen RGB565 framebuffer.
 * Pixels are stored byte-swapped so the buffer can be DMA'd to the
 * panel as-is (the GC9A01 wants big-endian RGB565). */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GFX_W 240
#define GFX_H 240

/* The framebuffer is split into two horizontal bands. A single 240x240x2
 * (112.5 KiB) buffer cannot be allocated on the ESP32-C3: its DMA heap is
 * fragmented into ~112 KiB regions ("RAM" + "Retention RAM"), so the largest
 * contiguous DMA block is just under what one full frame needs. Two
 * 240x120x2 (56.25 KiB) bands fit easily and are flushed in two transfers. */
#define GFX_BANDS 2
#define GFX_BAND_H (GFX_H / GFX_BANDS)   /* 120 rows per band */

/* Build a byte-swapped RGB565 color from 8-bit components. */
static inline uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

bool     gfx_init(void);           /* allocates the framebuffer (DMA RAM)  */
uint16_t *gfx_band(int b);         /* band b's buffer (rows b*GFX_BAND_H..) */

void gfx_clear(uint16_t c);
void gfx_pixel(int x, int y, uint16_t c);
void gfx_hline(int x0, int x1, int y, uint16_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
void gfx_circle(int cx, int cy, int r, uint16_t c);
void gfx_fill_circle(int cx, int cy, int r, uint16_t c);
void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

/* 5x7 bitmap font, `scale` integer multiplier, 1px spacing per glyph. */
void gfx_text(int x, int y, const char *s, uint16_t c, int scale);
int  gfx_text_width(const char *s, int scale);
void gfx_text_center(int cx, int y, const char *s, uint16_t c, int scale);
