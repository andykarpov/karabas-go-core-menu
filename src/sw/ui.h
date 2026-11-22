// The parts shared by the screens of the menu (the core list, Setup): strings, the look, the
// pictures (double buffered), the keys and the gamepad, the mouse pointer, the clickable areas.
#pragma once
#include <stdint.h>
#include "gfx.h"
#include "fonts.h"

// ---- strings ----
void dbg_puts(const char *s);
void dbg_printf(const char *fmt, ...);            // a line (the newline added)
int str_eq(const char *a, const char *b);
int str_ieq(const char *a, const char *b);          // ignoring the case of ASCII letters
int str_len(const char *s);
char *str_cpy(char *d, const char *s);              // returns the end (the NUL)
void str_ncpy(char *d, const char *s, int size);    // at most size - 1 chars, NUL ended
char *u2s(char *p, uint32_t v, int digits);         // at least digits digits, returns the end
char *hex6(char *p, uint32_t v);                    // 6 lowercase hex digits
int parse_hex(const char *s, uint32_t *v);          // [0x]hex, 1 if it is one
int parse_uint(const char *s, int *v);              // decimal, 1 if it is one

// ---- the look: [ft812] of karabas.ini (settings.c reads it) ----
typedef struct {
  uint32_t bg, gradient, button, active, text, text_active, copyright;
  int time, date, chr, mouse_speed, click, sound;
} look_t;
extern look_t look;
void look_apply(void);           // the background gradient (menu_compose) of the look

// ---- the picture: drawn into the buffer not shown, then shown at a frame start ----
void scr_begin(void);            // the other buffer, cleared, its palette empty
void scr_end(void);              // shown (waits for the frame start); the pointer goes on it
// the video mode (MMIO VMODE): 0 640 x 480, 1 800 x 600; the screens are laid out for scr_w x scr_h (mmio.h)
void video_set(int mode);        // the mode and scr_w / scr_h (the pointer kept on the screen); then draw the screen again
int video_mode(void);
// the parts every screen has: the logo, the clock (top right), the character
void draw_logo(void);
void draw_clock(void);           // reads the RTC (SYS TIME_GET); clears its place first
void draw_character(void);

// ---- keys: the USB keyboard and the pads (a pad as the arrows, A / B Enter, C / Mode Esc) ----
enum {
  K_A = 0x04, K_R = 0x15, K_S = 0x16, K_1 = 0x1E, K_0 = 0x27, K_ENTER = 0x28, K_ESC = 0x29, K_BKSP = 0x2A, K_TAB = 0x2B,
  K_SPACE = 0x2C, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52, K_PGUP = 0x4B, K_PGDN = 0x4E,
  K_HOME = 0x4A, K_END = 0x4D, K_KPENTER = 0x58, K_KP1 = 0x59, K_KP0 = 0x62,
  K_PAD_START = 0xF0,            // the Start button of a pad (no key of the keyboard)
};
typedef struct {
  uint8_t key;                   // HID usage (K_*); a pad gives K_UP .. K_ESC, K_PAD_START
  char ch;                       // the character typed (US layout), 0 if none
  uint8_t shift;
  uint8_t pad;                   // from a pad
  uint8_t repeat;                // a repeat of a held key
} key_ev_t;
int key_next(key_ev_t *e);       // 1 with the next key press (and the repeats of a held one)
char key_digit(uint8_t key);     // '0'..'9' of the digits and the keypad, else 0

// ---- the mouse pointer (firmware ui/ft_mouse.c): hidden after 5 s of rest ----
typedef struct { int x, y, visible, buttons; } pointer_t;   // buttons: held (bit 0 left, 1 right)
extern pointer_t ptr;
enum { M_MOVED = 1, M_LEFT = 2, M_RIGHT = 4 };
int mouse_next(void);            // M_* of the reports since the last call (presses: a hidden pointer shows first)
void pointer_show(void);
void pointer_hide(void);

// ---- the clickable areas of the last picture (firmware ft_hit_add) ----
#define HIT_NONE (-1)
void hit_clear(void);
int hit_add(int x, int y, int w, int h, int id);   // 1 if the pointer is on it (and on no area added before)
int hit_at(int x, int y);                          // the first area added that holds (x, y)

// ---- the clicks of the keys (the MIDI note of the FT812 menu) ----
void ui_click(void);
