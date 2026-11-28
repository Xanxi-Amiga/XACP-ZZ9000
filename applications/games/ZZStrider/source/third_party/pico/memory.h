#ifndef ZZS_PICO_MEMORY_SHIM_H
#define ZZS_PICO_MEMORY_SHIM_H
/*
 * pico/memory.h - the Z80 part of PicoDrive's memory map interface, as used by cz80.c:
 * one entry per 1 KiB page, holding (host pointer >> 1) for direct memory or
 * (handler address >> 1) | MAP_FLAG for a handler. The tables themselves are defined by the
 * sound board (arm/zzs_sound.c). Declarations taken from PicoDrive pico/memory.h (26ecb2b).
 * ASCII only.
 */
#include "pico_port.h"

#define Z80_MEM_SHIFT 10
extern uptr z80_read_map [0x10000 >> Z80_MEM_SHIFT];
extern uptr z80_write_map[0x10000 >> Z80_MEM_SHIFT];
typedef unsigned char (z80_read_f)(unsigned short a);
typedef void (z80_write_f)(unsigned int a, unsigned char data);

#define MAP_FLAG ((uptr)1 << (sizeof(uptr) * 8 - 1))
#define map_flag_set(x) ((x) & MAP_FLAG)

#endif
