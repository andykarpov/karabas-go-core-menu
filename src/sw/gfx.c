// Drawing of the menu into the frame buffer (gfx.h).
#include "gfx.h"
#include "mmio.h"

static volatile uint8_t *fb = FB0;
static int bank = 0;             // the frame buffer drawn into = the palette bank its colours go to

// the palette of each buffer: entries handed out as they are asked for, a colour (or a ramp) once
#define CACHE_MAX 40
typedef struct { uint32_t fg; int32_t surface; uint8_t first; } cached_t;   // surface: GFX_ALPHA, GFX_SOLID or a colour
static struct { int next; int n; cached_t c[CACHE_MAX]; } pal[2];

#ifdef GFX_DEBUG_PUTS
void GFX_DEBUG_PUTS(const char *s);
#endif

void gfx_init(void) { gfx_target(0); gfx_reset_palette(); gfx_target(1); gfx_reset_palette(); gfx_target(0); }
void gfx_target(int buffer) { bank = buffer ? 1 : 0; fb = bank ? FB1 : FB0; }
int gfx_target_buffer(void) { return bank; }
void gfx_reset_palette(void) { pal[bank].next = 1; pal[bank].n = 0; R_PAL(bank, 0) = 0; }

static uint8_t new_entry(uint32_t argb) {
  if (pal[bank].next > 255) {
#ifdef GFX_DEBUG_PUTS
    GFX_DEBUG_PUTS("gfx: the palette is full\n");
#endif
    return 255;
  }
  R_PAL(bank, pal[bank].next) = argb;
  return (uint8_t) pal[bank].next++;
}

static uint32_t mix(uint32_t fg, uint32_t bg, uint32_t k) {  // k / 15 of fg
  uint32_t out = 0;
  for (int s = 0; s < 24; s += 8) {
    uint32_t f = (fg >> s) & 255, b = (bg >> s) & 255;
    out |= ((f * k + b * (15 - k) + 7) / 15) << s;
  }
  return out;
}

static uint8_t lookup(uint32_t fg, int32_t surface) {
  fg &= 0xFFFFFF;
  for (int i = 0; i < pal[bank].n; i++)
    if (pal[bank].c[i].fg == fg && pal[bank].c[i].surface == surface) return pal[bank].c[i].first;
  uint8_t first = (uint8_t) pal[bank].next;
  if (surface == GFX_SOLID) new_entry(0xF0000000u | fg);
  else for (uint32_t k = 1; k <= 15; k++)
    new_entry(surface == GFX_ALPHA ? (k << 28) | fg : 0xF0000000u | mix(fg, (uint32_t) surface, k));
  if (pal[bank].n < CACHE_MAX) pal[bank].c[pal[bank].n++] = (cached_t) { fg, surface, first };
  return first;
}

uint8_t gfx_color(uint32_t rgb) { return lookup(rgb, GFX_SOLID); }
ramp_t gfx_ramp_alpha(uint32_t rgb) { return lookup(rgb, GFX_ALPHA); }
ramp_t gfx_ramp_over(uint32_t fg, uint32_t bg) { return lookup(fg, (int32_t) (bg & 0xFFFFFF)); }
ramp_t gfx_ramp(uint32_t fg, int32_t surface) { return surface == GFX_ALPHA ? gfx_ramp_alpha(fg) : gfx_ramp_over(fg, (uint32_t) surface); }

// a rectangle left as it is (gfx_keep): a redraw around a button does not blink its text
static int keep_on = 0, keep_x0, keep_y0, keep_x1, keep_y1;
void gfx_keep(int x, int y, int w, int h) { keep_on = 1; keep_x0 = x; keep_y0 = y; keep_x1 = x + w; keep_y1 = y + h; }
void gfx_keep_off(void) { keep_on = 0; }

static inline void pset(int x, int y, uint8_t c) {
  if (keep_on && x >= keep_x0 && x < keep_x1 && y >= keep_y0 && y < keep_y1) return;
  if ((unsigned) x < FB_W && (unsigned) y < FB_H) fb[y * FB_W + x] = c;
}

static void span(volatile uint8_t *p, int w, uint8_t c) {
  uint32_t c4 = c * 0x01010101u;
  int i = 0;
  for (; i < w && ((uintptr_t) (p + i) & 3); i++) p[i] = c;
  for (; i + 4 <= w; i += 4) *(volatile uint32_t *) (p + i) = c4;
  for (; i < w; i++) p[i] = c;
}

