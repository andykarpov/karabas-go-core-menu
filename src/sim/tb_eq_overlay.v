`timescale 1ns / 1ps

// kg_osd_overlay with the parameters of the tops against the overlay.vhd the core had (sim/ref/overlay_ref.vhd,
// a netlist of ghdl --synth), iverilog. The sync of the tops (vga_sync, 800 x 600 at 40 MHz, a line of 1056
// clocks: counter steps of 2 clocks), the font and the character memory loaded by commands with random contents
// (attributes with flashing and black ink / paper), random video under the OSD; the frames: the OSD of the menu
// (DEFAULT / ALWAYS_ON) with overlay off, overlay on, popup on; then the flash counters of both held at 20 (flash
// on; the frames to bit 5 would take long) for two frames. rgb_o is compared before every rising clock (the old one changes on both edges; HDMI and the VGA
// DAC take it with the rising edge).
// Not compared, counted: the last two clocks of each character cell. overlay.vhd changed the attribute one clock
// before the pixels of the next cell (its counters on the falling clock), kg_osd_overlay one counter step (two
// clocks) before them (as overlay.vhd of nanomig); the pixels are in the same places (H_OFFSET 171 / V_OFFSET 60
// of the tops for 172 / 60 of overlay.vhd). In the popup (32 clocks a cell, the boot menu does not show it) the
// first clock of a cell and the edges of the window too: there overlay.vhd showed its paper one clock early and
// a half cell before the first one.
// Ends with "tb_eq_overlay: N errors".

module tb_eq_overlay;

reg clk = 1'b0;
always #12.5 clk = ~clk;

integer errors = 0;

// ---------------- sync ----------------

// vga_sync of the tops
wire hs, vs, de;
vga_sync u_sync(.clk(clk), .hs(hs), .vs(vs), .de(de));
wire [11:0] hpos = u_sync.hcnt;

// ---------------- the two ----------------

reg [23:0] rgb = 24'd0;
reg [15:0] cmd = 16'd0;
wire [23:0] old_rgb, new_rgb;

overlay_ref old_dut(.CLK(clk), .RGB_I(rgb), .RGB_O(old_rgb), .HSYNC_I(hs), .VSYNC_I(vs), .OSD_COMMAND(cmd));
kg_osd_overlay #(.H_OFFSET(171), .V_OFFSET(60), .H_DIV4_WIDTH(1200), .ALWAYS_ON(1)) new_dut(
	.clk(clk), .rgb_i(rgb), .rgb_o(new_rgb), .hsync_i(hs), .vsync_i(vs), .osd_command(cmd)
);

// ---------------- compare ----------------

integer seed = 23;
integer compared = 0, osd_clocks = 0, edge_diffs = 0, edge_clocks = 0;
reg check = 1'b0;
wire popup = new_dut.osd_popup;
wire [4:0] phase = hpos - 12'd144; // the cell grid of the pixels: 16 clocks from 144, popup 32 from 160
wire edge_zone = popup ? (phase == 5'd14 || phase == 5'd15 || phase == 5'd16 || hpos == 12'd143 ||
		(hpos >= 12'd144 && hpos < 12'd160) || hpos == 12'd655) :
	(phase[3:0] == 4'd14 || phase[3:0] == 4'd15);
wire [23:0] dim = { 3'b000, rgb[23:19], 3'b000, rgb[15:11], 3'b000, rgb[7:3] };

always @(negedge clk) begin
	#11; // before the rising edge
	if (check) begin
		if (edge_zone) begin
			edge_clocks = edge_clocks + 1;
			if (old_rgb !== new_rgb) edge_diffs = edge_diffs + 1;
		end
		else begin
			compared = compared + 1;
			if (old_rgb !== new_rgb) begin
				if (errors < 20) $display("%t ERROR: rgb %h, overlay.vhd %h (x %0d, popup %b)", $time,
					new_rgb, old_rgb, hpos, popup);
				errors = errors + 1;
			end
			if (new_rgb !== dim) osd_clocks = osd_clocks + 1;
		end
	end
end

// random video under the OSD, changed at the falling clock (as the commands)
always @(negedge clk) rgb <= $random(seed);

// ---------------- commands ----------------

task command(input [15:0] v);
	begin
		@(negedge clk);
		cmd = v;
		repeat (3) @(negedge clk);
	end
endtask

reg [7:0] r;
task put_cell(input [9:0] a);
	begin
		command({ 8'h10, 3'b000, a[4:0] });
		command({ 8'h11, 3'b000, a[9:5] });
		r = $random(seed);
		command({ 8'h12, r });
		r = $random(seed);
		case (r[2:0]) // some flashing, some with black ink / paper
			3'd0: r[3:0] = 4'b0001;
			3'd1: r[7:5] = 3'b000;
			3'd2: r[3:1] = 3'b000;
			default: ;
		endcase
		command({ 8'h13, r });
	end
endtask

task wait_frame_start;
	begin
		@(negedge clk);
		while (!(u_sync.vcnt == 12'd0 && hpos == 12'd0)) @(negedge clk);
	end
endtask

task frames(input integer n);
	integer k;
	begin
		for (k = 0; k < n; k = k + 1) begin
			wait_frame_start;
			@(negedge clk);
		end
	end
endtask

integer i;
initial begin
	// font and characters, all 32 x 32 cells (the rows below 26 are not shown)
	command(16'h2001);
	for (i = 0; i < 2048; i = i + 1) begin
		r = $random(seed);
		command({ 8'h21, r });
	end
	for (i = 0; i < 1024; i = i + 1) put_cell(i);
	// the OSD of the menu (overlay off), compared from a frame start on: the size of the lines and frames
	// is measured by then
	wait_frame_start;
	check = 1'b1;
	frames(2);
	// overlay on, then popup on, switched and written in the blanking (a switch in the picture differs for a
	// cell, see the modules tb_eq_overlay)
	command(16'h0101);
	frames(1);
	for (i = 0; i < 16; i = i + 1) put_cell($random(seed));
	frames(1);
	command(16'h0201);
	frames(2);
	command(16'h0200);
	frames(1);
	// flash on: both counters held (the netlist of overlay.vhd has flash_cnt as a reg, the copy of its register)
	check = 1'b0;
	wait_frame_start;
	force old_dut.flash_cnt = 8'h20;
	force new_dut.flash_cnt = 8'h20;
	check = 1'b1;
	frames(1);
	command(16'h0100);
	frames(1);
	check = 1'b0;
	release old_dut.flash_cnt;
	release new_dut.flash_cnt;

	if (osd_clocks < compared / 8) begin
		$display("ERROR: %0d of %0d clocks with OSD pixels, the test does not test", osd_clocks, compared);
		errors = errors + 1;
	end
	$display("tb_eq_overlay: %0d clocks compared (%0d with OSD pixels); last clocks of the cells: %0d, %0d differ",
		compared, osd_clocks, edge_clocks, edge_diffs);
	$display("tb_eq_overlay: %0d errors", errors);
	$finish;
end

endmodule
