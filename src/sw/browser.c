// The core list (firmware app_core_browser_ft_menu: its geometry at 800 x 600, the [ft812] colours
// of karabas.ini): the logo, the clock, Setup, the core buttons of a page, the footer and the
// character over the gradient of menu_compose. The keys, the pads, the mouse; the autoload.
// The groups (docs/PLAN.md Ф6): the list shows the groups and the cores of no group, or the cores of
// the group open (its first button goes back); G / the right button moves a core to another group.
#include "browser.h"
#include "setup.h"
#include "setup_ui.h"
#include "settings.h"
#include "groups.h"
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
int msc_mode = 0;     // SYS INFO: the USB drive mode, 0 none, 1 the SD card, 2 the second one, 3 it is not found
char fw_build[9] = "";
int hw_id = 0;       // SYS INFO: the board (1 Go, 2 Mini, 3 Mini G)
// the list shown: a core (its index in cores[]), E_BACK (the button out of the group open) or a group
#define E_BACK (-1)
#define E_GROUP(g) (-2 - (g))
#define E_GROUP_OF(e) (-2 - (e))
static int vis[MAX_CORES + MAX_GROUPS + 1], nvis = 0;
static int open_group = -1;      // the group shown (group_names), -1 the top of the list
static void view_build(void);

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
    c->visible = f & 1;            // visible ones only in the list
    c->type = (uint8_t) (f >> 1);
    c->group[0] = 0;
    ncores++;
  }
  if (ncores) groups_read();
  if (rpc_call(RPC_CORES, 3, 0, 0, buf, sizeof(buf), &n, 1000) == RPC_OK && n >= 2) {
    autoload_s = buf[0];
    const uint8_t *p = buf + 1;
    copy_str(autoload_file, 33, &p, buf + n);
  }
  if (rpc_call(RPC_SYS, 0, 0, 0, buf, sizeof(buf), &n, 1000) == RPC_OK && n >= 9) {
    for (int i = 0; i < 8; i++) fw_build[i] = (char) buf[i];
    fw_build[8] = 0;
    hw_id = buf[8];
    if (n >= 13) msc_mode = buf[12];
  }
  open_group = -1;
  view_build();
}

// ---- the view: the groups or the cores of one ----

static int grouped(void) { return set_val[S_GROUPS] != 0; }
// the group a core is shown in: a group of one core shows the core itself
static int shown_group(int c) {
  int g = grouped() ? group_of(c) : -1;
  return g >= 0 && group_count[g] > 1 ? g : -1;
}

static void view_build(void) {
  nvis = 0;
  if (open_group >= ngroups || (open_group >= 0 && (!grouped() || group_count[open_group] < 2))) open_group = -1;
  if (open_group >= 0) {
    vis[nvis++] = E_BACK;
    for (int i = 0; i < ncores; i++) if (cores[i].visible && shown_group(i) == open_group) vis[nvis++] = i;
    return;
  }
  if (grouped()) for (int g = 0; g < ngroups; g++) if (group_count[g] > 1) vis[nvis++] = E_GROUP(g);
  for (int i = 0; i < ncores; i++) if (cores[i].visible && shown_group(i) < 0) vis[nvis++] = i;
}

static int view_find(int e) {
  for (int i = 0; i < nvis; i++) if (vis[i] == e) return i;
  return -1;
}

static int core_of_file(const char *file) {
  for (int i = 0; i < ncores; i++) if (cores[i].visible && file[0] && str_eq(cores[i].file, file)) return i;
  return -1;
}

// ---- the screen (the geometry of app_core_browser_ft_menu) ----

#define CORE_TOP 88
#define CORE_STEP 40
#define PAGE ((FB_H - 60 - CORE_TOP) / CORE_STEP)   // 11 in 800 x 600, 8 in 640 x 480
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

// a folder on the left of the button of a group
static void draw_folder(int x, int cy, ramp_t r) {
  gfx_round_rect(x, cy - 8, 9, 5, 1, r);
  gfx_round_rect(x, cy - 5, 20, 13, 2, r);
}

