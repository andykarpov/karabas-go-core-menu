// The sounds of the menu: MIDI on the SAM2695 (menu_midi_mux): the key click and the start melody.
#pragma once
#include <stdint.h>

void midi_send(const uint8_t *msg, int len);   // a whole message with its status byte
void midi_click(void);                         // the key click: GM percussion Cowbell (as the FT812 menu)
void melody_start(void);                       // the start melody ([ft812] sound), from its beginning
int melody_poll(void);                         // its next notes when they are due; 1 while it plays
void melody_stop(void);                        // all its notes off at once
