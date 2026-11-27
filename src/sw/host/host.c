// The host build of the menu drawing (no CPU, no RTL): the screens of the menu C code drawn into
// arrays, composed as menu_compose does (the gradient, the alpha of the palette) and written as PPM,
// for a quick look at the pictures (the approval frames of a UI step). The registers are an array,
// RPC answers from a fake karabas.ini (the colours and values of the bench) and a fake core list.
//   host/run.sh: build + run in both video modes -> host/out/640/*.png, host/out/800/*.png
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#include "../mmio.h"
#include "../rpc.h"
#include "../ui.h"
#include "../settings.h"
#include "../cores.h"
#include "../browser.h"
#include "../setup.h"
#include "../intro.h"

uint8_t host_fb[2][0x80000];
static uint32_t regs[0x1000];
static uint32_t now_ms = 1000;
static int host_intro = 0;       // argv[2] "intro": the boot intro frame by frame (host_vsync), its MIDI
static int learn_calls = 0;
void host_puts(const char *s) { fputs(s, stdout); }

volatile uint32_t *host_reg(uint32_t off) {
  static uint32_t tmp;
  if (off == 0x1c) { tmp = host_intro ? now_ms : now_ms++; return &tmp; }   // the intro: the time of its frame
  if (off == 0x40) { tmp = now_ms * 1000; return &tmp; }
  if (off >= 0x2000 || off == 0xa0) { tmp = 0; return &tmp; }   // RPC; MIDI: never full
  return &regs[off / 4];
}

// ---- a fake ESP8266: AT 1.7.6, joined to M-WiFinkA, three networks around ----
static char esp_out[4096];
static int esp_out_n = 0, esp_out_at = 0;
static char esp_cmd[200];
static int esp_cmd_n = 0;
static void esp_say(const char *s) { while (*s && esp_out_n < (int) sizeof(esp_out)) esp_out[esp_out_n++] = *s++; }
static int esp_joined = 1;
void host_esp_tx(uint8_t c) {
  if (c == '\n') {
    esp_cmd[esp_cmd_n] = 0;
    if (esp_cmd_n && esp_cmd[esp_cmd_n - 1] == '\r') esp_cmd[--esp_cmd_n] = 0;
    esp_cmd_n = 0;
    const char *c = esp_cmd;
    if (!strcmp(c, "AT+GMR")) esp_say("AT version:1.7.6.0(Jan 24 2022 08:56:02)\r\nSDK version:3.0.6\r\n");
    else if (!strcmp(c, "AT+UART_DEF?")) esp_say("+UART_DEF:115200,8,1,0,2\r\n");
    else if (!strcmp(c, "AT+CWMODE?")) esp_say("+CWMODE:1\r\n");
    else if (!strcmp(c, "AT+CWJAP?")) esp_say(esp_joined ? "+CWJAP:\"M-WiFinkA\",\"aa:bb:cc:dd:ee:ff\",6,-52\r\n" : "No AP\r\n");
    else if (!strcmp(c, "AT+CIFSR")) esp_say(esp_joined ? "+CIFSR:STAIP,\"192.168.1.114\"\r\n" : "+CIFSR:STAIP,\"0.0.0.0\"\r\n");
    else if (!strcmp(c, "AT+CWLAP")) esp_say("+CWLAP:(3,\"M-WiFinkA\",-52,\"aa\",6)\r\n+CWLAP:(0,\"Guest\",-71,\"bb\",1)\r\n+CWLAP:(4,\"\xd0\x94\xd0\xbe\xd0\xbc\",-80,\"cc\",11)\r\n");
    else if (!strncmp(c, "AT+CIPSNTPTIME?", 15)) esp_say("+CIPSNTPTIME:Sat Nov 21 11:59:30 2026\r\n");
    esp_say("\r\nOK\r\n");
    return;
  }
  if (esp_cmd_n < (int) sizeof(esp_cmd) - 1) esp_cmd[esp_cmd_n++] = (char) c;
}
int host_esp_rx(void) { return esp_out_at < esp_out_n ? (uint8_t) esp_out[esp_out_at++] : -1; }

// the setup loop for a while (the AT steps answer as the main loop runs them)
static void run_ms(int ms) { uint32_t end = now_ms + ms; while (now_ms < end) setup_tick(); }

