`timescale 1ns / 1ps
`default_nettype none
/*
 menu_soc.v

 The computer of the boot menu (docs/PLAN.md): VexRiscv (rv32im, I$ 4K, D$ 4K, wishbone), the SRAM
 (menu_sram: the program, the data and the two frame buffers), the MMIO registers, the loader. One
 clock (clk, 75 MHz). After doom_soc.v of karabas-go-core-doom (no SDRAM: the Mini G rev G2 has it off,
 the menu must run on every board).

 Memory map of the CPU (sw/mmio.h):
   0x00000000 SRAM 4 MB: the program from 0 (the ROMs of boot.kg), the frame buffers FB0 0x300000 and
              FB1 0x380000 (800 x 600 bytes each, a colour index per pixel)
   0x80000000 MMIO (uncached: the VexRiscv IO range = bit 31), word registers:
     00 W  DBG: a byte of the debug console (dbg_uart, the testbench prints it)
     04 W  FB: bit 0 the frame buffer to show from the next frame; R: bit 0 the one shown now
     08 R  FRAMES: frames begun (the start of the vertical sync)
     0c R  CYCLES: clocks since the CPU started (low 32)
     10 W  SHOW: bit 0 the frame buffer on the screen (0: the background of the old menu)
     18 W  GRAD_TOP: 0x00RRGGBB, the top colour of the background gradient (menu_compose)
     20 W  GRAD_BOT: 0x00RRGGBB, its bottom colour (3/4 of the height and below)
     14 W  LAUNCH: the RPC call written (CORES LAUNCH) is made after the clear of the SRAM: the CPU
           stops for good, the SRAM is cleared (the next core starts with clean memory), then the call
           goes to the firmware, which replaces the core
     1c R  MS: milliseconds since the CPU started
     28 R  KBD0: kg_mcu2 KBD[31:0] = { key 3, key 2, key 1, modifiers } (the USB boot report)
     2c R  KBD1: { 0, key 6, key 5, key 4 }            30 R  KBDN: reports seen (a counter)
     34 R  MOUSE: bits 31..24 buttons (held now, or pressed since the last read: a click of press and
           release between two reads is not lost), 23..12 dy sum, 11..0 dx sum (signed, saturated;
           reading clears)
     38 R  JOY: { joy_r, joy_l }
     40 R  US: microseconds since the CPU started
     80..9c      SOUND: the two channels of menu_sound (+0 address, +4 length, +8 step, +c control /
                 R playing), channel 1 at 0x90
     a0 W  MIDI: a byte to the SAM2695 (menu_midi_mux, whole messages with their status); R bit 0
           its fifo is full
     400..7fc W  PAL: palette entry ((a - 400) / 4) = 0xARRGGBB: alpha 0..15 in bits 31..28 over the
                 background (0 transparent, 15 opaque)
     2000..3ffc  RPC: byte (a - 2000) / 4 of the mailbox of kg_l2_rpc (kg_mcu2 RPC_*), in the low byte
                 of a word (map in kg_l2_rpc.v: request 000.., response 200.., event 400.., registers 600..)
 Instruction fetch and data share the SRAM (the data first); an owner keeps it to the last ack of its
 transaction (CTI 010 = more beats).

 Loader (kg_mcu2 LD_*): kind 0 (the ROMs of boot.kg) to the SRAM from 0, a byte per write; ld_ready
 low while one is pending or the SRAM is being cleared (caps bit 4: the firmware asks LOAD STATUS before
 each packet). The CPU is held in reset while anything loads, while the SRAM is cleared, and until the
 first load has ended (no program before it).
*/
module menu_soc #(
	parameter MS_DIV = 17'd75000,         // clocks per millisecond
	parameter US_DIV = 7'd75,             // clocks per microsecond
	parameter [31:0] RESET_VECTOR = 32'h00000000,
	parameter [20:0] CLR_LAST = 21'h1FFFFF
) (
	input wire clk,
	input wire reset,                     // the PLL

	// loader (kg_mcu2)
	input wire [1:0] ld_kind,
	input wire ld_active,
	input wire ld_end,
	input wire [31:0] ld_addr,
	input wire [7:0] ld_data,
	input wire ld_wr,
	output wire ld_ready,

	// HID (kg_mcu2)
	input wire [55:0] kbd,
	input wire kbd_wr,
	input wire [7:0] ms_x,
	input wire [7:0] ms_y,
	input wire [7:0] ms_b,
	input wire ms_wr,
	input wire [15:0] joy_l,
	input wire [15:0] joy_r,

	// video (menu_video): the SRAM read port, the palette, the buffers
	input wire vid_req,
	input wire [20:0] vid_adr,
	input wire [8:0] vid_len,
	output wire vid_we,
	output wire [8:0] vid_wa,
	output wire [15:0] vid_wd,
	output reg pal_we = 1'b0,
	output reg [7:0] pal_wa = 8'd0,
	output reg [27:0] pal_wd = 28'd0,
	output reg [23:0] grad_top = 24'h0000C8,  // the FT812 defaults of the firmware
	output reg [23:0] grad_bot = 24'h000000,
	output reg fb_sel = 1'b0,             // the buffer to show
	input wire fb_shown,                  // the one shown (synchronised to clk)
	input wire frame_t,                   // toggles at each frame (synchronised to clk)
	output reg show = 1'b0,

	// sound: the PCM mix (48 kHz, snd_t toggles with each value) and the MIDI bytes of the CPU
	output wire signed [15:0] snd_out,
	output wire snd_t,
	output reg [7:0] midi_data = 8'd0,
	output reg midi_wr = 1'b0,
	input wire midi_full,

	// RPC (kg_mcu2 RPC_*)
	output reg [10:0] rpc_a = 11'd0,
	output reg rpc_we = 1'b0,
	output reg [7:0] rpc_wd = 8'd0,
	input wire [7:0] rpc_rd,

	// debug
	output reg [7:0] dbg_uart = 8'd0,
	output reg dbg_uart_wr = 1'b0,
	output wire cpu_run,

	// SRAM pins
	output wire [20:0] MA,
	input wire [15:0] MD_I,
	output wire [15:0] MD_O,
	output wire [15:0] MD_OE_N,
	output wire [1:0] MWR_N,
	output wire [1:0] MRD_N
);

