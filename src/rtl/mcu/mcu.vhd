-------------------------------------------------------------------------------
-- MCU SPI comm module
-------------------------------------------------------------------------------

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.std_logic_arith.conv_integer;
use IEEE.numeric_std.all;
use IEEE.std_logic_unsigned.all;

entity mcu is
	port
	(
	 CLK			 : in std_logic;
	 N_RESET 	 : in std_logic := '1';

	 -- spi
    MCU_SPI_MOSI    : in std_logic;
    MCU_SPI_MISO    : out std_logic := 'Z';
    MCU_SPI_SCK     : in std_logic;
	 MCU_SPI_SS 		 : in std_logic;
	 MCU_SPI_FT_SS 		 : in std_logic;
	 MCU_SPI_SD2_SS 		 : in std_logic;

	 -- ft812 exclusive access by mcu
	 FT_SPI_ON : buffer std_logic := '0'; -- spi access on
	 FT_VGA_ON : buffer std_logic := '0'; -- vga access on

	 FT_SCK	  : out std_logic := '1';
	 FT_MOSI	  : out std_logic := '1';
	 FT_MISO	  : in  std_logic := '1';
	 FT_CS_N   : out std_logic := '1';
	 
	 -- sd2 exclusive access by mcu
	 SD2_SCK	  : out std_logic := '1';
	 SD2_MOSI	  : out std_logic := '1';
	 SD2_MISO	  : in  std_logic := '1';
	 SD2_CS_N   : out std_logic := '1';

    -- osd command
	 OSD_COMMAND: out std_logic_vector(15 downto 0);
	 
	 DEBUG : in std_logic_vector(15 downto 0)
	);
    end mcu;
architecture rtl of mcu is

	-- spi commands
	constant CMD_CONTROL    : std_logic_vector(7 downto 0) := x"09";
	constant CMD_OSD 			: std_logic_vector(7 downto 0) := x"20";
	constant CMD_DEBUG_DATA : std_logic_vector(7 downto 0) := x"31";
	constant CMD_NOPE			: std_logic_vector(7 downto 0) := x"FF";

	 -- spi
	 signal spi_do_valid 	: std_logic := '0';
	 signal spi_do 			: std_logic_vector(23 downto 0);
	 signal spi_miso 		 	: std_logic;
	
begin
	
	--------------------------------------------------------------------------
	-- MCU SPI communication
	--------------------------------------------------------------------------		  
	
	U_SPI: entity work.spi_slave
	generic map(
			N             => 24 -- 3 bytes (cmd + addr + data)       
	 )
	port map(
		  clk_i          => CLK,
		  spi_sck_i      => MCU_SPI_SCK,
		  spi_ssel_i     => MCU_SPI_SS,
		  spi_mosi_i     => MCU_SPI_MOSI,
		  spi_miso_o     => spi_miso,

		  di_req_o       => open,
		  di_i           => CMD_DEBUG_DATA & DEBUG,
		  wren_i         => '1',
		  
		  do_valid_o     => spi_do_valid,
		  do_o           => spi_do,

		  do_transfer_o  => open,
		  wren_o         => open,
		  wren_ack_o     => open,
		  rx_bit_reg_o   => open,
		  state_dbg_o    => open
	);

	MCU_SPI_MISO <= 
		spi_miso when MCU_SPI_SS = '0' else 
		SD2_MISO when MCU_SPI_SD2_SS = '0' else
		FT_MISO when MCU_SPI_FT_SS = '0' else 
		'1';		
	
	FT_SCK <= MCU_SPI_SCK;
	FT_CS_N <= MCU_SPI_FT_SS;
	FT_MOSI <= MCU_SPI_MOSI;
	
	SD2_SCK <= MCU_SPI_SCK;
	SD2_CS_N <= MCU_SPI_SD2_SS;
	SD2_MOSI <= MCU_SPI_MOSI;
	
	process (CLK, spi_do_valid, spi_do)
	begin
		if (rising_edge(CLK)) then
		
			if spi_do_valid = '1' then
				case spi_do(23 downto 16) is 

					-- osd commands					
					when CMD_OSD => OSD_COMMAND <= spi_do(15 downto 0);
					
					-- ft812 / SD spi control
					when CMD_CONTROL => 
								FT_SPI_ON <= spi_do(0);
								FT_VGA_ON <= spi_do(1);

					-- nope
					when CMD_NOPE => null;
					
					when others => null;
				end case;
			end if;
		end if;
	end process;

end RTL;

