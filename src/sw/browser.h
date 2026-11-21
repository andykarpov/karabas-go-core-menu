// The core list (browser.c).
#pragma once
#include "ui.h"

void browser_start(void);        // the autoload countdown of karabas.ini, if any
void browser_open(void);         // the screen drawn
void browser_reload(void);       // the list read again (after Setup)
void browser_key(const key_ev_t *e);
void browser_mouse(int ev);
void browser_tick(void);
void browser_msc_draw(void);     // the USB drive mode screen (msc_mode)
void browser_msc(void);          // that screen until the firmware reboots, never returns
