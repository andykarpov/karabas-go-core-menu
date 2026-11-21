// The self-drawing boot menu (docs/PLAN.md), Ф1: the core list from the firmware (RPC CORES), a
// plain list in the frame buffer, the keys of the USB keyboard, the autoload countdown, the start of
// a core (RPC CORES LAUNCH after the clear of the SRAM). The look of the FT812 menu comes in Ф2.
#include <stdint.h>
#include "mmio.h"
#include "rpc.h"
#include "font8.h"

static void dbg_puts(const char *s) { while (*s) R_DBG = (uint8_t) *s++; }

// ---- drawing ----

enum { C_TEXT = 255, C_DIM = 254, C_SEL = 253, C_FRAME = 252, C_BLACK = 0 };

static void palette(void) {
  for (int i = 0; i < 128; i++) R_PAL(i) = (uint32_t) (i * 200 / 127); // the glow: black .. 0x0000C8
  R_PAL(C_FRAME) = 0x3060ff;
  R_PAL(C_SEL) = 0x2850d8;
  R_PAL(C_DIM) = 0x9090b0;
  R_PAL(C_TEXT) = 0xffffff;
}

static volatile uint8_t *fb;

static void fill(int x, int y, int w, int h, uint8_t c) {
  uint32_t c4 = c * 0x01010101u;
  for (int j = 0; j < h; j++) {
    volatile uint8_t *p = fb + (y + j) * FB_W + x;
    int i = 0;
    for (; i < w && ((uintptr_t) (p + i) & 3); i++) p[i] = c;
    for (; i + 4 <= w; i += 4) *(volatile uint32_t *) (p + i) = c4;
    for (; i < w; i++) p[i] = c;
  }
}

static void glyph(int x, int y, char ch, uint8_t c, int scale) {
  if (ch < 32 || ch > 126) ch = '?';
  const unsigned char *g = font8[ch - 32];
  for (int r = 0; r < 8; r++)
    for (int b = 0; b < 8; b++)
      if (g[r] & (0x80 >> b)) fill(x + b * scale, y + r * scale, scale, scale, c);
}

static int text(int x, int y, const char *s, uint8_t c, int scale) {
  for (; *s; s++, x += 8 * scale) glyph(x, y, *s, c, scale);
  return x;
}

static void glow_rows(int y0, int y1) {
  for (int y = y0; y < y1; y++) fill(0, y, FB_W, 1, (uint8_t) (y < 450 ? 127 - y * 127 / 450 : 0));
}

