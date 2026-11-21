# Karabas Go Menu Core

The boot core of the Karabas Go, Mini and Mini G boards: it shows the core selector and Setup and asks the firmware to load the chosen core.

The core draws the menu itself: a VexRiscv soft CPU in the FPGA, its program (`src/sw`, built into `src/roms/menu.bin`) and sounds loaded from the ROMs of `boot.kg` into the SRAM, 800 x 600 frame buffers in the SRAM. It talks to the firmware over the RPC target of link v2 (the core list, karabas.ini, the clock, the USB gamepads). The menu is updated with `boot.kg`, without a firmware flash. See `docs/PLAN.md` and `docs/RPC.md`.

## USB MIDI keyboard

A USB MIDI keyboard on the USB host of the board plays the SAM2695 synth while the menu runs: the firmware sends the keyboard bytes to PORT 2 of the link (caps bit 9, MIDI IN) and the core puts them out on `MIDI_TX` at 31250 baud, merged at whole messages with the key sounds of the menu (`menu_midi_mux`, `kg_midi_tx` of karabas-go-modules).

## SRAM clear

The SRAM of the board keeps its content over a core load and an MCU reboot (only a power cycle clears it), so a core would start with the memory of the one before. The menu fills the SRAM with zeros after it starts and again before it asks for a core (`menu_sram`, 84 ms): the program and the frame buffers of the menu are in the SRAM too, so the CPU stops, the SRAM is cleared and only then the request goes to the firmware. The firmware loads the menu before a reload of the core (the `reload` button action), so a reloaded core gets clean memory too. The SDRAM is not cleared.
