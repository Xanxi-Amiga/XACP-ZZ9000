/* libvgm resampler, with a smaller initial scratch reservation for Core1. */
#include <stdlib.h>
#include "system18_backend.h"
#ifdef ZZ_S18_REFERENCE
#include "../third_party/libvgm-system18/emu/Resampler.c"
#else
#define calloc zz_s18_calloc
#define malloc zz_s18_malloc
#define free zz_s18_free
#include "../third_party/libvgm-system18/emu/Resampler_baremetal.c"
#endif
