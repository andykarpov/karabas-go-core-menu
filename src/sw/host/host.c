// The host build of the menu drawing (no CPU, no RTL): the screens of the menu C code drawn into
// arrays, composed as menu_compose does (the gradient, the alpha of the palette) and written as PPM,
// for a quick look at the pictures (the approval frames of a UI step). The registers are an array,
// RPC answers from a fake karabas.ini (the colours and values of the bench) and a fake core list.
//   host/run.sh: build + run -> host/out/*.png
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

uint8_t host_fb[2][0x80000];
static uint32_t regs[0x1000];
static uint32_t now_ms = 1000;

volatile uint32_t *host_reg(uint32_t off) {
  static uint32_t tmp;
  if (off == 0x1c) { tmp = now_ms++; return &tmp; }
  if (off == 0x40) { tmp = now_ms * 1000; return &tmp; }
  if (off >= 0x2000 || off == 0xa0) { tmp = 0; return &tmp; }   // RPC; MIDI: never full
  return &regs[off / 4];
}

// ---- the fake firmware ----
static const char *ini[][3] = {
  { "setup", "debug", "1" }, { "setup", "dvi_only", "0" }, { "setup", "core_sort", "name" }, { "setup", "tz", "+01:00" },
  { "matrix", "enabled", "1" }, { "matrix", "brightness", "1" },
  { "autoload", "enabled", "0" }, { "autoload", "timeout", "10" }, { "autoload", "core", "pro" },
  { "ft812", "time", "1" }, { "ft812", "date", "1" }, { "ft812", "char", "1" }, { "ft812", "mouse_speed", "5" },
  { "ft812", "sound", "1" }, { "ft812", "click", "1" },
  { "ft812", "color_bg", "000608" }, { "ft812", "color_gradient", "006a80" }, { "ft812", "color_button", "0e2228" },
  { "ft812", "color_active", "00a0b8" }, { "ft812", "color_text", "e0fcff" }, { "ft812", "color_text_active", "ffffff" },
  { "ft812", "color_copyright", "5a8088" },
};
static const char *core_list[][3] = {   // file, name, id
  { "boot.kg", "Boot Menu", "boot" }, { "archie.kg", "Acorn Archimedes", "archie" }, { "agon.kg", "Agon", "agon" },
  { "alf.kg", "Alf", "alf" }, { "cpc.kg", "Amstrad CPC", "cpc" }, { "apple2.kg", "Apple //e", "apple2" },
  { "a2600.kg", "Atari 2600", "a2600" }, { "a800.kg", "Atari 800XL", "a800" }, { "atarist.kg", "Atari ST", "atarist" },
  { "bbc.kg", "BBC Micro", "bbc" }, { "bk.kg", "BK-0011M", "bk" }, { "pro.kg", "Karabas Pro +", "pro" },
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
  if (svc == RPC_SYS && fn == 0) { memcpy(out, "DEVBUILD", 8); *out_len = 12; return RPC_OK; }
  if (svc == RPC_SYS && fn == 1) {
    uint8_t t[8] = { 2026 & 255, 2026 >> 8, 11, 21, 12, 59, (uint8_t) (now_ms / 1000 % 60), 6 };
    memcpy(out, t, 8); *out_len = 8; return RPC_OK;
  }
  if (svc == RPC_SYS && fn == 2) return RPC_OK;
  if (svc == RPC_CFG && fn == 0) {
    const char *sec = arg(in, in_len, 1), *key = arg(in, in_len, 2);
    for (unsigned i = 0; i < sizeof(ini) / sizeof(ini[0]); i++)
      if (!strcmp(ini[i][0], sec) && !strcmp(ini[i][1], key)) { strcpy((char *) out, ini[i][2]); *out_len = strlen(ini[i][2]) + 1; return RPC_OK; }
    return RPC_NOT_FOUND;
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
  if (svc == RPC_CORES && fn == 3) { out[0] = 0; out[1] = 0; *out_len = 2; return RPC_OK; }
  return RPC_UNKNOWN;
}

// ---- the picture as menu_compose makes it ----
static void dump(const char *name) {
  int b = regs[0x04 / 4] & 1;
  uint32_t top = regs[0x18 / 4], bot = regs[0x20 / 4];
  char path[256];
  snprintf(path, sizeof(path), "host/out/%s.ppm", name);
  FILE *o = fopen(path, "wb");
  fprintf(o, "P6 800 600 255\n");
  for (int y = 0; y < 600; y++) {
    int t = y >= 450 ? 256 : y * 256 / 450;
    for (int x = 0; x < 800; x++) {
      uint32_t e = regs[(0x400 + 0x400 * b) / 4 + host_fb[b][y * 800 + x]];
      if (host_fb[b][y * 800 + x] == 0) e = 0;
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

static void key(uint8_t k) { key_ev_t e = { 0 }; e.key = k; if (setup_active) setup_key(&e); else browser_key(&e); }
static void keys(const char *s) {   // a script: u d l r e x(esc) t(tab) b(bksp) s, digits
  for (; *s; s++) {
    uint8_t k = *s == 'u' ? K_UP : *s == 'd' ? K_DOWN : *s == 'l' ? K_LEFT : *s == 'r' ? K_RIGHT : *s == 'e' ? K_ENTER :
                *s == 'x' ? K_ESC : *s == 't' ? K_TAB : *s == 'b' ? K_BKSP : *s == 's' ? K_S :
                *s >= '1' && *s <= '9' ? K_1 + (*s - '1') : *s == '0' ? K_0 : 0;
    key(k);
  }
}

static void stuck(int sig) {   // a hang: where (addr2line on the addresses)
  void *bt[16];
  (void) sig;
  backtrace_symbols_fd(bt, backtrace(bt, 16), 2);
  _exit(1);
}

int main(int argc, char **argv) {
  (void) argc; (void) argv;
  setvbuf(stdout, 0, _IONBF, 0);
  signal(SIGALRM, stuck);
  alarm(20);
  gfx_init();
  settings_read();
  cores_read();
  browser_start();
  browser_open();                                dump("00_browser");
  keys("s");                                     dump("01_tabs_system");
  keys("e");                                     dump("02_page_system");
  keys("xde");                                   dump("03_page_autoload");
  keys("xde");                                   dump("04_page_display");
  keys("xde");                                   dump("05_page_sound");
  keys("xde");                                   dump("06_page_theme");
  keys("de");                                    dump("07_color_editor");
  keys("xxde");                                  dump("08_page_clock");
  keys("xd");                                    dump("09_tabs_wifi");
  keys("d");                                     dump("10_tabs_gamepad");
  keys("d");                                     dump("11_tabs_save");
  keys("uuuuuuuueddddde");                       dump("12_matrix_effects");
  keys("r");                                     dump("13_matrix_changed");
  keys("xxx");                                   dump("14_dialog");
  keys("le");                                    dump("15_back_dropped");
  // the mouse: a hover on Autoload, a hovered row
  keys("s");
  ptr.visible = 1; ptr.x = 80; ptr.y = 88 + 40 + 10; setup_mouse(M_MOVED); dump("16_hover_tab");
  keys("de"); ptr.x = 400; ptr.y = 128 + 36 + 10; setup_mouse(M_MOVED); dump("17_hover_row");
  return 0;
}
