`timescale 1ns / 1ps
`default_nettype none
/*
 menu_midi_mux.v

 The MIDI out of the menu (docs/PLAN.md Ф2): the bytes of the menu CPU (its sounds on the SAM2695,
 MMIO MIDI) and the thru of a USB MIDI keyboard (PORT 2 of kg_mcu2) into one stream for kg_midi_tx.
 The streams are switched at whole messages only: the keyboard is held (kb_ready low) while a
 message of the CPU goes out, and its running status is sent again before its next data byte. The
 CPU writes whole messages with their status bytes (no running status of its own). Real time bytes
 of the keyboard (F8..FF) pass at any time. One clock (clk).
*/
module menu_midi_mux (
	input wire clk,

	input wire [7:0] kb_data,         // the keyboard (PORT 2 of kg_mcu2)
	input wire kb_wr,
	output wire kb_ready,

	input wire [7:0] cpu_data,        // the CPU (a write of the MMIO register)
	input wire cpu_wr,
	output wire cpu_full,

	output reg [7:0] tx_data = 8'd0,  // to kg_midi_tx
	output reg tx_wr = 1'b0
);

// the data bytes of a message after its status byte (0xF0: a system exclusive, until 0xF7)
function [1:0] data_len(input [7:0] st);
	casez (st)
		8'b10??_????, 8'b1110_????: data_len = 2'd2;   // 8x 9x Ax Bx, Ex
		8'b110?_????: data_len = 2'd1;                 // Cx Dx
		8'hF1, 8'hF3: data_len = 2'd1;
		8'hF2: data_len = 2'd2;
		default: data_len = 2'd0;
	endcase
endfunction

// the CPU bytes: a fifo
wire c_empty;
wire [7:0] c_q;
reg c_rd = 1'b0;
kg_fifo #(.DW(8), .AW(4)) u_fifo (
	.clk(clk), .reset(1'b0),
	.wr(cpu_wr), .din(cpu_data), .full(cpu_full),
	.rd(c_rd), .dout(c_q), .empty(c_empty), .count()
);

// the keyboard stream: its running status, the data bytes left in its message, a sysex
reg [7:0] kb_status = 8'd0;
reg [1:0] kb_left = 2'd0;
reg kb_sysex = 1'b0;
wire kb_between = kb_left == 2'd0 && !kb_sysex;
reg resend = 1'b0;                     // send kb_status again before the next data byte
reg hold_v = 1'b0;                     // a keyboard byte held for the clock after the status
reg [7:0] hold = 8'd0;

// the CPU stream
reg cpu_mode = 1'b0;
reg [1:0] c_left = 2'd0;
reg c_sysex = 1'b0;
reg c_started = 1'b0;                  // a byte of the message went out
reg c_pop = 1'b0;                      // c_q is valid (the clock after c_rd)

assign kb_ready = !cpu_mode && !hold_v;

always @(posedge clk) begin
	tx_wr <= 1'b0;
	c_rd <= 1'b0;
	c_pop <= c_rd;

	if (hold_v) begin
		tx_data <= hold;
		tx_wr <= 1'b1;
		hold_v <= 1'b0;
	end else if (kb_wr && kb_ready) begin
		if (kb_data >= 8'hF8) begin
			tx_data <= kb_data;            // real time: anywhere
			tx_wr <= 1'b1;
		end else begin
			if (kb_data[7]) begin
				resend <= 1'b0;
				if (kb_data == 8'hF7) kb_sysex <= 1'b0;
				else if (kb_data == 8'hF0) kb_sysex <= 1'b1;
				else begin
					kb_sysex <= 1'b0;
					kb_left <= data_len(kb_data);
					if (kb_data < 8'hF0) kb_status <= kb_data;  // channel messages keep running status
				end
				tx_data <= kb_data;
				tx_wr <= 1'b1;
			end else begin
				// a data byte: the next of the message, or a new one in running status
				if (kb_left != 2'd0) kb_left <= kb_left - 2'd1;
				else if (!kb_sysex && kb_status[7]) kb_left <= data_len(kb_status) - 2'd1;
				if (resend && kb_left == 2'd0 && !kb_sysex && kb_status[7]) begin
					resend <= 1'b0;
					tx_data <= kb_status;
					tx_wr <= 1'b1;
					hold <= kb_data;
					hold_v <= 1'b1;
				end else begin
					tx_data <= kb_data;
					tx_wr <= 1'b1;
				end
			end
		end
	end else if (!cpu_mode) begin
		// a message of the CPU waits and the keyboard is between messages
		if (!c_empty && kb_between) begin
			cpu_mode <= 1'b1;
			c_started <= 1'b0;
		end
	end

	// the CPU mode: a byte a clock (a read, then the byte), until its message is complete
	if (cpu_mode && !hold_v) begin
		if (c_pop) begin
			tx_data <= c_q;
			tx_wr <= 1'b1;
			c_started <= 1'b1;
			if (c_q[7]) begin
				c_sysex <= c_q == 8'hF0;
				c_left <= (c_q == 8'hF0 || c_q == 8'hF7) ? 2'd0 : data_len(c_q);
				if (c_q != 8'hF0 && data_len(c_q) == 2'd0) begin
					cpu_mode <= 1'b0;          // a message of one byte
					resend <= 1'b1;
				end
			end else if (c_left > 2'd1 || c_sysex) begin
				if (!c_sysex) c_left <= c_left - 2'd1;
			end else begin
				c_left <= 2'd0;
				cpu_mode <= 1'b0;              // the last data byte
				resend <= 1'b1;
			end
			if (c_q == 8'hF7) begin
				c_sysex <= 1'b0;
				cpu_mode <= 1'b0;
				resend <= 1'b1;
			end
		end else if (!c_rd && !c_empty)
			c_rd <= 1'b1;
	end
end

endmodule
