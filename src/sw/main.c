// The self-drawing boot menu (docs/PLAN.md), Ф2: the screen of the FT812 menu (firmware
// app_core_browser_ft_menu: its geometry at 800 x 600, the [ft812] colours of karabas.ini) drawn by
// the CPU: the gradient in hardware (menu_compose), over it the logo, the clock, Setup, the core
// buttons of a page, the footer and the character. Keys of the USB keyboard; the autoload.
#include <stdint.h>
#include "mmio.h"
#include "rpc.h"
#include "gfx.h"
#include "fonts_gen.h"
#include "images_gen.h"
#include "sound.h"

static void dbg_puts(const char *s) { while (*s) R_DBG = (uint8_t) *s++; }

static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static char *str_cpy(char *d, const char *s) { while ((*d = *s)) { d++; s++; } return d; }
static char *u2s(char *p, uint32_t v, int digits) {
  char t[11]; int n = 0;
  do { t[n++] = '0' + v % 10; v /= 10; } while (v || n < digits);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

// ---- settings: [ft812] of karabas.ini over CFG ----

typedef struct {
  uint32_t bg, gradient, button, active, text, text_active, copyright;
  int time, date, chr, mouse_speed, click, sound;
} look_t;
static look_t look = { 0x010101, 0x0000c8, 0x202026, 0x0000c8, 0xffffff, 0xffffff, 0x787878, 1, 1, 1, 3, 1, 1 };

static uint8_t buf[RPC_MAX];

static int cfg_get(const char *section, const char *key, char *val, int max) {
  char req[64], *p = req;
  p = str_cpy(p, "0") + 1; p = str_cpy(p, section) + 1; p = str_cpy(p, key) + 1;
  uint16_t n;
  if (rpc_call(RPC_CFG, 0, req, (uint16_t) (p - req), buf, sizeof(buf), &n, 1000) != RPC_OK) return 0;
  int i = 0;
  for (; i < n && i < max - 1 && buf[i]; i++) val[i] = (char) buf[i];
  val[i] = 0;
  return 1;
}

static void cfg_color(const char *key, uint32_t *c) {
  char v[16];
  if (!cfg_get("ft812", key, v, sizeof(v))) return;
  uint32_t x = 0; int any = 0;
  for (const char *s = v; *s; s++) {
    int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
            *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
    if (s == v + 1 && (*s == 'x' || *s == 'X') && v[0] == '0') { x = 0; continue; }
    if (d < 0) return;
    x = x * 16 + d; any = 1;
  }
  if (any) *c = x & 0xFFFFFF;
}

static void cfg_bool(const char *key, int *b) {
  char v[8];
  if (!cfg_get("ft812", key, v, sizeof(v))) return;
  *b = v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y' || (v[0] == 'o' && v[1] == 'n');
}

static void read_look(void) {
  cfg_color("color_bg", &look.bg);
  cfg_color("color_gradient", &look.gradient);
  cfg_color("color_button", &look.button);
  cfg_color("color_active", &look.active);
  cfg_color("color_text", &look.text);
  cfg_color("color_text_active", &look.text_active);
  cfg_color("color_copyright", &look.copyright);
  cfg_bool("time", &look.time);
  cfg_bool("date", &look.date);
  cfg_bool("char", &look.chr);
  char v[8];
  if (cfg_get("ft812", "mouse_speed", v, sizeof(v)) && v[0] >= '1' && v[0] <= '5') look.mouse_speed = v[0] - '0';
  cfg_bool("click", &look.click);
  if (cfg_get("ft812", "sound", v, sizeof(v)) && v[0] >= '0' && v[0] <= '4') look.sound = v[0] - '0';
}

// the palette of the look
static ramp_t r_text, r_copy, r_btn, r_act, r_txt_btn, r_txt_act, r_white, r_txt_on_act;
static uint8_t c_black, c_white;
static uint8_t logo_map[LOGO_COLORS];

static void make_palette(void) {
  gfx_reset_palette();
  r_text = gfx_ramp_alpha(look.text);
  r_copy = gfx_ramp_alpha(look.copyright);
  r_btn = gfx_ramp_alpha(look.button);
  r_act = gfx_ramp_alpha(look.active);
  r_txt_btn = gfx_ramp_over(look.text, look.button);
  r_txt_act = gfx_ramp_over(look.text_active, look.active);
  r_white = gfx_ramp_alpha(0xffffff);
  r_txt_on_act = gfx_ramp_over(look.text, look.active);  // the gauge hand, the S of a focused Setup
  c_black = gfx_color(0x000000);
  c_white = gfx_color(0xffffff);
  logo_map[0] = 0;
  for (int i = 1; i < LOGO_COLORS; i++) logo_map[i] = gfx_color(logo_pal[i]);
  R_GRAD_TOP = look.gradient;
  R_GRAD_BOT = look.bg;
}

// ---- the core list ----

#define MAX_CORES 128
typedef struct { char file[33], name[33]; } core_t;
static core_t cores[MAX_CORES];
static int ncores = 0;
static char autoload_file[33];
static int autoload_s = 0;
static char fw_build[9] = "";

static void copy_str(char *dst, int max, const uint8_t **p, const uint8_t *end) {
  int i = 0;
  while (*p < end && **p) { if (i < max - 1) dst[i++] = (char) **p; (*p)++; }
  dst[i] = 0;
  if (*p < end) (*p)++;
}

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
    char build[9], id[33], flags[4];
    copy_str(c->file, 33, &p, end); copy_str(c->name, 33, &p, end); copy_str(build, 9, &p, end);
    copy_str(id, 33, &p, end); copy_str(flags, 4, &p, end);
    int f = 0;
    for (char *q = flags; *q; q++) f = f * 10 + (*q - '0');
    if (f & 1) ncores++;  // visible ones only
  }
  if (rpc_call(RPC_CORES, 3, 0, 0, buf, sizeof(buf), &n, 1000) == RPC_OK && n >= 2) {
    autoload_s = buf[0];
    const uint8_t *p = buf + 1;
    copy_str(autoload_file, 33, &p, buf + n);
  }
  if (rpc_call(RPC_SYS, 0, 0, 0, buf, sizeof(buf), &n, 1000) == RPC_OK && n >= 8) {
    for (int i = 0; i < 8; i++) fw_build[i] = (char) buf[i];
    fw_build[8] = 0;
  }
}

