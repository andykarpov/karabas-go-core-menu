// The parts of the boot animations (fx.h). Driven by the time (R_MS), not by a frame count: a slow frame
// is dropped, the sound stays with the picture. Double buffered: each frame is drawn into the hidden
// buffer (the rectangles it drew two frames ago cleared first) with its own palette, then shown at a
// frame start; the background gradient (one register pair for both buffers) is written just after the
// switch. All in fixed point: the CPU has no FPU and divides slowly.
#include "fx.h"
#include "mmio.h"
#include "ui.h"
#include "sound.h"

#ifdef HOST
void host_vsync(void);           // host/host.c: the frame shown is written, the time goes on a frame
#endif

uint32_t fx_mix(uint32_t a, uint32_t b, int u) {
  uint32_t o = 0;
  for (int s = 0; s < 24; s += 8) o |= (uint32_t) lerp((int) ((a >> s) & 255), (int) ((b >> s) & 255), u) << s;
  return o;
}

// ---- the frames ----
volatile uint8_t *fx_fb;
int fx_solid_out;
static int glow_lo, solid_lo, light_lo, light_hi, cur;
void fx_layout(int g, int s, int l, int h) { glow_lo = g; solid_lo = s; light_lo = l; light_hi = h; }

typedef struct { int x, y, w, h; } rect_t;
#define NDIRTY 16
static rect_t dirty[2][NDIRTY];
static int ndirty[2];
int fx_nomark;
void fx_mark(int x, int y, int w, int h) {
  if (fx_nomark) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > scr_w) w = scr_w - x;
  if (y + h > scr_h) h = scr_h - y;
  if (w <= 0 || h <= 0) return;
  if (ndirty[cur] == NDIRTY) { dirty[cur][NDIRTY - 1] = (rect_t) { 0, 0, scr_w, scr_h }; return; }
  dirty[cur][ndirty[cur]++] = (rect_t) { x, y, w, h };
}

void fx_start(void) {
  ndirty[0] = ndirty[1] = 0;
  fx_solid_out = 0;
  fx_nomark = 0;
  for (int b = 0; b < 2; b++) { gfx_target(b); gfx_reset_palette(); gfx_clear(0, 0, scr_w, scr_h); }
  R_GRAD_TOP = 0; R_GRAD_BOT = 0;
}

int fx_cut_short(void) {
  key_ev_t e;
  if (key_next(&e)) return 1;
  return (mouse_next() & (M_LEFT | M_RIGHT)) != 0;
}

int fx_hold(int ms) {
#ifndef HOST
  uint32_t th = R_MS;
  while (R_MS - th < (uint32_t) ms) if (fx_cut_short()) return 1;
#else
  (void) ms;
#endif
  return 0;
}

int fx_hold_monitor(void) {
  // the video runs from the start of the core (640 x 480); a switch to 800 x 600 was just made
  uint32_t th = R_MS;
  return fx_hold(video_mode() ? 2000 : (th < 2000 ? (int) (2000 - th) : 0));
}

int fx_frame(void) {
  int b = !(R_FB & 1);
  cur = b;
  gfx_target(b);
  fx_fb = b ? FB1 : FB0;
  int nd = ndirty[b];
  ndirty[b] = 0;
  for (int k = 0; k < nd; k++) {
    const rect_t *r = &dirty[b][k];
    if (!fx_idle) { gfx_clear(r->x, r->y, r->w, r->h); continue; }
    for (int y = 0; y < r->h; y += 16) {     // in parts, the MIDI of show.c between them
      gfx_clear(r->x, r->y + y, r->w, r->h - y < 16 ? r->h - y : 16);
      fx_idle();
    }
  }
  return b;
}

void (*fx_idle)(void);
void fx_show(int b, void (*background)(int t), int t) {
  R_FB = (uint32_t) b;
  R_SHOW = 1;
#ifdef HOST
  background(t);                 // the host writes the frame with the gradient at the switch
  host_vsync();
#else
  uint32_t tw = R_MS;
  while ((int) (R_FB & 1) != b && R_MS - tw < 50) if (fx_idle) fx_idle();
  background(t);                 // right after the switch (the gradient is for both buffers)
#endif
}

