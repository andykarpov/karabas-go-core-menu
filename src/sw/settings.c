// The settings of karabas.ini that Setup shows (settings.h).
#include "settings.h"
#include "ui.h"
#include "rpc.h"
#include "cores.h"

static const char *const core_sorts[] = { "Name", "File name", "Order" };
static const char *const core_sort_keys[] = { "name", "file", "order" };
// [ft812] video_mode: the modes of the FT812 menu of the firmware had these numbers; another number (the older
// firmware had 15 modes) is 640 x 480, as the firmware took it
static const char *const video_names[] = { "640x480", "800x600" };
static const char *const sounds_names[] = { "Off", "Klyuchik", "Ta-dan", "Run", "Sound 4" };
// the options of the matrix (firmware lib/common/matrix_style.c): karabas.ini has the names
static const char *const mx_colors[] = { "Green", "Yellow", "Red" };
static const char *const mx_seconds[] = { "Dot", "Colon", "Bar", "Off" };
static const char *const mx_styles[] = { "Bars", "Wide", "Center", "Mono", "Vertical" };
static const char *const mx_meter_colors[] = { "Zones", "Green", "Yellow", "Red", "Fire" };
static const char *const mx_decays[] = { "Slow", "Normal", "Fast" };

#define B(label, hint, sec, key, def) { label, hint, sec, key, SK_BOOL, 2, 0, 0, 0, def }
#define C(label, hint, sec, key, names, ini, def) { label, hint, sec, key, SK_CHOICE, sizeof(names) / sizeof(names[0]), names, ini, 0, def }
#define N(label, hint, sec, key, n, base, def) { label, hint, sec, key, SK_CHOICE, n, 0, 0, base, def }
#define K(label, hint, key, def) { label, hint, "ft812", key, SK_COLOR, 0, 0, 0, 0, def }

const setting_t settings[S_COUNT] = {
  B("Debug log",      "Log to the USB serial port",     "setup", "debug", 0),
  B("DVI only",       "No HDMI audio, next core",       "setup", "dvi_only", 0),
  C("Core sort",      "The order of the core list",     "setup", "core_sort", core_sorts, 1, 0),
  B("LED matrix",     "The dot matrix add-on",          "matrix", "enabled", 1),
  N("Matrix bright.", "Core: as the core sets it",      "matrix", "brightness", 17, 0, 0),
  { "Matrix effects", "The clock and the audio meters", 0, 0, SK_PAGE, 0, 0, 0, 0, 0 },
  C("Clock hours",    "The color of the hours",         "matrix", "clock_hours", mx_colors, 1, 0),
  C("Clock minutes",  "The color of the minutes",       "matrix", "clock_minutes", mx_colors, 1, 1),
  C("Clock seconds",  "A dot, a colon or a bar",        "matrix", "clock_seconds", mx_seconds, 1, 0),
  C("Seconds color",  "The color of the seconds",       "matrix", "clock_seconds_color", mx_colors, 1, 2),
  C("Meter style",    "How the audio meters look",      "matrix", "meter_style", mx_styles, 1, 0),
  C("Meter colors",   "Zones: green, yellow, red",      "matrix", "meter_colors", mx_meter_colors, 1, 0),
  C("Meter decay",    "How fast the meters fall",       "matrix", "meter_decay", mx_decays, 1, 1),
  B("Peak hold",      "A dot holds the peak a second",  "matrix", "meter_peak", 0),
  B("Autoload",       "Start a core on power on",       "autoload", "enabled", 0),
  N("Timeout",        "Seconds before the start",       "autoload", "timeout", 61, 0, 15),
  { "Core",           "The core to start",              "autoload", "core", SK_OTHER, 0, 0, 0, 0, 0 },
  C("Video mode",     "Left/Right: try it, then keep it",  "ft812", "video_mode", video_names, 0, 0),
  B("Show time",      "The clock at the top",           "ft812", "time", 1),
  B("Show date",      "The date under the clock",       "ft812", "date", 1),
  B("Character",      "The Karabas picture",            "ft812", "char", 1),
  N("Mouse speed",    "How fast the pointer moves",     "ft812", "mouse_speed", 5, 1, 2),   // def: the index (speed 3)
  C("Start sound",    "Played when a core starts",      "ft812", "sound", sounds_names, 0, 1),
  B("Key click",      "A click on every key",           "ft812", "click", 1),
  { "Theme",          "The colors of the menu",         0, 0, SK_CHOICE, 0, 0, 0, 0, 0 },
  K("Background",     "Behind everything",              "color_bg", 0x010101),
  K("Gradient",       "The glow at the top",            "color_gradient", 0x0000c8),
  K("Button",         "The buttons",                    "color_button", 0x202026),
  K("Active button",  "The chosen button",              "color_active", 0x0000c8),
  K("Text",           "The text and the buttons",       "color_text", 0xffffff),
  K("Active text",    "The text of the chosen button",  "color_text_active", 0xffffff),
  K("Footer text",    "The build, page and autoload",   "color_copyright", 0x787878),
  { "Time zone",      "",                               "setup", "tz", SK_OTHER, 0, 0, 0, 0, 0 },
};

