// The sounds of the menu: PCM of menu_sound (2 channels of raw s16le in the SRAM) and MIDI notes on
// the SAM2695 (menu_midi_mux).
#pragma once
#include <stdint.h>
#include "sounds_gen.h"

void snd_play(int ch, const sound_t *s, uint8_t volume);
int snd_playing(int ch);
void snd_stop(int ch);
void midi_send(const uint8_t *msg, int len);   // a whole message with its status byte
void midi_click(void);                         // the key click: GM percussion Cowbell (as the FT812 menu)
