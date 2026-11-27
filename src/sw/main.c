// The self-drawing boot menu (docs/PLAN.md): the screens of the FT812 menu drawn by the CPU, the core
// list (browser.c) and Setup (setup.c); the settings and the cores of the firmware over RPC.
#include <stdint.h>
#include "mmio.h"
#include "rpc.h"
#include "ui.h"
#include "settings.h"
#include "cores.h"
#include "browser.h"
#include "setup.h"
#include "sound.h"
#include "intro.h"
#include "show.h"

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
  if (rpc_enabled()) { settings_read(); cores_read(); dbg_puts("menu: cores read\n"); }
  else dbg_puts("menu: no RPC\n");
  video_set(set_val[S_VIDEO]);   // [ft812] video_mode (640 x 480 without it, the mode after the start)
  // the boot intro (intro.c) at every start of the menu, a key cuts it short; the autoload counts after it
  int year;
  if (rpc_enabled() && !msc_mode && show_today(&year)) show_play(year, 0, 1);
  else if (rpc_enabled() && !msc_mode && intro_wanted()) intro_play();
  // the cards are USB drives of the PC: the msc screen of the FT812 menu, until the firmware reboots
  if (msc_mode) browser_msc();
  browser_start();
  uint32_t t_draw = R_US;
  browser_open();
  t_draw = R_US - t_draw;
  { char b[40], *p = str_cpy(b, "menu: drawn in "); p = u2s(p, t_draw, 1); str_cpy(p, " us\n"); dbg_puts(b); }

  for (;;) {
    key_ev_t e;
    int m = mouse_next();
    if (setup_active) setup_mouse(m); else browser_mouse(m);
    if (key_next(&e)) { if (setup_active) setup_key(&e); else browser_key(&e); }
    if (setup_active) setup_tick(); else browser_tick();
    melody_poll();                 // the melody heard in Setup as it is chosen
  }
}
