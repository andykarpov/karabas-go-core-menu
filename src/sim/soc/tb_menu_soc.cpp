// tb_menu_soc: the menu program (sw/build/menu.bin) loaded through the LD port as the firmware does
// it, then run against a model of the MCU on the link side of kg_l2_rpc: HELLO, the answers of
// CORES SCAN / ITEM / AUTOLOAD (three cores), the keys Down and Enter, and the LAUNCH of the second
// core after the clear of the SRAM. A frame of the list is captured into frame.ppm and checked.
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

static void on_pix_edge() {
  int de = t->de, vs = t->vs;
  if (cap_state == 1 && vs && !vs_p) { cap_state = 2; px = 0; py = 0; }
  else if (cap_state == 2) {
    if (de) { if (py < 600 && px < 800) frame[py * 800 + px] = t->rgb; px++; }
    if (!de && de_p) { py++; px = 0; if (py == 600) cap_state = 3; }
  }
  de_p = de; vs_p = vs;
}

static void step() {
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
  }
}

// one rising edge of clk_cpu (the inputs set before it are taken at it)
static void cpu_clock() {
  do step(); while (!(t->clk_cpu && now_ps == next_cpu - P_CPU / 2));
}
static void cpu_clocks(int n) { for (int i = 0; i < n; i++) cpu_clock(); }

// ---- the MCU model: the byte bus of kg_l2_link for target 08 ----
static void bus_idle() { t->l_stb = 0; t->l_rq = 0; }
// a byte of the mcu at payload position n; with it, the answer of position n + 2 is asked
static uint8_t bus_byte(int n, uint8_t d, bool ask = true) {
  t->l_stb = 1; t->l_n = n; t->l_d = d;
  t->l_rq = ask; t->l_rq_m = n + 2;
  t->eval();
  uint8_t q = t->l_q;
  cpu_clock();
  bus_idle();
  cpu_clocks(6); // the next byte comes later (8 sck periods)
  return q;
}
static void packet_begin(uint8_t cmd) {
  t->l_sel = 1; t->l_cmd = cmd;
  // the command byte: the answers of payload positions 0 and 1 (rq with the target / command bytes)
  t->l_rq = 1; t->l_rq_m = 1; t->eval();
  uint8_t q1 = t->l_q; (void) q1;
  cpu_clock(); bus_idle(); cpu_clocks(6);
}
static std::vector<uint8_t> rx_q; // the answers from position 1 on
// reads n answer bytes of a command without parameters (positions 1 .. n): the first with the
// command byte, the next with the stb of byte m - 2
static void read_answers(uint8_t cmd, int n) {
  rx_q.clear();
  t->l_sel = 1; t->l_cmd = cmd;
  t->l_rq = 1; t->l_rq_m = 1; t->eval();
  rx_q.push_back(t->l_q);
  cpu_clock(); bus_idle(); cpu_clocks(6);
  for (int m = 2; m <= n; m++) rx_q.push_back(bus_byte(m - 2, 0));
  t->l_sel = 0; cpu_clocks(4);
}

static void hello() {
  t->l_sel = 1; t->l_cmd = 1;
  bus_byte(0, 1); bus_byte(1, 0); bus_byte(2, 0); bus_byte(3, 0); bus_byte(4, 0);
  t->l_sel = 0; cpu_clocks(4);
}

