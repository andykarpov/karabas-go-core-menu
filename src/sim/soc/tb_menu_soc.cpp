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
#include <algorithm>

static Vtb_menu_soc *t;
static uint64_t now_ps = 0, next_cpu = 0, next_pix = 0;
static const uint64_t P_CPU = 13333;   // 75 MHz
// the pixel clock by the VMODE register (the BUFGMUX of the tops): 25 MHz (640 x 480) or 40 MHz (800 x 600);
// TB_LOOK=video_mode=1 gives 800 x 600 in karabas.ini
static uint64_t p_pix(void) { return t->vmode ? 25000 : 40000; }
static int W = 640, H = 480;           // the frame captured: the mode at its start
static int errors = 0;
static std::string console;
static std::vector<uint32_t> frame(640 * 480);
static int cap_state = 0; // 0 wait, 1 waiting for vs, 2 capturing, 3 done
static int px = 0, py = 0, de_p = 0, vs_p = 0;
static std::vector<uint8_t> midi_bytes;
// the start melody of sound.c begins: Vibraphone (program 11) on channel 16, then C5 on
static bool melody_heard() {
  static const uint8_t m[5] = { 0xCF, 11, 0x9F, 72, 100 };
  return std::search(midi_bytes.begin(), midi_bytes.end(), m, m + 5) != midi_bytes.end();
}
static int snd_t_p = 0, snd_peak = 0;
static long snd_values = 0;

static void on_pix_edge() {
  int de = t->de, vs = t->vs;
  if (cap_state == 1 && vs && !vs_p) {
    cap_state = 2; px = 0; py = 0;
    W = t->vmode ? 800 : 640; H = t->vmode ? 600 : 480;
    frame.assign(W * H, 0);
  }
  else if (cap_state == 2) {
    if (de) { if (py < H && px < W) frame[py * W + px] = t->rgb; px++; }
    if (!de && de_p) { py++; px = 0; if (py == H) cap_state = 3; }
  }
  de_p = de; vs_p = vs;
}

static void step() {
  uint64_t nc = next_cpu, np = next_pix;
  uint64_t n = nc < np ? nc : np;
  now_ps = n;
  if (n == nc) { t->clk_cpu = !t->clk_cpu; next_cpu += P_CPU / 2; }
  if (n == np) { t->clk_pix = !t->clk_pix; next_pix += p_pix() / 2; }
  t->eval();
  if (n == np && t->clk_pix) on_pix_edge();
  if (n == nc && t->clk_cpu && t->midi_wr) midi_bytes.push_back(t->midi_data);
  if (n == nc && t->clk_cpu && t->snd_t != snd_t_p) {
    snd_t_p = t->snd_t;
    int v = (int16_t) t->snd_out;
    if (v < 0) v = -v;
    if (v > snd_peak) snd_peak = v;
    snd_values++;
  }
  if (n == nc && t->clk_cpu && t->dbg_uart_wr) {
    char c = (char) t->dbg_uart;
    putchar(c);
    console += c;
  }
}

// one rising edge of clk_cpu (the inputs set before it are taken at it)
// a fake ESP8266 (TB_WIFI): AT 1.7.6, joined; replies at the uart rate are not modelled (at once)
static std::string esp_cmd, esp_out;
static size_t esp_at = 0;
static int esp_cmds = 0;
static void esp_model() {
  if (t->esp_tx_wr) {
    char c = (char) t->esp_tx_data;
    if (c == '\n') {
      if (!esp_cmd.empty() && esp_cmd.back() == '\r') esp_cmd.pop_back();
      esp_cmds++;
      if (esp_cmd == "AT+GMR") esp_out += "AT version:1.7.6.0(Jan 24 2022 08:56:02)\r\n";
      else if (esp_cmd == "AT+UART_DEF?") esp_out += "+UART_DEF:115200,8,1,0,2\r\n";
      else if (esp_cmd == "AT+CWMODE?") esp_out += "+CWMODE:1\r\n";
      else if (esp_cmd == "AT+CWJAP?") esp_out += "+CWJAP:\"TestNet\",\"aa:bb:cc:dd:ee:ff\",6,-52\r\n";
      else if (esp_cmd == "AT+CIFSR") esp_out += "+CIFSR:STAIP,\"10.0.0.7\"\r\n";
      esp_out += "\r\nOK\r\n";
      esp_cmd.clear();
    } else esp_cmd += c;
  }
  if (t->esp_rx_ack) esp_at++;
  t->esp_rx_wr = esp_at < esp_out.size();
  t->esp_rx_data = esp_at < esp_out.size() ? (uint8_t) esp_out[esp_at] : 0;
}

