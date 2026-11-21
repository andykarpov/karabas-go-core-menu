// Drawing of the menu into the frame buffer (Ф2): colour ramps in the palette (an alpha ramp over
// the hardware background, or a colour blended over a solid surface), anti-aliased rounded
// rectangles, circles, text and the pictures of the FT812 menu.
#pragma once
#include <stdint.h>
#include "font.h"

// a ramp: 15 palette entries, coverage 1..15 (0 = the surface stays)
typedef uint8_t ramp_t;          // its first entry
#define RAMP_FULL(r) ((uint8_t) ((r) + 14))

void gfx_init(void);
void gfx_target(int buffer);     // 0 / 1: the frame buffer drawn into
// a colour over the background: alpha 1..15
ramp_t gfx_ramp_alpha(uint32_t rgb);
// fg blended over a solid colour (opaque entries)
ramp_t gfx_ramp_over(uint32_t fg, uint32_t bg);
uint8_t gfx_color(uint32_t rgb); // one opaque entry
void gfx_reset_palette(void);    // all entries free again (the colours changed)

void gfx_clear(int x, int y, int w, int h);                 // transparent: the background shows
void gfx_fill(int x, int y, int w, int h, uint8_t idx);
void gfx_round_rect(int x, int y, int w, int h, int r, ramp_t ramp);
void gfx_circle(int cx, int cy, int r, ramp_t ramp);
void gfx_ring(int cx, int cy, int r, ramp_t ramp);          // a ring 2 px wide inside radius r
void gfx_hand(int cx, int cy, int len, int deg, ramp_t ramp); // a line 2 px wide from the centre, deg clockwise from up
// a sprite with the pixels under it kept: art rows of ' ' (none), 'X' (c_dark), '.' (c_light)
void gfx_sprite_show(int x, int y, const char *const *art, int w, int h, uint8_t c_dark, uint8_t c_light);
void gfx_sprite_hide(void);
// text: surface 0 = over the background (ramp = an alpha ramp), else a ramp blended over it
int gfx_text_width(const font_t *f, const char *s);
void gfx_text(const font_t *f, int x, int y, const char *s, ramp_t ramp);  // y: the top of the line
void gfx_text_center(const font_t *f, int cx, int cy, const char *s, ramp_t ramp);
void gfx_coverage(int x, int y, int w, int h, const uint8_t *cov4, ramp_t ramp);  // 4-bit coverage, 2 px a byte
void gfx_indexed(int x, int y, int w, int h, int scale, const uint8_t *px, const uint8_t *map); // 0 = transparent
