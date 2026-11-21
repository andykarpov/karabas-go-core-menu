// The ESP8266 from the menu CPU (esp.h).
#include <string.h>
#include <stdio.h>
#include "esp.h"
#include "mmio.h"
#include "ui.h"
#include "esp_at_parse.h"

#ifdef HOST
int host_esp_rx(void);           // host/host.c: a fake ESP
void host_esp_tx(uint8_t c);
static int hw_rx(void) { return host_esp_rx(); }
static void hw_tx(uint8_t c) { host_esp_tx(c); }
#else
static int hw_rx(void) { uint32_t v = R_ESP; return (v & 0x100) ? (int) (v & 0xff) : -1; }
static void hw_tx(uint8_t c) { R_ESP = c; }
#endif

#define RING 4096
static uint8_t ring[RING];
static uint32_t head = 0, tail = 0;
static int owned = 0;

void esp_own(int on) {
  owned = on;
  R_ESP_OWN = (uint32_t) on;
  head = tail = 0;
}

void esp_pump(void) {
  if (!owned) return;
  for (int n = 0; n < 64; n++) {
    int c = hw_rx();
    if (c < 0) break;
    if (head - tail < RING) ring[head++ % RING] = (uint8_t) c;
  }
}

static int esp_read(void) { esp_pump(); return head != tail ? ring[tail++ % RING] : -1; }

static void esp_print(const char *s) {
  // the 256 bytes of the fifo of esp8266.vhd: a command is shorter
  while (*s) hw_tx((uint8_t) *s++);
}

// ---- AT (firmware periph/esp_at.c) ----

static uint8_t state = ESP_AT_IDLE;
static esp_at_line_cb line_cb;
static uint32_t timeout, timer;
static char line[160];           // a longer line is cut
static uint8_t line_len;

static void log_line(const char *dir, const char *s) {
  char b[200];
  snprintf(b, sizeof(b), "ESP %s %s\n", dir, s);
  dbg_puts(b);
}

void esp_at_start(const char *cmd, uint32_t timeout_ms, esp_at_line_cb on_line) {
  if (!owned) esp_own(1);
  esp_pump();
  tail = head;                   // what the ESP sent before is dropped
  line_len = 0;
  line_cb = on_line;
  timeout = timeout_ms;
  timer = R_MS;
  state = ESP_AT_BUSY;
  log_line(">>", strncmp(cmd, "AT+CWJAP=", 9) == 0 ? "AT+CWJAP=..." : cmd);   // the password stays out of the log
  esp_print(cmd);
  esp_print("\r\n");
}

static void on_line(void) {
  line[line_len] = 0;
  line_len = 0;
  if (!line[0]) return;
  log_line("<<", line);
  switch (kg_esp_line(line)) {
    case KG_ESP_LINE_OK: state = ESP_AT_OK; break;
    case KG_ESP_LINE_ERROR: state = ESP_AT_ERROR; break;
    default: if (line_cb) line_cb(line);
  }
}

uint8_t esp_at_poll(void) {
  int c;
  while (state == ESP_AT_BUSY && (c = esp_read()) >= 0) {
    if (c == '\r' || c == '\n') on_line();
    else if (c && line_len < sizeof(line) - 1) line[line_len++] = (char) c;
  }
  if (state == ESP_AT_BUSY && R_MS - timer >= timeout) { dbg_puts("ESP: timeout\n"); state = ESP_AT_TIMEOUT; }
  return state;
}