const uint8_t settings_mx_ids[] = { S_MX_HOURS, S_MX_MINUTES, S_MX_SECONDS, S_MX_SECONDS_COLOR, S_MX_STYLE, S_MX_COLORS, S_MX_DECAY, S_MX_PEAK };
const int settings_mx_len = sizeof(settings_mx_ids);

typedef struct { const char *name; uint32_t c[7]; } theme_t;   // bg, gradient, button, active, text, text active, footer
static const theme_t themes[] = {
  { "Classic",  { 0x010101, 0x0000c8, 0x202026, 0x0000c8, 0xffffff, 0xffffff, 0x787878 } },
  { "Spectrum", { 0x000000, 0x0000d7, 0x10102a, 0xd7d700, 0xffffff, 0x000000, 0x00d7d7 } },
  { "Emerald",  { 0x000400, 0x00702a, 0x102016, 0x00a040, 0xc8ffd0, 0xffffff, 0x4f7a58 } },
  { "Amber",    { 0x0a0500, 0x8a4a00, 0x241808, 0xc87000, 0xffd9a0, 0x1a0d00, 0x8a6a40 } },
  { "Crimson",  { 0x080000, 0xa00018, 0x261012, 0xc8102a, 0xffe0e0, 0xffffff, 0x806060 } },
  { "Ocean",    { 0x000608, 0x006a80, 0x0e2228, 0x00a0b8, 0xe0fcff, 0xffffff, 0x5a8088 } },
  { "Mono",     { 0x000000, 0x505050, 0x202020, 0xc0c0c0, 0xe0e0e0, 0x000000, 0x707070 } },
};
#define THEMES ((int) (sizeof(themes) / sizeof(themes[0])))

int32_t set_val[S_COUNT];
static int32_t saved[S_COUNT];
char set_core[33];
static char saved_core[33];
static int core_sel;             // in cores[]: ncores = the core of karabas.ini that is not on the card
static uint8_t buf[RPC_MAX];

// ---- CFG ----

// GET of several values: the pairs of settings[from..] that fit a request; their values (1 + value,
// or 0 + "" when not in the file) into vals; returns the next id, -1 on an error
static int cfg_get_many(int from, char vals[][40]) {
  char req[RPC_MAX], *p = req;
  int id = from;
  p = str_cpy(p, "0") + 1;
  for (; id < S_COUNT; id++) {
    const setting_t *s = &settings[id];
    if (!s->section) continue;
    int len = str_len(s->section) + str_len(s->key) + 2;
    if ((p - req) + len > RPC_MAX - 8 || (id - from) >= 20) break;   // the answer: 20 values of at most 24 chars
    p = str_cpy(p, s->section) + 1; p = str_cpy(p, s->key) + 1;
  }
  uint16_t n;
  if (rpc_call(RPC_CFG, 0, req, (uint16_t) (p - req), buf, sizeof(buf), &n, 2000) != RPC_OK) return -1;
  const uint8_t *q = buf, *end = buf + n;
  for (int k = from; k < id; k++) {
    vals[k][0] = 0;
    if (!settings[k].section) continue;
    int found = q < end && *q++;
    int i = 0;
    while (q < end && *q) { if (i < 39) vals[k][i++] = (char) *q; q++; }
    if (q < end) q++;
    vals[k][i] = 0;
    if (!found) vals[k][0] = 1;          // not in the file: the default
  }
  return id;
}

