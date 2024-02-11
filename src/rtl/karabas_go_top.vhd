library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.all;
use IEEE.numeric_std.all; 

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
           
			  MCU_CS_N : in  STD_LOGIC;
           MCU_SCK : in  STD_LOGIC;
           MCU_MOSI : in  STD_LOGIC;
           MCU_MISO : out  STD_LOGIC);
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

signal osd_rgb : std_logic_vector(23 downto 0);
signal osd_command: std_logic_vector(15 downto 0);

signal ft_spi_on : std_logic := '0';
signal ft_vga_on : std_logic := '0';
signal ft_cs_n   : std_logic := '1';
signal ft_sck    : std_logic := '1';
signal ft_mosi   : std_logic := '1';

  component ODDR2
  port(
          D0	: in std_logic;
          D1	: in std_logic;
          C0	: in std_logic;
          C1	: in std_logic;
          Q	: out std_logic;
          CE    : in std_logic;
          S     : in std_logic;
          R	: in std_logic
    );
  end component;

begin

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
SD_CLK <= '1';
SD_CS_N <= '1';
FDC_DRIVE <= "00";
FDC_MOTOR <= '0';

-- PLL
pll0_inst: entity work.pll 
port map(
	CLK_IN1 => CLK_50MHZ,
	CLK_OUT1 => clk_vga,
	LOCKED => locked
);

areset <= not locked;

-- V_CLK buf
ODDR2_inst: ODDR2
port map(
	Q => V_CLK,
	C0 => clk_vga,
	C1 => not(clk_vga),
	CE => '1',
	D0 => '1',
	D1 => '0',
	R => '0',
	S => '0'
);

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
	
	MCU_MOSI => MCU_MOSI,
	MCU_MISO => MCU_MISO,
	MCU_SCK => MCU_SCK,
	MCU_SS => MCU_CS_N,
	
	MS_X => open,
	MS_Y => open,
	MS_Z => open,
	MS_B => open,
	MS_UPD => open,
	
	KB_STATUS => open,
	KB_DAT0 => open,
	KB_DAT1 => open,
	KB_DAT2 => open,
	KB_DAT3 => open,
	KB_DAT4 => open,
	KB_DAT5 => open,
	
	JOY_L => open,
	JOY_R => open,
	
	RTC_A => (others => '0'),
	RTC_DI => (others => '0'),
	RTC_DO => open,
	RTC_CS => '0',
	RTC_WR_N => '1',
	
	OSD_COMMAND => osd_command,
	SOFTSW_COMMAND => open,
	
	UART_RX_DATA => open,
	UART_RX_IDX => open,
	 
	UART_TX_DATA => (others => '0'),
	UART_TX_WR	=> '0',
	UART_TX_MODE => '0',
	 
	UART_DLM => (others => '0'),
	UART_DLL => (others => '0'),
	UART_DLM_WR => '0',
	UART_DLL_WR => '0',
	 
	ROMLOADER_ACTIVE => open,
	ROMLOAD_ADDR => open,
	ROMLOAD_DATA => open,
	ROMLOAD_WR => open,
	
	FT_SPI_ON => ft_spi_on,
	FT_VGA_ON => ft_vga_on,
	FT_CS_N => FT_SPI_CS_N,
	FT_MOSI => FT_SPI_MOSI,
	FT_MISO => FT_SPI_MISO,
	FT_SCK => FT_SPI_SCK,

	BUSY => open
);

red	<= (hcnt(7 downto 0) + shift) and "11111111";
green	<= (vcnt(7 downto 0) + shift) and "11111111";
blue	<= (hcnt(7 downto 0) + vcnt(7 downto 0) - shift) and "11111111";

VGA_R <= osd_rgb(23 downto 16) when blank = '0' else "00000000";
VGA_G <= osd_rgb(15 downto 8) when blank = '0' else "00000000";
VGA_B <= osd_rgb(7 downto 0) when blank = '0' else "00000000";
VGA_HS <= hsync;
VGA_VS <= vsync;

-- ft812 exclusive access by mcu
FT_OE_N <= '0' when ft_vga_on = '1' else '1';

end Behavioral;

