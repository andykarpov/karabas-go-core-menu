// Drawing of the menu into the frame buffer (gfx.h).
#include "gfx.h"
#include "mmio.h"

static volatile uint8_t *fb = FB0;
static int next_entry = 1;       // 0 is transparent

void gfx_init(void) { gfx_reset_palette(); fb = FB0; }
void gfx_target(int buffer) { fb = buffer ? FB1 : FB0; }
void gfx_reset_palette(void) { next_entry = 1; R_PAL(0) = 0; }

static uint8_t new_entry(uint32_t argb) {
  if (next_entry > 255) return 255;
  R_PAL(next_entry) = argb;
  return (uint8_t) next_entry++;
}

uint8_t gfx_color(uint32_t rgb) { return new_entry(0xF0000000u | (rgb & 0xFFFFFF)); }

ramp_t gfx_ramp_alpha(uint32_t rgb) {
  ramp_t r = (ramp_t) next_entry;
  for (uint32_t k = 1; k <= 15; k++) new_entry((k << 28) | (rgb & 0xFFFFFF));
  return r;
}

static uint32_t mix(uint32_t fg, uint32_t bg, uint32_t k) {  // k / 15 of fg
  uint32_t out = 0;
  for (int s = 0; s < 24; s += 8) {
    uint32_t f = (fg >> s) & 255, b = (bg >> s) & 255;
    out |= ((f * k + b * (15 - k) + 7) / 15) << s;
  }
  return out;
}

ramp_t gfx_ramp_over(uint32_t fg, uint32_t bg) {
  ramp_t r = (ramp_t) next_entry;
  for (uint32_t k = 1; k <= 15; k++) new_entry(0xF0000000u | mix(fg, bg, k));
  return r;
}

static inline void pset(int x, int y, uint8_t c) {
  if ((unsigned) x < FB_W && (unsigned) y < FB_H) fb[y * FB_W + x] = c;
}

void gfx_fill(int x, int y, int w, int h, uint8_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > FB_W) w = FB_W - x;
  if (y + h > FB_H) h = FB_H - y;
  if (w <= 0 || h <= 0) return;
  uint32_t c4 = c * 0x01010101u;
  for (int j = 0; j < h; j++) {
    volatile uint8_t *p = fb + (y + j) * FB_W + x;
    int i = 0;
    for (; i < w && ((uintptr_t) (p + i) & 3); i++) p[i] = c;
    for (; i + 4 <= w; i += 4) *(volatile uint32_t *) (p + i) = c4;
    for (; i < w; i++) p[i] = c;
  }
}

void gfx_clear(int x, int y, int w, int h) { gfx_fill(x, y, w, h, 0); }

// the coverage (0..15) of pixel (px, py) by the disc of radius r around (cx, cy), all in 1/4 pixel
// units: 4 x 4 samples
static int disc_cov(int px4, int py4, int cx4, int cy4, int r4) {
  int n = 0;
  int rr = r4 * r4;
  for (int sy = 0; sy < 4; sy++)
    for (int sx = 0; sx < 4; sx++) {
      int dx = px4 + sx * 1 + 0 - cx4, dy = py4 + sy - cy4;
      // the sample at its centre: + 0.5 of a sample
      dx = 2 * dx + 1; dy = 2 * dy + 1;
      if (dx * dx + dy * dy <= 4 * rr) n++;
    }
  return n >= 16 ? 15 : n;
}

void gfx_round_rect(int x, int y, int w, int h, int r, ramp_t ramp) {
  uint8_t full = RAMP_FULL(ramp);
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  gfx_fill(x, y + r, w, h - 2 * r, full);
  gfx_fill(x + r, y, w - 2 * r, r, full);
  gfx_fill(x + r, y + h - r, w - 2 * r, r, full);
  // the corners: the disc of radius r around the inner corner points
  for (int j = 0; j < r; j++)
    for (int i = 0; i < r; i++) {
      const int cx[4] = { x + r, x + w - r, x + r, x + w - r }, cy[4] = { y + r, y + r, y + h - r, y + h - r };
      const int px[4] = { x + i, x + w - r + i, x + i, x + w - r + i }, py[4] = { y + j, y + j, y + h - r + j, y + h - r + j };
      for (int k = 0; k < 4; k++) {
        int c = disc_cov(px[k] * 4, py[k] * 4, cx[k] * 4, cy[k] * 4, r * 4);
        if (c) pset(px[k], py[k], (uint8_t) (ramp + c - 1));
      }
    }
}

void gfx_circle(int cx, int cy, int r, ramp_t ramp) {
  for (int y = cy - r - 1; y <= cy + r; y++)
    for (int x = cx - r - 1; x <= cx + r; x++) {
      int c = disc_cov(x * 4, y * 4, cx * 4, cy * 4, r * 4);
      if (c) pset(x, y, (uint8_t) (ramp + c - 1));
    }
}

// ---- text ----

static uint32_t utf8_next(const char **s) {
  const uint8_t *p = (const uint8_t *) *s;
  uint32_t c = *p++;
  if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
  else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
    c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
    p += 2;
  }
  *s = (const char *) p;
  return c;
}

static const font_glyph_t *glyph(const font_t *f, uint32_t cp) {
  int lo = 0, hi = f->n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (f->g[m].cp == cp) return &f->g[m];
    if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
  }
  return cp != '?' ? glyph(f, '?') : 0;
}

int gfx_text_width(const font_t *f, const char *s) {
  int w = 0;
  while (*s) { const font_glyph_t *g = glyph(f, utf8_next(&s)); if (g) w += g->adv; }
  return w;
}

void gfx_coverage(int x, int y, int w, int h, const uint8_t *cov4, ramp_t ramp) {
  int stride = (w + 1) / 2;
  for (int j = 0; j < h; j++) {
    const uint8_t *row = cov4 + j * stride;
    for (int i = 0; i < w; i++) {
      int c = (row[i >> 1] >> ((i & 1) * 4)) & 15;
      if (c) pset(x + i, y + j, (uint8_t) (ramp + c - 1));
    }
  }
}

void gfx_text(const font_t *f, int x, int y, const char *s, ramp_t ramp) {
  while (*s) {
    const font_glyph_t *g = glyph(f, utf8_next(&s));
    if (!g) continue;
    if (g->w && g->h) gfx_coverage(x + g->x, y + g->y, g->w, g->h, f->bits + g->off, ramp);
    x += g->adv;
  }
}

void gfx_text_center(const font_t *f, int cx, int cy, const char *s, ramp_t ramp) {
  // the middle of the capital letters at cy (as FT81x OPT_CENTER puts the middle of the cell)
  gfx_text(f, cx - gfx_text_width(f, s) / 2, cy - f->line / 2, s, ramp);
}

void gfx_indexed(int x, int y, int w, int h, int scale, const uint8_t *px, const uint8_t *map) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint8_t v = px[j * w + i];
      if (v) gfx_fill(x + i * scale, y + j * scale, scale, scale, map[v]);
    }
}
