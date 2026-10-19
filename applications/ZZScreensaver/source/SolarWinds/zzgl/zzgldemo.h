/* Solar Winds scene adapter for the validated Core1 presenter. 0BSD. */
#ifndef ZZGLDEMO_H
#define ZZGLDEMO_H
#include "zzgl.h"
int zzgldemo_done(void);
void zzgldemo_preset(u32 preset);
int zzgldemo_init(u32 pool_base,u32 pool_pages,u32 zbuf,int fullclear);
int zzgldemo_render(u32 frame_no,u32 *tris_out);
#endif
