// The cores on the card (RPC CORES SCAN / ITEM, in the order of [setup] core_sort), the autoload and
// the build of the firmware (SYS INFO).
#pragma once
#include <stdint.h>

#define MAX_CORES 128
#define GROUP_LEN 20             // the name of a group (groups.h), chars
typedef struct { char file[33], name[33], id[33], group[GROUP_LEN + 1]; uint8_t visible, type; } core_t;   // type: 0 boot, 1 osd, 2 fileloader, 255 hidden
extern core_t cores[MAX_CORES];  // all of them (Setup > Autoload > Core)
extern int ncores;
extern char autoload_file[33];   // CORES AUTOLOAD: "" none
extern int autoload_s;
extern int cores_busy;           // CORES busy: the cards are USB drives of the PC
extern int msc_mode;             // SYS INFO: the USB drive mode (0 none, 1 SD, 2 SD2, 3 SD2 not found)
extern char fw_build[9];
extern int hw_id;                // SYS INFO: the board, 1 Go, 2 Mini, 3 Mini G (0 not read yet): the logo
void cores_read(void);          // the list, the groups (groups_read) and the view of the browser