// ---- the screen (the geometry of app_core_browser_ft_menu) ----

#define CORE_TOP 88
#define CORE_STEP 40
#define PAGE ((FB_H - 60 - CORE_TOP) / CORE_STEP)   // 11
#define BX (FB_W / 4 + 8)
#define BW (FB_W / 2 - 16)
#define BH 32

static int sel = 0;
static int autoload_left = -1;
static int hover = -1;          // the area under the pointer: a core index, or one of HIT_*
enum { HIT_NONE = -1, HIT_PREV = 1000, HIT_NEXT, HIT_SETUP };
static int page_of(int i) { return i / PAGE; }
static int pages(void) { return ncores ? (ncores + PAGE - 1) / PAGE : 1; }

static void draw_button(int i) {
  int by = CORE_TOP + (i % PAGE) * CORE_STEP;
  int on = i == sel;
  gfx_clear(BX - 3, by - 3, BW + 6, BH + 6);
  // hovered: a frame of 2 px in the active colour around the button (the FT812 menu)
  if (hover == i) gfx_round_rect(BX - 2, by - 2, BW + 4, BH + 4, 6, r_act);
  gfx_round_rect(BX, by, BW, BH, 4, on ? r_act : r_btn);
  char name[34];
  int k = 0;
  for (; cores[i].name[k] && k < 33; k++) name[k] = cores[i].name[k];
  name[k] = 0;
  gfx_text_center(&f28, BX + BW / 2, by + BH / 2, name, on ? r_txt_act : r_txt_btn);
  // the autoload gauge: the seconds left of the countdown, on the left of the selected button
  if (on && autoload_left >= 0 && autoload_s > 0) {
    int cx = BX + 20, cy = by + BH / 2;
    gfx_ring(cx, cy, 12, r_txt_on_act);
    gfx_hand(cx, cy, 8, 225 + 270 * autoload_left / autoload_s, r_txt_on_act);  // as ft_draw_gauge: the seconds left
  }
}

