`timescale 1ns / 1ps
`default_nettype none

/*-------------------------------------------------------------------------------------------------------------------
-- 
-- 
-- #       #######                                                 #                                               
-- #                                                               #                                               
-- #                                                               #                                               
-- ############### ############### ############### ############### ############### ############### ############### 
-- #             #               # #                             # #             #               # #               
-- #             # ############### #               ############### #             # ############### ############### 
-- #             # #             # #               #             # #             # #             #               # 
-- #             # ############### #               ############### ############### ############### ############### 
--                                                                                                                 
--         ####### ####### ####### #######                                         ############### ############### 
--                                                                                 #               #             # 
--                                                                                 #   ########### #             # 
--                                                                                 #             # #             # 
-- https://github.com/andykarpov/karabas-go                                        ############### ############### 
--
-- FPGA Boot menu core for Karabas-Go
--
-- @author Andy Karpov <andy.karpov@gmail.com>
-- EU, 2025
------------------------------------------------------------------------------------------------------------------*/

module karabas_go_top (
	//---------------------------
	input wire 				CLK_50MHZ,

	//---------------------------
	inout wire 				UART_RX,
	inout wire 				UART_TX,
	inout wire 				UART_CTS,
	inout wire 				ESP_RESET_N,
	inout wire 				ESP_BOOT_N,

	//---------------------------
	output wire [20:0] 	MA,
	inout wire [15:0] 	MD,
	output wire [1:0] 	MWR_N,
	output wire [1:0] 	MRD_N,

	//---------------------------
	output wire [1:0] 	SDR_BA,
	output wire [12:0] 	SDR_A,
	output wire 			SDR_CLK,
	output wire [1:0] 	SDR_DQM,
	output wire 			SDR_WE_N,
	output wire 			SDR_CAS_N,
	output wire 			SDR_RAS_N,
	inout wire [15:0] 	SDR_DQ,

	//---------------------------
	output wire 			SD_CS_N,
	output wire 			SD_CLK,
	output wire 			SD_DI,
	input wire 				SD_DO,
	input wire 				SD_DET_N,

	//---------------------------
	output wire [7:0] 	VGA_R,
	output wire [7:0] 	VGA_G,
	output wire [7:0] 	VGA_B,
	output wire 			VGA_HS,
	output wire 			VGA_VS,
	output wire 			V_CLK,

	//---------------------------
	output wire 			FT_SPI_CS_N,
	output wire 			FT_SPI_SCK,
	input wire 				FT_SPI_MISO,
	output wire 			FT_SPI_MOSI,
	input wire 				FT_INT_N,
	input wire 				FT_CLK,
	output wire 			FT_OE_N,

	//---------------------------
	output wire [2:0] 	WA,
	output wire [1:0] 	WCS_N,
	output wire 			WRD_N,
	output wire 			WWR_N,
	output wire 			WRESET_N,
	inout wire [15:0] 	WD,

	//---------------------------
	input wire 				FDC_INDEX,
	output wire [1:0] 	FDC_DRIVE,
	output wire 			FDC_MOTOR,
	output wire 			FDC_DIR,
	output wire 			FDC_STEP,
	output wire 			FDC_WDATA,
	output wire 			FDC_WGATE,
	input wire 				FDC_TR00,
	input wire 				FDC_WPRT,
	input wire 				FDC_RDATA,
	output wire 			FDC_SIDE_N,

	//---------------------------
	output wire 			TAPE_OUT,
	input wire 				TAPE_IN,
	output wire 			BEEPER,

	//---------------------------
	output wire 			DAC_LRCK,
	output wire 			DAC_DAT,
	output wire 			DAC_BCK,
	output wire 			DAC_MUTE,

	//---------------------------
	input wire 				MCU_CS_N,
	input wire 				MCU_SCK,
	inout wire 				MCU_MOSI,
	output wire 			MCU_MISO,
	input wire 				MCU_SPI_FT_CS_N,
	input wire 				MCU_SPI_SD2_CS_N,
	inout wire [1:0] 		MCU_SPI_IO,

	//---------------------------
	output wire 			MIDI_TX,
	output wire 			MIDI_CLK,
	output wire 			MIDI_RESET_N,

	//---------------------------
	output wire 			FLASH_CS_N,
	input wire  			FLASH_DO,
	output wire 			FLASH_DI,
	output wire 			FLASH_SCK,
	output wire 			FLASH_WP_N,
	output wire 			FLASH_HOLD_N
);

