`ifdef SIMULATION
    `define POR_MAX 16'h000f // period of power on reset
`else  // Real FPGA
    `define POR_MAX 16'hffff  // period of power on reset
`endif

module pcm1808
    #(parameter bitNum = 16)
    (
	AD_DAT,MCLK,LRCLK,SCLK,DATA_L,DATA_R
    );

    //ADC PCM1808PWR SlaveMode
    input MCLK;		//PIN_132
    input LRCLK;	//PIN_130
    input SCLK;		//PIN_131
    input AD_DAT;	//PIN_124
    output signed [bitNum-1:0]DATA_L;
    output signed [bitNum-1:0]DATA_R;


    //--------------------------
    // Internal Power on Reset
    //--------------------------
    wire res_n;            // Internal Reset Signal
    reg  por_n;            // should be power-up level = Low
    reg  [15:0] por_count; // should be power-up level = Low
    //
    always @(posedge MCLK)
    begin
	if (por_count != `POR_MAX)
	begin
	    por_n <= 1'b0;
	    por_count <= por_count + 16'h0001;
	end
	else
	begin
	    por_n <= 1'b1;
	    por_count <= por_count;
	end
    end
    //
    assign res_n = por_n;


    /* MCLK 12.288MHz
     * SCK 3.072MHz	MCLK/4
     * LRLCK 48KHz	MCLK/256
     */
    wire signed [bitNum-1:0]DATA_L;
    wire signed [bitNum-1:0]DATA_R;

    i2s_s2p #(.bitNum(bitNum)) ADC  (
    //clock_bit, clock_lr, data_in, data_l,data_r
	.clock_bit(SCLK), .data_in(AD_DAT), .clock_lr(LRCLK), .data_l(DATA_L), .data_r(DATA_R)
    );

endmodule

module i2s_s2p
    #(parameter bitNum = 16)
    (clock_bit, clock_lr, data_in, data_l,data_r);
    input clock_bit;
    input clock_lr;
    input data_in;

    output [bitNum-1:0]data_l;
    output [bitNum-1:0]data_r;
    reg signed [bitNum-1:0]data_l;
    reg signed [bitNum-1:0]data_r;

    reg [64:0]data64_tmp;

    reg previous_clock_lr;

    always @ (posedge clock_bit)
    begin
        data64_tmp = data64_tmp << 1;
        data64_tmp[0] = data_in;
        if (previous_clock_lr == 1 && clock_lr == 0)
            begin
                data_l[bitNum-1:0] <= data64_tmp[63:63-bitNum+1];
                data_r[bitNum-1:0] <= data64_tmp[31:31-bitNum+1];
            end
        previous_clock_lr = clock_lr;
    end
endmodule
