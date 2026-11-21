// Setup (firmware apps/app_ft_setup.c, its geometry at 800 x 600): the sections on the left, the
// settings of the chosen one on the right. Every change shows at once (the colours, the clock, the
// matrix), Save writes karabas.ini (CFG SET + SAVE), Cancel drops the changes (Esc asks first). The
// pages after the settings (Clock, Wi-Fi, Gamepad) do their work at once. The mouse: a click on a
// section opens it, on a setting picks it (and does Enter when it is picked), on a toggle or an
// arrow changes it; the sliders of a colour follow a drag; the right button is Esc.
#include "setup.h"
#include "setup_ui.h"
#include "esp.h"
#include "ntp.h"
#include "settings.h"
#include "browser.h"
#include "cores.h"
#include "ui.h"
#include "sound.h"
#include "mmio.h"
#include "rpc.h"

enum { T_SYSTEM, T_AUTOLOAD, T_DISPLAY, T_SOUND, T_THEME, T_PAGES, T_CLOCK = T_PAGES, T_WIFI, T_PAD, T_SAVE, T_CANCEL, T_COUNT };
enum { F_TABS, F_PAGE, F_COLOR, F_DIALOG, F_CUSTOM };
enum { HIT_TAB = 100, HIT_ROW = 200, HIT_TOGGLE = 300, HIT_LESS = 400, HIT_MORE = 500,
       HIT_CHAN = 600, HIT_SLIDER = 700, HIT_DLG_LEFT = 800, HIT_DLG_RIGHT, HIT_PANEL = 900 };

typedef struct { const char *name; const uint8_t *ids; uint8_t len; } page_t;
static const uint8_t page_system[] = { S_DEBUG, S_DVI, S_CORE_SORT, S_MATRIX, S_MATRIX_BRIGHT, S_MX_EFFECTS };
static const uint8_t page_autoload[] = { S_AUTOLOAD, S_TIMEOUT, S_CORE };
static const uint8_t page_display[] = { S_TIME, S_DATE, S_CHAR, S_MOUSE };
static const uint8_t page_sound[] = { S_SOUND, S_CLICK };
static const uint8_t page_theme[] = { S_THEME, S_C_BG, S_C_GRADIENT, S_C_BUTTON, S_C_ACTIVE, S_C_TEXT, S_C_TEXT_ACTIVE, S_C_COPYRIGHT };
#define PAGE(name, ids) { name, ids, sizeof(ids) }
static const page_t pages[T_PAGES] = {
  PAGE("System", page_system), PAGE("Autoload", page_autoload), PAGE("Display", page_display),
  PAGE("Sound", page_sound), PAGE("Theme", page_theme),
};
static const char *const tab_names[T_COUNT] = { "System", "Autoload", "Display", "Sound", "Theme", "Clock", "Wi-Fi", "Gamepad", "Save", "Cancel" };

#define TOP 88
#define TAB_X 16
#define TAB_W 160
#define TAB_STEP 40
#define TAB_GAP 12               // before Save
#define PANEL_X (TAB_X + TAB_W + 16)
#define PANEL_W 560
#define ROW_H 32
#define ROW_STEP 36
#define VALUE_W 200              // '<', the value of a choice, '>'
#define COLOR_STEP 8

int setup_active = 0;
static int focus, tab, row, mx_page, dialog_left, color_ch, dragging;
static uint32_t color_old;
static const char *error;
static int hover = HIT_NONE;     // under the pointer at the last picture

static const cpage_t *custom_page(void);
extern const cpage_t page_wifi, page_pad;   // wifi_page.c, pad_page.c

static const page_t *cur_page(void) {
  static const page_t mx = { "System > Matrix effects", settings_mx_ids, 8 };   // settings_mx_len
  return mx_page ? &mx : &pages[tab];
}
static int cur_id(void) { return cur_page()->ids[row]; }
static int tab_y(int i) { return TOP + i * TAB_STEP + (i >= T_SAVE ? TAB_GAP : 0); }

// ---- widgets (the FT812 commands of the firmware) ----

