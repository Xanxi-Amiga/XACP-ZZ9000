/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLaraRIX A2: OpenLara adapter to the provided ZZQuake640 Core1 runtime.
 *
 * Unchanged from A1 in its contract with the runtime: QG_Create / QG_Tick / Host_Shutdown, the
 * layout checks, the .q2bss clear, the arena, the C++ constructors and the 8-byte stack trampoline.
 * A2: the whole preloaded container (a PAK with several files) goes to the engine's file layer
 * (ol_fs.c); diagnostic stages are published in SH_DIAG as 'O','L',0,stage.
 * ASCII only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zzquake_config.h"
#include "ol_api.h"
typedef unsigned int u32;
extern volatile u32 *shared;
extern volatile int zzq_running;
extern u32 _q2bss_start,_q2bss_end;
extern void zzq2_arena_init(u32,u32);
extern unsigned zzq_us_now(void);
extern int QG_GetKey(int*,int*);
extern void QG_GetMouseMove(int*,int*);
extern void zzq_diag_stage_d(void);
extern void Sys_Error(char*,...);
extern int zzq_printf(const char*,...);
extern void zz_rix_close(void);
static unsigned pack_end;
static int initialized;
static u32 le32(const unsigned char*p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
#define OL_RESERVE_LO   0x3D800000u   /* GPU reservation (texture pool, Z, binds, ring): heap limit */
#define OL_HEAP_MIN     (48u*1024u*1024u)   /* room the engine needs above the container */
unsigned zzq2_last_pak_end(void){return pack_end;}
unsigned ol_clock_us(void){return zzq_us_now();}
int ol_key(int*d,int*k){return QG_GetKey(d,k);}
void ol_mouse(int*x,int*y){QG_GetMouseMove(x,y);}
void ol_stop(void){zzq_running=0;}
void ol_diag(unsigned stage){shared[SH_DIAG]=0x4F4C0000u|(stage&0xFFFFu);}
void ol_fail(const char*s,int a,int b){zzq_printf("OL ERROR %s %d %d\n",s,a,b);shared[SH_DIAG]=0x4F4CEE00u;zz_rix_close();Sys_Error("OL %s %d %d",s,a,b);for(;;){}}
void zzq2_heap_fail(const char*w,u32 a,u32 b,u32 c,u32 d,u32 e){(void)w;(void)b;(void)c;(void)d;ol_fail("E230 HEAP",a,e);}
extern void ol_call_aligned(void (*fn)(void));
__asm__(".section .text.ol_call_aligned,\"ax\",%progbits\n.arm\n.align 2\n.global ol_call_aligned\nol_call_aligned:\npush {r4,lr}\nmov r4,sp\nbic r12,r4,#7\nmov sp,r12\nblx r0\nmov sp,r4\npop {r4,pc}\n.text\n");
static void init_aligned(void){
    unsigned base=shared[SH_PACK0_ADDR],size=shared[SH_PACK0_SIZE];
    unsigned bss=(unsigned)&_q2bss_start,end=(unsigned)&_q2bss_end;
    unsigned lo;
    if(base!=ZZQ_PACK_ARENA_BASE || size<76 || base+size>=OL_RESERVE_LO-OL_HEAP_MIN ||
       bss!=0x3EC00000u || end<=bss || end>ZZQ_PACK_ARENA_END)
        ol_fail("E201 LAYOUT",base,size);
    pack_end=base+size;
    for(u32*p=&_q2bss_start;p<&_q2bss_end;p++)*p=0;
    lo=(pack_end+0xfffffu)&~0xfffffu;
    zzq2_arena_init(lo,OL_RESERVE_LO);
    shared[SH_HUNK_BASE]=lo;shared[SH_HUNK_SIZE]=OL_RESERVE_LO-lo;
    {   extern void (*__ol_init_start[])(void),(*__ol_init_end[])(void);
        for(void (**p)(void)=__ol_init_start;p<__ol_init_end;p++)(*p)();
    }
    if(memcmp((const void*)base,"PACK",4))ol_fail("E202 PACK",0,0);
    zzq_printf("ZZOpenLara: container %u bytes at %08x, heap %08x..%08x\n",size,base,lo,OL_RESERVE_LO);
#ifdef OL_VIEWER
    {   /* A1 room viewer (reference build): the first file of the container, as A1 received it */
        const unsigned char*p=(const unsigned char*)base;
        unsigned dir=le32(p+4),len=le32(p+8),off,bytes;
        char name[57];
        if(dir>size-64||len<64||len%64)ol_fail("E202 PACK",dir,len);
        off=le32(p+dir+56);bytes=le32(p+dir+60);
        if(off>size||bytes>size-off||bytes<8||off+bytes>dir)ol_fail("E203 DATA",off,bytes);
        memcpy(name,p+dir,56);name[56]=0;
        zzq_printf("ZZOpenLaraRIX A1 viewer: %s %u bytes\n",name,bytes);
        ol_init(p+off,bytes,name);initialized=1;
    }
#else
    ol_init((const void*)base,size,"PACK");initialized=1;
#endif
}
void QG_Create(int argc,char**argv){(void)argc;(void)argv;zzq_diag_stage_d();ol_call_aligned(init_aligned);}
void QG_Tick(double dt){(void)dt;if(initialized)ol_call_aligned(ol_tick);}
void Host_Shutdown(void){if(initialized){ol_call_aligned(ol_shutdown);initialized=0;}}
volatile u32 zzq2_world_count;
void zzq_publish_engine_state(void){shared[SH_SV_ACTIVE]=0;shared[SH_DEMOPLAYBACK]=0;shared[SH_DEMONUM]=0;}
