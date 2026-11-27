// The sounds of the menu (sound.h).
#include "sound.h"
#include "mmio.h"

#ifdef HOST
void host_midi(const uint8_t *msg, int len);   // host/host.c: kept with its time for a .mid file
#endif

void midi_send(const uint8_t *msg, int len) {
#ifdef HOST
  host_midi(msg, len);
#endif
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

// The start melody, "ta-dan": C5 E5 G5 of 120 ms, then the chord C6 G5 E5 for 500 ms, GM 12
// Vibraphone (program 11) on channel 16: no other sound of the menu or of a USB keyboard uses it;
// its program goes back to 0 (Piano) at the end, for the core after the menu.
#define CH 15
typedef struct { uint16_t at; uint8_t st, d1, d2; } midi_ev_t;   // at: ms from the start
static const midi_ev_t melody[] = {
  {   0, 0xC0 | CH, 11, 0 },
  {   0, 0x90 | CH, 72, 100 }, { 120, 0x80 | CH, 72, 0 },
  { 120, 0x90 | CH, 76, 100 }, { 240, 0x80 | CH, 76, 0 },
  { 240, 0x90 | CH, 79, 100 }, { 360, 0x80 | CH, 79, 0 },
  { 360, 0x90 | CH, 84, 100 }, { 360, 0x90 | CH, 79, 90 }, { 360, 0x90 | CH, 76, 90 },
  { 860, 0x80 | CH, 84, 0 }, { 860, 0x80 | CH, 79, 0 }, { 860, 0x80 | CH, 76, 0 },
  { 860, 0xC0 | CH, 0, 0 },
};
#define MELODY_N ((int) (sizeof(melody) / sizeof(melody[0])))

static int mel_i = MELODY_N;         // the next event; MELODY_N: not playing
static uint32_t mel_t0;

static void ev_send(const midi_ev_t *e) {
  uint8_t m[3] = { e->st, e->d1, e->d2 };
  midi_send(m, (e->st & 0xF0) == 0xC0 ? 2 : 3);
}

void melody_start(void) { melody_stop(); mel_i = 0; mel_t0 = R_MS; melody_poll(); }

int melody_poll(void) {
  while (mel_i < MELODY_N && R_MS - mel_t0 >= melody[mel_i].at) ev_send(&melody[mel_i++]);
  return mel_i < MELODY_N;
}

void melody_stop(void) {
  if (mel_i >= MELODY_N) return;
  // the notes on: the note offs after them, and the program back to 0
  for (; mel_i < MELODY_N; mel_i++) if ((melody[mel_i].st & 0xF0) != 0x90) ev_send(&melody[mel_i]);
}
