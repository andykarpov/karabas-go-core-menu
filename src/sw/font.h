// Anti-aliased fonts of the menu (tools/mkfonts.py): 4 bits of coverage a pixel.
#pragma once
#include <stdint.h>

typedef struct {
  uint16_t cp;      // the code point
  uint32_t off;     // the first byte of the bitmap
  uint8_t w, h;     // the bitmap
  int8_t x, y;      // its place: from the pen x, from the top of the line
  uint8_t adv;      // the pen moves on by
} font_glyph_t;

typedef struct {
  const font_glyph_t *g;
  const uint8_t *bits;
  uint16_t n;       // glyphs, sorted by cp
  uint8_t line;     // the height of a line
  uint8_t ascent;
} font_t;