static void draw_footer(void) {
  char b[48], *p;
  gfx_clear(240, FB_H - 56, 320, 52);
  p = str_cpy(b, "Page "); p = u2s(p, page_of(sel) + 1, 1); p = str_cpy(p, " of "); u2s(p, pages(), 1);
  gfx_text_center(&f27, FB_W / 2, FB_H - 40, b, r_copy);
  if (pages() > 1) {
    gfx_text_center(&f28, FB_W / 2 - 80, FB_H - 40, "<", hover == HIT_PREV ? r_text : r_copy);
    gfx_text_center(&f28, FB_W / 2 + 80, FB_H - 40, ">", hover == HIT_NEXT ? r_text : r_copy);
  }
  if (autoload_left >= 0) { p = str_cpy(b, "Autoloading core in "); p = u2s(p, autoload_left, 1); str_cpy(p, " s"); }
  else str_cpy(b, "Autoload disabled");
  gfx_text_center(&f27, FB_W / 2, FB_H - 20, b, r_copy);
}

static void draw_page(void) {
  gfx_clear(BX - 4, CORE_TOP - 4, BW + 8, PAGE * CORE_STEP + 4);
  int from = page_of(sel) * PAGE;
  for (int i = from; i < from + PAGE && i < ncores; i++) draw_button(i);
  if (!ncores) gfx_text_center(&f28, FB_W / 2, CORE_TOP + BH / 2, "No cores on the SD card", r_text);
  draw_footer();
}

static void draw_clock(void) {
  uint16_t n;
  if (!look.time && !look.date) return;
  if (rpc_call(RPC_SYS, 1, 0, 0, buf, sizeof(buf), &n, 500) != RPC_OK || n < 8) return;
  char t[12], *p;
  gfx_clear(FB_W - 176, 8, 170, 76);
  if (look.time) {
    p = u2s(t, buf[4], 2); *p++ = ':'; p = u2s(p, buf[5], 2); *p++ = ':'; u2s(p, buf[6], 2);
    gfx_text_center(&f30, FB_W - 88, 30, t, r_text);
  }
  if (look.date) {
    p = u2s(t, buf[3], 2); *p++ = '/'; p = u2s(p, buf[2], 2); *p++ = '/'; u2s(p, (buf[0] | (buf[1] << 8)) % 100, 2);
    gfx_text_center(&f30, FB_W - 88, 60, t, r_text);
  }
}

// Setup under the logo (a hover ring in the active colour)
static void draw_setup(void) {
  gfx_clear(14, 82, 40, 44);
  if (hover == HIT_SETUP) gfx_circle(36, 104, 20, r_act);
  gfx_circle(36, 104, 18, r_btn);
  gfx_text_center(&f29, 36, 104, "S", r_txt_btn);
  gfx_clear(60, 90, 80, 30);
  gfx_text(&f28, 62, 104 - f28.line / 2, "Setup", r_text);
}

static void draw_static(void) {
  gfx_clear(0, 0, FB_W, FB_H);
  gfx_indexed(8, 8, LOGO_W, LOGO_H, 4, logo_px, logo_map);   // the logo, x4
  draw_setup();
  // the build and the copyright
  char b[40], *p = str_cpy(b, "FW build: ");
  str_cpy(p, fw_build);
  gfx_text_center(&f27, 112, FB_H - 40, b, r_copy);
  gfx_text_center(&f27, 112, FB_H - 20, "(c) 2026 andykarpov", r_copy);
  if (look.chr) gfx_coverage(FB_W - CHAR_W - 8, FB_H - CHAR_H - 8, CHAR_W, CHAR_H, char_cov, r_white);
  draw_clock();
  draw_page();
}

