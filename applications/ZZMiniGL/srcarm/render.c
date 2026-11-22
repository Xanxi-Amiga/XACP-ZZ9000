#include "wire.h"
#include "zzgl.h"
#include "rix_diag.h"
#include "rix_window.h"
#include <string.h>
static u32 colors[2]={RIX_FB0_ARM,RIX_FB1_ARM};
/* A8.26: 800 x 600.  The picture sizes 640 x 480 and 320 x 240 keep the 640 x 481 buffers of the RasterIX
   window (FB0, FB1, depth in FB2) exactly as before.  At 800 x 600 a buffer is 800 x 601 x 2 = 961,600
   bytes: the two colour buffers still fit in the window, below the ring of display lists (FB0
   0x06600000, FB1 0x066EAC40, end 0x067D5880, ring 0x067E3000; the 64 texture pages of the window that
   FB1 covers are only used by rix_init's probe, which runs before); the depth buffer goes to the Core1
   arena, one section (0x3EE, mmu.c) holding a guard page, the buffer and a second guard page.  A third
   guard page lies between FB1 and the ring.  The guards are filled at start and compared at start,
   every 64 frames and at the end: a change is fatal (ZE_GUARD).  The geometry is set after the init
   ladder, which stays at 640 x 481 (rix_set_geometry, librixgpu 1.4). */
#define ZZ_BIG_W 800u
#define ZZ_BIG_H 600u
#define ZZ_BIG_STRIDE (ZZ_BIG_W*(ZZ_BIG_H+1u)*2u)     /* 0xEAC40 */
#define ZZ_BIG_FBGUARD 0x067D6000u                    /* FB1 ends 0x067D5880; the ring starts 0x067E3000 */
#define ZZ_BIG_ZGUARD0 0x3EE00000u
#define ZZ_BIG_Z       0x3EE01000u                    /* ends 0x3EEEBC40 */
#define ZZ_BIG_ZGUARD1 0x3EEEC000u
#define ZZ_GUARD_WORDS 1024u
static u32 geo_w=RIX_W,geo_h=RIX_H,geo_pitch=RIX_PITCH;
static int big;
static void guard_fill(u32 arm){volatile u32 *g=(volatile u32 *)rix_ptr(arm);u32 i;for(i=0;i<ZZ_GUARD_WORDS;i++)g[i]=0x5A5AC3C3u^(i*0x01010101u);}
static u32 guard_changed(u32 arm){volatile u32 *g=(volatile u32 *)rix_ptr(arm);u32 i,n=0;for(i=0;i<ZZ_GUARD_WORDS;i++)if(g[i]!=(0x5A5AC3C3u^(i*0x01010101u)))n++;return n;}
static int guards_ok(void){return !big||(guard_changed(ZZ_BIG_FBGUARD)|guard_changed(ZZ_BIG_ZGUARD0)|guard_changed(ZZ_BIG_ZGUARD1))==0u;}
static volatile u32 stats[RS_COUNT], diag[SD_COUNT], caps, hz;
static u32 desc[RIX_TRI_DESC_WORDS], current, fence;
static volatile zu32 *report;
extern int zz_p96_copy(volatile u32 *,const volatile u32 *,u32,u32,u32,u32,u32,u32);
static void set_target(void){
 zzgl_target t;
 fence=fence>=65534u?1u:fence+1u;
 t.color=colors[current]; rix_fence_reset(t.color); t.fence=fence; zzgl_set_target(&t);
}
#ifndef RIXDMA_EXTRA
#define RIXDMA_EXTRA
#endif
/* A6: the Core1 ZZGL and the wire protocol agree on the texture name space */
typedef char zz_tex_names_agree[(ZZGL_MAX_TEXTURES == ZZ_MAX_TEXTURES) ? 1 : -1];
static u32 tex_refused_before;   /* A8 */
#if defined(ZZGL_STATE_ARENA)
#define ZZ_VC_ATTR __attribute__((section(".zz_vcache"), aligned(32)))
#define ZZ_VS_ATTR __attribute__((section(".zz_vstatic"), aligned(32)))
#else
#define ZZ_VC_ATTR
#define ZZ_VS_ATTR
#endif
static union { zu32 w[ZZ_VC_BYTES/4u]; zu8 b[ZZ_VC_BYTES]; } c1_vcache ZZ_VC_ATTR;   /* A8.17: kept vertex loads */
static union { zu32 w[ZZ_VS_BYTES/4u]; zu8 b[ZZ_VS_BYTES]; } c1_vstatic ZZ_VS_ATTR;  /* A8.21: the static store */
/* a kept record's place: the range was checked by zpacket (zkept_range_ok), it lies in one of the two */
static zu8 *c1_kept(zu32 off){return off<ZZ_VC_BYTES?c1_vcache.b+off:c1_vstatic.b+(off-ZZ_VC_BYTES);}
static zu32 c1_res_n,c1_nprims,c1_used,c1_flushing;   /* A8.17: Core1 unit-1 pass queue (below) */
/* A8.4: texture pool in the Core1 arena (mmu.c) instead of the 64 pages (128 KiB) of the RasterIX window:
   8192 pages of 2 KiB = 16 MiB, the ZZQuakeGL pool.  The window page RIX_TEX_ARM stays the init probe's. */
