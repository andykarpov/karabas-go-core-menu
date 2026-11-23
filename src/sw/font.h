// Anti-aliased fonts of the menu (tools/mkfonts.py): 4 bits of coverage a pixel.
#pragma once
#include <stdint.h>

typedef struct {
  uint16_t off;     // the first byte of the bitmap
  uint8_t w, h;     // the bitmap
  int8_t x, y;      // its place: from the pen x, from the top of the line
  uint8_t adv;      // the pen moves on by
  uint8_t none;     // 1: not in the font ('?' is drawn)
} font_glyph_t;
_Static_assert(sizeof(font_glyph_t) == 8, "a glyph is 8 bytes");

typedef struct {
  uint16_t first, n;   // code points first .. first + n - 1
  uint16_t g;          // the glyph of first
} font_range_t;

typedef struct {
  const font_glyph_t *g;
  const uint8_t *bits;
  const font_range_t *r;
  uint8_t nr;       // ranges, sorted by first
  uint8_t line;     // the height of a line
  uint8_t ascent;
} font_t;
