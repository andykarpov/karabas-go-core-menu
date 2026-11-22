// The Wi-Fi page of Setup (firmware apps/app_ft_wifi.c): wifi.c (the firmware's wifi_setup) asks the
// module, scans and joins over the uart of the ESP; this page draws it. The status is asked as soon
// as the section is chosen.
#include <string.h>
#include <stdio.h>
#include "setup_ui.h"
#include "wifi.h"
#include "esp_update.h"
#include "esp.h"

enum { HIT_STATUS = 0, HIT_RETRY = 10, HIT_RESCAN, HIT_NEXT, HIT_CONNECT, HIT_SHOW, HIT_OK, HIT_FORGET, HIT_CANCEL, HIT_UPDATE, HIT_AP = 100 };

#define INFO_STEP 28
#define INFO_X 140
#define BTN_W 220
#define BTN_STEP 40
#define LIST_ROW_H 30
#define CHAR_W 13
#define BAR_H 20
#define LOCK_W 14

static int px, pw, py, ph, pfocused;   // the place of the page in this picture

// a string cut to w pixels ("..." at its end)
static const char *fit(const font_t *f, const char *s, int w) {
  static char b[72];
  strncpy(b, s, sizeof(b) - 1);
  b[sizeof(b) - 1] = 0;
  int n = strlen(b);
  if (gfx_text_width(f, b) <= w) return b;
  while (n > 3 && gfx_text_width(f, b) > w) { b[--n] = 0; b[n - 1] = '.'; b[n - 2] = '.'; b[n - 3] = '.'; }
  return b;
}

static void info(int y, const char *label, const char *value, uint32_t color) {
  gfx_text_left(&f28, px + 16, y, label, gfx_ramp_alpha(look.text));
  gfx_text_left(&f28, px + INFO_X, y, fit(&f28, value, pw - INFO_X - 16), gfx_ramp_alpha(color));
}

static void message(int y, const char *msg, uint32_t color) {
  gfx_text_left(&f28, px + 16, y, fit(&f28, msg, pw - 32), gfx_ramp_alpha(color));
}

// the text field: one char per CHAR_W, the tail that fits, a cyan cursor at the end
static void field(int y, const char *text, int hidden) {
  int fx = px + 16, fw = pw - 32, width = (fw - 16) / CHAR_W;
  int len = strlen(text), from = len >= width ? len - width + 1 : 0;
  gfx_round_rect(fx - 2, y - 2, fw + 4, FIELD_H + 4, 4, gfx_ramp_alpha(look.active));
  gfx_round_rect(fx, y, fw, FIELD_H, 4, gfx_ramp_over(look.bg, look.active));
  for (int i = from; i <= len; i++) {
    int cx = fx + 8 + (i - from) * CHAR_W;
    if (i == len) { gfx_round_rect(cx - 1, y + 3, CHAR_W + 2, FIELD_H - 6, 1, gfx_ramp_over(CURSOR_COLOR, look.bg)); break; }
    char ch[2] = { hidden ? '*' : text[i], 0 };
    if (ch[0] < 32 || ch[0] >= 127) ch[0] = '?';
    gfx_text_center(&f27, cx + CHAR_W / 2, y + FIELD_H / 2, ch, gfx_ramp_over(look.text, look.bg));
  }
}

static void status(int y) {
  const ws_t *w = ws_state();
  info(y + INFO_STEP / 2, "Module", w->version[0] ? w->version : "?", look.text);
  info(y + INFO_STEP * 3 / 2, "CTS", w->cts == WS_CTS_ON ? "on" : w->cts == WS_CTS_OFF ? "could not turn on" : "unknown",
       w->cts == WS_CTS_ON ? look.text : ERROR_COLOR);
  info(y + INFO_STEP * 5 / 2, "Network", w->cur_ssid[0] ? w->cur_ssid : "not connected", w->cur_ssid[0] ? look.text : look.copyright);
  info(y + INFO_STEP * 7 / 2, "IP", w->ip[0] && strcmp(w->ip, "0.0.0.0") != 0 ? w->ip : "-", look.text);
}

