// The cores on the card (RPC CORES SCAN / ITEM, in the order of [setup] core_sort), the autoload and
// the build of the firmware (SYS INFO).
#pragma once
#include <stdint.h>

#define MAX_CORES 128
typedef struct { char file[33], name[33], id[33]; uint8_t visible; } core_t;
extern core_t cores[MAX_CORES];  // all of them (Setup > Autoload > Core)
extern int ncores;
extern char autoload_file[33];   // CORES AUTOLOAD: "" none
extern int autoload_s;
extern int cores_busy;           // CORES busy: the cards are USB drives of the PC
extern char fw_build[9];
void cores_read(void);