// an arrow to the left in the place of the folder: the button out of the group open
static void draw_back(int x, int cy, ramp_t r) {
  gfx_hand(x, cy, 20, 90, r);
  gfx_hand(x, cy, 8, 45, r);
  gfx_hand(x, cy, 8, 135, r);
}

static void draw_button(int i) {
  int by = CORE_TOP + (i % PAGE) * CORE_STEP, cy = by + BH / 2;
  int on = i == sel && !setup_focus, e = vis[i];
  uint32_t bg = on ? look.active : look.button;
  ramp_t r = on ? gfx_ramp_over(look.text_active, look.active) : gfx_ramp_over(look.text, look.button);
  gfx_clear(BX - 3, by - 3, BW + 6, BH + 6);
  // hovered: a frame of 2 px in the active colour around the button (the FT812 menu)
  if (hover == i) gfx_round_rect(BX - 2, by - 2, BW + 4, BH + 4, 6, gfx_ramp_alpha(look.active));
  gfx_round_rect(BX, by, BW, BH, 4, gfx_ramp_alpha(bg));
  if (e >= 0) gfx_text_center(&f27, BX + BW / 2, cy, cores[e].name, r);
  else if (e == E_BACK) {
    draw_back(BX + 14, cy, r);
    gfx_text_center(&f27, BX + BW / 2, cy, group_names[open_group], r);
  } else {
    int g = E_GROUP_OF(e);
    char b[8];
    draw_folder(BX + 14, cy, r);
    gfx_text_center(&f27, BX + BW / 2, cy, group_names[g], r);
    u2s(b, (uint32_t) group_count[g], 1);
    gfx_text_right(&f27, BX + BW - 14, cy, b, on ? r : gfx_ramp_over(look.copyright, bg));
  }
  // the autoload gauge: the seconds left of the countdown, on the left of the selected button
  if (on && autoload_left >= 0 && autoload_s > 0) {
    int cx = BX + 20, cy = by + BH / 2;
    ramp_t r = gfx_ramp_over(look.text, look.active);
    gfx_ring(cx, cy, 12, r);
    gfx_hand(cx, cy, 8, 225 + 270 * autoload_left / autoload_s, r);  // as ft_draw_gauge: the seconds left
  }
}

// the hover frame only: around the button, its middle (the text) kept, so it does not blink
static void draw_hover(int i) {
  int by = CORE_TOP + (i % PAGE) * CORE_STEP;
  int on = i == sel && !setup_focus;
  gfx_keep(BX + 6, by, BW - 12, BH);
  gfx_clear(BX - 3, by - 3, BW + 6, BH + 6);
  if (hover == i) gfx_round_rect(BX - 2, by - 2, BW + 4, BH + 4, 6, gfx_ramp_alpha(look.active));
  gfx_round_rect(BX, by, BW, BH, 4, gfx_ramp_alpha(on ? look.active : look.button));
  gfx_keep_off();
}

// the arrows of the pager: < at FB_W / 2 - 80, > at FB_W / 2 + 80
static void draw_arrow(int next) {
  int cx = FB_W / 2 + (next ? 80 : -80);
  gfx_clear(cx - 16, FB_H - 54, 32, 28);
  if (pages() > 1)
    gfx_text_center(&f27, cx, FB_H - 40, next ? ">" : "<",
                    hover == (next ? HIT_NEXT : HIT_PREV) ? gfx_ramp_alpha(look.text) : gfx_ramp_alpha(look.copyright));
}

static void draw_footer(void) {
  char b[48], *p;
  ramp_t copy = gfx_ramp_alpha(look.copyright);
  gfx_clear(FB_W / 2 - 124, FB_H - 56, 248, 52);   // the pager and the autoload / hint line, not the build on the left
  p = str_cpy(b, "Page "); p = u2s(p, page_of(sel) + 1, 1); p = str_cpy(p, " of "); u2s(p, pages(), 1);
  gfx_text_center(&f27, FB_W / 2, FB_H - 40, b, copy);
  draw_arrow(0);
  draw_arrow(1);
  if (autoload_left >= 0) { p = str_cpy(b, "Autoloading core in "); p = u2s(p, autoload_left, 1); str_cpy(p, " s"); }
  // no autoload: with the groups, what the keys do on the entry selected (the move is not seen elsewhere)
  else if (!grouped() || !nvis) str_cpy(b, "Autoload disabled");
  else if (vis[sel] >= 0) str_cpy(b, "G / right click: move to a group");
  else if (vis[sel] == E_BACK) str_cpy(b, "Enter / Esc: back");
  else str_cpy(b, "Enter: open the group");
  gfx_text_center(&f27, FB_W / 2, FB_H - 20, b, copy);
}

