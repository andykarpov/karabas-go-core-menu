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
	inout wire 				SD_DI,
	inout wire 				SD_DO,
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
assign UART_CTS 		= 1'b0;
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
assign SD_CS_N			= 1'b1;
assign FDC_SIDE_N		= 1'b1;
assign SD_CLK			= 1'b1;
assign FDC_WDATA		= 1'b1;
assign FDC_WGATE		= 1'b1;
assign MIDI_RESET_N	= 1'b1;
assign FDC_STEP		= 1'b1;
assign FDC_DIR			= 1'b1;
assign MIDI_TX			= 1'b1;
	
// system clocks
wire clk_sys, clk_8mhz, clk_16mhz, clk_12mhz, v_clk_int;
wire locked, areset;

pll pll (
	.CLK_IN1				(CLK_50MHZ),
	.CLK_OUT1			(clk_sys), // 40
	.CLK_OUT2			(clk_8mhz),
	.CLK_OUT3			(clk_16mhz),
	.CLK_OUT4			(clk_12mhz),
	.LOCKED				(locked)
);
assign areset = ~locked;

// midi clk 12mhz out
ODDR2 u_midi_clk (.Q(MIDI_CLK), .C0(clk_12mhz), .C1(~clk_12mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft control signals / mcu only access
wire vdac2_sel = mcu_ft_vga_on;
assign FT_SPI_CS_N 	= mcu_ft_spi_on ? mcu_ft_cs_n : 1'b1;
assign FT_SPI_SCK 	= mcu_ft_spi_on ? mcu_ft_sck : 1'b1;
assign FT_SPI_MOSI 	= mcu_ft_spi_on ? mcu_ft_mosi : 1'b1;
assign FT_OE_N 		= ~vdac2_sel;

// vga / ft mux
assign VGA_R[7:0] 	= (vdac2_sel ? 8'bZZZZZZZZ : osd_r[7:0]);
assign VGA_G[7:0] 	= (vdac2_sel ? 8'bZZZZZZZZ : osd_g[7:0]);
assign VGA_B[7:0] 	= (vdac2_sel ? 8'bZZZZZZZZ : osd_b[7:0]);
assign VGA_HS 			= (vdac2_sel ? 1'bZ : video_hs);
assign VGA_VS 			= (vdac2_sel ? 1'bZ : video_vs);
assign V_CLK 			= (vdac2_sel ? FT_CLK : clk_sys);

//---------- DAC ------------
wire [15:0] audio_out_l, audio_out_r;
PCM5102 PCM5102(
	.clk				(clk_sys),
	.reset			(areset),
	.left				(audio_out_l),
	.right			(audio_out_r),
	.din				(DAC_DAT),
	.bck				(DAC_BCK),
	.lrck				(DAC_LRCK)
);
assign DAC_MUTE = 1'b1; // soft mute, 0 = mute, 1 = unmute

//---------- MCU ------------
wire [15:0] osd_command;
wire mcu_ft_spi_on, mcu_ft_vga_on, mcu_ft_sck, mcu_ft_mosi, mcu_ft_cs_n, mcu_ft_reset, mcu_busy;
mcu mcu(
	.CLK				(clk_sys),
	.N_RESET			(~areset),

	.MCU_MOSI		(MCU_MOSI),
	.MCU_MISO		(MCU_MISO),
	.MCU_SCK			(MCU_SCK),
	.MCU_SS			(MCU_CS_N),

	.MCU_SPI_FT_SS	(MCU_SPI_FT_CS_N),
	.MCU_SPI_SD2_SS(MCU_SPI_SD2_CS_N),

	.RTC_A			(8'b0),
	.RTC_DI			(8'b0),
	.RTC_CS			(1'b1),
	.RTC_WR_N		(1'b1),

	.UART_TX_DATA	(8'b0),
	.UART_TX_WR		(1'b0),
	.UART_TX_MODE	(1'b0),
	.UART_DLL		(8'b0),
	.UART_DLM		(8'b0),
	.UART_DLL_WR	(1'b0),
	.UART_DLM_WR	(1'b0),

	.OSD_COMMAND	(osd_command),

	.FT_SPI_ON		(mcu_ft_spi_on),
	.FT_VGA_ON		(mcu_ft_vga_on),
	.FT_SCK			(mcu_ft_sck),
	.FT_MISO			(FT_SPI_MISO),
	.FT_MOSI			(mcu_ft_mosi),
	.FT_CS_N			(mcu_ft_cs_n),
	.FT_RESET		(mcu_ft_reset),

	.DEBUG_ADDR		(16'd0),
	.DEBUG_DATA		(16'd0),

	.BUSY				(mcu_busy)
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
wire [7:0] osd_r, osd_g, osd_b;
overlay #(.DEFAULT(1)) overlay(
	.CLK				(clk_sys),
	.RGB_I			(24'b0),
	.RGB_O			({osd_r[7:0], osd_g[7:0], osd_b[7:0]}),
	.HSYNC_I			(video_hs),
	.VSYNC_I			(video_vs),
	.OSD_COMMAND	(osd_command)
);

endmodule
