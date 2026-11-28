#ifndef ZZS_PICO_INT_SHIM_H
#define ZZS_PICO_INT_SHIM_H
/*
 * pico/pico_int.h - ZZStrider stand-in for PicoDrive's internal header.
 *
 * cz80.c (PicoDrive commit 26ecb2b, built with PICODRIVE_HACKS) includes <pico/pico_int.h> and
 * <pico/memory.h> only to reach the Z80 page maps. PicoDrive's real pico_int.h drags in the
 * whole Mega Drive emulator, so ZZStrider ships this small header instead. pico_port.h and
 * pico_types.h next to it ARE the unmodified PicoDrive files.
 * Original ZZStrider code. ASCII only.
 */
#include "pico_port.h"
#include "memory.h"
#endif