struct req_t { uint8_t id, svc, fn; std::vector<uint8_t> data; };
static bool read_req(req_t &r) {
  read_answers(2, 3); // STATUS: flags, len
  if (!(rx_q[0] & 1)) return false;
  int len = rx_q[1] | (rx_q[2] << 8);
  read_answers(3, 5 + len + 2);
  r.id = rx_q[0]; r.svc = rx_q[1]; r.fn = rx_q[2];
  int l2 = rx_q[3] | (rx_q[4] << 8);
  uint16_t sum = 0;
  for (int i = 0; i < 5 + len; i++) sum += rx_q[i];
  uint16_t got = rx_q[5 + len] | (rx_q[6 + len] << 8);
  if (l2 != len || sum != got) { printf("tb_menu_soc: bad REQ (len %d/%d sum %04x/%04x)\n", l2, len, got, sum); errors++; return false; }
  r.data.assign(rx_q.begin() + 5, rx_q.begin() + 5 + len);
  return true;
}
static bool respond(uint8_t id, uint8_t st, const std::vector<uint8_t> &d) {
  std::vector<uint8_t> b = { id, st, (uint8_t) (d.size() & 255), (uint8_t) (d.size() >> 8) };
  b.insert(b.end(), d.begin(), d.end());
  uint16_t sum = 0;
  for (uint8_t x : b) sum += x;
  b.push_back(sum & 255); b.push_back(sum >> 8); b.push_back(0);
  t->l_sel = 1; t->l_cmd = 4;
  uint8_t ack = 1;
  int ack_m = (int) d.size() + 7;
  for (size_t i = 0; i < b.size(); i++) {
    uint8_t q = bus_byte((int) i, b[i]);
    if ((int) i + 2 == ack_m) ack = q;
  }
  t->l_sel = 0; cpu_clocks(4);
  return ack == 0;
}
static void put_s(std::vector<uint8_t> &v, const char *s) { while (*s) v.push_back((uint8_t) *s++); v.push_back(0); }

#define NCORES 15
static const char *files[NCORES] = { "a.kg", "b.kg", "c.kg", "d.kg", "e.kg", "f.kg", "g.kg", "h.kg", "i.kg",
  "j.kg", "k.kg", "l.kg", "m.kg", "n.kg", "o.kg" };
static const char *names[NCORES] = { "Acorn Archimedes", "Agon", "Alf", "Amstrad CPC", "Apple //e", "Atari 2600",
  "Atari 800XL", "Atari ST", "BBC Micro", "BK-0011M", "\xd0\xa1\xd0\xbf\xd0\xb5\xd1\x86\xd0\xb8\xd0\xb0\xd0\xbb\xd0\xb8\xd1\x81\xd1\x82",
  "Commodore 64", "Doom", "MSX2", "ZX Spectrum Next" };
static std::string launched;
static int served = 0;