// ---- the mouse (firmware ui/ft_mouse.c: the pointer, hidden after 5 s, a click selects, a click
// on the selected core starts it) ----

static const char *const pointer_art[17] = {
  "X               ", "XX              ", "X.X             ", "X..X            ", "X...X           ",
  "X....X          ", "X.....X         ", "X......X        ", "X.......X       ", "X........X      ",
  "X.....XXXXX     ", "X..X..X         ", "X.X X..X        ", "XX  X..X        ", "X    X..X       ",
  "     X..X       ", "      XX        ",
};
static int mx = FB_W / 2, my = FB_H / 2, m_visible = 0, m_rx = 0, m_ry = 0;
static uint8_t m_buttons = 0;
static uint32_t m_idle = 0;

static int hit_test(int x, int y) {
  int from = page_of(sel) * PAGE;
  for (int i = from; i < from + PAGE && i < ncores; i++) {
    int by = CORE_TOP + (i % PAGE) * CORE_STEP;
    if (x >= BX && x < BX + BW && y >= by && y < by + BH) return i;
  }
  if (pages() > 1 && y >= FB_H - 54 && y < FB_H - 26) {
    if (x >= FB_W / 2 - 96 && x < FB_W / 2 - 64) return HIT_PREV;
    if (x >= FB_W / 2 + 64 && x < FB_W / 2 + 96) return HIT_NEXT;
  }
  if (x >= 14 && x < 134 && y >= 82 && y < 126) return HIT_SETUP;
  return HIT_NONE;
}

static void redraw_hit(int h) {
  if (h == HIT_NONE) return;
  if (h == HIT_SETUP) draw_setup();
  else if (h == HIT_PREV || h == HIT_NEXT) draw_footer();
  else if (h >= 0 && h < ncores && page_of(h) == page_of(sel)) draw_button(h);
}

static void pointer_show(void) {
  if (m_visible) gfx_sprite_show(mx, my, pointer_art, 16, 17, c_black, c_white);
}

static void set_hover(int h) {
  if (h == hover) return;
  int old = hover;
  hover = h;
  redraw_hit(old);
  redraw_hit(h);
}

static void move_to(int n);
static void launch(int i);

static void mouse_poll(void) {
  uint32_t m = R_MOUSE;
  int dx = (int) (m << 20) >> 20, dy = (int) (m << 8) >> 20;
  uint8_t b = (uint8_t) (m >> 24);
  static const int mul[6] = { 4, 2, 3, 4, 6, 8 };  // speed 1..5: x0.5 .. x2
  if (dx || dy) {
    m_rx += dx * mul[look.mouse_speed]; m_ry += dy * mul[look.mouse_speed];
    int nx = mx + m_rx / 4, ny = my + m_ry / 4;
    m_rx %= 4; m_ry %= 4;
    if (nx < 0) nx = 0;
    if (nx > FB_W - 1) nx = FB_W - 1;
    if (ny < 0) ny = 0;
    if (ny > FB_H - 1) ny = FB_H - 1;
    gfx_sprite_hide();
    mx = nx; my = ny;
    m_visible = 1;
    m_idle = R_MS;
    set_hover(hit_test(mx, my));
    pointer_show();
  }
  if ((b & 1) && !(m_buttons & 1)) {
    m_idle = R_MS;
    if (!m_visible) { m_visible = 1; pointer_show(); }    // a click while hidden only shows it
    else {
      int h = hit_test(mx, my);
      gfx_sprite_hide();
      if (h >= 0) { if (h == sel) launch(h); else move_to(h); }
      else if (h == HIT_PREV) move_to(sel - PAGE);
      else if (h == HIT_NEXT) move_to(sel + PAGE);
      set_hover(hit_test(mx, my));
      pointer_show();
    }
  }
  m_buttons = b;
  if (m_visible && R_MS - m_idle > 5000) {
    gfx_sprite_hide();
    m_visible = 0;
    set_hover(HIT_NONE);
  }
}

// ---- keys ----

enum { K_ENTER = 0x28, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52,
       K_PGUP = 0x4B, K_PGDN = 0x4E, K_HOME = 0x4A, K_END = 0x4D };

