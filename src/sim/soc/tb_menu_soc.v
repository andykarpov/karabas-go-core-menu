`timescale 1ns / 1ps
`default_nettype none
// tb_menu_soc: menu_soc + menu_video with a model of the SRAM (two 2M x 8 chips, asynchronous); the
// clocks, the loader and the frame capture are driven by tb_menu_soc.cpp (Verilator).
module tb_menu_soc (
	input wire clk_cpu,
	input wire clk_pix,
	input wire reset,
	input wire ld_active,
	input wire ld_end,
	input wire [31:0] ld_addr,
	input wire [7:0] ld_data,
	input wire ld_wr,
	output wire ld_ready,
	output wire [7:0] dbg_uart,
	output wire dbg_uart_wr,
	output wire cpu_run,
	output wire [23:0] rgb,
	output wire show_o,
	output wire hs,
	output wire vs,
	output wire de,
	// the keyboard report and the link side of kg_l2_rpc (the model of the MCU in the .cpp)
	input wire [55:0] kbd,
	input wire l_sel,
	input wire l_stb,
	input wire [7:0] l_cmd,
	input wire [9:0] l_n,
	input wire [7:0] l_d,
	input wire l_rq,
	input wire [9:0] l_rq_m,
	output wire [7:0] l_q,
	output wire l_irq
);
wire [10:0] rpc_a;
wire rpc_we;
wire [7:0] rpc_wd, rpc_rd;
kg_l2_rpc #(.SERVICES(16'h0007)) rpc (
	.clk(clk_cpu), .reset(reset),
	.sel(l_sel), .stb(l_stb), .cmd(l_cmd), .n(l_n), .d(l_d), .rq(l_rq), .rq_m(l_rq_m), .q(l_q), .irq(l_irq),
	.cpu_a(rpc_a), .cpu_we(rpc_we), .cpu_wd(rpc_wd), .cpu_rd(rpc_rd)
);

wire vid_req, vid_we, pal_we, fb_sel, fb_shown, frame_t, show;
wire [20:0] vid_adr;
wire [8:0] vid_len, vid_wa;
wire [15:0] vid_wd;
wire [7:0] pal_wa;
wire [23:0] pal_wd;
wire [20:0] MA;
wire [15:0] MD_O, MD_OE_N;
wire [1:0] MWR_N, MRD_N;
reg [15:0] mem [0:(1 << 21) - 1];

// the SRAM: a read gives the word at MA; a write is taken when /WE rises
wire [15:0] MD_I = mem[MA];
always @(posedge MWR_N[0]) if (MD_OE_N[0] == 1'b0) mem[MA][7:0] <= MD_O[7:0];
always @(posedge MWR_N[1]) if (MD_OE_N[8] == 1'b0) mem[MA][15:8] <= MD_O[15:8];

menu_soc #(.CLR_LAST(21'h00FFFF)) soc (
	.clk(clk_cpu), .reset(reset),
	.ld_kind(2'd0), .ld_active(ld_active), .ld_end(ld_end), .ld_addr(ld_addr), .ld_data(ld_data),
	.ld_wr(ld_wr), .ld_ready(ld_ready),
	.kbd(kbd), .kbd_wr(1'b0), .ms_x(8'd0), .ms_y(8'd0), .ms_b(8'd0), .ms_wr(1'b0),
	.joy_l(16'd0), .joy_r(16'd0),
	.vid_req(vid_req), .vid_adr(vid_adr), .vid_len(vid_len), .vid_we(vid_we), .vid_wa(vid_wa), .vid_wd(vid_wd),
	.pal_we(pal_we), .pal_wa(pal_wa), .pal_wd(pal_wd), .fb_sel(fb_sel), .fb_shown(fb_shown),
	.frame_t(frame_t), .show(show),
	.rpc_a(rpc_a), .rpc_we(rpc_we), .rpc_wd(rpc_wd), .rpc_rd(rpc_rd),
	.dbg_uart(dbg_uart), .dbg_uart_wr(dbg_uart_wr), .cpu_run(cpu_run),
	.MA(MA), .MD_I(MD_I), .MD_O(MD_O), .MD_OE_N(MD_OE_N), .MWR_N(MWR_N), .MRD_N(MRD_N)
);

menu_video video (
	.clk_cpu(clk_cpu),
	.vid_req(vid_req), .vid_adr(vid_adr), .vid_len(vid_len), .vid_we(vid_we), .vid_wa(vid_wa), .vid_wd(vid_wd),
	.pal_we(pal_we), .pal_wa(pal_wa), .pal_wd(pal_wd), .show(show), .fb_sel(fb_sel), .fb_shown(fb_shown),
	.frame_t(frame_t),
	.clk_pix(clk_pix), .rgb(rgb), .show_o(show_o), .hs(hs), .vs(vs), .de(de)
);

endmodule
