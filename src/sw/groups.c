// The groups of the core list (groups.h).
#include "groups.h"
#include "ui.h"
#include "rpc.h"

char group_names[MAX_GROUPS][GROUP_LEN + 1];
int group_count[MAX_GROUPS];
int ngroups = 0;
static int8_t core_group[MAX_CORES];
static uint8_t buf[RPC_MAX];

// the groups of the table, in the order of the list; the groups of the user come after them, by name
static const char *const known[] = { "ZX Spectrum", "USSR", "Computers", "Consoles", "Games", "Utilities" };
#define KNOWN ((int) (sizeof(known) / sizeof(known[0])))

// the cores of today by their id (docs/PLAN.md Ф6, 2026-11-24); later the `group` of core.json comes first
static const struct { const char *id; uint8_t g; } table[] = {
  { "tsconf", 0 }, { "baseconf", 0 }, { "profi", 0 }, { "zxnext", 0 }, { "zxuno", 0 }, { "spec256", 0 },
  { "quadspeccy", 0 }, { "quadspeccyx4", 0 }, { "sprinter", 0 }, { "alf", 0 }, { "p4096", 0 },
  { "bk", 1 }, { "uknc", 1 }, { "vector06c", 1 }, { "orion128", 1 }, { "orionpro", 1 }, { "specialist", 1 },
  { "rk", 1 }, { "pdp2026", 1 }, { "criss", 1 },
  { "apple2", 2 }, { "atari800", 2 }, { "atarist", 2 }, { "archie", 2 }, { "bbc", 2 }, { "c64", 2 }, { "cpc", 2 },
  { "macplus", 2 }, { "msx", 2 }, { "msx2", 2 }, { "nanomig", 2 }, { "ql", 2 }, { "vic20", 2 }, { "agon", 2 },
  { "gigatron", 2 }, { "next186", 2 }, { "pcxt", 2 }, { "rc2014", 2 }, { "v6z80p", 2 },
  { "atari2600", 3 }, { "nes", 3 }, { "snes", 3 }, { "sega", 3 }, { "wonderswan", 3 }, { "uzebox", 3 }, { "pong", 3 },
  { "doom", 4 }, { "heretic", 4 }, { "hexen", 4 },
  { "boardtest", 5 }, { "term", 5 }, { "vt100", 5 }, { "musicplayer", 5 },
};
#define TABLE ((int) (sizeof(table) / sizeof(table[0])))

#define SECTION "groups"

static void group_default(core_t *c) {
  c->group[0] = 0;
  for (int i = 0; i < TABLE; i++) if (str_eq(table[i].id, c->id)) { str_cpy(c->group, known[table[i].g]); return; }
}

// the value as a group name: the spaces around it trimmed, at most GROUP_LEN chars
static void name_copy(char *dst, const char *src) {
  while (*src == ' ') src++;
  str_ncpy(dst, src, GROUP_LEN + 1);
  int n = str_len(dst);
  while (n > 0 && dst[n - 1] == ' ') dst[--n] = 0;
}

void groups_read(void) {
  for (int i = 0; i < ncores; i++) group_default(&cores[i]);
  // [groups] <id> of all the cores, as many pairs a GET as fit (the answer: GROUP_LEN + 2 bytes a value at most)
  for (int from = 0; from < ncores; ) {
    char req[RPC_MAX], *p = req;
    int to = from;
    p = str_cpy(p, "0") + 1;
    for (; to < ncores; to++) {
      int len = sizeof(SECTION) + str_len(cores[to].id) + 1;
      if ((p - req) + len > RPC_MAX - 8 || (to - from + 1) * (GROUP_LEN + 2) > RPC_MAX) break;
      p = str_cpy(p, SECTION) + 1; p = str_cpy(p, cores[to].id) + 1;
    }
    if (to == from) break;
    uint16_t n;
    if (rpc_call(RPC_CFG, 0, req, (uint16_t) (p - req), buf, sizeof(buf), &n, 2000) != RPC_OK) break;
    const uint8_t *q = buf, *end = buf + n;
    for (int k = from; k < to && q < end; k++) {
      int found = *q++;
      char v[40];
      int i = 0;
      while (q < end && *q) { if (i < 39) v[i++] = (char) *q; q++; }
      if (q < end) q++;
      v[i] = 0;
      if (found) name_copy(cores[k].group, v);
    }
    from = to;
  }
  groups_build();
}

static int rank(const char *name) {
  for (int i = 0; i < KNOWN; i++) if (str_ieq(known[i], name)) return i;
  return KNOWN;
}

static int name_cmp(const char *a, const char *b) {
  for (;; a++, b++) {
    char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
    if (x != y || !x) return x - y;
  }
}

static int before(const char *a, const char *b) {
  int ra = rank(a), rb = rank(b);
  return ra != rb ? ra < rb : name_cmp(a, b) < 0;
}

int group_find(const char *name) {
  for (int g = 0; g < ngroups; g++) if (str_ieq(group_names[g], name)) return g;
  return -1;
}

void groups_build(void) {
  ngroups = 0;
  for (int i = 0; i < ncores; i++) {
    const core_t *c = &cores[i];
    if (!c->visible || !c->group[0] || group_find(c->group) >= 0 || ngroups == MAX_GROUPS) continue;
    // in its place: the known ones in their order, then by name
    int at = ngroups;
    while (at > 0 && before(c->group, group_names[at - 1])) { str_cpy(group_names[at], group_names[at - 1]); at--; }
    str_cpy(group_names[at], c->group);
    ngroups++;
  }
  for (int g = 0; g < ngroups; g++) group_count[g] = 0;
  for (int i = 0; i < ncores; i++) {
    core_group[i] = (int8_t) (cores[i].visible && cores[i].group[0] ? group_find(cores[i].group) : -1);
    if (core_group[i] >= 0) group_count[core_group[i]]++;
  }
}

int group_of(int core) { return core >= 0 && core < ncores ? core_group[core] : -1; }

int groups_set(int core, const char *name) {
  core_t *c = &cores[core];
  char v[GROUP_LEN + 1], req[96], *p = req;
  name_copy(v, name);
  p = str_cpy(p, "0") + 1; p = str_cpy(p, SECTION) + 1; p = str_cpy(p, c->id) + 1; p = str_cpy(p, v) + 1;
  uint16_t n;
  int st = rpc_call(RPC_CFG, 1, req, (uint16_t) (p - req), buf, sizeof(buf), &n, 2000);   // written at once
  dbg_puts(st == RPC_OK ? "menu: group saved\n" : "menu: group not saved\n");
  if (st != RPC_OK) return 0;
  str_cpy(c->group, v);
  groups_build();
  return 1;
}
