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
			  CLK_50MHZ_OLD : in STD_LOGIC;

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
           MCU_IO : inout STD_LOGIC_VECTOR(3 downto 0)
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
signal locked, lockedx5 : std_logic;
signal areset 		: std_logic;

signal sysclk, sysclk_buf   : std_logic;
signal clk_vga, clk_vga_buf : std_logic;
signal clkpll1_fbout, clkpll1_fbout_buf : std_logic;
signal clkfbout : std_logic;
signal pllclk0, pllclk1, pllclk2 : std_logic;
signal pll_lckd, pclk_lckd : std_logic;
signal pclk, pclkx2, pclkx10 : std_logic;
signal serdesstrobe : std_logic;
signal bufpll_lock: std_logic;
signal serdes_rst : std_logic := '0';
signal tmdsclkint : std_logic_vector(4 downto 0) := "00000";
signal tmdsclk : std_logic;
signal toggle : std_logic := '0';
signal tmdsint : std_logic_vector(2 downto 0);

signal v_clk_int : std_logic;

signal osd_rgb : std_logic_vector(23 downto 0);
signal osd_command: std_logic_vector(15 downto 0);

signal ft_spi_on : std_logic := '0';
signal ft_vga_on : std_logic := '0';
signal ft_cs_n   : std_logic := '1';
signal ft_sck    : std_logic := '1';
signal ft_mosi   : std_logic := '1';
signal ft_audio_stb : std_logic := '0';
signal ft_audio_data : std_logic_vector(11 downto 0);
signal ft_clk_ibuf, ft_clk_buf : std_logic;

signal host_vga_r, host_vga_g, host_vga_b : std_logic_vector(7 downto 0);
signal host_vga_hs, host_vga_vs, host_vga_blank : std_logic;
signal tmds_red, tmds_green, tmds_blue : std_logic_vector(4 downto 0);

signal adc_l, adc_r : std_logic_vector(23 downto 0);
signal audio_mix_l, audio_mix_r : std_logic_vector(15 downto 0);

signal prev_ft_vga_on, reset_pll2 : std_logic := '0';

-- regs in ft_clk_buf clock 
signal vga_r_r, vga_g_r, vga_b_r, vga_r_r2, vga_g_r2, vga_b_r2 : std_logic_vector(7 downto 0);
signal vga_hs_r, vga_vs_r, vga_hs_r2, vga_vs_r2 : std_logic;
signal ft_de_r, ft_de_r2 : std_logic;

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
MIDI_CLK <= '0';
MIDI_RST_N <= '1';
FT_RESET <= '1';

-- GLobal clock

sysclk_ibuf: IBUF port map(I => CLK_50MHZ, O => sysclk);
sysclk_bufg: BUFG port map(I => sysclk, O => sysclk_buf);

-- 25 MHz PLL
pll1: PLL_BASE
generic map(
	BANDWIDTH => "OPTIMIZED",
	CLK_FEEDBACK => "CLKFBOUT",
	COMPENSATION => "SYSTEM_SYNCHRONOUS",
	DIVCLK_DIVIDE => 1,
	CLKFBOUT_MULT => 8,
	CLKFBOUT_PHASE => 0.000,
	CLKOUT0_DIVIDE => 16,
	CLKOUT0_PHASE => 0.000,
	CLKOUT0_DUTY_CYCLE => 0.500,
	CLKIN_PERIOD => 20.0,
	REF_JITTER => 0.010
)
port map(
	CLKIN => sysclk,
	RST => '0',

	CLKOUT0 => clk_vga,
	LOCKED => locked,
	
	CLKFBIN => clkpll1_fbout_buf,
	CLKFBOUT => clkpll1_fbout
);

sysclkf_buf0: BUFG port map(O => clkpll1_fbout_buf, I => clkpll1_fbout);
clk_vga_buf0: BUFG port map(O => clk_vga_buf, I => clk_vga);
areset <= not locked;

-- PLL 1x, 2x, 10x 