// ft_draw_rect: line width r, so the rectangle grows by r on each side
void ft_rect(int x, int y, int w, int h, int r, uint32_t c, int32_t surface) {
  gfx_round_rect(x - r, y - r, w + 2 * r, h + 2 * r, r, gfx_ramp(c, surface));
}
void button(int x, int y, int w, int h, const font_t *f, uint32_t fg, uint32_t bg, int32_t surface, const char *label) {
  gfx_round_rect(x, y, w, h, 4, gfx_ramp(bg, surface));
  gfx_text_center(f, x + w / 2, y + h / 2, label, gfx_ramp_over(fg, bg));
}
void hover_frame(int x, int y, int w, int h) {
  gfx_round_rect(x - 2, y - 2, w + 4, h + 4, 6, gfx_ramp_alpha(look.active));
}
void marker(int x, int y, int h, uint32_t surface) {
  gfx_circle(x + 9, y + h / 2, 3, gfx_ramp_over(look.active, surface));
}
static void toggle(int x, int cy, int w, int on, int32_t surface) {
  uint32_t bar = on ? look.active : look.copyright;
  gfx_pill(x, x + w, cy, 12, gfx_ramp(bar, surface));
  gfx_circle(on ? x + w : x, cy, 11, gfx_ramp_over(look.text, bar));
}
static void slider(int x, int cy, int w, uint32_t fill, int v, int range, int32_t surface) {
  int kx = x + v * w / range;
  gfx_pill(x, x + w, cy, 5, gfx_ramp(look.copyright, surface));
  if (kx > x) gfx_pill(x, kx, cy, 5, gfx_ramp(fill, surface));
  gfx_circle(kx, cy, 10, gfx_ramp(look.text, surface));
}
static void title(const char *t) {
  gfx_text_left(&f29, PANEL_X + 16, TOP + TAB_H / 2, t, gfx_ramp_alpha(look.text));
  gfx_fill(PANEL_X + 16, TOP + 34, PANEL_W - 32, 1, RAMP_FULL(gfx_ramp_alpha(look.copyright)));
  gfx_fill(PANEL_X + 16, TOP + 33, PANEL_W - 32, 1, gfx_ramp_alpha(look.copyright) + 6);
  gfx_fill(PANEL_X + 16, TOP + 35, PANEL_W - 32, 1, gfx_ramp_alpha(look.copyright) + 6);
}
// the row of a setting: focused (the marker) or hovered, on the button colour
int32_t row_back(int x, int y, int w, int h, int focused, int hovered) {
  if (!focused && !hovered) return GFX_ALPHA;
  ft_rect(x, y, w, h, 4, look.button, GFX_ALPHA);
  if (focused) marker(x, y, h, look.button);
  return (int32_t) look.button;
}

// ---- the picture ----

static void draw_tabs(void) {
  for (int i = 0; i < T_COUNT; i++) {
    int chosen = i == tab, lit = chosen && focus == F_TABS;
    if (hit_add(TAB_X, tab_y(i), TAB_W, TAB_H, HIT_TAB + i)) hover_frame(TAB_X, tab_y(i), TAB_W, TAB_H);
    button(TAB_X, tab_y(i), TAB_W, TAB_H, &f28, lit ? look.text_active : look.text, lit ? look.active : look.button,
           GFX_ALPHA, tab_names[i]);
    if (chosen && !lit) marker(TAB_X, tab_y(i), TAB_H, look.button);
  }
}

static void draw_value(int id, int y, int arrows, int32_t surf) {
  int right = PANEL_X + PANEL_W - 16, cy = y + ROW_H / 2;
  const setting_t *st = &settings[id];
  ramp_t text = gfx_ramp(look.text, surf);
  char v[40];
  if (st->kind == SK_BOOL) { toggle(right - 52, cy - 1, 40, set_val[id], surf); return; }
  if (st->kind == SK_COLOR) {
    settings_value_text(id, v, sizeof(v));
    gfx_solid_rect(right - 50, y + 4, 52, ROW_H - 8, gfx_color((uint32_t) set_val[id]));
    gfx_text_right(&f27, right - 60, cy, v, text);
    return;
  }
  if (st->kind == SK_PAGE) { gfx_text_center(&f28, right - 12, cy, ">", text); return; }
  settings_value_text(id, v, sizeof(v));
  // a long name is cut to fit between the arrows
  int n = str_len(v);
  while (n > 3 && gfx_text_width(&f28, v) > VALUE_W - 48) { v[--n] = 0; v[n - 1] = '.'; v[n - 2] = '.'; }
  gfx_text_center(&f28, right - VALUE_W / 2, cy, v, text);
  if (arrows) {
    gfx_text_center(&f28, right - VALUE_W + 12, cy, "<", text);
    gfx_text_center(&f28, right - 12, cy, ">", text);
  }
}

static void draw_page(void) {
  const page_t *p = cur_page();
  title(p->name);
  for (int i = 0; i < p->len; i++) {
    int y = TOP + 40 + i * ROW_STEP, right = PANEL_X + PANEL_W - 16;
    int kind = settings[p->ids[i]].kind;
    int focused = focus == F_PAGE && i == row, hovered = 0;
    // the toggle and the arrows before the row: the first area that holds the pointer wins
    if (kind == SK_BOOL) hovered |= hit_add(right - 64, y, 80, ROW_H, HIT_TOGGLE + i);
    else if (kind == SK_CHOICE || kind == SK_OTHER) {
      hovered |= hit_add(right - VALUE_W, y, 28, ROW_H, HIT_LESS + i);
      hovered |= hit_add(right - 28, y, 44, ROW_H, HIT_MORE + i);
    }
    hovered |= hit_add(PANEL_X, y, PANEL_W, ROW_H, HIT_ROW + i);
    int32_t surf = row_back(PANEL_X, y, PANEL_W, ROW_H, focused, hovered);
    gfx_text_left(&f28, PANEL_X + 16, y + ROW_H / 2, settings[p->ids[i]].label, gfx_ramp(look.text, surf));
    draw_value(p->ids[i], y, focused || hovered, surf);
  }
}