static void serve() {
  req_t r;
  if (!read_req(r)) return;
  std::vector<uint8_t> out;
  uint8_t st = 0;
  served++;
  if (r.svc == 2 && r.fn == 0) { out = { NCORES, 0 }; }
  else if (r.svc == 0 && r.fn == 0) { for (const char *c = "26112103"; *c; c++) out.push_back(*c); out.insert(out.end(), { 3, 1, 7, 0 }); }
  else if (r.svc == 0 && r.fn == 1) { out = { 0xEA, 0x07, 11, 21, 2, 34, 22, 6 }; }  // 21/11/2026 02:34:22
  else if (r.svc == 1 && r.fn == 0) {
    // CFG GET file, section, key: TB_LOOK="key=value,key=value" gives [ft812] keys, the rest are not
    // there (the defaults of the menu)
    const char *key = (const char *) r.data.data() + 2 + strlen((const char *) r.data.data() + 2) + 1;
    const char *look = getenv("TB_LOOK");
    st = 3;
    std::string k = std::string(key) + "=";
    if (look) {
      std::string l = std::string(",") + look + ",";
      size_t at = l.find("," + k);
      if (at != std::string::npos) {
        size_t v = at + 1 + k.size(), e = l.find(',', v);
        put_s(out, l.substr(v, e - v).c_str());
        st = 0;
      }
    }
  }
  else if (r.svc == 2 && r.fn == 1) {
    int i = r.data[0] | (r.data[1] << 8);
    if (i < NCORES) { put_s(out, files[i]); put_s(out, names[i]); put_s(out, "26112101"); put_s(out, names[i]); put_s(out, "3"); }
    else st = 3;
  }
  else if (r.svc == 2 && r.fn == 3) { out.push_back(0); put_s(out, ""); }
  else if (r.svc == 2 && r.fn == 2) { launched = std::string((const char *) r.data.data()); return; }
  else st = 1;
  if (!respond(r.id, st, out)) { printf("tb_menu_soc: RESP %u refused\n", r.id); errors++; }
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
  int waited = 0;
  while (!t->ld_ready) { cpu_clock(); waited++; }
  printf("tb_menu_soc: the clear took %d clocks\n", waited);
  hello(); // as the firmware: HELLO before the ROMs
  t->ld_active = 1;
  for (size_t i = 0; i < prog.size(); i++) {
    if (i % 512 == 0) while (!t->ld_ready) cpu_clock();
    t->ld_addr = (uint32_t) i; t->ld_data = prog[i]; t->ld_wr = 1;
    cpu_clock();
    t->ld_wr = 0;
    cpu_clocks(24);
  }
  t->ld_active = 0; t->ld_end = 1; cpu_clock(); t->ld_end = 0;
  printf("tb_menu_soc: %zu bytes loaded\n", prog.size());

  uint64_t n = 0, limit = 60000000;
  int phase = 0; // 0 until the list is drawn, 1 Down pressed, 2 released, 3 Enter, 4 launched
  uint64_t t_phase = 0;
  while (n < limit && phase < 5) {
    if (t->l_irq) serve();
    cpu_clocks(200); n += 200;
    if (phase == 0 && console.find("menu: drawn") != std::string::npos) { phase = 1; t_phase = n; }
    else if (phase == 1 && n - t_phase > 300000) { t->kbd = (uint64_t) 0x51 << 8; phase = 2; t_phase = n; }        // Down
    else if (phase == 2 && n - t_phase > 600000) { t->kbd = 0; phase = 3; t_phase = n; cap_state = 1; }
    else if (phase == 3 && cap_state == 3 && n - t_phase > 600000) { t->kbd = (uint64_t) 0x28 << 8; phase = 4; t_phase = n; } // Enter
    else if (phase == 4 && !launched.empty()) phase = 5;
  }
  t->kbd = 0;
  printf("tb_menu_soc: %d requests served, %llu clocks\n", served, (unsigned long long) n);
  if (console.find("menu: rpc buffer ok") == std::string::npos) { printf("tb_menu_soc: the mailbox check failed\n"); errors++; }
  if (console.find("menu: cores read") == std::string::npos) { printf("tb_menu_soc: the core list was not read\n"); errors++; }
  if (cap_state != 3) { printf("tb_menu_soc: no frame captured\n"); errors++; }
  else {
    FILE *o = fopen(getenv("TB_FRAME") ? getenv("TB_FRAME") : "frame.ppm", "wb");
    fprintf(o, "P6 800 600 255\n");
    for (uint32_t p : frame) { fputc(p >> 16, o); fputc((p >> 8) & 255, o); fputc(p & 255, o); }
    fclose(o);
    int white = 0, selc = 0;
    for (uint32_t p : frame) { if (p == 0xffffff) white++; if (p == 0x2850d8) selc++; }
    // the button of the second core (Agon after Down) in the active colour 0x0000c8, the first in
    // the button colour 0x202026 (the defaults of the firmware): a pixel left of the text
    uint32_t b1 = frame[(88 + 16) * 800 + 220], b2 = frame[(88 + 40 + 16) * 800 + 220];
    if (white < 1000) { printf("tb_menu_soc: %d white pixels, no text\n", white); errors++; }
    if (b2 != 0x0000c8) { printf("tb_menu_soc: the second button is not active (%06x)\n", b2); errors++; }
    if (b1 != 0x202026) { printf("tb_menu_soc: the first button is not a button (%06x)\n", b1); errors++; }
    printf("tb_menu_soc: frame.ppm, %d white, %d selection pixels\n", white, selc);
  }
  if (launched != "b.kg") { printf("tb_menu_soc: launched '%s', want b.kg\n", launched.c_str()); errors++; }
  else printf("tb_menu_soc: LAUNCH %s after the clear\n", launched.c_str());
  printf("tb_menu_soc: %d errors\n", errors);
  delete t;
  return errors ? 1 : 0;
}
