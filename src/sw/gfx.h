// Drawing of the menu into the frame buffer (Ф2): colour ramps in the palette (an alpha ramp over
// the hardware background, or a colour blended over a solid surface), anti-aliased rounded
// rectangles, circles, text and the pictures of the FT812 menu.
#pragma once
#include <stdint.h>
#include "font.h"

// a ramp: 15 palette entries, coverage 1..15 (0 = the surface stays)
typedef uint8_t ramp_t;          // its first entry
#define RAMP_FULL(r) ((uint8_t) ((r) + 14))

// Each frame buffer has its own palette (a bank of 256 entries, menu_video): the colours are asked
// for by value as they are drawn and get entries of the palette of the buffer drawn into, once.
void gfx_init(void);
void gfx_target(int buffer);     // 0 / 1: the frame buffer drawn into (and its palette)
int gfx_target_buffer(void);
void gfx_reset_palette(void);    // all entries of the palette of the target free again (a new picture)
#define GFX_ALPHA (-1)           // the surface of a ramp: the background of menu_compose
#define GFX_SOLID (-2)
// fg over a surface: GFX_ALPHA (alpha 1..15 over the background) or an opaque colour (blended entries)
ramp_t gfx_ramp(uint32_t fg, int32_t surface);
ramp_t gfx_ramp_alpha(uint32_t rgb);
ramp_t gfx_ramp_over(uint32_t fg, uint32_t bg);
uint8_t gfx_color(uint32_t rgb); // one opaque entry

void gfx_keep(int x, int y, int w, int h);                  // nothing drawn inside it until gfx_keep_off
void gfx_keep_off(void);
void gfx_clear(int x, int y, int w, int h);                 // transparent: the background shows
void gfx_fill(int x, int y, int w, int h, uint8_t idx);
void gfx_round_rect(int x, int y, int w, int h, int r, ramp_t ramp);
void gfx_circle(int cx, int cy, int r, ramp_t ramp);
void gfx_ring(int cx, int cy, int r, ramp_t ramp);          // a ring 2 px wide inside radius r
void gfx_arc_top(int cx, int cy, int r, ramp_t ramp);         // the upper half of gfx_ring
void gfx_hand(int cx, int cy, int len, int deg, ramp_t ramp); // a line 2 px wide from the centre, deg clockwise from up
// a sprite with the pixels under it kept: art rows of ' ' (none), 'X' (c_dark), '.' (c_light)
void gfx_sprite_show(int x, int y, const char *const *art, int w, int h, uint8_t c_dark, uint8_t c_light);
void gfx_sprite_hide(void);
void gfx_sprite_forget(void);    // the buffer of the sprite went away (a new picture): nothing to restore
// text: surface 0 = over the background (ramp = an alpha ramp), else a ramp blended over it
int gfx_text_width(const font_t *f, const char *s);
void gfx_text(const font_t *f, int x, int y, const char *s, ramp_t ramp);  // y: the top of the line
void gfx_text_center(const font_t *f, int cx, int cy, const char *s, ramp_t ramp);
void gfx_text_left(const font_t *f, int x, int cy, const char *s, ramp_t ramp);    // cy: the middle
void gfx_text_right(const font_t *f, int rx, int cy, const char *s, ramp_t ramp);  // ends at rx
void gfx_pill(int x0, int x1, int cy, int r, ramp_t ramp);  // a bar with round ends from x0 to x1 (centres)
void gfx_solid_rect(int x, int y, int w, int h, uint8_t idx);  // one entry, the corner pixels cut
void gfx_coverage(int x, int y, int w, int h, const uint8_t *cov4, ramp_t ramp);  // 4-bit coverage, 2 px a byte
void gfx_indexed(int x, int y, int w, int h, int scale, const uint8_t *px, const uint8_t *map); // 0 = transparent
