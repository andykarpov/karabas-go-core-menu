#!/bin/sh
# tb_menu_soc (Verilator): menu_soc + menu_video + kg_l2_rpc, the program sw/build/menu.bin against a model of
# the MCU (the core list over RPC, Down, Enter, LAUNCH), a frame -> frame.ppm
set -e
cd "$(dirname "$0")"
make -s -C ../../sw
M=../../../modules/rtl
verilator --cc --exe --build -O2 -j 4 -Wno-fatal -Wno-WIDTH -Wno-UNUSED -Wno-PINCONNECTEMPTY -Wno-CASEINCOMPLETE \
  -Wno-UNOPTFLAT -Wno-MULTIDRIVEN -Wno-SYNCASYNCNET -Wno-TIMESCALEMOD -Wno-DECLFILENAME -Wno-PINMISSING \
  --top-module tb_menu_soc -Mdir obj_soc \
  tb_menu_soc.v ../../rtl/soc/menu_soc.v ../../rtl/soc/menu_sram.v ../../rtl/soc/vexriscv/VexRiscv.v \
  ../../rtl/vga/menu_video.v ../../rtl/vga/menu_bg.v ../../rtl/vga/menu_compose.v $M/ctl/kg_sync.v $M/mem/kg_sdpram.v $M/mem/kg_tdpram.v $M/mcu/kg_l2_rpc.v tb_menu_soc.cpp > obj_soc.log 2>&1 || { tail -30 obj_soc.log; exit 1; }
./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin
