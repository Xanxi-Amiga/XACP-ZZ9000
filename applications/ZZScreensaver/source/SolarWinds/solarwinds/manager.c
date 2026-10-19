/* 2026 Xanxi. GPL-2.0-or-later: adapts Solar Winds to ZZGL/Core1. */
#include "zzgldemo.h"
#include "solarwinds.h"
static u32 preset=1,last_tick;
static double remaining;
void zzgldemo_preset(u32 p){preset=p;}
int zzgldemo_done(void){return 0;}
int zzgldemo_init(u32 pool,u32 pages,u32 zbuf,int fullclear)
{
    zzgl_config c;
    (void)fullclear;
    c.pool_base=pool;c.pool_pages=pages;c.pool_cacheable=1;
    c.zbuf=zbuf;c.width=640;c.height=480;c.ztrick=0;
    int rc=zzgl_init(&c);if(rc)return rc;
    if(!sw_init(preset,0x58414e58u))return RIXE_BADMODE;
    if(zzglGetError())return RIXE_DESC;
    last_tick=0;remaining=0;
    return 0;
}
int zzgldemo_render(u32 frame_no,u32 *tris_out)
{
    u32 now=rix_now();
    float dt=frame_no?(float)rix_ticks_to_us(now-last_tick)/1000000.0f:1.0f/60;
    last_tick=now;
    if(dt>8.0f/60)dt=8.0f/60;   /* bounded recovery after a stalled presenter */
    remaining+=dt;
    int steps=0;
    while(remaining>=1.0/60 && steps<8){sw_step();remaining-=1.0/60;steps++;}
    sw_draw(dt,frame_no==0);
    *tris_out=zzgl_tris_frame();
    return zzglGetError()?RIXE_DESC:0;
}