static void draw_color(void) {
  static const char *const names[3] = { "R", "G", "B" };
  static const uint32_t fills[3] = { 0xd02020, 0x20b020, 0x3050ff };
  int id = cur_id();
  uint32_t c = (uint32_t) set_val[id];
  char b[40];
  str_cpy(str_cpy(b, "Theme > "), settings[id].label);
  title(b);
  gfx_solid_rect(PANEL_X + 12, TOP + 44, 104, 72, gfx_color(c));
  hex6(b, c);
  gfx_text_left(&f30, PANEL_X + 128, TOP + 80, b, gfx_ramp_alpha(look.text));
  for (int i = 0; i < 3; i++) {
    int y = TOP + 128 + i * ROW_STEP, v = (c >> (16 - 8 * i)) & 0xff;
    int hovered = hit_add(PANEL_X + 40, y, PANEL_W - 48 - 56, ROW_H, HIT_SLIDER + i);
    hovered |= hit_add(PANEL_X, y, PANEL_W, ROW_H, HIT_CHAN + i);
    int32_t surf = row_back(PANEL_X, y, PANEL_W, ROW_H, i == color_ch, hovered);
    gfx_text_left(&f28, PANEL_X + 16, y + ROW_H / 2, names[i], gfx_ramp(look.text, surf));
    slider(PANEL_X + 48, y + ROW_H / 2, PANEL_W - 48 - 72, fills[i], v, 255, surf);
    u2s(b, (uint32_t) v, 1);
    gfx_text_right(&f28, PANEL_X + PANEL_W - 16, y + ROW_H / 2, b, gfx_ramp(look.text, surf));
  }
}

static void draw_dialog(void) {
  int w = 400, h = 160, x = (FB_W - w) / 2, y = (FB_H - h) / 2;
  hit_clear();                   // only the buttons of the dialog take clicks
  ft_rect(x - 3, y - 3, w + 6, h + 6, 8, look.active, GFX_ALPHA);
  ft_rect(x, y, w, h, 8, look.button, (int32_t) look.active);
  gfx_text_center(&f29, x + w / 2, y + 30, "Drop the changes?", gfx_ramp_over(look.text, look.button));
  gfx_text_center(&f27, x + w / 2, y + 66, "They are not saved", gfx_ramp_over(look.copyright, look.button));
  int by = y + h - 56;
  if (hit_add(x + 40, by, 140, TAB_H, HIT_DLG_LEFT)) ft_rect(x + 40, by, 140, TAB_H, 2, look.active, (int32_t) look.button);
  if (hit_add(x + w - 180, by, 140, TAB_H, HIT_DLG_RIGHT)) ft_rect(x + w - 180, by, 140, TAB_H, 2, look.active, (int32_t) look.button);
  button(x + 40, by, 140, TAB_H, &f28, dialog_left ? look.text_active : look.text, dialog_left ? look.active : look.bg,
         (int32_t) look.button, "Drop");
  button(x + w - 180, by, 140, TAB_H, &f28, !dialog_left ? look.text_active : look.text, !dialog_left ? look.active : look.bg,
         (int32_t) look.button, "Stay");
}

static void draw_footer(void) {
  const char *hint = "", *keys = "";
  switch (focus) {
    case F_TABS:
      hint = custom_page() ? "Enter: open" : tab == T_SAVE ? "Write the settings to karabas.ini" :
             tab == T_CANCEL ? "Drop the changes" : "";
      keys = "Up/Down: section   Enter: open   Esc: cancel";
      break;
    case F_PAGE:
      hint = settings[cur_id()].hint;
      keys = "Up/Down: select   Left/Right: change   Esc: back";
      break;
    case F_COLOR:
      hint = "Shift: fine steps";
      keys = "Up/Down: R G B  Left/Right: change  Enter: OK  Esc: undo";
      break;
    case F_DIALOG:
      keys = "Left/Right: choose   Enter: OK   Esc: back";
      break;
    case F_CUSTOM:
      hint = custom_page()->hint();
      keys = custom_page()->keys();
      break;
  }
  gfx_text_left(&f27, TAB_X, FB_H - 40, error ? error : hint, gfx_ramp_alpha(error ? ERROR_COLOR : look.copyright));
  gfx_text_left(&f27, TAB_X, FB_H - 20, keys, gfx_ramp_alpha(look.copyright));
}

