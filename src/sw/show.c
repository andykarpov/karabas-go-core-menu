// The second boot animation (show.h), on the parts of fx.c: the digits of the year flip to the new one,
// a flash, fireworks on the beats, a tree drawn by a beam with lamps and a star, snow, the title lines;
// "Jingle Bells" (James Lord Pierpont, 1857, public domain) on MIDI. On its day it is shown instead of
// the intro; at 23:59:50 of the day before, in the core list, a countdown leads into it.
#include <stdint.h>
#include "show.h"
#include "fx.h"
#include "mmio.h"
#include "ui.h"
#include "rpc.h"
#include "fonts.h"

// ---- the palette ----
#define P_GLOW   1               // the glow of the digits (15 levels)
#define P_DIGIT  16              // the digits
#define P_TEXT   17              // 15: the title lines
#define P_TEXT2  32              // 15: the small lines
#define P_SPARK  47              // 6 colours x 15 levels
#define P_TREE   137             // 15
#define P_STAR   152             // 15
#define P_SNOW   167             // 15
#define P_LAMP   182             // 4 x 15: their colours run along the garland
#define P_END    242
static const uint32_t spark_col[6] = { 0xff3030, 0xffe040, 0x40ff60, 0x40e0ff, 0xff50ff, 0xffffff };
static const uint32_t lamp_col[4] = { 0xff2020, 0xffd000, 0x2090ff, 0xff40ff };

// ---- the timeline (ms) ----
#define BEAT     340             // a quarter note (176 bpm)
#define T_ROLL0  700             // the changed digits roll
#define T_ROLL1  1250
#define T_FLASH  1360            // the first beat of the song
#define AT(hb)   (T_FLASH + (hb) * BEAT / 2)   // a half beat of the song
#define T_TREE   AT(32)          // bar 5: the tree, the snow; the year fades
#define T_TEXT   AT(48)          // bar 7: the lines
#define T_FADE0  AT(64)          // the end of the song: all fades, the background of the menu comes
#define T_END    (T_FADE0 + 900)

// ---- sine (a quarter of 256 steps, x Q) and a pseudo random sequence ----
static const int16_t sin_q[65] = {
  0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369, 392, 415, 438, 460, 483, 505,
  526, 548, 569, 590, 610, 630, 650, 669, 688, 706, 724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878,
  891, 903, 915, 926, 936, 946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021,
  1023, 1024, 1024 };
static int isin(int a) {
  a &= 255;
  return a < 64 ? sin_q[a] : a < 128 ? sin_q[128 - a] : a < 192 ? -sin_q[a - 128] : -sin_q[256 - a];
}
static int icos(int a) { return isin(a + 64); }
static uint32_t rnd_s;
static uint32_t rnd(void) { rnd_s = rnd_s * 1664525u + 1013904223u; return rnd_s >> 8; }
static int rnd_in(int lo, int hi) { return lo + (int) (rnd() % (uint32_t) (hi - lo + 1)); }
static int isqrt(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

// ---- the digits: boxy, in the manner of the logo letters ----
#define DG_W 6
#define DG_H 9
static const char *const digit_art[10] = {
  "#######....##....##....##....##....##....##....#######",
  "...##.....#.....#.....#.....#.....#.....#.....#.....#.",
  "######.....#.....#.....########.....#.....#.....######",
  "######.....#.....#.....#.#####.....#.....#.....#######",
  "#....##....##....##....#######.....#.....#.....#.....#",
  "#######.....#.....#.....######.....#.....#.....#######",
  "#######.....#.....#.....#######....##....##....#######",
  "######.....#.....#.....#.....#.....#.....#.....#.....#",
  "#######....##....##....########....##....##....#######",
  "#######....##....##....#######.....#.....#.....#######",
};
static int digit_px(int d, int x, int y) {     // 1 if (x, y) of digit d is set
  const char *a = digit_art[d];
  int i = y * DG_W + x, n = 0;
  while (a[n]) n++;
  return i < n && a[i] == '#';
}
#define NUM_MAX 4
#define PX_W (NUM_MAX * (DG_W + 2))
static uint8_t num_px[PX_W * DG_H];
static int num_w;
static void number_px(const char *s) {          // the picture of a number, 2 pixels between the digits
  int n = 0;
  while (s[n]) n++;
  num_w = n * (DG_W + 2) - 2;
  for (int y = 0; y < DG_H; y++)
    for (int x = 0; x < num_w; x++) {
      int d = x / (DG_W + 2), dx = x % (DG_W + 2);
      num_px[y * num_w + x] = (uint8_t) (dx < DG_W && digit_px(s[d] - '0', dx, y) ? 1 : 0);
    }
}
#define YS_MAX 12
#define YC_MAX ((PX_W + 6) * YS_MAX * (DG_H + 6) * YS_MAX)
static uint8_t comp_old[YC_MAX], comp_new[YC_MAX];
static fx_img_t im_old = { comp_old, 0, 0, 0, 0 }, im_new = { comp_new, 0, 0, 0, 0 };
static char s_old[8], s_new[8];
static void num_text(char *p, int v) { char t[8]; int n = 0; do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v); while (n) *p++ = t[--n]; *p = 0; }

