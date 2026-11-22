`timescale 1ns / 1ps
`default_nettype none

/*
 HDMI output of the Mini / Mini G: the video of the menu in its pixel clock (clk_pix, the BUFGMUX of the
 top: 25 MHz in 640 x 480, 40 MHz in 800 x 600), with the audio of the core. The transmitter is zhdmi
 (hdmi_tx of karabas-go-modules, DVI when audio_en = 0); hdmi_pll makes the 5x serial clocks and is reset
 when the mode changes (mode, the select of the BUFGMUX). The FT812 video it had is gone (hdmi_pll.v).

 Audio: a sample is taken every CLKRATE / SAMPLERATE clocks of clk_ref (clk_sys, 40 MHz in both modes)
 and passed to the pixel clock with a toggle: the data stays the same for a whole sample period, far
 longer than the synchronizer takes, so it is stable when the pixel clock takes it. hdmi_tx measures the
 CTS of the audio clock regeneration from the pixel clock, so this works with either pixel clock.
*/

module hdmi_top #(
	parameter SAMPLERATE = 44100,
	parameter CLKRATE = 40000000 // clk_ref
) (
	input wire clk_pix,   // the pixel clock of the video
	input wire clk_ref,   // clk_sys, 40 MHz
	input wire mode,      // clk_ref: the select of the pixel clock BUFGMUX

	input wire reset,

	input wire [23:0] vga_rgb,   // clk_pix
	input wire vga_hs,
	input wire vga_vs,
	input wire vga_de,

	input wire audio_en,         // clk_ref
	input wire [15:0] audio_l,   // clk_ref
	input wire [15:0] audio_r,

	output wire [3:0] tmds_p,
	output wire [3:0] tmds_n
);

// clocks
wire clk_hdmi, clk_hdmi_n;
wire p_clk_int = clk_pix;
wire lockedx5;
wire pll_reset;
hdmi_pll hdmi_pll (
	.clk_pix(clk_pix),
	.clk_ref(clk_ref),
	.mode(mode),
	.reset(reset),
	.clk_hdmi(clk_hdmi),
	.clk_hdmi_n(clk_hdmi_n),
	.locked(lockedx5),
	.o_reset(pll_reset)
);

// From clk_ref into the pixel clock only through synchronisers (audio_en, the reset of hdmi_tx: set at once,
// released two pixel clocks later; the audio sample toggle below).
reg audio_en_n = 1'b0, pll_reset_n = 1'b1;
always @(negedge clk_ref) begin
	audio_en_n <= audio_en;
	pll_reset_n <= pll_reset;
end
reg [1:0] audio_en_s = 2'b00;
reg [1:0] tx_reset_s = 2'b11;
wire tx_reset_a = pll_reset_n || ~lockedx5;
always @(posedge p_clk_int)
	audio_en_s <= {audio_en_s[0], audio_en_n};
always @(posedge p_clk_int or posedge tx_reset_a)
	if (tx_reset_a)
		tx_reset_s <= 2'b11;
	else
		tx_reset_s <= {tx_reset_s[0], 1'b0};

// the video, one register in the pixel clock
reg [26:0] vga_data = 27'd0;
always @(posedge p_clk_int)
	vga_data <= {vga_de, vga_hs, vga_vs, vga_rgb[23:0]};

wire [7:0] host_vga_r = ~vga_data[26] ? 8'b0 : vga_data[23:16];
wire [7:0] host_vga_g = ~vga_data[26] ? 8'b0 : vga_data[15:8];
wire [7:0] host_vga_b = ~vga_data[26] ? 8'b0 : vga_data[7:0];
wire host_vga_hs = vga_data[25];
wire host_vga_vs = vga_data[24];
wire host_vga_blank = ~vga_data[26];

// audio samples in clk_ref (on its falling edge, see above), passed to the pixel clock
localparam integer SAMPLE_DIV = CLKRATE / SAMPLERATE;
reg [15:0] sample_cnt = 16'd0;
reg [31:0] sample_ref = 32'd0;
reg sample_tgl = 1'b0;
always @(negedge clk_ref) begin
	if (sample_cnt == SAMPLE_DIV - 1) begin
		sample_cnt <= 16'd0;
		sample_ref <= {audio_l, audio_r};
		sample_tgl <= ~sample_tgl;
	end
	else
		sample_cnt <= sample_cnt + 16'd1;
end

reg [2:0] sample_sync = 3'b000;
reg audio_clk = 1'b0;
reg signed [23:0] hdmi_audio_l = 24'd0, hdmi_audio_r = 24'd0;
always @(posedge p_clk_int) begin
	sample_sync <= {sample_sync[1:0], sample_tgl};
	audio_clk <= sample_sync[2] ^ sample_sync[1];
	if (sample_sync[2] ^ sample_sync[1]) begin
		hdmi_audio_l <= {sample_ref[31:16], 8'd0};
		hdmi_audio_r <= {sample_ref[15:0], 8'd0};
	end
end

// hdmi tx
hdmi_tx #(.SAMPLE_FREQ(SAMPLERATE), .INPUT_REG(1), .OUTPUT_REG(1)) hdmi_tx(   // INPUT_REG: the video -> the TMDS encoders (Mini G coremode -1.5 ns); OUTPUT_REG: the encoder -> serializer path (-0.73 ns)
	.clk(p_clk_int),
	.sclk(clk_hdmi),
	.sclk_n(clk_hdmi_n),
	.reset(tx_reset_s[1]),
	.rgb({host_vga_r, host_vga_g, host_vga_b}),
	.vsync(host_vga_vs),
	.hsync(host_vga_hs),
	.de(~host_vga_blank),

	.audio_en(audio_en_s[1]),
	.audio_l(hdmi_audio_l),
	.audio_r(hdmi_audio_r),
	.audio_clk(audio_clk),

	.tx_clk_n(tmds_n[3]),
	.tx_clk_p(tmds_p[3]),
	.tx_d_n(tmds_n[2:0]),
	.tx_d_p(tmds_p[2:0]) // 0:blue, 1:green, 2:red
);

endmodule
