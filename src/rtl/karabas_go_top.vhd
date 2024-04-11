-------------------------------------------------------------------------------------------------------------------
-- 
-- 
-- #       #######                                                 #                                               
-- #                                                               #                                               
-- #                                                               #                                               
-- ############### ############### ############### ############### ############### ############### ############### 
-- #             #               # #                             # #             #               # #               
-- #             # ############### #               ############### #             # ############### ############### 
-- #             # #             # #               #             # #             # #             #               # 
-- #             # ############### #               ############### ############### ############### ############### 
--                                                                                                                 
--         ####### ####### ####### #######                                         ############### ############### 
--                                                                                 #               #             # 
--                                                                                 #   ########### #             # 
--                                                                                 #             # #             # 
-- https://github.com/andykarpov/karabas-go                                        ############### ############### 
--
-- FPGA Boot core for Karabas-Go
--
-- @author Andy Karpov <andy.karpov@gmail.com>
-- Ukraine, 2024
------------------------------------------------------------------------------------------------------------------

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.all;
use IEEE.numeric_std.all; 
library unisim;
use unisim.vcomponents.all;

entity karabas_go is
    Port ( CLK_50MHZ : in  STD_LOGIC;

           TAPE_IN : in  STD_LOGIC;
           TAPE_OUT : out  STD_LOGIC;
           BEEPER : out  STD_LOGIC;

           DAC_LRCK : out  STD_LOGIC;
           DAC_DAT : out  STD_LOGIC;
           DAC_BCK : out  STD_LOGIC;
           DAC_MUTE : out  STD_LOGIC;
           
			  ESP_RESET_N : out  STD_LOGIC;
           ESP_BOOT_N : out  STD_LOGIC;           
			  UART_RX : inout  STD_LOGIC;
           UART_TX : inout  STD_LOGIC;
           UART_CTS : out  STD_LOGIC;
           
			  WA : out  STD_LOGIC_VECTOR (2 downto 0);
           WCS_N : out  STD_LOGIC_VECTOR(1 downto 0);
           WRD_N : out  STD_LOGIC;
           WWR_N : out  STD_LOGIC;
           WRESET_N : out  STD_LOGIC;
           WD : inout  STD_LOGIC_VECTOR (15 downto 0);
           
			  MA : out  STD_LOGIC_VECTOR (20 downto 0);
           MD : inout  STD_LOGIC_VECTOR (15 downto 0);
           MWR_N : out  STD_LOGIC_VECTOR (1 downto 0);
           MRD_N : out  STD_LOGIC_VECTOR (1 downto 0);
           
			  SDR_BA : out  STD_LOGIC_VECTOR (1 downto 0);
           SDR_A : out  STD_LOGIC_VECTOR (12 downto 0);
           SDR_CLK : out  STD_LOGIC;
           SDR_DQM : out  STD_LOGIC_VECTOR (1 downto 0);
           SDR_WE_N : out  STD_LOGIC;
           SDR_CAS_N : out  STD_LOGIC;
           SDR_RAS_N : out  STD_LOGIC;
           SDR_DQ : inout  STD_LOGIC_VECTOR (15 downto 0);
           
			  SD_CS_N : out  STD_LOGIC;
           SD_DI : inout  STD_LOGIC;
           SD_DO : inout  STD_LOGIC;
           SD_CLK : out  STD_LOGIC;
           SD_DET_N : in  STD_LOGIC;
           
			  FDC_INDEX : inout  STD_LOGIC;
           FDC_DRIVE : out  STD_LOGIC_VECTOR (1 downto 0);
           FDC_MOTOR : out  STD_LOGIC;
           FDC_DIR : inout  STD_LOGIC;
           FDC_STEP : inout  STD_LOGIC;
           FDC_WDATA : inout  STD_LOGIC;
           FDC_WGATE : inout  STD_LOGIC;
           FDC_TR00 : inout  STD_LOGIC;
           FDC_WPRT : inout  STD_LOGIC;
           FDC_RDATA : inout  STD_LOGIC;
           FDC_SIDE_N : inout  STD_LOGIC;
           
			  FT_SPI_CS_N : out  STD_LOGIC;
           FT_SPI_SCK : out  STD_LOGIC;
           FT_SPI_MISO : in  STD_LOGIC;
           FT_SPI_MOSI : out  STD_LOGIC;
           FT_INT_N : in  STD_LOGIC;
           FT_CLK : in  STD_LOGIC;
           FT_OE_N : out  STD_LOGIC;
			  
           VGA_R : out  STD_LOGIC_VECTOR (7 downto 0);
           VGA_G : out  STD_LOGIC_VECTOR (7 downto 0);
           VGA_B : out  STD_LOGIC_VECTOR (7 downto 0);
           VGA_HS : out  STD_LOGIC;
           VGA_VS : out  STD_LOGIC;
           V_CLK : out  STD_LOGIC;
           
			  MCU_SPI_CS_N : in  STD_LOGIC;
           MCU_SPI_SCK : in  STD_LOGIC;
           MCU_SPI_MOSI : in  STD_LOGIC;
           MCU_SPI_MISO : out  STD_LOGIC;

			  MCU_SPI_FT_CS_N : in  STD_LOGIC;
			  MCU_SPI_SD2_CS_N : in  STD_LOGIC;
			  
           MCU_SPI_IO : inout  std_logic_vector(1 downto 0)
			  
			  );
