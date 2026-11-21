`timescale 1ns / 1ps
// tb_midi_mux: menu_midi_mux with a keyboard in running status and messages of the CPU between its
// notes. Checked: the output stream (the CPU message only between keyboard messages, the running
// status sent again before the next keyboard data byte, a real time byte at any time, a CPU message
// written while a keyboard message is half out waits for its end). Prints "tb_midi_mux: 0 errors".
module tb_midi_mux;
reg clk = 0;
always #6.67 clk = ~clk;
reg [7:0] kb_data = 0, cpu_data = 0;
reg kb_wr = 0, cpu_wr = 0;
wire kb_ready, cpu_full, tx_wr;
wire [7:0] tx_data;
menu_midi_mux dut (.clk(clk), .kb_data(kb_data), .kb_wr(kb_wr), .kb_ready(kb_ready),
	.cpu_data(cpu_data), .cpu_wr(cpu_wr), .cpu_full(cpu_full), .tx_data(tx_data), .tx_wr(tx_wr));

reg [7:0] out [0:63];
integer n_out = 0, errors = 0, i;
always @(posedge clk) if (tx_wr) begin out[n_out] = tx_data; n_out = n_out + 1; end

// a keyboard byte, as kg_l2_port gives it: presented with wr while ready
task kb(input [7:0] b);
	begin
		@(negedge clk); kb_data = b;
		while (!kb_ready) @(negedge clk);
		kb_wr = 1; @(negedge clk); kb_wr = 0;
		repeat (3) @(negedge clk);
	end
endtask
task cpu(input [7:0] b);
	begin @(negedge clk); cpu_data = b; cpu_wr = 1; @(negedge clk); cpu_wr = 0; end
endtask

reg [7:0] want [0:63];
integer n_want;
initial begin
	// keyboard: note on, then a second note in running status
	kb(8'h90); kb(8'h3C); kb(8'h40); kb(8'h3E); kb(8'h40);
	// the CPU: a click (two messages)
	cpu(8'h99); cpu(8'h38); cpu(8'h64); cpu(8'h89); cpu(8'h38); cpu(8'h00);
	repeat (40) @(negedge clk);
	// keyboard goes on in running status: the status must come again
	kb(8'h40); kb(8'h40);
	// a CPU message written while a keyboard message is half out: after it
	kb(8'h91);
	cpu(8'hC9); cpu(8'h05);
	kb(8'h30); kb(8'h20);
	repeat (40) @(negedge clk);
	// real time from the keyboard in running status, then a data pair
	kb(8'hF8); kb(8'h31); kb(8'h21);
	repeat (20) @(negedge clk);

	n_want = 0;
	want[n_want] = 8'h90; n_want = n_want + 1; want[n_want] = 8'h3C; n_want = n_want + 1; want[n_want] = 8'h40; n_want = n_want + 1;
	want[n_want] = 8'h3E; n_want = n_want + 1; want[n_want] = 8'h40; n_want = n_want + 1;
	want[n_want] = 8'h99; n_want = n_want + 1; want[n_want] = 8'h38; n_want = n_want + 1; want[n_want] = 8'h64; n_want = n_want + 1;
	want[n_want] = 8'h89; n_want = n_want + 1; want[n_want] = 8'h38; n_want = n_want + 1; want[n_want] = 8'h00; n_want = n_want + 1;
	want[n_want] = 8'h90; n_want = n_want + 1; want[n_want] = 8'h40; n_want = n_want + 1; want[n_want] = 8'h40; n_want = n_want + 1;
	want[n_want] = 8'h91; n_want = n_want + 1; want[n_want] = 8'h30; n_want = n_want + 1; want[n_want] = 8'h20; n_want = n_want + 1;
	want[n_want] = 8'hC9; n_want = n_want + 1; want[n_want] = 8'h05; n_want = n_want + 1;
	want[n_want] = 8'hF8; n_want = n_want + 1;
	want[n_want] = 8'h91; n_want = n_want + 1; want[n_want] = 8'h31; n_want = n_want + 1; want[n_want] = 8'h21; n_want = n_want + 1;
	if (n_out != n_want) begin $display("ERROR %0d bytes out, want %0d", n_out, n_want); errors = errors + 1; end
	for (i = 0; i < n_want && i < n_out; i = i + 1)
		if (out[i] !== want[i]) begin $display("ERROR byte %0d: %h, want %h", i, out[i], want[i]); errors = errors + 1; end
	$display("tb_midi_mux: %0d errors", errors);
	$finish;
end
endmodule
