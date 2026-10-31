# Karabas Go Menu Core

A basic FPGA core to show a core selector

## USB MIDI keyboard

A USB MIDI keyboard on the USB host of the board plays the SAM2695 synth while the menu runs: the core answers `CMD_CAPS` with bit 4 (MIDI IN), the firmware sends it the bytes of the keyboard (`CMD_MIDI`, firmware v26103114 or later) and the core puts them out on `MIDI_TX` at 31250 baud (`kg_mcu_midi`, `kg_midi_tx` of karabas-go-modules).