static void track_tab(void);
static void draw(void) {
  track_tab();
  pointer_hide();
  scr_begin();
  look_apply();
  hit_clear();
  draw_logo();
  draw_clock();
  if (tab >= T_SAVE) draw_character();   // the picture only where it does not cover the settings
  draw_tabs();
  if (focus == F_COLOR) draw_color();
  else if (tab < T_PAGES) draw_page();
  else if (custom_page()) {
    title(tab_names[tab]);
    int y = TOP + 40, focused = focus == F_CUSTOM;
    if (!focused) hit_add(PANEL_X, y, PANEL_W, FB_H - 56 - y, HIT_PANEL);   // a click opens it
    custom_page()->draw(PANEL_X, y, PANEL_W, FB_H - 56 - y, focused);
  }
  draw_footer();
  if (focus == F_DIALOG) draw_dialog();
  hover = ptr.visible ? hit_at(ptr.x, ptr.y) : HIT_NONE;
  scr_end();
  dbg_puts("menu: setup drawn\n");
}

// ---- the matrix preview: the clock or the meters while their rows have the focus ----

static int preview_mode = -1;
static void preview(void) {
  int mode = 0;
  if (focus == F_PAGE && mx_page) mode = cur_id() <= S_MX_SECONDS_COLOR ? 1 : 2;
  static int32_t last[S_MX_PEAK + 1];
  int same = mode == preview_mode;
  for (int id = S_MATRIX; id <= S_MX_PEAK; id++) same &= last[id] == set_val[id];
  if (same) return;
  for (int id = S_MATRIX; id <= S_MX_PEAK; id++) last[id] = set_val[id];
  preview_mode = mode;
  settings_preview(mode);
}

// ---- keys ----

static const cpage_t *shown;     // the page whose section is chosen (show / hide)
static void track_tab(void) {
  const cpage_t *p = setup_active ? custom_page() : 0;
  if (p == shown) return;
  if (shown && shown->hide) shown->hide();
  shown = p;
  if (shown && shown->show) shown->show();
}

static void leave(void) {
  if (focus == F_CUSTOM && custom_page()->close) custom_page()->close();
  focus = F_TABS;
  setup_active = 0;
  track_tab();
  ntp_sync_cancel();
  esp_own(0);                    // the ESP back to the MCU
  if (preview_mode >= 0) settings_preview(-1);
  preview_mode = -1;
  browser_open();
}

static void open_tab(void) {
  switch (tab) {
    case T_SAVE:
      if (!settings_save()) { error = "Can't save karabas.ini"; break; }
      browser_reload();
      leave();
      break;
    case T_CANCEL:
      settings_revert();
      leave();
      break;
    default:
      if (custom_page()) {
        if (!custom_page()->key) break;    // nothing to open yet
        focus = F_CUSTOM;
        if (custom_page()->open) custom_page()->open();
        break;
      }
      focus = F_PAGE;
      row = 0;
      mx_page = 0;
  }
}

static void change(int d) {
  int id = cur_id();
  if (settings[id].kind == SK_PAGE) { if (d > 0) { mx_page = 1; row = 0; } return; }
  settings_change(id, d);
  if (id == S_SOUND) snd_start(set_val[S_SOUND]);   // heard as it is chosen (Off stops it)
}

static void enter_row(void) {
  int id = cur_id();
  if (settings[id].kind == SK_COLOR) { focus = F_COLOR; color_ch = 0; color_old = (uint32_t) set_val[id]; }
  else change(1);
}

static void change_color(int d) {
  int32_t *c = &set_val[cur_id()];
  int sh = 16 - 8 * color_ch, v = ((*c >> sh) & 0xff) + d;
  v = v < 0 ? 0 : v > 255 ? 255 : v;
  *c = (*c & ~(0xff << sh)) | (v << sh);
  settings_look();
}

static void to_tabs(void) {
  if (focus == F_CUSTOM && custom_page()->close) custom_page()->close();
  focus = F_TABS;
  mx_page = 0;
}

typedef struct { int up, down, left, right, enter, esc, back, tab, shift, repeat; } keys_t;

