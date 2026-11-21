// The sounds of the menu (sound.h).
#include "sound.h"
#include "mmio.h"

#define SND(ch, r) MMIO(0x80 + 0x10 * (ch) + (r))

void snd_play(int ch, const sound_t *s, uint8_t volume) {
  SND(ch, 0xc) = 0;                                   // stop
  SND(ch, 0x0) = s->addr;
  SND(ch, 0x4) = s->samples;
  SND(ch, 0x8) = (uint32_t) (((uint64_t) s->rate << 16) / 48000);
  SND(ch, 0xc) = 0x100 | volume;
}

void snd_start(int n) { if (n >= 1 && n <= 4) snd_play(0, &sounds[n - 1], 255); else snd_stop(0); }

int snd_playing(int ch) { return (SND(ch, 0xc) >> 8) & 1; }
void snd_stop(int ch) { SND(ch, 0xc) = 0; }

void midi_send(const uint8_t *msg, int len) {
  for (int i = 0; i < len; i++) {
    while (R_MIDI & 1) ;                              // the fifo is full
    R_MIDI = msg[i];
  }
}

void midi_click(void) {
  // channel 10 (percussion), note 56 Cowbell: on, then off
  static const uint8_t on[3] = { 0x99, 56, 100 }, off[3] = { 0x89, 56, 0 };
  midi_send(on, 3);
  midi_send(off, 3);
}