#define ZZ_TEX_POOL_ARM   0x3D800000u
#define ZZ_TEX_POOL_PAGES 8192u
static int dma_disabled;   /* A5: set after the first DMA presentation failure of a run */
static int close_frame(int present);
int zrender_init(volatile zu32 *r){
 rix_config cfg; rix_ring_cfg ring; zzgl_config gl;
 rix_vertex a={0},b,c;u32 x,y; int rc;
 volatile u16 *tex=(volatile u16 *)rix_ptr(RIX_TEX_ARM);
 report=r;dma_disabled=0;
 big=(r[MH_WIDTH]==ZZ_BIG_W&&r[MH_HEIGHT]==ZZ_BIG_H);   /* A8.26 */
 if(!big&&!((r[MH_WIDTH]==640&&r[MH_HEIGHT]==480)||(r[MH_WIDTH]==320&&r[MH_HEIGHT]==240)))return ZE_ARG;
 colors[0]=RIX_FB0_ARM;colors[1]=big?RIX_FB0_ARM+ZZ_BIG_STRIDE:RIX_FB1_ARM;
 geo_w=big?ZZ_BIG_W:RIX_W;geo_h=big?ZZ_BIG_H:RIX_H;geo_pitch=geo_w*2u;
 memset(&cfg,0,sizeof cfg); memset(&ring,0,sizeof ring); memset(&gl,0,sizeof gl);
 a.sx=200;a.sy=151;a.z=.5f;a.invw=1;a.r=a.g=a.b=a.a=1;
 b=a;b.sx=440;b.u=1;c=a;c.sx=320;c.sy=401;c.u=.5f;c.v=1;
 if(!rix_tri_desc(&a,&b,&c,desc))return ZE_GPU;
 for(y=0;y<256;y++)for(x=0;x<256;x++)tex[y*256+x]=(u16)(((x&31)<<11)|((y&63)<<5)|((x^y)&31));
 cfg.flags=RIXF_SPIN|RIXF_NODMA|RIXF_NOSWAP;cfg.idle_tick_us=100;
 cfg.stats=stats;cfg.sd=diag;cfg.caps_out=&caps;cfg.timer_hz_out=&hz;cfg.dma_prog=RIX_DL1_ARM;
 cfg.probe_tri=desc;cfg.probe_x=320;cfg.probe_y=250;cfg.tmu_env=RIX_TMU_ENV_REPLACE;
 cfg.tmu_cfg=RIX_TMU_CFG_RGB565(8u,8u,1);cfg.tex_pages=64;cfg.tex_base=RIX_TEX_ARM;
 r[MH_PHASE]=2;rc=rix_init(&cfg);r[MH_CAPS]=rix_caps();r[MH_TIMER]=hz;
 r[MH_GPU_ID]=diag[SD_ID_MAGIC];r[MH_GPU_VER]=diag[SD_ID_VERSION];r[MH_LADDER]=diag[SD_LADDER_FAILED];
 if(rc)return rc;
 if((rix_caps()&(RIXCAP_FENCE_MEM|RIXCAP_TRI_DDR|RIXCAP_TEX))!=(RIXCAP_FENCE_MEM|RIXCAP_TRI_DDR|RIXCAP_TEX))return ZE_GPU;
 r[MH_PHASE]=3;
 /* A3: three 16 KiB chunks (DL0, DL1, DL2) so Core1 builds chunk N+2 while the GPU draws N and N+1;
    the two 512-byte bind lists move to the scratch page (free after rix_init's self-tests). */
 if(big){
   if((rc=rix_set_geometry(ZZ_BIG_W,ZZ_BIG_H))!=0)return rc;
   guard_fill(ZZ_BIG_FBGUARD);guard_fill(ZZ_BIG_ZGUARD0);guard_fill(ZZ_BIG_ZGUARD1);
 }
 ring.ring_base=RIX_DL0_ARM;ring.ring_slots=3;ring.chunk_bytes=16384;
 ring.color_bufs=colors;ring.color_count=2;ring.fence_x=RIX_CHUNK_FENCE_X;ring.bind_base=RIX_SCRATCH_ARM;
 if((rc=rix_ring_init(&ring))!=0)return rc;
 if(rix_caps()&RIXCAP_TEX_DDR){if((rc=rix_frame_set_bind_mode(RIX_BIND_LIST))!=0)return rc;}
 gl.pool_base=ZZ_TEX_POOL_ARM;gl.pool_pages=ZZ_TEX_POOL_PAGES;gl.pool_cacheable=1;gl.zbuf=(r[MH_NOZBUF]==1u)?0u:(big?ZZ_BIG_Z:RIX_FB2_ARM);   /* A6: ZZMiniGLNoZ */
 gl.width=r[MH_WIDTH];gl.height=r[MH_HEIGHT];if((rc=zzgl_init(&gl))!=0)return rc;
 /* Opt-in: profiling inserts per-triangle timers and must not be used for
    normal FPS comparisons. */
 zzgl_set_profile(r[MH_PROFILE_SWITCH] ? 1 : 0);
 r[MH_PROFILE_ACTIVE] = r[MH_PROFILE_SWITCH] ? 1u : 0u;
 current=0;fence=0;set_target();zzgl_set_alpha_textures(1);c1_res_n=c1_nprims=c1_used=c1_flushing=0;
 if(big){
   /* A8.26: the placement is proved before the application draws: a frame that only clears colour and
      depth is drawn and fenced (never shown); the depth clear must have replaced marks written in the
      depth buffer, the colour clear marks in the colour buffer, and the three guards must be intact */
   volatile u16 *z=(volatile u16 *)rix_ptr(ZZ_BIG_Z),*cb=(volatile u16 *)rix_ptr(colors[current]);u32 i,left=0;
   for(i=0;i<ZZ_BIG_W*ZZ_BIG_H;i+=4099u){cb[i]=0x5A5Au;if(gl.zbuf)z[i]=0x5A5Au;}
   zzglClearColor(0.0f,0.0f,0.0f,0.0f);zzglClear(0x4100u);
   if((rc=close_frame(0))!=0)return rc;
   for(i=0;i<ZZ_BIG_W*ZZ_BIG_H;i+=4099u){if(cb[i]==0x5A5Au)left++;if(gl.zbuf&&z[i]==0x5A5Au)left++;}
   if(left||!guards_ok())return ZE_GUARD;
 }
 r[MH_PHASE]=4;return 0;
}
static int close_frame(int present){
 u32 t,us,w=report[MH_WIDTH],h=report[MH_HEIGHT],i,j,v,hash=2166136261u,first=0,vary=0,nz=0;
 volatile u16 *p;int rc;
 if(!present && !zzgl_frame_open())return 0;
 rc=zzgl_swap();if(rc)return rc;
 if(big&&(!present||(report[MH_FRAMES]&63u)==63u)&&!guards_ok())return ZE_GUARD;   /* A8.26 */
 t=rix_now();rc=rix_frame_wait(colors[current],fence,2000000,0,0);
 us=rix_ticks_to_us(rix_now()-t);report[MH_FENCE_US]+=us;
 if(zzgl_frame()->first_submit_tick)report[MH_GPU_SPAN_US]+=rix_ticks_to_us(rix_now()-zzgl_frame()->first_submit_tick);
 if(rc)return rc;
 report[MH_TRIS]+=zzgl_tris_frame();
 {const rix_frame *fr=zzgl_frame();const zzgl_stats *st=zzgl_get_stats();
  report[MH_CHUNKS]+=fr->chunks;report[MH_RINGWAIT_US]+=fr->ringwait_us+fr->bind_wait_us;
  report[MH_LIST_WORDS]+=fr->words;   /* A7 */
  report[MH_XFORM_US]+=rix_ticks_to_us(st->xform_ticks);report[MH_SETUP_US]+=rix_ticks_to_us(st->setup_ticks+st->end_ticks);
  report[MH_VERTS]+=st->verts_xformed;report[MH_IDX_TRIS]+=st->idx_tris;
  report[MH_FAST_CLIP_TRIS]+=st->fast_clip_tris;
  report[MH_SCREEN_CLIP_TRIS]+=st->screen_clip_tris;
  if(report[MH_PROFILE_ACTIVE]){
    report[MH_DESC_US]+=rix_ticks_to_us(st->desc_ticks);
    report[MH_EMIT_US]+=rix_ticks_to_us(st->emit_ticks);
  }
 }
 if(present){
   report[MH_BUF]=colors[current]+(geo_h-h)*geo_pitch;
   if(report[MH_FRAMES]==0 || report[MH_FRAMES]==15){
     p=(volatile u16 *)rix_ptr(report[MH_BUF]);first=p[0];
     for(j=0;j<h;j+=8)for(i=0;i<w;i+=8){v=p[j*geo_w+i];hash=(hash^v)*16777619u;nz+=(v!=0);vary+=(v!=first);}
     report[MH_CHECK]=hash;report[MH_VARY]=vary;
     /* RGB565 texture is almost monochrome; count conspicuously chromatic GPU samples. */
     { u32 ch=0u;
       for (j=0;j<h;j+=8) for (i=0;i<w;i+=8) {
         u32 c16=p[j*geo_w+i], cr=((c16>>11)&31u)*255u/31u,
             cg=((c16>>5)&63u)*255u/63u, cb=(c16&31u)*255u/31u;
         if ((cr>cg?cr-cg:cg-cr)>24u || (cg>cb?cg-cb:cb-cg)>24u) ch++;
       }
       report[MH_NONZERO] = ch & 65535u;
     }
   }
   report[MH_FRAMES]++;current^=1u;
 }
 set_target();return 0;
}
/* Core1 direct presentation is bounded to the low DDR aperture, with no
   access to XACP code, firmware, or the 2MB RasterIX color/Z window.
   The source fence has already completed in close_frame(1).
   Destination is supplied by p96LockBitMap and held until ACK. */