static void trim(char *s) {
  char *a = s;
  while (*a == ' ') a++;
  int n = 0;
  while (a[n]) { s[n] = a[n]; n++; }
  while (n > 0 && s[n - 1] == ' ') n--;
  s[n] = 0;
}

static int tz_parse(const char *s, int32_t *v) {
  int sign = 1, h = 0, m = 0;
  if (*s == '+' || *s == '-') sign = *s++ == '-' ? -1 : 1;
  if (*s < '0' || *s > '9') return 0;
  h = *s++ - '0';
  if (*s >= '0' && *s <= '9') h = h * 10 + (*s++ - '0');
  if (*s == ':') {
    if (s[1] < '0' || s[1] > '9' || s[2] < '0' || s[2] > '9') return 0;
    m = (s[1] - '0') * 10 + (s[2] - '0');
    s += 3;
  }
  if (*s || m >= 60 || m % 15) return 0;
  int x = sign * (h * 60 + m);
  if (x < -12 * 60 || x > 14 * 60) return 0;
  *v = x;
  return 1;
}

static void tz_text(int32_t v, char *out) {
  int a = v < 0 ? -v : v;
  *out++ = v < 0 ? '-' : '+';
  out = u2s(out, a / 60, 2); *out++ = ':'; u2s(out, a % 60, 2);
}

// the value of a setting as karabas.ini has it
static void ini_text(int id, char *out) {
  const setting_t *s = &settings[id];
  int32_t v = set_val[id];
  if (id == S_CORE) { str_cpy(out, set_core); return; }
  if (id == S_TZ) { tz_text(v, out); return; }
  if (id == S_CORE_SORT) { str_cpy(out, core_sort_keys[v]); return; }
  switch (s->kind) {
    case SK_BOOL: str_cpy(out, v ? "1" : "0"); break;
    case SK_COLOR: hex6(out, (uint32_t) v); break;
    case SK_CHOICE: if (s->ini_names) str_cpy(out, s->names[v]); else u2s(out, (uint32_t) (v + s->base), 1); break;
    default: out[0] = 0;
  }
}

static void core_find(void) {
  core_sel = ncores;
  for (int i = 0; i < ncores; i++) if (str_eq(cores[i].id, set_core)) { core_sel = i; break; }
}

void settings_read(void) {
  static char vals[S_COUNT][40];
  for (int id = 0; id < S_COUNT; id++) { set_val[id] = settings[id].def; vals[id][0] = 1; }
  for (int id = 0; id >= 0 && id < S_COUNT; ) id = cfg_get_many(id, vals);
  set_core[0] = 0;
  for (int id = 0; id < S_COUNT; id++) {
    const setting_t *s = &settings[id];
    char *v = vals[id];
    if (!s->section || v[0] == 1) continue;   // not in the file
    trim(v);
    int n;
    uint32_t c;
    if (id == S_CORE) str_ncpy(set_core, v, sizeof(set_core));
    else if (id == S_TZ) tz_parse(v, &set_val[id]);
    else if (id == S_CORE_SORT) { for (int i = 0; i < 3; i++) if (str_ieq(v, core_sort_keys[i])) set_val[id] = i; }
    else if (s->kind == SK_BOOL) {
      if (str_ieq(v, "1") || str_ieq(v, "true") || str_ieq(v, "yes")) set_val[id] = 1;
      else if (str_ieq(v, "0") || str_ieq(v, "false") || str_ieq(v, "no")) set_val[id] = 0;
    } else if (s->kind == SK_COLOR) { if (parse_hex(v, &c)) set_val[id] = (int32_t) (c & 0xFFFFFF); }
    else if (s->kind == SK_CHOICE && s->ini_names) { for (int i = 0; i < s->n; i++) if (str_ieq(v, s->names[i])) set_val[id] = i; }
    else if (s->kind == SK_CHOICE && parse_uint(v, &n) && n >= s->base && n - s->base < s->n) set_val[id] = n - s->base;
  }
  if (set_val[S_SOUND] >= 5) set_val[S_SOUND] = 1;
  for (int id = 0; id < S_COUNT; id++) saved[id] = set_val[id];
  str_cpy(saved_core, set_core);
  core_find();
  settings_look();
}