static void handle(const keys_t *k) {
  error = 0;
  switch (focus) {
    case F_TABS:
      if (k->up) tab = (tab + T_COUNT - 1) % T_COUNT;
      else if (k->down || k->tab) tab = (tab + 1) % T_COUNT;
      else if (k->enter || (k->right && tab < T_SAVE)) open_tab();
      else if ((k->esc || (k->left && !k->repeat)) && settings_differ()) { focus = F_DIALOG; dialog_left = 0; }
      else if (k->esc || (k->left && !k->repeat)) { leave(); return; }   // Left: the sections are the top
      break;
    case F_PAGE: {
      int n = cur_page()->len;
      if (k->up) row = (row + n - 1) % n;
      else if (k->down) row = (row + 1) % n;
      else if (k->tab) { tab = (tab + 1) % T_PAGES; row = 0; mx_page = 0; }
      else if (k->left || k->right) change(k->left ? -1 : 1);
      else if (k->enter) enter_row();
      else if (k->back && mx_page) { mx_page = 0; row = 5; }    // back to its row of System
      else if (k->back) focus = F_TABS;
      break;
    }
    case F_COLOR:
      if (k->up) color_ch = (color_ch + 2) % 3;
      else if (k->down || k->tab) color_ch = (color_ch + 1) % 3;
      else if (k->left || k->right) change_color((k->left ? -1 : 1) * (k->shift ? 1 : COLOR_STEP));
      else if (k->enter) focus = F_PAGE;
      else if (k->back) { set_val[cur_id()] = (int32_t) color_old; settings_look(); focus = F_PAGE; }
      break;
    case F_DIALOG:
      if (k->left || k->right) dialog_left = !dialog_left;
      else if (k->enter && dialog_left) { settings_revert(); leave(); return; }
      else if (k->enter || k->esc) focus = F_TABS;
      break;
  }
  if (!setup_active) return;
  preview();
  ui_click();
  draw();
}

void setup_open(void) {
  setup_active = 1;
  focus = F_TABS;
  tab = T_SYSTEM;
  row = 0;
  mx_page = 0;
  error = 0;
  dragging = 0;
  draw();
}

void setup_key(const key_ev_t *e) {
  pointer_hide();
  if (focus == F_CUSTOM) {
    error = 0;
    if (!custom_page()->key(e)) to_tabs();
    if (!setup_active) return;
    ui_click();
    draw();
    return;
  }
  keys_t k = { 0 };
  k.up = e->key == K_UP; k.down = e->key == K_DOWN; k.left = e->key == K_LEFT; k.right = e->key == K_RIGHT;
  k.enter = e->key == K_ENTER; k.esc = e->key == K_ESC; k.back = k.esc || e->key == K_BKSP; k.tab = e->key == K_TAB;
  k.shift = e->shift; k.repeat = e->repeat;
  if (!(k.up || k.down || k.left || k.right || k.enter || k.back || k.tab)) { pointer_show(); return; }
  handle(&k);
}

// ---- the mouse ----

static void slider_set(int x) {
  int sx = PANEL_X + 48, sw = PANEL_W - 48 - 72;
  int v = (x - sx) * 255 / sw;
  v = v < 0 ? 0 : v > 255 ? 255 : v;
  int32_t *c = &set_val[cur_id()];
  int sh = 16 - 8 * color_ch;
  *c = (*c & ~(0xff << sh)) | (v << sh);
  settings_look();
}

static void pick_row(int i) { focus = F_PAGE; row = i; }

static void click(int id) {
  if (id >= HIT_TAB && id < HIT_TAB + T_COUNT) {
    to_tabs();
    tab = id - HIT_TAB;
    if (tab >= T_SAVE) open_tab();
  } else if (id == HIT_PANEL) open_tab();
  else if (id >= HIT_PAGE && focus == F_CUSTOM) { if (custom_page()->click) custom_page()->click(id - HIT_PAGE); }
  else if (id >= HIT_ROW && id < HIT_ROW + 16) {
    int i = id - HIT_ROW;
    if (focus == F_PAGE && row == i) enter_row(); else pick_row(i);
  } else if (id >= HIT_TOGGLE && id < HIT_TOGGLE + 16) { pick_row(id - HIT_TOGGLE); change(1); }
  else if (id >= HIT_LESS && id < HIT_LESS + 16) { pick_row(id - HIT_LESS); change(-1); }
  else if (id >= HIT_MORE && id < HIT_MORE + 16) { pick_row(id - HIT_MORE); change(1); }
  else if (id >= HIT_CHAN && id < HIT_CHAN + 3) color_ch = id - HIT_CHAN;
  else if (id >= HIT_SLIDER && id < HIT_SLIDER + 3) { color_ch = id - HIT_SLIDER; dragging = 1; slider_set(ptr.x); }
  else if (id == HIT_DLG_LEFT || id == HIT_DLG_RIGHT) {
    dialog_left = id == HIT_DLG_LEFT;
    keys_t k = { 0 };
    k.enter = 1;
    handle(&k);
  }
}

void setup_mouse(int ev) {
  if (!ev) return;
  if (dragging) {
    if (focus == F_COLOR && (ptr.buttons & 1)) { if (ev & M_MOVED) { slider_set(ptr.x); draw(); } return; }
    dragging = 0;
  }
  if (ev & M_RIGHT) {
    if (focus == F_CUSTOM) { key_ev_t e = { 0 }; e.key = K_ESC; setup_key(&e); return; }
    keys_t k = { 0 };
    k.esc = k.back = 1;
    pointer_hide();
    handle(&k);
    return;
  }
  if (ev & M_LEFT) {
    error = 0;
    int was_active = setup_active;
    click(hit_at(ptr.x, ptr.y));
    if (!setup_active || !was_active) return;
    preview();
    ui_click();
    draw();
    return;
  }
  int h = ptr.visible ? hit_at(ptr.x, ptr.y) : HIT_NONE;
  if (h != hover) draw();
  else { pointer_hide(); pointer_show(); }
}