// a number picture at (x, y) (its top left); the digits that differ from the old one roll by off (0..ch)
static void number_draw(int x, int y, const fx_img_t *n, const fx_img_t *o, int off) {
  static uint8_t line[(PX_W + 6) * YS_MAX] __attribute__((aligned(4)));
  int cw = n->cw, ch = n->ch, cell = (DG_W + 2) * n->s;
  for (int j = 0; j < ch; j++) {
    if ((unsigned) (y + j) >= (unsigned) scr_h) continue;
    const uint8_t *rn = n->comp + j * cw;
    for (int i = 0; i < cw; i++) {
      int d = (i - n->pad + n->s) / cell;      // the digit cell of the column (split in the gaps)
      if (d < 0) d = 0;
      if (o && off < ch && d < NUM_MAX && s_old[d] != s_new[d]) {
        int sn = j - (ch - off), so = j + off;  // the new one comes from below, the old one goes up
        line[i] = sn >= 0 && sn < ch ? n->comp[sn * cw + i] : so >= 0 && so < ch ? o->comp[so * cw + i] : 0;
      } else line[i] = rn[i];
    }
    volatile uint32_t *p = (volatile uint32_t *) (fx_fb + (y + j) * FB_STRIDE + x);
    const uint32_t *l = (const uint32_t *) line;
    for (int k = 0; k < cw / 4; k++) p[k] = l[k];
    for (int i = cw & ~3; i < cw; i++) fx_fb[(y + j) * FB_STRIDE + x + i] = line[i];
  }
  fx_mark(x, y, cw, ch);
}

// ---- the MIDI of the song, sent whenever it is due: in the drawing and while a frame start is awaited
static int pump_on;
static uint32_t pump_t0;
static void pump(void) { if (pump_on) fx_score_pump((int) (R_MS - pump_t0)); }