void settings_look(void) {
  look.bg = set_val[S_C_BG]; look.gradient = set_val[S_C_GRADIENT]; look.button = set_val[S_C_BUTTON];
  look.active = set_val[S_C_ACTIVE]; look.text = set_val[S_C_TEXT]; look.text_active = set_val[S_C_TEXT_ACTIVE];
  look.copyright = set_val[S_C_COPYRIGHT];
  look.time = set_val[S_TIME]; look.date = set_val[S_DATE]; look.chr = set_val[S_CHAR];
  look.mouse_speed = set_val[S_MOUSE] + 1; look.click = set_val[S_CLICK]; look.sound = set_val[S_SOUND];
}

int settings_differ(void) {
  for (int id = 0; id < S_COUNT; id++) if (set_val[id] != saved[id]) return 1;
  return !str_eq(set_core, saved_core);
}

void settings_revert(void) {
  for (int id = 0; id < S_COUNT; id++) set_val[id] = saved[id];
  str_cpy(set_core, saved_core);
  core_find();
  settings_look();
}

// CFG SET of the changed values (as many triples a call as fit), then SAVE
static int changed(int id) { return id == S_CORE ? !str_eq(set_core, saved_core) : set_val[id] != saved[id]; }

static int save_ids(const uint8_t *ids, int n) {
  char req[RPC_MAX], *p = req;
  int count = 0;
  p = str_cpy(p, "0") + 1;
  for (int i = 0; i <= n; i++) {
    char v[40];
    int id = i < n ? ids[i] : -1;
    int len = 0;
    if (id >= 0) {
      ini_text(id, v);
      len = str_len(settings[id].section) + str_len(settings[id].key) + str_len(v) + 3;
    }
    if (count && (id < 0 || (p - req) + len > RPC_MAX)) {
      uint16_t rn;
      if (rpc_call(RPC_CFG, 1, req, (uint16_t) (p - req), buf, sizeof(buf), &rn, 3000) != RPC_OK) return 0;
      p = str_cpy(req, "0") + 1;
      count = 0;
    }
    if (id < 0) break;
    p = str_cpy(p, settings[id].section) + 1; p = str_cpy(p, settings[id].key) + 1; p = str_cpy(p, v) + 1;
    count++;
  }
  uint16_t rn;
  if (rpc_call(RPC_CFG, 2, "0", 2, buf, sizeof(buf), &rn, 3000) != RPC_OK) return 0;
  for (int i = 0; i < n; i++) { saved[ids[i]] = set_val[ids[i]]; if (ids[i] == S_CORE) str_cpy(saved_core, set_core); }
  return 1;
}

int settings_save(void) {
  uint8_t ids[S_COUNT];
  int n = 0;
  for (int id = 0; id < S_COUNT; id++) if (settings[id].section && changed(id)) ids[n++] = (uint8_t) id;
  return n ? save_ids(ids, n) : 1;
}

int settings_save_one(int id) {
  uint8_t ids[1] = { (uint8_t) id };
  return changed(id) ? save_ids(ids, 1) : 1;
}

// ---- the values ----