void fx_end(void) {
  look_apply();
  for (int b = 0; b < 2; b++) { gfx_target(b); gfx_reset_palette(); }
}

static inline int level_of(uint8_t c) {       // the level of a light pixel; the pictures are never overdrawn
  if (c >= light_lo && c < light_hi) return (c - light_lo) % 15 + 1;
  if (c >= solid_lo && c < light_lo) return fx_solid_out ? 99 : 0;
  if (c >= glow_lo && c < solid_lo) return fx_solid_out ? (c - glow_lo) % 15 + 1 : 0;
  return 0;
}
void fx_put(int x, int y, int base, int lev) {
  if ((unsigned) x >= (unsigned) scr_w || (unsigned) y >= (unsigned) scr_h || lev <= 0) return;
  if (lev > 15) lev = 15;
  volatile uint8_t *p = fx_fb + y * FB_STRIDE + x;
  if (level_of(*p) < lev) *p = (uint8_t) (base + lev - 1);
}
void fx_disc(int cx, int cy, int r, int lev, int base) {
  int rr = r * r + 1;
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++) {
      int d2 = dx * dx + dy * dy;
      if (d2 > r * r) continue;
      int f = (rr - d2) * Q / rr;             // (1 - d^2 / r^2)^2: a bright core, a soft edge
      fx_put(cx + dx, cy + dy, base, lev * f / Q * f / Q);
    }
  fx_mark(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}
void fx_streak(int cx, int cy, int hl, int i, int base) {
  static const int row_k[4] = { 16, 10, 5, 2 };
  if (hl < 1 || i <= 0) return;
  for (int dy = -3; dy <= 3; dy++) {
    int k = row_k[dy < 0 ? -dy : dy];
    for (int x = cx - hl; x <= cx + hl; x++) {
      int f = Q - (x > cx ? x - cx : cx - x) * Q / hl;      // 1 in the middle, 0 at the ends
      fx_put(x, cy + dy, base, 15 * i / Q * f / Q * k / 16 + (f > Q / 2 && dy == 0 ? 1 : 0));
    }
  }
  fx_mark(cx - hl, cy - 3, 2 * hl + 1, 7);
}

// ---- a picture with its glow ----
// The glow is blurred on a coarse grid (2 samples a picture pixel: in the data cache), then stretched
// to the screen pixels bilinearly; no divide in the pixel loops. At full size it took ~3 s.
#define GPAD 3
#define GW_MAX (62 * 2)
#define GH_MAX (25 * 2)
#define CW_MAX 800
static uint16_t ga[GW_MAX * GH_MAX], gb[GW_MAX * GH_MAX];
static uint8_t glev[GW_MAX * GH_MAX], ggrp[GW_MAX * GH_MAX];   // the level x 16 (0..240), the group
static int16_t tix[CW_MAX], tiy[CW_MAX];      // the sample left / above of a pixel
static uint8_t tfx[CW_MAX], tfy[CW_MAX];      // and the fraction to the next (0..255)
static int16_t tpx[CW_MAX], tpy[CW_MAX];      // the picture pixel of a pixel, -1 outside

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

static void table(int n, int s, int pad, int len, int gn, int16_t *ti, uint8_t *tf, int16_t *tp) {
  for (int x = 0; x < n; x++) {
    int u = (2 * x + 1) * 256 / s - 128;   // the sample position x 256 of the pixel centre
    if (u < 0) u = 0;
    ti[x] = (int16_t) (u >> 8); tf[x] = (uint8_t) (u & 255);
    if (ti[x] >= gn - 1) { ti[x] = (int16_t) (gn - 2); tf[x] = 255; }
    tp[x] = (int16_t) (x >= pad && x < pad + len * s ? (x - pad) / s : -1);
  }
}

