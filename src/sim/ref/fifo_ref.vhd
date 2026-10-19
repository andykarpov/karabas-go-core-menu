-- A VHDL model of src/rtl/fifo/fifo.v of new_hdmi_pll d31dec0 (the old queue to the MCU in mcu.vhd and the fifos
-- of esp8266.vhd), for the tests of src/sim: ISim cannot elaborate the Verilog module with open ports from VHDL.
-- Same behaviour: dout registered on rd, empty when the pointers are equal, full one entry early (DEPTH - 1 words),
-- a write while full and a read while empty do nothing, data_count saturated at DEPTH - 1, asynchronous reset.

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity fifo_ref is
	generic (
		DATA_WIDTH : integer := 8;
		ADDR_WIDTH : integer := 8
	);
	port (
		clk        : in  std_logic;
		reset      : in  std_logic;
		rd         : in  std_logic;
		wr         : in  std_logic;
		din        : in  std_logic_vector(DATA_WIDTH-1 downto 0);
		dout       : out std_logic_vector(DATA_WIDTH-1 downto 0);
		full       : out std_logic;
		empty      : out std_logic;
		data_count : out std_logic_vector(ADDR_WIDTH-1 downto 0)
	);
end fifo_ref;

architecture sim of fifo_ref is
	constant DEPTH : integer := 2**ADDR_WIDTH;
	type mem_t is array (0 to DEPTH-1) of std_logic_vector(DATA_WIDTH-1 downto 0);
	signal mem : mem_t;
	signal rptr, wptr : unsigned(ADDR_WIDTH-1 downto 0) := (others => '0');
	signal counter : unsigned(ADDR_WIDTH downto 0) := (others => '0');
	signal is_full, is_empty : std_logic;
begin
	is_empty <= '1' when wptr = rptr else '0';
	is_full <= '1' when wptr + 1 = rptr else '0';
	empty <= is_empty;
	full <= is_full;
	data_count <= std_logic_vector(to_unsigned(DEPTH-1, ADDR_WIDTH)) when counter >= DEPTH else
		std_logic_vector(counter(ADDR_WIDTH-1 downto 0));

	process (clk, reset)
	begin
		if reset = '1' then
			rptr <= (others => '0');
			wptr <= (others => '0');
			counter <= (others => '0');
		elsif rising_edge(clk) then
			if wr = '1' and is_full = '0' then
				mem(to_integer(wptr)) <= din;
				wptr <= wptr + 1;
				counter <= counter + 1;
			end if;
			if rd = '1' and is_empty = '0' then
				dout <= mem(to_integer(rptr));
				rptr <= rptr + 1;
				counter <= counter - 1;
			end if;
			if rd = '1' and wr = '1' and is_full = '0' and is_empty = '0' then
				counter <= counter;
			end if;
		end if;
	end process;
end sim;
