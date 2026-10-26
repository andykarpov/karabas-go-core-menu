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
assign MA 				= 21'b0;
assign MWR_N			= 2'b11;
assign MRD_N 			= 2'b11;
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
assign MIDI_TX			= 1'b1;
	
// system clocks
wire clk_sys, clk_12mhz;
wire locked, areset;

// VCO 50 * 18 / 2 = 450 MHz, phase 0: clk_sys 40.91 MHz (/11: the menu, its 800 x 600 video, the MCU link),
// 12.16 MHz (/37: MIDI_CLK)
wire [5:0] pll_clk;
kg_pll #(
	.DIVCLK_DIVIDE(2), .CLKFBOUT_MULT(18),
	.CLKOUT0_DIVIDE(11), .CLKOUT1_DIVIDE(37),
	.BUFG_MASK(6'b000010)
) pll (
	.clk_in				(CLK_50MHZ),
	.rst					(1'b0),
	.clk_out				(pll_clk),
	.locked				(locked)
);
assign clk_12mhz = pll_clk[1];
BUFG bufg_clk_sys(.I(pll_clk[0]), .O(clk_sys)); // in the top, as on the Mini / Mini G (the net of the ucf)
assign areset = ~locked;

// midi clk 12mhz out
ODDR2 u_midi_clk (.Q(MIDI_CLK), .C0(clk_12mhz), .C1(~clk_12mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft control signals / mcu only access
wire vdac2_sel = mcu_ft_vga_on;
assign FT_SPI_CS_N 	= mcu_ft_spi_on ? mcu_ft_cs_n : 1'b1;
assign FT_SPI_SCK 	= mcu_ft_spi_on ? MCU_SCK : 1'b1;
assign FT_SPI_MOSI 	= mcu_ft_spi_on ? mcu_ft_mosi : 1'b1;
assign FT_OE_N 		= ~vdac2_sel;

// vga / ft mux. The video DAC takes the pixels with the rising V_CLK: in the mode of the menu it is the inverted
// clk_sys, the middle of the pixel (the OSD changes its pixels with the rising clk_sys; overlay.vhd had its
// counters and pixels on the falling clock and V_CLK = clk_sys). The pixels and syncs leave flip-flops of the
// pads, a clock later both (the OSD output goes through the attribute logic: 16.7 ns to the pads).
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
// the menu has no sound on the Go
kg_pcm5102 PCM5102(
	.clk				(clk_sys),
	.reset			(areset),
	.left				(16'h0000),
	.right			(16'h0000),
	.din				(DAC_DAT),
	.bck				(DAC_BCK),
	.lrck				(DAC_LRCK)
);
assign DAC_MUTE = 1'b1; // soft mute, 0 = mute, 1 = unmute

//---------- MCU ------------
wire [15:0] osd_command;
wire mcu_ft_spi_on, mcu_ft_vga_on, mcu_ft_mosi, mcu_ft_cs_n;
wire [7:0] esp_uart_rx_data, esp_uart_tx_data;
wire esp_uart_rx_wr, esp_uart_rx_ack, esp_uart_tx_wr;
wire mcu_burst_ready;
assign MCU_SPI_IO[0] = mcu_burst_ready;
assign MCU_SPI_IO[1] = 1'bz;

kg_mcu #(
	.BURST_MAX_MHZ(0), .CS_FILTER(3),
	.ENABLE_RTC(0), .ENABLE_UART(0), .ENABLE_MATRIX(0), .ENABLE_DEBUG(0), .ENABLE_IMG(0), .ENABLE_FILE(0),
	.ENABLE_FT(1), .ENABLE_SD2(1), .ENABLE_ESP(1)
) mcu(
	.CLK				(clk_sys),
	.N_RESET			(~areset),

	.MCU_MOSI		(MCU_MOSI),
	.MCU_MISO		(MCU_MISO),
	.MCU_SCK			(MCU_SCK),
	.MCU_SS			(MCU_CS_N),
	.MCU_SPI_FT_SS	(MCU_SPI_FT_CS_N),
	.MCU_SPI_SD2_SS(MCU_SPI_SD2_CS_N),
	// burst transfers: firmware IO0 (GPIO14) = MCU_SPI_IO[1] (M11), IO1 (GPIO2) = MCU_SPI_IO[0] (N9)
	.MCU_BURST_CS_N(MCU_SPI_IO[1]),
	.MCU_BURST_READY(mcu_burst_ready),

	.RTC_A			(8'h00),
	.RTC_DI			(8'h00),
	.RTC_CS			(1'b0),
	.RTC_WR_N		(1'b1),
	.UART_TX_DATA	(8'h00),
	.UART_TX_WR		(1'b0),
	.UART_TX_MODE	(1'b0),
	.UART_DLM		(8'h00),
	.UART_DLL		(8'h00),
	.UART_DLM_WR	(1'b0),
	.UART_DLL_WR	(1'b0),
	.MATRIX_CMD		(24'h000000),
	.MATRIX_CMD_WR	(1'b0),
	.DEBUG_ADDR		(16'h0000),
	.DEBUG_DATA		(16'h0000),
	.IMG_Q_WR		(1'b0),
	.IMG_Q_DI		(24'h000000),
	.SD2_SCK		(SD_CLK),
	.SD2_MOSI		(SD_DI),
	.SD2_MISO		(SD_DO),
	.SD2_CS_N		(SD_CS_N),

	.ESP_UART_TX_DATA	(esp_uart_tx_data),
	.ESP_UART_TX_WR	(esp_uart_tx_wr),
	.ESP_UART_RX_DATA	(esp_uart_rx_data),
	.ESP_UART_RX_WR	(esp_uart_rx_wr),
	.ESP_UART_RX_ACK	(esp_uart_rx_ack),

	.OSD_COMMAND	(osd_command),

	.FT_SPI_ON		(mcu_ft_spi_on),
	.FT_VGA_ON		(mcu_ft_vga_on),
	.FT_MISO			(FT_SPI_MISO),
	.FT_MOSI			(mcu_ft_mosi),
	.FT_CS_N			(mcu_ft_cs_n)
);

//--------- ESP8266 UART -------
esp8266 esp8266(
	.CLK					(clk_sys),
	.RESET				(areset),

	.ESP_UART_RX_DATA	(esp_uart_rx_data),
	.ESP_UART_RX_WR	(esp_uart_rx_wr),
	.ESP_UART_RX_ACK	(esp_uart_rx_ack),
	.ESP_UART_TX_DATA	(esp_uart_tx_data),
	.ESP_UART_TX_WR	(esp_uart_tx_wr),

	.UART_RX				(UART_RX),
	.UART_TX				(UART_TX),
	.UART_CTS			(UART_CTS)
);

//--------- VGA sync ---------
wire video_hs, video_vs, video_de;
vga_sync vga_sync(
	.clk				(clk_sys),
	.hs				(video_hs),
	.vs				(video_vs),
	.de				(video_de)
);

//--------- OSD --------------
// the screen of the menu is the OSD (ALWAYS_ON); its place as overlay.vhd had it at 172 / 60 (sim/tb_eq_overlay.v)
wire [7:0] osd_r, osd_g, osd_b;
kg_osd_overlay #(.H_OFFSET(171), .V_OFFSET(60), .H_DIV4_WIDTH(1200), .ALWAYS_ON(1)) overlay(
	.clk				(clk_sys),
	.rgb_i			(24'b0),
	.rgb_o			({osd_r[7:0], osd_g[7:0], osd_b[7:0]}),
	.hsync_i			(video_hs),
	.vsync_i			(video_vs),
	.osd_command	(osd_command)
);

endmodule