static int p96_destination(zu32 a,zu32 pitch){
 zu32 width=report[MH_WIDTH],height=report[MH_HEIGHT],length;
 if(!width||!height||width>geo_w||height>geo_h)return 0;
 if((a&3u)||(pitch&3u)||pitch<width*2u||pitch>4096u)return 0;
 length=(height-1u)*pitch+width*2u;
 return a>=ZZ_P96_LOW_ARM && a<=ZZ_P96_HIGH_ARM-length;
}
/* A8.23: close = 0 is Z_SHOW: the frame closed last (Z_PRESENT_NONE) is copied, nothing is closed; the
   buffer it lies in is not drawn into before the next close */
static int fast_present(zu32 a,zu32 pitch,zu32 format,zu32 mode,int close){
 const volatile u32 *src;volatile u32 *dst;
 zu32 t;
 int rc;
 if(!p96_destination(a,pitch))return ZE_ARG;
 if(format!=ZZ_P96_FMT_RGB565_PC && format!=ZZ_P96_FMT_RGB565_BE)return ZE_ARG;
 if(mode!=ZZ_P96_COPY_SCALAR && mode!=ZZ_P96_COPY_BURST32 && mode!=ZZ_P96_COPY_DMA)return ZE_ARG;
 if(close){rc=close_frame(1);if(rc)return rc;}
 else{
   if(!report[MH_FRAMES])return ZE_ARG;   /* no frame was closed yet */
   /* A8.25: the PL330 copy shares its channel and its program with the transport of the display lists.
      After a close the GPU is idle and nothing is in flight; a Z_SHOW may come while the next frame is
      open (lists sent or queued): then the CPU copies.  A8.23/A8.24 started the PL330 there (640x480
      only: at 320x240 the rows are not contiguous and the CPU always copies) and corrupted the list in
      flight. */
   if(mode==ZZ_P96_COPY_DMA&&(zzgl_frame_open()||rix_frame_queued()))mode=ZZ_P96_COPY_BURST32;
 }
 src=(const volatile u32 *)rix_ptr(report[MH_BUF]);
 dst=(volatile u32 *)rix_ptr(a);
 t=rix_now();
 rc=-1;
 if(mode==ZZ_P96_COPY_DMA){
   /* A5: the PL330 copies the whole surface in one program when the rows are contiguous on both
      sides (full-width 640 rows, pitch 1280 = RIX_PITCH) and the addresses are 64-byte aligned;
      the 16-bit swap is done by the DMAC (CCR endian swap).  Anything else, an unavailable DMAC,
      a fault or a timeout falls back to the CPU copy of A1c..A4R (and disables the DMA for the run). */
   zu32 bytes=report[MH_HEIGHT]*pitch;
   if(dma_disabled)rc=-1;
   else if(!rix_dmacopy_available()){report[MH_P96_DMA_INFO]=0xA5000002u;dma_disabled=1;}
   else if(pitch!=geo_pitch||report[MH_WIDTH]*2u!=pitch||((report[MH_BUF]|a|bytes)&63u)||format!=ZZ_P96_FMT_RGB565_BE){report[MH_P96_DMA_INFO]=0xA5000003u;dma_disabled=1;}
   else{
     /* A6: the unified librixgpu (ZZHalfLife 1.4.2) copy always swaps the bytes: big-endian P96 only */
     int st=rix_dmacopy_start(report[MH_BUF],a,bytes RIXDMA_EXTRA);
     if(st==0){
       zu32 t0=rix_now();
       for(;;){
         st=rix_dmacopy_poll();
         if(st==1){rc=2;break;}
         if(st==2){report[MH_P96_DMA_INFO]=rix_dmacopy_fault_info();break;}
         if(rix_ticks_to_us(rix_now()-t0)>50000u){rix_dmacopy_abort();report[MH_P96_DMA_INFO]=0xA5000001u;break;}
       }
     }else report[MH_P96_DMA_INFO]=0xA5000000u|(zu32)st;
     if(rc!=2){report[MH_P96_DMA_FAULTS]++;dma_disabled=1;}
   }
   if(rc==2)report[MH_P96_DMA_FRAMES]++;
   else mode=ZZ_P96_COPY_BURST32;
 }
 if(rc!=2){
   rc=zz_p96_copy(dst,src,report[MH_WIDTH],report[MH_HEIGHT],
                  pitch,geo_pitch,format,mode);
   if(rc<0)return ZE_ARG;
 }
 /* The P96 lock is held until the 68k receives the ACK. */
 dsb();
 report[MH_P96_COPY_US]+=rix_ticks_to_us(rix_now()-t);
 report[MH_P96_FAST_FRAMES]++;
 if (report[MH_FRAMES]==1u) {
   u32 y,x,bad=0;
   for(y=0;y<report[MH_HEIGHT];y+=8u) {
     const volatile u16 *sr=(const volatile u16 *)((const volatile zu8 *)src+y*geo_pitch);
     const volatile u16 *dr=(const volatile u16 *)((const volatile zu8 *)dst+y*pitch);
     for(x=0;x<report[MH_WIDTH];x+=8u) {
       u16 v=sr[x], e=(format==ZZ_P96_FMT_RGB565_BE)?(u16)((v>>8)|(v<<8)):v;
       if(dr[x]!=e)bad++;
     }
   }
   report[MH_NONZERO]=(report[MH_NONZERO]&65535u)|((bad&65535u)<<16);
 }
 return 0;
}
#define U(i) zget(p+4*(i))
#define F(i) zreal(U(i))
/* ---- A8.17: raw records and the unit-1 pass kept by Core1 ----
   The 68k sends its own words (no byte assembly, colours packed); the reordering and the expansion to
   ZZGL's float layout are done here.  The unit-1 lightmap pass (A8.12/A8.13, until now queued and
   replayed by the 68k) is queued here from the resident vertices (xyz, s1 t1) and the indices of each
   Z_DRAW_ELEMENTS_MT, and replayed on Z_MT_FLUSH as the very records the 68k sent: blend ZERO,
   SRC_COLOR, depth writes off, GL_REPLACE, one bind per lightmap, Z_ARRAY_LOAD (xyz s1 t1) +
   Z_DRAW_ELEMENTS + Z_ARRAY_UNLOCK per primitive, then the application's state back. */