static char *u2s(char *p, uint32_t v) {
  char t[11]; int n = 0;
  do { t[n++] = '0' + v % 10; v /= 10; } while (v);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

// ---- the core list ----

#define MAX_CORES 64
typedef struct { char file[33], name[33], build[9], id[33]; uint8_t flags; } core_t;
static core_t cores[MAX_CORES];
static int ncores = 0;
static char autoload_file[33];
static int autoload_s = 0;
static uint8_t buf[RPC_MAX];

static void copy_str(char *dst, int max, const uint8_t **p, const uint8_t *end) {
  int i = 0;
  while (*p < end && **p) { if (i < max - 1) dst[i++] = (char) **p; (*p)++; }
  dst[i] = 0;
  if (*p < end) (*p)++;
}

static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void read_cores(void) {
  uint16_t n;
  ncores = 0;
  if (rpc_call(RPC_CORES, 0, 0, 0, buf, 2, &n, 3000) != RPC_OK || n < 2) { dbg_puts("menu: SCAN failed\n"); return; }
  int count = buf[0] | (buf[1] << 8);
  for (int i = 0; i < count && ncores < MAX_CORES; i++) {
    uint8_t idx[2] = { (uint8_t) i, (uint8_t) (i >> 8) };
    if (rpc_call(RPC_CORES, 1, idx, 2, buf, sizeof(buf), &n, 1000) != RPC_OK) continue;
    const uint8_t *p = buf, *end = buf + n;
    core_t *c = &cores[ncores];
    char flags[4];
    copy_str(c->file, 33, &p, end); copy_str(c->name, 33, &p, end); copy_str(c->build, 9, &p, end);
    copy_str(c->id, 33, &p, end); copy_str(flags, 4, &p, end);
    c->flags = (uint8_t) (flags[0] - '0');
    if (flags[1]) c->flags = c->flags * 10 + (flags[1] - '0');
    if (c->flags & 1) ncores++;  // visible ones only
  }
  if (rpc_call(RPC_CORES, 3, 0, 0, buf, sizeof(buf), &n, 1000) == RPC_OK && n >= 2) {
    autoload_s = buf[0];
    const uint8_t *p = buf + 1;
    copy_str(autoload_file, 33, &p, buf + n);
  }
}

// ---- the screen ----

#define ROWS 14
#define ROW_H 24
#define LIST_X 120
#define LIST_Y 120
#define LIST_W 560

static int sel = 0, top = 0;
static int autoload_left = -1; // seconds, -1 none

static void draw_list(void) {
  // the area of the list and the footer, on the glow
  glow_rows(LIST_Y - 8, LIST_Y + ROWS * ROW_H + 60);
  if (sel < top) top = sel;
  if (sel >= top + ROWS) top = sel - ROWS + 1;
  for (int r = 0; r < ROWS && top + r < ncores; r++) {
    int i = top + r, y = LIST_Y + r * ROW_H;
    if (i == sel) fill(LIST_X - 8, y - 4, LIST_W + 16, ROW_H, C_SEL);
    char num[4]; u2s(num, i + 1);
    text(LIST_X, y, num, C_DIM, 2);
    text(LIST_X + 56, y, cores[i].name, C_TEXT, 2);
    text(LIST_X + LIST_W - 8 * 16, y + 4, cores[i].build, C_DIM, 1);
  }
  if (!ncores) text(LIST_X, LIST_Y, "No cores on the SD card", C_TEXT, 2);
  int fy = LIST_Y + ROWS * ROW_H + 20;
  if (autoload_left >= 0) {
    char t[40] = "Autoload in ", *p = t + 12;
    p = u2s(p, autoload_left); *p++ = ' '; *p++ = 's'; *p = 0;
    text(LIST_X, fy, t, C_TEXT, 2);
  } else
    text(LIST_X, fy, "Up / Down, PgUp / PgDn, Enter", C_DIM, 2);
}

static int back = 0;
static void flip(void) {
  R_FB = (uint32_t) back;
  while ((R_FB & 1) != (uint32_t) back) ;
  back ^= 1;
  fb = back ? FB1 : FB0;
}

static void redraw(void) {
  draw_list();
  flip();
  draw_list(); // the other buffer the same, for the next change
}

static void draw_all(void) {
  for (int b = 0; b < 2; b++) {
    fb = b ? FB1 : FB0;
    glow_rows(0, FB_H);
    text(LIST_X, 48, "Karabas Go", C_TEXT, 4);
    text(LIST_X + 8 * 4 * 11, 64, "boot menu", C_DIM, 2);
    fill(LIST_X - 8, LIST_Y - 24, LIST_W + 16, 3, C_FRAME);
  }
  fb = back ? FB1 : FB0;
}

// ---- keys ----

enum { K_ENTER = 0x28, K_ESC = 0x29, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52,
       K_PGUP = 0x4B, K_PGDN = 0x4E, K_HOME = 0x4A, K_END = 0x4D };

// the first key of the report; held keys repeat after 400 ms every 60 ms
static int key_poll(void) {
  static uint8_t last = 0;
  static uint32_t t_down = 0, t_rep = 0;
  uint8_t k = (uint8_t) (R_KBD0 >> 8);
  uint32_t now = R_MS;
  if (k != last) {
    last = k;
    t_down = t_rep = now;
    return k;
  }
  if (k && now - t_down > 400 && now - t_rep > 60) { t_rep = now; return k; }
  return 0;
}

static void launch(int i) {
  dbg_puts("menu: launch "); dbg_puts(cores[i].file); dbg_puts("\n");
  fill(LIST_X - 8, LIST_Y + (sel - top) * ROW_H - 4, LIST_W + 16, ROW_H, C_FRAME);
  rpc_launch(cores[i].file);
}

int main(void) {
  dbg_puts("menu: start\n");
  // a check of the mailbox: the request buffer written and read back
  for (int i = 0; i < 16; i++) RPC(RPC_REQ + i) = (uint8_t) (0xA5 ^ i);
  int bad = 0;
  for (int i = 0; i < 16; i++) if (RPC(RPC_REQ + i) != (uint8_t) (0xA5 ^ i)) bad++;
  dbg_puts(bad ? "menu: rpc buffer BAD\n" : "menu: rpc buffer ok\n");

  palette();
  fb = FB0;
  draw_all();
  R_FB = 0;
  R_SHOW = 1;
  uint32_t t0 = R_MS;
  while (!rpc_enabled() && R_MS - t0 < 2000) ;
  if (rpc_enabled()) {
    read_cores();
    dbg_puts("menu: cores read\n");
  } else
    dbg_puts("menu: no RPC\n");
  for (int i = 0; i < ncores; i++)
    if (autoload_s && str_eq(cores[i].file, autoload_file)) { sel = i; autoload_left = autoload_s; }
  redraw();
  uint32_t t_auto = R_MS;

  for (;;) {
    int k = key_poll();
    if (k) {
      int old = sel;
      if (autoload_left >= 0) { autoload_left = -1; }
      if (k == K_UP && sel > 0) sel--;
      else if (k == K_DOWN && sel + 1 < ncores) sel++;
      else if ((k == K_PGUP || k == K_LEFT)) sel = sel > ROWS ? sel - ROWS : 0;
      else if ((k == K_PGDN || k == K_RIGHT)) sel = sel + ROWS < ncores ? sel + ROWS : (ncores ? ncores - 1 : 0);
      else if (k == K_HOME) sel = 0;
      else if (k == K_END && ncores) sel = ncores - 1;
      else if (k == K_ENTER && ncores) launch(sel);
      (void) old;
      redraw();
    }
    if (autoload_left >= 0) {
      int left = autoload_s - (int) ((R_MS - t_auto) / 1000);
      if (left < 0) launch(sel);
      if (left != autoload_left) { autoload_left = left; redraw(); }
    }
  }
}
