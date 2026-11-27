// The fonts of the menu (fonts_gen.h, fonts.c): f27 all the text (the size of the ROM font 27 of the
// FT812), f30 the clock (digits and signs only), f20 the small labels of the Gamepad page.
#pragma once
#include "font.h"
extern const font_t f20, f27, f30, f32;   // f32: the big title lines (a few characters, tools/mkfonts.py)
