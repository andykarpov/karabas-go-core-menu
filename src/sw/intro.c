// The boot intro (intro.h). Driven by the time (R_MS), not by a frame count: a slow frame is dropped,
// the sound stays with the picture. Double buffered: each frame is drawn into the hidden buffer (the
// rectangles it drew two frames ago cleared first) with its own palette, then shown at a frame start.
// The background gradient (one register pair for both buffers) is written just after the switch.
// The logo: the pixels of the menu logo (images_gen.h) x S, its glow computed here at the start.
// All in fixed point (Q = 1024): the CPU has no FPU.
#include <stdint.h>
#include "intro.h"
#include "mmio.h"
#include "ui.h"
#include "sound.h"
#include "cores.h"
#include "images_gen.h"
#include "rpc.h"

#ifdef HOST
void host_vsync(void);           // host/host.c: the frame shown is written, the time goes on a frame
#endif

#define Q 1024
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int ramp(int t, int t0, int t1) { return t <= t0 ? 0 : t >= t1 ? Q : (t - t0) * Q / (t1 - t0); }
static int ease_in(int u) { return u * u / Q * u / Q; }
static int ease_out(int u) { int v = Q - u; return Q - v * v / Q * v / Q; }
static int ease_io(int u) { return u * u / Q * (3 * Q - 2 * u) / Q; }
static int lerp(int a, int b, int u) { return a + (b - a) * u / Q; }
static uint32_t mixc(uint32_t a, uint32_t b, int u) {   // a -> b
  uint32_t o = 0;
  for (int s = 0; s < 24; s += 8) o |= (uint32_t) lerp((int) ((a >> s) & 255), (int) ((b >> s) & 255), u) << s;
  return o;
}

// ---- the timeline (ms) ----
#define T_BEAM0   500            // the first beam starts (the others 60 ms apart)
#define T_MEET   1400            // the beams meet: the flash
#define T_GLARE0 2250
#define T_GLARE1 2750
#define T_SHRINK0 2900           // the logo goes to the corner, the background comes
#define T_SHRINK1 3500
#define T_END    3600
#define T_HOLD   2000            // black first: a monitor takes 1..2 s to sync to a new signal or mode

// ---- the palette (the same entries in both banks) ----
#define P_GLOW   1               // 5 colour groups x 15 levels
#define P_LOGO   76              // 5: the logo pixels
#define P_GLARE  81              // 5: the logo pixels under the glare
#define P_BEAM   86              // 4 beams x 15 levels
#define P_WHITE  146             // 15 levels: the streak and the flash
static const uint32_t col[6]  = { 0, 0xffffff, 0xff0000, 0xffff00, 0x00ff00, 0x00ffff };  // = logo_pal
static const uint32_t glow[6] = { 0, 0x58b0ff, 0xff3018, 0xffc800, 0x28ff40, 0x28e0ff };

// ---- the logo and its glow, made at the start ----
#define S_MAX 10
#define PAD_MAX (3 * S_MAX)
#define CW_MAX (LOGO_W * S_MAX + 2 * PAD_MAX)
#define CH_MAX (LOGO_H * S_MAX + 2 * PAD_MAX)
static uint8_t comp[CW_MAX * CH_MAX];       // the palette index of each pixel around the logo
static int S, PAD, CW, CH, lx, ly;          // lx, ly: the top left of comp on the screen
static const uint8_t *logo_px;

// The glow is blurred on a coarse grid (2 samples a logo pixel: 124 x 44, in the data cache), then
// stretched to the pixels bilinearly; no divide in the pixel loops (the CPU divides slowly). It took
// ~3 s at full size.
#define GPAD 3                                // logo pixels of glow around the logo
#define GW ((LOGO_W + 2 * GPAD) * 2)
#define GH ((LOGO_H + 2 * GPAD) * 2)
static uint16_t ga[GW * GH], gb[GW * GH];
static uint8_t glev[GW * GH], ggrp[GW * GH];  // the level x 16 (0..240) and the group of each sample
static int16_t tix[CW_MAX], tiy[CH_MAX];      // the sample left / above of a pixel
static uint8_t tfx[CW_MAX], tfy[CH_MAX];      // and the fraction to the next (0..255)
static int16_t tpx[CW_MAX], tpy[CH_MAX];      // the logo pixel of a pixel, -1 outside

