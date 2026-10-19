#!/bin/bash
# The rewrites against the old code of the core (sim/ref, the versions of new_hdmi_pll d31dec0, renamed *_ref):
#   tb_eq_overlay (iverilog): kg_osd_overlay with the parameters of the tops against overlay.vhd (a netlist of
#                             ghdl --synth), frame by frame
# Usage: src/sim/run_eq_sim.sh
# Each test prints "<tb>: 0 errors"; the script fails on any other count.

set -e
SIM="$(cd "$(dirname "$0")" && pwd)"
SRC="$(dirname "$SIM")"
RTL="$SRC/rtl"
M="$SRC/../modules"
OUT="$SIM/build/eq"
mkdir -p "$OUT"

result() {
	grep -E "ERROR|$1: " "$2" | head -12
	grep -q "$1: 0 errors" "$2" || { echo "$1 FAILED"; exit 1; }
}

# overlay.vhd instantiates dpram.v: the VHDL model of modules; DEFAULT is a reserved word of VHDL-2008
ghdl --synth --std=93c -fexplicit --ieee=synopsys -gDEFAULT=1 --out=verilog "$M/sim/mcu/dpram_sim.vhd" \
	"$SIM/ref/pix_doubler_ref.vhd" "$SIM/ref/overlay_ref.vhd" -e overlay_ref > "$OUT/overlay_ref_vhdl.v" 2> "$OUT/ghdl.log" \
	|| { cat "$OUT/ghdl.log"; exit 1; }
iverilog -g2001 -o "$OUT/tb_eq_overlay" "$SIM/tb_eq_overlay.v" "$OUT/overlay_ref_vhdl.v" "$RTL/vga/vga_sync.v" \
	"$M/rtl/video/osd/kg_osd_overlay.v" "$M/rtl/video/osd/kg_pix_doubler.v" "$M/rtl/mem/kg_sdpram.v" 2>&1 \
	| grep -v 'found no sensitivities' || true
vvp -n "$OUT/tb_eq_overlay" > "$OUT/tb_eq_overlay.log"
result tb_eq_overlay "$OUT/tb_eq_overlay.log"

echo "EQ SIMULATION PASSED"
