#!/bin/sh
# tb_midi_mux (iverilog): the MIDI streams of the keyboard and of the menu CPU (menu_midi_mux)
set -e
cd "$(dirname "$0")"
iverilog -g2001 -o tb_midi_mux.vvp tb_midi_mux.v ../../rtl/soc/menu_midi_mux.v ../../../modules/rtl/mem/kg_fifo.v
vvp -n tb_midi_mux.vvp | grep -v finish
