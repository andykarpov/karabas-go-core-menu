library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.all;
use IEEE.numeric_std.all; 
library unisim;
use unisim.vcomponents.all;

entity karabas_mini is
    Port ( CLK_50MHZ : in  STD_LOGIC;

           TAPE_IN  : in  STD_LOGIC;
           TAPE_OUT : out  STD_LOGIC;
           AUDIO_L  : out  STD_LOGIC;
           AUDIO_R  : out  STD_LOGIC;

           ADC_LRCK : in  STD_LOGIC;
           ADC_DOUT : in  STD_LOGIC;
           ADC_CLK  : out  STD_LOGIC;
           ADC_BCK   : in  STD_LOGIC;

           MIDI_IN : out std_logic;
           MIDI_CLK : out std_logic;
           MIDI_RST_N : out std_logic;

           TMDS_P : out std_logic_vector(3 downto 0);
           TMDS_N : out std_logic_vector(3 downto 0);
           
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
           
		   FT_SPI_CS_N : out  STD_LOGIC;
           FT_SPI_SCK : out  STD_LOGIC;
           FT_SPI_MISO : in  STD_LOGIC;
           FT_SPI_MOSI : out  STD_LOGIC;
           FT_INT_N : in  STD_LOGIC;
           FT_CLK : in  STD_LOGIC;
           FT_AUDIO : in std_logic;
           FT_DE : in std_logic;
           FT_DISP : in std_logic;
           FT_RESET : out std_logic;
			  
           VGA_R : in  STD_LOGIC_VECTOR (7 downto 0);
           VGA_G : in  STD_LOGIC_VECTOR (7 downto 0);
           VGA_B : in  STD_LOGIC_VECTOR (7 downto 0);
           VGA_HS : in  STD_LOGIC;
           VGA_VS : in  STD_LOGIC;
           
		   MCU_CS_N : in  STD_LOGIC;
           MCU_SCK : in  STD_LOGIC;
           MCU_MOSI : in  STD_LOGIC;
           MCU_MISO : out  STD_LOGIC;
           MCU_IO : inout STD_LOGIC_VECTOR(4 downto 0)
);
end karabas_mini;

architecture Behavioral of karabas_mini is

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
signal locked, lockedx5 : std_logic;
signal areset 		: std_logic;
signal v_clk_vga : std_logic;
signal v_clk_int : std_logic;
signal clk_hdmi : std_logic;
signal clk_hdmi_n : std_logic;

signal osd_rgb : std_logic_vector(23 downto 0);
signal osd_command: std_logic_vector(15 downto 0);

signal ft_spi_on : std_logic := '0';
signal ft_vga_on : std_logic := '0';
signal ft_cs_n   : std_logic := '1';
signal ft_sck    : std_logic := '1';
signal ft_mosi   : std_logic := '1';

signal host_vga_r, host_vga_g, host_vga_b : std_logic_vector(7 downto 0);
signal host_vga_hs, host_vga_vs, host_vga_blank : std_logic;
signal tmds_red, tmds_green, tmds_blue : std_logic_vector(9 downto 0);

begin

TAPE_OUT <= '0';
AUDIO_L <= '0';
AUDIO_R <= '0';
ADC_CLK <= '0';
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
ADC_CLK <= '0';
MIDI_IN <= '0';
MIDI_CLK <= '0';
MIDI_RST_N <= '0';
FT_RESET <= '1';

-- PLL
pll0_inst: entity work.pll 
port map(
	CLK_IN1 => CLK_50MHZ,
	CLK_OUT1 => clk_vga,
	LOCKED => locked
);

pllx5_inst : entity work.pllx5
port map(
	CLK_IN1 => v_clk_int,
	CLK_OUT1 => clk_hdmi,
	CLK_OUT2 => clk_hdmi_n,
	LOCKED => lockedx5
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
	FT_CS_N => FT_SPI_CS_N, -- todo: mux on ft_spi_on with host spi
	FT_MOSI => FT_SPI_MOSI,
	FT_MISO => FT_SPI_MISO,
	FT_SCK => FT_SPI_SCK,

	BUSY => open
);

red	<= (hcnt(7 downto 0) + shift) and "11111111";
green	<= (vcnt(7 downto 0) + shift) and "11111111";
blue	<= (hcnt(7 downto 0) + vcnt(7 downto 0) - shift) and "11111111";

host_vga_r <= VGA_R when ft_vga_on = '1' else osd_rgb(23 downto 16) when blank = '0' else "00000000";
host_vga_g <= VGA_G when ft_vga_on = '1' else osd_rgb(15 downto 8) when blank = '0' else "00000000";
host_vga_b <= VGA_B when ft_vga_on = '1' else osd_rgb(7 downto 0) when blank = '0' else "00000000";
host_vga_hs <= VGA_HS when ft_vga_on = '1' else hsync;
host_vga_vs <= VGA_VS when ft_vga_on = '1' else vsync;
host_vga_blank <= not(FT_DE) when ft_vga_on = '1' else blank;

V_CLK_MUX : BUFGMUX_1
port map (
 I0      => clk_vga,
 I1      => FT_CLK,
 O       => v_clk_int,
 S       => ft_vga_on
);

-- TODO: HDMI
hdmi: entity work.hdmi
generic map(
	FREQ => 25000000, -- TODO: remap as signals to adjust on the fly ?
	FS => 48000,
	CTS => 25000,
	N => 6144
)
port map(
	I_CLK_PIXEL => v_clk_int,
	I_R => host_vga_r,
	I_G => host_vga_g,
	I_B => host_vga_b,
	I_BLANK => host_vga_blank,
	I_HSYNC => host_vga_hs,
	I_VSYNC => host_vga_vs,
	I_AUDIO_ENABLE => '0',
	I_AUDIO_PCM_L => (others => '0'),
	I_AUDIO_PCM_R => (others => '0'),
	O_RED => tmds_red,
	O_GREEN => tmds_green,
	O_BLUE => tmds_blue
);

hdmio: entity work.hdmi_out_xilinx
port map(
	clock_pixel_i => v_clk_int,
	clock_tdms_i => clk_hdmi,
	clock_tdms_n_i => clk_hdmi_n,
	red_i => tmds_red,
	green_i => tmds_green,
	blue_i => tmds_blue,
	tmds_out_p => tmds_p,
	tmds_out_n => tmds_n	
);

-- TODO: ADC

-- TODO: FT PWM ADC

-- TODO: PWM AUDIO OUT

end Behavioral;

