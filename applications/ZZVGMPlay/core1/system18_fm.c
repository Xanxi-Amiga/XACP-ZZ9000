/* Unmodified libvgm GPGX/MAME engine; platform allocation only is redirected. */
#include <stdlib.h>
#include "system18_backend.h"
#define SNDDEV_SELECT
#define SNDDEV_YM2612
#ifndef ZZ_S18_REFERENCE
#define calloc zz_s18_calloc
#define malloc zz_s18_malloc
#define free zz_s18_free
#endif
#include "../third_party/libvgm-system18/emu/cores/fmopn.c"
