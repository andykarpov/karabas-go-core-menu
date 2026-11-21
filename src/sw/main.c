// Ф0 of the self-drawing menu (docs/PLAN.md): the CPU draws a test screen into a frame buffer,
// the background glow of the FT812 menu, a frame and text, then a live counter and the keyboard.
#include <stdint.h>
#include "mmio.h"
#include "font8.h"

static void dbg_puts(const char *s) { while (*s) R_DBG = (uint8_t) *s++; }

// the palette: 0..127 the glow (black -> 0x0000C8), 200 the frame, 255 white
static void palette(void) {
  for (int i = 0; i < 128; i++) R_PAL(i) = (uint32_t) (i * 200 / 127);
  R_PAL(200) = 0x3060ff;
  R_PAL(255) = 0xffffff;
}

static uint8_t *fb;

static void fill(int x, int y, int w, int h, uint8_t c) {
  for (int j = 0; j < h; j++) {
    uint8_t *p = fb + (y + j) * FB_W + x;
    for (int i = 0; i < w; i++) p[i] = c;
  }
}

static void glyph(int x, int y, char ch, uint8_t c, int scale) {
  if (ch < 32 || ch > 126) ch = '?';
  const unsigned char *g = font8[ch - 32];
  for (int r = 0; r < 8; r++)
    for (int b = 0; b < 8; b++)
      if (g[r] & (0x80 >> b)) fill(x + b * scale, y + r * scale, scale, scale, c);
}

static void text(int x, int y, const char *s, uint8_t c, int scale) {
  for (; *s; s++, x += 8 * scale) glyph(x, y, *s, c, scale);
}

static void hex(char *out, uint32_t v, int n) {
  for (int i = n - 1; i >= 0; i--, v >>= 4) out[i] = "0123456789ABCDEF"[v & 15];
  out[n] = 0;
}

int main(void) {
  dbg_puts("menu: start\n");
  palette();
  fb = (uint8_t *) FB0;
  // the glow: from the top to 3/4 of the height, then black
  for (int y = 0; y < FB_H; y++) {
    int c = y < 450 ? 127 - y * 127 / 450 : 0;
    fill(0, y, FB_W, 1, (uint8_t) c);
  }
  fill(100, 200, 600, 4, 200);
  fill(100, 396, 600, 4, 200);
  fill(100, 200, 4, 200, 200);
  fill(696, 200, 4, 200, 200);
  text(160, 240, "Karabas boot menu", 255, 3);
  text(160, 300, "Phase 0: VexRiscv draws this", 255, 2);
  R_FB = 0;
  R_SHOW = 1;
  dbg_puts("menu: shown\n");
  char buf[12];
  for (;;) {
    uint32_t f = R_FRAMES;
    while (R_FRAMES == f) ;
    fill(160, 340, 480, 16, 0);
    hex(buf, R_FRAMES, 8);
    text(160, 340, buf, 255, 2);
    hex(buf, R_KBD0, 8);
    text(400, 340, buf, 255, 2);
    if (f == 2) dbg_puts("menu: frame 2\n");
  }
}
