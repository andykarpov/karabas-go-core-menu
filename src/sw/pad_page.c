// The Gamepad page of Setup (firmware apps/app_ft_pad.c). First the two joystick ports: the USB pad
// on each and what maps it (a driver ini, the default map, XInput); Forget renames the driver ini
// of a pad to .bak. Assign learns a pad: the learning runs in the firmware (pad_learn, the reports
// of the pads are there), the page starts and steers it over RPC HID and draws its state. A Mega
// Drive pad shows the button asked for and the ones mapped; when all are done it shows the presses
// through the learned map, also after Save (a test). Test: both ports as the core gets them (the
// joysticks of kg_mcu2) until Esc or a hold of Start+Mode. The keyboard and the mouse drive the
// page: the pad being mapped must not.
#include <string.h>
#include <stdio.h>
#include "setup_ui.h"
#include "mmio.h"
#include "rpc.h"

enum { V_LIST, V_TEST, V_MAPPING };
enum { HIT_SKIP, HIT_BACK, HIT_SAVE, HIT_DONE, HIT_MAP, HIT_TEST, HIT_FORGET, HIT_PORT = 10 };
// the states of the firmware's pad_learn
enum { PL_IDLE, PL_SELECT, PL_SETTLE, PL_REST, PL_PRESS, PL_RELEASE, PL_DONE, PL_SAVED, PL_FAILED };
enum { USB_PAD_NONE, USB_PAD_DRIVER, USB_PAD_DEFAULT, USB_PAD_XINPUT };
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_C, B_X, B_Y, B_Z, B_START, B_MODE, B_COUNT };
static const char *const btn_names[B_COUNT] = { "Up", "Down", "Left", "Right", "A", "B", "C", "X", "Y", "Z", "Start", "Mode" };
// the bits of the joysticks (sega.h of the firmware) in the order of B_*
static const uint16_t joy_bits[B_COUNT] = { 2, 4, 8, 16, 64, 128, 256, 512, 1024, 2048, 32, 4096 };

#define LIST_REFRESH_MS 500
#define LEARN_POLL_MS 100
#define TEST_EXIT_MS 2000
#define TEST_EXIT (32 | 4096)    // Start + Mode
#define PAD_W 300
#define PAD_H 98
#define BTN_W 110
#define LIST_BTN_W 120
#define TEST_LABEL_H 22
#define ROW_H 30
#define COL_PAD 80
#define COL_SRC 220
#define PAD_BODY 0xc8ccd4
#define PAD_EDGE 0x334155
#define PAD_KEY 0x475569
#define PAD_LABEL 0xffffff
#define PAD_LABEL_OFF 0x94a3b8

typedef struct { uint8_t source; uint16_t vid, pid; char file[40]; } pad_t;
static pad_t pads[2];
static int view = V_LIST, port, list_btn, list_msg_ok, draw_port, joy_released = 1, dirty;
static const char *list_msg;
static uint32_t timer, test_exit;
static uint16_t test_joy[2];
// the learning as the firmware has it (HID LEARN_STATE)
static struct { uint8_t state, btn, left_s; int8_t last; uint16_t vid, pid, mapped, live; char rule[24], saved[40]; } pl;
static uint8_t buf[RPC_MAX];

static int px, py, pw, pfocused;

// ---- RPC HID ----

static int read_pads(pad_t *p) {
  uint16_t n;
  if (rpc_call(RPC_HID, 0, 0, 0, buf, sizeof(buf), &n, 1000) != RPC_OK) return 0;
  memset(p, 0, 2 * sizeof(pad_t));   // compared whole (memcmp)
  const uint8_t *q = buf, *end = buf + n;
  for (int i = 0; i < 2 && q + 5 <= end; i++) {
    p[i].source = q[0]; p[i].vid = q[1] | (q[2] << 8); p[i].pid = q[3] | (q[4] << 8);
    q += 5;
    int k = 0;
    while (q < end && *q) { if (k < (int) sizeof(p[i].file) - 1) p[i].file[k++] = (char) *q; q++; }
    p[i].file[k] = 0;
    if (q < end) q++;
  }
  return 1;
}