// ---- the fake firmware ----
static const char *ini[][3] = {
  { "setup", "debug", "1" }, { "setup", "dvi_only", "0" }, { "setup", "core_sort", "name" }, { "setup", "tz", "+01:00" },
  { "matrix", "enabled", "1" }, { "matrix", "brightness", "1" },
  { "autoload", "enabled", "0" }, { "autoload", "timeout", "10" }, { "autoload", "core", "profi" },
  { "ft812", "time", "1" }, { "ft812", "date", "1" }, { "ft812", "char", "1" }, { "ft812", "mouse_speed", "5" },
  { "ft812", "sound", "1" }, { "ft812", "click", "1" }, { "ft812", "video_mode", "0" },
  { "ft812", "color_bg", "000608" }, { "ft812", "color_gradient", "006a80" }, { "ft812", "color_button", "0e2228" },
  { "ft812", "color_active", "00a0b8" }, { "ft812", "color_text", "e0fcff" }, { "ft812", "color_text_active", "ffffff" },
  { "ft812", "color_copyright", "5a8088" },
  { "groups", "c64", "My cores" }, { "groups", "zxuno", "My cores" }, { "groups", "bbc", "" },   // the user's
};
static const char *core_list[][3] = {   // file, name, id
  { "boot.kg", "Boot Menu", "boot" }, { "archie.kg", "Acorn Archimedes", "archie" }, { "agon.kg", "Agon", "agon" },
  { "alf.kg", "Alf", "alf" }, { "cpc.kg", "Amstrad CPC", "cpc" }, { "apple2.kg", "Apple //e", "apple2" },
  { "atari2600.kg", "Atari 2600", "atari2600" }, { "atari800.kg", "Atari 800XL", "atari800" }, { "atarist.kg", "Atari ST", "atarist" },
  { "bbc.kg", "BBC Micro", "bbc" }, { "bk.kg", "BK-0011M", "bk" }, { "profi.kg", "Karabas Pro +", "profi" },
  { "c64.kg", "Commodore 64", "c64" }, { "msx.kg", "MSX", "msx" }, { "nes.kg", "NES", "nes" },
  { "sega.kg", "Sega Mega Drive", "sega" }, { "vic20.kg", "VIC-20", "vic20" }, { "zxuno.kg", "ZX-Uno", "zxuno" },
  { "tsconf.kg", "TS-Conf +", "tsconf" }, { "uknc.kg", "UKNC MC-0511", "uknc" }, { "doom.kg", "Doom", "doom" },
  { "x.kg", "Unknown core", "x" },
};
#define NCORES ((int) (sizeof(core_list) / sizeof(core_list[0])))

int rpc_enabled(void) { return 1; }
void rpc_launch(const char *file) { printf("launch %s\n", file); exit(0); }

static const char *arg(const uint8_t *in, uint16_t len, int k) {
  uint16_t p = 0;
  for (int i = 0; i < k; i++) { while (p < len && in[p]) p++; if (p >= len) return 0; p++; }
  return p < len ? (const char *) in + p : 0;
}

