// The second boot animation (show.c), on the parts of fx.c.
#pragma once
#include <stdint.h>

int show_play(int year, int count, int hold);   // count: seconds counted down first; hold: black for the monitor first
int show_today(int *year);       // 1 on its day (the RTC), with the year
int show_tick(void);             // in the core list, once a loop: 1 if it played (the list is drawn again)
int show_key(uint8_t key);       // the keys of the core list: 1 when their sequence is complete
int show_year(void);             // the year it is for (the RTC)
