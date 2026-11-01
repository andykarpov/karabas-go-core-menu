// Fills the SRAM of the board with zeros once after reset, so the next core starts with clean memory: the SRAM keeps
// its content over a core reload and an MCU reboot (only a power cycle clears it). Seen with BaseConf: its device
// manager keeps the found partitions in RAM with a CRC and did not see an HDD image mounted later.
// The two 2M x 8 chips share the address: both are written at once (MD 16 bits, both WE), 2M writes.
// One write is 4 clocks: the address and the data set (WE high), WE low for 2 clocks, WE high. At clk_sys 40.91 MHz
// WE is low 49 ns with 24 ns of address setup and hold around it, a write is 98 ns, the whole SRAM 0.2 s.
// A core chosen before the end just loads: the configuration of the FPGA lets the pins go, the rest stays as it was.
module sram_clear #(
	parameter ADDR_BITS = 21
) (
	input  wire                 clk,
	input  wire                 reset,      // the fill starts when it goes low

	output reg  [ADDR_BITS-1:0] addr,
	output wire [15:0]          data,
	output reg                  data_oe,    // drive MD with data
	output reg  [1:0]           we_n,
	output wire [1:0]           rd_n,
	output reg                  done
);

assign data = 16'h0000;
assign rd_n = 2'b11;

reg [1:0] phase;

always @(posedge clk) begin
	if (reset) begin
		addr    <= 0;
		phase   <= 2'd0;
		data_oe <= 1'b0;
		we_n    <= 2'b11;
		done    <= 1'b0;
	end else if (!done) begin
		data_oe <= 1'b1;
		phase   <= phase + 2'd1;
		case (phase)
			2'd0: we_n <= 2'b00;      // the address and the data are set for a clock
			2'd1: we_n <= 2'b00;
			2'd2: we_n <= 2'b11;      // the write ends, the address and the data are held a clock
			2'd3: begin
				if (&addr) begin
					done    <= 1'b1;
					data_oe <= 1'b0;
					addr    <= 0;
				end else
					addr <= addr + 1'd1;
			end
		endcase
	end
end

endmodule
