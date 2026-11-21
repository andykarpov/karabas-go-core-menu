// The registers of menu_soc (rtl/soc/menu_soc.v): word registers at 0x80000000.
#pragma once
#include <stdint.h>

#define MMIO(off) (*(volatile uint32_t *) (0x80000000u + (off)))
#define R_DBG    MMIO(0x00) // W: a byte of the debug console
#define R_FB     MMIO(0x04) // W: bit 0 the buffer to show from the next frame; R: the one shown
#define R_FRAMES MMIO(0x08) // R: frames begun
#define R_CYCLES MMIO(0x0c) // R: clocks since the start
#define R_SHOW   MMIO(0x10) // W: bit 0 the frame buffer on the screen
#define R_MS     MMIO(0x1c) // R: milliseconds since the start
#define R_KBD0   MMIO(0x28) // R: { key 3, key 2, key 1, modifiers }
#define R_KBD1   MMIO(0x2c) // R: { 0, key 6, key 5, key 4 }
#define R_KBDN   MMIO(0x30) // R: keyboard reports seen
#define R_MOUSE  MMIO(0x34) // R: buttons 31..24, dy 23..12, dx 11..0 (reading clears)
#define R_JOY    MMIO(0x38) // R: { joy_r, joy_l }
#define R_US     MMIO(0x40) // R: microseconds since the start
#define R_PAL(i) MMIO(0x400 + 4 * (i)) // W: 0x00RRGGBB

#define FB_W 800
#define FB_H 600
#define FB0 ((volatile uint8_t *) 0x300000)
#define FB1 ((volatile uint8_t *) 0x380000)
