# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

The boot menu core of the Karabas Go, Mini and Mini G boards: Xilinx Spartan-6 XC6SLX25, ISE 14.7 (XST). The MCU draws the core selector into the OSD (`kg_osd_overlay` always on, over a black 800x600 frame of `vga_sync`) and loads the chosen core; the FPGA side is only board glue. Three tops: `src/rtl/karabas_go_top.v` (VGA through the FT812 / video DAC), `karabas_mini_top.v` and `karabas_minig_top.v` (HDMI, the FT812 video on HDMI too). The defines HW_ID1 (Go), HW_ID2 (Mini), HW_ID2 + HW_ID3 (Mini G) are set in the `.xst` files. The menu bridges the uart of the ESP8266 module to the MCU (`rtl/uart/esp8266.vhd`, `kg_mcu` with `ENABLE_ESP`, CMD_ESP_UART F8).

The board modules (MCU link, memories, clocks, DACs, OSD, HDMI) come from the git submodule `modules/` ([karabas-go-modules](https://github.com/andykarpov/karabas-go-modules), shared by the Karabas Go cores; clone with `--recursive` or run `git submodule update --init`). A change to them is made there and taken here by updating the submodule commit. Its `CLAUDE.md` and `docs/PLAN.md` have the rules (Verilog-2001 for XST, tests, checks of LUTs and timing).

This core is the loader of every other core: a broken build leaves the board without a menu. Test in simulation and on the hardware of the three boards before a release.

## Build

- `cd src && make` builds the three boards (`make go`, `mini`, `minig` one of them; `make minig-xst`, `-impl`, `-core` one step; `make minig-report` the utilization and timing of the last build, `make minig-coremode` the second timing run of the Mini / Mini G, see Timing). It runs the ISE command-line flow and sources `/opt/Xilinx/14.7/ISE_DS/settings64.sh` (`ISE_SETTINGS=` to change it). The boards share `xst/` and `_ngo/` in `src/`, so they are built one after the other; a board takes a few minutes.
- Inputs per board: `karabas_<board>.prj` (the source list, the shared modules as `"../modules/rtl/..."`), `.xst`, `.ucf`, `.ut`. There is no ISE GUI project (`.xise`); the Makefile has the map / par options (those of tsconf). The map cost table is per board, `MAP_T_<board>` (the Mini 2, the others 1): when a build misses a constraint by a placement (a path without logic and a long route), try another (2...5) before changing the design.
- `core` packs `karabas_<board>.bin` with `core.json` (Go), `core_mini.json`, `core_minig.json` into `boot.kg1` / `kg2` / `kg3` with `make_core.py`. The `.bin` and `.kg*` files are committed as the release; `build` of the json is `date +%y%m%d%H` of the release build.
- The `osd` items of the json are the soft switch registers (Reset, Global Reset).

## Timing

- Clocks (the tops), one PLL from CLK_50MHZ (VCO 450 MHz): clk_sys 40.91 MHz (everything of the menu, the 800x600 frame; the old comments said 40), clk_8 8.18 MHz (FT_CLK_OUT and the base of DCM_CLKGEN of `hdmi_pll`), on the Go and the Mini 12.16 MHz (MIDI). The PLL output of clk_sys has no BUFG in `kg_pll`: the BUFG is in the top and on the Mini / Mini G the same output feeds the BUFGMUX of `hdmi_pll`.
- HDMI (`rtl/hdmi`, the code of tsconf): `hdmi_top.v` (the FT812 / menu mux, audio, the crossings into the pixel clock) around zhdmi (`hdmi_tx` of modules); `hdmi_pll.v` measures FT_CLK (`freq_counter.vhd`) and programs DCM_CLKGEN (clk_8 x M) for the FT modes, up to 64 MHz. The FT812 video goes through `kg_async_fifo` written on the falling FT_CLK; what goes from clk_sys to the pixel clock leaves registers on the falling clk_sys.
- The three ucf have the same timing block (the end of each file, then the parts of the board): PERIOD on CLK_50MHZ (derived onto the PLL outputs), the group TG_CLK_SYS on the BUFG `clk_sys` (on the PLL output net it leaks through the BUFGMUX into the pixel clock), TS_CLK_MCU 32 MHz, DATAPATHONLY bounds for the asynchronous crossings (MCU_SCK <-> clk_sys; on the Mini / Mini G FT_CLK <-> clk_sys / pixel clock, clk_sys -> pixel clock), the FT pixel clock with its own PERIOD (64 MHz), a TIG on the KEEP net `mux_sel` of the BUFGMUX. On the Go the VGA outputs are IOB registers bounded by TS_VGA_OUT (V_CLK is ~clk_sys). Each exception is explained there; do not TIG paths that are real.
- `make <board>-coremode` (Mini, Mini G) runs trce again without the FT-mode constraints (`src/pcf_core.py`): the clk_sys -> pixel clock paths with the setup and hold of clk_sys, the mode of the menu. Run it after every build of the Mini / Mini G.
- MCU_SCK of the Mini G goes through IBUFG + BUFG; the SCK of the FT812 SPI takes the IBUFG output.
- No clocks from logic and no latches: check "Clock Information" of the `.syr`.

## Simulation

- `src/sim/run_eq_sim.sh` (ghdl, iverilog): `tb_eq_overlay`, `kg_osd_overlay` with the parameters of the tops against the old `overlay.vhd` (`sim/ref`, a ghdl `--synth` netlist) over the frames of `vga_sync`: always on, overlay, popup, flash. The last two clocks of each cell and the popup edges are counted, not compared (the old overlay ran on the falling edge).
- `src/sim/run_mcu_sim.sh` (ISim): `tb_mcu_esp`, `kg_mcu` and `esp8266.vhd` as in the tops against the old `mcu.vhd` / `esp8266.vhd` (`sim/ref`): the control commands, the ESP uart both ways (bit time, a full reply queue), CMD_CAPS.
- `src/sim/run_hdmi_sim.sh` (ISim): `tb_hdmi_top`, the menu video at 40.9 MHz and the FT812 at 64 / 40 MHz through `hdmi_pll` and the fifo, every pixel where it enters `hdmi_tx`, the audio samples.
- Each prints `<tb>: 0 errors`. The tests of the shared modules are in `modules/sim` (`run_all.sh`).

## Layout

- `src/rtl/karabas_*_top.v`: the boards: PLL, `kg_mcu`, `kg_soft_switches`, `vga_sync`, `kg_osd_overlay`, `esp8266`, the DACs, HDMI.
- `src/rtl/hdmi`: `hdmi_top.v`, `hdmi_pll.v`, `freq_counter.vhd`. `src/rtl/uart`: `esp8266.vhd`, `uart_rx.vhd`, `uart_tx.vhd`. `src/rtl/adc`: `i2s_transceiver.vhd` (the ADC of the Mini / Mini G). `src/rtl/vga/vga_sync.v`: 800x600 at 60 Hz.
- `src/sim/ref`: the old code (new_hdmi_pll d31dec0) for the equivalence tests.