int settings_theme(void) {
  for (int t = 0; t < THEMES; t++) {
    int same = 1;
    for (int i = 0; i < 7 && same; i++) same = (uint32_t) set_val[S_C_BG + i] == themes[t].c[i];
    if (same) return t;
  }
  return -1;
}

void settings_value_text(int id, char *out, int size) {
  const setting_t *s = &settings[id];
  int32_t v = set_val[id];
  char t[40];
  t[0] = 0;
  if (id == S_THEME) { int th = settings_theme(); str_cpy(t, th < 0 ? "Custom" : themes[th].name); }
  else if (id == S_CORE) {
    core_find();                 // the list may be newer than the value
    const char *name = core_sel < ncores ? cores[core_sel].name : set_core;
    str_ncpy(t, name[0] ? name : "-", 33);
    trim(t);
  }
  else if (id == S_TZ) { t[0] = 'U'; t[1] = 'T'; t[2] = 'C'; tz_text(v, t + 3); }
  else if (id == S_TIMEOUT) { str_cpy(u2s(t, (uint32_t) v, 1), " s"); }
  else if (id == S_MATRIX_BRIGHT && v == 0) str_cpy(t, "Core");
  else if (s->kind == SK_BOOL) str_cpy(t, v ? "On" : "Off");
  else if (s->kind == SK_COLOR) hex6(t, (uint32_t) v);
  else if (s->kind == SK_CHOICE && s->names) str_cpy(t, s->names[v]);
  else if (s->kind == SK_CHOICE) u2s(t, (uint32_t) (v + s->base), 1);
  str_ncpy(out, t, size);
}

static int wrap(int v, int d, int n) { return ((v + d) % n + n) % n; }

void settings_change(int id, int d) {
  const setting_t *s = &settings[id];
  if (id == S_THEME) {
    int t = settings_theme();
    if (t < 0) t = d > 0 ? -1 : 0;               // Custom: to the first / the last theme
    t = wrap(t, d, THEMES);
    for (int i = 0; i < 7; i++) set_val[S_C_BG + i] = (int32_t) themes[t].c[i];
  } else if (id == S_CORE) {
    core_find();
    int other = 0;
    for (int i = 0; i < ncores; i++) other |= str_eq(cores[i].id, saved_core);
    int n = ncores + (saved_core[0] && !other ? 1 : 0);   // the core of karabas.ini not on the card: one more
    if (!n) return;
    core_sel = wrap(core_sel, d, n);
    str_cpy(set_core, core_sel < ncores ? cores[core_sel].id : saved_core);
  } else if (id == S_TZ) {
    int k = (set_val[id] + 12 * 60) / 15;
    set_val[id] = wrap(k, d, (26 * 60) / 15 + 1) * 15 - 12 * 60;
  } else if (s->kind == SK_BOOL) set_val[id] = !set_val[id];
  else if (s->kind == SK_CHOICE) set_val[id] = wrap(set_val[id], d, s->n);
  settings_look();
}

// ---- the LED matrix as edited (CFG PREVIEW) ----

void settings_preview(int mode) {
  static const char *const modes[] = { "none", "clock", "meter" };
  static const uint8_t ids[] = { S_MATRIX, S_MATRIX_BRIGHT, S_MX_HOURS, S_MX_MINUTES, S_MX_SECONDS, S_MX_SECONDS_COLOR,
                                 S_MX_STYLE, S_MX_COLORS, S_MX_DECAY, S_MX_PEAK };
  char req[RPC_MAX], *p = req, v[40];
  p = str_cpy(p, "0") + 1;
  p = str_cpy(p, modes[mode < 0 ? 0 : mode]) + 1;
  for (unsigned i = 0; i < sizeof(ids) && mode >= 0; i++) {
    ini_text(ids[i], v);
    p = str_cpy(p, settings[ids[i]].section) + 1; p = str_cpy(p, settings[ids[i]].key) + 1; p = str_cpy(p, v) + 1;
  }
  uint16_t rn;
  rpc_call(RPC_CFG, 3, req, (uint16_t) (p - req), buf, sizeof(buf), &rn, 1000);
}
