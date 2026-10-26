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
-- FPGA Boot menu core for Karabas-Go Mini
--
-- @author Andy Karpov <andy.karpov@gmail.com>
-- EU, 2025
------------------------------------------------------------------------------------------------------------------*/

// HW_ID2: defined in karabas_mini.xst

module karabas_mini_top (
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
	input wire [7:0] 		VGA_R,
	input wire [7:0] 		VGA_G,
	input wire [7:0] 		VGA_B,
	input wire 				VGA_HS,
	input wire 				VGA_VS,

	output wire [3:0] 	TMDS_P,
	output wire [3:0] 	TMDS_N,

	//---------------------------
	output wire 			FT_SPI_CS_N,
	output wire 			FT_SPI_SCK,
	input wire 				FT_SPI_MISO,
	output wire 			FT_SPI_MOSI,
	input wire 				FT_INT_N,
	input wire 				FT_CLK,
	input wire 				FT_AUDIO,
	input wire 				FT_DE,
	input wire 				FT_DISP,
	output wire 			FT_RESET,
	output wire 			FT_CLK_OUT,

	//---------------------------
	output wire [2:0] 	WA,
	output wire [1:0] 	WCS_N,
	output wire 			WRD_N,
	output wire 			WWR_N,
	output wire 			WRESET_N,
	inout wire [15:0] 	WD,

	//---------------------------	
	output wire 			TAPE_OUT,
	input wire 				TAPE_IN,
	output wire 			AUDIO_L,
	output wire 			AUDIO_R,

	//---------------------------
	output wire 			ADC_CLK,
	inout wire 				ADC_BCK,
	inout wire 				ADC_LRCK,
	input wire 				ADC_DOUT,

	//---------------------------
	input wire 				MCU_CS_N,
	input wire 				MCU_SCK,
	input wire 				MCU_MOSI,
	output wire 			MCU_MISO,
	inout wire [3:0] 		MCU_IO,  // [0] burst cs_n, [1] burst ready, [2] sd2 cs_n, [3] ft cs_n

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

// unused signals yet
assign ESP_RESET_N 	= 1'bZ;
assign ESP_BOOT_N 	= 1'bZ;
assign FLASH_CS_N 	= 1'b1;
assign FLASH_DI 		= 1'b1;
assign FLASH_SCK 		= 1'b0;
assign FLASH_WP_N 	= 1'b1;
assign FLASH_HOLD_N 	= 1'b1;
assign MWR_N			= 1'b1;
assign MRD_N			= 1'b1;
assign SDR_WE_N		= 1'b1;
assign SDR_CAS_N		= 1'b1;
assign SDR_RAS_N		= 1'b1;
assign WCS_N			= 1'b1;
assign WRD_N			= 1'b1;
assign WWR_N			= 1'b1;
assign WRESET_N		= 1'b1;
assign MIDI_TX			= 1'b1;
assign MIDI_RESET_N	= 1'b1;
assign TAPE_OUT		= 1'b0;
assign SDR_A			= 13'b0;
assign SDR_CLK			= 1'b1;
assign SDR_BA	 		= 2'b11;
assign SDR_DQM			= 2'b11;
assign MA				= 21'b0;
assign WA				= 3'b000;

// system clocks
wire clk_sys, clk_sys_pll, clk_8mhz, clk_12mhz;
wire locked, areset;

// VCO 50 * 18 / 2 = 450 MHz, phase 0: clk_sys 40.91 MHz (/11: the menu, its 800 x 600 video, the MCU link),
// clk_8 8.18 MHz (/55: FT_CLK_OUT, the base of DCM_CLKGEN in hdmi_pll), 12.16 MHz (/37: MIDI_CLK). The PLL output
// of clk_sys has no BUFG in kg_pll: it drives the BUFG of clk_sys and the BUFGMUX of hdmi_pll.
wire [5:0] pll_clk;
kg_pll #(
    .DIVCLK_DIVIDE(2), .CLKFBOUT_MULT(18),
    .CLKOUT0_DIVIDE(11), .CLKOUT1_DIVIDE(55), .CLKOUT2_DIVIDE(37),
    .BUFG_MASK(6'b000110)
) pll (
    .clk_in              (CLK_50MHZ),
    .rst                 (1'b0),
    .clk_out             (pll_clk),
    .locked              (locked)
);
assign clk_sys_pll = pll_clk[0];
assign clk_8mhz = pll_clk[1];
assign clk_12mhz = pll_clk[2];
BUFG bufg_clk_sys(.I(clk_sys_pll), .O(clk_sys));
assign areset = ~locked;

// midi clk 12mhz out
ODDR2 u_midi_clk (.Q(MIDI_CLK), .C0(clk_12mhz), .C1(~clk_12mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft clk 8mhz out
ODDR2 u_ft_clk (.Q(FT_CLK_OUT), .C0(clk_8mhz), .C1(~clk_8mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft control signals / mcu only access
wire vdac2_sel = mcu_ft_vga_on;
assign FT_SPI_CS_N = mcu_ft_spi_on ? mcu_ft_cs_n : 1'b1;
assign FT_SPI_SCK = mcu_ft_spi_on ? MCU_SCK : 1'b1;
assign FT_RESET = ~mcu_ft_reset;
assign FT_SPI_MOSI = mcu_ft_spi_on ? mcu_ft_mosi : 1'b1;

// FT_CLK: the pixel clock of the FT812 (its video is taken on its falling edge, hdmi_pll measures it). It was a
// BUFGMUX with clk_sys, which hdmi_pll measured in the mode of the menu too.
wire ft_clk_ibufg, ft_clk_int;
IBUFG ibufg_ft_clk(.I(FT_CLK), .O(ft_clk_ibufg));
BUFG bufg_ft_clk(.I(ft_clk_ibufg), .O(ft_clk_int));

// hdmi
hdmi_top #(.SAMPLERATE(48000)) hdmi_top(
    .clk                (ft_clk_int),
    .clk_ref            (clk_sys),
    .clk_ref_pll        (clk_sys_pll),
    .clk_8              (clk_8mhz),
    .reset              (areset),

    .vga_rgb            ({osd_r[7:0], osd_g[7:0], osd_b[7:0]}),
    .vga_hs             (video_hs),
    .vga_vs             (video_vs),
    .vga_de             (video_de),

    .ft_rgb             ({VGA_R[7:0], VGA_G[7:0], VGA_B[7:0]}),
    .ft_hs              (VGA_HS),
    .ft_vs              (VGA_VS),
    .ft_de              (FT_DE),

    .ft_sel             (vdac2_sel),

    .audio_en           (~dvi_only),
    .audio_l            (audio_mix_l),
    .audio_r            (audio_mix_r),

    .tmds_p             (TMDS_P),
    .tmds_n             (TMDS_N),

    .freq               ()
);

// ------- PCM1808 ADC ---------
wire signed [23:0] adc_l, adc_r;
i2s_transceiver adc(
    .reset_n            (~areset),
    .mclk               (clk_sys),
    .sclk               (ADC_BCK),
    .ws                 (ADC_LRCK),
    .sd_tx              (),
    .sd_rx              (ADC_DOUT),
    .l_data_tx          (24'b0),
    .r_data_tx          (24'b0),
    .l_data_rx          (adc_l),
    .r_data_rx          (adc_r)
);

// ------- ADC_CLK output buf
ODDR2 oddr_adc2(.Q(ADC_CLK), .C0(clk_sys), .C1(~clk_sys), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ------- audio mix
wire [15:0] audio_mix_l = adc_l[23:8];
wire [15:0] audio_mix_r = adc_r[23:8];

//------- Sigma-Delta DAC ---------
kg_sigma_delta_dac #(.MSBI(15)) dac_l(
    .clk                (clk_sys),
    .reset              (areset),
    .din                ({2'b00, !audio_mix_l[15], audio_mix_l[14:4], 2'b00}),
    .dout               (AUDIO_L)
);

kg_sigma_delta_dac #(.MSBI(15)) dac_r(
    .clk                (clk_sys),
    .reset              (areset),
    .din                ({2'b00, !audio_mix_r[15], audio_mix_r[14:4], 2'b00}),
    .dout               (AUDIO_R)
);

//---------- MCU ------------
wire [15:0] osd_command;
wire mcu_ft_spi_on, mcu_ft_vga_on, mcu_ft_mosi, mcu_ft_cs_n, mcu_ft_reset;
wire [7:0] esp_uart_rx_data, esp_uart_tx_data;
wire esp_uart_rx_wr, esp_uart_rx_ack, esp_uart_tx_wr;
wire dvi_only;
wire mcu_burst_ready;
assign MCU_IO[1] = mcu_burst_ready;
assign MCU_IO[0] = 1'bz;
assign MCU_IO[3:2] = 2'bzz;

kg_mcu #(
    .BURST_MAX_MHZ(0), .CS_FILTER(3),
    .ENABLE_RTC(0), .ENABLE_UART(0), .ENABLE_MATRIX(0), .ENABLE_DEBUG(0), .ENABLE_IMG(0), .ENABLE_FILE(0),
    .ENABLE_FT(1), .ENABLE_SD2(1), .ENABLE_ESP(1)
) mcu(
    .CLK                (clk_sys),
    .N_RESET            (~areset),

    .MCU_MOSI           (MCU_MOSI),
    .MCU_MISO           (MCU_MISO),
    .MCU_SCK            (MCU_SCK),
    .MCU_SS             (MCU_CS_N),
    .MCU_SPI_FT_SS      (MCU_IO[3]),
    .MCU_SPI_SD2_SS     (MCU_IO[2]),
    .MCU_BURST_CS_N     (MCU_IO[0]),
    .MCU_BURST_READY    (mcu_burst_ready),

    .RTC_A              (8'h00),
    .RTC_DI             (8'h00),
    .RTC_CS             (1'b0),
    .RTC_WR_N           (1'b1),
    .UART_TX_DATA       (8'h00),
    .UART_TX_WR         (1'b0),
    .UART_TX_MODE       (1'b0),
    .UART_DLM           (8'h00),
    .UART_DLL           (8'h00),
    .UART_DLM_WR        (1'b0),
    .UART_DLL_WR        (1'b0),
    .MATRIX_CMD         (24'h000000),
    .MATRIX_CMD_WR      (1'b0),
    .DEBUG_ADDR         (16'h0000),
    .DEBUG_DATA         (16'h0000),
    .IMG_Q_WR           (1'b0),
    .IMG_Q_DI           (24'h000000),
    .SD2_SCK            (SD_CLK),
    .SD2_MOSI           (SD_DI),
    .SD2_MISO           (SD_DO),
    .SD2_CS_N           (SD_CS_N),

    .ESP_UART_TX_DATA   (esp_uart_tx_data),
    .ESP_UART_TX_WR     (esp_uart_tx_wr),
    .ESP_UART_RX_DATA   (esp_uart_rx_data),
    .ESP_UART_RX_WR     (esp_uart_rx_wr),
    .ESP_UART_RX_ACK    (esp_uart_rx_ack),

    .OSD_COMMAND        (osd_command),

    .FT_SPI_ON          (mcu_ft_spi_on),
    .FT_VGA_ON          (mcu_ft_vga_on),
    .FT_MISO            (FT_SPI_MISO),
    .FT_MOSI            (mcu_ft_mosi),
    .FT_CS_N            (mcu_ft_cs_n),
    .FT_RESET           (mcu_ft_reset),

    .DVI_ONLY           (dvi_only)
);

//--------- ESP8266 UART -------
esp8266 esp8266(
    .CLK                (clk_sys),
    .RESET              (areset),

    .ESP_UART_RX_DATA   (esp_uart_rx_data),
    .ESP_UART_RX_WR     (esp_uart_rx_wr),
    .ESP_UART_RX_ACK    (esp_uart_rx_ack),
    .ESP_UART_TX_DATA   (esp_uart_tx_data),
    .ESP_UART_TX_WR     (esp_uart_tx_wr),

    .UART_RX            (UART_RX),
    .UART_TX            (UART_TX),
    .UART_CTS           (UART_CTS)
);

//--------- VGA sync ---------
wire video_hs, video_vs, video_de;
vga_sync vga_sync(
    .clk                (clk_sys),
    .hs                 (video_hs),
    .vs                 (video_vs),
    .de                 (video_de)
);

//--------- OSD --------------
// the screen of the menu is the OSD (ALWAYS_ON); its place as overlay.vhd had it at 172 / 60 (sim/tb_eq_overlay.v)
wire [7:0] osd_r, osd_g, osd_b;
kg_osd_overlay #(.H_OFFSET(171), .V_OFFSET(60), .H_DIV4_WIDTH(1200), .ALWAYS_ON(1)) overlay(
    .clk                (clk_sys),
    .rgb_i              (24'b0),
    .rgb_o              ({osd_r[7:0], osd_g[7:0], osd_b[7:0]}),
    .hsync_i            (video_hs),
    .vsync_i            (video_vs),
    .osd_command        (osd_command)
);

endmodule
