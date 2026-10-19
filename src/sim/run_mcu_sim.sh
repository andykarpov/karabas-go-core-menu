#!/bin/bash
# The MCU link of the tops against the old one (ISim, ISE 14.7):
#   tb_mcu_esp: kg_mcu with the parameters of the tops and esp8266.vhd against mcu.vhd / esp8266.vhd of the core
#               (sim/ref), the commands of the menu, the ESP uart both ways, CMD_CAPS
# Usage: src/sim/run_mcu_sim.sh [path to ISE_DS/settings64.sh]
# Prints "tb_mcu_esp: 0 errors"; the script fails on any other count.

set -e
SETTINGS=${1:-/opt/Xilinx/14.7/ISE_DS/settings64.sh}
SIM="$(cd "$(dirname "$0")" && pwd)"
SRC="$(dirname "$SIM")"
RTL="$SRC/rtl"
M="$SRC/../modules/rtl"
OUT="$SIM/build/mcu"
mkdir -p "$OUT"
set --
source "$SETTINGS" > /dev/null
cd "$OUT"

{
	for f in mem/kg_fifo.v mem/kg_sdpram.v mcu/kg_spi_slave.v mcu/kg_burst_rx.v mcu/kg_mcu_link.v mcu/kg_mcu_ctl.v \
		mcu/kg_mcu_hid.v mcu/kg_mcu_loader.v mcu/kg_mcu_rtc.v mcu/kg_mcu_uart.v mcu/kg_mcu_img.v mcu/kg_mcu_debug.v \
		mcu/kg_mcu_esp.v mcu/kg_mcu.v; do
		echo "verilog work \"$M/$f\" -i \"$M/mcu\""
	done
	echo "vhdl work \"$RTL/uart/uart_rx.vhd\""
	echo "vhdl work \"$RTL/uart/uart_tx.vhd\""
	echo "vhdl work \"$RTL/uart/esp8266.vhd\""
	echo "vhdl work \"$SIM/ref/fifo_ref.vhd\""
	echo "vhdl work \"$SIM/ref/spi_slave_ref.vhd\""
	echo "vhdl work \"$SIM/ref/mcu_ref.vhd\""
	echo "vhdl work \"$SIM/ref/esp8266_ref.vhd\""
	echo "verilog work \"$SIM/tb_mcu_esp.v\""
} > tb_mcu_esp.prj
fuse -prj tb_mcu_esp.prj -top work.tb_mcu_esp -o tb_mcu_esp.exe > fuse.log || { grep ERROR fuse.log; exit 1; }
echo "run all; quit" > run.tcl
./tb_mcu_esp.exe -tclbatch run.tcl > tb_mcu_esp.log 2>&1 || true
grep -E "ERROR|tb_mcu_esp: " tb_mcu_esp.log | head -20
grep -q "tb_mcu_esp: 0 errors" tb_mcu_esp.log || { echo "tb_mcu_esp FAILED"; exit 1; }
echo "MCU SIMULATION PASSED"
