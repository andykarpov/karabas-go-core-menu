// The core list (firmware app_core_browser_ft_menu: its geometry at 800 x 600, the [ft812] colours
// of karabas.ini): the logo, the clock, Setup, the core buttons of a page, the footer and the
// character over the gradient of menu_compose. The keys, the pads, the mouse; the autoload.
#include "browser.h"
#include "setup.h"
#include "cores.h"
#include "ui.h"
#include "mmio.h"
#include "rpc.h"
#include "sound.h"

core_t cores[MAX_CORES];
int ncores = 0;
char autoload_file[33];
int autoload_s = 0;
int cores_busy = 0;   // CORES answered busy: the cards are USB drives of the PC (MSC)
char fw_build[9] = "";
static int vis[MAX_CORES], nvis = 0;   // the visible cores: the list

static uint8_t buf[RPC_MAX];

static void copy_str(char *dst, int max, const uint8_t **p, const uint8_t *end) {
  int i = 0;
  while (*p < end && **p) { if (i < max - 1) dst[i++] = (char) **p; (*p)++; }
  dst[i] = 0;
  if (*p < end) (*p)++;
}

void cores_read(void) {
  uint16_t n;
  ncores = nvis = 0;
  autoload_s = 0;
  autoload_file[0] = 0;
  int st = rpc_call(RPC_CORES, 0, 0, 0, buf, 2, &n, 3000);
  cores_busy = st == RPC_BUSY;
  int count = 0;
  if (st == RPC_OK && n >= 2) count = buf[0] | (buf[1] << 8);
  else dbg_puts(cores_busy ? "menu: SCAN busy (MSC)\n" : "menu: SCAN failed\n");
  for (int i = 0; i < count && ncores < MAX_CORES; i++) {
    uint8_t idx[2] = { (uint8_t) i, (uint8_t) (i >> 8) };
    if (rpc_call(RPC_CORES, 1, idx, 2, buf, sizeof(buf), &n, 1000) != RPC_OK) continue;
    const uint8_t *p = buf, *end = buf + n;
    core_t *c = &cores[ncores];
    char build[9], flags[4];
    copy_str(c->file, 33, &p, end); copy_str(c->name, 33, &p, end); copy_str(build, 9, &p, end);
    copy_str(c->id, 33, &p, end); copy_str(flags, 4, &p, end);
    int f = 0;
    for (char *q = flags; *q; q++) f = f * 10 + (*q - '0');
    c->visible = f & 1;
    if (c->visible) vis[nvis++] = ncores;   // visible ones only in the list
    ncores++;
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
static uint32_t t_auto = 0;
static int hover = -1;          // the area under the pointer: a core index, or one of HIT_*
static int setup_focus = 0;     // the keys are on the Setup button (Left on the first core, as the FT812 menu)
enum { HIT_PREV = 1000, HIT_NEXT, HIT_SETUP };
static int page_of(int i) { return i / PAGE; }
static int pages(void) { return nvis ? (nvis + PAGE - 1) / PAGE : 1; }

static void draw_button(int i) {
  int by = CORE_TOP + (i % PAGE) * CORE_STEP;
  int on = i == sel && !setup_focus;
  gfx_clear(BX - 3, by - 3, BW + 6, BH + 6);
  // hovered: a frame of 2 px in the active colour around the button (the FT812 menu)
  if (hover == i) gfx_round_rect(BX - 2, by - 2, BW + 4, BH + 4, 6, gfx_ramp_alpha(look.active));
  gfx_round_rect(BX, by, BW, BH, 4, gfx_ramp_alpha(on ? look.active : look.button));
  gfx_text_center(&f28, BX + BW / 2, by + BH / 2, cores[vis[i]].name,
                  on ? gfx_ramp_over(look.text_active, look.active) : gfx_ramp_over(look.text, look.button));
  // the autoload gauge: the seconds left of the countdown, on the left of the selected button
  if (on && autoload_left >= 0 && autoload_s > 0) {
    int cx = BX + 20, cy = by + BH / 2;
    ramp_t r = gfx_ramp_over(look.text, look.active);
    gfx_ring(cx, cy, 12, r);
    gfx_hand(cx, cy, 8, 225 + 270 * autoload_left / autoload_s, r);  // as ft_draw_gauge: the seconds left
  }
}

static void draw_footer(void) {
  char b[48], *p;
  ramp_t copy = gfx_ramp_alpha(look.copyright);
  gfx_clear(240, FB_H - 56, 320, 52);
  p = str_cpy(b, "Page "); p = u2s(p, page_of(sel) + 1, 1); p = str_cpy(p, " of "); u2s(p, pages(), 1);
  gfx_text_center(&f27, FB_W / 2, FB_H - 40, b, copy);
  if (pages() > 1) {
    gfx_text_center(&f28, FB_W / 2 - 80, FB_H - 40, "<", hover == HIT_PREV ? gfx_ramp_alpha(look.text) : copy);
    gfx_text_center(&f28, FB_W / 2 + 80, FB_H - 40, ">", hover == HIT_NEXT ? gfx_ramp_alpha(look.text) : copy);
  }
  if (autoload_left >= 0) { p = str_cpy(b, "Autoloading core in "); p = u2s(p, autoload_left, 1); str_cpy(p, " s"); }
  else str_cpy(b, "Autoload disabled");
  gfx_text_center(&f27, FB_W / 2, FB_H - 20, b, copy);
}

static void draw_page(void) {
  gfx_clear(BX - 4, CORE_TOP - 4, BW + 8, PAGE * CORE_STEP + 4);
  int from = page_of(sel) * PAGE;
  for (int i = from; i < from + PAGE && i < nvis; i++) draw_button(i);
  if (!nvis) gfx_text_center(&f28, FB_W / 2, CORE_TOP + BH / 2,
                             cores_busy ? "The SD card is a USB drive of the PC" : "No cores on the SD card",
                             gfx_ramp_alpha(look.text));
  draw_footer();
}

// Setup under the logo (a hover ring in the active colour)
static void draw_setup(void) {
  gfx_clear(14, 82, 40, 44);
  if (hover == HIT_SETUP) gfx_circle(36, 104, 20, gfx_ramp_alpha(look.active));
  uint32_t c = setup_focus ? look.active : look.button;
  gfx_circle(36, 104, 18, gfx_ramp_alpha(c));
  gfx_text_center(&f29, 36, 104, "S", gfx_ramp_over(setup_focus ? look.text_active : look.text, c));
  gfx_clear(60, 90, 80, 30);
  gfx_text(&f28, 62, 104 - f28.line / 2, "Setup", gfx_ramp_alpha(look.text));
}

static void draw_all(void) {
  scr_begin();
  look_apply();
  draw_logo();
  draw_setup();
  // the build and the copyright
  char b[40], *p = str_cpy(b, "FW build: ");
  str_cpy(p, fw_build);
  ramp_t copy = gfx_ramp_alpha(look.copyright);
  gfx_text_center(&f27, 112, FB_H - 40, b, copy);
  gfx_text_center(&f27, 112, FB_H - 20, "(c) 2026 andykarpov", copy);
  draw_character();
  draw_clock();
  draw_page();
  scr_end();
  dbg_puts("menu: list drawn\n");
}

// ---- the mouse ----

static int hit_test(int x, int y) {
  int from = page_of(sel) * PAGE;
  for (int i = from; i < from + PAGE && i < nvis; i++) {
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
  else if (h >= 0 && h < nvis && page_of(h) == page_of(sel)) draw_button(h);
}

static void set_hover(int h) {
  if (h == hover) return;
  int old = hover;
  hover = h;
  redraw_hit(old);
  redraw_hit(h);
}

static void move_to(int n) {
  if (n < 0) n = 0;
  if (n >= nvis) n = nvis - 1;
  if (n < 0 || n == sel) return;
  int old = sel;
  sel = n;
  ui_click();
  // another page: the whole screen into the hidden buffer, then shown (cleared and drawn in the shown one,
  // the list blinked)
  if (page_of(old) != page_of(sel)) draw_all();
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
  dbg_puts("menu: launch "); dbg_puts(cores[vis[i]].file); dbg_puts("\n");
  rpc_launch(cores[vis[i]].file);
}

static void stop_autoload(void) {
  if (autoload_left < 0) return;
  autoload_left = -1;
  draw_footer();
  draw_button(sel);
}

void browser_start(void) {
  for (int i = 0; i < nvis; i++)
    if (autoload_s && str_eq(cores[vis[i]].file, autoload_file)) { sel = i; autoload_left = autoload_s; t_auto = R_MS; }
}

void browser_open(void) {
  if (sel >= nvis) sel = nvis ? nvis - 1 : 0;
  hover = ptr.visible ? hit_test(ptr.x, ptr.y) : HIT_NONE;
  draw_all();
}

// the keys on the Setup button: Enter opens it, Right / Up / Down go back to the cores
static void set_setup_focus(int on) {
  if (on == setup_focus) return;
  setup_focus = on;
  ui_click();
  draw_setup();
  if (nvis) draw_button(sel);
}

void browser_key(const key_ev_t *e) {
  pointer_hide();
  stop_autoload();
  if (setup_focus) {
    switch (e->key) {
      case K_ENTER: case K_S: case K_PAD_START: setup_focus = 0; ui_click(); setup_open(); return;
      case K_RIGHT: case K_UP: case K_DOWN: set_setup_focus(0); break;
    }
    pointer_show();
    return;
  }
  // on the first core (or with no cores): Left goes onto Setup, not by a held key
  if (e->key == K_LEFT && sel == 0) {
    if (!e->repeat) set_setup_focus(1);
    pointer_show();
    return;
  }
  switch (e->key) {
    case K_UP: move_to(sel - 1); break;
    case K_DOWN: move_to(sel + 1); break;
    case K_LEFT: case K_PGUP: move_to(sel - PAGE); break;
    case K_RIGHT: case K_PGDN: move_to(sel + PAGE); break;
    case K_HOME: move_to(0); break;
    case K_END: move_to(nvis - 1); break;
    case K_ENTER: if (nvis) launch(sel); break;
    case K_S: case K_PAD_START: ui_click(); setup_open(); return;
  }
  pointer_show();
}

void browser_mouse(int ev) {
  if (!ev) return;
  pointer_hide();
  if (ev & (M_LEFT | M_RIGHT)) stop_autoload();
  if (ev & M_LEFT) {
    int h = hit_test(ptr.x, ptr.y);
    if (h >= 0) { set_setup_focus(0); if (h == sel) launch(h); else move_to(h); }
    else if (h == HIT_PREV) move_to(sel - PAGE);
    else if (h == HIT_NEXT) move_to(sel + PAGE);
    else if (h == HIT_SETUP) { setup_focus = 0; ui_click(); setup_open(); return; }
  }
  set_hover(ptr.visible ? hit_test(ptr.x, ptr.y) : HIT_NONE);
  pointer_show();
}

void browser_tick(void) {
  static uint32_t t_clock = 0;
  if (autoload_left >= 0) {
    int left = autoload_s - (int) ((R_MS - t_auto) / 1000);
    if (left < 0) launch(sel);
    if (left != autoload_left) {
      autoload_left = left;
      pointer_hide();
      draw_footer();
      draw_button(sel);   // the gauge
      pointer_show();
    }
  }
  if (R_MS - t_clock >= 1000) { t_clock = R_MS; pointer_hide(); draw_clock(); pointer_show(); }
}

// the list again after Setup (the sort or the visible cores may differ): the selected core kept
void browser_reload(void) {
  char file[33];
  str_cpy(file, nvis ? cores[vis[sel]].file : "");
  cores_read();
  autoload_left = -1;
  sel = 0;
  for (int i = 0; i < nvis; i++) if (str_eq(cores[vis[i]].file, file)) sel = i;
}
