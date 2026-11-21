// tb_menu_soc: the program of the menu (sw/build/menu.bin) loaded through the LD port as the firmware
// does it (a byte when ld_ready, 25 clocks apart: 333 ns at 75 MHz), then the CPU runs; the debug console
// is printed, the frame after "menu: frame 2" is captured into frame.ppm and checked:
// the glow at the top, black at the bottom, white text pixels, the size 800 x 600.
#include "Vtb_menu_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static Vtb_menu_soc *t;
static uint64_t now_ps = 0, next_cpu = 0, next_pix = 0;
static const uint64_t P_CPU = 13333, P_PIX = 24444; // 75 MHz, 40.91 MHz
static int errors = 0;
static std::string console;
static std::vector<uint32_t> frame(800 * 600);
static int cap_state = 0; // 0 wait, 1 waiting for vs, 2 capturing, 3 done
static int px = 0, py = 0, de_p = 0, vs_p = 0;
static int show_seen = 0;

static void on_pix_edge() {
  // after the posedge of clk_pix: the outputs of the video
  int de = t->de, vs = t->vs;
  if (cap_state == 1 && vs && !vs_p) { cap_state = 2; px = 0; py = 0; }
  else if (cap_state == 2) {
    if (de) {
      if (py < 600 && px < 800) frame[py * 800 + px] = t->rgb;
      if (t->show_o) show_seen++;
      px++;
    }
    if (!de && de_p) { py++; px = 0; if (py == 600) cap_state = 3; }
  }
  de_p = de; vs_p = vs;
}

static void step() {
  // the next edge of either clock
  uint64_t nc = next_cpu, np = next_pix;
  uint64_t n = nc < np ? nc : np;
  now_ps = n;
  if (n == nc) { t->clk_cpu = !t->clk_cpu; next_cpu += P_CPU / 2; }
  if (n == np) { t->clk_pix = !t->clk_pix; next_pix += P_PIX / 2; }
  t->eval();
  if (n == np && t->clk_pix) on_pix_edge();
  if (n == nc && t->clk_cpu && t->dbg_uart_wr) {
    char c = (char) t->dbg_uart;
    putchar(c);
    console += c;
    if (cap_state == 0 && console.find("menu: frame 2") != std::string::npos) cap_state = 1;
  }
}

static void cpu_clocks(int n) {
  for (int i = 0; i < n; i++) {
    do step(); while (!(t->clk_cpu && now_ps == next_cpu - P_CPU / 2));
  }
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const char *bin = argc > 1 ? argv[1] : "../../sw/build/menu.bin";
  FILE *f = fopen(bin, "rb");
  if (!f) { printf("tb_menu_soc: no %s\n", bin); return 1; }
  std::vector<uint8_t> prog;
  int c;
  while ((c = fgetc(f)) != EOF) prog.push_back((uint8_t) c);
  fclose(f);

  t = new Vtb_menu_soc;
  t->reset = 1;
  t->eval();
  cpu_clocks(20);
  t->reset = 0;
  cpu_clocks(20);
  // the loader waits for ld_ready (the clear) before the first packet, as LOAD STATUS
  int waited = 0;
  while (!t->ld_ready) { cpu_clocks(1); waited++; }
  printf("tb_menu_soc: the clear took %d clocks\n", waited);
  t->ld_active = 1;
  for (size_t i = 0; i < prog.size(); i++) {
    if (i % 512 == 0) while (!t->ld_ready) cpu_clocks(1);
    t->ld_addr = (uint32_t) i;
    t->ld_data = prog[i];
    t->ld_wr = 1;
    cpu_clocks(1);
    t->ld_wr = 0;
    cpu_clocks(24);
    if (!t->ld_ready) cpu_clocks(0);
  }
  t->ld_active = 0;
  t->ld_end = 1;
  cpu_clocks(1);
  t->ld_end = 0;
  printf("tb_menu_soc: %zu bytes loaded\n", prog.size());

  uint64_t limit = 40000000; // clocks
  uint64_t n = 0;
  while (cap_state != 3 && n < limit) { cpu_clocks(1000); n += 1000; }
  if (cap_state != 3) { printf("tb_menu_soc: no frame captured (state %d)\n", cap_state); errors++; }
  else {
    FILE *o = fopen("frame.ppm", "wb");
    fprintf(o, "P6 800 600 255\n");
    for (uint32_t p : frame) { fputc(p >> 16, o); fputc((p >> 8) & 255, o); fputc(p & 255, o); }
    fclose(o);
    // checks: the top is the glow (blue ~200), the bottom black, some white pixels
    uint32_t top = frame[5 * 800 + 5], bottom = frame[590 * 800 + 5];
    int white = 0;
    for (uint32_t p : frame) if (p == 0xffffff) white++;
    if ((top & 0xff) < 190 || (top >> 8) != 0) { printf("tb_menu_soc: top pixel %06x, not the glow\n", top); errors++; }
    if (bottom != 0) { printf("tb_menu_soc: bottom pixel %06x, not black\n", bottom); errors++; }
    if (white < 1000) { printf("tb_menu_soc: %d white pixels, no text\n", white); errors++; }
    if (show_seen < 400000) { printf("tb_menu_soc: show only on %d pixels\n", show_seen); errors++; }
    // the frame against the image of the program (the reference drawn by gen_ref.py, if present)
    printf("tb_menu_soc: frame.ppm, %d white pixels, top %06x, %llu clocks\n", white, top, (unsigned long long) n);
  }
  if (console.find("menu: shown") == std::string::npos) { printf("tb_menu_soc: the program did not run\n"); errors++; }
  printf("tb_menu_soc: %d errors\n", errors);
  delete t;
  return errors ? 1 : 0;
}