static void box(const uint16_t *src, uint16_t *dst, int w, int h, int step, int stride) {
  // a box blur of radius 2 (5 samples: x 13107 >> 16 = / 5) along lines of w samples
  for (int j = 0; j < h; j++) {
    const uint16_t *s = src + j * stride;
    uint16_t *d = dst + j * stride;
    uint32_t sum = 0;
    for (int i = 0; i <= 2 && i < w; i++) sum += s[i * step];
    for (int i = 0; i < w; i++) {
      d[i * step] = (uint16_t) ((sum * 13107u) >> 16);
      if (i + 3 < w) sum += s[(i + 3) * step];
      if (i - 2 >= 0) sum -= s[(i - 2) * step];
    }
  }
}

static void logo_make(void) {
  S = scr_w / 80;                           // 8 in 640 x 480, 10 in 800 x 600
  PAD = GPAD * S;
  CW = LOGO_W * S + 2 * PAD;
  CH = LOGO_H * S + 2 * PAD;
  lx = ((scr_w - CW) / 2) & ~3;              // word aligned rows
  ly = (scr_h - CH) / 2 - scr_h / 24;
  logo_px = hw_id == 1 ? logo_go_px : logo_mini_px;
  // the glow of each colour group on the grid; the brightest group of a sample wins
  for (int i = 0; i < GW * GH; i++) { glev[i] = 0; ggrp[i] = 0; }
  for (int g = 1; g < LOGO_COLORS; g++) {
    int any = 0;
    for (int j = 0; j < GH; j++)
      for (int i = 0; i < GW; i++) {
        int px = i / 2 - GPAD, py = j / 2 - GPAD, v = 0;
        if (px >= 0 && py >= 0 && px < LOGO_W && py < LOGO_H && logo_px[py * LOGO_W + px] == g) v = 255;
        ga[j * GW + i] = (uint16_t) v;
        any |= v;
      }
    if (!any) continue;
    for (int k = 0; k < 3; k++) { box(ga, gb, GW, GH, 1, GW); box(gb, ga, GH, GW, GW, 1); }
    for (int i = 0; i < GW * GH; i++) {
      int l = (int) ((ga[i] * 22u * 16u * 257u) >> 16);   // v * 22 / 255 levels, x 16
      if (l > 240) l = 240;
      if (l > glev[i]) { glev[i] = (uint8_t) l; ggrp[i] = (uint8_t) g; }
    }
  }
  // the tables of the pixel columns and rows (the only divides)
  for (int x = 0; x < CW; x++) {
    int u = (2 * x + 1) * 256 / S - 128;           // the sample position x 256 of the pixel centre
    if (u < 0) u = 0;
    tix[x] = (int16_t) (u >> 8); tfx[x] = (uint8_t) (u & 255);
    if (tix[x] >= GW - 1) { tix[x] = GW - 2; tfx[x] = 255; }
    tpx[x] = (int16_t) (x >= PAD && x < PAD + LOGO_W * S ? (x - PAD) / S : -1);
  }
  for (int y = 0; y < CH; y++) {
    int u = (2 * y + 1) * 256 / S - 128;
    if (u < 0) u = 0;
    tiy[y] = (int16_t) (u >> 8); tfy[y] = (uint8_t) (u & 255);
    if (tiy[y] >= GH - 1) { tiy[y] = GH - 2; tfy[y] = 255; }
    tpy[y] = (int16_t) (y >= PAD && y < PAD + LOGO_H * S ? (y - PAD) / S : -1);
  }
  for (int y = 0; y < CH; y++) {
    uint8_t *c = comp + y * CW;
    int j = tiy[y], fy = tfy[y], py = tpy[y];
    const uint8_t *l0 = glev + j * GW, *l1 = l0 + GW, *g0 = ggrp + j * GW, *g1 = g0 + GW;
    for (int x = 0; x < CW; x++) {
      int px = tpx[x];
      if (px >= 0 && py >= 0 && logo_px[py * LOGO_W + px]) { c[x] = (uint8_t) (P_LOGO + logo_px[py * LOGO_W + px] - 1); continue; }
      int i = tix[x], fx = tfx[x];
      int top = l0[i] * (256 - fx) + l0[i + 1] * fx, bot = l1[i] * (256 - fx) + l1[i + 1] * fx;
      int l = (top * (256 - fy) + bot * fy) >> 20;          // / 65536 / 16: the level 0..15
      if (l <= 0) { c[x] = 0; continue; }
      // the group of the brightest of the four samples
      int g = g0[i], m = l0[i];
      if (l0[i + 1] > m) { m = l0[i + 1]; g = g0[i + 1]; }
      if (l1[i] > m) { m = l1[i]; g = g1[i]; }
      if (l1[i + 1] > m) { g = g1[i + 1]; }
      c[x] = (uint8_t) (P_GLOW + (g - 1) * 15 + l - 1);
    }
  }
}