static void draw_page(void) {
  gfx_clear(BX - 4, CORE_TOP - 4, BW + 8, PAGE * CORE_STEP + 4);
  int from = page_of(sel) * PAGE;
  for (int i = from; i < from + PAGE && i < nvis; i++) draw_button(i);
  if (!nvis) gfx_text_center(&f27, FB_W / 2, CORE_TOP + BH / 2,
                             cores_busy ? "The SD card is a USB drive of the PC" : "No cores on the SD card",
                             gfx_ramp_alpha(look.text));
  draw_footer();
}

// Setup under the logo (a hover ring in the active colour)
static void draw_setup_ring(void) {
  uint32_t c = setup_focus ? look.active : look.button;
  gfx_clear(12, 80, 48, 48);
  if (hover == HIT_SETUP) gfx_circle(36, 104, 20, gfx_ramp_alpha(look.active));
  gfx_circle(36, 104, 18, gfx_ramp_alpha(c));
}

static void draw_setup(void) {
  uint32_t c = setup_focus ? look.active : look.button;
  draw_setup_ring();
  gfx_text_center(&f27, 36, 104, "S", gfx_ramp_over(setup_focus ? look.text_active : look.text, c));
  gfx_clear(60, 90, 80, 30);
  gfx_text(&f27, 62, 104 - f27.line / 2, "Setup", gfx_ramp_alpha(look.text));
}

static void draw_move(void);
static int mv_core = -1;         // the core of the move dialog, -1: none

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
  if (mv_core >= 0) draw_move();
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
  if (h == HIT_SETUP) { gfx_keep(24, 92, 24, 24); draw_setup_ring(); gfx_keep_off(); }   // the S kept
  else if (h == HIT_PREV || h == HIT_NEXT) draw_arrow(h == HIT_NEXT);
  else if (h >= 0 && h < nvis && page_of(h) == page_of(sel)) draw_hover(h);
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

// the core started last ([autoload] last_core of karabas.ini, its file name): the selected one at the start
// when there is no autoload; written by the launch of another one (not a boot or hidden core)
#define LAST_SECTION "autoload"
#define LAST_KEY "last_core"
static char last_file[33] = "";

static void last_read(void) {
  static const char req[] = "0\0" LAST_SECTION "\0" LAST_KEY;   // with its NUL at the end
  uint16_t n;
  last_file[0] = 0;
  if (rpc_call(RPC_CFG, 0, req, sizeof(req), buf, sizeof(buf), &n, 1000) != RPC_OK || n < 2 || !buf[0]) return;
  const uint8_t *p = buf + 1;
  copy_str(last_file, 33, &p, buf + n);
}

static void last_write(const core_t *c) {
  if (!c->visible || c->type == 0 || c->type == 255 || str_eq(c->file, last_file)) return;
  char req[64], *p = req;
  p = str_cpy(p, "0") + 1; p = str_cpy(p, LAST_SECTION) + 1; p = str_cpy(p, LAST_KEY) + 1; p = str_cpy(p, c->file) + 1;
  uint16_t n;
  int st = rpc_call(RPC_CFG, 1, req, (uint16_t) (p - req), buf, sizeof(buf), &n, 2000);   // written at once
  dbg_puts(st == RPC_OK ? "menu: last core saved\n" : "menu: last core not saved\n");
}