static void read_learn(void) {
  uint16_t n;
  if (rpc_call(RPC_HID, 3, 0, 0, buf, sizeof(buf), &n, 1000) != RPC_OK || n < 12) return;
  pl.state = buf[0]; pl.btn = buf[1]; pl.last = (int8_t) buf[2]; pl.left_s = buf[3];
  pl.vid = buf[4] | (buf[5] << 8); pl.pid = buf[6] | (buf[7] << 8);
  pl.mapped = buf[8] | (buf[9] << 8); pl.live = buf[10] | (buf[11] << 8);
  const uint8_t *q = buf + 12, *end = buf + n;
  int k = 0;
  while (q < end && *q) { if (k < 23) pl.rule[k++] = (char) *q; q++; }
  pl.rule[k] = 0;
  if (q < end) q++;
  k = 0;
  while (q < end && *q) { if (k < 39) pl.saved[k++] = (char) *q; q++; }
  pl.saved[k] = 0;
}

static void learn_do(uint8_t what) {   // 0 stop, 1 skip, 2 back, 3 save
  uint16_t n;
  rpc_call(RPC_HID, 4, &what, 1, buf, sizeof(buf), &n, 1000);
  read_learn();
}

// ---- the views ----

static void set_view(int v) { view = v; timer = R_MS; dirty = 1; }
static int can_forget(void) { return pads[port].source == USB_PAD_DRIVER; }
static int list_btns(void) { return can_forget() ? 3 : 2; }

static void show_list(void) {
  if (view == V_MAPPING) learn_do(0);
  read_pads(pads);
  list_btn = 0;
  set_view(V_LIST);
}

static void start_test(void) {
  test_joy[0] = test_joy[1] = 0;
  list_msg = 0;
  test_exit = R_MS;
  set_view(V_TEST);
}

static void stop_test(void) {
  joy_released = 0;              // Start+Mode may still be held: Mode is Esc on the list
  show_list();
  list_btn = 1;
}

static void start(void) {
  uint16_t n;
  list_msg = 0;
  rpc_call(RPC_HID, 2, 0, 0, buf, sizeof(buf), &n, 1000);
  read_learn();
  set_view(V_MAPPING);
}

static void forget(void) {
  uint8_t p = (uint8_t) port;
  uint16_t n;
  int ok = rpc_call(RPC_HID, 1, &p, 1, buf, sizeof(buf), &n, 2000) == RPC_OK;
  list_msg = ok ? "The driver is forgotten (.bak)" : "Could not rename the driver";
  list_msg_ok = ok;
  read_pads(pads);
  list_btn = 0;
}

// ---- drawing ----

static int lit(int i) {
  if (view == V_TEST) return (test_joy[draw_port] & joy_bits[i]) != 0;
  if (pl.state == PL_PRESS || pl.state == PL_RELEASE) return i == pl.btn;
  if (pl.state >= PL_DONE) return (pl.live >> i) & 1;
  return 0;
}
static uint32_t key_color(int i) { return lit(i) ? look.active : PAD_KEY; }
static uint32_t label_color(int i) {
  if (lit(i)) return look.text_active;
  int st = view == V_MAPPING ? pl.state : PL_IDLE;
  return st >= PL_PRESS && !((pl.mapped >> i) & 1) && (st >= PL_DONE || i < pl.btn) ? PAD_LABEL_OFF : PAD_LABEL;
}

// the pad is drawn in units of a 400 x 130 picture, at 3/4
#define K(v) ((v) * 3 / 4)

static void draw_round(int x, int y, int r, int i) {
  gfx_circle(x, y, K(r), gfx_ramp_over(key_color(i), PAD_BODY));
  gfx_text_center(r > 14 ? &f26 : &f20, x, y, btn_names[i], gfx_ramp_over(label_color(i), key_color(i)));
}
static void draw_small(int x, int y, int i, const char *label) {
  gfx_round_rect(x, y, K(60), K(18), 6, gfx_ramp_over(key_color(i), PAD_BODY));
  gfx_text_center(&f20, x + K(30), y + K(9), label, gfx_ramp_over(label_color(i), key_color(i)));
}
static void draw_key(int x, int y, int w, int h, int i) {
  gfx_round_rect(x, y, w, h, 2, gfx_ramp_over(key_color(i), PAD_BODY));
}