// ---- the drawn rectangles of each buffer (cleared two frames later) ----
typedef struct { int x, y, w, h; } rect_t;
static rect_t dirty[2][8];
static int ndirty[2];
static void mark(int b, int x, int y, int w, int h) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > scr_w) w = scr_w - x;
  if (y + h > scr_h) h = scr_h - y;
  if (w <= 0 || h <= 0) return;
  if (ndirty[b] == 8) { dirty[b][7] = (rect_t) { 0, 0, scr_w, scr_h }; return; }
  dirty[b][ndirty[b]++] = (rect_t) { x, y, w, h };
}

static volatile uint8_t *fbp;
static int logo_out;                          // the logo pixels are shown: no light drawn over them
static inline int level_of(uint8_t c) {       // the level of a light pixel; the logo is never overdrawn
  if (c >= P_BEAM && c < P_WHITE + 15) return (c - P_BEAM) % 15 + 1;
  if (c >= P_LOGO && c < P_BEAM) return logo_out ? 99 : 0;
  if (c >= P_GLOW && c < P_LOGO) return logo_out ? (c - P_GLOW) % 15 + 1 : 0;
  return 0;
}
static inline void put_max(int x, int y, int base, int lev) {   // a beam / white pixel, the brighter one stays
  if ((unsigned) x >= (unsigned) scr_w || (unsigned) y >= (unsigned) scr_h || lev <= 0) return;
  if (lev > 15) lev = 15;
  volatile uint8_t *p = fbp + y * FB_STRIDE + x;
  if (level_of(*p) < lev) *p = (uint8_t) (base + lev - 1);
}
static void disc(int b, int cx, int cy, int r, int lev, int base) {   // a soft disc, lev at the centre
  int rr = r * r + 1;
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++) {
      int d2 = dx * dx + dy * dy;
      if (d2 > r * r) continue;
      int f = (rr - d2) * Q / rr;             // (1 - d^2 / r^2)^2: a bright core, a soft edge
      put_max(cx + dx, cy + dy, base, lev * f / Q * f / Q);
    }
  mark(b, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

// a horizontal streak through (cx, cy): half length hl, brightness i (Q)
static void streak(int b, int cx, int cy, int hl, int i) {
  static const int row_k[4] = { 16, 10, 5, 2 };
  if (hl < 1 || i <= 0) return;
  for (int dy = -3; dy <= 3; dy++) {
    int k = row_k[dy < 0 ? -dy : dy];
    for (int x = cx - hl; x <= cx + hl; x++) {
      int f = Q - (x > cx ? x - cx : cx - x) * Q / hl;      // 1 in the middle, 0 at the ends
      put_max(x, cy + dy, P_WHITE, 15 * i / Q * f / Q * k / 16 + (f > Q / 2 && dy == 0 ? 1 : 0));
    }
  }
  mark(b, cx - hl, cy - 3, 2 * hl + 1, 7);
}

// ---- the beams: from the sides to the meeting point, each with a trail of its past positions ----
#define NT 60
#define DT 3
static int bx0[4], by0[4], mx, my;
static void beam_pos(int i, int t, int *x, int *y) {
  int u = ramp(t, T_BEAM0 + 60 * i, T_MEET);
  *x = lerp(bx0[i], mx, ease_in(u));
  *y = lerp(by0[i], my, ease_io(u));
}
static void beams(int b, int t) {
  for (int i = 0; i < 4; i++) {
    int t0 = T_BEAM0 + 60 * i;
    if (t < t0 || t >= T_MEET) continue;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (int k = NT - 1; k >= 0; k--) {
      int ts = t - k * DT;
      if (ts < t0) continue;
      int x, y, r = k == 0 ? 5 : k < 12 ? 3 : 2, lev = 15 * (NT - k) / NT + 2;
      beam_pos(i, ts, &x, &y);
      int rr = r * r + 1;
      for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
          int d2 = dx * dx + dy * dy;
          if (d2 <= r * r) put_max(x + dx, y + dy, P_BEAM + 15 * i, lev - lev * d2 / rr);
        }
      if (x - r < x0) x0 = x - r;
      if (y - r < y0) y0 = y - r;
      if (x + r > x1) x1 = x + r;
      if (y + r > y1) y1 = y + r;
    }
    if (x1 >= x0) mark(b, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  }
}

// ---- the logo: comp with a wipe from the middle and the glare band ----
static void logo_draw(int b, int t) {
  int wipe = ease_out(ramp(t, T_MEET, T_MEET + 450)) * (CW / 2 + 2) / Q;   // half width shown
  if (wipe <= 0) return;
  int gpos = -1 << 30, gw = 6 * S;
  if (t >= T_GLARE0 && t < T_GLARE1) gpos = lerp(-gw - CH / 2, CW + gw, ease_io(ramp(t, T_GLARE0, T_GLARE1)));
  static uint8_t line[CW_MAX] __attribute__((aligned(4)));
  for (int y = 0; y < CH; y++) {
    const uint8_t *s = comp + y * CW;
    int g0 = gpos - (CH - y) / 2;            // the band leans to the right
    for (int x = 0; x < CW; x++) {
      int d = x - CW / 2;
      uint8_t c = (d < -wipe || d >= wipe) ? 0 : s[x];
      if (x >= g0 && x < g0 + gw) {            // the glare: the logo whiter, its glow brighter to the middle
        int e = x - g0, m = gw / 2, k = 9 - 9 * (e > m ? e - m : m - e) / m;
        if (c >= P_LOGO && c < P_GLARE) c = (uint8_t) (c + 5);
        else if (c >= P_GLOW && c < P_GLOW + 15 && k > 0) {   // the white group only
          int g = (c - P_GLOW) / 15, l = (c - P_GLOW) % 15 + 1 + k;
          c = (uint8_t) (P_GLOW + g * 15 + (l > 15 ? 15 : l) - 1);
        }
      }
      line[x] = c;
    }
    volatile uint32_t *p = (volatile uint32_t *) (fbp + (ly + y) * FB_STRIDE + lx);
    const uint32_t *l = (const uint32_t *) line;
    for (int k = 0; k < CW / 4; k++) p[k] = l[k];
    for (int x = CW & ~3; x < CW; x++) fbp[(ly + y) * FB_STRIDE + lx + x] = line[x];
  }
  mark(b, lx, ly, CW, CH);
}

// the logo pixels at a scale of s / Q per pixel with the top left at (x, y): the shrink into the corner
static void logo_scaled(int b, int x, int y, int s) {
  int w = LOGO_W * s / Q, h = LOGO_H * s / Q;
  for (int j = 0; j < h; j++) {
    int py = j * Q / s;
    if (py >= LOGO_H || (unsigned) (y + j) >= (unsigned) scr_h) continue;
    volatile uint8_t *p = fbp + (y + j) * FB_STRIDE + x;
    for (int i = 0; i < w; i++) {
      int px = i * Q / s;
      uint8_t v = px < LOGO_W ? logo_px[py * LOGO_W + px] : 0;
      if (v && (unsigned) (x + i) < (unsigned) scr_w) p[i] = (uint8_t) (P_LOGO + v - 1);
    }
  }
  mark(b, x, y, w + 1, h + 1);
}

// ---- the palette of a frame ----
static void palette(int b, int t) {
  int gi = ramp(t, T_MEET + 40, T_MEET + 200);                         // the glow
  if (t > 2500) gi = lerp(Q, Q * 2 / 5, ramp(t, 2500, T_SHRINK0));
  if (t > T_SHRINK0) gi = lerp(Q * 2 / 5, 0, ramp(t, T_SHRINK0, T_SHRINK0 + 250));
  int la = ramp(t, T_MEET + 40, T_MEET + 100);   // opaque at once, white hot                    // the logo comes out of the flash
  int sharp = ease_io(ramp(t, T_MEET + 200, T_MEET + 900));       // white hot -> its colours
  for (int g = 1; g <= 5; g++) {
    for (int l = 1; l <= 15; l++) R_PAL(b, P_GLOW + (g - 1) * 15 + l - 1) = ((uint32_t) (l * gi / Q) << 28) | glow[g];
    uint32_t c = mixc(0xffffff, col[g], sharp);
    R_PAL(b, P_LOGO + g - 1) = ((uint32_t) (15 * la / Q) << 28) | c;
    R_PAL(b, P_GLARE + g - 1) = ((uint32_t) (15 * la / Q) << 28) | mixc(c, 0xffffff, Q * 3 / 4);
  }
  for (int l = 1; l <= 15; l++) {
    for (int i = 0; i < 4; i++) R_PAL(b, P_BEAM + 15 * i + l - 1) = ((uint32_t) l << 28) | mixc(col[i + 2], 0xffffff, l * Q / 15 / 2);
    R_PAL(b, P_WHITE + l - 1) = ((uint32_t) l << 28) | 0xe0f4ff;
  }
}

// the background: black, the flash, then the gradient of the look
static void background(int t) {
  uint32_t top = 0, bot = 0;
  if (t >= T_MEET && t < T_MEET + 220) {
    int v = Q - ramp(t, T_MEET, T_MEET + 220);
    v = v * v / Q * v / Q;
    top = bot = mixc(0, 0xb0d8ff, v / 2);
  }
  if (t >= T_SHRINK0) {
    int u = ease_io(ramp(t, T_SHRINK0, T_SHRINK1));
    top = mixc(0, look.gradient, u);
    bot = mixc(0, look.bg, u);
  }
  R_GRAD_TOP = top;
  R_GRAD_BOT = bot;
}

// ---- the sound: GM on channels 9 (drums) .. 15, a table by the time and two volume ramps ----
enum { C_DR = 9, C_PAD = 10, C_SW = 11, C_FX = 12, C_TAIKO = 13, C_TIMP = 14, C_HIT = 15 };
typedef struct { uint16_t at; uint8_t st, d1, d2; } ev_t;
#define ON(c) (0x90 | (c))
#define OFF(c) (0x80 | (c))
#define PRG(c) (0xC0 | (c))
static const ev_t score[] = {
  { 0, PRG(C_PAD), 95, 0 }, { 0, PRG(C_SW), 119, 0 }, { 0, PRG(C_FX), 98, 0 },
  { 0, PRG(C_TAIKO), 116, 0 }, { 0, PRG(C_TIMP), 47, 0 }, { 0, PRG(C_HIT), 55, 0 },
  { 0, 0xB0 | C_PAD, 11, 30 },
  { 0, ON(C_PAD), 48, 90 }, { 0, ON(C_PAD), 55, 80 }, { 0, ON(C_PAD), 60, 70 },     // the sweep pad swells
  { 250, ON(C_SW), 60, 110 },                                                        // the reverse cymbal
  { 500, ON(C_FX), 79, 70 }, { 560, ON(C_FX), 84, 70 }, { 620, ON(C_FX), 88, 70 }, { 680, ON(C_FX), 91, 70 },
  { 900, OFF(C_FX), 79, 0 }, { 960, OFF(C_FX), 84, 0 }, { 1020, OFF(C_FX), 88, 0 }, { 1080, OFF(C_FX), 91, 0 },
  // the meeting: the hit
  { T_MEET, 0x99, 35, 127 }, { T_MEET, 0x99, 49, 120 }, { T_MEET, ON(C_TAIKO), 36, 127 }, { T_MEET, ON(C_HIT), 48, 110 },
  { T_MEET, ON(C_HIT), 60, 110 }, { T_MEET, ON(C_TIMP), 36, 127 },
  { T_MEET, OFF(C_PAD), 48, 0 }, { T_MEET, OFF(C_PAD), 55, 0 }, { T_MEET, OFF(C_PAD), 60, 0 }, { T_MEET, OFF(C_SW), 60, 0 },
  { T_MEET + 20, PRG(C_SW), 89, 0 }, { T_MEET + 20, 0xB0 | C_SW, 11, 100 },          // the warm pad: C maj9
  { T_MEET + 40, ON(C_SW), 48, 80 }, { T_MEET + 40, ON(C_SW), 55, 75 }, { T_MEET + 40, ON(C_SW), 64, 70 },
  { T_MEET + 40, ON(C_SW), 71, 65 }, { T_MEET + 40, ON(C_SW), 74, 60 },
  { T_MEET + 400, OFF(C_TAIKO), 36, 0 }, { T_MEET + 400, OFF(C_TIMP), 36, 0 }, { T_MEET + 400, OFF(C_HIT), 48, 0 },
  { T_MEET + 400, OFF(C_HIT), 60, 0 }, { T_MEET + 400, 0x89, 35, 0 }, { T_MEET + 400, 0x89, 49, 0 },
  // the glare: a sparkle
  { T_GLARE0 + 50, ON(C_FX), 96, 60 }, { T_GLARE0 + 110, ON(C_FX), 100, 60 }, { T_GLARE0 + 170, ON(C_FX), 103, 60 },
  { T_GLARE0 + 230, ON(C_FX), 108, 60 },
  { T_GLARE0 + 450, OFF(C_FX), 96, 0 }, { T_GLARE0 + 450, OFF(C_FX), 100, 0 }, { T_GLARE0 + 450, OFF(C_FX), 103, 0 },
  { T_GLARE0 + 450, OFF(C_FX), 108, 0 },
  { T_END, OFF(C_SW), 48, 0 }, { T_END, OFF(C_SW), 55, 0 }, { T_END, OFF(C_SW), 64, 0 }, { T_END, OFF(C_SW), 71, 0 },
  { T_END, OFF(C_SW), 74, 0 },
};
#define SCORE_N ((int) (sizeof(score) / sizeof(score[0])))
static int sc_i, cc_pad, cc_sw;
static void send3(uint8_t a, uint8_t b, uint8_t c) {
  uint8_t m[3] = { a, b, c };
  midi_send(m, (a & 0xE0) == 0xC0 ? 2 : 3);
}
static void sound_poll(int t) {
  // at most 5 messages a frame: the fifo of menu_midi_mux holds 16 bytes (1 ms a byte), a wait would drop a frame
  for (int k = 0; k < 5 && sc_i < SCORE_N && t >= score[sc_i].at; k++, sc_i++) send3(score[sc_i].st, score[sc_i].d1, score[sc_i].d2);
  if (t < T_MEET) {                                   // the swell of the sweep pad
    int v = lerp(30, 120, ramp(t, 0, T_MEET));
    if (v - cc_pad >= 4) { cc_pad = v; send3(0xB0 | C_PAD, 11, (uint8_t) v); }
  }
  if (t > 2700) {                                     // the warm pad fades out
    int v = lerp(100, 0, ramp(t, 2700, T_END));
    if (cc_sw - v >= 4) { cc_sw = v; send3(0xB0 | C_SW, 11, (uint8_t) v); }
  }
}
static void sound_end(void) {                         // every channel used as it was: no notes, piano
  for (int c = C_DR; c <= C_HIT; c++) {
    send3((uint8_t) (0xB0 | c), 123, 0);              // all notes off
    send3((uint8_t) (0xB0 | c), 121, 0);              // the controllers reset (expression, pitch bend)
    if (c != C_DR) send3((uint8_t) (PRG(c)), 0, 0);
  }
}

// ---- the frames ----
static int cut_short(void) {
  key_ev_t e;
  if (key_next(&e)) return 1;
  return (mouse_next() & (M_LEFT | M_RIGHT)) != 0;
}

int intro_play(void) {
  uint32_t tm = R_US;
  logo_make();
  dbg_printf("intro: glow made in %u us", (unsigned) (R_US - tm));
  mx = lx + CW / 2; my = ly + CH / 2;
  bx0[0] = -40;         by0[0] = scr_h / 6;          // red from the left, high
  bx0[1] = scr_w + 40;  by0[1] = scr_h / 4;          // yellow from the right
  bx0[2] = -40;         by0[2] = scr_h * 3 / 4;      // green from the left, low
  bx0[3] = scr_w + 40;  by0[3] = scr_h * 5 / 6;      // cyan from the right
  ndirty[0] = ndirty[1] = 0;
  sc_i = 0; cc_pad = 0; cc_sw = 100;
  int sound = look.sound, cut = 0;
  // both buffers empty, the background black
  for (int b = 0; b < 2; b++) { gfx_target(b); gfx_reset_palette(); gfx_clear(0, 0, scr_w, scr_h); }
  R_GRAD_TOP = 0; R_GRAD_BOT = 0;
  // the video runs from the start of the core (640 x 480), the switch to 800 x 600 was just made:
  // wait until the monitor shows the picture (a key cuts it short here too)
  uint32_t th = R_MS, hold = video_mode() ? T_HOLD : (th < T_HOLD ? T_HOLD - th : 0);
#ifndef HOST
  while (R_MS - th < hold) if (cut_short()) { cut = 1; break; }
  if (cut) { look_apply(); return 1; }
#else
  (void) hold;
#endif
  uint32_t t0 = R_MS;
  for (;;) {
    int t = (int) (R_MS - t0);
    if (t >= T_END) break;
    if (cut_short()) { cut = 1; break; }
    int b = !(R_FB & 1);
    gfx_target(b);
    fbp = b ? FB1 : FB0;
    int nd = ndirty[b];
    ndirty[b] = 0;
    for (int k = 0; k < nd; k++) gfx_clear(dirty[b][k].x, dirty[b][k].y, dirty[b][k].w, dirty[b][k].h);
    // the streak at the start; the flare of the meeting
    if (t < 700) streak(b, mx, my, ease_out(ramp(t, 0, 450)) * (scr_w / 2 + 20) / Q, Q - ramp(t, 400, 700));
    logo_out = t >= T_MEET + 40;
    beams(b, t);
    if (t < T_SHRINK0) logo_draw(b, t);
    else {
      int u = ease_io(ramp(t, T_SHRINK0, T_SHRINK1));
      logo_scaled(b, lerp(lx + PAD, 8, u), lerp(ly + PAD, 8, u), lerp(S * Q, 4 * Q, u));
    }
    if (t >= T_MEET && t < T_MEET + 500) {
      int u = ramp(t, T_MEET, T_MEET + 500);
      streak(b, mx, my, lerp(40, scr_w / 2 + 40, ease_out(u)), Q - u);
      int v = Q - ramp(t, T_MEET, T_MEET + 260);
      v = v * v / Q;
      if (v > 0) disc(b, mx, my, lerp(56, 20, ease_out(Q - v)), 18 * v / Q, P_WHITE);
    }
    palette(b, t);
    R_FB = (uint32_t) b;
    R_SHOW = 1;
#ifdef HOST
    background(t);                 // the host writes the frame with the gradient at the switch
    host_vsync();
#else
    uint32_t tw = R_MS;
    while ((int) (R_FB & 1) != b && R_MS - tw < 50) ;
    background(t);                 // right after the switch (the gradient is for both buffers)
#endif
    if (sound) sound_poll(t);
  }
  if (sound) sound_end();
  look_apply();
  for (int b = 0; b < 2; b++) { gfx_target(b); gfx_reset_palette(); }
  return cut;
}

// at every start of the menu (user 2026-11-27: a core does not restart the menu by Ctrl+Alt+Backspace);
// [ft812] intro=0 of karabas.ini turns it off (no Setup item; the sims run without it)
int intro_wanted(void) {
  static const char req[] = "0\0ft812\0intro";
  uint8_t out[16];
  uint16_t n;
  if (rpc_call(RPC_CFG, 0, req, sizeof(req), out, sizeof(out), &n, 500) != RPC_OK) return 1;
  return !(n >= 2 && out[0] == 1 && out[1] == '0');
}