// a padlock LOCK_W wide, centred on y: closed for a network with a password, open for one without
static void lock(int x, int y, int closed, uint32_t color, int32_t surf) {
  ramp_t r = gfx_ramp(color, surf);
  int cx = x + LOCK_W / 2;
  gfx_arc_top(cx, y - 5, 5, r);
  gfx_fill(cx - 5, y - 5, 2, 5, RAMP_FULL(r));                     // the left leg into the body
  gfx_fill(cx + 3, y - 5, 2, closed ? 5 : 1, RAMP_FULL(r));         // the right one: out when open
  gfx_round_rect(x, y - 1, LOCK_W, 9, 2, r);                        // the body
}

static void list(void) {
  const ws_t *w = ws_state();
  int y0 = py + TAB_H + 8, rows = (ph - (y0 - py)) / LIST_ROW_H;
  gfx_text_left(&f27, px + 16, py + TAB_H / 2, "dBm      Network", gfx_ramp_alpha(look.copyright));
  if (w->aps_len > 0) {
    char b[12];
    snprintf(b, sizeof(b), "%u/%u", w->sel + 1, w->aps_len);
    gfx_text_right(&f27, px + pw - 16 - 140 - 16, py + TAB_H / 2, b, gfx_ramp_alpha(look.copyright));
  }
  page_button(px + pw - 16 - 140, py, 140, "Rescan", w->on_rescan || w->aps_len == 0, pfocused, HIT_RESCAN);
  if (rows < 1) rows = 1;
  ws_set_list((uint8_t) rows, true);
  if (w->aps_len == 0) { message(y0 + LIST_ROW_H / 2, "No networks found", look.text); return; }
  int top = ws_list_top();
  for (int row = 0; row < rows && top + row < w->aps_len; row++) {
    const kg_wifi_ap_t *ap = &w->aps[top + row];
    int y = y0 + row * LIST_ROW_H, cy = y + (LIST_ROW_H - 2) / 2;
    int hovered = pfocused && hit_add(px, y, pw, LIST_ROW_H - 2, HIT_PAGE + HIT_AP + top + row);
    int32_t surf = pfocused ? row_back(px, y, pw, LIST_ROW_H - 2, top + row == w->sel && !w->on_rescan, hovered) : GFX_ALPHA;
    char b[8];
    snprintf(b, sizeof(b), "%d", ap->rssi);
    gfx_text_right(&f28, px + 60, cy, b, gfx_ramp(look.text, surf));
    int joined = strcmp(ap->ssid, w->cur_ssid) == 0;
    uint32_t color = joined ? look.active : look.text;
    lock(px + 72, cy, ap->ecn != 0, color, surf);
    gfx_text_left(&f28, px + 96, cy, fit(&f28, ap->ssid, pw - 112), gfx_ramp(color, surf));
  }
}

