// Setup (setup.c): the sections on the left, the settings of the chosen one on the right.
#pragma once
#include "ui.h"

extern int setup_active;         // the screen is Setup (else the core list)
void setup_open(void);
void setup_key(const key_ev_t *e);
void setup_mouse(int ev);
void setup_tick(void);