void gfx_fill(int x, int y, int w, int h, uint8_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > FB_W) w = FB_W - x;
  if (y + h > FB_H) h = FB_H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    volatile uint8_t *p = fb + (y + j) * FB_W + x;
    int yy = y + j;
    if (keep_on && yy >= keep_y0 && yy < keep_y1 && x < keep_x1 && x + w > keep_x0) {
      int a = keep_x0 - x, b = x + w - keep_x1;     // the parts left and right of the kept one
      if (a > 0) span(p, a, c);
      if (b > 0) span(p + (keep_x1 - x), b, c);
    } else span(p, w, c);
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

void gfx_ring(int cx, int cy, int r, ramp_t ramp) {
  for (int y = cy - r - 1; y <= cy + r; y++)
    for (int x = cx - r - 1; x <= cx + r; x++) {
      int c = disc_cov(x * 4, y * 4, cx * 4, cy * 4, r * 4) - disc_cov(x * 4, y * 4, cx * 4, cy * 4, r * 4 - 8);
      if (c > 0) pset(x, y, (uint8_t) (ramp + c - 1));
    }
}

// sin of 0..90 degrees in steps of 6, x 256
static const uint16_t sin6[16] = { 0, 27, 53, 79, 104, 128, 150, 171, 190, 207, 222, 234, 243, 250, 255, 256 };
static int s90(int a) {   // sin of 0..90 degrees, x 256
  int i = a / 6, f = a % 6;
  return i >= 15 ? 256 : sin6[i] + (sin6[i + 1] - sin6[i]) * f / 6;
}
static int isin(int deg) {
  deg %= 360; if (deg < 0) deg += 360;
  int q = deg / 90, d = deg % 90;
  return q == 0 ? s90(d) : q == 1 ? s90(90 - d) : q == 2 ? -s90(d) : -s90(90 - d);
}

void gfx_hand(int cx, int cy, int len, int deg, ramp_t ramp) {
  int sx = isin(deg), sy = -isin(deg + 90);   // x right, y down
  for (int t = 0; t <= len * 2; t++) {
    int x = cx + sx * t / 512, y = cy + sy * t / 512;
    gfx_fill(x, y, 2, 2, RAMP_FULL(ramp));
  }
}

// ---- the sprite (the mouse pointer) ----
#define SPR_MAX_W 16
#define SPR_MAX_H 20
static uint8_t under[SPR_MAX_W * SPR_MAX_H];
static int spr_x, spr_y, spr_w, spr_h, spr_on = 0;
static const char *const *spr_art;

void gfx_sprite_forget(void) { spr_on = 0; }

void gfx_sprite_hide(void) {
  if (!spr_on) return;
  for (int j = 0; j < spr_h; j++)
    for (int i = 0; i < spr_w; i++)
      if (spr_art[j][i] != ' ') pset(spr_x + i, spr_y + j, under[j * SPR_MAX_W + i]);
  spr_on = 0;
}

void gfx_sprite_show(int x, int y, const char *const *art, int w, int h, uint8_t c_dark, uint8_t c_light) {
  gfx_sprite_hide();
  spr_x = x; spr_y = y; spr_w = w; spr_h = h; spr_art = art;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      char c = art[j][i];
      if (c == ' ') continue;
      int px = x + i, py = y + j;
      under[j * SPR_MAX_W + i] = ((unsigned) px < FB_W && (unsigned) py < FB_H) ? fb[py * FB_W + px] : 0;
      pset(px, py, c == 'X' ? c_dark : c_light);
    }
  spr_on = 1;
}

// ---- shapes of the widgets ----

void gfx_pill(int x0, int x1, int cy, int r, ramp_t ramp) {
  gfx_round_rect(x0 - r, cy - r, x1 - x0 + 2 * r, 2 * r, r, ramp);
}

void gfx_solid_rect(int x, int y, int w, int h, uint8_t idx) {
  // a solid colour (one entry) with the corner pixels cut: the swatches of the colours
  gfx_fill(x + 1, y, w - 2, 1, idx);
  gfx_fill(x, y + 1, w, h - 2, idx);
  gfx_fill(x + 1, y + h - 1, w - 2, 1, idx);
}

void gfx_text_right(const font_t *f, int rx, int cy, const char *s, ramp_t ramp) {
  gfx_text(f, rx - gfx_text_width(f, s), cy - f->line / 2, s, ramp);
}

void gfx_text_left(const font_t *f, int x, int cy, const char *s, ramp_t ramp) {
  gfx_text(f, x, cy - f->line / 2, s, ramp);
}

void gfx_arc_top(int cx, int cy, int r, ramp_t ramp) {
  for (int y = cy - r - 1; y <= cy; y++)
    for (int x = cx - r - 1; x <= cx + r; x++) {
      int c = disc_cov(x * 4, y * 4, cx * 4, cy * 4, r * 4) - disc_cov(x * 4, y * 4, cx * 4, cy * 4, r * 4 - 8);
      if (c > 0) pset(x, y, (uint8_t) (ramp + c - 1));
    }
}