static void esp_model();
static void cpu_clock() {
  do step(); while (!(t->clk_cpu && now_ps == next_cpu - P_CPU / 2));
  esp_model();
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
static std::string launched, last_set;   // last_set: the value of [autoload] last_core of a CFG SET
static int served = 0, cfg_calls = 0;

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
    // CFG GET file, (section, key)...: per pair 1 + value, or 0 + "" (not there: the defaults of the
    // menu). TB_LOOK="key=value,key=value" gives keys of any section; [ft812] sound is 0 (1 s of the
    // start sound is too long for the run) but in the TB_SOUND run
    std::vector<std::string> a;
    for (size_t p = 0; p < r.data.size(); p += strlen((const char *) r.data.data() + p) + 1) a.push_back((const char *) r.data.data() + p);
    const char *look = getenv("TB_LOOK");
    std::string l = std::string(",") + (look ? look : "") + ",";
    if (!getenv("TB_SOUND")) l += "sound=0,";
    for (size_t k = 1; k + 1 < a.size(); k += 2) {
      size_t at = l.find("," + a[k + 1] + "=");
      if (at == std::string::npos) { out.push_back(0); out.push_back(0); continue; }
      size_t v = at + a[k + 1].size() + 2, e = l.find(',', v);
      out.push_back(1);
      put_s(out, l.substr(v, e - v).c_str());
    }
  }
  else if (r.svc == 1) {   // SET, SAVE, PREVIEW: ok
    cfg_calls++;
    std::vector<std::string> a;
    for (size_t p = 0; p < r.data.size(); p += strlen((const char *) r.data.data() + p) + 1) a.push_back((const char *) r.data.data() + p);
    if (r.fn == 1) for (size_t k = 1; k + 2 < a.size(); k += 3) if (a[k] == "autoload" && a[k + 1] == "last_core") last_set = a[k + 2];
  }
  else if (r.svc == 2 && r.fn == 1) {
    int i = r.data[0] | (r.data[1] << 8);
    if (i < NCORES) { put_s(out, files[i]); put_s(out, names[i]); put_s(out, "26112101"); put_s(out, names[i]); put_s(out, "3"); }
    else st = 3;
  }
  else if (r.svc == 2 && r.fn == 3) {
    // TB_AUTOLOAD=seconds,file: the autoload of karabas.ini
    const char *a = getenv("TB_AUTOLOAD");
    if (a) { out.push_back((uint8_t) atoi(a)); put_s(out, strchr(a, ',') + 1); }
    else { out.push_back(0); put_s(out, ""); }
  }
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
  bool setup_done = false;
  int wifi_k = 0;
  std::vector<uint32_t> setup_frame;
  while (n < limit && phase != 5) {
    if (t->l_irq) serve();
    cpu_clocks(200); n += 200;
    if (phase == 0 && console.find("menu: drawn") != std::string::npos) { phase = 1; t_phase = n; }
    else if (phase == 1 && getenv("TB_MOUSE") && n - t_phase > 300000) {
      // TB_MOUSE: the pointer onto the fourth button (up from the middle of 640 x 480, y 240: three reports
      // of -5 to y 225, the button is 208..240), then a frame
      static int moves = 0;
      if (moves < 3) {
        t->ms_x = 0; t->ms_y = 0xFB; t->ms_b = 0; t->ms_wr = 1; cpu_clock(); t->ms_wr = 0; moves++; t_phase = n;
      } else if (n - t_phase > 400000) { phase = 6; cap_state = 1; t_phase = n; }
    }
    // TB_MOUSE: after the frame two quick clicks (press and release two clocks apart, between two
    // reads of the CPU): the first selects the core under the pointer, the second starts it
    else if (phase == 6 && cap_state == 3 && n - t_phase > 1500000) {  // 20 ms apart
      static int clicks = 0;
      if (clicks < 2) {
        t->ms_x = 0; t->ms_y = 0; t->ms_b = 1; t->ms_wr = 1; cpu_clock(); t->ms_wr = 0; cpu_clocks(2);
        t->ms_b = 0; t->ms_wr = 1; cpu_clock(); t->ms_wr = 0;
        clicks++; t_phase = n; printf("tb_menu_soc: click %d at %llu\n", clicks, (unsigned long long) n);
      } else phase = 4;
    }
    // TB_WIFI: S, Down x 6 (the Wi-Fi section): the status is asked over the uart of the ESP (the
    // model above), a frame of it
    else if (phase == 1 && getenv("TB_WIFI") && n - t_phase > 300000) { phase = 30; wifi_k = 0; t_phase = n; }
    else if (phase == 30 && n - t_phase > 600000) {
      static const uint8_t ks[7] = { 0x16, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51 };
      // a press, held until the picture of it is drawn, then the release
      static size_t drawn = 0;
      size_t d = 0;
      for (size_t p = console.find("menu: setup drawn"); p != std::string::npos; p = console.find("menu: setup drawn", p + 1)) d++;
      if (wifi_k < 14 && !(wifi_k & 1)) { t->kbd = (uint64_t) ks[wifi_k / 2] << 8; wifi_k++; drawn = d; t_phase = n; }
      else if (wifi_k < 14 && d > drawn) { t->kbd = 0; wifi_k++; t_phase = n; }
      else if (console.find("ESP << +CIFSR") != std::string::npos) { phase = 31; t_phase = n; }
    }
    else if (phase == 31 && n - t_phase > 6000000) { cap_state = 1; phase = 32; }
    else if (phase == 32 && cap_state == 3) phase = 5;
    // TB_SETUP: S opens Setup (drawn into the other buffer, its own palette), a frame of it, Esc back
    // to the list (drawn again), then on as the key run
    else if (phase == 1 && getenv("TB_SETUP") && !setup_done && n - t_phase > 300000) { t->kbd = (uint64_t) 0x16 << 8; phase = 7; t_phase = n; }
    else if (phase == 7 && n - t_phase > 600000) { t->kbd = 0; phase = 8; t_phase = n; }
    else if (phase == 8 && console.find("menu: setup drawn") != std::string::npos) { cap_state = 1; phase = 9; t_phase = n; }
    else if (phase == 9 && cap_state == 3) {
      setup_frame = frame;
      cap_state = 0;
      t->kbd = (uint64_t) 0x29 << 8; phase = 10; t_phase = n;   // Esc
    }
    else if (phase == 10 && n - t_phase > 600000) { t->kbd = 0; phase = 11; }
    else if (phase == 11 && console.find("menu: list drawn") != console.rfind("menu: list drawn")) { setup_done = true; phase = 1; t_phase = n; }
    else if (phase == 1 && !getenv("TB_MOUSE") && (!getenv("TB_SETUP") || setup_done) && n - t_phase > 300000) {
      // Down, or (TB_WHEEL) a notch of the wheel towards the user: the same move
      if (getenv("TB_WHEEL")) { t->ms_x = 0; t->ms_y = 0; t->ms_z = 0xFF; t->ms_b = 0; t->ms_wr = 1; cpu_clock(); t->ms_wr = 0; t->ms_z = 0; }
      else t->kbd = (uint64_t) 0x51 << 8;
      phase = 2; t_phase = n;
    }
    else if (phase == 2 && n - t_phase > 600000) { t->kbd = 0; phase = 3; t_phase = n; cap_state = 1; }
    else if (phase == 3 && cap_state == 3 && n - t_phase > 600000) { t->kbd = (uint64_t) 0x28 << 8; phase = 4; t_phase = n; } // Enter
    else if (phase == 4 && !launched.empty()) phase = 5;
    // TB_SOUND: the start melody after Enter, its first note is enough (the whole of it is 0.9 s)
    else if (phase == 4 && getenv("TB_SOUND") && melody_heard()) phase = 5;
  }
  t->kbd = 0;
  printf("tb_menu_soc: %d requests served, %llu clocks\n", served, (unsigned long long) n);
  if (console.find("menu: rpc buffer ok") == std::string::npos) { printf("tb_menu_soc: the mailbox check failed\n"); errors++; }
  if (console.find("menu: cores read") == std::string::npos) { printf("tb_menu_soc: the core list was not read\n"); errors++; }
  if (cap_state != 3) { printf("tb_menu_soc: no frame captured\n"); errors++; }
  else {
    FILE *o = fopen(getenv("TB_FRAME") ? getenv("TB_FRAME") : "frame.ppm", "wb");
    fprintf(o, "P6 %d %d 255\n", W, H);
    for (uint32_t p : frame) { fputc(p >> 16, o); fputc((p >> 8) & 255, o); fputc(p & 255, o); }
    fclose(o);
    int white = 0, selc = 0;
    for (uint32_t p : frame) { if (p == 0xffffff) white++; if (p == 0x2850d8) selc++; }
    // the button of the second core (Agon after Down) in the active colour 0x0000c8, the first in
    // the button colour 0x202026 (the defaults of the firmware): a pixel left of the text
    // (last_core=c.kg: the fourth one after Down, the first stays in the button colour)
    const char *lk = getenv("TB_LOOK");
    int row = lk && strstr(lk, "last_core=c.kg") ? 3 : 1;
    uint32_t b1 = frame[(88 + 16) * W + 220], b2 = frame[(88 + 40 * row + 16) * W + 220];
    printf("tb_menu_soc: the frame is %d x %d\n", W, H);
    int want_w = lk && strstr(lk, "video_mode=1") ? 800 : 640;
    if (W != want_w) { printf("tb_menu_soc: the video mode is not that of karabas.ini (%d wanted)\n", want_w); errors++; }
    if (white < 1000) { printf("tb_menu_soc: %d white pixels, no text\n", white); errors++; }
    if (getenv("TB_MOUSE") || getenv("TB_WIFI")) b1 = 0x202026, b2 = 0x0000c8;   // not the list  // the frame is before the clicks: not checked
    if (b2 != 0x0000c8) { printf("tb_menu_soc: the button %d is not active (%06x)\n", row + 1, b2); errors++; }
    if (b1 != 0x202026) { printf("tb_menu_soc: the first button is not a button (%06x)\n", b1); errors++; }
    printf("tb_menu_soc: frame.ppm, %d white, %d selection pixels\n", white, selc);
  }
  if (getenv("TB_SETUP")) {
    // the System section chosen and lit (the active colour), Autoload a button: a pixel left of the text
    // the tabs: 40 px apart in 800 x 600, 32 in 640 x 480 (24 high)
    int step = W == 800 ? 40 : 32, sz = (int) setup_frame.size();
    uint32_t s1 = sz == W * H ? setup_frame[(88 + 12) * W + 24] : 0;
    uint32_t s2 = sz == W * H ? setup_frame[(88 + step + 12) * W + 24] : 0;
    printf("tb_menu_soc: Setup frame, System %06x, Autoload %06x\n", s1, s2);
    if (s1 != 0x0000c8 || s2 != 0x202026) { printf("tb_menu_soc: Setup is not drawn\n"); errors++; }
    FILE *o = fopen("frame_setup.ppm", "wb");
    fprintf(o, "P6 %d %d 255\n", W, H);
    for (uint32_t p : setup_frame) { fputc(p >> 16, o); fputc((p >> 8) & 255, o); fputc(p & 255, o); }
    fclose(o);
  }
  if (getenv("TB_WIFI")) {
    printf("tb_menu_soc: %d AT commands to the ESP\n", esp_cmds);
    if (esp_cmds < 6 || console.find("ESP << +CWJAP:\"TestNet\"") == std::string::npos) { printf("tb_menu_soc: no Wi-Fi status\n"); errors++; }
  }
  // the pointer is on the fourth core; TB_LOOK=last_core=c.kg: the list starts on c.kg, Down to d.kg
  const char *lk0 = getenv("TB_LOOK");
  const char *want = getenv("TB_MOUSE") ? "d.kg" : lk0 && strstr(lk0, "last_core=c.kg") ? "d.kg" : "b.kg";
  // the click of Down / the mouse: Cowbell on channel 10, on and off
  {
    static const uint8_t click[6] = { 0x99, 56, 100, 0x89, 56, 0 };
    bool ok = midi_bytes.size() >= 6 && std::equal(click, click + 6, midi_bytes.begin());
    printf("tb_menu_soc: %zu MIDI bytes\n", midi_bytes.size());
    if (!ok) { printf("tb_menu_soc: no click on MIDI\n"); errors++; }
  }
  if (getenv("TB_SOUND")) {
    printf("tb_menu_soc: start melody %s\n", melody_heard() ? "heard" : "NOT heard");
    if (!melody_heard()) { printf("tb_menu_soc: no start melody on MIDI\n"); errors++; }
    if (snd_peak) { printf("tb_menu_soc: PCM out of menu_sound (peak %d), none was asked\n", snd_peak); errors++; }
    want = "";  // the run ends in the melody, before the launch
  }
  if (getenv("TB_WIFI")) want = "";   // the run ends on the Wi-Fi page
  if (launched != want) { printf("tb_menu_soc: launched '%s', want %s\n", launched.c_str(), want); errors++; }
  else printf("tb_menu_soc: LAUNCH %s after the clear\n", launched.c_str());
  // the started core is written to [autoload] last_core before the LAUNCH
  if (last_set != want) { printf("tb_menu_soc: last_core '%s', want %s\n", last_set.c_str(), want); errors++; }
  else if (*want) printf("tb_menu_soc: last_core %s saved\n", last_set.c_str());
  printf("tb_menu_soc: %d errors\n", errors);
  delete t;
  return errors ? 1 : 0;
}
