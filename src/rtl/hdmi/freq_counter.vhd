library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.STD_LOGIC_UNSIGNED.ALL;

entity freq_counter is
port (
  i_clk_ref            : in  std_logic;
  i_clk_test           : in  std_logic;
  i_reset              : in  std_logic;
  o_freq         		  : out std_logic_vector(31 downto 0));
end freq_counter;

architecture rtl of freq_counter is

signal cnt : std_logic_vector(31 downto 0) := (others => '0');
signal measure : std_logic_vector(31 downto 0) := (others => '0');
signal freq : std_logic_vector(31 downto 0) := (others => '0');

constant time_interval : integer := 200000000/1000;
signal test : std_logic;
signal test_r : std_logic_vector(1 downto 0);
signal prev_test_r : std_logic;

begin

-- convert i_clk_test to test (div2)
process (i_clk_test)
begin
	if rising_edge(i_clk_test) then
		test <= not test;
	end if;
end process;

-- cross domain test clock
process (i_clk_ref)
begin
	if rising_edge(i_clk_ref) then
		test_r(0) <= test;
		test_r(1) <= test_r(0);
	end if;
end process;

-- measuring interval counter
process (i_clk_ref)
begin
	if rising_edge(i_clk_ref) then 
		if (cnt < time_interval) then 
			cnt <= cnt + 1;
		else
			cnt <= (others => '0');
		end if;
	end if;
end process;

-- measuring freq counter by rising_edge
process (i_clk_ref)
begin
	if rising_edge(i_clk_ref) then
		prev_test_r <= test_r(1);
		if (cnt = 0) then 
			freq <= measure;
			measure <= (others => '0');
		else
			if (prev_test_r = '0' and test_r(1) = '1') then
				measure <= measure + 1;
			end if;
		end if;
	end if;
end process;

-- align measured freq to known frequencies
process (i_clk_test)
begin
	if rising_edge(i_clk_test) then

		-- 80
		if (freq > 75000000/2000) then
			o_freq <= x"04c4b400"; -- 80000000;
		-- 72
		elsif (freq > 70000000/2000) then 
			o_freq <= x"044aa200"; -- 72000000;
		-- 64
		elsif (freq > 60000000/2000) then
			o_freq <= x"03d09000"; -- 64000000;
		-- 56
		elsif (freq > 53000000/2000) then 
			o_freq <= x"03567e00"; -- 56000000;
		-- 48
		elsif (freq >= 43000000/2000) then 
			o_freq <= x"02dc6c00"; -- 48000000;
		-- 40
		elsif (freq >= 35000000/2000) then 
			o_freq <= x"02625a00"; -- 40000000;
		-- 32
		elsif (freq >= 30000000/2000) then 
			o_freq <= x"01e84800"; -- 32000000;
		-- 28
		elsif (freq >= 26000000/2000) then
			o_freq <= x"01ab3f00"; -- 28000000;
		-- 24
		else 
			o_freq <= x"016e3600"; -- 24000000;
		end if;
	end if;
end process;

end rtl;