pll2: PLL_BASE 
generic map(
	CLKIN_PERIOD => 13.0,
	CLKFBOUT_MULT => 10, -- set VCO to 10x of CLKIN
	CLKOUT0_DIVIDE => 1,
	CLKOUT1_DIVIDE => 10,
	CLKOUT2_DIVIDE => 5,
	COMPENSATION => "INTERNAL"	
)
port map (
	CLKFBOUT => clkfbout,
	CLKOUT0 => pllclk0,
	CLKOUT1 => pllclk1,
	CLKOUT2 => pllclk2,
	LOCKED => pll_lckd,
	CLKFBIN => clkfbout,
	CLKIN => v_clk_int,
	RST => reset_pll2 -- not(pclk_lckd)
  );
  
  pclk_lckd <= locked;
  
  process(clk_vga_buf) 
  begin
	if rising_edge(clk_vga_buf) then
		reset_pll2 <= '0';
		if prev_ft_vga_on /= ft_vga_on then
			reset_pll2 <= '1';
			prev_ft_vga_on <= ft_vga_on;
		end if;
	end if;
  end process;
  
bufpll0: BUFPLL generic map(
	DIVIDE => 5
) 
port map (
	PLLIN => pllclk0, 
	GCLK => pclkx2, 
	LOCKED => pll_lckd, 
	IOCLK => pclkx10, 
	SERDESSTROBE => serdesstrobe, 
	LOCK => bufpll_lock
);

pclkbufg: BUFG port map (I => pllclk1, O => pclk);
pclkx2bufg: BUFG port map (I => pllclk2, O => pclkx2);

-- VGA SYNC
vga_sync_inst: entity work.vga_sync
port map(
	CLK => clk_vga_buf,
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
	CLK => clk_vga_buf,
	RGB_I => red & green & blue,
	RGB_O => osd_rgb,
	HCNT_I => hcnt,
	VCNT_I => vcnt,
	OSD_COMMAND => osd_command
);

-- MCU
mcu_inst: entity work.mcu
port map(
	CLK => clk_vga_buf,
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
	
	SD2_CS_N => SD_CS_N,
	SD2_MOSI => SD_DI,
	SD2_MISO => SD_DO,
	SD2_SCK => SD_CLK
);

red	<= (hcnt(7 downto 0) + shift) and "11111111";
green	<= (vcnt(7 downto 0) + shift) and "11111111";
blue	<= (hcnt(7 downto 0) + vcnt(7 downto 0) - shift) and "11111111";

process (pclk)
begin
	if rising_edge(pclk) then
		vga_r_r <= VGA_R;
		vga_g_r <= VGA_G;
		vga_b_r <= VGA_B;
		vga_hs_r <= VGA_HS;
		vga_vs_r <= VGA_VS;
		ft_de_r <= FT_DE;
		vga_r_r2 <= vga_r_r;
		vga_g_r2 <= vga_g_r;
		vga_b_r2 <= vga_b_r;
		vga_hs_r2 <= vga_hs_r;
		vga_vs_r2 <= vga_vs_r;
		ft_de_r2 <= ft_de_r;
	end if;
end process;

host_vga_r <= vga_r_r2 when ft_vga_on = '1' else osd_rgb(23 downto 16) when blank = '0' else "00000000";
host_vga_g <= vga_g_r2 when ft_vga_on = '1' else osd_rgb(15 downto 8) when blank = '0' else "00000000";
host_vga_b <= vga_b_r2 when ft_vga_on = '1' else osd_rgb(7 downto 0) when blank = '0' else "00000000";
host_vga_hs <= vga_hs_r2 when ft_vga_on = '1' else hsync;
host_vga_vs <= vga_vs_r2 when ft_vga_on = '1' else vsync;
host_vga_blank <= not(ft_de_r2) when ft_vga_on = '1' else blank;

FT_CLK_IBUF0: IBUF
port map (
	I		=> FT_CLK,
	O		=> ft_clk_ibuf
);

FT_CLK_BUF0: BUFG
port map (
	I		=> ft_clk_ibuf,
	O 		=> ft_clk_buf
);