static void launch(int c) {
  // the start melody of [ft812] sound (MIDI), to its end; then 10 ms for the last bytes to leave the
  // fifo of menu_midi_mux (31250 baud: 1 ms a byte), the CPU stops at the launch
  if (look.sound) {
    dbg_puts("menu: start melody\n");
    melody_start();
    while (melody_poll()) ;
    uint32_t t0 = R_MS;
    while (R_MS - t0 < 10) ;
  }
  last_write(&cores[c]);
  dbg_puts("menu: launch "); dbg_puts(cores[c].file); dbg_puts("\n");
  rpc_launch(cores[c].file);
}

// the core selected: its group open
static void select_core(int c) {
  open_group = c >= 0 ? shown_group(c) : -1;
  view_build();
  int i = c >= 0 ? view_find(c) : -1;
  sel = i >= 0 ? i : 0;
}

static void open_entry(int g) {
  open_group = g;
  view_build();
  sel = nvis > 1 ? 1 : 0;
  ui_click();
  draw_all();
}

static void close_group(void) {
  int g = open_group;
  if (g < 0) return;
  open_group = -1;
  view_build();
  int i = view_find(E_GROUP(g));
  sel = i >= 0 ? i : 0;
  ui_click();
  draw_all();
}

// Enter on an entry: a core starts, a group opens, the back button closes the group
static void activate(int i) {
  if (i < 0 || i >= nvis) return;
  int e = vis[i];
  if (e >= 0) launch(e);
  else if (e == E_BACK) close_group();
  else open_entry(E_GROUP_OF(e));
}

static void stop_autoload(void) {
  if (autoload_left < 0) return;
  autoload_left = -1;
  draw_footer();
  draw_button(sel);
}