// ---- fireworks: a rocket, then a burst of sparks falling ----
#define NB 13
#define LIFE 1700
typedef struct { int t, x, y, x0, r, n, c; uint32_t seed; } burst_t;
static burst_t bursts[NB];
static const int8_t burst_hb[NB] = { 2, 6, 10, 14, 18, 20, 22, 26, 28, 30, 56, 58, 60 };   // half beats
static void bursts_make(void) {
  rnd_s = 2027;
  int k = scr_w * Q / 640;                      // the sizes for 640 x 480
  for (int i = 0; i < NB; i++) {
    burst_t *b = &bursts[i];
    b->t = AT(burst_hb[i]);
    int late = i >= NB - 3;                     // the last ones high, over the lines
    b->x = scr_w * rnd_in(late ? 45 : 15, 85) / 100;
    b->y = scr_h * rnd_in(late ? 10 : 36, late ? 24 : 60) / 100;
    b->x0 = b->x + rnd_in(-40, 40);
    b->r = rnd_in(95, 135) * k / Q;
    b->n = rnd_in(56, 72);
    b->c = i % 6;
    b->seed = rnd();
  }
}
static uint32_t hash(uint32_t a) { a ^= a >> 15; a *= 0x2c1b3c6du; a ^= a >> 12; a *= 0x297a2d39u; a ^= a >> 15; return a; }
static void spark(int x, int y, int base, int lev) {   // 2 x 2 with a ring around
  fx_put(x, y, base, lev); fx_put(x + 1, y, base, lev); fx_put(x, y + 1, base, lev); fx_put(x + 1, y + 1, base, lev);
  fx_put(x - 1, y, base, lev - 5); fx_put(x + 2, y, base, lev - 5); fx_put(x - 1, y + 1, base, lev - 5); fx_put(x + 2, y + 1, base, lev - 5);
  fx_put(x, y - 1, base, lev - 5); fx_put(x + 1, y - 1, base, lev - 5); fx_put(x, y + 2, base, lev - 5); fx_put(x + 1, y + 2, base, lev - 5);
}
static void burst_draw(const burst_t *b, int t) {
  int base = P_SPARK + 15 * b->c, g = scr_h / 7;
  if (t >= b->t - 600 && t < b->t) {            // the rocket with its trail
    for (int k = 14; k >= 0; k--) {
      int ts = t - k * 12;
      if (ts < b->t - 600) continue;
      int u = ramp(ts, b->t - 600, b->t);
      int x = lerp(b->x0, b->x, u), y = lerp(scr_h + 4, b->y, ease_out(u));
      spark(x, y, k ? base : P_SPARK + 75, 15 - k);
    }
    fx_mark(b->x0 < b->x ? b->x0 - 3 : b->x - 3, b->y - 3, (b->x0 > b->x ? b->x0 - b->x : b->x - b->x0) + 7, scr_h - b->y + 10);
    return;
  }
  int a = t - b->t;
  if (a < 0 || a >= LIFE) return;
  if (a < 160) { fx_nomark = 1; fx_disc(b->x, b->y, 16, 15 * (160 - a) / 160, P_SPARK + 75); fx_nomark = 0; }
  for (int j = 0; j < b->n; j++) {
    pump();
    uint32_t h = hash(b->seed + (uint32_t) j);
    int ang = j * 256 / b->n + (int) (h & 7), sp = Q * 6 / 10 + (int) ((h >> 4) % 410);
    int rx = b->r * sp / Q * icos(ang) / Q, ry = b->r * sp / Q * isin(ang) / Q;
    for (int k = 3; k >= 0; k--) {              // the spark and three of its past places
      int aa = a - k * 25;
      if (aa < 0) continue;
      int u = aa * Q / LIFE, e = ease_out(ramp(aa, 0, 1000));
      int x = b->x + rx * e / Q, y = b->y + ry * e / Q + g * u / Q * u / Q;
      int v = Q - u, lev = 15 * v / Q * v / Q + 3 - k * 3;
      if (u > Q * 6 / 10) lev = ((j * 7 + aa / 40) & 3) == 0 ? lev + 6 : lev - 2;   // the crackle at the end
      spark(x, y, base, lev);
    }
  }
  fx_mark(b->x - b->r - 4, b->y - b->r - 4, 2 * b->r + 9, 2 * b->r + g + 9);
}