// a Mega Drive pad, PAD_W x PAD_H
static void draw_pad(int x, int y) {
  gfx_round_rect(x, y, PAD_W, PAD_H, K(30), gfx_ramp_alpha(PAD_EDGE));
  gfx_round_rect(x + 3, y + 3, PAD_W - 6, PAD_H - 6, K(30) - 3, gfx_ramp_over(PAD_BODY, PAD_EDGE));
  int cx = x + K(80), cy = y + PAD_H / 2;
  draw_key(cx - K(12), cy - K(42), K(24), K(30), B_UP);
  draw_key(cx - K(12), cy + K(12), K(24), K(30), B_DOWN);
  draw_key(cx - K(42), cy - K(12), K(30), K(24), B_LEFT);
  draw_key(cx + K(12), cy - K(12), K(30), K(24), B_RIGHT);
  gfx_fill(cx - K(12), cy - K(12), K(24), K(24), gfx_color(PAD_EDGE));
  draw_small(x + K(170), y + K(34), B_MODE, "MODE");
  draw_small(x + K(170), y + K(70), B_START, "START");
  draw_round(x + K(282), y + K(42), 13, B_X);
  draw_round(x + K(316), y + K(36), 13, B_Y);
  draw_round(x + K(350), y + K(30), 13, B_Z);
  draw_round(x + K(272), y + K(90), 17, B_A);
  draw_round(x + K(314), y + K(82), 17, B_B);
  draw_round(x + K(356), y + K(74), 17, B_C);
}

static const char *pad_source(const pad_t *p) {
  switch (p->source) {
    case USB_PAD_DRIVER: return p->file;
    case USB_PAD_DEFAULT: return "default map";
    case USB_PAD_XINPUT: return "XInput";
    default: return "no map";
  }
}

static void draw_list(void) {
  int x = px + 16, y = py + 4;
  ramp_t dim = gfx_ramp_alpha(look.copyright);
  gfx_text_left(&f27, x, y + ROW_H / 2, "Port", dim);
  gfx_text_left(&f27, x + COL_PAD, y + ROW_H / 2, "Pad", dim);
  gfx_text_left(&f27, x + COL_SRC, y + ROW_H / 2, "Mapped by", dim);
  for (int i = 0; i < 2; i++) {
    const pad_t *p = &pads[i];
    int ry = y + ROW_H * (i + 1), cy = ry + (ROW_H - 2) / 2;
    int hovered = pfocused && hit_add(px, ry, pw, ROW_H - 2, HIT_PAGE + HIT_PORT + i);
    int32_t surf = pfocused ? row_back(px, ry, pw, ROW_H - 2, i == port, hovered) : GFX_ALPHA;
    char b[32];
    snprintf(b, sizeof(b), "%d", i + 1);
    gfx_text_left(&f28, x + 8, cy, b, gfx_ramp(look.text, surf));
    if (p->source == USB_PAD_NONE && p->vid == 0) { gfx_text_left(&f28, x + COL_PAD, cy, "-", gfx_ramp(look.copyright, surf)); continue; }
    snprintf(b, sizeof(b), "%04x:%04x", p->vid, p->pid);
    gfx_text_left(&f28, x + COL_PAD, cy, b, gfx_ramp(look.text, surf));
    gfx_text_left(&f28, x + COL_SRC, cy, pad_source(p), gfx_ramp(p->source == USB_PAD_DRIVER ? look.text : look.copyright, surf));
  }
  int by = y + ROW_H * 3 + 12;
  page_button(x, by, LIST_BTN_W, "Assign", list_btn == 0, pfocused, HIT_MAP);
  page_button(x + LIST_BTN_W + 12, by, LIST_BTN_W, "Test", list_btn == 1, pfocused, HIT_TEST);
  if (can_forget()) page_button(x + 2 * (LIST_BTN_W + 12), by, LIST_BTN_W, "Forget", list_btn == 2, pfocused, HIT_FORGET);
  if (list_msg && pfocused) gfx_text_left(&f28, x, by + TAB_H + 20, list_msg, gfx_ramp_alpha(list_msg_ok ? OK_COLOR : ERROR_COLOR));
}

