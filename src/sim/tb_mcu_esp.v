`timescale 1ns / 1ps

// The MCU link of the tops (kg_mcu with their parameters and esp8266.vhd, ISim) against the old one (sim/ref:
// mcu.vhd and esp8266.vhd of the core with the fifo.v model), the same frames from one MCU model on the SPI bus:
// - the commands of the menu (INIT_START / DONE, HW_SETUP, OSD, CONTROL of the FT812): OSD_COMMAND, FT_SPI_ON,
//   FT_VGA_ON, FT_RESET, HWID, DVI_ONLY, BUSY of both compared after every frame (an old output still X, without
//   an initial value, is not compared);
// - CMD_ESP_UART bytes of the MCU: the bytes on UART_TX of both (each at its own bit time: the old uart counted 347
//   clocks a bit for 40 MHz, 117.9 kbaud at 40.9 MHz; now 355, measured on a 00 byte);
// - bytes of the ESP on UART_RX: the CMD_ESP_UART words to the MCU, in order; then 700 bytes while the MCU sends no
//   frames (the reply queue holds 512), then read: all of them in order;
// - CMD_CAPS: answered now (bursts, as kg_mcu does in every core), not before.
// Errors count the new link only; what the old one did with the bytes of the ESP is printed ("old link:"): it gave
// each byte one later (the byte read before from its fifo) and lost what did not fit in its reply queue.
// Ends with "tb_mcu_esp: N errors".

module tb_mcu_esp;

localparam real T = 24.444; // clk_sys 40.91 MHz
reg clk = 1'b0;
always #(T / 2) clk = ~clk;

integer errors = 0;
task automatic fail(input [8*80-1:0] what);
	begin
		if (errors < 30) $display("%t ERROR: %0s", $time, what);
		errors = errors + 1;
	end
endtask

reg n_reset = 1'b0;
reg cs_n = 1'b1, sck = 1'b0, mosi = 1'b0;
reg ft_ss = 1'b1;
reg rx_new = 1'b1, rx_old = 1'b1; // UART_RX of each
wire miso_new, miso_old, tx_new, tx_old, cts_new, cts_old;

// ---------------- new: as the tops ----------------

wire [15:0] osd_new;
wire ft_spi_new, ft_vga_new, ft_reset_new, dvi_new, busy_new;
wire [7:0] hwid_new;
wire [7:0] esp_rx_data, esp_tx_data;
wire esp_rx_wr, esp_rx_ack, esp_tx_wr;

kg_mcu #(
	.BURST_MAX_MHZ(0), .CS_FILTER(3),
	.ENABLE_RTC(0), .ENABLE_UART(0), .ENABLE_MATRIX(0), .ENABLE_DEBUG(0), .ENABLE_IMG(0), .ENABLE_FILE(0),
	.ENABLE_FT(1), .ENABLE_SD2(0), .ENABLE_ESP(1)
) new_mcu(
	.CLK(clk), .N_RESET(n_reset),
	.MCU_MOSI(mosi), .MCU_MISO(miso_new), .MCU_SCK(sck), .MCU_SS(cs_n),
	.MCU_SPI_FT_SS(ft_ss), .MCU_SPI_SD2_SS(1'b1), .MCU_BURST_CS_N(1'b1), .MCU_BURST_READY(),
	.RTC_A(8'h00), .RTC_DI(8'h00), .RTC_CS(1'b0), .RTC_WR_N(1'b1),
	.UART_TX_DATA(8'h00), .UART_TX_WR(1'b0), .UART_TX_MODE(1'b0), .UART_DLM(8'h00), .UART_DLL(8'h00),
	.UART_DLM_WR(1'b0), .UART_DLL_WR(1'b0), .MATRIX_CMD(24'h000000), .MATRIX_CMD_WR(1'b0),
	.DEBUG_ADDR(16'h0000), .DEBUG_DATA(16'h0000), .IMG_Q_WR(1'b0), .IMG_Q_DI(24'h000000), .SD2_MISO(1'b1),
	.ESP_UART_TX_DATA(esp_tx_data), .ESP_UART_TX_WR(esp_tx_wr),
	.ESP_UART_RX_DATA(esp_rx_data), .ESP_UART_RX_WR(esp_rx_wr), .ESP_UART_RX_ACK(esp_rx_ack),
	.OSD_COMMAND(osd_new), .FT_SPI_ON(ft_spi_new), .FT_VGA_ON(ft_vga_new), .FT_RESET(ft_reset_new),
	.FT_MISO(1'b1), .FT_MOSI(), .FT_CS_N(), .FT_SCK(),
	.HWID(hwid_new), .DVI_ONLY(dvi_new), .BUSY(busy_new)
);

esp8266 new_esp(
	.CLK(clk), .RESET(~n_reset),
	.ESP_UART_RX_DATA(esp_rx_data), .ESP_UART_RX_WR(esp_rx_wr), .ESP_UART_RX_ACK(esp_rx_ack),
	.ESP_UART_TX_DATA(esp_tx_data), .ESP_UART_TX_WR(esp_tx_wr),
	.UART_RX(rx_new), .UART_TX(tx_new), .UART_CTS(cts_new)
);

// ---------------- old ----------------

wire [15:0] osd_old;
wire ft_spi_old, ft_vga_old, ft_reset_old, dvi_old, busy_old;
wire [7:0] hwid_old;
wire [7:0] oesp_rx_data, oesp_tx_data;
wire oesp_rx_wr, oesp_tx_wr;

mcu_ref old_mcu(
	.CLK(clk), .N_RESET(n_reset),
	.MCU_MOSI(mosi), .MCU_MISO(miso_old), .MCU_SCK(sck), .MCU_SS(cs_n),
	.MCU_SPI_FT_SS(ft_ss), .MCU_SPI_SD2_SS(1'b1),
	.ESP_UART_RX_DATA(oesp_rx_data), .ESP_UART_RX_WR(oesp_rx_wr),
	.ESP_UART_TX_DATA(oesp_tx_data), .ESP_UART_TX_WR(oesp_tx_wr),
	.FT_SPI_ON(ft_spi_old), .FT_VGA_ON(ft_vga_old), .FT_SCK(), .FT_MOSI(), .FT_MISO(1'b1), .FT_CS_N(),
	.FT_RESET(ft_reset_old), .SOFTSW_COMMAND(), .OSD_COMMAND(osd_old), .HWID(hwid_old), .DVI_ONLY(dvi_old),
	.BUSY(busy_old)
);

esp8266_ref old_esp(
	.CLK(clk), .RESET(~n_reset),
	.ESP_UART_RX_DATA(oesp_rx_data), .ESP_UART_RX_WR(oesp_rx_wr),
	.ESP_UART_TX_DATA(oesp_tx_data), .ESP_UART_TX_WR(oesp_tx_wr),
	.UART_RX(rx_old), .UART_TX(tx_old), .UART_CTS(cts_old)
);

// ---------------- mcu model ----------------

reg [7:0] rn, ro; // the bytes read from each in a byte slot
task automatic spi_byte(input [7:0] tx, output [7:0] rxn, output [7:0] rxo);
	integer k;
	begin
		for (k = 7; k >= 0; k = k - 1) begin
			mosi = tx[k];
			#31;
			sck = 1'b1;
			rxn[k] = miso_new;
			rxo[k] = miso_old;
			#31;
			sck = 1'b0;
		end
	end
endtask

reg [23:0] qn [0:2047], qo [0:2047]; // the words to the mcu that are not NOPE
integer qn_n = 0, qo_n = 0;
task automatic frame(input [7:0] c, input [7:0] a, input [7:0] d);
	reg [7:0] cn, co, an, ao, dn, do_;
	begin
		cs_n = 1'b0;
		#100;
		spi_byte(c, cn, co);
		spi_byte(a, an, ao);
		spi_byte(d, dn, do_);
		#50;
		cs_n = 1'b1;
		#500;
		if (cn !== 8'hFF) begin qn[qn_n] = { cn, an, dn }; qn_n = qn_n + 1; end
		if (co !== 8'hFF) begin qo[qo_n] = { co, ao, do_ }; qo_n = qo_n + 1; end
	end
endtask

// the outputs of the decoders, after a frame
task automatic compare_ctl;
	begin
		if (^osd_old !== 1'bx && osd_new !== osd_old) fail("OSD_COMMAND"); // X: no value before the first OSD command
		if ({ ft_spi_new, ft_vga_new, ft_reset_new } !== { ft_spi_old, ft_vga_old, ft_reset_old }) fail("FT control");
		if (hwid_new !== hwid_old || dvi_new !== dvi_old) fail("HWID / DVI_ONLY");
		if (busy_new !== busy_old) fail("BUSY");
	end
endtask

task automatic ctl_frame(input [7:0] c, input [7:0] a, input [7:0] d);
	begin
		frame(c, a, d);
		compare_ctl;
	end
endtask

// ---------------- the ESP side ----------------

localparam integer BIT_NEW = 355, BIT_OLD = 347; // clocks a bit

task automatic uart_send(input integer bit_clocks, input which, input [7:0] b);
	integer k;
	begin
		for (k = 0; k < 10; k = k + 1) begin
			if (which) rx_new = (k == 0) ? 1'b0 : (k == 9) ? 1'b1 : b[k - 1];
			else rx_old = (k == 0) ? 1'b0 : (k == 9) ? 1'b1 : b[k - 1];
			#(bit_clocks * T);
		end
	end
endtask

// receivers of UART_TX
reg [7:0] un [0:63], uo [0:63];
integer un_n = 0, uo_n = 0;
always begin : rx_of_new
	integer k;
	reg [7:0] b;
	@(negedge tx_new);
	#(BIT_NEW * T * 1.5);
	for (k = 0; k < 8; k = k + 1) begin b[k] = tx_new; #(BIT_NEW * T); end
	if (tx_new !== 1'b1) fail("new UART_TX: no stop bit");
	un[un_n % 64] = b; un_n = un_n + 1;
end
// the bit time of UART_TX: a 00 byte is low for 9 bits (start and data)
integer zero_bytes = 0;
realtime t_fall;
always @(negedge tx_new) t_fall = $realtime;
always @(posedge tx_new)
	if ($realtime - t_fall > 8.5 * BIT_NEW * T) begin
		zero_bytes = zero_bytes + 1;
		if ($realtime - t_fall < 0.99 * 9 * BIT_NEW * T || $realtime - t_fall > 1.01 * 9 * BIT_NEW * T) begin
			$display("%t ERROR: UART_TX: 9 bits in %0.0f ns, expected %0.0f (115200 baud)", $time, $realtime - t_fall,
				9 * BIT_NEW * T);
			errors = errors + 1;
		end
	end

always begin : rx_of_old
	integer k;
	reg [7:0] b;
	@(negedge tx_old);
	#(BIT_OLD * T * 1.5);
	for (k = 0; k < 8; k = k + 1) begin b[k] = tx_old; #(BIT_OLD * T); end
	uo[uo_n % 64] = b; uo_n = uo_n + 1;
end

// ---------------- test ----------------

reg [7:0] msg [0:6];
integer i, n0n, n0o, good, old_good;
initial begin
	msg[0] = "H"; msg[1] = "e"; msg[2] = "l"; msg[3] = "l"; msg[4] = "o"; msg[5] = 8'h00; msg[6] = 8'hFF;
	#2000;
	n_reset = 1'b1;
	#2000;
	for (i = 0; i < 8; i = i + 1) frame(8'hFF, 8'h00, 8'h00);
	qn_n = 0; qo_n = 0;
	compare_ctl;

	// ---- the commands of the menu (the order of the firmware's core loader) ----
	ctl_frame(8'hFD, 8'h00, 8'h00);
	ctl_frame(8'hF9, 8'h00, 8'h03);
	ctl_frame(8'hF9, 8'h01, 8'h01);
	ctl_frame(8'h20, 8'h20, 8'h01);
	for (i = 0; i < 16; i = i + 1) ctl_frame(8'h20, 8'h21, i * 17);
	ctl_frame(8'h20, 8'h10, 8'h05);
	ctl_frame(8'h20, 8'h11, 8'h03);
	ctl_frame(8'h20, 8'h12, 8'h41);
	ctl_frame(8'h20, 8'h13, 8'h70);
	ctl_frame(8'h20, 8'h01, 8'h01);
	ctl_frame(8'h09, 8'h00, 8'h01);
	ctl_frame(8'h09, 8'h00, 8'h03);
	ctl_frame(8'h09, 8'h00, 8'h13);
	ctl_frame(8'h09, 8'h00, 8'h00);
	ctl_frame(8'hF9, 8'h01, 8'h00);
	ctl_frame(8'hFE, 8'h00, 8'h00);
	if (osd_new !== 16'h0101 || busy_new !== 1'b0 || hwid_new !== 8'h03) fail("the commands not taken");
	if (qn_n != 0 || qo_n != 0) fail("a word to the mcu without a reason");

	// ---- bytes to the ESP ----
	for (i = 0; i < 7; i = i + 1) frame(8'hF8, 8'h00, msg[i]);
	#(10 * 10 * BIT_NEW * T);
	if (un_n != 7) begin $display("%t ERROR: %0d bytes on UART_TX, expected 7", $time, un_n); errors = errors + 1; end
	for (i = 0; i < 7; i = i + 1) if (un[i] !== msg[i]) fail("a byte on UART_TX");
	if (zero_bytes != 1) fail("the 00 byte not measured on UART_TX");
	good = 0;
	for (i = 0; i < 7; i = i + 1) if (uo_n == 7 && uo[i] === msg[i]) good = good + 1;
	$display("tb_mcu_esp: old link: %0d bytes on UART_TX, %0d as sent", uo_n, good);

	// ---- bytes of the ESP to the MCU ----
	fork
		begin uart_send(BIT_NEW, 1'b1, "O"); uart_send(BIT_NEW, 1'b1, "K"); uart_send(BIT_NEW, 1'b1, 8'h0D); uart_send(BIT_NEW, 1'b1, 8'h0A); end
		begin uart_send(BIT_OLD, 1'b0, "O"); uart_send(BIT_OLD, 1'b0, "K"); uart_send(BIT_OLD, 1'b0, 8'h0D); uart_send(BIT_OLD, 1'b0, 8'h0A); end
	join
	#(2 * 10 * BIT_NEW * T);
	for (i = 0; i < 8; i = i + 1) frame(8'hFF, 8'h00, 8'h00);
	if (qn_n != 4 || qn[0] !== 24'hF8_00_4F || qn[1] !== 24'hF8_00_4B || qn[2] !== 24'hF8_00_0D || qn[3] !== 24'hF8_00_0A) begin
		$display("%t ERROR: bytes of the ESP: %0d words %h %h %h %h", $time, qn_n, qn[0], qn[1], qn[2], qn[3]);
		errors = errors + 1;
	end
	$display("tb_mcu_esp: old link: OK CR LF of the ESP gave %0d words: %h %h %h %h", qo_n, qo[0], qo[1], qo[2], qo[3]);

	// ---- 700 bytes while the mcu reads nothing ----
	n0n = qn_n; n0o = qo_n;
	fork
		for (i = 0; i < 700; i = i + 1) uart_send(BIT_NEW, 1'b1, i[7:0]);
		begin : old_side
			integer j;
			for (j = 0; j < 700; j = j + 1) uart_send(BIT_OLD, 1'b0, j[7:0]);
		end
	join
	#(2 * 10 * BIT_NEW * T);
	if (new_mcu.u_link.u_queue.count != 512) fail("the reply queue is not full");
	for (i = 0; i < 760; i = i + 1) frame(8'hFF, 8'h00, 8'h00);
	good = 0;
	while (n0n + good < qn_n && qn[n0n + good] === { 16'hF8_00, good[7:0] }) good = good + 1;
	if (good != 700 || qn_n != n0n + 700) begin
		$display("%t ERROR: 700 bytes of the ESP: %0d in order, %0d words", $time, good, qn_n - n0n);
		errors = errors + 1;
	end
	old_good = 0;
	for (i = n0o; i < qo_n; i = i + 1) if (qo[i][23:8] === 16'hF8_00) old_good = old_good + 1;
	$display("tb_mcu_esp: old link: 700 bytes of the ESP gave %0d words to the mcu", old_good);

	// ---- CAPS ----
	frame(8'hF7, 8'h00, 8'h01);
	for (i = 0; i < 4; i = i + 1) frame(8'hFF, 8'h00, 8'h00);
	if (qn[qn_n - 1] !== 24'hF7_03_00) fail("CMD_CAPS not answered");

	$display("tb_mcu_esp: %0d errors", errors);
	$finish;
end

initial begin
	#200000000;
	$display("ERROR: timeout");
	$display("tb_mcu_esp: 1 errors");
	$finish;
end

endmodule