// ---- the tree: a zigzag drawn by a beam from the bottom, lamps along it, a star at the top ----
#define TP 13
static int tx[TP], ty[TP], tlen[TP], ttotal, tree_x0, tree_y0, tree_x1, tree_y1;
#define LAMP_STEP 20
static void tree_make(void) {
  int cx = scr_w * 22 / 100, top = scr_h * 20 / 100, bot = scr_h * 86 / 100, hw = scr_w * 17 / 100;
  for (int k = 0; k < TP; k++) {
    int y = Q - k * Q / (TP - 1);               // 1 at the bottom, 0 at the top
    tx[k] = cx + (k & 1 ? 1 : -1) * hw * y / Q * 95 / 100;
    ty[k] = top + (bot - top) * y / Q;
  }
  ttotal = 0;
  for (int k = 1; k < TP; k++) {
    int dx = tx[k] - tx[k - 1], dy = ty[k] - ty[k - 1];
    tlen[k] = isqrt(dx * dx + dy * dy);
    ttotal += tlen[k];
  }
  tree_x0 = cx - hw - 24; tree_x1 = cx + hw + 24; tree_y0 = top - 30; tree_y1 = bot + 8;
}
static void tree_draw(int t) {
  if (t < T_TREE) return;
  int d = ttotal * ease_io(ramp(t, T_TREE, T_TREE + 1600)) / Q, done = 0, hx = tx[0], hy = ty[0];
  fx_nomark = 1;
  for (int k = 1; k < TP && done < d; k++) {
    pump();
    int len = tlen[k], n = d - done < len ? d - done : len;
    int sx = ((tx[k] - tx[k - 1]) << 16) / len, sy = ((ty[k] - ty[k - 1]) << 16) / len;
    int x = tx[k - 1] << 16, y = ty[k - 1] << 16;
    for (int i = 0; i <= n; i++, x += sx, y += sy) {
      int px = x >> 16, py = y >> 16;
      for (int dy = -3; dy <= 3; dy++)            // a line 3 px thick with a soft edge
        for (int dx = -3; dx <= 3; dx++) {
          int d2 = dx * dx + dy * dy;
          if (d2 <= 9) fx_put(px + dx, py + dy, P_TREE, d2 <= 1 ? 13 : d2 <= 4 ? 9 : 4);
        }
      int at = done + i;
      if (at % LAMP_STEP == LAMP_STEP / 2) fx_disc(px, py + 4, 5, 16, P_LAMP + 15 * ((at / LAMP_STEP) & 3));
      hx = px; hy = py;
    }
    done += len;
  }
  if (d < ttotal) fx_disc(hx, hy, 5, 15, P_STAR);        // the head of the beam
  else {                                               // the star
    int a = t - (T_TREE + 1600), p = 12 + 3 * isin(a / 8) / Q;
    int sx = tx[TP - 1], sy = ty[TP - 1] - 4;
    fx_disc(sx, sy, 7, 15, P_STAR);
    fx_streak(sx, sy, 22 + 4 * isin(a / 6) / Q, Q * p / 15, P_STAR);
    for (int dy = -20; dy <= 20; dy++) fx_put(sx, sy + dy, P_STAR, p - (dy < 0 ? -dy : dy) * p / 20);
  }
  fx_nomark = 0;
  fx_mark(tree_x0, tree_y0, tree_x1 - tree_x0, tree_y1 - tree_y0);
}

// ---- snow: erased flake by flake (a mark would clear the whole screen every frame) ----
#define NF 140
typedef struct { int16_t x, y, sp, amp, ph; uint8_t big, lev; } flake_t;
static flake_t flakes[NF];
static int16_t fprev[2][NF][2];
static void snow_make(void) {
  rnd_s = 1225;
  for (int i = 0; i < NF; i++) {
    flake_t *f = &flakes[i];
    f->x = (int16_t) rnd_in(0, scr_w - 1); f->y = (int16_t) rnd_in(-scr_h, scr_h - 1);
    f->sp = (int16_t) rnd_in(25, 70); f->amp = (int16_t) rnd_in(3, 12); f->ph = (int16_t) rnd_in(0, 255);
    f->big = (uint8_t) (rnd_in(0, 3) == 0); f->lev = (uint8_t) rnd_in(6, 13);
  }
  for (int b = 0; b < 2; b++) for (int i = 0; i < NF; i++) fprev[b][i][0] = -1;
}
static void snow_erase(int b) {                 // the flakes of two frames ago
  for (int i = 0; i < NF; i++) {
    int x = fprev[b][i][0], y = fprev[b][i][1];
    if (x < 0) continue;
    for (int k = 0; k < (flakes[i].big ? 4 : 1); k++) {
      int px = x + (k & 1), py = y + (k >> 1);
      if ((unsigned) px < (unsigned) scr_w && (unsigned) py < (unsigned) scr_h) {
        volatile uint8_t *p = fx_fb + py * FB_STRIDE + px;
        if (*p >= P_SNOW && *p < P_SNOW + 15) *p = 0;
      }
    }
    fprev[b][i][0] = -1;
  }
}
static void snow_put(int b, int t) {             // over all, where it is brighter
  if (t < T_TREE) return;
  int a = t - T_TREE;
  for (int i = 0; i < NF; i++) {
    const flake_t *f = &flakes[i];
    int y = f->y + f->sp * a / 1000;
    if (y < 0) continue;
    y %= scr_h + 8;
    int x = f->x + f->amp * isin(f->ph + a / 12) / Q;
    for (int k = 0; k < (f->big ? 4 : 1); k++) fx_put(x + (k & 1), y + (k >> 1), P_SNOW, f->lev);
    fprev[b][i][0] = (int16_t) x; fprev[b][i][1] = (int16_t) y;
  }
}

