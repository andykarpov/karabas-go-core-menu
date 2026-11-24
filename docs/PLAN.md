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
| Ф6 | Core groups (see below): one level of groups in the core list, the defaults of the menu, `[groups]` of karabas.ini, a core moved to another group; later the `group` key of `core.json` (kgo2), Recent / Favorites | UI approval, hw |

## Ф4: what leaves the firmware (user, 2026-11-21: the FT812 menu is not needed any more)

- Both menus of the boot core go: the FT812 one (app_core_browser FT part, app_ft_setup, app_ft_clock,
  app_ft_wifi, app_ft_pad, ui/ft_page, ui/ft_mouse, the FT sounds / bitmaps, the FT detection at the
  boot core start with its Space fallback, ft_init and the video modes) and the text one (the core
  browser and Setup screens of the OSD: app_setup, app_settings, app_rtc, app_wifi, app_pad_mapper,
  app_about as far as only the boot core uses them). What the menu CPU does now over RPC stays in
  the firmware as services (the core list, karabas.ini, the RTC, pad_learn, the matrix).
- Stays: the FT812 reset pin and the bus control (ft_begin / ft_reset before a core: cores drive the
  FT812 themselves), the OSD of the other cores, MSC, the AT+KARABAS control.
- karabas.ini keeps its [ft812] section (the look of the CPU menu reads it); enabled, video_mode and
  3d_buttons become unused (left in the file, not written).
- An old boot.kg (no RPC in its caps) with the new firmware: a message and the MSC hint only.
- Check what AT+KARABAS commands open firmware screens (at_control.c "ft_setup", "wifi", "rtc",
  "settings") and what the hands-free tests of other repos use.

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

## The boot core in the flash of the RP2040 (user, 2026-11-21; to think over before the release)

Goal: the board starts the menu without the SD1 card (no card, a broken card, a card without
boot.kg). The menu then shows "No cores on the SD card" and Setup (Wi-Fi, the clock, the pads) still
works. To decide:

- Size. boot.kg of the Mini G (2026-11-21): bitstream 737 KB + menu.bin 216 KB + sounds.bin 319 KB =
  ~1.27 MB (since 2026-11-23 no sounds.bin and menu.bin ~117 KB: ~0.85 MB); the firmware ~0.5 MB. The firmware is built for PICO_BOARD pico (2 MB of flash): 1.8 MB
  of 2 is tight. Check the flash chip of each board (Go / Mini / Mini G, maybe 16 MB). If it is 2 MB:
  keep the sounds out of the flash copy (the menu runs without them), compress the bitstream
  (Spartan-6 bitstreams pack well, about 2-3x), or both.
- How it gets there. (a) A part of the uf2: boot.kg (per board, the uf2 is per board already)
  linked into the firmware image or a second uf2 block range, updated with each firmware release.
  (b) A cache: the firmware copies boot.kg from SD1 into a flash partition when it is newer (build
  id), and uses the copy when the card has none. (c) Both: (a) as the factory copy, (b) on top.
  (a) is simple and always matches the firmware (the RPC versions); (b) keeps the menu updatable
  from the card without a firmware flash, which is the point of menu.bin in the ROMs of boot.kg.
- Which one wins. SD1 boot.kg first (a user build, a test), the flash one when it is missing or
  does not start (no `'K' 'G' 2` answer after the configuration).
- The loader: core_loader reads the bitstream and the ROMs from a file now; it needs a source
  from the flash (XIP reads) as well, the same kgo2 parsing.
- MSC / msc2 and the firmware update flow (BOOTSEL) stay as they are; a flash write of the cache
  must not run while the core loads.

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

- Ф3 decisions (user, 2026-11-21): the FT-only rows (Graphic menu, Video mode, 3D buttons) are
  dropped from Setup (their karabas.ini keys stay); System > Matrix effects keeps the live preview on
  the LED matrix (RPC CFG PREVIEW); no About page.

