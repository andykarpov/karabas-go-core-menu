#!/bin/sh
# tb_menu_soc (Verilator): menu_soc + menu_video + kg_l2_rpc, the program sw/build/menu.bin against a model of
# the MCU (the core list over RPC, Down, Enter, LAUNCH), a frame -> frame.ppm (640 x 480; the last run 800 x 600)
set -e
cd "$(dirname "$0")"
make -s -C ../../sw
M=../../../modules/rtl
verilator --cc --exe --build -O2 -j 4 -Wno-fatal -Wno-WIDTH -Wno-UNUSED -Wno-PINCONNECTEMPTY -Wno-CASEINCOMPLETE \
  -Wno-UNOPTFLAT -Wno-MULTIDRIVEN -Wno-SYNCASYNCNET -Wno-TIMESCALEMOD -Wno-DECLFILENAME -Wno-PINMISSING \
  --top-module tb_menu_soc -Mdir obj_soc \
  tb_menu_soc.v ../../rtl/soc/menu_soc.v ../../rtl/soc/menu_sram.v ../../rtl/soc/menu_sound.v ../../rtl/soc/vexriscv/VexRiscv.v \
  ../../rtl/vga/menu_video.v ../../rtl/vga/menu_compose.v $M/ctl/kg_sync.v $M/mem/kg_sdpram.v $M/mem/kg_tdpram.v $M/mcu/kg_l2_rpc.v tb_menu_soc.cpp > obj_soc.log 2>&1 || { tail -30 obj_soc.log; exit 1; }
./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin
# the mouse: the pointer onto the fourth core, two quick clicks start it (buttons latched until read)
echo "tb_menu_soc: the mouse run"
TB_MOUSE=1 TB_FRAME=frame_mouse.ppm ./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin | grep -E "tb_menu_soc|launch"
# the wheel: a notch towards the user moves the focus down as the Down key, Enter starts b.kg
echo "tb_menu_soc: the wheel run"
TB_WHEEL=1 TB_FRAME=frame_wheel.ppm ./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin | grep -E "tb_menu_soc|launch"
# the start sound of [ft812] sound after Enter: PCM out of menu_sound
echo "tb_menu_soc: the sound run"
TB_SOUND=1 TB_FRAME=frame_sound.ppm ./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin | grep -E "tb_menu_soc|sound"
# Setup: S, a frame of it (the other buffer, its own palette), Esc back to the list, Down, Enter
echo "tb_menu_soc: the Setup run"
TB_SETUP=1 TB_FRAME=frame_setup_list.ppm ./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin | grep -E "tb_menu_soc"
# Wi-Fi: the section asks the status of the ESP over its uart (MMIO ESP / ESP_OWN, a model of the ESP)
echo "tb_menu_soc: the Wi-Fi run"
TB_WIFI=1 TB_FRAME=frame_wifi.ppm ./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin | grep -E "tb_menu_soc|ESP"
# 800 x 600 ([ft812] video_mode=1): the CPU sets VMODE, the pixel clock and the frame follow; Setup in it
echo "tb_menu_soc: the 800 x 600 run"
TB_LOOK=video_mode=1 TB_SETUP=1 TB_FRAME=frame_800.ppm ./obj_soc/Vtb_menu_soc ../../sw/build/menu.bin | grep -E "tb_menu_soc"
