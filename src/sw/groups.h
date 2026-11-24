// The groups of the core list (docs/PLAN.md Ф6): one level, a name per core. The group of a core:
// [groups] <id> of karabas.ini (the user; empty: no group), else the table of groups.c by the id.
#pragma once
#include "cores.h"

#define MAX_GROUPS 32
extern char group_names[MAX_GROUPS][GROUP_LEN + 1];   // the groups of the visible cores, in their order
extern int group_count[MAX_GROUPS];                   // their visible cores
extern int ngroups;

void groups_read(void);          // cores[].group of every core (CFG GET), then groups_build()
void groups_build(void);         // group_names / group_count from cores[].group
int group_of(int core);          // the index in group_names, -1 none
int group_find(const char *name);   // the index of a name (the case ignored), -1 none
int groups_set(int core, const char *name);   // [groups] <id> = name written at once (CFG SET); 1 if it went