// unused signals
assign ESP_RESET_N 	= 1'bZ;
assign ESP_BOOT_N 	= 1'bZ;
assign FLASH_CS_N 	= 1'b1;
assign FLASH_DI 		= 1'b1;
assign FLASH_SCK 		= 1'b0;
assign FLASH_WP_N 	= 1'b1;
assign FLASH_HOLD_N 	= 1'b1;
assign TAPE_OUT 		= 1'b0;
assign BEEPER 			= 1'b0;
assign WA 				= 3'b0;
assign WCS_N 			= 2'b11;
assign WRD_N 			= 1'b1;
assign WWR_N 			= 1'b1;
assign WRESET_N 		= 1'b1;
assign SDR_BA 			= 2'b00;
assign SDR_A 			= 12'b0;
assign SDR_CLK 		= 1'b0;
assign SDR_DQM 		= 2'b00;
assign SDR_WE_N 		= 1'b1;
assign SDR_CAS_N 		= 1'b1;
assign SDR_RAS_N 		= 1'b1;
assign FDC_DRIVE 		= 2'b00;
assign FDC_MOTOR 		= 1'b0;
assign FDC_SIDE_N		= 1'b1;
assign FDC_WDATA		= 1'b1;
assign FDC_WGATE		= 1'b1;
assign MIDI_RESET_N	= 1'b1;
assign FDC_STEP		= 1'b1;
assign FDC_DIR			= 1'b1;
// system clocks
wire clk_sys, clk_12mhz, clk_cpu;
wire locked, areset;
wire signed [15:0] snd_out;   // the PCM sounds of menu_soc (clk_cpu)
wire snd_t;
wire [7:0] midi_data;
wire midi_wr;

