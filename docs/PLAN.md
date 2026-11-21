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
  `read_roms()` runs for the boot core too): the CPU is held in reset until `LOAD END`. The program
  of the CPU is updated without a new bitstream (user, 2026-11-21).
- The code runs from SRAM behind the VexRiscv caches (I$ / D$ 4K of the Doom config), not from
  BRAM: no RAMB16 limit for the later file manager / Wi-Fi code; Ф0 measures the speed with the
  scanout on the same SRAM (fallback: code in BRAM, loaded the same way).
- Setup RTC and Map USB gamepad are **rewritten on the CPU** too (the firmware keeps its own for the
  F12 OSD of the other cores); the gamepad learning stays in the firmware (`hid_learn`) and is called
  over RPC.
- Wi-Fi: the ESP8266 is behind the FPGA and belongs to the core (user, 2026-11-21): the menu CPU
  talks AT to it directly (no RPC); PORT 1 (the passthrough to the MCU) goes from the boot core.
- Fallback: a boot core without the RPC caps bit (an old `boot.kg` with a new firmware) gets only a
  message "update boot.kg" in its bitmap OSD and the MSC hint (BTN2); no menu in the firmware.
- SRAM: the code and the frame buffer are in it, so the clear before a core is done by the
  hardware: the CPU writes the `CORES LAUNCH` request and sets "clear, then call"; `sram_clear`
  stops the CPU, fills the SRAM and only then makes the request pending for the MCU. The fill at
  start stays.

## Phases

| Phase | Work | Gate |
|---|---|---|
| Ф0 | Fit gate on Mini G (`-t 2`): VexRiscv, the program loaded into SRAM by `LOAD` from `boot.kg`, frame buffer + palette + scanout from SRAM, a stub blitter, "hello" drawn by C; the SRAM shared by scanout / CPU / blitter at 40.9 MHz; `sram_clear` kept; the existing OSD / ESP / MIDI / SD2 / FT812 paths untouched | LUT, RAMB16, timing met (and `minig-coremode`), a frame on the board |
| Ф1 | RPC: the spec into firmware `docs/link_v2.md` (+ `.ru`); modules `kg_l2_rpc` + testbench; firmware server (SYS, CORES, CFG) + the fallback; menu: a plain core list and launch | sims `0 errors`, a core starts from the new menu on Mini G |
| Ф2 | Graphics: the full blitter, the font pipeline, logo / background, mouse cursor, key sounds; the core browser in the FT look | **UI approval of the user** (frames) |
| Ф3 | Setup on the CPU: Settings (CFG), RTC, Wi-Fi (ESP direct), Map USB gamepad (HID learn over RPC), About, autoload | UI approval, hw on Mini G |
| Ф4 | The boot UI removed from the firmware; Go and Mini builds; **release of the minimal version** | sims, the three boards, one PR per repo |
| Ф5 | Later iterations: two-panel file manager (copy / move / delete between the cards, see below), RPC FS (generic, sandboxed) + the guide for other cores | UI approval, hw |

## Ф5: two-panel file manager (later, after the minimal release)

Drives of the CPU, one VFS in C (FatFs, BSD-style licence) over three back ends:

| Drive | Where it is wired | Access from the menu |
|---|---|---|
| SD1 (cores, configs) | RP2040 | RPC FS (the MCU owns the card) |
| SD2 | FPGA (`SD_*`; now passed to the MCU for `msc2`) | local: an SPI master in the FPGA, FatFs on the CPU; the passthrough to the MCU is off while the menu uses it (`msc2` and the menu exclusive) |
| CF | FPGA (`WA` / `WD` / `WCS_N` IDE bus: Go, the shield of Mini G) | local: a CF / ATA PIO port in the FPGA, FatFs on the CPU |

A copy SD1 <-> SD2 / CF goes through the RAM of the CPU (RPC FS reads / writes 512-byte blocks).
Keep the space for it in Ф0-Ф4: the pins of SD2 and CF stay free in the RTL of the menu, the
memory map of the CPU has slots for the two ports, RPC FS stays in the spec.

## Status

- Ф0 DONE 2026-11-21 (67c932d): Mini G 5695 LUT (37 %), 6 RAMB16 + 10 RAMB8, timing met (75 MHz:
  12.886 ns, 0.45 ns of slack), coremode met; sim `src/sim/soc/run_soc_sim.sh` 0 errors; on the board
  as a separate test core `menutest.kg` (type osd, the real `boot.kg` untouched): the frame drawn by
  the CPU, the frame counter running, the keyboard report read. Test cores go to the card under
  another name until the release (user).

- Ф1 DONE 2026-11-21 (core-menu 37507eb, modules feature/rpc e4c1cde, firmware feature/rpc):
  kg_l2_rpc (target 08) + tests, firmware RPC server (SYS, CFG, CORES) + spec 5.9 (en / ru); on
  Mini G with the feature/rpc firmware: the real core list over RPC, keys, LAUNCH of Atari 2600;
  an old boot.kg with the new firmware works as before (its own menu). Mini G 40 % LUT, timing met.
  The released firmware 26112021 was put back on the bench after the check. The fallback message
  for an old boot.kg comes with Ф4 (the firmware menu goes then). Test builds run as `menutest.kg`
  of type osd (CORES in its HELLO); a boot type core cannot be started by AT load.

- Ф2 look APPROVED by the user 2026-11-21 ("1-в-1", docs/img/f2_compare_teal.png, 8464ada): the
  core list hides the cores with `visible: false` (the boot core too: that is what the flag is for).
  Left in Ф2: the mouse pointer with the hover frame, the autoload gauge on the button, the key and
  start sounds. Checked in the firmware: with the OSD closed (a boot core with RPC) no key reaches the
  screens of the firmware (osd_ui.c: only while is_osd; osd_toggle does nothing in the boot core).

- Sounds (the last part of Ф2, design 2026-11-21, user): (1) PCM: raw signed 16-bit little-endian
  mono samples (no u-law: that was for the FT812 only) in the ROM section of boot.kg, loaded into the
  SRAM; a player in RTL (a slow read port of menu_sram, 2 channels: address, length, rate, volume,
  play / stop), added with saturation to the DAC / HDMI mix; the four start sounds of the FT menu
  (key1..4 of the firmware bitmaps.h) converted once by a script, own phrases from WAV (mono, the rate,
  silence trimmed); an 8-bit mode only if a sound gets long. (2) MIDI: the CPU writes MIDI bytes to
  the SAM2695 (MIDI_TX) on its own channel, a mux in front of kg_midi_tx switches at whole messages
  and repeats the running status of the USB MIDI keyboard after a menu message. PCM first.

- Ф2 DONE 2026-11-21 (e1ef50a and before): the look approved, mouse (latched buttons, checked on
  the board: hover, select, start), autoload gauge, sounds: the start sounds as raw PCM (menu_sound;
  on the board the recorded envelope matches key1, the user heard it) and the key click as a MIDI
  Cowbell on the SAM2695 (menu_midi_mux; heard on the board). The bench keeps the feature/rpc
  firmware until the release (user), its karabas.ini has [ft812] sound=1 now (user).

## Open items

- The menu resolution: 800 x 600 now; the FT modes of `ft_height()` go away.
- FS sandbox for other cores: a `core.json` key with the allowed roots (tools `make_core`).