- Ф3 step 1 (3a611e5 and after, sim + host frames; on the board next): Setup on the CPU: the
  sections, the settings pages, the colour editor, the drop dialog, the Clock page (date / time
  fields, time zone, Set over SYS TIME_SET; NTP sync comes with Wi-Fi). The geometry of
  app_ft_setup.c, checked against grabs of the FT812 setup on the bench (docs/img/f3_compare.png:
  left the FT812, right the CPU; f3_new_screens.png). A palette bank per frame buffer (menu_video):
  a picture is drawn into the buffer not shown with its own colours, then shown. The ESP uart
  can go to the CPU (MMIO ESP / ESP_OWN, for the Wi-Fi page). Firmware: CFG GET / SET of several
  values a call (one read / one rewrite of karabas.ini), SAVE re-applies the matrix, PREVIEW.
  `src/sw/host/run.sh` draws the screens on the host (a fake firmware) for quick frames.
  On the board 2026-11-21 (Mini G, firmware feature/rpc 6aba736, menutest.kg): every section as on
  the host frames, Save round trip (Mouse speed 5 -> 4 -> 5 written, read back from karabas.ini after
  a reload). Mini G 43 % LUT, 8 RAMB16, timing met (coremode too).

- Ф3 step 2 (5883a1b) Wi-Fi + NTP, on the board 2026-11-21: the CPU has the uart of the ESP while
  Setup runs (ESP_OWN), the firmware's wifi_setup / esp_update / ntp_sync run on it as they are
  (newlib-nano, lib/ = files of the firmware lib/common). The real module: status (AT 1.7.6.0, CTS on,
  the network, IP), a scan (3 networks); NTP sync set the RTC back from 12:00 to the right time in
  ~6 s. Not tried on the board: join, Forget, Upgrade / Reset module (the bench module stays joined).

