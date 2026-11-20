# Self-drawing boot menu: plan

Started 2026-11-21. Branch `feature/selfmenu` (this repo); in karabas-go-modules and
karabas-go-firmware the branch `feature/rpc` (worktrees, the shared checkouts stay as they are).
Phases are commits on these branches; **one PR per repo at the final release**, nothing goes to
master before it.

## Goal

1. The boot core draws the menu itself: a soft CPU and a small 2D video adapter in the FPGA draw
   what the FT812 menu shows now (logo, background gradient, buttons, the core list, setup pages,
   mouse), the same on boards with and without the FT812.
2. All the boot-menu code leaves the RP2040 firmware (the FT812 and the bitmap OSD paths). The
   firmware keeps the core loader, the files, USB, RTC, `karabas.ini`, MSC and the F12 OSD of the
   other cores, and serves the boot core over a new link v2 target **RPC** (`docs/RPC.md`), made
   generic so that other cores can use it too (files, settings, system calls).
3. The menu is updated as `boot.kg`, without a firmware flash.

## Decisions (user, 2026-11-21)

- CPU: VexRiscv rv32im (the config of karabas-go-core-doom: 2195 LUT, 6 RAMB16, ~100 MHz
  estimate under XST), C with gcc. Not Z80 (SDCC, 64 KB, slow); PicoRV32 is the fallback.
- Video: a frame buffer in SRAM, 800 x 600, 8 bpp with a palette (2 pixels a 16-bit word), double
  buffered, plus a blitter (fill, rounded rect, 4 bpp alpha glyphs, bitmap copy) and a hardware
  cursor. Base: `doom_video.v` / `doom_sram.v` of the Doom core.
- Fonts: our own (an OFL font rendered at build time to 4 bpp), not the FT812 ROM fonts. "Looks
  like the FT menu", not pixel exact.
- Code, fonts, logo, background and key sounds go into the ROM section of `boot.kg` (LOAD kind 0,
  `read_roms()` runs for the boot core too): the CPU is held in reset until `LOAD END`.
- Setup RTC and Map USB gamepad are **rewritten on the CPU** too (the firmware keeps its own for the
  F12 OSD of the other cores); the gamepad learning stays in the firmware (`hid_learn`) and is called
  over RPC.
- Wi-Fi: the ESP8266 is behind the FPGA; the menu CPU talks AT to it directly (no RPC).
- Fallback: a boot core without the RPC caps bit (an old `boot.kg` with a new firmware) gets only a
  message "update boot.kg" in its bitmap OSD and the MSC hint (BTN2); no menu in the firmware.
- SRAM: the menu clears the SRAM before it asks for a core (the frame buffer must not reach the
  next core); `sram_clear` at start stays.

## Phases

| Phase | Work | Gate |
|---|---|---|
| Ф0 | Fit gate on Mini G (`-t 2`): VexRiscv, code in BRAM, frame buffer + palette + scanout from SRAM, a stub blitter, "hello" drawn by C; the SRAM shared by scanout / CPU / blitter at 40.9 MHz; `sram_clear` kept; the existing OSD / ESP / MIDI / SD2 / FT812 paths untouched | LUT, RAMB16, timing met (and `minig-coremode`), a frame on the board |
| Ф1 | RPC: the spec into firmware `docs/link_v2.md` (+ `.ru`); modules `kg_l2_rpc` + testbench; firmware server (SYS, CORES, CFG) + the fallback; menu: a plain core list and launch | sims `0 errors`, a core starts from the new menu on Mini G |
| Ф2 | Graphics: the full blitter, the font pipeline, logo / background, mouse cursor, key sounds; the core browser in the FT look | **UI approval of the user** (frames) |
| Ф3 | Setup on the CPU: Settings (CFG), RTC, Wi-Fi (ESP direct), Map USB gamepad (HID learn over RPC), About, autoload | UI approval, hw on Mini G |
| Ф4 | FS service (generic, sandboxed) + the guide for other cores; the boot UI removed from the firmware; Go and Mini builds; release | sims, the three boards, one PR per repo |

## Open items

- Code in BRAM (32-64 KB = 16-32 RAMB16) or in SRAM behind an I-cache: decided by Ф0.
- The menu resolution: 800 x 600 now; the FT modes of `ft_height()` go away.
- The ESP while the menu runs: owned by the CPU; PORT 1 (USB passthrough to the ESP) only when the
  menu does not use it.
- FS sandbox for other cores: a `core.json` key with the allowed roots (tools `make_core`).
