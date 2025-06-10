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
-- https://github.com/andykarpov/karabas-mini                                      ############### ############### 
--
-- FPGA Boot core for Karabas-Go Mini
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

entity karabas_mini is
    Port ( CLK_50MHZ : in  STD_LOGIC;

           TAPE_IN  : in  STD_LOGIC;
           TAPE_OUT : out  STD_LOGIC;
           AUDIO_L  : out  STD_LOGIC;
           AUDIO_R  : out  STD_LOGIC;

           ADC_CLK  : out  STD_LOGIC;
           ADC_BCK   : out  STD_LOGIC;
           ADC_LRCK : out  STD_LOGIC;
           ADC_DOUT : in  STD_LOGIC;

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
signal clk_vga, clk_8, clk_12		: std_logic;
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
signal ft_audio_stb : std_logic := '0';
signal ft_audio_data : std_logic_vector(11 downto 0);

signal host_vga_r, host_vga_g, host_vga_b, host_vga_r_r, host_vga_g_r, host_vga_b_r, host_vga_r_r2, host_vga_g_r2, host_vga_b_r2 : std_logic_vector(7 downto 0);
signal host_vga_hs, host_vga_vs, host_vga_blank, host_vga_hs_r, host_vga_vs_r, host_vga_blank_r, host_vga_hs_r2, host_vga_vs_r2, host_vga_blank_r2 : std_logic;
signal tmds_red, tmds_green, tmds_blue : std_logic_vector(9 downto 0);

signal adc_l, adc_r : std_logic_vector(23 downto 0);
signal audio_mix_l, audio_mix_r, audio_mix_l_r, audio_mix_r_r, audio_mix_l_r2, audio_mix_r_r2 : std_logic_vector(15 downto 0);

signal clk0, clkfx, clkfx180, clkdv, clkfbout : std_logic;
signal pll_rst_cnt : std_logic_vector(7 downto 0) := "00000000";
signal pll_rst : std_logic;
signal prev_vdac2_sel : std_logic;
signal p_clk_div2, adc_clk_int, adc_div2 : std_logic;
signal hdmi_freq : std_logic_vector(7 downto 0);
signal p_clk_int : std_logic;
signal hdmi_reset : std_logic;

signal vga_hs_buf, vga_vs_buf, ft_de_buf : std_logic;
signal mcu_ft_reset : std_logic;

begin

TAPE_OUT <= '0';
--AUDIO_L <= '0';
--AUDIO_R <= '0';
--ADC_CLK <= '0';
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
--SD_CLK <= '1';
--SD_CS_N <= '1';
MIDI_IN <= '0';
--MIDI_CLK <= '0';
MIDI_RST_N <= '1';
--FT_RESET <= '1';

-- PLL
pll0_inst: entity work.pll 
port map(
	CLK_IN1 => CLK_50MHZ,
	CLK_OUT1 => clk_vga,
	CLK_OUT2 => clk_8,
	CLK_OUT3 => clk_12,
	LOCKED => locked
);

--pllx5_inst : entity work.pllx5
--port map(
--	CLK_IN1 => v_clk_int,
--	CLK_OUT1 => clk_hdmi,
--	CLK_OUT2 => clk_hdmi_n,
--	LOCKED => lockedx5
--);

areset <= not locked;

pllx5 : PLL_BASE
generic map(
    CLKIN_PERIOD => 13.0,
	 CLKFBOUT_MULT => 10,
	 CLKOUT0_DIVIDE => 2,
	 CLKOUT1_DIVIDE => 2,
	 CLKOUT1_PHASE => 180.0,
	 CLKOUT2_DIVIDE => 10,
	 CLKOUT3_DIVIDE => 20,
	 COMPENSATION => "INTERNAL"
)
port map(
	CLKIN => v_clk_int,
	CLKFBIN => clkfbout,
	CLKFBOUT => clkfbout,
	RST => pll_rst,
	LOCKED => lockedx5,
	CLKOUT0 => clkfx, -- 5x
	CLKOUT1 => clkfx180, -- 5x 180deg
	CLKOUT2 => clk0, -- 1x
	CLKOUT3 => clkdv -- div2
);
	 
clkout1_buf: BUFG port map (O => clk_hdmi, I => clkfx);
clkout2_buf: BUFG port map (O => clk_hdmi_n, I => clkfx180);
clkout3_buf: BUFG port map (O => p_clk_int, I => clk0);
clkout4_buf: BUFG port map (O => p_clk_div2, I => clkdv);

process (clk_vga)
begin
	if rising_edge(clk_vga) then
		if ((prev_vdac2_sel /= ft_vga_on) or areset = '1' or hdmi_reset = '1') then
			pll_rst_cnt <= "10000000";
		end if;
		prev_vdac2_sel <= ft_vga_on;
		if (pll_rst_cnt > 0) then 
			pll_rst_cnt <= pll_rst_cnt+1;
		end if;
	end if;
end process;

pll_rst <= pll_rst_cnt(7);

-- VGA SYNC
vga_sync_inst: entity work.vga_sync
port map(
	CLK => p_clk_int,
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
	CLK => p_clk_int,
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
	
	MCU_SPI_MOSI => MCU_MOSI,
	MCU_SPI_MISO => MCU_MISO,
	MCU_SPI_SCK => MCU_SCK,
	MCU_SPI_SS => MCU_CS_N,	
	MCU_SPI_FT_SS => MCU_IO(3),
	MCU_SPI_SD2_SS => MCU_IO(2),
	
	OSD_COMMAND => osd_command,
	
	FT_SPI_ON => ft_spi_on,
	FT_VGA_ON => ft_vga_on,
	FT_CS_N => FT_SPI_CS_N,
	FT_MOSI => FT_SPI_MOSI,
	FT_MISO => FT_SPI_MISO,
	FT_SCK => FT_SPI_SCK,
	FT_RESET => mcu_ft_reset,
	
	SD2_CS_N => SD_CS_N,
	SD2_MOSI => SD_DI,
	SD2_MISO => SD_DO,
	SD2_SCK => SD_CLK
);

FT_RESET <= not mcu_ft_reset;

--red	<= (hcnt(7 downto 0) + shift) and "11111111";
--green	<= (vcnt(7 downto 0) + shift) and "11111111";
--blue	<= (hcnt(7 downto 0) + vcnt(7 downto 0) - shift) and "11111111";
red <= x"00";
green <= x"00";
blue <= x"00";

--host_vga_r <= VGA_R when ft_vga_on = '1' else osd_rgb(23 downto 16) when blank = '0' else "00000000";
--host_vga_g <= VGA_G when ft_vga_on = '1' else osd_rgb(15 downto 8) when blank = '0' else "00000000";
--host_vga_b <= VGA_B when ft_vga_on = '1' else osd_rgb(7 downto 0) when blank = '0' else "00000000";
--host_vga_hs <= VGA_HS when ft_vga_on = '1' else hsync;
--host_vga_vs <= VGA_VS when ft_vga_on = '1' else vsync;
--host_vga_blank <= not(FT_DE) when ft_vga_on = '1' else blank;

--vga_hs_buf0: IBUF port map (I => VGA_HS, O => vga_hs_buf);
--vga_vs_buf0: IBUF port map (I => VGA_VS, O => vga_vs_buf);
--ft_de_buf0: IBUF port map (I => FT_DE, O => ft_de_buf);

process(p_clk_int)
begin
	if rising_edge(p_clk_int) then
		if (ft_vga_on = '1') then
			host_vga_hs_r <= VGA_HS;
			host_vga_vs_r <= VGA_VS;
			host_vga_blank_r <= not(FT_DE);
			host_vga_r_r <= VGA_R;
			host_vga_g_r <= VGA_G;
			host_vga_b_r <= VGA_B;
		else
			host_vga_hs_r <= hsync;
			host_vga_vs_r <= vsync;
			host_vga_blank_r <= blank;
			host_vga_r_r <= osd_rgb(23 downto 16);
			host_vga_g_r <= osd_rgb(15 downto 8);
			host_vga_b_r <= osd_rgb(7 downto 0);
		end if;
		host_vga_hs_r2 <= host_vga_hs_r;
		host_vga_vs_r2 <= host_vga_vs_r;
		host_vga_blank_r2  <= host_vga_blank_r;
		host_vga_r_r2 <= host_vga_r_r;
		host_vga_g_r2 <= host_vga_g_r;
		host_vga_b_r2 <= host_vga_b_r;
		audio_mix_l_r <= audio_mix_l; audio_mix_l_r2 <= audio_mix_l_r;
		audio_mix_r_r <= audio_mix_r; audio_mix_r_r2 <= audio_mix_r_r;
	end if;
end process;

host_vga_r <= "00000000" when host_vga_blank_r2 = '1' else host_vga_r_r2;
host_vga_g <= "00000000" when host_vga_blank_r2 = '1' else host_vga_g_r2;
host_vga_b <= "00000000" when host_vga_blank_r2 = '1' else host_vga_b_r2;
host_vga_hs <= host_vga_hs_r2;
host_vga_vs <= host_vga_vs_r2;
host_vga_blank <= host_vga_blank_r2;

V_CLK_MUX : BUFGMUX
port map (
 I0      => clk_vga,
 I1      => FT_CLK,
 O       => v_clk_int,
 S       => ft_vga_on
);

freq_counter: entity work.freq_counter
generic map(
	fs_ref => 40000000
)
port map(
	i_clk_ref => clk_vga,
	i_clk_test => p_clk_int,
	i_reset => areset,
	o_freq => hdmi_freq
);

-- TODO: HDMI
hdmi: entity work.hdmi
generic map(
	FS => 32000,
	N => 6144
)
port map(
	I_CLK_PIXEL => p_clk_int,
	I_RESET => pll_rst or not(lockedx5),
	I_FREQ => hdmi_freq,
	I_R => host_vga_r,
	I_G => host_vga_g,
	I_B => host_vga_b,
	I_BLANK => host_vga_blank,
	I_HSYNC => host_vga_hs,
	I_VSYNC => host_vga_vs,
	I_AUDIO_ENABLE => '1',
	I_AUDIO_PCM_L => audio_mix_l_r2,
	I_AUDIO_PCM_R => audio_mix_r_r2,
	O_RED => tmds_red,
	O_GREEN => tmds_green,
	O_BLUE => tmds_blue
);

hdmio: entity work.hdmi_out_xilinx
port map(
	clock_pixel_i => p_clk_int,
	clock_tdms_i => clk_hdmi,
	clock_tdms_n_i => clk_hdmi_n,
	red_i => tmds_red,
	green_i => tmds_green,
	blue_i => tmds_blue,
	tmds_out_p => tmds_p,
	tmds_out_n => tmds_n	
);

-- Sigma-Delta DAC
dac_l : entity work.dac
port map(
	I_CLK => p_clk_int,
	I_RESET => areset,
	I_DATA => "00" & not(audio_mix_l(15)) & audio_mix_l(14 downto 4) & "00",
	O_DAC => AUDIO_L
);

dac_r : entity work.dac
port map(
	I_CLK => p_clk_int,
	I_RESET => areset,
	I_DATA => "00" & not(audio_mix_r(15)) & audio_mix_r(14 downto 4) & "00",
	O_DAC => AUDIO_R
);

-- ADC
adc_clk_mux: BUFGMUX 
port map(
 I0 => p_clk_int,
 I1 => p_clk_div2,
 O => adc_clk_int,
 S => adc_div2
);

adc_div2 <= '1' when (hdmi_freq > 32) else '0';

adc : entity work.i2s_transceiver
port map(
	reset_n => not(areset),
	mclk => adc_clk_int,
	sclk => ADC_BCK,
	ws => ADC_LRCK,
	sd_tx => open,
	sd_rx => ADC_DOUT,
	l_data_tx => (others => '0'),
	r_data_tx => (others => '0'),
	l_data_rx => adc_l,
	r_data_rx => adc_r
);

-- ADC_CLK output buf
ODDR2_ADC: ODDR2
port map(
	Q => ADC_CLK,
	C0 => adc_clk_int,
	C1 => not(adc_clk_int),
	CE => '1',
	D0 => '1',
	D1 => '0',
	R => '0',
	S => '0'
);

-- FT_CLK output buf
ODDR2_FT: ODDR2
port map(
	Q => MCU_IO(4),
	C0 => clk_8,
	C1 => not(clk_8),
	CE => '1',
	D0 => '1',
	D1 => '0',
	R => '0',
	S => '0'
);

-- MIDI_CLK output buf
ODDR2_MIDI: ODDR2
port map(
	Q => MIDI_CLK,
	C0 => clk_12,
	C1 => not(clk_12),
	CE => '1',
	D0 => '1',
	D1 => '0',
	R => '0',
	S => '0'
);


audio_mix_l <= adc_l(23 downto 8);
audio_mix_r <= adc_r(23 downto 8);

end Behavioral;