void browser_start(void) {
  if (rpc_enabled() && !cores_busy) last_read();
  int c = autoload_s ? core_of_file(autoload_file) : -1;
  if (c >= 0) { autoload_left = autoload_s; t_auto = R_MS; }
  // no autoload (off, its core not on the card, or a return to the menu): the core started last
  else c = core_of_file(last_file);
  select_core(c);
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

// ---- the move of a core to another group: G or the right button on a core ----
// the rows: + New group (a name typed in its place), No group, the groups; more groups than fit scroll,
// a chevron on the right says there are more above / below
enum { R_NEW, R_NONE, R_GROUP };
static int mv_row, mv_first, mv_edit, mv_hover = HIT_NONE;
static char mv_name[GROUP_LEN + 1];
#define MV_W 360
#define MV_STEP 36
#define MV_TOP 56                // the title
#define MV_BOTTOM 44             // the keys
#define MV_Y_MIN 88              // the clock ends at line 84 (draw_clock), the frame of the dialog is 3 px
#define HIT_MV 2000
#define HIT_MV_UP 1998
#define HIT_MV_DOWN 1999
static int mv_rows(void) { return ngroups + R_GROUP; }
static int mv_shown(void) { int n = (FB_H - 120 - MV_TOP - MV_BOTTOM) / MV_STEP; return n < mv_rows() ? n : mv_rows(); }
static int mv_cur(void) { return cores[mv_core].group[0] ? group_find(cores[mv_core].group) + R_GROUP : R_NONE; }

// a chevron 2 px wide: up (dir -1) or down (dir 1), its tip at (cx, cy + dir * 4)
static void chevron(int cx, int cy, int dir, ramp_t r) {
  int ty = cy + dir * 4;
  gfx_hand(cx, ty, 9, dir < 0 ? 135 : 45, r);
  gfx_hand(cx, ty, 9, dir < 0 ? 225 : 315, r);
}

static void draw_move(void) {
  int h = MV_TOP + mv_shown() * MV_STEP + MV_BOTTOM, x = (FB_W - MV_W) / 2, y = (FB_H - h) / 2;
  if (y < MV_Y_MIN) y = MV_Y_MIN;   // below the clock: its tick clears its place in the shown buffer
  char b[48];
  hit_clear();
  ft_rect(x - 3, y - 3, MV_W + 6, h + 6, 8, look.active, GFX_ALPHA);
  ft_rect(x, y, MV_W, h, 8, look.button, (int32_t) look.active);
  str_cpy(str_cpy(b, "Move "), cores[mv_core].name);
  gfx_text_center(&f27, x + MV_W / 2, y + 28, b, gfx_ramp_over(look.text, look.button));
  int cur = mv_cur(), rx = x + 28, rw = MV_W - 56;
  for (int k = 0; k < mv_shown(); k++) {
    int row = mv_first + k, ry = y + MV_TOP + k * MV_STEP, cy = ry + TAB_H / 2;
    int on = row == mv_row;
    // hovered: a frame of 2 px in the active colour, its middle in the colour of the dialog (the text is drawn for it)
    if (hit_add(rx, ry, rw, TAB_H, HIT_MV + row) && !on) {
      ft_rect(rx, ry, rw, TAB_H, 2, look.active, (int32_t) look.button);
      gfx_round_rect(rx, ry, rw, TAB_H, 4, gfx_ramp(look.button, (int32_t) look.active));
    }
    if (on) gfx_round_rect(rx, ry, rw, TAB_H, 4, gfx_ramp(look.active, (int32_t) look.button));
    uint32_t bg = on ? look.active : look.button;
    ramp_t t = gfx_ramp_over(on ? look.text_active : look.text, bg);
    if (row == R_NEW && mv_edit) {
      // the name typed: a field on the row, the cursor after the text
      int fx = rx + 30, fw = rw - 38;
      gfx_round_rect(fx, ry + 3, fw, TAB_H - 6, 3, gfx_ramp(look.bg, (int32_t) bg));
      ramp_t ft = gfx_ramp_over(look.text, look.bg);
      gfx_text_left(&f27, fx + 8, cy, mv_name, ft);
      int cx = fx + 9 + gfx_text_width(&f27, mv_name);
      gfx_fill(cx, ry + 8, 2, TAB_H - 16, gfx_color(CURSOR_COLOR));
    } else if (row == R_NEW) gfx_text_center(&f27, rx + rw / 2, cy, "New group", t);
    else gfx_text_center(&f27, rx + rw / 2, cy, row == R_NONE ? "No group" : group_names[row - R_GROUP], t);
    if (row == R_NEW) {          // a plus on the left
      gfx_hand(rx + 16, cy, 7, 90, t); gfx_hand(rx + 16, cy, 7, 270, t);
      gfx_hand(rx + 16, cy, 7, 0, t); gfx_hand(rx + 16, cy, 7, 180, t);
    }
    if (row == cur) gfx_circle(rx + 16, cy, 3, gfx_ramp_over(on ? look.text_active : look.active, bg));   // the group now
  }
  // more rows above / below: chevrons on the right of the first / last row (a click scrolls)
  ramp_t ch = gfx_ramp_over(look.text, look.button);
  int cx = x + MV_W - 14;
  if (mv_first > 0) { hit_add(cx - 12, y + MV_TOP, 24, TAB_H, HIT_MV_UP); chevron(cx, y + MV_TOP + TAB_H / 2, -1, ch); }
  if (mv_first + mv_shown() < mv_rows()) {
    int ly = y + MV_TOP + (mv_shown() - 1) * MV_STEP;
    hit_add(cx - 12, ly, 24, TAB_H, HIT_MV_DOWN);
    chevron(cx, ly + TAB_H / 2, 1, ch);
  }
  const char *keys = mv_edit ? "Type a name   Enter: create   Esc: back" :
                     mv_row == R_NEW ? "Enter: a new group   Esc: cancel" : "Up/Down: group   Enter: move   Esc: cancel";
  gfx_text_center(&f27, x + MV_W / 2, y + h - 22, keys, gfx_ramp_over(look.copyright, look.button));
}

static void mv_open(int i) {
  if (i < 0 || i >= nvis || vis[i] < 0 || !grouped()) return;
  stop_autoload();
  mv_core = vis[i];
  mv_edit = 0;
  mv_row = mv_cur();
  mv_first = 0;
  if (mv_row >= mv_shown()) mv_first = mv_row - mv_shown() + 1;
  ui_click();
  draw_all();
}

static void mv_close(void) { mv_core = -1; mv_edit = 0; mv_hover = HIT_NONE; draw_all(); }

static void mv_apply(const char *name) {
  int c = mv_core;
  ui_click();
  if (groups_set(c, name)) select_core(c);   // the core followed into its group
  mv_close();
}

static void mv_scroll(int first) {
  if (first > mv_rows() - mv_shown()) first = mv_rows() - mv_shown();
  if (first < 0) first = 0;
  mv_first = first;
}

static void mv_move(int row) {
  if (row < 0) row = 0;
  if (row >= mv_rows()) row = mv_rows() - 1;
  if (row == mv_row && !mv_edit) return;
  mv_row = row;
  mv_edit = 0;
  if (mv_row < mv_first) mv_scroll(mv_row);
  if (mv_row >= mv_first + mv_shown()) mv_scroll(mv_row - mv_shown() + 1);
  ui_click();
  draw_all();
}

static void mv_enter(void) {
  if (mv_row == R_NONE) mv_apply("");
  else if (mv_row >= R_GROUP) mv_apply(group_names[mv_row - R_GROUP]);
  else if (!mv_edit) { mv_edit = 1; mv_name[0] = 0; ui_click(); draw_all(); }
  else if (mv_name[0]) mv_apply(mv_name);
}

static void mv_key(const key_ev_t *e) {
  if (mv_edit) {
    int n = str_len(mv_name);
    if (e->key == K_ESC) { mv_edit = 0; ui_click(); draw_all(); }
    else if (e->key == K_ENTER) mv_enter();
    else if (e->key == K_BKSP) { if (n) { mv_name[n - 1] = 0; ui_click(); draw_all(); } }
    // a name of the ini: no = (the value is after it), no [ ] ; # (a section, a comment)
    else if (e->ch >= ' ' && e->ch <= '~' && e->ch != '=' && e->ch != '[' && e->ch != ']' && e->ch != ';' &&
             e->ch != '#' && n < GROUP_LEN && (n || e->ch != ' ')) {
      mv_name[n] = e->ch; mv_name[n + 1] = 0; ui_click(); draw_all();
    }
    else if (e->key == K_DOWN) mv_move(mv_row + 1);
    return;
  }
  switch (e->key) {
    case K_UP: mv_move(mv_row - 1); break;
    case K_DOWN: mv_move(mv_row + 1); break;
    case K_PGUP: mv_move(mv_row - mv_shown()); break;
    case K_PGDN: mv_move(mv_row + mv_shown()); break;
    case K_HOME: mv_move(0); break;
    case K_END: mv_move(mv_rows() - 1); break;
    case K_ENTER: mv_enter(); break;
    case K_ESC: case K_BKSP: ui_click(); mv_close(); break;
    default: if (e->ch == 'g' || e->ch == 'G') { ui_click(); mv_close(); }
  }
}

static void mv_mouse(int ev) {
  int h = ptr.visible ? hit_at(ptr.x, ptr.y) : HIT_NONE;
  if (ev & M_RIGHT) { ui_click(); mv_close(); return; }
  if ((ev & M_LEFT) && (h == HIT_MV_UP || h == HIT_MV_DOWN)) {
    mv_scroll(mv_first + (h == HIT_MV_UP ? -1 : 1));
    if (mv_row < mv_first) mv_row = mv_first;
    if (mv_row >= mv_first + mv_shown()) mv_row = mv_first + mv_shown() - 1;
    if (mv_row != R_NEW) mv_edit = 0;
    ui_click();
    draw_all();
    return;
  }
  if ((ev & M_LEFT) && h >= HIT_MV) {
    int row = h - HIT_MV;
    if (row != mv_row) { mv_row = row; mv_edit = 0; }
    mv_enter();
    return;
  }
  if (h != mv_hover) { mv_hover = h; draw_all(); }   // the hover frame of a row
}

void browser_key(const key_ev_t *e) {
  pointer_hide();
  stop_autoload();
  if (mv_core >= 0) { mv_key(e); pointer_show(); return; }
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
    case K_ENTER: activate(sel); break;
    case K_ESC: case K_BKSP: close_group(); break;
    case K_S: case K_PAD_START: ui_click(); setup_open(); return;
    default: if (e->ch == 'g' || e->ch == 'G') mv_open(sel);
  }
  pointer_show();
}

