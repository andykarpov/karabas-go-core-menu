`timescale 1ns / 1ps
`default_nettype none
/*
 menu_compose.v

 The picture of the menu under the OSD (docs/PLAN.md Ф2), in the pixel clock:
 - the background: while the CPU shows its frame buffer (show), the gradient of the FT812 menu
   (firmware app_core_browser_ft_background: grad_top at the top to grad_bot at 3/4 of the height,
   grad_bot below; the [ft812] colours of karabas.ini, written by the CPU), with a 4 x 4 ordered
   dither on 4 fraction bits; else the glow of menu_bg (glow_i, the old menu, the firmware OSD on it);
 - the frame buffer over it: rgba_i = the palette entry of the pixel (menu_video), alpha 0..15
   (0 transparent, 15 opaque): out = (c * a + bg * (16 - a)) / 16, a = 16 for 15.
 glow_i comes one clock after hs_i / vs_i / de_i / rgba_i (menu_bg); the output two clocks after
 glow_i. grad_top / grad_bot are registers of the CPU clock, stable while they are used (written
 when the menu starts or the colours change).
*/
module menu_compose #(
	parameter [9:0] V_GRAD = 10'd450   // lines of the gradient (3/4 of 600)
) (
	input wire clk,
	input wire [27:0] rgba_i,          // { alpha, r, g, b }
	input wire show_i,
	input wire hs_i,
	input wire vs_i,
	input wire de_i,
	input wire [23:0] glow_i,          // menu_bg, one clock later
	input wire [23:0] grad_top,
	input wire [23:0] grad_bot,
	output reg [23:0] rgb_o = 24'd0,
	output reg hs_o = 1'b0,
	output reg vs_o = 1'b0,
	output reg de_o = 1'b0
);

// ---------------- the position ----------------
reg [10:0] x = 11'd0;
reg [9:0] y = 10'd0;
reg de_d = 1'b0, vs_d = 1'b0;
wire line_end = de_d & ~de_i;
wire frame_start = vs_i & ~vs_d;
always @(posedge clk) begin
	de_d <= de_i;
	vs_d <= vs_i;
	x <= de_i ? x + 11'd1 : 11'd0;
	if (frame_start) y <= 10'd0;
	else if (line_end) y <= y + 10'd1;
end

// ---------------- the gradient: a colour per line, worked out in the blanking ----------------
// t = y * 256 / V_GRAD (0..255); c = top + (bot - top) * t / 256 in 8.4 fixed point
reg [8:0] t = 9'd0;
reg [2:0] step = 3'd0;
reg signed [17:0] dr = 18'sd0, dg = 18'sd0, db = 18'sd0;
reg [11:0] line_r = 12'd0, line_g = 12'd0, line_b = 12'd0;
wire [17:0] t_prod = { 8'd0, y } * 18'd146;    // 256 / 450 * 256 = 145.6
function signed [17:0] dmul(input [7:0] a, input [7:0] b, input [8:0] tt);
	dmul = ($signed({ 1'b0, b }) - $signed({ 1'b0, a })) * $signed({ 1'b0, tt });
endfunction
function [11:0] lerp(input [7:0] a, input signed [17:0] d);
	reg signed [17:0] s;
	begin
		s = $signed({ 6'd0, a, 4'd0 }) + (d >>> 4);
		lerp = s[11:0];
	end
endfunction
always @(posedge clk) begin
	if (line_end || frame_start) step <= 3'd1;
	else if (step != 3'd0) step <= step + 3'd1;
	case (step)
		// (the product reaches 256 on the last lines of the gradient: 9 bits, at most 256)
		3'd1: t <= (y >= V_GRAD || t_prod[16]) ? 9'd256 : { 1'b0, t_prod[15:8] };
		3'd2: begin
			dr <= dmul(grad_top[23:16], grad_bot[23:16], t);
			dg <= dmul(grad_top[15:8], grad_bot[15:8], t);
			db <= dmul(grad_top[7:0], grad_bot[7:0], t);
		end
		3'd3: begin
			line_r <= lerp(grad_top[23:16], dr);
			line_g <= lerp(grad_top[15:8], dg);
			line_b <= lerp(grad_top[7:0], db);
		end
		default: ;
	endcase
end

reg [3:0] bayer;
always @* begin
	case ({ y[1:0], x[1:0] })
		4'h0: bayer = 4'd0;  4'h1: bayer = 4'd8;  4'h2: bayer = 4'd2;  4'h3: bayer = 4'd10;
		4'h4: bayer = 4'd12; 4'h5: bayer = 4'd4;  4'h6: bayer = 4'd14; 4'h7: bayer = 4'd6;
		4'h8: bayer = 4'd3;  4'h9: bayer = 4'd11; 4'hA: bayer = 4'd1;  4'hB: bayer = 4'd9;
		4'hC: bayer = 4'd15; 4'hD: bayer = 4'd7;  4'hE: bayer = 4'd13; default: bayer = 4'd5;
	endcase
end
function [7:0] dith(input [11:0] v, input [3:0] th);
	reg [12:0] s;
	begin
		s = v + th;
		dith = s[12] ? 8'hFF : s[11:4];
	end
endfunction

// ---------------- stage 1 (with glow_i): the background and the pixel ----------------
reg [23:0] bg = 24'd0;
reg [27:0] px = 28'd0;
reg show_d = 1'b0;
reg [27:0] rgba_d = 28'd0;
reg hs_d = 1'b0, vs_d2 = 1'b0, de_d2 = 1'b0;
always @(posedge clk) begin
	// the inputs a clock later, with glow_i
	rgba_d <= rgba_i;
	show_d <= show_i;
	hs_d <= hs_i;
	vs_d2 <= vs_i;
	de_d2 <= de_i;
end
wire [23:0] grad = { dith(line_r, bayer), dith(line_g, bayer), dith(line_b, bayer) };
reg [23:0] grad_d = 24'd0;
always @(posedge clk) grad_d <= grad;  // the gradient pixel a clock later, with glow_i

// ---------------- stage 2: the blend ----------------
wire [4:0] a = (rgba_d[27:24] == 4'd15) ? 5'd16 : { 1'b0, rgba_d[27:24] };
wire [4:0] na = 5'd16 - a;
wire [23:0] base = show_d ? grad_d : glow_i;
reg [12:0] br = 13'd0, bgc = 13'd0, bb = 13'd0;
reg hs_e = 1'b0, vs_e = 1'b0, de_e = 1'b0;
always @(posedge clk) begin
	br  <= rgba_d[23:16] * a + base[23:16] * na;
	bgc <= rgba_d[15:8] * a + base[15:8] * na;
	bb  <= rgba_d[7:0] * a + base[7:0] * na;
	hs_e <= hs_d; vs_e <= vs_d2; de_e <= de_d2;
	rgb_o <= de_e ? { br[11:4], bgc[11:4], bb[11:4] } : 24'd0;
	hs_o <= hs_e; vs_o <= vs_e; de_o <= de_e;
end

endmodule
