`timescale 1ns / 1ps
`default_nettype none
/*
 menu_video.v

 The picture of the menu CPU: 800 x 600 at 60 Hz (the timing of vga_sync, clk_pix 40.91 MHz), a colour
 index per pixel from one of the two frame buffers in the SRAM (FB0_HW / FB1_HW, halfword addresses;
 two pixels a halfword, the even pixel in the low byte), through the palette of that buffer (two banks of 256 x { alpha 4, RGB 24 }, bank = buffer:
 menu_compose blends it over the background). After
 doom_video.v of karabas-go-core-doom.

 The buffer: fb_sel (clk_cpu, the MMIO FB register) is taken at the start of the vertical sync
 (fb_shown, back to clk_cpu), so a frame is never changed while it is shown; frame_t toggles there too.

 Lines: line L + 1 is fetched into line buffer half (L + 1) & 1 at the start of line L (h 0), line 0
 during the last line of the frame; a toggle with the line to clk_cpu (menu_sram vid_* port, 400
 halfwords, about 11 us of the 25.8 us of a line). The other half holds the line being shown.

 Output: hs / vs / de with the polarity of vga_sync (high active, as the overlay wants them), rgba the
 palette entry of the pixel (show: the MMIO SHOW register; 0 = transparent when off), 4 clocks after
 the counters.
*/
module menu_video #(
	parameter [20:0] FB0_HW = 21'h180000,    // byte 0x300000
	parameter [20:0] FB1_HW = 21'h1C0000     // byte 0x380000
) (
	input wire clk_cpu,
	output reg vid_req = 1'b0,
	output reg [20:0] vid_adr = 21'd0,
	output wire [8:0] vid_len,
	input wire vid_we,
	input wire [8:0] vid_wa,
	input wire [15:0] vid_wd,
	input wire pal_we,
	input wire [8:0] pal_wa,               // { bank, entry }: bank = the frame buffer it is for
	input wire [27:0] pal_wd,
	input wire show,                       // clk_cpu
	input wire fb_sel,                     // clk_cpu
	output wire fb_shown,                  // clk_cpu
	output wire frame_t,                   // clk_cpu

	input wire clk_pix,
	output reg [27:0] rgba = 28'd0,
	output reg show_o = 1'b0,              // the frame buffer is on (with rgb)
	output reg hs = 1'b0,
	output reg vs = 1'b0,
	output reg de = 1'b0
);

// 800 x 600 at 60 Hz, as vga_sync: the line counter steps at h 839
localparam [10:0] H_VIS = 11'd800, H_SYNC_ON = 11'd839, H_SYNC_OFF = 11'd967, H_END = 11'd1055;
localparam [9:0] V_VIS = 10'd600, V_SYNC_ON = 10'd600, V_SYNC_OFF = 10'd604, V_END = 10'd627;

assign vid_len = 9'd400;

// ------------------------------------------------------------------ counters (clk_pix)

reg [10:0] h = 11'd0;
reg [9:0] v = 10'd0;
reg req_t = 1'b0;                          // toggled for each line to fetch
reg [9:0] req_line = 10'd0;
wire sel_p;
kg_sync #(.W(1)) sync_sel (.clk(clk_pix), .d(fb_sel), .q(sel_p));
reg shown_p = 1'b0;
reg frame_p = 1'b0;
wire [9:0] v_next = (v == V_END) ? 10'd0 : v + 10'd1;
always @(posedge clk_pix) begin
	h <= (h == H_END) ? 11'd0 : h + 11'd1;
	if (h == H_SYNC_ON) v <= v_next;
	// the buffer for the next frame, at the start of the vertical sync (before the fetch of line 0)
	if (h == 11'd0 && v == V_SYNC_ON + 10'd1) begin
		shown_p <= sel_p;
		frame_p <= ~frame_p;
	end
	// the fetch of the next line at the start of this one
	if (h == 11'd0 && v_next < V_VIS) begin
		req_line <= v_next;
		req_t <= ~req_t;
	end
end

// ------------------------------------------------------------------ fetch (clk_cpu)

wire req_s;
kg_sync #(.W(1)) sync_req (.clk(clk_cpu), .d(req_t), .q(req_s));
kg_sync #(.W(1)) sync_shown (.clk(clk_cpu), .d(shown_p), .q(fb_shown));
kg_sync #(.W(1)) sync_frame (.clk(clk_cpu), .d(frame_p), .q(frame_t));
reg req_p = 1'b0;
reg buf_w = 1'b0;                          // the half of the line being fetched
// line * 400 = line * 256 + line * 128 + line * 16
wire [20:0] line_ofs = { 3'd0, req_line, 8'd0 } + { 4'd0, req_line, 7'd0 } + { 7'd0, req_line, 4'd0 };
always @(posedge clk_cpu) begin
	req_p <= req_s;
	vid_req <= 1'b0;
	if (req_s != req_p) begin
		// req_line and shown_p are stable: they changed with the toggle two clk_cpu syncs ago
		vid_req <= 1'b1;
		vid_adr <= (fb_shown ? FB1_HW : FB0_HW) + line_ofs;
		buf_w <= req_line[0];
	end
end

wire [15:0] lb_q;
reg [9:0] lb_ra = 10'd0;
kg_sdpram #(.DW(16), .AW(10)) linebuf (
	.clka(clk_cpu), .wea(vid_we), .addra({ buf_w, vid_wa }), .dina(vid_wd),
	.clkb(clk_pix), .enb(1'b1), .addrb(lb_ra), .doutb(lb_q)
);

wire [27:0] pal_q;
reg [8:0] pal_ra = 9'd0;
kg_sdpram #(.DW(28), .AW(9)) palette (
	.clka(clk_cpu), .wea(pal_we), .addra(pal_wa), .dina(pal_wd),
	.clkb(clk_pix), .enb(1'b1), .addrb(pal_ra), .doutb(pal_q)
);

wire show_p;
kg_sync #(.W(1)) sync_show (.clk(clk_pix), .d(show), .q(show_p));

// ------------------------------------------------------------------ pixels (clk_pix)

// edge n: the line buffer address of (h, v); n+1: the halfword; n+2: the palette address; n+3: the
// colour; n+4: out (the syncs and de through 4 stages as well)
wire vis0 = h < H_VIS && v < V_VIS;
reg [3:0] vis = 4'd0, hsd = 4'd0, vsd = 4'd0;
reg [1:0] bsel = 2'd0;
always @(posedge clk_pix) begin
	lb_ra <= { v[0], h[9:1] };
	bsel <= { bsel[0], h[0] };
	pal_ra <= { shown_p, bsel[1] ? lb_q[15:8] : lb_q[7:0] };   // the palette of the buffer shown
	vis <= { vis[2:0], vis0 };
	hsd <= { hsd[2:0], h > H_SYNC_ON && h <= H_SYNC_OFF };
	vsd <= { vsd[2:0], v > V_SYNC_ON && v <= V_SYNC_OFF };
	rgba <= (vis[3] && show_p) ? pal_q : 28'd0;
	show_o <= show_p;
	de <= vis[3];
	hs <= hsd[3];
	vs <= vsd[3];
end

endmodule
