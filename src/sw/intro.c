// The boot intro (intro.h), on the parts of fx.c (the frames, the light, the glow, the MIDI table).
// The logo: the pixels of the menu logo (images_gen.h) x S with its glow.
#include <stdint.h>
#include "intro.h"
#include "fx.h"
#include "mmio.h"
#include "ui.h"
#include "cores.h"
#include "images_gen.h"
#include "settings.h"

// ---- the timeline (ms) ----
#define T_BEAM0   500            // the first beam starts (the others 60 ms apart)
#define T_MEET   1400            // the beams meet: the flash
#define T_GLARE0 2250
#define T_GLARE1 2750
#define T_SHRINK0 2900           // the logo goes to the corner, the background comes
#define T_SHRINK1 3500
#define T_END    3600

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
#define CW_MAX (LOGO_W * S_MAX + 6 * S_MAX)
#define CH_MAX (LOGO_H * S_MAX + 6 * S_MAX)
static uint8_t comp[CW_MAX * CH_MAX];       // the palette index of each pixel around the logo
static fx_img_t im = { comp, 0, 0, 0, 0 };
static int S, PAD, CW, CH, lx, ly;          // lx, ly: the top left of comp on the screen
static const uint8_t *logo_px;

static void logo_make(void) {
  logo_px = hw_id == 1 ? logo_go_px : logo_mini_px;
  fx_glow(&im, logo_px, LOGO_W, LOGO_H, LOGO_COLORS - 1, scr_w / 80, P_LOGO, P_GLOW);   // x 8 in 640 x 480, x 10 in 800 x 600
  S = im.s; PAD = im.pad; CW = im.cw; CH = im.ch;
  lx = ((scr_w - CW) / 2) & ~3;              // word aligned rows
  ly = (scr_h - CH) / 2 - scr_h / 24;
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
static void beams(int t) {
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
          if (d2 <= r * r) fx_put(x + dx, y + dy, P_BEAM + 15 * i, lev - lev * d2 / rr);
        }
      if (x - r < x0) x0 = x - r;
      if (y - r < y0) y0 = y - r;
      if (x + r > x1) x1 = x + r;
      if (y + r > y1) y1 = y + r;
    }
    if (x1 >= x0) fx_mark(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  }
}

// ---- the logo: comp with a wipe from the middle and the glare band ----
static void logo_draw(int t) {
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
    volatile uint32_t *p = (volatile uint32_t *) (fx_fb + (ly + y) * FB_STRIDE + lx);
    const uint32_t *l = (const uint32_t *) line;
    for (int k = 0; k < CW / 4; k++) p[k] = l[k];
    for (int x = CW & ~3; x < CW; x++) fx_fb[(ly + y) * FB_STRIDE + lx + x] = line[x];
  }
  fx_mark(lx, ly, CW, CH);
}

// the logo pixels at a scale of s / Q per pixel with the top left at (x, y): the shrink into the corner
static void logo_scaled(int x, int y, int s) {
  int w = LOGO_W * s / Q, h = LOGO_H * s / Q;
  for (int j = 0; j < h; j++) {
    int py = j * Q / s;
    if (py >= LOGO_H || (unsigned) (y + j) >= (unsigned) scr_h) continue;
    volatile uint8_t *p = fx_fb + (y + j) * FB_STRIDE + x;
    for (int i = 0; i < w; i++) {
      int px = i * Q / s;
      uint8_t v = px < LOGO_W ? logo_px[py * LOGO_W + px] : 0;
      if (v && (unsigned) (x + i) < (unsigned) scr_w) p[i] = (uint8_t) (P_LOGO + v - 1);
    }
  }
  fx_mark(x, y, w + 1, h + 1);
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
    uint32_t c = fx_mix(0xffffff, col[g], sharp);
    R_PAL(b, P_LOGO + g - 1) = ((uint32_t) (15 * la / Q) << 28) | c;
    R_PAL(b, P_GLARE + g - 1) = ((uint32_t) (15 * la / Q) << 28) | fx_mix(c, 0xffffff, Q * 3 / 4);
  }
  for (int l = 1; l <= 15; l++) {
    for (int i = 0; i < 4; i++) R_PAL(b, P_BEAM + 15 * i + l - 1) = ((uint32_t) l << 28) | fx_mix(col[i + 2], 0xffffff, l * Q / 15 / 2);
    R_PAL(b, P_WHITE + l - 1) = ((uint32_t) l << 28) | 0xe0f4ff;
  }
}