// VCO 50 * 18 / 2 = 450 MHz, phase 0: clk_sys 40.91 MHz (/11: the 800 x 600 video, the DAC), 12.16 MHz
// (/37: MIDI_CLK), clk_cpu 75 MHz (/6: the menu CPU, the SRAM, the MCU link, the ESP and MIDI uarts)
wire [5:0] pll_clk;
kg_pll #(
	.DIVCLK_DIVIDE(2), .CLKFBOUT_MULT(18),
	.CLKOUT0_DIVIDE(11), .CLKOUT1_DIVIDE(37), .CLKOUT2_DIVIDE(6),
	.BUFG_MASK(6'b000110)
) pll (
	.clk_in				(CLK_50MHZ),
	.rst					(1'b0),
	.clk_out				(pll_clk),
	.locked				(locked)
);
assign clk_12mhz = pll_clk[1];
assign clk_cpu = pll_clk[2];
BUFG bufg_clk_sys(.I(pll_clk[0]), .O(clk_sys)); // in the top, as on the Mini / Mini G (the net of the ucf)
assign areset = ~locked;

// the reset of the clk_cpu side, synchronous to it
reg [1:0] areset_cpu = 2'b11;
always @(posedge clk_cpu) areset_cpu <= { areset_cpu[0], areset };
wire reset_cpu = areset_cpu[1];

// midi clk 12mhz out
ODDR2 u_midi_clk (.Q(MIDI_CLK), .C0(clk_12mhz), .C1(~clk_12mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft control signals / mcu only access; kg_mcu2 runs on clk_cpu: its FT video setting into clk_sys first
reg [1:0] ft_vga_s = 2'b00;
always @(posedge clk_sys) ft_vga_s <= { ft_vga_s[0], mcu_ft_vga_on };
wire vdac2_sel = ft_vga_s[1];
wire [7:0] osd_r, osd_g, osd_b;   // the bitmap OSD over the picture of the menu (below)
wire video_hs, video_vs, video_de;
assign FT_SPI_CS_N 	= mcu_ft_spi_on ? mcu_ft_cs_n : 1'b1;
assign FT_SPI_SCK 	= mcu_ft_spi_on ? MCU_SCK : 1'b1;
assign FT_SPI_MOSI 	= mcu_ft_spi_on ? mcu_ft_mosi : 1'b1;
assign FT_OE_N 		= ~vdac2_sel;

// vga / ft mux. The video DAC takes the pixels with the rising V_CLK: in the mode of the menu it is the inverted
// clk_sys, the middle of the pixel. The pixels and syncs leave flip-flops of the pads, a clock later both.
reg [7:0] vga_r_q = 8'd0, vga_g_q = 8'd0, vga_b_q = 8'd0;
reg vga_hs_q = 1'b0, vga_vs_q = 1'b0;
always @(posedge clk_sys) begin
	vga_r_q <= osd_r;
	vga_g_q <= osd_g;
	vga_b_q <= osd_b;
	vga_hs_q <= video_hs;
	vga_vs_q <= video_vs;
end
assign VGA_R[7:0] 	= (vdac2_sel ? 8'bZZZZZZZZ : vga_r_q);
assign VGA_G[7:0] 	= (vdac2_sel ? 8'bZZZZZZZZ : vga_g_q);
assign VGA_B[7:0] 	= (vdac2_sel ? 8'bZZZZZZZZ : vga_b_q);
assign VGA_HS 			= (vdac2_sel ? 1'bZ : vga_hs_q);
assign VGA_VS 			= (vdac2_sel ? 1'bZ : vga_vs_q);
assign V_CLK 			= (vdac2_sel ? FT_CLK : ~clk_sys);

//---------- DAC ------------
// the PCM sounds of the menu (menu_sound, clk_cpu: a value with each snd_t toggle); the Go has no ADC here
reg [2:0] snd_t_s = 3'd0;
reg signed [15:0] snd_s = 16'sd0;
always @(posedge clk_sys) begin
	snd_t_s <= { snd_t_s[1:0], snd_t };
	if (snd_t_s[2] != snd_t_s[1]) snd_s <= snd_out;   // stable: it changed with the toggle
end
kg_pcm5102 PCM5102(
	.clk				(clk_sys),
	.reset			(areset),
	.left				(snd_s),
	.right			(snd_s),
	.din				(DAC_DAT),
	.bck				(DAC_BCK),
	.lrck				(DAC_LRCK)
);
assign DAC_MUTE = 1'b1; // soft mute, 0 = mute, 1 = unmute

//---------- MCU ------------
// MCU link v2 (karabas-go-firmware docs/link_v2.md, kg_mcu2): INT on IO1, the bitmap OSD, PORT 1
// the ESP8266, PORT 2 MIDI IN (caps bit 9), the FT812 and the SD2 slot passed through, RPC (target 08)
wire mcu_ft_spi_on, mcu_ft_vga_on, mcu_ft_mosi, mcu_ft_cs_n;
wire [7:0] esp_uart_rx_data, esp_uart_tx_data;
wire esp_uart_rx_wr, esp_uart_rx_ack, esp_uart_tx_wr;
wire dvi_only;
wire mcu_int;
assign MCU_SPI_IO[0] = mcu_int;   // INT of link v2 on N9 (IO1 of the firmware)
assign MCU_SPI_IO[1] = 1'bz;

// HID and the loader of kg_mcu2 for menu_soc
wire [55:0] kbd;
wire kbd_wr, ms_wr;
wire [7:0] ms_x, ms_y, ms_z, ms_b;
wire [15:0] joy_l, joy_r;
wire [1:0] ld_kind;
wire ld_active, ld_end, ld_wr, ld_ready;
wire [31:0] ld_addr;
wire [7:0] ld_data;
// the RPC mailbox of kg_mcu2 (target 08) for menu_soc
wire [10:0] rpc_a;
wire rpc_we;
wire [7:0] rpc_wd, rpc_rd;
wire osd_wr, osd_en, osd_zoom;
wire [11:0] osd_waddr;
wire [7:0] osd_wdata;
wire [23:0] osd_fg, osd_bg;
wire [1:0] osd_alpha, osd_place;
wire [4:0] osd_px, osd_ph;
wire [3:0] osd_py;
wire [5:0] osd_pw;
wire [31:0] port_out_data;
wire [3:0] port_out_wr, port_in_full;

// the ESP: its byte is held until the ack; taken when port 1 has room, or by the menu CPU while it
// has the uart (esp_own: Setup > Wi-Fi, NTP sync; the MCU gets nothing meanwhile)
wire esp_own, cpu_esp_ack, cpu_esp_wr;
wire [7:0] cpu_esp_data;
wire esp_in_wr = esp_uart_rx_wr && !port_in_full[1] && !esp_own;
assign esp_uart_rx_ack = esp_own ? cpu_esp_ack : esp_in_wr;
assign esp_uart_tx_data = esp_own ? cpu_esp_data : port_out_data[15:8];
assign esp_uart_tx_wr = esp_own ? cpu_esp_wr : port_out_wr[1];
// MIDI: the keyboard of PORT 2 and the sounds of the menu CPU, switched at whole messages
wire [7:0] cpu_midi_data;
wire cpu_midi_wr, cpu_midi_full, kb_midi_ready;
menu_midi_mux midi_mux (
    .clk(clk_cpu),
    .kb_data(port_out_data[23:16]), .kb_wr(port_out_wr[2]), .kb_ready(kb_midi_ready),
    .cpu_data(cpu_midi_data), .cpu_wr(cpu_midi_wr), .cpu_full(cpu_midi_full),
    .tx_data(midi_data), .tx_wr(midi_wr)
);

kg_mcu2 #(
    .CAPS(16'h0210), .SW_N(2), .CS_FILTER(3), // CAPS bit 9 MIDI IN, bit 4 the core drives LD_READY (the menu program is loaded into the SRAM)
    .ENABLE_OSD(1), .ENABLE_PORTS(4'b0110), .PORT_IN_AW(6),
    .ENABLE_FT(1), .ENABLE_SD2(1),
    .ENABLE_RPC(1), .RPC_SERVICES(16'h000F)    // SYS, CFG, CORES, HID (karabas-go-firmware docs/link_v2.md 5.9)
) mcu(
    .CLK(clk_cpu), .N_RESET(~reset_cpu),
    .MCU_SCK(MCU_SCK), .MCU_MOSI(MCU_MOSI), .MCU_MISO(MCU_MISO), .MCU_SS(MCU_CS_N), .MCU_INT(mcu_int),
    .MCU_SPI_FT_SS(MCU_SPI_FT_CS_N), .MCU_SPI_SD2_SS(MCU_SPI_SD2_CS_N),

    .SW(), .SW_WR(), .SW_INDEX(), .SW_VALUE(), .HW_ID(), .DVI_ONLY(dvi_only), .CORE_BUILD(), .FW_BUILD(),
    .BUSY(), .BTNS(), .RELOAD_REQ(1'b0), .RELOAD_CORE(8'd0),
    .KBD(kbd), .KBD_WR(kbd_wr), .PS2_CODE(), .PS2_KIND(), .PS2_WR(), .MS_X(ms_x), .MS_Y(ms_y), .MS_Z(ms_z),
    .MS_B(ms_b), .MS_WR(ms_wr), .JOY_L(joy_l), .JOY_R(joy_r), .JOY_WR(), .PS2_CMD(16'h0000), .PS2_CMD_WR(1'b0),
    .LD_KIND(ld_kind), .LD_SLOT(), .LD_SIZE(), .LD_START(), .LD_ACTIVE(ld_active), .LD_END(ld_end),
    .LD_ADDR(ld_addr), .LD_DATA(ld_data), .LD_WR(ld_wr), .LD_READY(ld_ready),

    .OSD_WR(osd_wr), .OSD_WADDR(osd_waddr), .OSD_WDATA(osd_wdata), .OSD_EN(osd_en),
    .OSD_FG(osd_fg), .OSD_BG(osd_bg), .OSD_ALPHA(osd_alpha), .OSD_PX(osd_px), .OSD_PY(osd_py),
    .OSD_PW(osd_pw), .OSD_PH(osd_ph), .OSD_PLACE(osd_place), .OSD_ZOOM(osd_zoom),

    .SDC_IMG_MOUNTED(), .SDC_IMG_SIZE(), .SDC_RD(8'd0), .SDC_WR(8'd0), .SDC_SECTOR(32'd0),
    .SDC_BUSY(), .SDC_DONE(), .SDC_ERROR(), .SDC_SLOT(), .SDC_BYTE_IN_STROBE(), .SDC_BYTE_ADDR(),
    .SDC_BYTE_IN_DATA(), .SDC_BYTE_OUT_DATA(8'd0), .IMG_WP(), .IMG_PHYS_WR(), .IMG_PHYS(),
    .IMG_FLUSH(), .FDD_CHNG_WR(), .FDD_CHNG(), .FDD_CHNG_REQ(1'b0), .FDD_CHNG_DS(2'd0), .IMG_ACTIVE(),

    .PORT_OUT_DATA(port_out_data), .PORT_OUT_WR(port_out_wr), .PORT_OUT_READY({ 1'b1, kb_midi_ready, 2'b11 }),
    .PORT_IN_DATA({ 16'd0, esp_uart_rx_data, 8'd0 }), .PORT_IN_WR({ 2'b00, esp_in_wr, 1'b0 }),
    .PORT_IN_FULL(port_in_full), .PORT_CFG(128'd0), .PORT_STAT(), .PORT_STAT_WR(),

    .FT_SPI_ON(mcu_ft_spi_on), .FT_VGA_ON(mcu_ft_vga_on), .FT_RESET(), .FT_SCK(), .FT_MOSI(mcu_ft_mosi),
    .FT_MISO(FT_SPI_MISO), .FT_CS_N(mcu_ft_cs_n),
    .RPC_A(rpc_a), .RPC_WE(rpc_we), .RPC_WD(rpc_wd), .RPC_RD(rpc_rd),
    .SD2_SCK(SD_CLK), .SD2_MOSI(SD_DI), .SD2_MISO(SD_DO), .SD2_CS_N(SD_CS_N)
);

//--------- MIDI thru -------
// the bytes of a USB MIDI keyboard (PORT 2) and the notes of the menu (menu_midi_mux) to the SAM2695
kg_midi_tx #(.CLK_HZ(75000000)) midi_thru(
	.clk(clk_cpu), .data(midi_data), .wr(midi_wr), .overflow(), .tx(MIDI_TX)
);

//--------- ESP8266 UART -------
esp8266 #(.CLKS_PER_BIT(651)) esp8266(    // 75 MHz / 115200
	.CLK					(clk_cpu),
	.RESET				(reset_cpu),

	.ESP_UART_RX_DATA	(esp_uart_rx_data),
	.ESP_UART_RX_WR	(esp_uart_rx_wr),
	.ESP_UART_RX_ACK	(esp_uart_rx_ack),
	.ESP_UART_TX_DATA	(esp_uart_tx_data),
	.ESP_UART_TX_WR	(esp_uart_tx_wr),

	.UART_RX				(UART_RX),
	.UART_TX				(UART_TX),
	.UART_CTS			(UART_CTS)
);

//--------- the menu computer ---------
// menu_soc (docs/PLAN.md): VexRiscv, its program loaded from the ROMs of boot.kg into the SRAM, the frame buffers
// in the SRAM; it clears the SRAM after the PLL lock (every core starts with clean memory)
wire vid_req, vid_we, pal_we, fb_sel, fb_shown, frame_t, fb_show;
wire [20:0] vid_adr;
wire [8:0] vid_len, vid_wa;
wire [15:0] vid_wd;
wire [8:0] pal_wa;
wire [27:0] pal_wd;
wire [23:0] grad_top, grad_bot;
wire [15:0] sram_do, sram_oe_n;

menu_soc soc (
    .clk(clk_cpu), .reset(reset_cpu),
    .ld_kind(ld_kind), .ld_active(ld_active), .ld_end(ld_end), .ld_addr(ld_addr), .ld_data(ld_data),
    .ld_wr(ld_wr), .ld_ready(ld_ready),
    .kbd(kbd), .kbd_wr(kbd_wr), .ms_x(ms_x), .ms_y(ms_y), .ms_z(ms_z), .ms_b(ms_b), .ms_wr(ms_wr), .joy_l(joy_l), .joy_r(joy_r),
    .vid_req(vid_req), .vid_adr(vid_adr), .vid_len(vid_len), .vid_we(vid_we), .vid_wa(vid_wa), .vid_wd(vid_wd),
    .pal_we(pal_we), .pal_wa(pal_wa), .pal_wd(pal_wd), .fb_sel(fb_sel), .fb_shown(fb_shown),
    .frame_t(frame_t), .show(fb_show), .grad_top(grad_top), .grad_bot(grad_bot),
    .snd_out(snd_out), .snd_t(snd_t),
    .midi_data(cpu_midi_data), .midi_wr(cpu_midi_wr), .midi_full(cpu_midi_full),
    .esp_rx_data(esp_uart_rx_data), .esp_rx_valid(esp_uart_rx_wr && esp_own), .esp_rx_ack(cpu_esp_ack),
    .esp_tx_data(cpu_esp_data), .esp_tx_wr(cpu_esp_wr), .esp_own(esp_own),
    .rpc_a(rpc_a), .rpc_we(rpc_we), .rpc_wd(rpc_wd), .rpc_rd(rpc_rd),
    .dbg_uart(), .dbg_uart_wr(), .cpu_run(),
    .MA(MA), .MD_I(MD), .MD_O(sram_do), .MD_OE_N(sram_oe_n), .MWR_N(MWR_N), .MRD_N(MRD_N)
);
genvar gi;
generate for (gi = 0; gi < 16; gi = gi + 1) begin : g_md
    assign MD[gi] = sram_oe_n[gi] ? 1'bz : sram_do[gi];
end endgenerate

//--------- video ---------
// the frame buffer of the CPU (800 x 600, the timing of the old vga_sync), 4 clocks after its counters
wire [27:0] fb_rgba;
wire fb_on, vga_hs, vga_vs, vga_de;
menu_video video (
    .clk_cpu(clk_cpu),
    .vid_req(vid_req), .vid_adr(vid_adr), .vid_len(vid_len), .vid_we(vid_we), .vid_wa(vid_wa), .vid_wd(vid_wd),
    .pal_we(pal_we), .pal_wa(pal_wa), .pal_wd(pal_wd), .show(fb_show), .fb_sel(fb_sel), .fb_shown(fb_shown),
    .frame_t(frame_t),
    .clk_pix(clk_sys), .rgba(fb_rgba), .show_o(fb_on), .hs(vga_hs), .vs(vga_vs), .de(vga_de)
);

//--------- background ---------
// black while the CPU shows nothing (the load of its program, the wait for RPC): no colour before the one of
// karabas.ini (the blue glow of menu_bg was seen before the menu); else the FT812 gradient of menu_compose,
// the frame buffer blended over it
wire [23:0] bg_rgb;
wire bg_hs, bg_vs, bg_de;
menu_compose compose(
    .clk(clk_sys), .rgba_i(fb_rgba), .show_i(fb_on), .hs_i(vga_hs), .vs_i(vga_vs), .de_i(vga_de),
    .glow_i(24'd0), .grad_top(grad_top), .grad_bot(grad_bot),
    .rgb_o(bg_rgb), .hs_o(bg_hs), .vs_o(bg_vs), .de_o(bg_de)
);

//--------- OSD --------------
// the bitmap OSD over the picture of the menu, scale 2 (512 x 256 of 800 x 600); the VGA pins take the
// syncs as they come out of the overlay
kg_l2_osd_overlay #(.SCALE(2)) overlay(
    .wclk(clk_cpu), .osd_wr(osd_wr), .osd_waddr(osd_waddr), .osd_wdata(osd_wdata), .osd_en(osd_en),
    .osd_fg(osd_fg), .osd_bg(osd_bg), .osd_alpha(osd_alpha), .osd_px(osd_px), .osd_py(osd_py),
    .osd_pw(osd_pw), .osd_ph(osd_ph), .osd_place(osd_place), .osd_zoom(osd_zoom),
    .clk(clk_sys), .rgb_i(bg_rgb), .hs_i(bg_hs), .vs_i(bg_vs), .de_i(bg_de),
    .rgb_o({ osd_r, osd_g, osd_b }), .hs_o(video_hs), .vs_o(video_vs), .de_o(video_de)
);

endmodule