void browser_mouse(int ev) {
  if (!ev) return;
  pointer_hide();
  if (ev & (M_LEFT | M_RIGHT)) stop_autoload();
  if (mv_core >= 0) { mv_mouse(ev); pointer_show(); return; }
  if (ev & M_LEFT) {
    int h = hit_test(ptr.x, ptr.y);
    if (h >= 0 && h < nvis) { set_setup_focus(0); if (h == sel) activate(h); else move_to(h); }
    else if (h == HIT_PREV) { if (page_of(sel) > 0) move_to((page_of(sel) - 1) * PAGE); }
    else if (h == HIT_NEXT) { if (page_of(sel) + 1 < pages()) move_to((page_of(sel) + 1) * PAGE); }
    else if (h == HIT_SETUP) { setup_focus = 0; ui_click(); setup_open(); return; }
  } else if (ev & M_RIGHT) {
    // on a core: the move dialog; elsewhere in a group: back
    int h = hit_test(ptr.x, ptr.y);
    if (h >= 0 && h < nvis && vis[h] >= 0 && grouped()) { set_setup_focus(0); move_to(h); mv_open(h); pointer_show(); return; }
    close_group();
  }
  if (mv_core < 0) set_hover(ptr.visible ? hit_test(ptr.x, ptr.y) : HIT_NONE);
  pointer_show();
}

