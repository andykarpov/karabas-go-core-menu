// sram_clear against a model of the SRAM (iverilog): a memory of random bytes, written on the rising edge of WE;
// every address is zero after done, WE is low 2 clocks, the address and the data are stable a clock before the
// falling and a clock after the rising WE, RD stays high, MD is let go after done, a reset in the middle starts again.
`timescale 1ns / 1ps
module tb_sram_clear;

localparam AB = 12;              // 4096 words: the logic of 21 bits, faster
localparam N  = 1 << AB;

reg clk = 0, reset = 1;
always #12.22 clk = ~clk;        // 40.91 MHz

wire [AB-1:0] addr;
wire [15:0]   data;
wire          data_oe;
wire [1:0]    we_n, rd_n;
wire          done;

sram_clear #(.ADDR_BITS(AB)) dut (
	.clk(clk), .reset(reset), .addr(addr), .data(data), .data_oe(data_oe),
	.we_n(we_n), .rd_n(rd_n), .done(done)
);

wire [15:0] md = data_oe ? data : 16'hzzzz;

reg [15:0] mem [0:N-1];
integer errors = 0, i, writes = 0, cycles = 0;

// the SRAM: a write ends on the rising WE of each byte lane
always @(posedge we_n[0]) if (^addr !== 1'bx) mem[addr][7:0]  <= md[7:0];
always @(posedge we_n[1]) if (^addr !== 1'bx) mem[addr][15:8] <= md[15:8];

// the checks per clock
reg [AB-1:0] a_prev;
reg [1:0]    we_prev;
integer low = 0;
always @(posedge clk) if (!reset) begin
	cycles = cycles + 1;
	if (rd_n !== 2'b11) begin errors = errors + 1; $display("ERROR: rd_n %b", rd_n); end
	if (we_n[0] !== we_n[1]) begin errors = errors + 1; $display("ERROR: we_n %b", we_n); end
	if (we_n !== 2'b11 && (md !== 16'h0000 || !data_oe)) begin
		errors = errors + 1; $display("ERROR: write without data %h at %h", md, addr);
	end
	if (we_n === 2'b00) begin
		low = low + 1;
		if (addr !== a_prev) begin errors = errors + 1; $display("ERROR: address moves under WE at %h", addr); end
	end else begin
		if (we_prev === 2'b00) begin
			writes = writes + 1;
			if (low != 2) begin errors = errors + 1; $display("ERROR: WE low %0d clocks at %h", low, addr); end
		end
		low = 0;
	end
	if (we_prev === 2'b11 && we_n === 2'b00 && addr !== a_prev) begin
		errors = errors + 1; $display("ERROR: no address setup at %h", addr);
	end
	if (we_prev === 2'b00 && we_n === 2'b11 && addr !== a_prev) begin
		errors = errors + 1; $display("ERROR: no address hold at %h", addr);
	end
	a_prev  = addr;
	we_prev = we_n;
end

task fill_random;
	for (i = 0; i < N; i = i + 1) mem[i] = $random;
endtask

task check_zero;
	for (i = 0; i < N; i = i + 1)
		if (mem[i] !== 16'h0000) begin
			errors = errors + 1;
			if (errors < 10) $display("ERROR: mem[%h] = %h", i, mem[i]);
		end
endtask

initial begin
	fill_random;
	we_prev = 2'b11;
	repeat (5) @(posedge clk);
	reset = 0;
	// a reset in the middle: the fill starts again from 0
	repeat (N) @(posedge clk);
	reset = 1;
	repeat (3) @(posedge clk);
	if (data_oe !== 1'b0 || we_n !== 2'b11 || done !== 1'b0) begin errors = errors + 1; $display("ERROR: reset"); end
	fill_random;
	writes = 0; cycles = 0;
	reset = 0;
	wait (done === 1'b1);
	repeat (20) @(posedge clk);
	if (writes != N) begin errors = errors + 1; $display("ERROR: %0d writes, %0d expected", writes, N); end
	if (cycles > 4 * N + 25) begin errors = errors + 1; $display("ERROR: %0d clocks", cycles); end
	if (data_oe !== 1'b0 || we_n !== 2'b11) begin errors = errors + 1; $display("ERROR: bus not released"); end
	check_zero;
	// done holds: nothing is written any more
	writes = 0;
	mem[5] = 16'h1234;
	repeat (100) @(posedge clk);
	if (writes != 0 || mem[5] !== 16'h1234) begin errors = errors + 1; $display("ERROR: writes after done"); end
	$display("tb_sram_clear: %0d errors (%0d writes, %0d clocks)", errors, N, cycles);
	$finish;
end

endmodule