static void draw(int x, int y, int w, int h, int focused) {
  const ws_t *s = ws_state();
  char buf[64];
  int bx = x + 16;
  px = x; py = y; pw = w; ph = h; pfocused = focused;
  switch (s->screen) {
    case WS_INIT: message(y + INFO_STEP / 2, "Talking to ESP8266...", look.text); break;
    case WS_NO_ESP:
      message(y + INFO_STEP / 2, "ESP8266 is not responding", ERROR_COLOR);
      page_button(bx, y + 2 * INFO_STEP, BTN_W, "Retry", 1, focused, HIT_RETRY);
      break;
    case WS_STATUS: {
      status(y);
      int by = y + 4 * INFO_STEP + 16;
      uint8_t ids[WS_M_COUNT], n = ws_menu_items(ids), rows = (n + 1) / 2;
      int bw = (w - 48) / 2;           // two columns in the panel (narrower in 640 x 480)
      if (bw > BTN_W) bw = BTN_W;
      for (int i = 0; i < n; i++)
        page_button(bx + (i / rows) * (bw + 16), by + (i % rows) * BTN_STEP, bw, ws_menu_names[ids[i]], s->sel == i, focused, HIT_STATUS + i);
      break;
    }
    case WS_SCAN: message(y + INFO_STEP / 2, "Scanning...", look.text); break;
    case WS_LIST: list(); break;
    case WS_SSID:
      message(y + INFO_STEP / 2, "Network name:", look.text);
      field(y + INFO_STEP + 4, s->ssid, 0);
      page_button(bx, y + INFO_STEP + 4 + BTN_STEP + 8, 140, "Next", 0, focused, HIT_NEXT);
      break;
    case WS_PASS:
      info(y + INFO_STEP / 2, "Network", s->ssid, look.text);
      message(y + INFO_STEP * 3 / 2, "Password:", look.text);
      field(y + 2 * INFO_STEP + 4, s->pass, !s->show_pass);
      page_button(bx, y + 2 * INFO_STEP + 4 + BTN_STEP + 8, 140, "Connect", 0, focused, HIT_CONNECT);
      page_button(bx + 156, y + 2 * INFO_STEP + 4 + BTN_STEP + 8, 140, s->show_pass ? "Hide" : "Show", 0, focused, HIT_SHOW);
      break;
    case WS_JOIN:
      snprintf(buf, sizeof(buf), "Connecting to %s...", s->ssid);
      message(y + INFO_STEP / 2, buf, look.text);
      break;
    case WS_RESULT:
      if (s->result_error) message(y + INFO_STEP / 2, s->result_error, ERROR_COLOR);
      else {
        message(y + INFO_STEP / 2, "Connected", OK_COLOR);
        info(y + INFO_STEP * 3 / 2, "Network", s->cur_ssid, look.text);
        info(y + INFO_STEP * 5 / 2, "IP", s->ip[0] ? s->ip : "-", look.text);
        message(y + INFO_STEP * 7 / 2, "The ESP8266 remembers it", look.copyright);
      }
      page_button(bx, y + 4 * INFO_STEP + 16, 140, "OK", 1, focused, HIT_OK);
      break;
    case WS_FORGET:
      message(y + INFO_STEP / 2, "Forget this network?", look.text);
      info(y + INFO_STEP * 3 / 2, "Network", s->cur_ssid, look.text);
      message(y + INFO_STEP * 5 / 2, "The ESP8266 will not join it", look.copyright);
      page_button(bx, y + 3 * INFO_STEP + 16, 140, "Forget", s->sel == 0, focused, HIT_FORGET);
      page_button(bx + 156, y + 3 * INFO_STEP + 16, 140, "Cancel", s->sel == 1, focused, HIT_CANCEL);
      break;
    case WS_UPD_ASK:
      if (s->restoring) {
        message(y + INFO_STEP / 2, "Erase the ESP8266 settings?", look.text);
        message(y + INFO_STEP * 3 / 2, "For modules that lose settings", look.copyright);
        message(y + INFO_STEP * 5 / 2, "The network is forgotten too,", look.copyright);
        message(y + INFO_STEP * 7 / 2, "join it again after it", look.copyright);
      } else {
        message(y + INFO_STEP / 2, "Update the ESP8266 firmware?", look.text);
        info(y + INFO_STEP * 3 / 2, "Module", s->version[0] ? s->version : "?", look.text);
        message(y + INFO_STEP * 5 / 2, "From the Espressif server, ~30 s", look.copyright);
        message(y + INFO_STEP * 7 / 2, "Do not power off the board", look.copyright);
      }
      page_button(bx, y + 4 * INFO_STEP + 16, 140, s->restoring ? "Wipe" : "Update", s->sel == 0, focused, HIT_UPDATE);
      page_button(bx + 156, y + 4 * INFO_STEP + 16, 140, "Cancel", s->sel == 1, focused, HIT_CANCEL);
      break;
    case WS_UPD: {
      int percent = esp_update_progress();
      const char *msg = esp_update_message();
      int bw = w - 32 - 64, by = y + INFO_STEP + 8;
      message(y + INFO_STEP / 2, msg ? msg : "", look.text);
      gfx_round_rect(bx, by, bw, BAR_H, 4, gfx_ramp_alpha(look.copyright));
      gfx_round_rect(bx + 2, by + 2, bw - 4, BAR_H - 4, 3, gfx_ramp_over(look.bg, look.copyright));
      if (percent > 0) gfx_round_rect(bx + 2, by + 2, (bw - 4) * percent / 100, BAR_H - 4, 3, gfx_ramp_over(look.active, look.bg));
      snprintf(buf, sizeof(buf), "%u%%", percent);
      gfx_text_right(&f28, bx + bw + 56, by + BAR_H / 2, buf, gfx_ramp_alpha(look.text));
      message(y + INFO_STEP * 3 + 4, "Do not power off the board", look.copyright);
      break;
    }
    case WS_UPD_END: {
      int ok = esp_update_poll() == ESP_UPD_OK;
      message(y + INFO_STEP / 2, esp_update_message(), ok ? OK_COLOR : ERROR_COLOR);
      if (!s->restoring) {
        info(y + INFO_STEP * 3 / 2, ok ? "Was" : "Module", s->old_version[0] ? s->old_version : "?", look.text);
        if (ok) info(y + INFO_STEP * 5 / 2, "Now", esp_update_version()[0] ? esp_update_version() : "?", look.text);
      } else if (ok) message(y + INFO_STEP * 3 / 2, "Join the network again", look.copyright);
      page_button(bx, y + 4 * INFO_STEP + 16, 140, "OK", 1, focused, HIT_OK);
      break;
    }
  }
}