void browser_tick(void) {
  static uint32_t t_clock = 0;
  if (autoload_left >= 0) {
    int left = autoload_s - (int) ((R_MS - t_auto) / 1000);
    if (left < 0 && sel < nvis && vis[sel] >= 0) launch(vis[sel]);
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

// ---- the USB drive mode (MSC) ----

// the screen of app_msc_ft_draw of the FT812 menu: the background and the decor of the menu, the title and
// the lines of the firmware's msc screen, nothing to choose; the firmware reboots when the PC ejects the card
static const char *const msc_lines[3][6] = {
  { "The SD card is connected to the", "PC as a USB drive.", "", "Eject it on the PC to return", "to the menu.", "" },
  { "The second SD card (FPGA slot)", "is connected to the PC as a", "USB drive.", "", "Eject it on the PC to return",
    "to the menu." },
  { "The second SD card (FPGA slot)", "is not found.", "", "Back to the menu...", "", "" },
};

void browser_msc_draw(void) {
  int m = msc_mode >= 1 && msc_mode <= 3 ? msc_mode - 1 : 0;
  scr_begin();
  look_apply();
  draw_logo();
  draw_character();
  draw_clock();
  ramp_t t = gfx_ramp_alpha(look.text);
  int y = FB_H / 2 - 80;
  gfx_text_center(&f27, FB_W / 2, y, "USB mass storage", t);
  for (int i = 0; i < 6; i++) if (msc_lines[m][i][0]) gfx_text_center(&f27, FB_W / 2, y + 50 + i * 24, msc_lines[m][i], t);
  scr_end();
  dbg_puts("menu: msc screen\n");
}

void browser_msc(void) {
  browser_msc_draw();
  uint32_t t_clock = R_MS;
  for (;;) {
    key_ev_t e;
    mouse_next();
    key_next(&e);   // the keys do nothing here
    if (R_MS - t_clock >= 1000) { t_clock = R_MS; pointer_hide(); draw_clock(); pointer_show(); }
  }
}

// the list again after Setup (the sort or the visible cores may differ): the selected core kept
// (the groups may be on or off now): the selected core kept, or the group selected / open
void browser_reload(void) {
  char file[33] = "", group[GROUP_LEN + 1] = "";
  int e = nvis ? vis[sel] : E_BACK;
  if (e >= 0) str_cpy(file, cores[e].file);
  else if (e == E_BACK) str_cpy(group, group_names[open_group]);
  else str_cpy(group, group_names[E_GROUP_OF(e)]);
  cores_read();
  autoload_left = -1;
  int c = core_of_file(file);
  if (c >= 0) { select_core(c); return; }
  int g = group[0] ? group_find(group) : -1;
  open_group = -1;
  view_build();
  int i = g >= 0 ? view_find(E_GROUP(g)) : -1;
  sel = i >= 0 ? i : 0;
}
