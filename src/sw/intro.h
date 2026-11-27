// The boot intro (docs/PLAN.md "Boot intro", variant 4b "light out of the dark"): a light streak, four
// beams of the ZX stripe colours meet in a flash, the logo of the board comes out of the glow, a glare
// runs over it, then it shrinks into its place in the corner of the menu; MIDI (sound.c channels 9..15).
#pragma once

int intro_play(void);            // after settings_read / cores_read / video_set; 1 if a key cut it short
int intro_wanted(void);          // 0 with [ft812] intro=0 of karabas.ini