V_CLK_MUX : BUFGMUX_1
port map (
 I0      => clk_vga_buf,
 I1      => ft_clk_buf,
 O       => v_clk_int,
 S       => ft_vga_on
);

-- TODO: HDMI

-- todo: replace to hdmi with sound
enc0 : entity work.dvi_encoder
port map (
    clkin => pclk,
    clkx2in => pclkx2,
    rstin => areset,
    blue_din => host_vga_b,
    green_din => host_vga_g,
    red_din => host_vga_r,
    hsync => host_vga_hs,
    vsync => host_vga_vs,
    de => not host_vga_blank,
    tmds_data0 => tmds_red,
    tmds_data1 => tmds_green,
    tmds_data2 => tmds_blue);
	 
-- DVI serializers and OB
	 
oserdes_0: entity work.serdes_n_to_1
generic map (SF => 5)
port map(
	ioclk => pclkx10,
	serdesstrobe => serdesstrobe,
	reset => serdes_rst,
	gclk => pclkx2,
	datain => tmds_red,
	iob_data_out => tmdsint(0)
);

oserdes_1: entity work.serdes_n_to_1
generic map (SF => 5)
port map(
	ioclk => pclkx10,
	serdesstrobe => serdesstrobe,
	reset => serdes_rst,
	gclk => pclkx2,
	datain => tmds_green,
	iob_data_out => tmdsint(1)
);

oserdes_2: entity work.serdes_n_to_1
generic map (SF => 5)
port map(
	ioclk => pclkx10,
	serdesstrobe => serdesstrobe,
	reset => serdes_rst,
	gclk => pclkx2,
	datain => tmds_blue,
	iob_data_out => tmdsint(2)
);

oserdes_3: entity work.serdes_n_to_1
generic map (SF => 5)
port map(
	ioclk => pclkx10,
	serdesstrobe => serdesstrobe,
	reset => serdes_rst,
	gclk => pclkx2,
	datain => tmdsclkint,
	iob_data_out => tmdsclk
);

TMDS0: OBUFDS port map (I => tmdsint(0), O => TMDS_P(0), OB => TMDS_N(0));
TMDS1: OBUFDS port map (I => tmdsint(1), O => TMDS_P(1), OB => TMDS_N(1));
TMDS2: OBUFDS port map (I => tmdsint(2), O => TMDS_P(2), OB => TMDS_N(2));
TMDS3: OBUFDS port map (I => tmdsclk, O => TMDS_P(3), OB => TMDS_N(3));

process (pclkx2, serdes_rst)
begin
	if serdes_rst = '1' then 
		toggle <= '0';
	elsif rising_edge(pclkx2) then
		toggle <= not toggle;
	end if;
end process;

process (pclkx2)
begin
	if rising_edge(pclkx2) then
		if toggle = '1' then
			tmdsclkint <= "11111";
		else 
			tmdsclkint <= "00000";
		end if;
	end if;
end process;

serdes_rst <= areset;

-- Sigma-Delta DAC
dac_l : entity work.dac
port map(
	I_CLK => v_clk_int,
	I_RESET => areset,
	I_DATA => "00" & not(audio_mix_l(15)) & audio_mix_l(14 downto 4) & "00",
	O_DAC => AUDIO_L
);

dac_r : entity work.dac
port map(
	I_CLK => v_clk_int,
	I_RESET => areset,
	I_DATA => "00" & not(audio_mix_r(15)) & audio_mix_r(14 downto 4) & "00",
	O_DAC => AUDIO_R
);

-- ADC
adc : entity work.i2s_transceiver
port map(
	reset_n => not(areset),
	mclk => v_clk_int,
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
	C0 => v_clk_int,
	C1 => not(v_clk_int),
	CE => '1',
	D0 => '1',
	D1 => '0',
	R => '0',
	S => '0'
);

audio_mix_l <= adc_l(23 downto 8);
audio_mix_r <= adc_r(23 downto 8);

end Behavioral;

