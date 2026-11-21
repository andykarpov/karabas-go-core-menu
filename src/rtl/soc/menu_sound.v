`timescale 1ns / 1ps
`default_nettype none
/*
 menu_sound.v

 The PCM player of the menu (docs/PLAN.md Ф2): two channels of raw signed 16-bit little-endian mono
 samples in the SRAM (the sounds of boot.kg), mixed at 48 kHz. One clock (clk, the CPU clock).

 A channel: its byte address in the SRAM, its length in samples, its step (the sample rate / 48000 in
 0.16 fixed point: 22050 Hz = 30106, 44100 Hz = 60211), its volume (0..255) and play. Each 48 kHz
 tick it steps on (nearest sample, no interpolation) and fetches the sample it moved to over the
 sound port of menu_sram; past its length it stops. out = the sum of sample x volume / 256 of the
 channels, saturated to 16 bits; out_t toggles with each new value (taken into the audio clock by
 the top).

 Registers (the MMIO of menu_soc at 0x80 + 0x10 x channel), word writes:
   +0 W  the byte address        +4 W  the length (samples)       +8 W  the step (0.16)
   +c W  bits 7..0 the volume, bit 8 play (1: from the start, 0: stop); R  bit 8 playing
*/
module menu_sound #(
	parameter CLK_HZ = 75000000
) (
	input wire clk,
	input wire reset,

	input wire wr,
	input wire [1:0] wr_ch,
	input wire [1:0] wr_reg,                 // 0 address, 1 length, 2 step, 3 control
	input wire [31:0] wr_dat,
	output wire [1:0] playing,

	output reg aud_req = 1'b0,
	output reg [20:0] aud_adr = 21'd0,
	input wire aud_ack,
	input wire [15:0] aud_dat,

	output reg signed [15:0] out = 16'sd0,
	output reg out_t = 1'b0
);

// the 48 kHz tick
reg [26:0] acc = 27'd0;
reg tick = 1'b0;
always @(posedge clk) begin
	tick <= 1'b0;
	if (acc + 27'd48000 >= CLK_HZ) begin
		acc <= acc + 27'd48000 - CLK_HZ;
		tick <= 1'b1;
	end else
		acc <= acc + 27'd48000;
end

// the channels; the fetch serves one at a time, channel 0 first
wire need0, need1;
wire [20:0] adr0, adr1;
wire signed [15:0] smp0, smp1;
wire [7:0] vol0, vol1;
reg fetch_ch = 1'b0;
wire take0 = aud_req && aud_ack && !fetch_ch;
wire take1 = aud_req && aud_ack && fetch_ch;
menu_sound_ch ch0 (
	.clk(clk), .reset(reset), .tick(tick),
	.wr(wr && wr_ch == 2'd0), .wr_reg(wr_reg), .wr_dat(wr_dat), .playing(playing[0]),
	.need(need0), .adr(adr0), .take(take0), .dat(aud_dat), .smp(smp0), .vol(vol0)
);
menu_sound_ch ch1 (
	.clk(clk), .reset(reset), .tick(tick),
	.wr(wr && wr_ch == 2'd1), .wr_reg(wr_reg), .wr_dat(wr_dat), .playing(playing[1]),
	.need(need1), .adr(adr1), .take(take1), .dat(aud_dat), .smp(smp1), .vol(vol1)
);

always @(posedge clk) begin
	if (aud_req && aud_ack)
		aud_req <= 1'b0;
	else if (!aud_req && (need0 || need1)) begin
		fetch_ch <= !need0;
		aud_adr <= need0 ? adr0 : adr1;
		aud_req <= 1'b1;
	end
	if (reset) aud_req <= 1'b0;
end

// the mix at each tick
wire signed [24:0] m0 = smp0 * $signed({ 1'b0, vol0 });
wire signed [24:0] m1 = smp1 * $signed({ 1'b0, vol1 });
wire signed [15:0] a0 = m0[23:8], a1 = m1[23:8];   // (a part select is unsigned: signed wires first)
wire signed [17:0] sum = a0 + a1;
reg tick_d = 1'b0;
always @(posedge clk) begin
	tick_d <= tick;
	if (tick_d) begin
		out <= (sum > 18'sd32767) ? 16'sd32767 : (sum < -18'sd32768) ? -16'sd32768 : sum[15:0];
		out_t <= ~out_t;
	end
end

endmodule

// a channel of menu_sound
module menu_sound_ch (
	input wire clk,
	input wire reset,
	input wire tick,
	input wire wr,
	input wire [1:0] wr_reg,
	input wire [31:0] wr_dat,
	output reg playing = 1'b0,
	output reg need = 1'b0,                  // the sample at pos is wanted
	output wire [20:0] adr,                  // its halfword address
	input wire take,                         // here it is (dat)
	input wire [15:0] dat,
	output reg signed [15:0] smp = 16'sd0,
	output reg [7:0] vol = 8'd0
);

reg [21:0] base = 22'd0;
reg [19:0] len = 20'd0;
reg [16:0] step = 17'd0;
reg [19:0] pos = 20'd0;
reg [15:0] frac = 16'd0;
assign adr = base[21:1] + { 1'b0, pos };
wire [16:0] fsum = { 1'b0, frac } + step;   // carry: the next sample

always @(posedge clk) begin
	if (wr)
		case (wr_reg)
			2'd0: base <= wr_dat[21:0];
			2'd1: len <= wr_dat[19:0];
			2'd2: step <= wr_dat[16:0];
			2'd3: begin
				vol <= wr_dat[7:0];
				playing <= wr_dat[8];
				if (wr_dat[8]) begin
					pos <= 20'd0;
					frac <= 16'd0;
					smp <= 16'sd0;
					need <= 1'b1;
				end else
					need <= 1'b0;
			end
		endcase
	else if (tick && playing) begin
		frac <= fsum[15:0];
		if (fsum[16]) begin
			if (pos + 20'd1 >= len) begin
				playing <= 1'b0;
				smp <= 16'sd0;
				need <= 1'b0;
			end else begin
				pos <= pos + 20'd1;
				need <= 1'b1;
			end
		end
	end
	if (take) begin
		smp <= dat;
		need <= 1'b0;
	end
	if (reset) begin
		playing <= 1'b0;
		need <= 1'b0;
		smp <= 16'sd0;
	end
end

endmodule
