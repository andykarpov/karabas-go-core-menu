`timescale 1ns / 1ps
`default_nettype none
/*
 menu_sram.v

 The SRAM of the board (two 2M x 8 chips = one 2M x 16, 10 ns, asynchronous; MD[7:0] / MWR_N[0] /
 MRD_N[0] the low byte) at the CPU clock (clk, 75 MHz). After doom_sram.v of karabas-go-core-doom
 (the DMA dropped, the clear added). Three users:

 - clear: clr_req (a pulse) fills the whole SRAM with zeros, a halfword in three clocks (2M halfwords,
   84 ms at 75 MHz); clr_busy until done. Every core must start with clean memory (sram_clear of the
   old menu): the top asks it after the PLL lock, the menu again before it asks for a core (the code and
   the frame buffers live here). Nothing else is served meanwhile.
 - CPU, a wishbone slave as the VexRiscv buses use it: a line fill (CTI 010, 8 words from the first
   word of the line) or a single read, each word acked when its two halfwords are in; a write of one
   word with the byte selects (a halfword per write, the lanes of a halfword in one /WE), acked after it.
 - sound: aud_req (held until aud_ack) reads the halfword at aud_adr (menu_sound: a sample at most
   every 1562 clocks a channel), before the CPU.
 - video: vid_req (a pulse) asks vid_len halfwords from halfword address vid_adr; they come out as
   vid_we / vid_wa (0..vid_len-1) / vid_wd. Served between CPU transactions, VID_CHUNK halfwords at a
   time, the video first after a CPU transaction; vid_busy until done.

 Read: a halfword in two clocks (26.7 ns): MA from an IOB flip-flop, held two clocks, MD into an IOB
 flip-flop at the end of the second (TS_SRAM_OUT / TS_SRAM_IN of the ucf). Write: a halfword in three
 clocks: MA and MD out with /OE off, /WE of the lanes low, /WE high (address and data held a clock
 around /WE).
*/
module menu_sram #(
	parameter VID_CHUNK = 6'd32,
	parameter [20:0] CLR_LAST = 21'h1FFFFF   // the last halfword of the clear (the testbench makes it small)
) (
	input wire clk,

	input wire clr_req,
	output wire clr_busy,

	// CPU (byte address of the 4 MB)
	input wire wb_cyc,
	input wire wb_stb,
	input wire wb_we,
	input wire [3:0] wb_sel,
	input wire [21:2] wb_adr,
	input wire [2:0] wb_cti,
	input wire [31:0] wb_dat_i,
	output reg [31:0] wb_dat_o = 32'd0,
	output reg wb_ack = 1'b0,

	// sound
	input wire aud_req,
	input wire [20:0] aud_adr,
	output reg aud_ack = 1'b0,
	output reg [15:0] aud_dat = 16'd0,

	// video
	input wire vid_req,
	input wire [20:0] vid_adr,
	input wire [8:0] vid_len,
	output reg vid_we = 1'b0,
	output reg [8:0] vid_wa = 9'd0,
	output reg [15:0] vid_wd = 16'd0,
	output wire vid_busy,

	// pins (the top makes the tristate of MD)
	output wire [20:0] MA,
	input wire [15:0] MD_I,
	output wire [15:0] MD_O,
	output wire [15:0] MD_OE_N,
	output wire [1:0] MWR_N,
	output wire [1:0] MRD_N
);

(* IOB = "TRUE" *) reg [20:0] ma_r = 21'd0;
(* IOB = "TRUE" *) reg [1:0] mwr_r = 2'b11;
(* IOB = "TRUE" *) reg [1:0] mrd_r = 2'b11;
(* IOB = "TRUE" *) reg [15:0] md_out = 16'd0;
(* IOB = "TRUE", equivalent_register_removal = "no" *) reg [15:0] md_oe_n = 16'hFFFF;
(* IOB = "TRUE" *) reg [15:0] md_in = 16'd0;

assign MA = ma_r;
assign MWR_N = mwr_r;
assign MRD_N = mrd_r;
assign MD_O = md_out;
assign MD_OE_N = md_oe_n;

localparam [3:0] S_IDLE = 4'd0, S_RD = 4'd1, S_W1 = 4'd2, S_W2 = 4'd3, S_W3 = 4'd4, S_VID = 4'd5,
	S_C1 = 4'd6, S_C2 = 4'd7, S_C3 = 4'd8, S_A1 = 4'd9, S_A2 = 4'd10, S_A3 = 4'd11;

reg [3:0] st = S_IDLE;
wire wb_req = wb_cyc & wb_stb & ~wb_ack;
reg [21:2] q_adr = 20'd0;
reg [31:0] q_dat = 32'd0;
reg [3:0] q_sel = 4'd0;
reg q_hi = 1'b0;                  // write: the halfword being written
reg [4:0] rd_n = 5'd0;            // read: halfwords done
reg [4:0] rd_last = 5'd0;         // read: halfwords of the request (2 or 16)
reg ph = 1'b0;                    // read: the second clock of a halfword
reg [15:0] lo_half = 16'd0;

reg vid_pend = 1'b0;
reg [20:0] v_adr = 21'd0;
reg [8:0] v_left = 9'd0;
reg [8:0] v_n = 9'd0;
reg [5:0] v_run = 6'd0;
reg v_ph = 1'b0;
reg cpu_last = 1'b0;              // the last transaction was the CPU's: video next if pending
assign vid_busy = vid_pend;

reg clr_pend = 1'b0;
assign clr_busy = clr_pend;

