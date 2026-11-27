// The parts of the boot animations (intro.c, show.c): fixed point, the double buffered frames timed by
// R_MS, light drawn by levels (the brighter pixel stays), pixel pictures with a glow, MIDI by a table.
// The palette (the same entries in both banks) is laid out by the caller: [glow_lo, solid_lo) the glow
// of the pictures (ramps of 15 levels), [solid_lo, light_lo) their pixels (never drawn over while
// fx_solid_out), [light_lo, light_hi) the light (ramps of 15 levels).
#pragma once
#include <stdint.h>

#define Q 1024
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int ramp(int t, int t0, int t1) { return t <= t0 ? 0 : t >= t1 ? Q : (t - t0) * Q / (t1 - t0); }
static inline int ease_in(int u) { return u * u / Q * u / Q; }
static inline int ease_out(int u) { int v = Q - u; return Q - v * v / Q * v / Q; }
static inline int ease_io(int u) { return u * u / Q * (3 * Q - 2 * u) / Q; }
static inline int lerp(int a, int b, int u) { return a + (b - a) * u / Q; }
uint32_t fx_mix(uint32_t a, uint32_t b, int u);   // a -> b

// ---- the frames ----
extern volatile uint8_t *fx_fb;  // the buffer drawn into
extern int fx_solid_out;         // the pixels of the pictures are shown: no light over them
void fx_layout(int glow_lo, int solid_lo, int light_lo, int light_hi);
void fx_start(void);             // both buffers empty, their palettes free, the background black
int fx_hold(int ms);             // black for ms (a key cuts it short: 1)
int fx_hold_monitor(void);       // black until the monitor shows the picture: 2 s after the start of the
                                 // core (640 x 480) or after a switch to 800 x 600; 1 if a key cut it
int fx_cut_short(void);          // a key or a click
int fx_frame(void);              // the hidden buffer: its rectangles of two frames ago cleared; returns it
void fx_show(int b, void (*background)(int t), int t);   // shown at the next frame start, then the background
extern void (*fx_idle)(void);    // called while fx_show waits for the frame start (the MIDI of show.c)
void fx_end(void);               // the background of the look, the palettes free for the menu
void fx_mark(int x, int y, int w, int h);                 // drawn: cleared in this buffer two frames later
extern int fx_nomark;            // 1: fx_disc / fx_streak mark nothing (the caller marks a rectangle around)
void fx_put(int x, int y, int base, int lev);             // a light pixel of ramp base, the brighter stays
void fx_disc(int cx, int cy, int r, int lev, int base);   // a soft disc, lev in the middle
void fx_streak(int cx, int cy, int hl, int i, int base);  // a horizontal streak, half length hl, brightness i (Q)

// ---- a pixel picture x s with its glow (gpad pixels of glow around it) ----
typedef struct { uint8_t *comp; int cw, ch, s, pad; } fx_img_t;
// px: w x h, 0 none, 1..ngroups the colour groups; comp gets solid_base + g - 1 on the pixels,
// glow_base + (g - 1) * 15 + level - 1 around them; w + 6 <= 62, h + 6 <= 25
void fx_glow(fx_img_t *im, const uint8_t *px, int w, int h, int ngroups, int s, int solid_base, int glow_base);

// ---- MIDI by a table of times (ms) ----
typedef struct { uint16_t at; uint8_t st, d1, d2; } fx_ev_t;
void fx_send3(uint8_t a, uint8_t b, uint8_t c);           // 2 bytes for a program change
void fx_score(const fx_ev_t *ev, int n);
void fx_score_poll(int t);       // the events due, at most 5 messages a frame
void fx_score_pump(int t);       // the events due while the fifo takes them (called several times a frame)
void fx_sound_end(int c0, int c1);   // channels c0..c1: notes off, controllers reset, program 0 (not 9)