// ------------------------------------------------------------------ reset / clear / loader

// the clear of the SRAM after the PLL lock: every core starts with clean memory
reg reset_d = 1'b1;
reg clr_req = 1'b0;
wire clr_busy;
always @(posedge clk) begin
	reset_d <= reset;
	clr_req <= (reset_d & ~reset) | launch_clr;
end

reg loaded = 1'b0;                    // a program came (the end of a kind 0 load)
reg halted = 1'b0;                    // LAUNCH: the CPU stops for good (its program is cleared)
reg cpu_reset = 1'b1;
assign cpu_run = ~cpu_reset;
always @(posedge clk) begin
	if (reset) loaded <= 1'b0;
	else if (ld_end && ld_kind == 2'd0) loaded <= 1'b1;
	cpu_reset <= reset | ld_active | clr_busy | clr_req | ~loaded | halted;
end

// LAUNCH: the clear, then the call (the write of 605 to kg_l2_rpc)
reg launch_clr = 1'b0;                // the clear is asked
reg launch_wait = 1'b0;               // waiting for its end
reg launch_call = 1'b0;               // one clock: the call

reg ld_pend = 1'b0;
reg [21:0] ld_a = 22'd0;
reg [7:0] ld_d = 8'd0;
assign ld_ready = ~ld_pend & ~clr_busy & ~clr_req;

// ------------------------------------------------------------------ CPU

wire i_cyc, i_stb, d_cyc, d_stb, d_we;
wire [29:0] i_adr, d_adr;
wire [31:0] d_dat_o;
wire [3:0] d_sel;
wire [2:0] i_cti, d_cti;
reg [31:0] i_dat_i, d_dat_i;
reg i_ack, d_ack;

VexRiscv cpu (
	.externalResetVector(RESET_VECTOR),
	.timerInterrupt(1'b0),
	.softwareInterrupt(1'b0),
	.externalInterruptArray(32'd0),
	.iBusWishbone_CYC(i_cyc),
	.iBusWishbone_STB(i_stb),
	.iBusWishbone_ACK(i_ack),
	.iBusWishbone_WE(),
	.iBusWishbone_ADR(i_adr),
	.iBusWishbone_DAT_MISO(i_dat_i),
	.iBusWishbone_DAT_MOSI(),
	.iBusWishbone_SEL(),
	.iBusWishbone_ERR(1'b0),
	.iBusWishbone_CTI(i_cti),
	.iBusWishbone_BTE(),
	.dBusWishbone_CYC(d_cyc),
	.dBusWishbone_STB(d_stb),
	.dBusWishbone_ACK(d_ack),
	.dBusWishbone_WE(d_we),
	.dBusWishbone_ADR(d_adr),
	.dBusWishbone_DAT_MISO(d_dat_i),
	.dBusWishbone_DAT_MOSI(d_dat_o),
	.dBusWishbone_SEL(d_sel),
	.dBusWishbone_ERR(1'b0),
	.dBusWishbone_CTI(d_cti),
	.dBusWishbone_BTE(),
	.clk(clk),
	.reset(cpu_reset)
);

wire d_req = d_cyc & d_stb;
wire i_req = i_cyc & i_stb;
wire d_io = d_adr[29];                      // 0x80000000
wire d_sr = ~d_io;

// ------------------------------------------------------------------ SRAM and its owner

localparam [1:0] O_NONE = 2'd0, O_D = 2'd1, O_I = 2'd2, O_LD = 2'd3;
reg [1:0] own = O_NONE;
wire sr_ack;
wire [31:0] sr_dat;
reg sr_cyc, sr_we;
reg [3:0] sr_sel;
reg [21:2] sr_adr;
reg [2:0] sr_cti;
reg [31:0] sr_wdat;

always @(*) begin
	sr_cyc = 1'b0; sr_we = 1'b0; sr_sel = 4'hF; sr_adr = 20'd0; sr_cti = 3'd0; sr_wdat = 32'd0;
	case (own)
	O_D: begin sr_cyc = d_req; sr_we = d_we; sr_sel = d_sel; sr_adr = d_adr[19:0]; sr_cti = d_cti; sr_wdat = d_dat_o; end
	O_I: begin sr_cyc = i_req; sr_we = 1'b0; sr_sel = 4'hF; sr_adr = i_adr[19:0]; sr_cti = i_cti; end
	O_LD: begin sr_cyc = ld_pend; sr_we = 1'b1; sr_sel = 4'b0001 << ld_a[1:0]; sr_adr = ld_a[21:2]; sr_cti = 3'd0; sr_wdat = {4{ld_d}}; end
	default: ;
	endcase
end

// the owner: kept to the last ack of a transaction (an ack with CTI not 010)
always @(posedge clk) begin
	if (cpu_reset) begin
		if (own == O_NONE && ld_pend) own <= O_LD;
		else if (own != O_LD || sr_ack) own <= O_NONE;
	end else if (own == O_NONE) begin
		if (d_req && d_sr) own <= O_D;
		else if (i_req) own <= O_I;
	end else if (sr_ack && sr_cti != 3'b010)
		own <= O_NONE;
end

wire aud_req, aud_ack;
wire [20:0] aud_adr;
wire [15:0] aud_dat;
menu_sram #(.CLR_LAST(CLR_LAST)) sram (
	.clk(clk),
	.clr_req(clr_req), .clr_busy(clr_busy),
	.aud_req(aud_req), .aud_adr(aud_adr), .aud_ack(aud_ack), .aud_dat(aud_dat),
	.wb_cyc(sr_cyc), .wb_stb(sr_cyc), .wb_we(sr_we), .wb_sel(sr_sel), .wb_adr(sr_adr), .wb_cti(sr_cti),
	.wb_dat_i(sr_wdat), .wb_dat_o(sr_dat), .wb_ack(sr_ack),
	// no fetches while the frame buffer is off (a loader byte must not wait behind a video chunk)
	.vid_req(vid_req & show & ~cpu_reset), .vid_adr(vid_adr), .vid_len(vid_len), .vid_we(vid_we), .vid_wa(vid_wa),
	.vid_wd(vid_wd), .vid_busy(),
	.MA(MA), .MD_I(MD_I), .MD_O(MD_O), .MD_OE_N(MD_OE_N), .MWR_N(MWR_N), .MRD_N(MRD_N)
);

// ------------------------------------------------------------------ sound

reg snd_wr = 1'b0;
reg [1:0] snd_ch = 2'd0, snd_reg = 2'd0;
reg [31:0] snd_dat = 32'd0;
wire [1:0] snd_playing;
menu_sound #(.CLK_HZ(MS_DIV * 1000)) sound (
	.clk(clk), .reset(cpu_reset),
	.wr(snd_wr), .wr_ch(snd_ch), .wr_reg(snd_reg), .wr_dat(snd_dat), .playing(snd_playing),
	.aud_req(aud_req), .aud_adr(aud_adr), .aud_ack(aud_ack), .aud_dat(aud_dat),
	.out(snd_out), .out_t(snd_t)
);

// ------------------------------------------------------------------ MMIO

reg [31:0] cycles = 32'd0;
reg [31:0] ms = 32'd0;
reg [16:0] ms_cnt = 17'd0;
reg [31:0] us = 32'd0;
reg [6:0] us_div = 7'd0;
reg [31:0] kbd_n = 32'd0;
reg [31:0] frames = 32'd0;
reg frame_p = 1'b0;
reg signed [11:0] ms_dx = 12'sd0, ms_dy = 12'sd0;
reg [7:0] ms_btn = 8'd0;
reg [7:0] ms_press = 8'd0;   // the buttons pressed since the last read
reg io_ack = 1'b0;
reg [31:0] io_dat = 32'd0;
wire [11:0] io_off = { d_adr[9:0], 2'b00 };
wire io_rpc = d_adr[11:11] == 1'b1;          // 0x2000..0x3ffc (word address bit 11)
// an RPC read: kg_l2_rpc registers the address, then its RAM / register output: rpc_rd is valid two
// clocks after rpc_a
reg rpc_rd_wait = 1'b0, rpc_rd_wait2 = 1'b0;

function signed [11:0] sat_add(input signed [11:0] a, input [7:0] d);
	reg signed [12:0] s;
	begin
		s = { a[11], a } + { {5{d[7]}}, d };
		sat_add = (s > 13'sd2047) ? 12'sd2047 : (s < -13'sd2048) ? -12'sd2048 : s[11:0];
	end
endfunction

always @(posedge clk) begin
	io_ack <= 1'b0;
	pal_we <= 1'b0;
	dbg_uart_wr <= 1'b0;
	rpc_we <= 1'b0;
	snd_wr <= 1'b0;
	midi_wr <= 1'b0;
	launch_clr <= 1'b0;
	launch_call <= 1'b0;
	if (launch_wait && !clr_req && !clr_busy && !launch_clr) begin
		launch_wait <= 1'b0;
		rpc_a <= 11'h605;
		rpc_wd <= 8'h01;
		rpc_we <= 1'b1;
	end
	if (reset) begin
		halted <= 1'b0;
		launch_wait <= 1'b0;
	end
	// an RPC read: the byte a clock after its address
	rpc_rd_wait2 <= rpc_rd_wait;
	rpc_rd_wait <= 1'b0;
	if (rpc_rd_wait2) begin
		io_ack <= 1'b1;
		io_dat <= { 24'd0, rpc_rd };
	end
	if (cpu_reset) begin
		cycles <= 32'd0;
		ms <= 32'd0;
		ms_cnt <= 17'd0;
		us <= 32'd0;
		us_div <= 7'd0;
	end else begin
		cycles <= cycles + 32'd1;
		if (ms_cnt == MS_DIV - 17'd1) begin ms_cnt <= 17'd0; ms <= ms + 32'd1; end
		else ms_cnt <= ms_cnt + 17'd1;
		if (us_div == US_DIV - 7'd1) begin us_div <= 7'd0; us <= us + 32'd1; end
		else us_div <= us_div + 7'd1;
	end
	frame_p <= frame_t;
	if (frame_t != frame_p) frames <= frames + 32'd1;
	if (kbd_wr) kbd_n <= kbd_n + 32'd1;
	if (ms_wr) begin
		ms_dx <= sat_add(ms_dx, ms_x);
		ms_dy <= sat_add(ms_dy, ms_y);
		ms_btn <= ms_b;
		ms_press <= ms_press | ms_b;
	end
	if (reset) begin
		show <= 1'b0;
		fb_sel <= 1'b0;
	end

	if (d_req && d_io && !io_ack && !rpc_rd_wait && !rpc_rd_wait2 && !cpu_reset && io_rpc) begin
		rpc_a <= d_adr[10:0];
		if (d_we) begin
			rpc_wd <= d_dat_o[7:0];
			rpc_we <= 1'b1;
			io_ack <= 1'b1;
		end else
			rpc_rd_wait <= 1'b1;
	end else if (d_req && d_io && !io_ack && !rpc_rd_wait && !rpc_rd_wait2 && !cpu_reset && !io_rpc) begin
		io_ack <= 1'b1;
		if (d_we) begin
			if (io_off[11:10] == 2'b01) begin   // 0x400..0x7fc
				pal_we <= 1'b1;
				pal_wa <= io_off[9:2];
				pal_wd <= { d_dat_o[31:28], d_dat_o[23:0] };
			end else if (io_off[7:5] == 3'b100) begin   // 0x80..0x9c: the sound
				snd_wr <= 1'b1;
				snd_ch <= { 1'b0, io_off[4] };
				snd_reg <= io_off[3:2];
				snd_dat <= d_dat_o;
			end else case (io_off[7:0])
				8'h00: begin dbg_uart <= d_dat_o[7:0]; dbg_uart_wr <= 1'b1; end
				8'ha0: begin midi_data <= d_dat_o[7:0]; midi_wr <= 1'b1; end
				8'h04: fb_sel <= d_dat_o[0];
				8'h10: show <= d_dat_o[0];
				8'h18: grad_top <= d_dat_o[23:0];
				8'h20: grad_bot <= d_dat_o[23:0];
				8'h14: begin halted <= 1'b1; show <= 1'b0; launch_clr <= 1'b1; launch_wait <= 1'b1; end
				default: ;
			endcase
		end else begin
			case (io_off[7:0])
				8'h04: io_dat <= { 31'd0, fb_shown };
				8'h08: io_dat <= frames;
				8'h0c: io_dat <= cycles;
				8'h1c: io_dat <= ms;
				8'h28: io_dat <= kbd[31:0];
				8'h2c: io_dat <= { 8'd0, kbd[55:32] };
				8'h30: io_dat <= kbd_n;
				8'h34: begin
					io_dat <= { ms_btn | ms_press, ms_dy, ms_dx };
					ms_press <= ms_wr ? ms_b : 8'd0;
					if (ms_wr) begin
						ms_dx <= sat_add(12'sd0, ms_x);
						ms_dy <= sat_add(12'sd0, ms_y);
					end else begin
						ms_dx <= 12'sd0;
						ms_dy <= 12'sd0;
					end
				end
				8'h38: io_dat <= { joy_r, joy_l };
				8'h40: io_dat <= us;
				8'h8c: io_dat <= { 23'd0, snd_playing[0], 8'd0 };
				8'h9c: io_dat <= { 23'd0, snd_playing[1], 8'd0 };
				8'ha0: io_dat <= { 31'd0, midi_full };
				default: io_dat <= 32'd0;
			endcase
		end
	end

	// loader: kind 0 only (the program and its data)
	if (ld_wr && !ld_pend && ld_kind == 2'd0) begin
		ld_pend <= 1'b1;
		ld_d <= ld_data;
		ld_a <= ld_addr[21:0];
	end
	if (own == O_LD && sr_ack) ld_pend <= 1'b0;
end

// ------------------------------------------------------------------ CPU bus answers

always @(*) begin
	i_ack = (own == O_I) & sr_ack;
	i_dat_i = sr_dat;
	d_ack = ((own == O_D) & sr_ack) | io_ack;
	d_dat_i = d_io ? io_dat : sr_dat;
end

endmodule
