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
	inout wire [3:0] 		MCU_IO,  // [1] INT of link v2, [2] sd2 cs_n, [3] ft cs_n

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
assign ESP_RESET_N        = 1'bZ;
assign ESP_BOOT_N         = 1'bZ;
assign FLASH_CS_N         = 1'b1;
assign FLASH_DI           = 1'b1;
assign FLASH_SCK          = 1'b0;
assign FLASH_WP_N         = 1'b1;
assign FLASH_HOLD_N       = 1'b1;
assign SDR_WE_N           = 1'b1;
assign SDR_CAS_N          = 1'b1;
assign SDR_RAS_N          = 1'b1;
assign WCS_N              = 1'b1;
assign WRD_N              = 1'b1;
assign WWR_N              = 1'b1;
assign WRESET_N           = 1'b1;
// MIDI_TX: the thru of a USB MIDI keyboard (PORT 2 of kg_mcu2) to the SAM2695, below
wire [7:0] midi_data;
wire midi_wr;
assign MIDI_RESET_N	= 1'b1;
assign TAPE_OUT		= 1'b0;
assign SDR_A			= 13'b0;
assign SDR_CLK			= 1'b1;
assign SDR_BA	 		= 2'b11;
assign SDR_DQM			= 2'b11;
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

// the SRAM is filled with zeros once after the PLL lock (0.2 s), so the next core starts with clean memory
wire [15:0] sram_data;
wire sram_data_oe;
sram_clear sram_clear (
    .clk                (clk_sys),
    .reset              (areset),
    .addr               (MA),
    .data               (sram_data),
    .data_oe            (sram_data_oe),
    .we_n               (MWR_N),
    .rd_n               (MRD_N),
    .done               ()
);
assign MD = sram_data_oe ? sram_data : 16'bz;