#if defined(ZZGL_STATE_ARENA)
#define ZZ_C1_ATTR __attribute__((section(".zz_arena.c1"), aligned(32)))
#else
#define ZZ_C1_ATTR
#endif
#define C1_BUF_BYTES (128u*1024u)
#define C1_MAX_PRIMS 1536u
static union { float f[ZZ_RAW_MAX_VERTS*12u]; zu8 b[256u*256u*3u]; zu32 w[1]; } c1_scratch ZZ_C1_ATTR;
static float c1_res[ZZ_RAW_MAX_VERTS*5u] ZZ_C1_ATTR;          /* resident xyz s1 t1 (Z_ARRAY_LOAD_RAW with ZZ_VF_UV1) */
static union { zu32 w[C1_BUF_BYTES/4u]; zu8 b[C1_BUF_BYTES]; } c1_buf ZZ_C1_ATTR;   /* ready payloads */
static struct { zu32 name,load,draw; } c1_prim[C1_MAX_PRIMS] ZZ_C1_ATTR;
static inline zu32 rw(const zu8 *q,zu32 be){zu32 w=zget(q);return be?zswap32(w):w;}
static inline float rf(const zu8 *q,zu32 be){return zreal(rw(q,be));}
/* n raw vertices -> ZZGL's packed floats (xyz [n] [st] [rgba]); returns that format */
static zu32 raw_expand(const zu8 *s,zu32 n,zu32 fmt,zu32 flags,float *d,float *res){
 zu32 i,be=flags&ZZ_RAW_BE,ck=flags&0xFFu,vb=zraw_vertex_bytes(fmt,flags);
 for(i=0;i<n;i++,s+=vb){
   const zu8 *q=s;
   d[0]=rf(q,be);d[1]=rf(q+4,be);d[2]=rf(q+8,be);
   if(res){res[0]=d[0];res[1]=d[1];res[2]=d[2];}
   d+=3;q+=12;
   if(fmt&ZZ_VF_NORMAL){d[0]=rf(q,be);d[1]=rf(q+4,be);d[2]=rf(q+8,be);d+=3;q+=12;}
   if(fmt&ZZ_VF_UV){d[0]=rf(q,be);d[1]=rf(q+4,be);d+=2;q+=8;}
   if(fmt&ZZ_VF_UV1){if(res){res[3]=rf(q,be);res[4]=rf(q+4,be);}q+=8;}
   if(res)res+=5;
   if(fmt&ZZ_VF_COLOR){
     if(ck==ZZ_RAWCOL_FLOAT){d[0]=rf(q,be);d[1]=rf(q+4,be);d[2]=rf(q+8,be);d[3]=rf(q+12,be);}
     else if(ck==ZZ_RAWCOL_RGBA8||ck==ZZ_RAWCOL_RGB8){d[0]=q[0]/255.f;d[1]=q[1]/255.f;d[2]=q[2]/255.f;d[3]=ck==ZZ_RAWCOL_RGB8?1.f:q[3]/255.f;}
     else{zu32 w=rw(q,be),r,g,b,a;
       if(ck==ZZ_RAWCOL_ARGB){a=w>>24;r=(w>>16)&255u;g=(w>>8)&255u;b=w&255u;}else{b=w>>24;g=(w>>16)&255u;r=(w>>8)&255u;a=w&255u;}
       d[0]=r/255.f;d[1]=g/255.f;d[2]=b/255.f;d[3]=a/255.f;}
     d+=4;
   }
 }
 return fmt&(ZZ_VF_POS3|ZZ_VF_NORMAL|ZZ_VF_UV|ZZ_VF_COLOR);
}
int zrender_visit(zu32 op,const zu8 *p,void *unused);
static void c1_rec(zu32 op,zu32 n,zu32 a,zu32 b){zu8 q[8];zput(q,a);zput(q+4,b);(void)n;zrender_visit(op,q,0);}
/* the queued unit-1 pass, then the application's state as the flags and words give it */
static void c1_mt_flush(zu32 pf,zu32 env,zu32 bound,zu32 bs,zu32 bd){
 zu32 i,k;static zu32 done[C1_MAX_PRIMS/32u+1u];
 if(!c1_nprims||c1_flushing)return;
 c1_flushing=1;
 for(i=0;i<C1_MAX_PRIMS/32u+1u;i++)done[i]=0;
 if(!(pf&1u))c1_rec(Z_ENABLE,2,0x0BE2u,1);
 c1_rec(Z_BLEND,2,0,0x300u);
 if(pf&2u)c1_rec(Z_DEPTHMASK,1,0,0);
 if(env!=0x1E01u)c1_rec(Z_TEXENV,2,0x2200u,0x1E01u);
 for(i=0;i<c1_nprims;i++){
   zu32 name;
   if(done[i>>5]&(1u<<(i&31u)))continue;
   name=c1_prim[i].name;c1_rec(Z_BIND,1,name,0);
   if(pf&4u)c1_rec(Z_TEXPAR,2,0x2800u,0x2600u);
   for(k=i;k<c1_nprims;k++){
     if((done[k>>5]&(1u<<(k&31u)))||c1_prim[k].name!=name)continue;
     done[k>>5]|=1u<<(k&31u);
     zrender_visit(Z_ARRAY_LOAD,c1_buf.b+c1_prim[k].load,0);
     zrender_visit(Z_DRAW_ELEMENTS,c1_buf.b+c1_prim[k].draw,0);
     zrender_visit(Z_ARRAY_UNLOCK,c1_buf.b,0);
   }
 }
 c1_rec(Z_BIND,1,bound,0);
 if(env!=0x1E01u)c1_rec(Z_TEXENV,2,0x2200u,env);
 c1_rec(Z_BLEND,2,bs==0xFFFFFFFFu?1u:bs,bd==0xFFFFFFFFu?0u:bd);
 if(!(pf&1u))c1_rec(Z_ENABLE,2,0x0BE2u,0);
 if(pf&2u)c1_rec(Z_DEPTHMASK,1,1,0);
 c1_nprims=c1_used=0;c1_flushing=0;
}
/* Z_DRAW_ELEMENTS_MT: the resident range and these indices, for the pass (flushed first when full) */
/* A8.21: only the vertices the indices name are queued, renumbered in the order they come (a locked range
   may be a whole static chunk of which a few polygons are drawn); same positions, same triangles */
