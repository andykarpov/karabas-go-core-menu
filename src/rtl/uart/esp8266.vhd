--------------------------------------------------------------------------------
-- ESP8266 SPI wrapper
--
-- @author Andy Karpov <andy.karpov@gmail.com>
-- EU, 2026
--------------------------------------------------------------------------------
library IEEE; 
use IEEE.std_logic_1164.all; 
use IEEE.std_logic_unsigned.all;
use IEEE.numeric_std.all; 

entity esp8266 is
port(
    CLK                 : in std_logic;
    RESET               : in std_logic;

     -- mcu interface
    ESP_UART_RX_DATA    : out std_logic_vector(7 downto 0);
    ESP_UART_RX_WR      : out std_logic;
    ESP_UART_TX_DATA    : in std_logic_vector(7 downto 0);
    ESP_UART_TX_WR      : in std_logic;

     -- esp8266 interface
    UART_RX             : in std_logic;
    UART_TX             : out std_logic;
    UART_CTS            : out std_logic        
);
end esp8266;

architecture rtl of esp8266 is

signal fifo_tx_di          : std_logic_vector(7 downto 0);
signal fifo_tx_do          : std_logic_vector(7 downto 0);
signal fifo_tx_rd_req      : std_logic := '0';
signal fifo_tx_wr_req      : std_logic := '0';
signal fifo_tx_used        : std_logic_vector(7 downto 0) := (others => '0');

signal fifo_rx_di          : std_logic_vector(7 downto 0);
signal fifo_rx_do          : std_logic_vector(7 downto 0);
signal fifo_rx_rd_req      : std_logic := '0';
signal fifo_rx_wr_req      : std_logic := '0';
signal fifo_rx_used        : std_logic_vector(10 downto 0) := (others => '0');

signal tx_begin_req        : std_logic := '0';
signal txbusy              : std_logic := '0';
signal txdone              : std_logic := '0';

type txmachine IS (idle, pull_tx_fifo, end_pull_tx_fifo, req_uart_tx, end_req_uart_tx);
type rxmachine IS (idle, pull_rx_fifo, end_pull_rx_fifo);

signal txstate : txmachine := idle; 
signal rxstate : rxmachine := idle;

begin

FIFO_TX: entity work.fifo
generic map(
    ADDR_WIDTH => 8, -- 256 bytes
    DATA_WIDTH => 8
)
port map(
    clk        => CLK,
    reset      => reset,
    wr         => fifo_tx_wr_req,
    din        => fifo_tx_di,
    rd         => fifo_tx_rd_req,
    dout       => fifo_tx_do,
    data_count => fifo_tx_used
);

FIFO_RX: entity work.fifo
generic map(
    ADDR_WIDTH => 11, -- 2 kbytes
    DATA_WIDTH => 8
)
port map(
    clk        => CLK,
    reset      => reset,
    wr         => fifo_rx_wr_req,
    din        => fifo_rx_di,
    rd         => fifo_rx_rd_req,
    dout       => fifo_rx_do,
    data_count => fifo_rx_used     
);

UART_receiver: entity work.uart_rx
port map(
    i_Clk      => CLK,
    i_RX_Serial => UART_RX,
    o_RX_DV    => fifo_rx_wr_req,
    o_RX_Byte  => fifo_rx_di
);

UART_transmitter: entity work.uart_tx
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

process (RESET, CLK)
begin
    if RESET = '1' then
        fifo_rx_rd_req <= '0';
        rxstate <= idle;
        ESP_UART_RX_WR <= '0';
    elsif rising_edge(CLK) then
        ESP_UART_RX_WR <= '0';
        case rxstate is
            when idle => 
                if (fifo_rx_used > 0) then 
                    rxstate <= pull_rx_fifo;
                end if;
            when pull_rx_fifo => 
                fifo_rx_rd_req <= '1';
                rxstate <= end_pull_rx_fifo;
            when end_pull_rx_fifo =>
                fifo_rx_rd_req <= '0';
                ESP_UART_RX_DATA <= fifo_rx_do;
                ESP_UART_RX_WR <= '1';
                rxstate <= idle;
            when others =>
                rxstate <= idle;
        end case;
    end if;
end process;

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
                if (fifo_tx_used > 0 and txbusy = '0') then 
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