static int key_poll(void) {
  static uint8_t last = 0;
  static uint32_t t_down = 0, t_rep = 0;
  uint8_t k = (uint8_t) (R_KBD0 >> 8);
  uint32_t now = R_MS;
  if (k != last) { last = k; t_down = t_rep = now; return k; }
  if (k && now - t_down > 400 && now - t_rep > 60) { t_rep = now; return k; }
  return 0;
}

static void move_to(int n) {
  if (n < 0) n = 0;
  if (n >= ncores) n = ncores - 1;
  if (n < 0 || n == sel) return;
  int old = sel;
  sel = n;
  if (look.click) midi_click();
  if (page_of(old) != page_of(sel)) draw_page();
  else { draw_button(old); draw_button(sel); draw_footer(); }
}

static void launch(int i) {
  // the start sound of [ft812] sound (1..4, 0 none), to its end, as the FT812 menu
  if (look.sound >= 1 && look.sound <= 4) {
    dbg_puts("menu: start sound\n");
    snd_play(0, &sounds[look.sound - 1], 255);
    uint32_t t0 = R_MS;
    while (snd_playing(0) && R_MS - t0 < 4000) ;
  }
  dbg_puts("menu: launch "); dbg_puts(cores[i].file); dbg_puts("\n");
  rpc_launch(cores[i].file);
}

int main(void) {
  dbg_puts("menu: start\n");
  // a check of the mailbox: the request buffer written and read back
  for (int i = 0; i < 16; i++) RPC(RPC_REQ + i) = (uint8_t) (0xA5 ^ i);
  int bad = 0;
  for (int i = 0; i < 16; i++) if (RPC(RPC_REQ + i) != (uint8_t) (0xA5 ^ i)) bad++;
  dbg_puts(bad ? "menu: rpc buffer BAD\n" : "menu: rpc buffer ok\n");
  gfx_init();
  uint32_t t0 = R_MS;
  while (!rpc_enabled() && R_MS - t0 < 2000) ;
  if (rpc_enabled()) { read_look(); read_cores(); dbg_puts("menu: cores read\n"); }
  else dbg_puts("menu: no RPC\n");
  make_palette();
  for (int i = 0; i < ncores; i++)
    if (autoload_s && str_eq(cores[i].file, autoload_file)) { sel = i; autoload_left = autoload_s; }
  uint32_t t_draw = R_US;
  draw_static();
  t_draw = R_US - t_draw;
  { char b[40], *p = str_cpy(b, "menu: drawn in "); p = u2s(p, t_draw, 1); str_cpy(p, " us\n"); dbg_puts(b); }
  R_FB = 0;
  R_SHOW = 1;
  uint32_t t_auto = R_MS, t_clock = R_MS;

  for (;;) {
    mouse_poll();
    int k = key_poll();
    if (k) {
      gfx_sprite_hide();
      if (autoload_left >= 0) { autoload_left = -1; gfx_sprite_hide(); draw_footer(); draw_button(sel); }
      if (k == K_UP) move_to(sel - 1);
      else if (k == K_DOWN) move_to(sel + 1);
      else if (k == K_LEFT || k == K_PGUP) move_to(sel - PAGE);
      else if (k == K_RIGHT || k == K_PGDN) move_to(sel + PAGE);
      else if (k == K_HOME) move_to(0);
      else if (k == K_END) move_to(ncores - 1);
      else if (k == K_ENTER && ncores) launch(sel);
      pointer_show();
    }
    if (autoload_left >= 0) {
      int left = autoload_s - (int) ((R_MS - t_auto) / 1000);
      if (left < 0) launch(sel);
      if (left != autoload_left) {
        autoload_left = left;
        gfx_sprite_hide();
        draw_footer();
        draw_button(sel);   // the gauge
        pointer_show();
      }
    }
    if (R_MS - t_clock >= 1000) { t_clock = R_MS; gfx_sprite_hide(); draw_clock(); pointer_show(); }
  }
}
