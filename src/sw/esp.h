// The ESP8266 of the board from the menu CPU (firmware periph/esp_serial + esp_at): its uart taken
// from the MCU (MMIO ESP_OWN) while Setup runs, the bytes it sends kept in a ring (esp_pump from the
// main loop), AT commands started and then polled: the reply lines go to a callback until OK,
// ERROR / FAIL or the timeout.
#pragma once
#include <stdint.h>

void esp_own(int on);            // the uart to the CPU (1) or back to the MCU (0)
void esp_pump(void);             // the bytes of the ESP into the ring (often: the main loop)

enum { ESP_AT_IDLE, ESP_AT_BUSY, ESP_AT_OK, ESP_AT_ERROR, ESP_AT_TIMEOUT };
typedef void (*esp_at_line_cb)(const char *line);
void esp_at_start(const char *cmd, uint32_t timeout_ms, esp_at_line_cb on_line);   // "<cmd>\r\n"
uint8_t esp_at_poll(void);       // the state of the last command
