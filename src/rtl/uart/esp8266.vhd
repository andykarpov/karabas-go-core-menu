--------------------------------------------------------------------------------
-- ESP8266 SPI wrapper
--
-- The uart of the ESP8266 module (115200 8N1, CTS) bridged to the MCU by kg_mcu (ENABLE_ESP, CMD_ESP_UART):
-- - ESP_UART_TX_DATA / ESP_UART_TX_WR (one clock): a byte of the MCU, through a fifo of 256 to the uart;
-- - a byte of the uart goes through a fifo of 2048 to ESP_UART_RX_DATA, held with ESP_UART_RX_WR until
--   ESP_UART_RX_ACK: kg_mcu takes it when its reply queue has room. CTS stops the ESP when the fifo is nearly
--   full. The byte was taken from the fifo in the clock of its read, which gave the byte before (each byte
--   reached the MCU when the next one came), and sent on a strobe, lost when the reply queue was full.
--
-- @author Andy Karpov <andy.karpov@gmail.com>
-- EU, 2026
--------------------------------------------------------------------------------
library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.std_logic_unsigned.all;
use IEEE.numeric_std.all;

entity esp8266 is
generic(
    CLKS_PER_BIT        : integer := 355 -- clk / 115200 (40.9 MHz)
);
port(
    CLK                 : in std_logic;
    RESET               : in std_logic;

     -- mcu interface
    ESP_UART_RX_DATA    : out std_logic_vector(7 downto 0);
    ESP_UART_RX_WR      : out std_logic;
    ESP_UART_RX_ACK     : in std_logic;
    ESP_UART_TX_DATA    : in std_logic_vector(7 downto 0);
    ESP_UART_TX_WR      : in std_logic;

     -- esp8266 interface
    UART_RX             : in std_logic;
    UART_TX             : out std_logic;
    UART_CTS            : out std_logic
);
end esp8266;

architecture rtl of esp8266 is

component kg_fifo
generic(
    DW : integer := 8;
    AW : integer := 8
);
port(
    clk   : in std_logic;
    reset : in std_logic;
    wr    : in std_logic;
    din   : in std_logic_vector(DW-1 downto 0);
    full  : out std_logic;
    rd    : in std_logic;
    dout  : out std_logic_vector(DW-1 downto 0);
    empty : out std_logic;
    count : out std_logic_vector(AW downto 0)
);
end component;

signal fifo_tx_di          : std_logic_vector(7 downto 0);
signal fifo_tx_do          : std_logic_vector(7 downto 0);
signal fifo_tx_rd_req      : std_logic := '0';
signal fifo_tx_wr_req      : std_logic := '0';
signal fifo_tx_empty       : std_logic;

signal fifo_rx_di          : std_logic_vector(7 downto 0);
signal fifo_rx_do          : std_logic_vector(7 downto 0);
signal fifo_rx_rd_req      : std_logic := '0';
signal fifo_rx_wr_req      : std_logic := '0';
signal fifo_rx_empty       : std_logic;
signal fifo_rx_used        : std_logic_vector(11 downto 0);

signal tx_begin_req        : std_logic := '0';
signal txbusy              : std_logic := '0';
signal txdone              : std_logic := '0';

type txmachine IS (idle, pull_tx_fifo, end_pull_tx_fifo, req_uart_tx, end_req_uart_tx);
type rxmachine IS (idle, pull_rx_fifo, end_pull_rx_fifo, send_rx);

signal txstate : txmachine := idle;
signal rxstate : rxmachine := idle;

begin

FIFO_TX: kg_fifo
generic map(
    DW => 8,
    AW => 8 -- 256 bytes
)
port map(
    clk        => CLK,
    reset      => RESET,
    wr         => fifo_tx_wr_req,
    din        => fifo_tx_di,
    full       => open,
    rd         => fifo_tx_rd_req,
    dout       => fifo_tx_do,
    empty      => fifo_tx_empty,
    count      => open
);

FIFO_RX: kg_fifo
generic map(
    DW => 8,
    AW => 11 -- 2 kbytes
)
port map(
    clk        => CLK,
    reset      => RESET,
    wr         => fifo_rx_wr_req,
    din        => fifo_rx_di,
    full       => open,
    rd         => fifo_rx_rd_req,
    dout       => fifo_rx_do,
    empty      => fifo_rx_empty,
    count      => fifo_rx_used
);

UART_receiver: entity work.uart_rx
generic map(
    g_CLKS_PER_BIT => CLKS_PER_BIT
)
port map(
    i_Clk      => CLK,
    i_RX_Serial => UART_RX,
    o_RX_DV    => fifo_rx_wr_req,
    o_RX_Byte  => fifo_rx_di
);

UART_transmitter: entity work.uart_tx
generic map(
    g_CLKS_PER_BIT => CLKS_PER_BIT
)
port map(
    i_Clk      => CLK,
    i_TX_DV    => tx_begin_req,
    i_TX_Byte  => fifo_tx_do,
    o_TX_Active => txbusy,
    o_TX_Serial => UART_TX,
    o_TX_Done   => txdone
);

fifo_tx_di     <= ESP_UART_TX_DATA;
fifo_tx_wr_req <= ESP_UART_TX_WR;

-- uart -> mcu: a byte read from the fifo (fifo_rx_do has it a clock after the read), held until the ack
process (RESET, CLK)
begin
    if RESET = '1' then
        fifo_rx_rd_req <= '0';
        rxstate <= idle;
        ESP_UART_RX_WR <= '0';
    elsif rising_edge(CLK) then
        case rxstate is
            when idle =>
                if (fifo_rx_empty = '0') then
                    rxstate <= pull_rx_fifo;
                end if;
            when pull_rx_fifo =>
                fifo_rx_rd_req <= '1';
                rxstate <= end_pull_rx_fifo;
            when end_pull_rx_fifo =>
                fifo_rx_rd_req <= '0';
                rxstate <= send_rx;
            when send_rx =>
                ESP_UART_RX_DATA <= fifo_rx_do;
                ESP_UART_RX_WR <= '1';
                if (ESP_UART_RX_ACK = '1') then
                    ESP_UART_RX_WR <= '0';
                    rxstate <= idle;
                end if;
            when others =>
                rxstate <= idle;
        end case;
    end if;
end process;

-- mcu -> uart
process (RESET, CLK)
begin
    if RESET = '1' then
        fifo_tx_rd_req <= '0';
        tx_begin_req <= '0';
        txstate <= idle;
    elsif rising_edge(CLK) then
          tx_begin_req <= '0';
          case txstate is
            when idle =>
                if (fifo_tx_empty = '0' and txbusy = '0') then
                    txstate <= pull_tx_fifo;
                end if;
            when pull_tx_fifo =>
                fifo_tx_rd_req <= '1';
                txstate <= end_pull_tx_fifo;
            when end_pull_tx_fifo =>
                fifo_tx_rd_req <= '0';
                txstate <= req_uart_tx;
            when req_uart_tx =>
                tx_begin_req <= '1';
                txstate <= end_req_uart_tx;
            when end_req_uart_tx =>
                tx_begin_req <= '0';
                if (txdone = '1') then -- wait txdone from transmitter
                    txstate <= idle;
                end if;
            when others => null;
          end case;
    end if;
end process;

UART_CTS <= '1' when fifo_rx_used > 1792 else '0'; -- active 0

end rtl;