static void draw_mapper(void) {
  int x = px + 16;
  char l1[48] = "", l2[48] = "";
  uint32_t c1 = look.text;
  switch (pl.state) {
    case PL_SELECT: strcpy(l1, "Press a button on the USB pad"); break;
    case PL_SETTLE: case PL_REST:
      snprintf(l1, sizeof(l1), "Pad %04x:%04x", pl.vid, pl.pid);
      strcpy(l2, "Release all, don't touch it");
      break;
    case PL_PRESS:
      snprintf(l1, sizeof(l1), "Press %s", btn_names[pl.btn < B_COUNT ? pl.btn : 0]);
      snprintf(l2, sizeof(l2), "Skipped in %u s", pl.left_s);
      break;
    case PL_RELEASE: snprintf(l1, sizeof(l1), "Release %s", btn_names[pl.btn < B_COUNT ? pl.btn : 0]); break;
    case PL_DONE: strcpy(l1, "All done"); strcpy(l2, "Try the pad, then save"); break;
    case PL_SAVED: strcpy(l1, "Saved"); c1 = OK_COLOR; strcpy(l2, pl.saved); break;
    case PL_FAILED: strcpy(l1, "Not saved"); c1 = ERROR_COLOR; strcpy(l2, "Could not write the SD card"); break;
  }
  gfx_text_left(&f28, x, py + TAB_H / 2, l1, gfx_ramp_alpha(c1));
  gfx_text_left(&f27, x, py + TAB_H + 12, l2, gfx_ramp_alpha(look.copyright));
  int bx = px + pw - 16 - BTN_W, bx2 = bx - BTN_W - 8;
  if (pl.state == PL_PRESS) {
    page_button(bx, py, BTN_W, "Skip", 1, pfocused, HIT_SKIP);
    if (pl.btn > 0) page_button(bx2, py, BTN_W, "Back", 0, pfocused, HIT_BACK);
  } else if (pl.state == PL_DONE) {
    page_button(bx, py, BTN_W, "Save", 1, pfocused, HIT_SAVE);
    page_button(bx2, py, BTN_W, "Back", 0, pfocused, HIT_BACK);
  } else if (pl.state == PL_SAVED || pl.state == PL_FAILED) page_button(bx, py, BTN_W, "Done", 1, pfocused, HIT_DONE);
  int y = py + TAB_H + 28;
  draw_pad(px + 16 + (pw - 32 - PAD_W) / 2, y);
  if (pl.last >= 0 && pl.last < B_COUNT && pl.state >= PL_PRESS && pl.state <= PL_DONE) {
    char b[48];
    snprintf(b, sizeof(b), "%s = %s", btn_names[pl.last], ((pl.mapped >> pl.last) & 1) && pl.rule[0] ? pl.rule : "-");
    gfx_text_left(&f27, x, y + PAD_H + 16, b, gfx_ramp_alpha(look.copyright));
  }
}

// the two ports, one pad under the other
static void draw_test(void) {
  int x = px + 16 + (pw - 32 - PAD_W) / 2;
  for (int i = 0; i < 2; i++) {
    const pad_t *p = &pads[i];
    int y = py + i * (TEST_LABEL_H + PAD_H + 10);
    char b[72];
    if (p->source == USB_PAD_NONE && p->vid == 0) snprintf(b, sizeof(b), "Port %d: no USB pad", i + 1);
    else snprintf(b, sizeof(b), "Port %d: %04x:%04x, %s", i + 1, p->vid, p->pid, pad_source(p));
    gfx_text_left(&f27, x, y + TEST_LABEL_H / 2 - 2, b, gfx_ramp_alpha(look.text));
    draw_port = i;
    draw_pad(x, y + TEST_LABEL_H);
  }
}

static void draw(int x, int y, int w, int h, int focused) {
  (void) h;
  px = x; py = y; pw = w; pfocused = focused;
  if (view == V_LIST) draw_list();
  else if (view == V_TEST) draw_test();
  else draw_mapper();
}

// ---- the page ----

static void show(void) { port = 0; list_msg = 0; view = V_LIST; show_list(); }
static void hide(void) { if (view == V_MAPPING) learn_do(0); view = V_LIST; }
static void open(void) { if (view != V_LIST) show_list(); }
static void close(void) { if (view != V_LIST) show_list(); }