// ---- the Clock page (firmware apps/app_ft_clock.c, apps/clock_edit.c) ----

enum { C_DATE, C_TIME, C_TZ, C_SET, C_SYNC, C_COUNT };
enum { CH_FIELD = 0, CH_TZ = 2, CH_TZ_DOWN, CH_TZ_UP, CH_SET, CH_SYNC, CH_CHAR = 10 };   // CH_CHAR + 20 * field + char
#define FIELD_X 120
#define CHAR_W 14
#define TZ_W 160

typedef struct { char text[12]; int len, pos, edited; } field_t;
static field_t fields[2];
static int c_focus;
static const char *c_msg;
static int c_msg_ok;
static uint32_t c_msg_t;
static uint8_t rtc[8];
static int rtc_ok;

static void field_set(field_t *f, const char *t) {
  str_cpy(f->text, t);
  f->len = str_len(t);
  if (f->pos >= f->len) f->pos = 0;
}
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static void field_right(field_t *f) { for (int p = f->pos + 1; p < f->len; p++) if (is_digit(f->text[p])) { f->pos = p; return; } }
static void field_left(field_t *f) { for (int p = f->pos - 1; p >= 0; p--) if (is_digit(f->text[p])) { f->pos = p; return; } }
static int field_last(const field_t *f) { for (int p = f->pos + 1; p < f->len; p++) if (is_digit(f->text[p])) return 0; return 1; }

static void read_rtc(void) {
  uint16_t n;
  rtc_ok = rpc_call(RPC_SYS, 1, 0, 0, rtc, sizeof(rtc), &n, 500) == RPC_OK && n >= 8;
}

static void follow_rtc(void) {
  char b[12], *p;
  if (!rtc_ok) return;
  if (!fields[0].edited) {
    p = u2s(b, rtc[3], 2); *p++ = '.'; p = u2s(p, rtc[2], 2); *p++ = '.'; u2s(p, rtc[0] | (rtc[1] << 8), 4);
    field_set(&fields[0], b);
  }
  if (!fields[1].edited) {
    p = u2s(b, rtc[4], 2); *p++ = ':'; u2s(p, rtc[5], 2);
    field_set(&fields[1], b);
  }
}

static void say(const char *m, int ok) { c_msg = m; c_msg_ok = ok; c_msg_t = R_MS; }

static int num(const char *s, int n) { int v = 0; for (int i = 0; i < n; i++) v = v * 10 + (s[i] - '0'); return v; }

static void set_clock(void) {
  const char *d = fields[0].text, *t = fields[1].text;
  int day = num(d, 2), month = num(d + 3, 2), year = num(d + 6, 4), hour = num(t, 2), minute = num(t + 3, 2);
  static const uint8_t mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int leap = year % 4 == 0;
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > mdays[month - 1] || (month == 2 && day == 29 && !leap)) {
    c_focus = C_DATE; say("Invalid date", 0); return;
  }
  if (hour > 23 || minute > 59) { c_focus = C_TIME; say("Invalid time", 0); return; }
  if (!settings_save_one(S_TZ)) { say("Can't save karabas.ini", 0); return; }
  // the weekday (1 Monday .. 7 Sunday), Sakamoto
  static const uint8_t tk[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  int y = year - (month < 3), wd = (y + y / 4 - y / 100 + y / 400 + tk[month - 1] + day) % 7;
  uint8_t req[8] = { (uint8_t) year, (uint8_t) (year >> 8), (uint8_t) month, (uint8_t) day, (uint8_t) hour, (uint8_t) minute, 0,
                     (uint8_t) (wd ? wd : 7) };
  uint16_t n;
  if (rpc_call(RPC_SYS, 2, req, 8, rtc, sizeof(rtc), &n, 1000) != RPC_OK) { say("Can't set the clock", 0); return; }
  fields[0].edited = fields[1].edited = 0;
  read_rtc();
  follow_rtc();
  say("The clock is set", 1);
}

static void clock_open(void) {
  fields[0].edited = fields[1].edited = 0;
  fields[0].pos = fields[1].pos = 0;
  c_focus = C_DATE;
  c_msg = 0;
  read_rtc();
  follow_rtc();
}

static void clock_close(void) {
  if (!settings_save_one(S_TZ)) error = "Can't save karabas.ini";
}