static void show(void) { ws_start(); }
static void hide(void) { ws_stop(); esp_update_cancel(); }
static void open(void) { if (ws_state()->screen == WS_NO_ESP) ws_start(); }
static int key(const key_ev_t *e) { return ws_key(e); }
static int tick(void) { return ws_tick(); }

static void click(int id) {
  if (id >= HIT_AP && id < HIT_AP + WS_AP_MAX) ws_click_ap(id - HIT_AP);
  else if (id >= HIT_STATUS && id < HIT_STATUS + WS_M_COUNT) ws_menu(id - HIT_STATUS);
  else switch (id) {
    case HIT_RETRY: ws_start(); break;
    case HIT_RESCAN: ws_scan(); break;
    case HIT_NEXT: ws_next(); break;
    case HIT_CONNECT: ws_join(); break;
    case HIT_SHOW: ws_toggle_pass(); break;
    case HIT_OK: ws_ok(); break;
    case HIT_CANCEL: ws_confirm(false); break;
    case HIT_UPDATE: case HIT_FORGET: ws_confirm(true); break;
  }
  ws_drawn();
}

static const char *hint(void) {
  switch (ws_state()->screen) {
    case WS_INIT: case WS_SCAN: case WS_JOIN: return "Please wait";
    case WS_UPD: return "Please wait, do not power off the board";
    case WS_UPD_ASK: return ws_state()->restoring ? "AT+RESTORE: the network, the mode and the UART settings" : "The module keeps its old firmware if the download fails";
    case WS_LIST: return "Closed lock: password, joined in color";
    case WS_SSID: return "Pad: Up/Down: char   Right: add   Left: delete";
    case WS_PASS: return ws_state()->show_pass ? "Tab: hide the password" : "Tab: show the password";
    default: return "";
  }
}

static const char *keys(void) {
  switch (ws_state()->screen) {
    case WS_LIST: return "Up/Down: select   Enter: connect   R: rescan   Esc: back";
    case WS_SSID: return "Enter: next   Esc: back";
    case WS_PASS: return "Enter: connect   Esc: back";
    case WS_FORGET: case WS_UPD_ASK: return "Left/Right: select   Enter: OK   Esc: back";
    case WS_JOIN: case WS_UPD: return "";
    case WS_UPD_END: return "Enter: OK";
    default: return "Up/Down: select   Enter: OK   Esc: back";
  }
}

const cpage_t page_wifi = { open, 0, show, hide, draw, key, tick, click, hint, keys };