// midi clk 12mhz out
ODDR2 u_midi_clk (.Q(MIDI_CLK), .C0(clk_12mhz), .C1(~clk_12mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft clk 8mhz out
ODDR2 u_ft_clk (.Q(FT_CLK_OUT), .C0(clk_8mhz), .C1(~clk_8mhz), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ft control signals / mcu only access
wire vdac2_sel = mcu_ft_vga_on;
assign FT_SPI_CS_N = mcu_ft_spi_on ? mcu_ft_cs_n : 1'b1;
assign FT_SPI_SCK = mcu_ft_spi_on ? MCU_SCK : 1'b1;
assign SD_CLK = MCU_SCK;   // the SD2 pass-through (MCU_SCK is not on a clock pin before rev.G)
assign FT_RESET = ~mcu_ft_reset;
assign FT_SPI_MOSI = mcu_ft_spi_on ? mcu_ft_mosi : 1'b1;

// FT_CLK: the pixel clock of the FT812 (its video is taken on its falling edge, hdmi_pll measures it). It was a
// BUFGMUX with clk_sys, which hdmi_pll measured in the mode of the menu too.
wire ft_clk_ibufg, ft_clk_int;
IBUFG ibufg_ft_clk(.I(FT_CLK), .O(ft_clk_ibufg));
BUFG bufg_ft_clk(.I(ft_clk_ibufg), .O(ft_clk_int));

// the video of the menu: the bitmap OSD over the background (below)
wire [7:0] osd_r, osd_g, osd_b;
wire video_hs, video_vs, video_de;

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
// MCU link v2 (karabas-go-firmware docs/link_v2.md, kg_mcu2): INT on IO1, the bitmap OSD, PORT 1
// the ESP8266, PORT 2 MIDI IN (caps bit 9), the FT812 and the SD2 slot passed through
wire mcu_ft_spi_on, mcu_ft_vga_on, mcu_ft_mosi, mcu_ft_cs_n, mcu_ft_reset;
wire [7:0] esp_uart_rx_data, esp_uart_tx_data;
wire esp_uart_rx_wr, esp_uart_rx_ack, esp_uart_tx_wr;
wire dvi_only;
wire mcu_int;
assign MCU_IO[1] = mcu_int;
assign MCU_IO[0] = 1'bz;
assign MCU_IO[3:2] = 2'bzz;

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

// the ESP: its byte is held until the ack; taken when port 1 has room
wire esp_in_wr = esp_uart_rx_wr && !port_in_full[1];
assign esp_uart_rx_ack = esp_in_wr;
assign esp_uart_tx_data = port_out_data[15:8];
assign esp_uart_tx_wr = port_out_wr[1];
assign midi_data = port_out_data[23:16];
assign midi_wr = port_out_wr[2];

kg_mcu2 #(
    .CAPS(16'h0200), .SW_N(2), .CS_FILTER(3),
    .ENABLE_OSD(1), .ENABLE_PORTS(4'b0110), .PORT_IN_AW(6),
    .ENABLE_FT(1), .ENABLE_SD2(1)
) mcu(
    .CLK(clk_sys), .N_RESET(~areset),
    .MCU_SCK(MCU_SCK), .MCU_MOSI(MCU_MOSI), .MCU_MISO(MCU_MISO), .MCU_SS(MCU_CS_N), .MCU_INT(mcu_int),
    .MCU_SPI_FT_SS(MCU_IO[3]), .MCU_SPI_SD2_SS(MCU_IO[2]),

    .SW(), .SW_WR(), .SW_INDEX(), .SW_VALUE(), .HW_ID(), .DVI_ONLY(dvi_only), .CORE_BUILD(), .FW_BUILD(),
    .BUSY(), .BTNS(), .RELOAD_REQ(1'b0), .RELOAD_CORE(8'd0),
    .KBD(), .KBD_WR(), .PS2_CODE(), .PS2_KIND(), .PS2_WR(), .MS_X(), .MS_Y(), .MS_Z(), .MS_B(), .MS_WR(),
    .JOY_L(), .JOY_R(), .JOY_WR(), .PS2_CMD(16'h0000), .PS2_CMD_WR(1'b0),
    .LD_KIND(), .LD_SLOT(), .LD_SIZE(), .LD_START(), .LD_ACTIVE(), .LD_END(), .LD_ADDR(), .LD_DATA(),
    .LD_WR(), .LD_READY(1'b1),

    .OSD_WR(osd_wr), .OSD_WADDR(osd_waddr), .OSD_WDATA(osd_wdata), .OSD_EN(osd_en),
    .OSD_FG(osd_fg), .OSD_BG(osd_bg), .OSD_ALPHA(osd_alpha), .OSD_PX(osd_px), .OSD_PY(osd_py),
    .OSD_PW(osd_pw), .OSD_PH(osd_ph), .OSD_PLACE(osd_place), .OSD_ZOOM(osd_zoom),

    .SDC_IMG_MOUNTED(), .SDC_IMG_SIZE(), .SDC_RD(8'd0), .SDC_WR(8'd0), .SDC_SECTOR(32'd0),
    .SDC_BUSY(), .SDC_DONE(), .SDC_ERROR(), .SDC_SLOT(), .SDC_BYTE_IN_STROBE(), .SDC_BYTE_ADDR(),
    .SDC_BYTE_IN_DATA(), .SDC_BYTE_OUT_DATA(8'd0), .IMG_WP(), .IMG_PHYS_WR(), .IMG_PHYS(),
    .IMG_FLUSH(), .FDD_CHNG_WR(), .FDD_CHNG(), .FDD_CHNG_REQ(1'b0), .FDD_CHNG_DS(2'd0), .IMG_ACTIVE(),

    .PORT_OUT_DATA(port_out_data), .PORT_OUT_WR(port_out_wr), .PORT_OUT_READY(4'b1111),
    .PORT_IN_DATA({ 16'd0, esp_uart_rx_data, 8'd0 }), .PORT_IN_WR({ 2'b00, esp_in_wr, 1'b0 }),
    .PORT_IN_FULL(port_in_full), .PORT_CFG(128'd0), .PORT_STAT(), .PORT_STAT_WR(),

    .FT_SPI_ON(mcu_ft_spi_on), .FT_VGA_ON(mcu_ft_vga_on), .FT_RESET(mcu_ft_reset), .FT_SCK(), .FT_MOSI(mcu_ft_mosi),
    .FT_MISO(FT_SPI_MISO), .FT_CS_N(mcu_ft_cs_n),
    .SD2_SCK(), .SD2_MOSI(SD_DI), .SD2_MISO(SD_DO), .SD2_CS_N(SD_CS_N)
);

//--------- MIDI thru -------
// the bytes of a USB MIDI keyboard (PORT 2) to the SAM2695 at 31250 baud: the keyboard plays
// the synth of the board in the menu (karabas-go-firmware docs/usb_midi_plan.md)
kg_midi_tx #(.CLK_HZ(40910000)) midi_thru(
	.clk(clk_sys), .data(midi_data), .wr(midi_wr), .overflow(), .tx(MIDI_TX)
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
wire vga_hs, vga_vs, vga_de;
vga_sync vga_sync(
    .clk                (clk_sys),
    .hs                 (vga_hs),
    .vs                 (vga_vs),
    .de                 (vga_de)
);

//--------- background ---------
// the glow of the FT812 menu behind the OSD (menu_bg: a gradient that breathes and drifts in tint),
// one clock behind vga_sync with its syncs
wire [23:0] bg_rgb;
wire bg_hs, bg_vs, bg_de;
menu_bg background(
    .clk(clk_sys), .hs_i(vga_hs), .vs_i(vga_vs), .de_i(vga_de),
    .rgb_o(bg_rgb), .hs_o(bg_hs), .vs_o(bg_vs), .de_o(bg_de)
);

//--------- OSD --------------
// the screen of the menu is the bitmap OSD over the background, scale 2 (512 x 256 of 800 x 600);
// HDMI takes the syncs as they come out of the overlay (2 clocks later)
kg_l2_osd_overlay #(.SCALE(2)) overlay(
    .wclk(clk_sys), .osd_wr(osd_wr), .osd_waddr(osd_waddr), .osd_wdata(osd_wdata), .osd_en(osd_en),
    .osd_fg(osd_fg), .osd_bg(osd_bg), .osd_alpha(osd_alpha), .osd_px(osd_px), .osd_py(osd_py),
    .osd_pw(osd_pw), .osd_ph(osd_ph), .osd_place(osd_place), .osd_zoom(osd_zoom),
    .clk(clk_sys), .rgb_i(bg_rgb), .hs_i(bg_hs), .vs_i(bg_vs), .de_i(bg_de),
    .rgb_o({ osd_r, osd_g, osd_b }), .hs_o(video_hs), .vs_o(video_vs), .de_o(video_de)
);

endmodule