end karabas_go;

architecture Behavioral of karabas_go is

signal hcnt		: std_logic_vector(11 downto 0) := "000000000000"; 	-- horizontal pixel counter
signal vcnt		: std_logic_vector(11 downto 0) := "000000000000"; 	-- vertical line counter
signal hsync		: std_logic;
signal vsync		: std_logic;
signal blank		: std_logic;
signal shift		: std_logic_vector(7 downto 0);
signal red		: std_logic_vector(7 downto 0);
signal green		: std_logic_vector(7 downto 0);
signal blue		: std_logic_vector(7 downto 0);
signal clk_vga		: std_logic;
signal locked : std_logic;
signal areset 		: std_logic;
signal v_clk_vga : std_logic;
signal v_clk_int : std_logic;

signal osd_rgb : std_logic_vector(23 downto 0);
signal osd_command: std_logic_vector(15 downto 0);

signal ft_vga_on : std_logic := '0';

begin

-- PLL
pll0_inst: entity work.pll 
port map(
	CLK_IN1 => CLK_50MHZ,
	CLK_OUT1 => clk_vga,
	LOCKED => locked
);

areset <= not locked;

-- VGA SYNC
vga_sync_inst: entity work.vga_sync
port map(
	CLK => clk_vga,
	HSYNC => hsync,
	VSYNC => vsync,
	BLANK => blank,
	HPOS => hcnt,
	VPOS => vcnt,
	SHIFT => shift
);

-- OSD
osd_inst: entity work.overlay
port map(
	CLK => clk_vga,
	RGB_I => red & green & blue,
	RGB_O => osd_rgb,
	HCNT_I => hcnt,
	VCNT_I => vcnt,
	OSD_COMMAND => osd_command
);

-- MCU
mcu_inst: entity work.mcu
port map(
	CLK => clk_vga,
	N_RESET => not areset,
	
	MCU_SPI_MOSI => MCU_SPI_MOSI,
	MCU_SPI_MISO => MCU_SPI_MISO,
	MCU_SPI_SCK => MCU_SPI_SCK,
	MCU_SPI_SS => MCU_SPI_CS_N,

	MCU_SPI_FT_SS => MCU_SPI_FT_CS_N,
	MCU_SPI_SD2_SS => MCU_SPI_SD2_CS_N,
	
	OSD_COMMAND => osd_command,

	FT_VGA_ON => ft_vga_on,
	
	FT_CS_N => FT_SPI_CS_N,
	FT_MOSI => FT_SPI_MOSI,
	FT_MISO => FT_SPI_MISO,
	FT_SCK => FT_SPI_SCK,
	
	SD2_CS_N => SD_CS_N,
	SD2_MOSI => SD_DI,
	SD2_MISO => SD_DO,
	SD2_SCK => SD_CLK
);

red	<= (hcnt(7 downto 0) + shift) and "11111111";
green	<= (vcnt(7 downto 0) + shift) and "11111111";
blue	<= (hcnt(7 downto 0) + vcnt(7 downto 0) - shift) and "11111111";

VGA_R <= (others => 'Z') when ft_vga_on = '1' else osd_rgb(23 downto 16) when blank = '0' else "00000000";
VGA_G <= (others => 'Z') when ft_vga_on = '1' else osd_rgb(15 downto 8) when blank = '0' else "00000000";
VGA_B <= (others => 'Z') when ft_vga_on = '1' else osd_rgb(7 downto 0) when blank = '0' else "00000000";
VGA_HS <= 'Z' when ft_vga_on = '1' else hsync;
VGA_VS <= 'Z' when ft_vga_on = '1' else vsync;

-- ft812 exclusive access by mcu
FT_OE_N <= '0' when ft_vga_on = '1' else '1';

-- video clk mux
V_CLK_MUX : BUFGMUX_1
port map (
 I0      => clk_vga,
 I1      => FT_CLK,
 O       => v_clk_int,
 S       => ft_vga_on
);

-- V_CLK output buf
ODDR2_inst: ODDR2
port map(
	Q => V_CLK,
	C0 => v_clk_int,
	C1 => not(v_clk_int),
	CE => '1',
	D0 => '1',
	D1 => '0',
	R => '0',
	S => '0'
);

-- unused signals
TAPE_OUT <= '0';
BEEPER <= '0';
DAC_LRCK <= '0';
DAC_BCK <= '0';
DAC_DAT <= '0';
DAC_MUTE <= '1';
ESP_RESET_N <= '1';
ESP_BOOT_N <= '1';
UART_CTS <= '0';
WA <= (others => '1');
WCS_N <= "11";
WRD_N <= '1';
WWR_N <= '1';
WRESET_N <= '1';
MA <= (others => '0');
MWR_N <= "11";
MRD_N <= "11";
SDR_BA <= "00";
SDR_A <= (others => '0');
SDR_CLK <= '0';
SDR_DQM <= "00";
SDR_WE_N <= '1';
SDR_CAS_N <= '1';
SDR_RAS_N <= '1';
FDC_DRIVE <= "00";
FDC_MOTOR <= '0';

end Behavioral;