// ---- the lines ----
static char title1[40], title2[40];
static const char *const small1 = "Спасибо, что вы с Karabas!";
static const char *const small2 = "Thank you for being with Karabas!";
static void text_draw(int t) {
  if (t < T_TEXT) return;
  int cx = scr_w * 64 / 100;
  gfx_text_center(&f32, cx, scr_h * 36 / 100, title1, P_TEXT); pump();
  gfx_text_center(&f32, cx, scr_h * 46 / 100, title2, P_TEXT); pump();
  gfx_text_center(&f27, cx, scr_h * 58 / 100, small1, P_TEXT2); pump();
  gfx_text_center(&f27, cx, scr_h * 63 / 100, small2, P_TEXT2);
  fx_mark(scr_w * 30 / 100, scr_h * 30 / 100, scr_w * 70 / 100, scr_h * 37 / 100);
}

// ---- the palette of a frame; all fades out at the end ----
static void palette(int b, int t) {
  int gf = Q - ramp(t, T_FADE0, T_FADE0 + 700);
#define A(l, k) ((uint32_t) clampi((l) * (k) / Q * gf / Q, 0, 15) << 28)
  int yi = ramp(t, 0, 400) - ramp(t, T_TREE, T_TREE + 500);         // the year
  int hot = Q - ramp(t, T_FLASH, T_FLASH + 600);                     // white hot at the flash
  for (int l = 1; l <= 15; l++) R_PAL(b, P_GLOW + l - 1) = A(l, yi) | 0xff8a20;
  R_PAL(b, P_DIGIT) = A(15, yi) | fx_mix(0xffd860, 0xffffff, hot);
  int ti = ramp(t, T_TEXT, T_TEXT + 500), si = ramp(t, T_TEXT + 600, T_TEXT + 1100);
  for (int l = 1; l <= 15; l++) {
    R_PAL(b, P_TEXT + l - 1) = A(l, ti) | 0xfff4e0;
    R_PAL(b, P_TEXT2 + l - 1) = A(l, si) | 0xb8dcff;
    for (int c = 0; c < 6; c++) R_PAL(b, P_SPARK + 15 * c + l - 1) = A(l, Q) | fx_mix(spark_col[c], 0xffffff, l * Q / 15 / 2);
    R_PAL(b, P_TREE + l - 1) = A(l, Q) | fx_mix(0x20c040, 0xc0ffc0, l * Q / 15 / 3);
    R_PAL(b, P_STAR + l - 1) = A(l, Q) | fx_mix(0xffd040, 0xffffff, l * Q / 15 / 2);
    R_PAL(b, P_SNOW + l - 1) = A(l, ramp(t, T_TREE, T_TREE + 800)) | 0xe8f4ff;
    int ph = t / 280;                                               // the lamps run along the garland
    for (int k = 0; k < 4; k++) R_PAL(b, P_LAMP + 15 * k + l - 1) = A(l, Q) | fx_mix(lamp_col[(k + ph) & 3], 0xffffff, l * Q / 15 / 3);
  }
#undef A
}

// the background: a night sky, the flash, then the gradient of the look
static void background(int t) {
  uint32_t top = 0x030a20, bot = 0x000000;
  int u = ramp(t, 0, 600);
  top = fx_mix(0, top, u);
  if (t >= T_FLASH && t < T_FLASH + 260) {
    int v = Q - ramp(t, T_FLASH, T_FLASH + 260);
    v = v * v / Q * v / Q;
    top = bot = fx_mix(top, 0xffe0b0, v / 2);
  }
  if (t >= T_FADE0) {
    int w = ease_io(ramp(t, T_FADE0, T_END));
    top = fx_mix(top, look.gradient, w);
    bot = fx_mix(bot, look.bg, w);
  }
  R_GRAD_TOP = top;
  R_GRAD_BOT = bot;
}

