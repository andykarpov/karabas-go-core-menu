// The parts shared by the screens of the menu (ui.h).
#include <stdarg.h>
#include <stdio.h>
#include "ui.h"
#include "mmio.h"
#include "rpc.h"
#include "images_gen.h"
#include "sound.h"

// ---- strings ----

void dbg_puts(const char *s) { while (*s) R_DBG = (uint8_t) *s++; }
void dbg_printf(const char *fmt, ...) {
  char b[160];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof(b) - 1, fmt, a);
  va_end(a);
  dbg_puts(b);
  dbg_puts("\n");
}
int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int str_ieq(const char *a, const char *b) { while (*a && lower(*a) == lower(*b)) { a++; b++; } return lower(*a) == lower(*b); }
int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
char *str_cpy(char *d, const char *s) { while ((*d = *s)) { d++; s++; } return d; }
void str_ncpy(char *d, const char *s, int size) { int i = 0; for (; i < size - 1 && s[i]; i++) d[i] = s[i]; d[i] = 0; }

char *u2s(char *p, uint32_t v, int digits) {
  char t[11]; int n = 0;
  do { t[n++] = '0' + v % 10; v /= 10; } while (v || n < digits);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

char *hex6(char *p, uint32_t v) {
  for (int s = 20; s >= 0; s -= 4) *p++ = "0123456789abcdef"[(v >> s) & 15];
  *p = 0;
  return p;
}

int parse_hex(const char *s, uint32_t *v) {
  uint32_t x = 0; int any = 0;
  while (*s == ' ') s++;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  for (; *s && *s != ' '; s++) {
    int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
            *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
    if (d < 0) return 0;
    x = x * 16 + d; any = 1;
  }
  if (any) *v = x;
  return any;
}

int parse_uint(const char *s, int *v) {
  int x = 0, any = 0;
  while (*s == ' ') s++;
  for (; *s >= '0' && *s <= '9'; s++) { x = x * 10 + (*s - '0'); any = 1; }
  while (*s == ' ') s++;
  if (!any || *s) return 0;
  *v = x;
  return 1;
}

// ---- the look ----

look_t look = { 0x010101, 0x0000c8, 0x202026, 0x0000c8, 0xffffff, 0xffffff, 0x787878, 1, 1, 1, 3, 1, 1 };

void look_apply(void) {
  R_GRAD_TOP = look.gradient;
  R_GRAD_BOT = look.bg;
}

// ---- the picture ----

void scr_begin(void) {
  gfx_target(!(R_FB & 1));
  gfx_reset_palette();
  gfx_clear(0, 0, FB_W, FB_H);
}

void scr_end(void) {
  int b = gfx_target_buffer();
  R_FB = (uint32_t) b;
  R_SHOW = 1;
  uint32_t t0 = R_MS;
  while ((int) (R_FB & 1) != b && R_MS - t0 < 100) ;
  gfx_sprite_forget();             // the pointer was in the other buffer
  pointer_show();
}

void draw_logo(void) {
  static uint8_t map[LOGO_COLORS];
  map[0] = 0;
  for (int i = 1; i < LOGO_COLORS; i++) map[i] = gfx_color(logo_pal[i]);
  gfx_indexed(8, 8, LOGO_W, LOGO_H, 4, logo_px, map);   // the logo, x4
}

void draw_clock(void) {
  uint8_t t[16];
  uint16_t n;
  if (!look.time && !look.date) return;
  if (rpc_call(RPC_SYS, 1, 0, 0, t, sizeof(t), &n, 500) != RPC_OK || n < 8) return;
  char s[12], *p;
  ramp_t r = gfx_ramp_alpha(look.text);
  gfx_clear(FB_W - 176, 8, 170, 76);
  if (look.time) {
    p = u2s(s, t[4], 2); *p++ = ':'; p = u2s(p, t[5], 2); *p++ = ':'; u2s(p, t[6], 2);
    gfx_text_center(&f30, FB_W - 88, 30, s, r);
  }
  if (look.date) {
    p = u2s(s, t[3], 2); *p++ = '/'; p = u2s(p, t[2], 2); *p++ = '/'; u2s(p, (t[0] | (t[1] << 8)) % 100, 2);
    gfx_text_center(&f30, FB_W - 88, 60, s, r);
  }
}

void draw_character(void) {
  if (look.chr) gfx_coverage(FB_W - CHAR_W - 8, FB_H - CHAR_H - 8, CHAR_W, CHAR_H, char_cov, gfx_ramp_alpha(0xffffff));
}

// ---- keys ----

// the characters of the keys 0x04..0x38, US layout: without and with Shift
static const char keys_plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\033\b\t -=[]\\#;'`,./";
static const char keys_shift[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\033\b\t _+{}|~:\"~<>?";

static int repeats(uint8_t k) {
  return k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT || k == K_BKSP || k == K_PGUP || k == K_PGDN;
}

#define QMAX 8
static key_ev_t q[QMAX];
static int qn = 0;
static void push(uint8_t key, uint8_t shift, uint8_t pad, uint8_t rep) {
  if (qn >= QMAX) return;
  key_ev_t *e = &q[qn++];
  e->key = key; e->shift = shift; e->pad = pad; e->repeat = rep;
  e->ch = 0;
  if (!pad && key >= 0x04 && key <= 0x38) {
    char c = (shift ? keys_shift : keys_plain)[key - 0x04];
    if (c >= ' ' && c < 127) e->ch = c;
  }
  if (!pad && key >= K_KP1 && key <= K_KP0) e->ch = key == K_KP0 ? '0' : '1' + (key - K_KP1);
  if (key == K_KPENTER) e->key = K_ENTER;
}

// the pad (sega.h of the firmware): up 2, down 4, left 8, right 16, start 32, A 64, B 128, C 256, Mode 4096
static uint8_t pad_key(uint32_t j) {
  return (j & 2) ? K_UP : (j & 4) ? K_DOWN : (j & 8) ? K_LEFT : (j & 16) ? K_RIGHT :
         (j & (64 | 128)) ? K_ENTER : (j & (256 | 4096)) ? K_ESC : (j & 32) ? K_PAD_START : 0;
}

int key_next(key_ev_t *e) {
  static uint8_t prev[6];
  static uint8_t held = 0, held_pad = 0, last_pad = 0;
  static uint32_t t_down = 0, t_rep = 0;
  uint32_t k0 = R_KBD0, k1 = R_KBD1, now = R_MS;
  uint8_t cur[6] = { (uint8_t) (k0 >> 8), (uint8_t) (k0 >> 16), (uint8_t) (k0 >> 24), (uint8_t) k1, (uint8_t) (k1 >> 8), (uint8_t) (k1 >> 16) };
  uint8_t shift = (k0 & 0x22) != 0;
  for (int i = 0; i < 6; i++) {
    if (cur[i] < 0x04) continue;      // none, or the roll over error
    int was = 0;
    for (int j = 0; j < 6; j++) was |= prev[j] == cur[i];
    if (!was) { push(cur[i], shift, 0, 0); held = cur[i]; held_pad = 0; t_down = t_rep = now; }
  }
  int still = 0;
  for (int i = 0; i < 6; i++) { still |= cur[i] == held; prev[i] = cur[i]; }
  if (!held_pad && held && !still) held = 0;
  uint8_t pk = pad_key(R_JOY | (R_JOY >> 16));
  if (pk != last_pad) {
    if (pk) { push(pk, 0, 1, 0); held = pk; held_pad = 1; t_down = t_rep = now; }
    else if (held_pad) held = 0;
    last_pad = pk;
  }
  if (held && repeats(held) && now - t_down > 400 && now - t_rep > 60) { t_rep = now; push(held, shift, held_pad, 1); }
  if (!qn) return 0;
  *e = q[0];
  for (int i = 1; i < qn; i++) q[i - 1] = q[i];
  qn--;
  return 1;
}

char key_digit(uint8_t key) {
  if (key >= K_1 && key <= K_0) return key == K_0 ? '0' : '1' + (key - K_1);
  if (key >= K_KP1 && key <= K_KP0) return key == K_KP0 ? '0' : '1' + (key - K_KP1);
  return 0;
}

// ---- the mouse ----

static const char *const pointer_art[17] = {
  "X               ", "XX              ", "X.X             ", "X..X            ", "X...X           ",
  "X....X          ", "X.....X         ", "X......X        ", "X.......X       ", "X........X      ",
  "X.....XXXXX     ", "X..X..X         ", "X.X X..X        ", "XX  X..X        ", "X    X..X       ",
  "     X..X       ", "      XX        ",
};
pointer_t ptr = { FB_W / 2, FB_H / 2, 0, 0 };
static int m_rx = 0, m_ry = 0;
static uint8_t m_buttons = 0;
static uint32_t m_idle = 0;

void pointer_show(void) {
  if (ptr.visible) gfx_sprite_show(ptr.x, ptr.y, pointer_art, 16, 17, gfx_color(0x000000), gfx_color(0xffffff));
}
void pointer_hide(void) { gfx_sprite_hide(); }

int mouse_next(void) {
  uint32_t m = R_MOUSE;
  int dx = (int) (m << 20) >> 20, dy = (int) (m << 8) >> 20;
  uint8_t b = (uint8_t) (m >> 24);
  static const int mul[6] = { 4, 2, 3, 4, 6, 8 };  // speed 1..5: x0.5 .. x2
  int ev = 0, sp = look.mouse_speed >= 1 && look.mouse_speed <= 5 ? look.mouse_speed : 3;
  if (dx || dy) {
    m_rx += dx * mul[sp]; m_ry += dy * mul[sp];
    int nx = ptr.x + m_rx / 4, ny = ptr.y + m_ry / 4;
    m_rx %= 4; m_ry %= 4;
    ptr.x = nx < 0 ? 0 : nx > FB_W - 1 ? FB_W - 1 : nx;
    ptr.y = ny < 0 ? 0 : ny > FB_H - 1 ? FB_H - 1 : ny;
    ptr.visible = 1;
    m_idle = R_MS;
    ev |= M_MOVED;
  }
  uint8_t pressed = b & ~m_buttons;
  m_buttons = b;
  ptr.buttons = b;
  if (pressed) {
    m_idle = R_MS;
    if (!ptr.visible) { ptr.visible = 1; ev |= M_MOVED; }     // a click while hidden only shows it
    else ev |= ((pressed & 1) ? M_LEFT : 0) | ((pressed & 2) ? M_RIGHT : 0);
  }
  if (ptr.visible && R_MS - m_idle > 5000) { ptr.visible = 0; ev |= M_MOVED; }
  // the wheel moves the focus as Up / Down (a notch a key)
  int dz = (int) (R_WHEEL << 20) >> 20;
  for (int n = 0; n < 4 && dz; n++) { push(dz > 0 ? K_UP : K_DOWN, 0, 0, 0); dz += dz > 0 ? -1 : 1; }
  return ev;
}

// ---- the clickable areas ----

#define HITS_MAX 64
static struct { int16_t x, y, w, h; int id; } hits[HITS_MAX];
static int nhits = 0, hit_taken = 0;

void hit_clear(void) { nhits = 0; hit_taken = 0; }

int hit_add(int x, int y, int w, int h, int id) {
  if (nhits < HITS_MAX) { hits[nhits].x = x; hits[nhits].y = y; hits[nhits].w = w; hits[nhits].h = h; hits[nhits].id = id; nhits++; }
  if (hit_taken || !ptr.visible) return 0;
  if (ptr.x >= x && ptr.x < x + w && ptr.y >= y && ptr.y < y + h) { hit_taken = 1; return 1; }
  return 0;
}

int hit_at(int x, int y) {
  for (int i = 0; i < nhits; i++)
    if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h) return hits[i].id;
  return HIT_NONE;
}

void ui_click(void) { if (look.click) midi_click(); }