// the background: black, the flash, then the gradient of the look
static void background(int t) {
  uint32_t top = 0, bot = 0;
  if (t >= T_MEET && t < T_MEET + 220) {
    int v = Q - ramp(t, T_MEET, T_MEET + 220);
    v = v * v / Q * v / Q;
    top = bot = fx_mix(0, 0xb0d8ff, v / 2);
  }
  if (t >= T_SHRINK0) {
    int u = ease_io(ramp(t, T_SHRINK0, T_SHRINK1));
    top = fx_mix(0, look.gradient, u);
    bot = fx_mix(0, look.bg, u);
  }
  R_GRAD_TOP = top;
  R_GRAD_BOT = bot;
}

// ---- the sound: GM on channels 9 (drums) .. 15, a table by the time and two volume ramps ----
enum { C_DR = 9, C_PAD = 10, C_SW = 11, C_FX = 12, C_TAIKO = 13, C_TIMP = 14, C_HIT = 15 };
#define ON(c) (0x90 | (c))
#define OFF(c) (0x80 | (c))
#define PRG(c) (0xC0 | (c))
static const fx_ev_t score[] = {
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
static int cc_pad, cc_sw;
static void sound_poll(int t) {
  fx_score_poll(t);
  if (t < T_MEET) {                                   // the swell of the sweep pad
    int v = lerp(30, 120, ramp(t, 0, T_MEET));
    if (v - cc_pad >= 4) { cc_pad = v; fx_send3(0xB0 | C_PAD, 11, (uint8_t) v); }
  }
  if (t > 2700) {                                     // the warm pad fades out
    int v = lerp(100, 0, ramp(t, 2700, T_END));
    if (cc_sw - v >= 4) { cc_sw = v; fx_send3(0xB0 | C_SW, 11, (uint8_t) v); }
  }
}

// ---- the frames ----
int intro_play(void) {
  uint32_t tm = R_US;
  logo_make();
  dbg_printf("intro: glow made in %u us", (unsigned) (R_US - tm));
  fx_layout(P_GLOW, P_LOGO, P_BEAM, P_WHITE + 15);
  mx = lx + CW / 2; my = ly + CH / 2;
  bx0[0] = -40;         by0[0] = scr_h / 6;          // red from the left, high
  bx0[1] = scr_w + 40;  by0[1] = scr_h / 4;          // yellow from the right
  bx0[2] = -40;         by0[2] = scr_h * 3 / 4;      // green from the left, low
  bx0[3] = scr_w + 40;  by0[3] = scr_h * 5 / 6;      // cyan from the right
  fx_score(score, SCORE_N); cc_pad = 0; cc_sw = 100;
  int sound = look.sound, cut = 0;
  fx_start();
  if (fx_hold_monitor()) { look_apply(); return 1; }   // black until the monitor shows the picture
  uint32_t t0 = R_MS;
  for (;;) {
    int t = (int) (R_MS - t0);
    if (t >= T_END) break;
    if (fx_cut_short()) { cut = 1; break; }
    int b = fx_frame();
    // the streak at the start; the flare of the meeting
    if (t < 700) fx_streak(mx, my, ease_out(ramp(t, 0, 450)) * (scr_w / 2 + 20) / Q, Q - ramp(t, 400, 700), P_WHITE);
    fx_solid_out = t >= T_MEET + 40;
    beams(t);
    if (t < T_SHRINK0) logo_draw(t);
    else {
      int u = ease_io(ramp(t, T_SHRINK0, T_SHRINK1));
      logo_scaled(lerp(lx + PAD, 8, u), lerp(ly + PAD, 8, u), lerp(S * Q, 4 * Q, u));
    }
    if (t >= T_MEET && t < T_MEET + 500) {
      int u = ramp(t, T_MEET, T_MEET + 500);
      fx_streak(mx, my, lerp(40, scr_w / 2 + 40, ease_out(u)), Q - u, P_WHITE);
      int v = Q - ramp(t, T_MEET, T_MEET + 260);
      v = v * v / Q;
      if (v > 0) fx_disc(mx, my, lerp(56, 20, ease_out(Q - v)), 18 * v / Q, P_WHITE);
    }
    palette(b, t);
    fx_show(b, background, t);
    if (sound) sound_poll(t);
  }
  if (sound) fx_sound_end(C_DR, C_HIT);
  fx_end();
  return cut;
}

// at every start of the menu (user 2026-11-27: a core does not restart the menu by Ctrl+Alt+Backspace);
// Setup > Display > Boot animation, [ft812] intro of karabas.ini (read by settings_read)
int intro_wanted(void) { return set_val[S_INTRO] != 0; }