void fx_glow(fx_img_t *im, const uint8_t *px, int w, int h, int ngroups, int s, int solid_base, int glow_base) {
  int GW = (w + 2 * GPAD) * 2, GH = (h + 2 * GPAD) * 2;
  im->s = s; im->pad = GPAD * s;
  im->cw = w * s + 2 * im->pad; im->ch = h * s + 2 * im->pad;
  // the glow of each colour group on the grid; the brightest group of a sample wins
  for (int i = 0; i < GW * GH; i++) { glev[i] = 0; ggrp[i] = 0; }
  for (int g = 1; g <= ngroups; g++) {
    int any = 0;
    for (int j = 0; j < GH; j++)
      for (int i = 0; i < GW; i++) {
        int x = i / 2 - GPAD, y = j / 2 - GPAD, v = 0;
        if (x >= 0 && y >= 0 && x < w && y < h && px[y * w + x] == g) v = 255;
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
  table(im->cw, s, im->pad, w, GW, tix, tfx, tpx);
  table(im->ch, s, im->pad, h, GH, tiy, tfy, tpy);
  for (int y = 0; y < im->ch; y++) {
    uint8_t *c = im->comp + y * im->cw;
    int j = tiy[y], fy = tfy[y], py = tpy[y];
    const uint8_t *l0 = glev + j * GW, *l1 = l0 + GW, *g0 = ggrp + j * GW, *g1 = g0 + GW;
    for (int x = 0; x < im->cw; x++) {
      int pxx = tpx[x];
      if (pxx >= 0 && py >= 0 && px[py * w + pxx]) { c[x] = (uint8_t) (solid_base + px[py * w + pxx] - 1); continue; }
      int i = tix[x], fx = tfx[x];
      int top = l0[i] * (256 - fx) + l0[i + 1] * fx, bot = l1[i] * (256 - fx) + l1[i + 1] * fx;
      int l = (top * (256 - fy) + bot * fy) >> 20;          // / 65536 / 16: the level 0..15
      if (l <= 0) { c[x] = 0; continue; }
      int g = g0[i], m = l0[i];                             // the group of the brightest of the four samples
      if (l0[i + 1] > m) { m = l0[i + 1]; g = g0[i + 1]; }
      if (l1[i] > m) { m = l1[i]; g = g1[i]; }
      if (l1[i + 1] > m) { g = g1[i + 1]; }
      c[x] = (uint8_t) (glow_base + (g - 1) * 15 + l - 1);
    }
  }
}

// ---- MIDI ----
static const fx_ev_t *sc;
static int sc_n, sc_i;
void fx_send3(uint8_t a, uint8_t b, uint8_t c) {
  uint8_t m[3] = { a, b, c };
  midi_send(m, (a & 0xE0) == 0xC0 ? 2 : 3);
}
void fx_score(const fx_ev_t *ev, int n) { sc = ev; sc_n = n; sc_i = 0; }
void fx_score_poll(int t) {
  // at most 5 messages a frame: the fifo of menu_midi_mux holds 16 bytes (1 ms a byte), a wait would drop a frame
  for (int k = 0; k < 5 && sc_i < sc_n && t >= sc[sc_i].at; k++, sc_i++) fx_send3(sc[sc_i].st, sc[sc_i].d1, sc[sc_i].d2);
}
void fx_score_pump(int t) {
  // a message only when the fifo of menu_midi_mux is not full: the wait for its last bytes is 1..2 ms
  while (sc_i < sc_n && t >= sc[sc_i].at && !(R_MIDI & 1)) { fx_send3(sc[sc_i].st, sc[sc_i].d1, sc[sc_i].d2); sc_i++; }
}
void fx_sound_end(int c0, int c1) {
  for (int c = c0; c <= c1; c++) {
    fx_send3((uint8_t) (0xB0 | c), 123, 0);           // all notes off
    fx_send3((uint8_t) (0xB0 | c), 121, 0);           // the controllers reset (expression, pitch bend)
    if (c != 9) fx_send3((uint8_t) (0xC0 | c), 0, 0);
  }
}