int rpc_call(uint8_t svc, uint8_t fn, const void *in_, uint16_t in_len, void *out_, uint16_t out_max, uint16_t *out_len, uint32_t timeout_ms) {
  const uint8_t *in = in_;
  uint8_t *out = out_;
  (void) out_max; (void) timeout_ms;
  *out_len = 0;
  if (svc == RPC_SYS && fn == 0) { memcpy(out, "DEVBUILD", 8); out[8] = getenv("HOST_BOARD") ? (uint8_t) atoi(getenv("HOST_BOARD")) : 3; out[9] = 1; out[10] = 15; out[11] = 0; *out_len = 12; return RPC_OK; }
  if (svc == RPC_SYS && fn == 1) {
    uint8_t t[8] = { 2026 & 255, 2026 >> 8, 11, 21, 12, 59, (uint8_t) (now_ms / 1000 % 60), 6 };
    memcpy(out, t, 8); *out_len = 8; return RPC_OK;
  }
  if (svc == RPC_SYS && fn == 2) return RPC_OK;
  if (svc == RPC_CFG && fn == 0) {   // GET (section, key)... -> 1 + value / 0 + ""
    int n = 0;
    for (int k = 1; arg(in, in_len, k) && arg(in, in_len, k + 1); k += 2) {
      const char *sec = arg(in, in_len, k), *key = arg(in, in_len, k + 1), *v = 0;
      for (unsigned i = 0; i < sizeof(ini) / sizeof(ini[0]); i++)
        if (!strcmp(ini[i][0], sec) && !strcmp(ini[i][1], key)) v = ini[i][2];
      out[n++] = v ? 1 : 0;
      strcpy((char *) out + n, v ? v : ""); n += strlen(v ? v : "") + 1;
    }
    *out_len = n; return RPC_OK;
  }
  if (svc == RPC_CFG) {
    printf("CFG %u:", fn);
    for (int k = 1; arg(in, in_len, k); k++) printf(" %s", arg(in, in_len, k));
    printf("\n");
    return RPC_OK;
  }
  if (svc == RPC_CORES && fn == 0) { out[0] = NCORES; out[1] = 0; *out_len = 2; return RPC_OK; }
  if (svc == RPC_CORES && fn == 1) {
    int i = in[0], n = 0;
    const char *f[5] = { core_list[i][0], core_list[i][1], "26112100", core_list[i][2], "1" };
    for (int k = 0; k < 5; k++) { strcpy((char *) out + n, f[k]); n += strlen(f[k]) + 1; }
    *out_len = n; return RPC_OK;
  }
  // HID: port 1 a pad with a driver ini, port 2 free; the learning goes a step a call
  if (svc == RPC_HID && fn == 0) {
    static const uint8_t p[] = { 1, 0x10, 0x08, 0x01, 0x00 };
    int n = 0;
    memcpy(out, p, 5); n = 5; strcpy((char *) out + n, "/joy/0810_0001.ini"); n += 19;
    memset(out + n, 0, 6); n += 6;
    *out_len = n; return RPC_OK;
  }
  if (svc == RPC_HID && fn == 2) { learn_calls = 0; return RPC_OK; }
  if (svc == RPC_HID && fn == 4) return RPC_OK;
  if (svc == RPC_HID && fn == 3) {
    // the calls: select, rest, then the buttons a call each up to B (5 mapped), pressing B
    int c = learn_calls++;
    uint8_t st = c < 2 ? 1 : c < 4 ? 3 : 4, btn = c < 4 ? 0 : (c - 4 > 5 ? 5 : c - 4);
    uint16_t mapped = (1 << btn) - 1;
    uint8_t o[12] = { st, btn, (uint8_t) (btn - 1), 7, 0x10, 0x08, 0x01, 0x00, (uint8_t) mapped, (uint8_t) (mapped >> 8), 0, 0 };
    memcpy(out, o, 12);
    strcpy((char *) out + 12, "b3+");   // the rule of the last one
    *out_len = 12 + 4 + 1;
    out[16] = 0;
    return RPC_OK;
  }
  if (svc == RPC_CORES && fn == 3) { out[0] = 0; out[1] = 0; *out_len = 2; return RPC_OK; }
  return RPC_UNKNOWN;
}

// ---- the picture as menu_compose makes it ----
static const char *out_dir = "host/out/640";
static void dump(const char *name) {
  int b = regs[0x04 / 4] & 1, g_lines = scr_h * 3 / 4;
  uint32_t top = regs[0x18 / 4], bot = regs[0x20 / 4];
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
  FILE *o = fopen(path, "wb");
  fprintf(o, "P6 %d %d 255\n", scr_w, scr_h);
  for (int y = 0; y < scr_h; y++) {
    int t = y >= g_lines ? 256 : y * 256 / g_lines;
    for (int x = 0; x < scr_w; x++) {
      uint32_t e = regs[(0x400 + 0x400 * b) / 4 + host_fb[b][y * FB_STRIDE + x]];
      if (host_fb[b][y * FB_STRIDE + x] == 0) e = 0;
      int a = e >> 28;
      if (a == 15) a = 16;
      for (int s = 16; s >= 0; s -= 8) {
        int g = (int) ((top >> s) & 255) + (((int) ((bot >> s) & 255) - (int) ((top >> s) & 255)) * t >> 8);
        int c = (e >> s) & 255;
        fputc((c * a + g * (16 - a)) / 16, o);
      }
    }
  }
  fclose(o);
  printf("frame %s\n", name);
}