- Ф3 step 3 (cbe2f05, firmware 18df25f) Gamepad, on the board 2026-11-21: RPC HID (PADS, FORGET,
  LEARN_START / STATE / DO, spec 5.9); the page lists the ports, Test shows both ports (pad presses
  injected by AT, Up+A lit), Assign starts the learning in the firmware ("Press a button on the USB
  pad"), Esc stops it. Not tried on the board: a whole learning and Save (no USB pad on the bench),
  Forget. The HELLO of the core lists SYS, CFG, CORES, HID (RPC_SERVICES 16'h000F) since the Ф4 builds
  (the firmware grants HID to a menu core anyway).

## Video modes (user, 2026-11-22)

Users reported that the new menu is not shown, or shown broken, by some monitors (a photo of a scaler:
the upper half of the picture right, the lower half torn). The menu ran 800 x 600 with the VESA lines
(1056 x 628) at the 40.91 MHz of VCO 450 / 11: 61.7 Hz, 38.7 kHz, a mode no table has, over HDMI not a
CEA mode at all; the FT812 menu before it ran 640 x 480 by default (mode 0 of the firmware until
2026-10-28), so most users had that.

- Two VESA modes, as the FT812 menu of the firmware had: 640 x 480 at 25 MHz (800 x 525, 59.5 Hz,
  syncs negative) and 800 x 600 at 40 MHz (1056 x 628, 60.3 Hz, positive). `[ft812] video_mode` of
  karabas.ini as before (0 / 1; another number of the older firmware is 640 x 480); the default is
  640 x 480, also the mode after the start. Setup > Display > Video mode switches at once with a
  Keep / Back dialog that goes back after 15 s.
- The PLL on VCO 600 MHz: 40.000 (clk_sys), 25.000, 75 (the CPU), 8.000 (FT_CLK_OUT), 12.24 (MIDI_CLK).
  The pixel clock is a BUFGMUX of 25 and 40 MHz; the audio stays on clk_sys.
- The FT812 video over HDMI of the Mini / Mini G is gone (user): the BUFGMUX had no input left for it
  (a cascade of two BUFGMUX is not a dedicated route on Spartan-6), and the firmware does not start it
  for the boot core since Ф4. The Go keeps its FT812 path (V_CLK mux, free).
- The screens are laid out for the mode as the FT812 menu was (`ft_width()` / `ft_height()` of the
  firmware): 8 cores a page in 640 x 480, the Setup tabs 32 px apart, the panel 432 px wide.

## Smaller ROMs (user, 2026-11-23)

The firmware loaded 530 KB of ROMs after the bitstream (menu.bin 218 KB, sounds.bin 312 KB).

- No PCM sounds: roms/sounds.bin, sounds_gen.h and mksounds.py are gone, the program plays none
  (menu_sound.v stays in the bitstreams, silent: kept for a PCM use later, user). The start sound is a MIDI
  melody in menu.bin ("ta-dan": C5 E5 G5 of 120 ms, the chord C6 G5 E5 for 500 ms, GM Vibraphone on
  channel 16, its program back to 0 at the end; chosen from six by the user); Setup > Sound > Start
  sound is On / Off (`[ft812] sound` 1 / 0, the old 2..4 read as On). The key click stays (Cowbell).
- Fonts: one font for all the text (f27, Noto Sans 16 px, ASCII + Cyrillic; the user chose it over
  f28 everywhere, where the footers and hints did not fit); f30 (the clock) has only 32..64 (digits
  and signs); f20 (the small Gamepad labels) ASCII only; f26, f28, f29 are gone.
  A glyph is 8 bytes, found by code ranges (no code point in it).
- The character picture is in runs (char_rle, 16000 -> 5129 bytes).
- The firmware shows no ROM loading screen for the boot core (karabas-go-firmware #171).

## Ф6: core groups (user, 2026-11-24)

The core list gets one level of groups (no groups in groups). Studied: MiST (subfolders of the SD
with the system attribute), MiSTer (`_` folders, `names.txt`, `.mgl` shortcuts, `_@Favorites` of a
script), Analogue Pocket (`category` of `platform.json`, set by the author). Chosen:

- No folders on the card: the `.kg` stay in its root (the file name of 32 chars in `LAUNCH`,
  `[autoload]`, `AT+KARABAS=load`, the flat release `karabas-go/cores`, users copying the files over
  the old ones). The group is a name of the core, not a place.
- The group of a core, the first found: `[groups] <id> = <group>` of karabas.ini (the user; an
  empty value = no group), then the `group` of the core (later, step 2), then the table of the menu
  (`src/sw/groups.c`, by the id, for the cores of today and the ones of other authors). Unknown: no group.
- The groups (English): ZX Spectrum, exUSSR, Computers, Consoles, Games, Utilities; in the list
  ZX Spectrum goes first, all the others (the user's too) by name (user). The table (2026-11-24):
  ZX Spectrum: tsconf, baseconf, profi, zxnext, zxuno, spec256, quadspeccy, quadspeccyx4, sprinter,
  alf, p4096; exUSSR: bk, uknc, vector06c, orion128, orionpro, specialist, rk, pdp2026, criss;
  Computers: apple2, atari800, atarist, archie, bbc, c64, cpc, macplus, msx, msx2, nanomig, ql, vic20,
  agon, gigatron, next186, pcxt, rc2014, v6z80p; Consoles: atari2600, nes, snes, sega, wonderswan,
  uzebox, pong; Games: doom, heretic, hexen; Utilities: boardtest, term, vt100, musicplayer.
- The list: the groups first (a button with the count of its cores), then the cores of no group; a
  group of one core shows that core instead. Enter opens a group; in it the first button is the
  group name with a back arrow in the place of the folder (back), Esc too. The cores inside are in the order of `[setup] core_sort`. At the start
  the group of the autoload / last core is open, that core selected.
- A core moved: G (or the right mouse button) on a core: a list of + New group (a field for the
  name, 20 chars at most), No group, the groups (the one of the core marked; more than fit scroll,
  chevrons on the right); written at once by CFG SET `[groups] <id>`. A group exists while a core
  is in it: a new one is made by moving a core into it. No move from a gamepad (user).
- The footer line of the autoload says, while there is no autoload countdown and the groups are on,
  what the keys do on the entry selected: "G / right click: move to a group" on a core, "Enter: open
  the group", "Enter / Esc: back" (user: the move was not found without it).
- Setup > System > Core groups On / Off (`[setup] core_groups`, default On): Off is the flat list
  of before.
- Steps: (1) the menu: the table, `[groups]`, the list, the move, the setting (the firmware as it
  is: CFG of any section); (2) `group` in `core.json` -> the CORE record of kgo2 (make_core of
  tools), the firmware gives it in `CORES ITEM` as a 6th field (an old menu reads 5); (3) the key in
  the `core.json` of the cores at their next builds; (4) Recent (the last N started) and Favorites
  (`[favorites]`), groups of their own.

## Open items

- FS sandbox for other cores: a `core.json` key with the allowed roots (tools `make_core`).