static unsigned short c1_map[ZZ_RAW_MAX_VERTS];static zu32 c1_mapstamp[ZZ_RAW_MAX_VERTS],c1_mapcur;
static void c1_mt_queue(const zu8 *p,const void *idx){
 const unsigned short *ix=(const unsigned short *)idx;
 zu32 nb=U(1),nv=c1_res_n,ib=(nb*2u+3u)&~3u,mv=nv<nb?nv:nb,bytes=8u+20u*mv+8u+ib,o,i,n=0;zu8 *v,*d;
 if(!nv||nv>ZZ_RAW_MAX_VERTS||bytes>C1_BUF_BYTES)return;
 for(i=0;i<nb;i++)if(ix[i]>=nv)return;   /* an index outside the range: the unit-0 draw has reported it */
 if(c1_nprims>=C1_MAX_PRIMS||c1_used+bytes>C1_BUF_BYTES)c1_mt_flush(U(3)&7u,U(4),U(5),U(6),U(7));
 o=c1_used;
 if(!++c1_mapcur){for(i=0;i<ZZ_RAW_MAX_VERTS;i++)c1_mapstamp[i]=0;c1_mapcur=1;}
 v=c1_buf.b+o+8;
 for(i=0;i<nb;i++){
   zu32 k=ix[i];
   if(c1_mapstamp[k]!=c1_mapcur){c1_mapstamp[k]=c1_mapcur;c1_map[k]=(unsigned short)n;memcpy(v+20u*n,(const zu8 *)c1_res+20u*k,20u);n++;}
 }
 zput(c1_buf.b+o,ZZ_VF_POS3|ZZ_VF_UV);zput(c1_buf.b+o+4,n);
 d=c1_buf.b+o+8u+20u*n;
 zput(d,U(0));zput(d+4,nb);
 for(i=0;i<nb;i++)zput16(d+8u+2u*i,c1_map[ix[i]]);
 if(nb&1u)zput16(d+8u+2u*nb,0);
 c1_prim[c1_nprims].name=U(2);c1_prim[c1_nprims].load=o;c1_prim[c1_nprims].draw=o+8u+20u*n;c1_nprims++;
 c1_used+=8u+20u*n+8u+ib;
}
/* A8.18: n indices in the producer's order -> ours (in the conversion buffer when they must be swapped) */
static const unsigned short *raw_indices(const zu8 *s,zu32 n,zu32 be){
 zu32 i;unsigned short *d=(unsigned short *)(void *)c1_scratch.b;
 if(!be)return (const unsigned short *)(const void *)s;
 for(i=0;i<n;i++,s+=2)d[i]=(unsigned short)((zu32)s[0]<<8|s[1]);
 return d;
}
/* ARGB4444 words -> RGB bytes, as the 68k converted them until A8.16 */
static const zu8 *argb4444_rgb(const zu8 *s,zu32 n,zu32 be){
 zu32 i;zu8 *d=c1_scratch.b;
 for(i=0;i<n;i++,s+=2,d+=3){zu32 v=be?((zu32)s[0]<<8|s[1]):((zu32)s[1]<<8|s[0]),r=(v>>8)&15u,g=(v>>4)&15u,b=v&15u;d[0]=(zu8)(r<<4|r);d[1]=(zu8)(g<<4|g);d[2]=(zu8)(b<<4|b);}
 return c1_scratch.b;
}
int zrender_visit(zu32 op,const zu8 *p,void *unused){
 u32 i,glerr;int rc=0;(void)unused;
 if(op==Z_IMAGE||op==Z_SUBIMAGE){u32 up,ds,ev;zzgl_tex_counts(&up,&ds,&tex_refused_before,&ev);}   /* A8 */
 switch(op){
 case Z_CLEAR:zzglClear(U(0));break;
 case Z_CLEARCOLOR:zzglClearColor(F(0),F(1),F(2),F(3));break;
 case Z_VIEW:zzglViewport(U(0),U(1),U(2),U(3));break;
 case Z_MATRIX:zzglMatrixMode(U(0));break;
 case Z_IDENTITY:zzglLoadIdentity();break;
 case Z_MOVE:zzglTranslatef(F(0),F(1),F(2));break;
 case Z_ROT:zzglRotatef(F(0),F(1),F(2),F(3));break;
 case Z_SCALE:zzglScalef(F(0),F(1),F(2));break;
 case Z_ENABLE:if(U(1))zzglEnable(U(0));else zzglDisable(U(0));break;
 case Z_DEPTHFUNC:zzglDepthFunc(U(0));break;
 case Z_DEPTHMASK:zzglDepthMask(U(0));break;
 case Z_BIND:zzglBindTexture(ZGL_TEXTURE_2D,U(0));break;
 case Z_IMAGE:case Z_SUBIMAGE:{
   zu32 bpp=U(2);const zu8 *px=p+24;
   if(bpp==ZZ_TEX_ARGB4444_LE||bpp==ZZ_TEX_ARGB4444_BE){px=argb4444_rgb(px,U(0)*U(1),bpp==ZZ_TEX_ARGB4444_BE);bpp=3;}   /* A8.17 */
   if(op==Z_IMAGE){zzglTexImage2D(ZGL_TEXTURE_2D,0,bpp,U(0),U(1),0,bpp==3?ZGL_RGB:bpp==1?ZGL_LUMINANCE:ZGL_RGBA,ZGL_UNSIGNED_BYTE,px);report[MH_TEX_FNV]=zzgl_texture_debug_fnv();}
   else zzglTexSubImage2D(ZGL_TEXTURE_2D,0,U(3),U(4),U(0),U(1),bpp==3?ZGL_RGB:bpp==1?ZGL_LUMINANCE:ZGL_RGBA,ZGL_UNSIGNED_BYTE,px);
   break;
 }
 case Z_DRAW_PACKED:zzgl_draw_arrays(U(0),ZGL_VF_POS3|ZGL_VF_UV,U(1),(const float *)(const void *)(p+8));break;
 case Z_DRAW:{
   u32 n=U(1);zzglBegin(U(0));p+=8;
   for(i=0;i<n;i++,p+=36){zzglColor4f(F(3),F(4),F(5),F(6));zzglTexCoord2f(F(7),F(8));zzglVertex3f(F(0),F(1),F(2));}
   zzglEnd();break;
 }
 case Z_FLUSH:if(zzgl_frame_open())rc=rix_frame_flush((rix_frame *)zzgl_frame());break;
 case Z_PRESENT:rc=close_frame(1);break;
 case Z_P96_PROBE:{
   zu32 addr=U(0);
   if(addr>=ZZ_P96_LOW_ARM && addr<=ZZ_P96_HIGH_ARM-8u && !(addr&3u)){
     const volatile u32 *v=(const volatile u32 *)rix_ptr(addr);
     dmb();
     report[MH_P96_PROBE]=(v[0]==U(1) && v[1]==U(2))?1u:2u;
   }else report[MH_P96_PROBE]=2u;
   if(report[MH_P96_PROBE]!=1u)report[MH_P96_REJECTS]++;
   dsb();break;
 }
 case Z_PRESENT_DIRECT:rc=fast_present(U(0),U(1),U(2),U(3),1);break;
 case Z_SHOW:rc=fast_present(U(0),U(1),U(2),U(3),0);break;   /* A8.23 */
 case Z_POINTSIZE:zzglPointSize(F(0));break;
 case Z_PRESENT_NONE:rc=close_frame(1);break;   /* A7 OFFSCREEN: fenced and counted, not copied */
 case Z_FINISH:case Z_STOP:rc=close_frame(0);break;
 case Z_FRUSTUM:zzglFrustum(F(0),F(1),F(2),F(3),F(4),F(5));break;
 case Z_ORTHO:zzglOrtho(F(0),F(1),F(2),F(3),F(4),F(5));break;
 case Z_PUSH:zzglPushMatrix();break; case Z_POP:zzglPopMatrix();break;
 case Z_BLEND:zzglBlendFunc(U(0),U(1));break;case Z_ALPHA:zzglAlphaFunc(U(0),F(1));break;
 case Z_TEXENV:zzglTexEnvf(ZGL_TEXTURE_ENV,U(0),(float)U(1));break;
 case Z_TEXPAR:zzglTexParameterf(ZGL_TEXTURE_2D,U(0),(float)U(1));break;
 case Z_DEPTHRANGE:zzglDepthRange(F(0),F(1));break;case Z_CLEARD:zzglClearDepth(F(0));break;
 case Z_SHADE:zzglShadeModel(U(0));break;case Z_CULL:zzglCullFace(U(0));break;
 case Z_FRONT:zzglFrontFace(U(0));break;case Z_SCISSOR:zzglScissor(U(0),U(1),U(2),U(3));break;
 /* ---- A3 ---- */
 case Z_ARRAY_LOAD:{
   u32 fmt=U(0),n=U(1),e=zzgl_arrays_load(fmt,n,(const float *)(const void *)(p+8));
   if(e)report[MH_GL_ERRORS]++; report[MH_RESIDENT]=zzgl_resident_count();break;
 }
 case Z_ARRAY_UNLOCK:zzgl_arrays_unlock();report[MH_RESIDENT]=0;if(!c1_flushing)c1_res_n=0;break;
 /* ---- A8.17 ---- */
 case Z_ARRAY_LOAD_RAW:case Z_ARRAY_LOAD_CACHED:{
   u32 fmt,fl,n,e;
   if(op==Z_ARRAY_LOAD_RAW){
     u32 st=U(0),bytes=12u+U(3)*zraw_vertex_bytes(U(1),U(2));
     p+=4;
     if(st!=ZZ_VC_NONE)memcpy(c1_kept(st),p,bytes);   /* bounds checked by zpacket */
   }else{
     u32 off=U(0),bytes=U(1);
     if(!zraw_load_ok(c1_kept(off),bytes)){report[MH_GL_ERRORS]++;if(!report[MH_LAST_GL])report[MH_LAST_GL]=ZGL_INVALID_OPERATION;break;}
     p=c1_kept(off);
   }
   fmt=U(0);fl=U(1);n=U(2);c1_res_n=0;
   e=zzgl_arrays_load(raw_expand(p+12,n,fmt,fl,c1_scratch.f,(fmt&ZZ_VF_UV1)?c1_res:0),n,c1_scratch.f);
   if(fmt&ZZ_VF_UV1)c1_res_n=n;
   if(e)report[MH_GL_ERRORS]++; report[MH_RESIDENT]=zzgl_resident_count();break;
 }
 case Z_DRAW_ARRAYS_RAW:{
   u32 fmt=U(1),fl=U(2),n=U(3);
   zzgl_draw_arrays(U(0),raw_expand(p+16,n,fmt,fl,c1_scratch.f,0),n,c1_scratch.f);break;
 }
 case Z_DRAW_ELEMENTS_MT:{
   const unsigned short *ix=raw_indices(p+32,U(1),U(3)&ZZ_RAW_BE);
   zzgl_draw_elements(U(0),U(1),ix);
   c1_mt_queue(p,ix);break;
 }
 case Z_DRAW_ELEMENTS_RAW:zzgl_draw_elements(U(0),U(1),raw_indices(p+12,U(1),U(2)&ZZ_RAW_BE));break;
 case Z_COLOR:zzglColor4f(F(0),F(1),F(2),F(3));break;
 case Z_INDEX_KEEP:{
   /* A8.19: an index record kept in the cache (bounds checked by zpacket), then executed from there */
   zu8 *k=c1_kept(U(0));
   memcpy(k,p+4,zkept_draw_bytes(p+4));
   return zrender_visit(zget(k),k+4,unused);
 }
 case Z_INDEX_AGAIN:{
   const zu8 *k=c1_kept(U(0));
   if(!zkept_draw_ok(k,U(1))){report[MH_GL_ERRORS]++;if(!report[MH_LAST_GL])report[MH_LAST_GL]=ZGL_INVALID_OPERATION;break;}
   return zrender_visit(zget(k),k+4,unused);
 }
 case Z_MT_FLUSH:c1_mt_flush(U(0)&7u,U(1),U(2),U(3),U(4));break;
 case Z_DRAW_RAW:{
   u32 n=U(1),fl=U(2),be=fl&ZZ_RAW_BE,cst=fl&ZZ_RAW_CONST;const zu8 *q=p+12;
   float cr=0,cg=0,cb=0,ca=0;
   if(cst){cr=rf(q,be);cg=rf(q+4,be);cb=rf(q+8,be);ca=rf(q+12,be);q+=16;}
   zzglBegin(U(0));
   for(i=0;i<n;i++){
     if(cst){zzglColor4f(cr,cg,cb,ca);zzglTexCoord2f(rf(q+12,be),rf(q+16,be));zzglVertex3f(rf(q,be),rf(q+4,be),rf(q+8,be));q+=20;}
     else{zzglColor4f(rf(q+12,be),rf(q+16,be),rf(q+20,be),rf(q+24,be));zzglTexCoord2f(rf(q+28,be),rf(q+32,be));zzglVertex3f(rf(q,be),rf(q+4,be),rf(q+8,be));q+=36;}
   }
   zzglEnd();break;
 }
 case Z_DRAW_ELEMENTS:zzgl_draw_elements(U(0),U(1),(const unsigned short *)(const void *)(p+8));break;
 case Z_DRAW_ARRAYS:zzgl_draw_arrays(U(0),U(1),U(2),(const float *)(const void *)(p+12));break;
 case Z_TEXGEN:{
   if(U(1)==0x2500u||U(1)==0x5A5Au)zzglTexGeni(U(0),U(1),(s32)U(2));
   else{float pl[4];pl[0]=F(2);pl[1]=F(3);pl[2]=F(4);pl[3]=F(5);zzglTexGenfv(U(0),U(1),pl);}
   break;
 }
 case Z_DRAW_N:{
   u32 n=U(1);zzglBegin(U(0));p+=8;
   for(i=0;i<n;i++,p+=48){zzglNormal3f(F(3),F(4),F(5));zzglColor4f(F(6),F(7),F(8),F(9));zzglTexCoord2f(F(10),F(11));zzglVertex3f(F(0),F(1),F(2));}
   zzglEnd();break;
 }
 case Z_LOADMATRIX:zzglLoadMatrixf((const float *)(const void *)p);break;
 case Z_MULTMATRIX:zzglMultMatrixf((const float *)(const void *)p);break;
 case Z_DELETE_TEX:{u32 n=U(0);for(i=0;i<n;i++){u32 name=U(1+i);zzglDeleteTextures(1,&name);}break;}
 default:return ZE_OP;
 }
 /* A3: GL errors are reported to the application through GLGetError (MH_LAST_GL, MH_GL_ERRORS) and do
    not lock the service; GL_OUT_OF_MEMORY (a librixgpu failure behind a ZZGL call) still does. */
 glerr=zzglGetError();
 if(glerr){report[MH_GL_ERRORS]++;if(!report[MH_LAST_GL])report[MH_LAST_GL]=glerr;}
 if(op==Z_IMAGE||op==Z_SUBIMAGE){
   /* A8: a texture the pool cannot hold even at a quarter of its size is refused by ZZGL (GL_OUT_OF_MEMORY,
      nothing written, its refusal counter moves): the application goes on without it and the service does
      not stop.  Any other GL_OUT_OF_MEMORY (a drain or librixgpu failure) stays fatal, as before. */
   u32 up,ds,rf,ev;zzgl_tex_counts(&up,&ds,&rf,&ev);
   report[MH_TEX_STATS]=(rf&0xFFFFu)|((ds&0xFFFFu)<<16);
   if(glerr==ZGL_OUT_OF_MEMORY&&rf!=tex_refused_before)glerr=0;
 }
 return rc?rc:(glerr==ZGL_OUT_OF_MEMORY?(int)glerr:0);
}