static void draw_field(int x, int w, int focused_page, int i, int y, const char *label, const char *hint) {
  field_t *f = &fields[i];
  int focused = focused_page && c_focus == i, fx = x + FIELD_X, fw = f->len * CHAR_W + 16;
  for (int j = 0; j < f->len && focused_page; j++) hit_add(fx + 8 + j * CHAR_W, y, CHAR_W, FIELD_H, HIT_PAGE + CH_CHAR + 20 * i + j);
  int hovered = focused_page && hit_add(x, y, w, FIELD_H, HIT_PAGE + CH_FIELD + i);
  int32_t surf = focused_page ? row_back(x, y, w, FIELD_H, focused, hovered) : GFX_ALPHA;
  gfx_text_left(&f28, x + 16, y + FIELD_H / 2, label, gfx_ramp(look.text, surf));
  if (focused) gfx_round_rect(fx - 2, y - 2, fw + 4, FIELD_H + 4, 4, gfx_ramp(look.active, surf));
  gfx_round_rect(fx, y, fw, FIELD_H, 4, gfx_ramp(look.bg, focused ? (int32_t) look.active : surf));
  for (int j = 0; j < f->len; j++) {
    int cx = fx + 8 + j * CHAR_W;
    char ch[2] = { f->text[j], 0 };
    ramp_t r = gfx_ramp_over(look.text, look.bg);
    if (focused && j == f->pos) {
      gfx_round_rect(cx - 1, y + 3, CHAR_W + 2, FIELD_H - 6, 1, gfx_ramp_over(CURSOR_COLOR, look.bg));
      r = gfx_ramp_over(0x000000, CURSOR_COLOR);
    }
    gfx_text_center(&f27, cx + CHAR_W / 2, y + FIELD_H / 2, ch, r);
  }
  gfx_text_left(&f27, fx + fw + 16, y + FIELD_H / 2, hint, gfx_ramp(look.copyright, surf));
}

void page_button(int x, int y, int w, const char *label, int focused, int focused_page, int id) {
  int lit = focused && focused_page;
  if (focused_page && hit_add(x, y, w, TAB_H, HIT_PAGE + id)) hover_frame(x, y, w, TAB_H);
  button(x, y, w, TAB_H, &f28, lit ? look.text_active : look.text, lit ? look.active : look.button, GFX_ALPHA, label);
}

static void clock_draw(int x, int y0, int w, int h, int focused) {
  (void) h;
  if (!focused) { fields[0].edited = fields[1].edited = 0; read_rtc(); }
  follow_rtc();
  int y = y0 + 10;
  draw_field(x, w, focused, C_DATE, y, "Date", "DD.MM.YYYY");
  draw_field(x, w, focused, C_TIME, y + 40, "Time", "HH:MM");
  // the time zone: '<' UTC+03:00 '>'
  int ty = y + 80, vx = x + FIELD_X + 40, hovered = 0, tzf = focused && c_focus == C_TZ;
  if (focused) {
    hovered |= hit_add(vx, ty, 32, FIELD_H, HIT_PAGE + CH_TZ_DOWN);
    hovered |= hit_add(vx + TZ_W - 32, ty, 32, FIELD_H, HIT_PAGE + CH_TZ_UP);
    hovered |= hit_add(x, ty, w, FIELD_H, HIT_PAGE + CH_TZ);
  }
  int32_t surf = focused ? row_back(x, ty, w, FIELD_H, tzf, hovered) : GFX_ALPHA;
  ramp_t text = gfx_ramp(look.text, surf);
  char v[16];
  gfx_text_left(&f28, x + 16, ty + FIELD_H / 2, "Time zone", text);
  settings_value_text(S_TZ, v, sizeof(v));
  gfx_text_center(&f28, vx + TZ_W / 2, ty + FIELD_H / 2, v, text);
  if (tzf || hovered) {
    gfx_text_center(&f28, vx + 12, ty + FIELD_H / 2, "<", text);
    gfx_text_center(&f28, vx + TZ_W - 12, ty + FIELD_H / 2, ">", text);
  }
  int by = y + 128;
  page_button(x + 16, by, 140, "Set", c_focus == C_SET, focused, CH_SET);
  page_button(x + 172, by, 140, "NTP sync", c_focus == C_SYNC, focused, CH_SYNC);
  const char *m = ntp_sync_state() == NTP_BUSY ? ntp_sync_message() : c_msg;   // the progress as it goes
  if (m && focused)
    gfx_text_left(&f28, x + 16, by + TAB_H + 24, m,
                  gfx_ramp_alpha(ntp_sync_state() == NTP_BUSY ? look.text : c_msg_ok ? OK_COLOR : ERROR_COLOR));
}

static void ntp_sync(void) {
  if (!settings_save_one(S_TZ)) { say("Can't save karabas.ini", 0); return; }
  c_msg = 0;
  ntp_sync_start(set_val[S_TZ]);
}

