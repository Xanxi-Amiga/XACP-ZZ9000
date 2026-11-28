#ifndef ZZS_Z80_INTF_H
#define ZZS_Z80_INTF_H
/* z80_intf.h - the only "Zet" entry the retained CPS-1 files use: the 68000 RESET instruction
 * resets the sound Z80 (cps_mem.cpp, CPSResetCallback). Implemented in zzs_burn.cpp, which
 * forwards to the CZ80-based sound board in zzs_sound.c. ASCII only. */
void ZetReset();
#endif
