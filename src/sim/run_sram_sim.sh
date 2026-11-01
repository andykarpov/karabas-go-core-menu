#!/bin/bash
# The SRAM fill of the menu (iverilog):
#   tb_sram_clear: sram_clear against a model of the SRAM, 12 address bits: every word zero, the WE timing,
#                  a reset in the middle, the bus let go after done
# Usage: src/sim/run_sram_sim.sh
# Prints "tb_sram_clear: 0 errors"; the script fails on any other count.

set -e
SIM="$(cd "$(dirname "$0")" && pwd)"
SRC="$(dirname "$SIM")"
OUT="$SIM/build/sram"
mkdir -p "$OUT"

iverilog -g2001 -Wall -o "$OUT/tb_sram_clear" "$SIM/tb_sram_clear.v" "$SRC/rtl/mem/sram_clear.v" 2>&1 \
	| grep -v 'timescale' || true
vvp -n "$OUT/tb_sram_clear" > "$OUT/tb_sram_clear.log"
grep -E "ERROR|tb_sram_clear: " "$OUT/tb_sram_clear.log" | head -12
grep -q "tb_sram_clear: 0 errors" "$OUT/tb_sram_clear.log" || { echo "tb_sram_clear FAILED"; exit 1; }
echo "SRAM SIMULATION PASSED"
