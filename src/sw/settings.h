// The settings of karabas.ini that Setup shows (firmware ui/settings_items.c without the rows of the
// FT812: Graphic menu, 3D buttons), read and written over RPC CFG.
#pragma once
#include <stdint.h>

enum {
  S_DEBUG, S_DVI, S_CORE_SORT, S_GROUPS, S_MATRIX, S_MATRIX_BRIGHT, S_MX_EFFECTS,
  S_MX_HOURS, S_MX_MINUTES, S_MX_SECONDS, S_MX_SECONDS_COLOR, S_MX_STYLE, S_MX_COLORS, S_MX_DECAY, S_MX_PEAK,
  S_AUTOLOAD, S_TIMEOUT, S_CORE,
  S_VIDEO,                       // [ft812] video_mode: 0 640 x 480, 1 800 x 600 (the numbers of the FT812 menu)
  S_TIME, S_DATE, S_CHAR, S_MOUSE,
  S_SOUND, S_CLICK,
  S_THEME, S_C_BG, S_C_GRADIENT, S_C_BUTTON, S_C_ACTIVE, S_C_TEXT, S_C_TEXT_ACTIVE, S_C_COPYRIGHT,
  S_TZ,                          // [setup] tz, minutes (the Clock page)
  S_COUNT
};
enum { SK_BOOL, SK_CHOICE, SK_COLOR, SK_PAGE, SK_OTHER };

typedef struct {
  const char *label, *hint;
  const char *section, *key;     // NULL: not in karabas.ini (the page, the theme)
  uint8_t kind;
  uint8_t n;                     // the choices
  const char *const *names;      // their names (NULL: numbers from base)
  uint8_t ini_names;             // karabas.ini has the name (else the number)
  int8_t base;
  int32_t def;                   // the default: the value (a choice: its index, not the number)
} setting_t;

extern const setting_t settings[S_COUNT];
extern int32_t set_val[S_COUNT];       // as edited
extern char set_core[33];              // [autoload] core: the id
extern const uint8_t settings_mx_ids[]; // the rows of System > Matrix effects
extern const int settings_mx_len;

void settings_read(void);              // all of them over CFG GET (the defaults of the firmware where missing)
void settings_look(void);              // look (ui.h) from the values
int settings_differ(void);             // edited since read / saved
void settings_revert(void);            // back to the saved values
int settings_save(void);               // the changed ones: CFG SET, then SAVE; 1 if it went
int settings_save_one(int id);         // one value now (the time zone of the Clock page)
void settings_value_text(int id, char *out, int size);
void settings_change(int id, int d);
int settings_theme(void);              // the theme of the colours, -1 Custom
// the LED matrix shows the matrix settings as edited (firmware matrix_setup) and mode (matrix_preview):
// 0 as it runs, 1 the clock, 2 the meters; -1: back to karabas.ini, no preview (Setup left)
void settings_preview(int mode);