// ---- the song: a table made at the start (notes in half beats of the song) ----
enum { C_DR = 9, C_BASS = 10, C_CH = 11, C_MEL = 12, C_MEL2 = 13, C_BOOM = 14, C_HIT = 15 };
#define NEV 420
static fx_ev_t ev[NEV];
static int nev;
// the order of the messages of the same time (1 ms a byte on the wire): the note offs, the tune, the
// bells and drums, the bass, the chords, the tune an octave up
static int prio(uint8_t st) {
  static const uint8_t ch_prio[16] = { 6, 6, 6, 6, 6, 6, 6, 6, 6, 2, 3, 4, 1, 5, 6, 6 };
  return (st & 0xF0) == 0x80 ? 0 : (st & 0xF0) == 0xC0 ? 0 : ch_prio[st & 15];
}
static void ev_add(int at, uint8_t st, uint8_t d1, uint8_t d2) {
  if (nev >= NEV || at < 0) return;
  int i = nev++, p = prio(st);
  while (i > 0 && (ev[i - 1].at > at || (ev[i - 1].at == at && prio(ev[i - 1].st) > p))) { ev[i] = ev[i - 1]; i--; }
  ev[i] = (fx_ev_t) { (uint16_t) at, st, d1, d2 };
}
static void note(int ch, int hb, int len, int key, int vel) {
  ev_add(AT(hb), (uint8_t) (0x90 | ch), (uint8_t) key, (uint8_t) vel);
  ev_add(AT(hb + len) - 30, (uint8_t) (0x80 | ch), (uint8_t) key, 0);
}
static void drum(int at, int key, int vel) { ev_add(at, 0x99, (uint8_t) key, (uint8_t) vel); }
// the refrain: bars 1-4 and the last line (bars 13-16): E E E- | E E E- | E G C. D | E--- || F F F. F | F E E E E | G G F D | C---
static const uint8_t mel[][3] = {   // half beat, length, key
  { 0, 2, 76 }, { 2, 2, 76 }, { 4, 4, 76 }, { 8, 2, 76 }, { 10, 2, 76 }, { 12, 4, 76 },
  { 16, 2, 76 }, { 18, 2, 79 }, { 20, 3, 72 }, { 23, 1, 74 }, { 24, 8, 76 },
  { 32, 2, 77 }, { 34, 2, 77 }, { 36, 3, 77 }, { 39, 1, 77 }, { 40, 2, 77 }, { 42, 2, 76 }, { 44, 2, 76 },
  { 46, 1, 76 }, { 47, 1, 76 }, { 48, 2, 79 }, { 50, 2, 79 }, { 52, 2, 77 }, { 54, 2, 74 }, { 56, 8, 72 },
};
static const uint8_t bar_root[8] = { 48, 48, 48, 48, 41, 48, 43, 48 };   // C C C C F C G C
static const uint8_t bar_fifth[8] = { 43, 43, 43, 43, 48, 43, 50, 43 };
static const uint8_t chords[3][3] = { { 60, 64, 67 }, { 60, 65, 69 }, { 59, 62, 65 } };   // C, F, G7
static const uint8_t bar_chord[8] = { 0, 0, 0, 0, 1, 0, 2, 0 };
static void song_make(void) {
  nev = 0;
  ev_add(0, 0xC0 | C_BASS, 33, 0); ev_add(0, 0xC0 | C_CH, 45, 0); ev_add(0, 0xC0 | C_MEL, 10, 0);
  ev_add(0, 0xC0 | C_MEL2, 119, 0); ev_add(0, 0xC0 | C_BOOM, 47, 0); ev_add(0, 0xC0 | C_HIT, 55, 0);
  // before the song: the reverse cymbal to the flash, a clack as the digits roll and land
  ev_add(150, 0x90 | C_MEL2, 60, 100); ev_add(T_FLASH - 10, 0x80 | C_MEL2, 60, 0);
  drum(T_ROLL0, 75, 110); drum(T_ROLL1, 75, 120);
  ev_add(T_FLASH, 0x90 | C_HIT, 60, 110); ev_add(T_FLASH, 0x90 | C_HIT, 48, 110);
  ev_add(T_FLASH + 400, 0x80 | C_HIT, 60, 0); ev_add(T_FLASH + 400, 0x80 | C_HIT, 48, 0);
  ev_add(T_FLASH, 0xC0 | C_MEL2, 9, 0);         // then the glockenspiel doubles the tune an octave up
  for (unsigned i = 0; i < sizeof(mel) / sizeof(mel[0]); i++) {
    note(C_MEL, mel[i][0], mel[i][1], mel[i][2], 100);
    note(C_MEL2, mel[i][0], mel[i][1], mel[i][2] + 12, 55);
  }
  for (int b = 0; b < 8; b++) {
    int h = b * 8;
    if (b < 7) {
      note(C_BASS, h, 3, bar_root[b], 95); note(C_BASS, h + 4, 3, bar_fifth[b], 85);
      for (int k = 0; k < 3; k++) { note(C_CH, h + 2, 1, chords[bar_chord[b]][k], 70); note(C_CH, h + 6, 1, chords[bar_chord[b]][k], 70); }
      for (int k = 0; k < 8; k++) drum(AT(h + k), 83, k & 1 ? 55 : 90);   // the sleigh bells
      drum(AT(h), 36, 80);
    } else {
      note(C_BASS, h, 8, 36, 100);
      for (int k = 0; k < 3; k++) note(C_CH, h, 8, chords[0][k], 80);
      note(C_CH, h, 8, 72, 80);
      drum(AT(h), 83, 100); drum(AT(h), 36, 100);
    }
  }
  drum(AT(0), 49, 115); drum(AT(32), 49, 100); drum(AT(56), 49, 120); drum(AT(56), 57, 110);
  for (int i = 0; i < NB; i++) {                // the bursts
    ev_add(bursts[i].t, 0x90 | C_BOOM, 36, 105); ev_add(bursts[i].t + 300, 0x80 | C_BOOM, 36, 0);
  }
}