always @(posedge clk) begin
	wb_ack <= 1'b0;
	vid_we <= 1'b0;
	aud_ack <= 1'b0;
	md_in <= MD_I;
	if (clr_req) clr_pend <= 1'b1;
	if (vid_req && !vid_pend && vid_len != 9'd0) begin
		vid_pend <= 1'b1;
		v_adr <= vid_adr;
		v_left <= vid_len;
		v_n <= 9'd0;
	end

	case (st)
	S_IDLE: begin
		md_oe_n <= 16'hFFFF;
		mrd_r <= 2'b00;
		if (clr_pend || clr_req) begin
			ma_r <= 21'd0;
			md_out <= 16'd0;
			md_oe_n <= 16'h0000;
			mrd_r <= 2'b11;
			st <= S_C1;
		end else if (vid_pend && (cpu_last || !wb_req)) begin
			cpu_last <= 1'b0;
			ma_r <= v_adr;
			v_ph <= 1'b0;
			v_run <= VID_CHUNK;
			st <= S_VID;
		end else if (aud_req && !aud_ack) begin
			ma_r <= aud_adr;
			st <= S_A1;
		end else if (wb_req) begin
			cpu_last <= 1'b1;
			q_adr <= wb_adr;
			q_dat <= wb_dat_i;
			q_sel <= wb_sel;
			if (wb_we) begin
				if (wb_sel[1:0] != 2'b00) begin
					q_hi <= 1'b0;
					ma_r <= { wb_adr[21:2], 1'b0 };
					md_out <= wb_dat_i[15:0];
				end else begin
					q_hi <= 1'b1;
					ma_r <= { wb_adr[21:2], 1'b1 };
					md_out <= wb_dat_i[31:16];
				end
				md_oe_n <= 16'h0000;
				mrd_r <= 2'b11;
				st <= S_W1;
			end else begin
				ma_r <= { wb_adr[21:2], 1'b0 };
				rd_n <= 5'd0;
				ph <= 1'b0;
				rd_last <= (wb_cti == 3'b010) ? 5'd16 : 5'd2;
				st <= S_RD;
			end
		end
	end

	// a halfword every two clocks: the address of halfword rd_n is out (ph 0, 1); md_in takes it at
	// the end of ph 1 and is used a clock later (ph 0 of the next one, or here in S_IDLE's clock)
	S_RD: begin
		ph <= ~ph;
		if (ph) begin
			rd_n <= rd_n + 5'd1;
			if (rd_n != rd_last - 5'd1)
				ma_r <= { q_adr[21:5], q_adr[4:2] + rd_n[3:1] + { 2'b00, rd_n[0] }, ~rd_n[0] };
		end
		// md_in now holds halfword rd_n - 1 (taken at the end of its ph 1)
		if (!ph && rd_n != 5'd0) begin
			if (rd_n[0]) lo_half <= md_in;
			else begin
				wb_dat_o <= { md_in, lo_half };
				wb_ack <= 1'b1;
			end
			if (rd_n == rd_last) st <= S_IDLE;
		end
	end

	S_W1: begin
		mwr_r <= q_hi ? ~q_sel[3:2] : ~q_sel[1:0];
		st <= S_W2;
	end
	S_W2: begin
		mwr_r <= 2'b11;
		st <= S_W3;
	end
	S_W3: begin
		// the address and data held a clock after /WE; then the high half if it has lanes
		if (!q_hi && q_sel[3:2] != 2'b00) begin
			q_hi <= 1'b1;
			ma_r <= { q_adr[21:2], 1'b1 };
			md_out <= q_dat[31:16];
			st <= S_W1;
		end else begin
			wb_ack <= 1'b1;
			md_oe_n <= 16'hFFFF;
			mrd_r <= 2'b00;
			st <= S_IDLE;
		end
	end

	// video: a halfword every two clocks, as S_RD
	S_VID: begin
		v_ph <= ~v_ph;
		if (v_ph) begin
			v_adr <= v_adr + 21'd1;
			v_left <= v_left - 9'd1;
			v_run <= v_run - 6'd1;
			ma_r <= v_adr + 21'd1;
		end
		if (!v_ph && v_run != VID_CHUNK) begin
			vid_we <= 1'b1;
			vid_wa <= v_n;
			vid_wd <= md_in;
			v_n <= v_n + 9'd1;
			if (v_left == 9'd0) vid_pend <= 1'b0;
			if (v_left == 9'd0 || v_run == 6'd0) st <= S_IDLE;
		end
	end

	// sound: the address out two clocks (as a read halfword), md_in taken at the end of the second
	S_A1: st <= S_A2;
	S_A2: st <= S_A3;
	S_A3: begin
		aud_dat <= md_in;
		aud_ack <= 1'b1;
		st <= S_IDLE;
	end

	// clear: both chips, a halfword of zeros in three clocks (the address and the data set with /WE
	// high, /WE low a clock, /WE high a clock while the address and the data stay)
	S_C1: begin
		mwr_r <= 2'b00;
		st <= S_C2;
	end
	S_C2: begin
		mwr_r <= 2'b11;
		st <= S_C3;
	end
	S_C3: begin
		if (ma_r == CLR_LAST) begin
			clr_pend <= 1'b0;
			md_oe_n <= 16'hFFFF;
			mrd_r <= 2'b00;
			st <= S_IDLE;
		end else begin
			ma_r <= ma_r + 21'd1;
			st <= S_C1;
		end
	end
	default: st <= S_IDLE;
	endcase
end

endmodule