static int tick(void) {
  uint16_t joy = (uint16_t) (R_JOY | (R_JOY >> 16));
  if (!joy_released && joy == 0) joy_released = 1;
  if ((view == V_LIST || view == V_TEST) && R_MS - timer >= LIST_REFRESH_MS) {
    // the pads come and go
    pad_t now[2];
    timer = R_MS;
    if (read_pads(now) && memcmp(now, pads, sizeof(now)) != 0) {
      memcpy(pads, now, sizeof(now));
      if (list_btn >= list_btns()) list_btn = 0;
      dirty = 1;
    }
  }
  if (view == V_TEST) {
    uint16_t l = (uint16_t) R_JOY, r = (uint16_t) (R_JOY >> 16);
    if (l != test_joy[0] || r != test_joy[1]) { test_joy[0] = l; test_joy[1] = r; dirty = 1; }
    if ((l & TEST_EXIT) != TEST_EXIT && (r & TEST_EXIT) != TEST_EXIT) test_exit = R_MS;
    else if (R_MS - test_exit >= TEST_EXIT_MS) stop_test();
  } else if (view == V_MAPPING && R_MS - timer >= LEARN_POLL_MS) {
    timer = R_MS;
    uint8_t st = pl.state, b = pl.btn, s = pl.left_s;
    uint16_t live = pl.live, m = pl.mapped;
    read_learn();
    if (pl.state != st || pl.btn != b || pl.left_s != s || pl.live != live || pl.mapped != m) dirty = 1;
  }
  int d = dirty;
  dirty = 0;
  return d;
}

static int list_key(const key_ev_t *e) {
  int k = e->key;
  if (k == K_ESC) return 0;
  if (k == K_UP || k == K_DOWN) { port ^= 1; list_msg = 0; }
  else if (k == K_RIGHT || k == K_TAB) list_btn = (list_btn + 1) % list_btns();
  else if (k == K_LEFT) list_btn = (list_btn + list_btns() - 1) % list_btns();
  else if (k == K_ENTER && list_btn == 0) start();
  else if (k == K_ENTER && list_btn == 1) start_test();
  else if (k == K_ENTER) forget();
  if (list_btn >= list_btns()) list_btn = 0;
  return 1;
}

static int key(const key_ev_t *e) {
  if (e->pad && !joy_released) return 1;   // the keys of the Start+Mode that ended the test
  if (view == V_LIST) { int r = list_key(e); dirty = 0; return r; }
  if (e->pad) return 1;                    // it may be the pad being mapped
  if (e->key == K_ESC || (view == V_MAPPING && e->key == K_ENTER && pl.state >= PL_SAVED)) show_list();
  else if (view != V_MAPPING) {}
  else if (e->key == K_ENTER && pl.state == PL_PRESS) learn_do(1);
  else if (e->key == K_ENTER && pl.state == PL_DONE) learn_do(3);
  else if (e->key == K_BKSP) learn_do(2);
  dirty = 0;
  return 1;
}

static void click(int id) {
  if (id == HIT_PORT || id == HIT_PORT + 1) {
    if (port != id - HIT_PORT) list_msg = 0;
    port = id - HIT_PORT;
    if (list_btn >= list_btns()) list_btn = 0;
  } else switch (id) {
    case HIT_MAP: start(); break;
    case HIT_TEST: list_btn = 1; start_test(); break;
    case HIT_FORGET: list_btn = 2; forget(); break;
    case HIT_SKIP: learn_do(1); break;
    case HIT_BACK: learn_do(2); break;
    case HIT_SAVE: learn_do(3); break;
    case HIT_DONE: show_list(); break;
  }
  dirty = 0;
}

static const char *hint(void) {
  if (view == V_TEST) return "Hold Start+Mode for 2 s to go back";
  if (view == V_LIST)
    return list_btn == 2 ? "Forget: the driver ini becomes .bak, the default map is used" :
           list_btn == 1 ? "Test: the buttons held on both ports, as the core gets them" :
                           "Assign: learns the pad you press a button on, also one not on a port";
  switch (pl.state) {
    case PL_PRESS: return "Enter: skip   Backspace: back";
    case PL_DONE: return "Enter: save the driver   Backspace: redo the last one";
    case PL_SAVED: case PL_FAILED: return "Enter: back to the ports";
    default: return "";
  }
}

static const char *keys(void) {
  return view == V_LIST ? "Up/Down: port   Left/Right: button   Enter: OK   Esc: back" :
         view == V_TEST ? "Esc: back to the ports (the keyboard), or Start+Mode held on the pad" :
                          "Esc: back to the ports (the keyboard or the mouse, not the pad)";
}

const cpage_t page_pad = { open, close, show, hide, draw, key, tick, click, hint, keys };