// ---- the boot intro: a frame a vsync (60 Hz), the MIDI kept with the time -> intro.mid ----
static int intro_frame = 0;
static uint32_t intro_t0;
void host_vsync(void) {
  char n[32];
  snprintf(n, sizeof(n), "f%04d", intro_frame++);
  dump(n);
  now_ms = intro_t0 + (uint32_t) (intro_frame * 50 / 3);
}
static uint8_t mid[65536];
static int mid_n = 0;
static uint32_t mid_last = 0;
void host_midi(const uint8_t *msg, int len) {
  if (!host_intro || mid_n > (int) sizeof(mid) - 16) return;
  uint32_t d = now_ms - mid_last, v = d & 0x7F;   // ticks = ms (division 500, 120 bpm)
  mid_last = now_ms;
  while (d >>= 7) v = (v << 8) | 0x80 | (d & 0x7F);
  for (;;) { mid[mid_n++] = (uint8_t) v; if (v & 0x80) v >>= 8; else break; }
  for (int i = 0; i < len; i++) mid[mid_n++] = msg[i];
}
static void midi_write(const char *path) {
  FILE *o = fopen(path, "wb");
  int n = mid_n + 4;
  const uint8_t hdr[] = { 'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xF4, 'M','T','r','k' };
  fwrite(hdr, 1, sizeof(hdr), o);
  uint8_t l[4] = { (uint8_t) (n >> 24), (uint8_t) (n >> 16), (uint8_t) (n >> 8), (uint8_t) n };
  fwrite(l, 1, 4, o);
  fwrite(mid, 1, mid_n, o);
  const uint8_t eot[] = { 0, 0xFF, 0x2F, 0 };
  fwrite(eot, 1, 4, o);
  fclose(o);
}

static void key(uint8_t k) { key_ev_t e = { 0 }; e.key = k; if (setup_active) setup_key(&e); else browser_key(&e); }
static void keys(const char *s) {   // a script: u d l r e x(esc) t(tab) b(bksp) s, digits
  for (; *s; s++) {
    uint8_t k = *s == 'u' ? K_UP : *s == 'd' ? K_DOWN : *s == 'l' ? K_LEFT : *s == 'r' ? K_RIGHT : *s == 'e' ? K_ENTER :
                *s == 'x' ? K_ESC : *s == 't' ? K_TAB : *s == 'b' ? K_BKSP : *s == 's' ? K_S :
                *s >= '1' && *s <= '9' ? K_1 + (*s - '1') : *s == '0' ? K_0 : 0;
    key(k);
  }
}

static void type(const char *s) { for (; *s; s++) { key_ev_t e = { 0 }; e.key = 0x04; e.ch = *s; browser_key(&e); } }

static void stuck(int sig) {   // a hang: where (addr2line on the addresses)
  void *bt[16];
  (void) sig;
  backtrace_symbols_fd(bt, backtrace(bt, 16), 2);
  _exit(1);
}

int main(int argc, char **argv) {
  // argv[1]: the video mode of the fake karabas.ini, 640 (default) or 800
  if (argc > 1 && !strcmp(argv[1], "800")) {
    out_dir = "host/out/800";
    for (int i = 0; i < (int) (sizeof(ini) / sizeof(ini[0])); i++) if (!strcmp(ini[i][1], "video_mode")) ini[i][2] = "1";
  }
  setvbuf(stdout, 0, _IONBF, 0);
  signal(SIGALRM, stuck);
  alarm(20);
  gfx_init();
  settings_read();
  cores_read();
  video_set(set_val[S_VIDEO]);
  printf("host: %d x %d\n", scr_w, scr_h);
  if (argc > 2 && !strcmp(argv[2], "intro")) {
    static char dir[64];
    snprintf(dir, sizeof(dir), "host/out/intro%d", scr_w);
    out_dir = dir;
    alarm(120);
    host_intro = 1; intro_t0 = now_ms; mid_last = now_ms;
    intro_play();
    host_intro = 0;
    char mp[80]; snprintf(mp, sizeof(mp), "%s/intro.mid", dir); midi_write(mp);
    browser_start(); browser_open(); dump("menu");
    printf("host: intro %d frames\n", intro_frame);
    return 0;
  }
  browser_start();
  msc_mode = 1; browser_msc_draw();              dump("00a_msc"); msc_mode = 0;
  browser_open();                                dump("00_browser");
  // the mouse on the list: a hover on and off leaves the screen as it was (no traces), the pager by clicks
  {
    static uint8_t snap[0x80000];
    int b = regs[0x04 / 4] & 1, bad = 0;
    ptr.visible = 1; ptr.x = 700; ptr.y = 300; browser_mouse(M_MOVED);
    memcpy(snap, host_fb[b], sizeof(snap));
    const int at[4][2] = { { scr_w / 2, 88 + 40 + 10 }, { 36, 104 }, { scr_w / 2 + 80, scr_h - 40 }, { scr_w / 2 - 80, scr_h - 40 } };
    const char *names[4] = { "00d_hover_core", "00e_hover_setup", "00f_hover_next", "00g_hover_prev" };
    for (int i = 0; i < 4; i++) {
      ptr.x = at[i][0]; ptr.y = at[i][1]; browser_mouse(M_MOVED); dump(names[i]);
      ptr.x = 700; ptr.y = 300; browser_mouse(M_MOVED);
      int d = 0;
      for (int k = 0; k < (int) sizeof(snap); k++) d += snap[k] != host_fb[b][k];
      if (d) { printf("host: %s left %d pixels changed\n", names[i], d); bad++; }
    }
    printf("host: hover traces: %s\n", bad ? "BAD" : "none");
    ptr.x = scr_w / 2 + 80; ptr.y = scr_h - 40; browser_mouse(M_LEFT);  dump("00h_click_next");
    browser_mouse(M_LEFT);                            dump("00i_click_next_last");
    ptr.x = scr_w / 2 - 80; browser_mouse(M_LEFT);
    if (scr_h == 480) browser_mouse(M_LEFT);         // three pages: back to the first
    dump("00j_click_prev");
    ptr.x = 700; ptr.y = 300; browser_mouse(M_MOVED); ptr.visible = 0; browser_mouse(M_MOVED);
  }
  // the groups: Computers open, its next page, back to the top
  keys("de");                                    dump("00k_group_open");
  keys("r");                                     dump("00l_group_page2");
  keys("x");                                     dump("00m_group_closed");
  // a core moved (G): into a new group (alone in it: shown at the top), then back to Computers
  keys("e");  type("g");                         dump("00n_move");
  ptr.visible = 1; ptr.x = scr_w / 2; ptr.y = scr_h / 2 + 66; browser_mouse(M_MOVED); dump("00n1_move_hover");
  ptr.visible = 0; browser_mouse(M_MOVED);
  key(K_HOME);                                   dump("00n2_move_new_row");
  keys("e"); type("Fav");                        dump("00o_move_new");
  keys("e");                                     dump("00p_moved");
  type("g"); key(K_HOME); keys("ddd");           dump("00q_move_back");
  keys("e");                                     dump("00r_moved_back");
  keys("x"); key(K_HOME);
  keys("l");                                     dump("00b_setup_focus");   // Left on the first core
  keys("r");                                     dump("00c_setup_unfocus");
  keys("le");                                    dump("01_tabs_system");    // Enter on the Setup button
  keys("e");                                     dump("02_page_system");
  keys("xde");                                   dump("03_page_autoload");
  keys("xde");                                   dump("04_page_display");
  // the video mode tried: the other mode and its dialog, then back (Esc) to the one of karabas.ini
  keys("r");                                     dump("04a_mode_try");
  keys("x");                                     dump("04b_mode_back");
  keys("xde");                                   dump("05_page_sound");
  keys("xde");                                   dump("06_page_theme");
  keys("de");                                    dump("07_color_editor");
  keys("xxde");                                  dump("08_page_clock");
  keys("xd");                                    dump("09_tabs_wifi");
  keys("d");                                     dump("10_tabs_gamepad");
  keys("d");                                     dump("11_tabs_save");
  keys("uuuuuuuuedddddde");                       dump("12_matrix_effects");
  keys("r");                                     dump("13_matrix_changed");
  keys("xxx");                                   dump("14_dialog");
  keys("le");                                    dump("15_back_dropped");
  // the mouse: a hover on Autoload, a hovered row
  keys("s");
  ptr.visible = 1; ptr.x = 80; ptr.y = 88 + (scr_h == 480 ? 32 : 40) + 10; setup_mouse(M_MOVED); dump("16_hover_tab");
  keys("de"); ptr.x = 400; ptr.y = 128 + 36 + 10; setup_mouse(M_MOVED); dump("17_hover_row");
  // Wi-Fi: the status as soon as the section is chosen, the network list, a password
  keys("xxxs" "ddddd" "d");   run_ms(3000);      dump("19_wifi_status");
  keys("e" "e");              run_ms(3000);      dump("20_wifi_list");
  keys("e");                                     dump("21_wifi_pass");
  { key_ev_t e = { 0 }; e.key = 0x13; e.ch = 'p'; setup_key(&e); e.ch = 'w'; setup_key(&e); }   dump("22_wifi_typed");
  keys("xxx" "d");                               dump("23_wifi_menu2");
  // Clock: NTP sync
  keys("uu" "e" "dddd" "e"); run_ms(300);        dump("24_ntp_busy");
  run_ms(2500);                                  dump("25_ntp_done");
  // Gamepad: the ports, the learning, the test
  keys("x" "dd");             run_ms(600);       dump("26_pad_list");
  keys("e" "e");              run_ms(1200);      dump("27_pad_learning");
  keys("x" "r" "e");          regs[0x38 / 4] = 2 | 64 | (32 << 16);  run_ms(600);  dump("28_pad_test");
  regs[0x38 / 4] = 0;
  keys("x" "x");
  // Save: the changed values in one SET, then SAVE
  keys("xxx");                                   // back to the list (the hover runs left the page open)
  keys("serx" "dddddddd" "e");                   dump("18_saved");
  return 0;
}