// ---- the countdown before it: the seconds left in the digits, a tick each ----
static void count_palette(int b, int u) {       // u: Q at the start of a second, falling
  for (int l = 1; l <= 15; l++) R_PAL(b, P_GLOW + l - 1) = ((uint32_t) (l * (Q / 3 + u * 2 / 3) / Q) << 28) | 0xff8a20;
  R_PAL(b, P_DIGIT) = 0xF0000000u | fx_mix(0xffd860, 0xffffff, u);
}
static void count_bg(int t) { (void) t; R_GRAD_TOP = 0x030a20; R_GRAD_BOT = 0; }
static int countdown(int count, int sound) {
  static int shown = -1;
  uint32_t t0 = R_MS;
  shown = -1;
  for (;;) {
    int t = (int) (R_MS - t0), left = count - t / 1000;
    if (left <= 0) return 0;
    if (fx_cut_short()) return 1;
    int b = fx_frame();
    if (left != shown) {                        // a new number: its picture, the tick
      char s[4];
      num_text(s, left);
      number_px(s);
      fx_glow(&im_new, num_px, num_w, DG_H, 1, scr_w / 53, P_DIGIT, P_GLOW);
      shown = left;
      if (sound) {
        fx_send3(0x99, left <= 3 ? 76 : 77, left <= 3 ? 120 : 90);
        if (left <= 3) { fx_send3(0xC0 | C_BOOM, 47, 0); fx_send3(0x90 | C_BOOM, 43, 100); }
      }
    }
    gfx_clear(0, 0, scr_w, scr_h);              // the number changes width: all of it
    number_draw(((scr_w - im_new.cw) / 2) & ~3, (scr_h - im_new.ch) / 2, &im_new, 0, 0);
    count_palette(b, Q - ramp(t % 1000, 0, 700));
    fx_show(b, count_bg, t);
  }
}

