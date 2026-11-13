`timescale 1ns / 1ps
/*
 The background of the menu behind the bitmap OSD: the glow of the FT812 menu (firmware
 app_core_browser: a vertical gradient from color_gradient 0x0000C8 at the top to the black at 3/4
 of the height), alive: the glow breathes (75..100 % in about 8 s) and its tint drifts slowly
 between blue, violet and teal (about 50 s a turn). 4 x 4 ordered dither on 4 fraction bits, as
 the dithered gradient bitmap of the FT menu, so the 8 bit steps do not band.

 The colour of a line is worked out once per line (in the blanking), the pixels only add the dither.
 rgb_o / hs_o / vs_o / de_o are the inputs one clock later.
*/
module menu_bg #(
	parameter V_GLOW = 450   // lines from the top to the black (3/4 of 600)
)(
	input wire clk,
	input wire hs_i,
	input wire vs_i,
	input wire de_i,
	output reg [23:0] rgb_o = 24'd0,
	output reg hs_o = 1'b0,
	output reg vs_o = 1'b0,
	output reg de_o = 1'b0
);

// the position: x in a line, y from the first visible line
reg [10:0] x = 11'd0;
reg [9:0] y = 10'd0;
reg de_d = 1'b0, vs_d = 1'b0;
reg [12:0] frame = 13'd0;
wire line_end = de_d & ~de_i;
wire frame_start = vs_i & ~vs_d;
always @(posedge clk) begin
	de_d <= de_i;
	vs_d <= vs_i;
	x <= de_i ? x + 11'd1 : 11'd0;
	if (frame_start) begin
		y <= 10'd0;
		frame <= frame + 13'd1;
	end
	else if (line_end)
		y <= y + 10'd1;
end

// a triangle 0..255..0 of an 8 bit phase
function [7:0] triwave;
	input [8:0] p;
	triwave = p[8] ? ~p[7:0] : p[7:0];
endfunction

// the colour of the line: level = the glow at y (255 at the top, 0 at V_GLOW), scaled by the
// breath; tint: red / green 0..63 against blue 200 (0x0000C8 at the middle of the drift)
wire [7:0] tri_b = triwave(frame[8:0]);
wire [7:0] tri_r = triwave(frame[12:4]);
wire [7:0] tri_g = triwave(frame[12:4] + 9'd256);
wire [7:0] breath = 8'd191 + { 2'b00, tri_b[7:2] };   // 191..254
wire [7:0] tint_r = { 2'b00, tri_r[7:2] };            // 0..63
wire [7:0] tint_g = { 2'b00, tri_g[7:2] };            // 0..63, the other half
reg [7:0] level = 8'd0;
reg [15:0] lvl_b = 16'd0;
reg [11:0] line_r = 12'd0, line_g = 12'd0, line_b = 12'd0;  // 8.4
reg [15:0] mr = 16'd0, mg = 16'd0, mb = 16'd0;
reg [2:0] step = 3'd0;
wire [17:0] glow_prod = { 8'd0, V_GLOW[9:0] - y } * 18'd145;
always @(posedge clk) begin
	// three clocks after the end of a line (still in the blanking): level, breath, colour
	if (line_end || frame_start) step <= 3'd1;
	else if (step != 3'd0) step <= step + 3'd1;
	case (step)
		3'd1: level <= (y >= V_GLOW[9:0]) ? 8'd0 : glow_prod[15:8];  // (V_GLOW - y) * 255 / 450
		3'd2: lvl_b <= level * breath;
		3'd3: begin
			mr <= lvl_b[15:8] * tint_r;
			mg <= lvl_b[15:8] * tint_g;
			mb <= lvl_b[15:8] * 8'd200;
		end
		3'd4: begin
			line_r <= mr[15:4];
			line_g <= mg[15:4];
			line_b <= mb[15:4];
		end
		default: ;
	endcase
end

// 4 x 4 Bayer threshold (0..15) of the pixel
reg [3:0] bayer;
always @* begin
	case ({ y[1:0], x[1:0] })
		4'h0: bayer = 4'd0;  4'h1: bayer = 4'd8;  4'h2: bayer = 4'd2;  4'h3: bayer = 4'd10;
		4'h4: bayer = 4'd12; 4'h5: bayer = 4'd4;  4'h6: bayer = 4'd14; 4'h7: bayer = 4'd6;
		4'h8: bayer = 4'd3;  4'h9: bayer = 4'd11; 4'hA: bayer = 4'd1;  4'hB: bayer = 4'd9;
		4'hC: bayer = 4'd15; 4'hD: bayer = 4'd7;  4'hE: bayer = 4'd13; default: bayer = 4'd5;
	endcase
end

function [7:0] dith;
	input [11:0] v;
	input [3:0] t;
	reg [12:0] s;
	begin
		s = v + t;
		dith = s[12] ? 8'hFF : s[11:4];
	end
endfunction

always @(posedge clk) begin
	rgb_o <= de_i ? { dith(line_r, bayer), dith(line_g, bayer), dith(line_b, bayer) } : 24'd0;
	hs_o <= hs_i;
	vs_o <= vs_i;
	de_o <= de_i;
end

endmodule
