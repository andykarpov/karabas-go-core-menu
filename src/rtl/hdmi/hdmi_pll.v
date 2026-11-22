`timescale 1ns / 1ps
`default_nettype none

/*
 hdmi_pll.v

 The 5x serial clocks of HDMI (Mini / Mini G) from the pixel clock of the menu: clk_pix is the BUFGMUX of
 the top (25 MHz in 640 x 480, 40 MHz in 800 x 600, both PLL outputs); DCM_SP makes 5x of it (125 / 200
 MHz, CLKFX and CLKFX180). When the mode changes the DCM is reset (pll_reset: 8 clocks of clk_ref) and
 locks again to the new frequency; hdmi_top holds hdmi_tx in reset until then.

 It made the pixel clock of the FT812 video too (DCM_CLKGEN programmed from the measured FT_CLK, a
 BUFGMUX with clk_sys): the boot core has no FT812 video any more (the firmware does not start it since
 Ф4 of docs/PLAN.md), the second input of the BUFGMUX is the 25 MHz of 640 x 480 now.
*/

module hdmi_pll(
	input wire clk_pix,  // the pixel clock (the BUFGMUX of the top)
	input wire clk_ref,  // clk_sys, 40 MHz: the reset logic
	input wire mode,     // clk_ref: the select of the BUFGMUX (a change resets the DCM)
	input wire reset,
	output wire clk_hdmi, // x5
	output wire clk_hdmi_n, // x5 180deg
	output wire locked,
	output wire o_reset
);

reg mode_d = 1'b0;
reg mode_rst = 1'b0;
always @(posedge clk_ref) begin
	mode_d <= mode;
	mode_rst <= mode_d != mode;
end

wire pll_rst;
pll_reset pll_reset(
	.clk(clk_ref),
	.i_reset(mode_rst || reset),
	.o_reset(pll_rst)
);
assign o_reset = pll_rst;

wire clk0, clkfx, clkfx180, clkfbout, lockedx5;
DCM_SP
  #(.CLKDV_DIVIDE          (2.000),
    .CLKFX_DIVIDE          (1),
    .CLKFX_MULTIPLY        (5),
    .CLKIN_DIVIDE_BY_2     ("FALSE"),
    .CLKIN_PERIOD          (25.000),
    .CLKOUT_PHASE_SHIFT    ("NONE"),
    .CLK_FEEDBACK          ("1X"),
    .DESKEW_ADJUST         ("SYSTEM_SYNCHRONOUS"),
    .PHASE_SHIFT           (0),
    .STARTUP_WAIT          ("FALSE"))
  dcm_sp_inst
   (.CLKIN                 (clk_pix),
    .CLKFB                 (clkfbout),
    .CLK0                  (clk0),
    .CLK90                 (),
    .CLK180                (),
    .CLK270                (),
    .CLK2X                 (),
    .CLK2X180              (),
    .CLKFX                 (clkfx),
    .CLKFX180              (clkfx180),
    .CLKDV                 (),
    .PSCLK                 (1'b0),
    .PSEN                  (1'b0),
    .PSINCDEC              (1'b0),
    .PSDONE                (),
    .LOCKED                (lockedx5),
    .STATUS                (),
    .RST                   (pll_rst),
    .DSSEN                 (1'b0));

BUFG clkf_buf(.O(clkfbout), .I(clk0));
BUFG clkout1_buf(.O(clk_hdmi), .I(clkfx));
BUFG clkout2_buf(.O(clk_hdmi_n), .I(clkfx180));
assign locked = lockedx5;

endmodule