// ---- the show ----
int show_play(int year, int count, int hold) {
  int sound = look.sound, cut = 0;
  fx_layout(P_GLOW, P_DIGIT, P_SPARK, P_END);
  fx_start();
  if (hold && fx_hold_monitor()) { look_apply(); return 1; }
  if (count > 0) {
    fx_solid_out = 1;
    if (countdown(count, sound)) { if (sound) fx_sound_end(C_DR, C_HIT); fx_end(); return 1; }
    fx_start();
  }
  // the pictures of the two years, the lines, the fireworks, the tree, the snow, the song
  int s = scr_w >= 800 ? 12 : 10;              // the digits x 10 in 640 x 480, x 12 in 800 x 600
  num_text(s_old, year - 1); num_text(s_new, year);
  number_px(s_old); fx_glow(&im_old, num_px, num_w, DG_H, 1, s, P_DIGIT, P_GLOW);
  number_px(s_new); fx_glow(&im_new, num_px, num_w, DG_H, 1, s, P_DIGIT, P_GLOW);
  char *p = str_cpy(title1, "С Новым "); p = str_cpy(p, s_new); str_cpy(p, " годом!");
  p = str_cpy(title2, "Happy New Year "); p = str_cpy(p, s_new); str_cpy(p, "!");
  bursts_make();
  tree_make();
  snow_make();
  song_make();
  fx_score(ev, nev);
  fx_solid_out = 1;
  int yx = ((scr_w - im_new.cw) / 2) & ~3, ych = im_new.ch;
  uint32_t t0 = R_MS;
  pump_t0 = t0; pump_on = sound;
  fx_idle = pump;
  for (;;) {
    int t = (int) (R_MS - t0);
    if (t >= T_END) break;
    if (fx_cut_short()) { cut = 1; break; }
#define PUMP() pump()
    PUMP();
    int b = fx_frame();
    snow_erase(b);
    if (t < T_TREE + 500) {                     // the year: in the middle, up after the flash
      int yc = lerp(scr_h * 45 / 100, scr_h * 20 / 100, ease_io(ramp(t, T_FLASH + 100, T_FLASH + 800)));
      int off = t < T_ROLL0 ? 0 : t >= T_ROLL1 ? ych : ych * ease_out(ramp(t, T_ROLL0, T_ROLL1)) / Q;
      number_draw(yx, yc - ych / 2, &im_new, &im_old, off);
    }
    PUMP();
    text_draw(t);
    PUMP();
    tree_draw(t);
    PUMP();
    for (int i = 0; i < NB; i++) { burst_draw(&bursts[i], t); PUMP(); }
    if (t >= T_FLASH && t < T_FLASH + 500) {    // the flash at the year
      int u = ramp(t, T_FLASH, T_FLASH + 500), yc = scr_h * 45 / 100;
      fx_streak(scr_w / 2, yc, lerp(60, scr_w / 2 + 40, ease_out(u)), Q - u, P_STAR);
    }
    PUMP();
    snow_put(b, t);
    PUMP();
    palette(b, t);
    fx_show(b, background, t);
    PUMP();
  }
#undef PUMP
  fx_idle = 0; pump_on = 0;
  if (sound) fx_sound_end(C_DR, C_HIT);
  fx_end();
  return cut;
}

// ---- when ----
static int rtc(uint8_t t[8]) {
  uint16_t n;
  return rpc_call(RPC_SYS, 1, 0, 0, t, 8, &n, 500) == RPC_OK && n >= 8;
}
int show_year(void) {
  uint8_t t[8];
  if (!rtc(t)) return 2027;
  int y = t[0] | (t[1] << 8);
  return t[2] == 1 ? y : y + 1;
}
int show_today(int *year) {
  uint8_t t[8];
  if (!rtc(t)) return 0;
  *year = t[0] | (t[1] << 8);
  return t[2] == 1 && t[3] == 1 && *year >= 2026;
}
int show_tick(void) {
  static uint32_t last = 0;
  if (R_MS - last < 1000) return 0;
  last = R_MS;
  uint8_t t[8];
  if (!rtc(t)) return 0;
  if (t[2] != 12 || t[3] != 31 || t[4] != 23 || t[5] != 59 || t[6] < 50) return 0;
  show_play((t[0] | (t[1] << 8)) + 1, 60 - t[6], 0);
  return 1;
}
int show_key(uint8_t key) {
  static const uint8_t seq[10] = { K_UP, K_UP, K_DOWN, K_DOWN, K_LEFT, K_RIGHT, K_LEFT, K_RIGHT, 0x05, K_A };
  static int at = 0;
  if (key == seq[at]) at++;
  else at = key == seq[0] ? 1 : 0;
  if (at == 10) { at = 0; return 1; }
  return 0;
}
