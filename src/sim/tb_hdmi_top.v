`timescale 1ns / 1ps

// hdmi_top (rtl/hdmi/hdmi_top.v), ISim with the unisim models of the DCM and the clock buffers: the video of the
// menu in its two modes, the pixel clock a BUFGMUX of 25 and 40 MHz as in the tops (the select from a clk_ref
// flip-flop): 640 x 480 (25 MHz), 800 x 600 (40 MHz), and back and forth; hdmi_pll resets its 5x DCM on each
// change. Checked where the video enters hdmi_tx (host_vga_*):
// - every active pixel of a frame in order: the pixel value is its index in the frame (24 bits,
//   so a bit taken from the wrong place shows), a frame has all of them, each line its hsync;
// - the 5x DCM locked again after a switch;
// - the audio samples: audio_l counts the clocks of clk_ref, audio_r is its complement, so a
//   sample must be r = ~l (both halves of one sample) and the step from the sample before must
//   be CLKRATE / SAMPLERATE clocks; the number of samples per phase gives the rate (the same in both modes).
// Ends with "tb_hdmi_top: N errors".

module tb_hdmi_top;

parameter SAMPLERATE = 48000;
localparam integer SAMPLE_DIV = 40000000 / SAMPLERATE;

// frame of the generator (small, so a phase has several frames)
localparam H_TOTAL = 320, H_ACTIVE = 192, HS_START = 220, HS_LEN = 24;
localparam V_TOTAL = 24, V_ACTIVE = 16, VS_START = 18, VS_LEN = 2;
localparam ACTIVE_PIX = H_ACTIVE * V_ACTIVE;

reg clk_ref = 1'b0, clk_25 = 1'b0;
always #12.5 clk_ref = ~clk_ref;     // 40 MHz (600 / 15): clk_sys and the pixel clock of 800 x 600
always #20 clk_25 = ~clk_25;         // 25 MHz (600 / 24)

reg reset = 1'b1;
reg mode = 1'b0;                     // the VMODE of the CPU, in clk_ref as in the tops
reg mode_sys = 1'b0;
always @(posedge clk_ref) mode_sys <= mode;
wire clk_pix;
BUFGMUX bufgmux_pix(.I0(clk_25), .I1(clk_ref), .O(clk_pix), .S(mode_sys));

// ---------------- the video ----------------

// outputs change with the rising pixel clock (the registers of the overlay), hdmi_top takes them at the next one
reg [9:0] vh = 10'd0, vv = 10'd0;
reg [23:0] vpix = 24'd0;
reg [23:0] vga_rgb = 24'd0;
reg vga_hs = 1'b0, vga_vs = 1'b0, vga_de = 1'b0;
always @(posedge clk_pix) begin
	vh <= (vh == H_TOTAL - 1) ? 10'd0 : vh + 10'd1;
	if (vh == H_TOTAL - 1) vv <= (vv == V_TOTAL - 1) ? 10'd0 : vv + 10'd1;
	vga_de <= (vh < H_ACTIVE) && (vv < V_ACTIVE);
	vga_hs <= (vh >= HS_START) && (vh < HS_START + HS_LEN);
	vga_vs <= (vv >= VS_START) && (vv < VS_START + VS_LEN);
	if ((vh < H_ACTIVE) && (vv < V_ACTIVE)) begin
		vga_rgb <= vpix;
		vpix <= vpix + 24'd1;
	end
	else
		vga_rgb <= 24'hA5A5A5;
	if (vh == H_TOTAL - 1 && vv == V_TOTAL - 1) vpix <= 24'd0;
end

// audio: changes every clock of clk_ref
reg [15:0] audio_l = 16'd0;
always @(negedge clk_ref) audio_l <= audio_l + 16'd1;
wire [15:0] audio_r = ~audio_l;

// ---------------- DUT ----------------

wire [3:0] tmds_p, tmds_n;
hdmi_top #(.SAMPLERATE(SAMPLERATE), .CLKRATE(40000000)) dut(
	.clk_pix(clk_pix), .clk_ref(clk_ref), .mode(mode_sys), .reset(reset),
	.vga_rgb(vga_rgb), .vga_hs(vga_hs), .vga_vs(vga_vs), .vga_de(vga_de),
	.audio_en(1'b1), .audio_l(audio_l), .audio_r(audio_r),
	.tmds_p(tmds_p), .tmds_n(tmds_n)
);

// ---------------- checks ----------------

integer errors = 0;
task automatic fail;
	input [8*48-1:0] what;
	input [31:0] got;
	input [31:0] exp;
	begin
		errors = errors + 1;
		if (errors <= 20)
			$display("%t ERROR: %0s: got %h, expected %h", $time, what, got, exp);
	end
endtask

reg checking = 1'b0;       // set by the phases once the clocks are settled
wire pclk = dut.p_clk_int;
wire [23:0] t_rgb = {dut.host_vga_r, dut.host_vga_g, dut.host_vga_b};
wire t_de = ~dut.host_vga_blank;
wire t_hs = dut.host_vga_hs;
wire t_vs = dut.host_vga_vs;

reg in_frame = 1'b0;       // a frame start (vsync) seen since checking began
reg [23:0] exp_pix = 24'd0;
integer lines = 0, frames = 0;
reg prev_hs = 1'b0, prev_vs = 1'b0;
always @(posedge pclk) begin
	prev_hs <= t_hs;
	prev_vs <= t_vs;
	if (!checking) begin
		in_frame <= 1'b0;
	end
	else begin
		if (t_vs && !prev_vs) begin // the end of the active lines and the start of the next frame
			if (in_frame) begin
				if (exp_pix != ACTIVE_PIX) fail("pixels in a frame", exp_pix, ACTIVE_PIX);
				if (lines != V_TOTAL) fail("lines in a frame", lines, V_TOTAL);
				frames = frames + 1;
			end
			in_frame <= 1'b1;
			exp_pix <= 24'd0;
			lines = 0;
		end
		else begin
			if (t_hs && !prev_hs) lines = lines + 1;
			if (in_frame && t_de) begin
				if (t_rgb != exp_pix) fail("pixel", t_rgb, exp_pix);
				exp_pix <= exp_pix + 24'd1;
			end
		end
	end
end

integer samples = 0;
reg have_prev = 1'b0;
reg [15:0] prev_l = 16'd0;
always @(posedge pclk) begin
	if (!checking) begin
		have_prev <= 1'b0;
	end
	else if (dut.audio_clk) begin
		samples = samples + 1;
		if (dut.hdmi_audio_r[23:8] != ~dut.hdmi_audio_l[23:8])
			fail("sample halves (r = ~l)", {dut.hdmi_audio_l[23:8], dut.hdmi_audio_r[23:8]}, {dut.hdmi_audio_l[23:8], ~dut.hdmi_audio_l[23:8]});
		if (dut.hdmi_audio_l[7:0] != 8'd0 || dut.hdmi_audio_r[7:0] != 8'd0)
			fail("sample low bits", {dut.hdmi_audio_l[7:0], dut.hdmi_audio_r[7:0]}, 0);
		if (have_prev && (dut.hdmi_audio_l[23:8] - prev_l) != SAMPLE_DIV[15:0])
			fail("sample step", dut.hdmi_audio_l[23:8] - prev_l, SAMPLE_DIV);
		prev_l <= dut.hdmi_audio_l[23:8];
		have_prev <= 1'b1;
	end
end

// ---------------- phases ----------------

// the pixel clock measured over 10 us of clk_ref
time t_pix0;
integer n_pix = 0;
always @(posedge pclk) n_pix = n_pix + 1;

task automatic phase;
	input [8*16-1:0] name;
	input integer exp_mhz;
	input integer settle_us;
	input integer check_us;
	integer f0, s0, exp_samples, p0, mhz;
	begin
		#(settle_us * 1000);
		@(negedge clk_ref);
		if (dut.lockedx5 !== 1'b1) fail("DCM locked", dut.lockedx5, 1);
		p0 = n_pix;
		#10000;
		mhz = (n_pix - p0) / 10;
		if (mhz != exp_mhz) fail("pixel clock MHz", mhz, exp_mhz);
		f0 = frames; s0 = samples;
		checking = 1'b1;
		#(check_us * 1000);
		@(negedge clk_ref);
		checking = 1'b0;
		exp_samples = check_us * (SAMPLERATE / 1000) / 1000;
		$display("%t %0s: %0d MHz, %0d frames, %0d samples (expected %0d)", $time, name, mhz,
			frames - f0, samples - s0, exp_samples);
		if (frames - f0 < 3) fail("frames checked", frames - f0, 3);
		if (samples - s0 < exp_samples - 2 || samples - s0 > exp_samples + 2) fail("samples", samples - s0, exp_samples);
	end
endtask

initial begin
	#1000;
	@(negedge clk_ref) reset = 1'b0;
	phase("640x480 25 MHz", 25, 3000, 1500);
	@(negedge clk_ref) mode = 1'b1;
	phase("800x600 40 MHz", 40, 1000, 1500);
	@(negedge clk_ref) mode = 1'b0;
	phase("640x480 25 MHz", 25, 1000, 1500);
	@(negedge clk_ref) mode = 1'b1;
	phase("800x600 40 MHz", 40, 1000, 1500);
	$display("tb_hdmi_top: %0d errors", errors);
	$finish;
end

endmodule
