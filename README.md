# Karabas Go Menu Core

A basic FPGA core to show a core selector

## USB MIDI keyboard

A USB MIDI keyboard on the USB host of the board plays the SAM2695 synth while the menu runs: the core answers `CMD_CAPS` with bit 4 (MIDI IN), the firmware sends it the bytes of the keyboard (`CMD_MIDI`, firmware v26103114 or later) and the core puts them out on `MIDI_TX` at 31250 baud (`kg_mcu_midi`, `kg_midi_tx` of karabas-go-modules).

## SRAM clear

The SRAM of the board keeps its content over a core load and an MCU reboot (only a power cycle clears it), so a core would start with the memory of the one before. The menu fills both SRAM chips with zeros once after it starts (`src/rtl/mem/sram_clear.v`, 2M 16-bit writes of 98 ns, 0.2 s). A core chosen before the end just loads. The firmware (karabas-go-firmware #110 or later) loads the menu before a reload of the core (the `reload` button action, `CMD_FLASHBOOT`), so a reloaded core gets clean memory too. The SDRAM is not cleared.