static int clock_key(const key_ev_t *e) {
  field_t *f = c_focus < C_TZ ? &fields[c_focus] : 0;
  int enter = e->key == K_ENTER;
  char digit = key_digit(e->key);
  if (e->key == K_ESC) return 0;
  if (digit || enter) c_msg = 0;
  if (e->pad && f && (e->key == K_UP || e->key == K_DOWN)) {
    // a pad has no digits: Up / Down turn the one under the cursor
    char *c = &f->text[f->pos];
    *c = (char) ('0' + ((*c - '0') + (e->key == K_UP ? 1 : 9)) % 10);
    f->edited = 1;
  } else if (e->key == K_UP) c_focus = (c_focus + C_COUNT - 1) % C_COUNT;
  else if (e->key == K_DOWN || e->key == K_TAB) c_focus = (c_focus + 1) % C_COUNT;
  else if (f && digit) {
    f->text[f->pos] = digit;
    f->edited = 1;
    if (field_last(f)) { f->pos = 0; c_focus++; } else field_right(f);
  }
  else if (f && (e->key == K_LEFT || e->key == K_BKSP)) field_left(f);
  else if (f && e->key == K_RIGHT) field_right(f);
  else if (f && enter) c_focus++;
  else if (c_focus == C_TZ && (e->key == K_LEFT || e->key == K_RIGHT)) settings_change(S_TZ, e->key == K_RIGHT ? 1 : -1);
  else if (c_focus == C_TZ && enter) c_focus++;
  else if (e->key == K_LEFT || e->key == K_RIGHT) c_focus = c_focus == C_SET ? C_SYNC : C_SET;
  else if (enter && c_focus == C_SYNC) ntp_sync();
  else if (enter) set_clock();
  return 1;
}

static void clock_click(int id) {
  if (id >= CH_CHAR && id < CH_CHAR + 40) {
    c_focus = (id - CH_CHAR) / 20;
    field_t *f = &fields[c_focus];
    int j = (id - CH_CHAR) % 20;
    f->pos = j < f->len && is_digit(f->text[j]) ? j : f->pos;
  } else if (id == CH_FIELD + C_DATE || id == CH_FIELD + C_TIME) c_focus = id - CH_FIELD;
  else if (id == CH_TZ) c_focus = C_TZ;
  else if (id == CH_TZ_DOWN || id == CH_TZ_UP) { c_focus = C_TZ; settings_change(S_TZ, id == CH_TZ_UP ? 1 : -1); }
  else if (id == CH_SET) { c_focus = C_SET; set_clock(); }
  else if (id == CH_SYNC) { c_focus = C_SYNC; ntp_sync(); }
}

// the RTC as it runs (a new minute), an OK message goes after 5 s
static int clock_tick(void) {
  static uint32_t t = 0;
  int redraw = 0;
  if (c_msg && c_msg_ok && R_MS - c_msg_t > 5000) { c_msg = 0; redraw = 1; }
  if (ntp_sync_state() == NTP_BUSY) {
    static const char *last;
    uint8_t r = ntp_sync_poll();
    if (r != NTP_BUSY) {
      if (r == NTP_OK) { fields[0].edited = fields[1].edited = 0; read_rtc(); follow_rtc(); }
      say(ntp_sync_message(), r == NTP_OK);
      redraw = 1;
    } else if (ntp_sync_message() != last) redraw = 1;
    last = ntp_sync_message();
  }
  if (R_MS - t >= 1000) {
    t = R_MS;
    uint8_t old = rtc[5];
    read_rtc();
    if (rtc_ok && rtc[5] != old && (!fields[0].edited || !fields[1].edited)) redraw = 1;
  }
  return redraw;
}

static const char *clock_hint(void) {
  if (c_focus == C_TZ) return "Left/Right: the UTC offset of NTP sync, no summer time";
  if (c_focus == C_SYNC) return "Set the clock from NTP over Wi-Fi";
  return c_focus == C_SET ? "Write the date and the time to the RTC" : "Digits (pad: Up/Down): type   Left/Right: cursor";
}
static const char *clock_keys(void) { return "Up/Down: select   Enter: next / set   Esc: back"; }

static const cpage_t page_clock = { clock_open, clock_close, 0, 0, clock_draw, clock_key, clock_tick, clock_click, clock_hint, clock_keys };

static const cpage_t *custom_page(void) {
  return tab == T_CLOCK ? &page_clock : tab == T_WIFI ? &page_wifi : tab == T_PAD ? &page_pad : 0;
}

void setup_tick(void) {
  static uint32_t t_clock = 0;
  esp_pump();
  const cpage_t *p = custom_page();
  if (p && p->tick && p->tick()) { draw(); return; }
  if (R_MS - t_clock >= 1000) { t_clock = R_MS; pointer_hide(); draw_clock(); pointer_show(); }
}
