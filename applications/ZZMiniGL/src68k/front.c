/* front.c - ZZMiniGL A8: the 68k side of minigl.library (MiniGL 29.1 ABI 5, 205 slots).
 * Encodes ordinary GL calls into wire packets (shared/wire.h, ZZ_PROTO defined in wire.h) for the Core1 service.
 * A7: MGLGetWindowHandle / MGLGetInputWindowHandle (an input window on the context's screen, opened on
 * first request) and the OFFSCREEN presentation (ZZMiniGLOffscreen=ON: frames fenced, never copied).
 * The generic client-array path used by MiniGL applications such as GLQuake:
 *   GLVertexPointer / GLNormalPointer / GLTexCoordPointer / GLColorPointer + Enable/DisableClientState
 *   GLLockArrays(first, count)   the enabled arrays [first, first+count) are uploaded ONCE and stay
 *                                resident on Core1 until GLUnlockArrays (EXT_compiled_vertex_array)
 *   GLDrawElements               indices only (16-bit) when the range is resident; otherwise the
 *                                referenced vertices are gathered and sent (standard semantics)
 *   GLDrawArrays                 any primitive mode, any enabled attribute set
 *   GLTexGeni / GLTexGenfv       GL_SPHERE_MAP, GL_OBJECT_LINEAR, GL_EYE_LINEAR on Core1
 *   GLNormal3f(v), GLLoadMatrixf, GLMultMatrixf, GLGenTextures, GLDeleteTextures, GLIsTexture
 * Nothing in the packet format is specific to one application. */
#include "abi.h"
#include "os.h"
struct ZZContext {
 Z width,height,mode,count,begun,bad,seq,len,error,bound;
 Z rgba[4],uv[2],nrm[3],verts[ZZ_MAXVERT][14],ticks,begin_at;   /* A8.12: words 12,13 = unit-1 s,t */
 const zu8 *array_pos,*array_uv,*array_nrm,*array_col;
 Z array_pos_stride,array_uv_stride,array_nrm_stride,array_col_stride;
 Z array_pos_on,array_uv_on,array_nrm_on,array_col_on,array_pos_size,array_col_size,array_col_ub;
 Z array_pos_int,array_col_kind,client_unit,array_uv1_on,array_uv1_stride;   /* A8.12 */
 const zu8 *array_uv1;
 Z texgen_on,lock_first,lock_count,lock_fmt,reported_gl;
 zu8 texnames[ZZ_MAX_TEXTURES/8];
};
static struct ZZContext state;
static union {Z align;zu8 byte[ZZ_LIMIT];} packet;
GLcontext zz_current;
/* A8.10: the open context is tracked by the library itself.  zz_current is the variable the dispatch table
   publishes (currentContext) and the application may write it: GLQuake's VID_Shutdown clears it, then
   calls MGLDeleteContext with the saved value; A8.9 refused that call (zz_current already 0), so Core1
   was never stopped and the screen stayed open. */
static int ctx_open;
static Ptr owner;
/* ---- A8: the 68k copy of the matrix stacks (GLGetFloatv; GLQuake reads GL_MODELVIEW_MATRIX every frame).
   Core1 keeps doing all the transformations; this copy only answers queries.  Column-major, as GL. ---- */
static Z imm_points_dropped,imm_split_records;   /* A8 immediate-mode counters */
static Z imm_points_drawn,points_off,c1_point_size;   /* A8.23: GL_POINTS go to Core1 (not with NOPOINTS); the size Core1 has */
static Z img_mips_dropped,img_small_scaled,img_converted;   /* A8 texture-upload counters */
#define ZZ_TEX_SRC_MAX 2048
#define ZZ_BIG_BYTES (256u*256u*4u)
static IdleFn loop_idle;static KeyHandlerFn loop_key;static Z loop_run,loop_entries,loop_idles,loop_keys,loop_events,loop_auto,loop_ticks;   /* A8.28: MGLMainLoop */
static Z unpack_align=4u,pack_align=4u,pixelstore_calls,pixelstore_refused;   /* A8.28: GLPixelStorei */
static Z src_pitch(Z w,Z nb){return (w*nb+unpack_align-1u)&~(unpack_align-1u);}   /* bytes from a row of the application's image to the next */
static zu8 *big_buf;static zu8 tex_red[ZZ_MAX_TEXTURES];   /* A8.27 (image_body): per name, log2 of the reduction, x | y << 4 */
static Z img_reduced,img_reduced_w,img_reduced_h,img_sub_unaligned;
/* A8.5: MiniGL's ARGB4444 lightmaps (MGL_UNSIGNED_SHORT_4_4_4_4, alpha nibble 0xF, colour = light) are
   drawn by GLQuake in a second pass under its normal blend SRC_ALPHA, ONE_MINUS_SRC_ALPHA, which with an
   opaque texel would replace the surface by its lightmap (what A8.4 showed: lighting only).  The texel is
   the light factor, as in the LUMINANCE and 5_6_5 cases where GLQuake asks for ZERO, SRC_COLOR, so this
   blend is sent instead while such a texture is bound: the base texture times the light. */
static zu8 lm_names[ZZ_MAX_TEXTURES/8];
static Z blend_req_s,blend_req_d,blend_sent_s,blend_sent_d,lm_blend_mult;
#define ZZ_GL_FLOAT        0x1406u
#define ZZ_GL_UNSIGNED_BYTE 0x1401u
#define ZZ_GL_UNSIGNED_SHORT 0x1403u
#define ZZ_GL_UNSIGNED_INT 0x1405u
#define MT_BUF_BYTES (128u*1024u)
#define MT_MAX_PRIMS 1536u
#define MT_DRAW 1u      /* immediate batch: Z_DRAW, 36 bytes per vertex (xyz, white, s1 t1) */
#define MT_INDEXED 2u   /* locked range + indices: Z_ARRAY_LOAD (xyz s1 t1) + Z_DRAW_ELEMENTS + Z_ARRAY_UNLOCK */
#define MT_ARRAYS 3u    /* gathered vertices: Z_DRAW_ARRAYS (xyz s1 t1) */
static Z mt_bound1,mt_on1,mt_env1=0x2100u,mt_uv1[2];
/* A8.17: raw records.  The 68k writes its own words (move.l) where it assembled little-endian bytes
   and converted colours and lightmaps; Core1 reorders and expands (srcarm/render.c).  The unit-1 pass
   of locked ranges is queued and replayed by Core1 (Z_DRAW_ELEMENTS_MT, Z_MT_FLUSH).  NORAW: A8.16. */
#if defined(ZZ_RAW_SWAP_TEST)
#define RAWW(w) zswap32(w)
#define RAW_BE ZZ_RAW_BE
#define RAW_SWAPS 1u
#elif defined(__m68k__) || defined(__mc68000__) || defined(mc68000) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define RAWW(w) (w)
#define RAW_BE ZZ_RAW_BE
#define RAW_SWAPS 0u
#else
#define RAWW(w) (w)
#define RAW_BE 0u
#define RAW_SWAPS 0u
#endif
/* the order the words really leave in must be the one the flags announce: otherwise no raw records */
static int raw_order_ok(void){union{Z u;zu8 b[4];}e;e.u=1u;return ((e.b[0]==0u)^(RAW_SWAPS!=0u))==(RAW_BE!=0u);}
#define RPUT(p,w) (*(Z *)(void *)(p)=RAWW((Z)(w)))
static Z c1_pending,lock_uv1,raw_records,mt_core1;
/* A8.17: the vertex cache.  A locked range already sent with exactly these bytes (same record, compared
   with the 68k mirror of Core1's cache) goes out as an 8-byte reference.  The 68k chooses each offset and
   writes the mirror and Core1 the cache with the same bytes, so equal mirror bytes are equal cache bytes. */
#define VC_SLOTS 1024u
static zu8 *vc_mirror;static Z vc_head,vc_hits,vc_saved;
static struct {Z key,len,off;} vc_tab[VC_SLOTS];
/* A8.19: the same cache and mirror for the index records of the locked draws (Z_DRAW_ELEMENTS_RAW,
   Z_DRAW_ELEMENTS_MT): one built with the very bytes Core1 keeps goes out as an 8-byte reference. */
#define IC_SLOTS 512u
static struct {Z key,len,off;} ic_tab[IC_SLOTS];static Z ic_hits,ic_saved;
static Z vc_lap;   /* A8.21: laps of the ring */
/* A8.21: GL_RGL_static_arrays, GL_RGL_dynamic_arrays, GL_RGL_arrays_tag (glHint, as GLQuake R200 uses them).
   - static arrays hint on: a locked range is known by its addresses (and the tag, when one was given):
     sent once, then named by its place on Core1 without being read again.  Without a tag it is kept in
     Core1's static store until the application says its arrays are gone (GL_FASTEST); with a tag it
     lives in the vertex cache's ring, valid as long as the ring has not come round to it.
   - static indices hint on: the same for the index list of a locked draw (its record depends on the
     list's address, the lock and the state words it carries, all in the key).
   - dynamic arrays hint on: the range is new at every draw: sent, never kept nor compared.
   Announced in GL_EXTENSIONS only when the caches exist (not with NORAW, NOVCACHE or NORGL). */
#define RGL_STATIC_ARRAYS 0x52474c01u
#define RGL_STATIC_INDICES 0x52474c02u
#define RGL_DYNAMIC_ARRAYS 0x52474c03u
#define RGL_ARRAYS_TAG 0x52474c04u
#define RGL_K 10u
typedef struct {Z k[RGL_K],off,len,gen,lap;} RglKey;
#define SV_SLOTS 2048u
#define SI_SLOTS 2048u
#define TG_SLOTS 1024u
static RglKey *sv_tab,*si_tab,*tg_tab;
static Z rgl_on,rgl_static,rgl_static_idx,rgl_dynamic,rgl_tag,rgl_gen,vs_top;
static Z rgl_hits,rgl_stores,rgl_tag_hits,rgl_tag_stores,rgl_idx_hits,rgl_idx_stores,rgl_dyn_loads,rgl_full,rgl_forgets;
static Z rgl_hash(const Z *k){Z h=k[0]*0x9E3779B1u,i;for(i=1;i<RGL_K;i++)h=((h<<5)|(h>>27))^k[i];return h^(h>>13);}
static int rgl_eq(const RglKey *e,const Z *k){Z i;for(i=0;i<RGL_K;i++)if(e->k[i]!=k[i])return 0;return 1;}
/* the entry of k in a table that keeps what it holds (a few places tried, none taken from another key):
   *slot is where k may be put when it is not there, 0 when those places are all in use */
static RglKey *rgl_find(RglKey *tab,Z slots,const Z *k,RglKey **slot){
 Z h=rgl_hash(k),i;*slot=0;
 for(i=0;i<8u;i++){
   RglKey *e=tab+((h+i)&(slots-1u));
   if(e->gen!=rgl_gen||!e->len){if(!*slot)*slot=e;continue;}
   if(rgl_eq(e,k))return e;
 }
 return 0;
}
static void rgl_put(RglKey *e,const Z *k,Z off,Z len){Z i;for(i=0;i<RGL_K;i++)e->k[i]=k[i];e->off=off;e->len=len;e->gen=rgl_gen;e->lap=vc_lap;}
/* the ring of the vertex cache: a place for len bytes, and whether a place taken earlier is still whole
   (taken in this lap, or in the one before and not reached again) */
static Z vc_alloc(Z len){Z off=vc_head;if(off+len>ZZ_VC_BYTES){off=0;vc_lap++;}vc_head=(off+len+3u)&~3u;return off;}
static int vc_intact(const RglKey *e){return e->lap==vc_lap||(e->lap+1u==vc_lap&&e->off>=vc_head);}
static void rgl_forget(void){rgl_gen++;vs_top=0;rgl_forgets++;}   /* every key at once: entries of another generation are free */
/* A8.19: the current colour as Core1 has it (arrays without a colour array take it there) */
static Z c1_col[4],c1_col_ok;
/* A8.17: the lightmaps' texels as Core1 holds them (ARGB4444 uploads only): a lightmap update goes out as
   the runs of rows that changed, each cut to its changed columns (GLQuake sends whole 128-texel rows, of
   which a few percent change). */
static zu8 *lm_sh[ZZ_MAX_TEXTURES];static unsigned short lm_shw[ZZ_MAX_TEXTURES],lm_shh[ZZ_MAX_TEXTURES];
static zu8 lm_shb[ZZ_MAX_TEXTURES];   /* A8.21: bytes per texel of the shadow: 2 (ARGB4444) or 1 (GL_LUMINANCE) */
static Z lm_in,lm_out;
static void lm_drop(Z name);
static union {Z align;zu8 b[MT_BUF_BYTES];} mt_buf;   /* A8.13: wire-ready payloads */
static struct {Z name,kind,mode,off,na,nb;} mt_prim[MT_MAX_PRIMS];
static Z mt_nprims,mt_used,mt_flushes,mt_polys,mt_overflow,app_env=0x2100u;
static Z app_blend_on,app_depthmask=1,app_tex_on=1;
static Z app_cull_on,flat_on,flat_mm,flat_po,flat_tg,flat_vp[4],flat_prims;   /* A8.28: MGL_FLATFAN / MGL_FLATSTRIP (impl_GLBegin) */
static int mt_flushing;
static Z array_type_first;   /* A8.12: the first array type refused */
static Z tmu_active;   /* GL_TEXTURE0_ARB = 0x84C0; unit 1 (0x84C1) is emulated, A8.12 */
static void blend_apply(GLcontext c);
/* A8.5: capabilities Core1 does not know (MiniGL's MGL_Z_OFFSET...) end on the 68k, counted, the first one named */
static Z cap_unknown,cap_unknown_first;
static Z bind_oor,bind_oor_first;   /* A9: GLBindTexture names past ZZ_MAX_TEXTURES (A8 stopped at 1280: GLQuake goes higher) */
#define MX_DEPTH 16u
static float mx_mv[MX_DEPTH][16],mx_pj[MX_DEPTH][16],mx_tx[MX_DEPTH][16];
static Z mx_sp[3],mx_which,mv_viewport[4];
static float *mx_cur(void){Z w=mx_which;return w==1u?mx_pj[mx_sp[1]]:w==2u?mx_tx[mx_sp[2]]:mx_mv[mx_sp[0]];}
static void mx_identity(float *m){Z i;for(i=0;i<16;i++)m[i]=(i%5u)==0?1.0f:0.0f;}
static void mx_copy(float *d,const float *s){Z i;for(i=0;i<16;i++)d[i]=s[i];}
static void mx_mul(float *a,const float *b){   /* a = a b */
 float t[16];Z i,j;
 for(j=0;j<4;j++)for(i=0;i<4;i++)t[j*4+i]=a[i]*b[j*4]+a[4+i]*b[j*4+1]+a[8+i]*b[j*4+2]+a[12+i]*b[j*4+3];
 mx_copy(a,t);
}
static float zz_sqrtf(float v){
 float r;if(v<=0.0f)return 0.0f;
#if defined(__mc68000__) || defined(__m68k__)
 __asm__("fsqrt%.x %1,%0":"=f"(r):"f"(v));
#else
 {union{float f;Z b;}u;float x;u.f=v;u.b=(u.b>>1)+0x1FC00000u;x=u.f;x=0.5f*(x+v/x);x=0.5f*(x+v/x);x=0.5f*(x+v/x);r=x;}
#endif
 return r;
}
static void zz_sincos_deg(float deg,float *sn,float *cs){   /* no libm: quadrant reduction, Taylor on [-pi/4, pi/4] */
 double d=deg,x,x2,s1,c1;long q;
 q=(long)(d/90.0+(d>=0.0?0.5:-0.5));
 x=(d-90.0*(double)q)*(3.14159265358979323846/180.0);x2=x*x;
 s1=x*(1.0-x2/6.0*(1.0-x2/20.0*(1.0-x2/42.0*(1.0-x2/72.0*(1.0-x2/110.0)))));
 c1=1.0-x2/2.0*(1.0-x2/12.0*(1.0-x2/30.0*(1.0-x2/56.0*(1.0-x2/90.0*(1.0-x2/132.0)))));
 switch((int)(q&3)){case 0:*sn=(float)s1;*cs=(float)c1;break;case 1:*sn=(float)c1;*cs=(float)-s1;break;
  case 2:*sn=(float)-s1;*cs=(float)-c1;break;default:*sn=(float)-c1;*cs=(float)s1;break;}
}
static void mx_rotate_sc(float *m,float s,float c,float x,float y,float z){
 float C=1.0f-c;
 m[0]=x*x*C+c;  m[1]=y*x*C+z*s;m[2]=x*z*C-y*s;m[3]=0;
 m[4]=x*y*C-z*s;m[5]=y*y*C+c;  m[6]=y*z*C+x*s;m[7]=0;
 m[8]=x*z*C+y*s;m[9]=y*z*C-x*s;m[10]=z*z*C+c;m[11]=0;
 m[12]=m[13]=m[14]=0;m[15]=1;
}
static void mx_rotate(float *m,float deg,float x,float y,float z){
 float s,c,l=x*x+y*y+z*z;
 if(l>0.0f&&(l<0.9999f||l>1.0001f)){float r=1.0f/zz_sqrtf(l);x*=r;y*=r;z*=r;}
 zz_sincos_deg(deg,&s,&c);mx_rotate_sc(m,s,c,x,y,z);
}
static void mx_frustum(float l,float r,float b,float t,float n,float f,int ortho){
 float m[16];Z i;for(i=0;i<16;i++)m[i]=0.0f;
 if(r==l||t==b||f==n)return;
 if(ortho){m[0]=2.0f/(r-l);m[5]=2.0f/(t-b);m[10]=-2.0f/(f-n);m[12]=-(r+l)/(r-l);m[13]=-(t+b)/(t-b);m[14]=-(f+n)/(f-n);m[15]=1.0f;}
 else{m[0]=2.0f*n/(r-l);m[5]=2.0f*n/(t-b);m[8]=(r+l)/(r-l);m[9]=(t+b)/(t-b);m[10]=-(f+n)/(f-n);m[11]=-1.0f;m[14]=-2.0f*f*n/(f-n);}
 mx_mul(mx_cur(),m);
}
static void mx_mode(Z mode){mx_which=mode==0x1701u?1u:mode==0x1702u?2u:0u;}
static void mx_push(void){Z w=mx_which;float (*st)[16]=w==1u?mx_pj:w==2u?mx_tx:mx_mv;if(mx_sp[w]+1u<MX_DEPTH){mx_copy(st[mx_sp[w]+1u],st[mx_sp[w]]);mx_sp[w]++;}}
static void mx_pop(void){Z w=mx_which;if(mx_sp[w])mx_sp[w]--;}
static void mx_reset(void){mx_sp[0]=mx_sp[1]=mx_sp[2]=0;mx_which=0;mx_identity(mx_mv[0]);mx_identity(mx_pj[0]);mx_identity(mx_tx[0]);}
extern Z zz_pinned;
static void err(Z e){if(!state.error)state.error=e;}
/* A8 call trace: every slot of the table counts its calls (build/dispatch.c); an unsupported slot is
   also named in the log the first time it is called, so a crash still leaves the list */
#include "blackbox.h"
extern Z zz_calls[205];extern const char *const zz_slot_names[205];extern Z zz_last_slot;
/* A8.3 heartbeat: for a context authorised by ZZMiniGLLoad (GLQuake), one line every 5 seconds appended to
   PROGDIR:zzminigl_heartbeat.txt, the file closed after each line so that a crash or a reset keeps it:
   68k frames, Core1 status and counters, fault registers, texture refusals, the slot entered last. */
static Z hb_on,hb_last,hb_len,hb_frames,hb_err_seen;
static char hb_buf[640];
static const char hb_name[]="PROGDIR:zzminigl_heartbeat.txt";
static void hb_put(const char *s){while(*s&&hb_len<638u)hb_buf[hb_len++]=*s++;}
static void hb_num(const char *k,Z v,int hex){
 char t[12];static const char h[]="0123456789abcdef";unsigned i=11;Z b=hex?16u:10u;
 t[i]=0;do{t[--i]=h[v%b];v/=b;}while(v);
 hb_put(" ");hb_put(k);hb_put("=");hb_put(t+i);
}
static void hb_line(const char *tag,int create){
 Z fh,now=zz_clock(),hz=zz_eclock_hz();
 hb_len=0;hb_put(tag);
 hb_num("t_ms",hz>=1000u?now/(hz/1000u):0u,0);hb_num("frames_68k",hb_frames,0);
 hb_num("lib_error",zz_last_error,1);hb_num("pinned",zz_pinned,0);
 hb_num("core1_frames",zz_metric(MH_FRAMES),0);hb_num("status",zz_metric(MH_STATUS),0);hb_num("phase",zz_metric(MH_PHASE),0);
 hb_num("arm_error",zz_metric(MH_ERROR),1);hb_num("fault_pc",zz_metric(MH_FAULT_PC),1);hb_num("fault_far",zz_metric(MH_FAULT_FAR),1);hb_num("fault_type",zz_metric(MH_FAULT_TYPE),1);
 hb_num("gl_errors",zz_metric(MH_GL_ERRORS),0);hb_num("last_gl",zz_metric(MH_LAST_GL),1);
 {Z ts=zz_metric(MH_TEX_STATS);hb_num("tex_refused",ts&0xFFFFu,0);hb_num("tex_reduced",ts>>16,0);}
 hb_num("tris",zz_metric(MH_TRIS),0);hb_num("teximage",zz_calls[75],0);hb_num("texsub",zz_calls[77],0);
 hb_num("flags",zz_ctx_flags(),0);hb_num("lm_mult",lm_blend_mult,0);hb_num("bind_oor",bind_oor,0);
 hb_num("mtex",mt_polys,0);hb_num("mtex_overflow",mt_overflow,0);hb_num("array_refused",array_type_first,1);   /* A8.12 */
 hb_put(" last_slot=");hb_put(zz_last_slot<205u?zz_slot_names[zz_last_slot]:"-");hb_put("\n");
 ZZ_BB_PHASE(10u);                                       /* A8.4: heartbeat file I/O */
 fh=dos_open(hb_name,create?1006u:1004u);
 if(!fh){ZZ_BB_PHASE(1u);return;}
 if(!create)dos_seek(fh,0,1);                            /* OFFSET_END */
 dos_write(fh,hb_buf,hb_len);dos_close(fh);
 hb_last=now;ZZ_BB_PHASE(1u);
}
static void bb_put32(unsigned i,Z v){zz_bb[i]=(unsigned short)(v>>16);zz_bb[i+1]=(unsigned short)v;}
/* A8.5: CPU exceptions of the owning task reach the black box (trap.S) */
volatile unsigned short *zz_trap_bb;Ptr zz_trap_old;
static Ptr trap_task,lib_base;
void zz_trap_entry(void);
static void bb_segments(Ptr seg,unsigned at,unsigned count){
 /* a segment list (BPTR): each segment's size is the long word before it, the next BPTR its first long */
 unsigned k;
 for(k=0;k<count&&seg;k++){
   Z *p=(Z *)((Z)seg<<2);
   bb_put32(at+4*k,(Z)(p+1));bb_put32(at+4*k+2,p[-1]>=8u?p[-1]-8u:0u);
   seg=(Ptr)p[0];
 }
}
static void trap_open(void){
#if defined(__mc68000__) || defined(__m68k__)
 Ptr t=os_task(0),cli;
 if(!zz_bb||!t)return;
 cli=*(Ptr *)((zu8 *)t+172);                              /* pr_CLI (a process: GLQuake runs from the Shell) */
 if(*((zu8 *)t+8)==13&&cli)bb_segments(*(Ptr *)((zu8 *)((Z)cli<<2)+60),48,3);   /* cli_Module */
 if(lib_base)bb_segments(*(Ptr *)((zu8 *)lib_base+34),60,1);                     /* seglist kept by init */
 zz_trap_old=*(Ptr *)((zu8 *)t+50);                       /* tc_TrapCode */
 if(!zz_trap_old)zz_trap_old=*(Ptr *)((zu8 *)os_sys()+304);   /* ExecBase TaskTrapCode */
 zz_trap_bb=zz_bb;trap_task=t;
 *(Ptr *)((zu8 *)t+50)=(Ptr)zz_trap_entry;
#endif
}
static void trap_close(void){
#if defined(__mc68000__) || defined(__m68k__)
 if(trap_task&&*(Ptr *)((zu8 *)trap_task+50)==(Ptr)zz_trap_entry)*(Ptr *)((zu8 *)trap_task+50)=zz_trap_old;
#endif
 trap_task=0;zz_trap_bb=0;
}
/* A8.19 (ZZMiniGLLoad PAULAGUARD, an experiment, nothing of this runs otherwise).  GLQuake's Paula driver
   installs its own handler on an audio interrupt vector (exec SetIntVector).  That handler computes with
   the FPU and puts back fp0 only: the FPU status of the code it interrupted (condition codes included) is
   lost.  With the switch, once a frame, an audio vector whose code lies in the application's own segments
   is given to zz_aud_guard (trap.S), which saves the FPU frame and its three control registers, calls the
   application's handler with its own data, and puts everything back; it also counts the handler's entries
   and returns in the black box, with the frame number of the last entry.  The application's handler goes
   back on its vector when the context is deleted, unless the application has already replaced it. */
#if defined(__mc68000__) || defined(__m68k__)
void zz_aud_guard(void);
static struct {Ptr code,data;volatile unsigned short *bbw;} ag_data[4];
static Z ag_int[4][6];                 /* struct Interrupt: node (14 bytes), is_Data at 14, is_Code at 18 */
static Ptr ag_old[4],ag_seen[4];static Z ag_on[4];
static int ag_app_code(Ptr a){
 Ptr t=os_task(0),cli,seg;unsigned k;
 if(!t||*((zu8 *)t+8)!=13)return 0;
 cli=*(Ptr *)((zu8 *)t+172);if(!cli)return 0;                 /* pr_CLI */
 seg=*(Ptr *)((zu8 *)((Z)cli<<2)+60);                         /* cli_Module */
 for(k=0;k<64u&&seg;k++){Z *p=(Z *)((Z)seg<<2);if((Z)a>=(Z)(p+1)&&(Z)a<(Z)(p-1)+p[-1])return 1;seg=(Ptr)p[0];}
 return 0;
}
#endif
static Z ag_installs;
static void ag_poll(void){
#if defined(__mc68000__) || defined(__m68k__)
 Z ch;zu8 *sys;
 if(!(zz_ctx_flags()&ZZ_ARM_PAULAGUARD))return;
 sys=(zu8 *)os_sys();
 for(ch=0;ch<4u;ch++){
   Ptr *iv=(Ptr *)(sys+84u+12u*(7u+ch));                      /* ExecBase IntVects[INTB_AUD0 + ch]: data, code, node */
   zu8 *n=(zu8 *)ag_int[ch];Z i;
   if(iv[2]==(Ptr)n)continue;                                 /* already under the guard */
   ag_on[ch]=0;
   if(!iv[1]||iv[1]==ag_seen[ch])continue;                    /* none, or one already looked at */
   ag_seen[ch]=iv[1];
   if(!ag_app_code(iv[1]))continue;                           /* the system's own: not touched */
   ag_data[ch].code=iv[1];ag_data[ch].data=iv[0];ag_data[ch].bbw=zz_bb?zz_bb+ZZ_BB_AUD:0;
   for(i=0;i<24u;i++)n[i]=0;
   n[8]=2;*(Ptr *)(n+10)=(Ptr)"ZZMiniGL audio guard";         /* NT_INTERRUPT, ln_Name */
   *(Ptr *)(n+14)=(Ptr)&ag_data[ch];*(Ptr *)(n+18)=(Ptr)zz_aud_guard;
   ag_seen[ch]=0;ag_installs++;
   if(zz_bb){zz_bb[ZZ_BB_AUD+5]=(unsigned short)ag_installs;zz_bb[ZZ_BB_AUD]=ZZ_BB_AUD_ON;}
   ag_old[ch]=os_setintvec(7u+ch,(Ptr)n);ag_on[ch]=1;
 }
#endif
}
static void ag_close(void){
#if defined(__mc68000__) || defined(__m68k__)
 Z ch;zu8 *sys=(zu8 *)os_sys();
 for(ch=0;ch<4u;ch++){
   Ptr *iv=(Ptr *)(sys+84u+12u*(7u+ch));
   if(ag_on[ch]&&iv[2]==(Ptr)ag_int[ch])(void)os_setintvec(7u+ch,ag_old[ch]);   /* the application's handler back */
   ag_on[ch]=0;ag_seen[ch]=0;
 }
#endif
}
static void bb_open(void){
 /* A8.4: the black box of this run (blackbox.h); the magic is written last */
 unsigned i;
 zz_bb=(hb_on&&!(zz_ctx_flags()&ZZ_ARM_NOBLACKBOX))?zz_blackbox():0;zz_bb_seq=0;
 if(!zz_bb)return;
 zz_bb[0]=zz_bb[1]=0;
 for(i=2;i<ZZ_BB_WORDS;i++)zz_bb[i]=((i>=ZZ_BB_RING&&i<ZZ_BB_AUD)||i==8u)?0xFFFFu:0u;   /* ring and last-returned: 0xFFFF = never written (slot 255) */
 zz_bb[2]=ZZ_BB_LAYOUT;zz_bb[0]=ZZ_BB_MAGIC0;zz_bb[1]=ZZ_BB_MAGIC1;
 if(!(zz_ctx_flags()&ZZ_ARM_NOTRAP))trap_open();
 zz_bb_push();
}
static void bb_frame(Z now,Z hz){
 Z ts;
 if(!zz_bb)return;
 bb_put32(4,hb_frames);bb_put32(6,hz>=1000u?now/(hz/1000u):0u);
 bb_put32(10,zz_metric(MH_FRAMES));bb_put32(12,zz_metric(MH_GL_ERRORS));
 ts=zz_metric(MH_TEX_STATS);bb_put32(14,ts&0xFFFFu);bb_put32(16,zz_last_error);
 zz_bb_push();
}
static void heartbeat(void){
 Z now,hz;
 if(!hb_on)return;
 hb_frames++;now=zz_clock();hz=zz_eclock_hz();
 bb_frame(now,hz);
 ag_poll();   /* A8.19 */
 if(zz_last_error&&!hb_err_seen){hb_err_seen=1;hb_line("hb_error",0);return;}
 if(hz&&now-hb_last>=5u*hz)hb_line("hb",0);   /* A8.18: every 5 seconds (2 until A8.17): the black box has every frame */
}
static zu8 missing_seen[(205+7)/8];
void zz_missing(Z slot){
 if(!zz_badslot)zz_badslot=slot;err(0x502);
 if(slot>=1u&&slot<=205u&&!(missing_seen[(slot-1u)>>3]&(1u<<((slot-1u)&7u)))){
   missing_seen[(slot-1u)>>3]|=(zu8)(1u<<((slot-1u)&7u));
   zz_log("minigl_unsupported_first_call=");zz_log(zz_slot_names[slot-1u]);zz_log("\n");
 }
}
static void trace_dump(void){
 char key[48];Z i,j,used=0,unsup=0;const char *pre="calls_";
 for(i=0;i<205u;i++){
   const char *nm=zz_slot_names[i];
   if(!zz_calls[i])continue;
   used++;if(missing_seen[i>>3]&(1u<<(i&7u)))unsup+=zz_calls[i];
   for(j=0;pre[j];j++)key[j]=pre[j];
   {Z k=0;while(nm[k]&&j<47u)key[j++]=nm[k++];}
   key[j]=0;zz_num(key,zz_calls[i]);
 }
 zz_num("minigl_slots_used",used);zz_num("minigl_unsupported_calls",unsup);
}
Z zz_libopen(Ptr base){
 Ptr task=os_task(0);lib_base=base;
 if((*(unsigned short *)((zu8 *)Sys+296)&0xc0u)!=0xc0u || zz_pinned || (owner && owner!=task))return 0;
 if(owner!=task){Z i;for(i=0;i<205u;i++)zz_calls[i]=0;for(i=0;i<sizeof missing_seen;i++)missing_seen[i]=0;}   /* A8: a new client, a new trace */
 owner=task;return 1;
}
void zz_libclosed(void){if(ctx_open)zz_pinned=1;else owner=0;}
/* A8.18: the running task read from ExecBase (ThisTask, offset 276) instead of a FindTask(NULL) call:
   valid() runs for every GL call, about 1300 times per GLQuake frame */
#if defined(__m68k__) || defined(__mc68000__) || defined(mc68000)
#define THIS_TASK() (*(Ptr *)(void *)((zu8 *)Sys+276))
#else
#define THIS_TASK() os_task(0)
#endif
static int valid(GLcontext c){if(c!=&state || !ctx_open || owner!=THIS_TASK()){err(0x502);return 0;}return 1;}
static void ub_unit_init(void);
/* ---- A8.14 PROFILE (ZZMiniGLLoad PROFILE): where the 68060's time goes, per steady frame.
   Heavy paths are timed directly, exclusively and nested (EClock, two reads per region): immediate
   encoding (GLEnd, batch emission), array encoding (Lock/Draw/Unlock), texture uploads with their
   conversion, the unit-1 emulation (queue and pass), packet transfer (flush: copy to the board and wait
   for staging) and the presentation (MGLSwitchDisplay: last chunk, ACK wait, P96).  The light calls
   (vertex, attribute, state) are too frequent to time one by one: their cost is calibrated once at
   context creation, through the real dispatch table into a sink, and multiplied by their counts.  The
   game's own time is what remains of the frame.  Frames longer than 400 ms (loading) are left out. */
#define PC_GAME 0u
#define PC_IMM 1u
#define PC_ARR 2u
#define PC_TEX 3u
#define PC_MT 4u
#define PC_XFER 5u
#define PC_SYNC 6u
#define PC_CAL 7u
#define PC_N 8u
#define PCL_VERTEX 0u
#define PCL_ATTR 1u
#define PCL_STATE 2u
#define PCL_TIMED 3u
static Z prof_on,prof_cur,prof_last,prof_sp,prof_stack[12],prof_regions[PC_N];
static Z prof_acc[PC_N],prof_mark[PC_N],prof_tot[PC_N],prof_reg_mark[PC_N],prof_reg_tot[PC_N];
static Z prof_frames,prof_wall,prof_frame_t0,prof_sink,prof_dropped;
static Z prof_cls_mark[4],prof_cls_tot[4],prof_io_mark[3],prof_io_tot[3];
static zu8 prof_class[205];
static Z cal_clock,cal_dispatch,cal_vertex,cal_attr,cal_state;   /* ticks x 1000 per call */
static void prof_start(GLcontext c);static void prof_report(void);
static void P_IN(Z cat){
 Z now;
 if(!prof_on||prof_sp>=12u)return;
 now=zz_clock();prof_acc[prof_cur]+=now-prof_last;prof_stack[prof_sp++]=prof_cur;prof_cur=cat;prof_last=now;prof_regions[cat]++;
}
static void P_OUT(void){
 Z now;
 if(!prof_on||!prof_sp)return;
 now=zz_clock();prof_acc[prof_cur]+=now-prof_last;prof_cur=prof_stack[--prof_sp];prof_last=now;
}
#ifndef ZZ_TRACE_RECORD
#define ZZ_TRACE_RECORD(op,p,n)   /* a test build may observe the records */
#define ZZ_TRACE_RETRACT()        /* ... and be told that the last one was taken back */
#endif
/* A8.23 LATESHOW (ZZMiniGLLoad switch, armed contexts).  Until now MGLSwitchDisplay locked the P96 bitmap,
   sent the present and waited for Core1 to finish the frame and copy it: with the queue of A8.22 the
   68060 arrives there about 5 ms before Core1 (GLQuake R200, A4000).  With the switch MGLSwitchDisplay
   closes the frame (Z_PRESENT_NONE: no lock, no wait) and returns.  The picture is copied to the screen
   later, by Z_SHOW: when a packet of the next frame is about to go out and Core1 has acknowledged the
   closed frame, the bitmap is locked, Z_SHOW goes out alone ahead of that packet, its ACK is awaited
   (the copy only: Core1 was idle) and the lock is released.  The lock is thus held inside one library
   call, never across the application's own time (A8.9).  If no packet finds Core1 ready, the next
   MGLSwitchDisplay, GLFinish or the close shows the frame first, waiting as before. */
static Z show_pending,show_seq,show_busy;          /* a closed frame not shown yet; the packet that closed it */
static Z show_mid,show_forced,show_polls,show_ticks;
static void show_run(void){
 static union{zu8 byte[40];Z align;} sp;
 Z target=0,pitch=0,t;zu8 *b=sp.byte;
 show_pending=0;
 if(zz_last_error)return;
 P_IN(PC_SYNC);
 ZZ_BB_PHASE(80u);
 if(!zz_fast_lock(&target,&pitch)){
   /* the bitmap is not where the probe found it: the closed frame goes out the slow way */
   ZZ_BB_PHASE(84u);
   if(zz_wait())zz_present();
   P_OUT();return;
 }
 ZZ_BB_PHASE(81u);
 state.seq=znext(state.seq);
 zput(b,ZZ_MAGIC);zput(b+4,ZZ_PROTO);zput(b+8,state.seq);zput(b+12,40u);
 zput(b+16,Z_SHOW);zput(b+20,24u);zput(b+24,target);zput(b+28,pitch);zput(b+32,zz_fast_format());zput(b+36,zz_fast_copy_mode());
 ZZ_TRACE_RECORD(Z_SHOW,b+24,16);
 t=zz_clock();
 if(zz_send(b,40u,1)){t=zz_clock()-t;show_ticks+=t;zz_fast_timing(t);}   /* the lock is held until this ACK */
 ZZ_BB_PHASE(82u);
 if(!zz_pinned)zz_fast_unlock();
 ZZ_BB_PHASE(83u);
 P_OUT();
}
static int flush_body(int wait){
 zu8 *b=packet.byte;int ok;
 if(zz_last_error)return 0;
 if(show_pending&&!show_busy){
   show_polls++;
   if(zz_acked(show_seq)){show_busy=1;show_mid++;show_run();show_busy=0;if(zz_last_error)return 0;}
 }
 if(state.len==16)return wait?zz_wait():1;
 state.seq=znext(state.seq);zput(b,ZZ_MAGIC);zput(b+4,ZZ_PROTO);zput(b+8,state.seq);zput(b+12,state.len);
 ZZ_BB_PHASE(wait?71u:70u);                           /* A8.4: packet to Core1 (71: and its ACK awaited) */
 ok=zz_send(b,state.len,wait);state.len=16;
 ZZ_BB_PHASE(72u);
 return ok;
}
static int flush(int wait){int r;P_IN(PC_XFER);r=flush_body(wait);P_OUT();return r;}
/* A8.13: an armed context (GLQuake) sends its frame in chunks of about 32 KiB: Core1 stages a chunk at once
   (up to 64 KiB) and decodes and draws it while the 68k encodes the next, instead of receiving the
   whole frame at MGLSwitchDisplay (A8.12 on the A4000: 13 ms upload then 15 ms ACK wait per frame,
   one after the other).  Older client programs can use a single packet. */
#define ZZ_CHUNK_BYTES 4096u    /* A8.18: Core1 works through the frame while the 68k builds it (32768 until A8.17: most of the frame was left for the end, and the present waited for it) */
#define ZZ_CHUNK_BYTES_A817 32768u
static Z chunk_bytes,nodraw,nodraw_bytes,sw_first,sw_last,sw_n;
/* A8.13 NODRAW (measurement): geometry and texture records are encoded as usual, into the free tail of the
   packet buffer, and never sent: the frame rate is then that of the game and of the library's encoding,
   without transfer and without Core1 (the screen shows the clears only). */
/* A8.15: only the 3D view is dropped (draws under glFrustum, lightmap updates): the 2D under glOrtho
   (console, menus, HUD) and the texture loads go out, so the screen stays readable and the game can be
   quit as usual.  A8.14 dropped everything: the screen showed stale images and the quit menu was invisible. */
static Z proj_ortho=1u;
/* A8.15: frames that draw a 3D view (a glFrustum in the frame) and last under 400 ms: the game's frame
   rate without the loading, console and menu frames (fps_3d_x10 in the log; PROFILE counts only them) */
static Z f3d_seen,f3d_frames,f3d_ticks,f3d_last;
static int dropped(Z op){
 if(op==Z_SUBIMAGE)return 1;
 return !proj_ortho&&(op==Z_DRAW||op==Z_DRAW_N||op==Z_DRAW_ARRAYS||op==Z_DRAW_ELEMENTS||op==Z_ARRAY_LOAD||op==Z_ARRAY_UNLOCK||op==Z_DRAW_PACKED||op==Z_ARRAY_LOAD_RAW||op==Z_DRAW_ARRAYS_RAW||op==Z_DRAW_ELEMENTS_MT||op==Z_DRAW_RAW||op==Z_DRAW_ELEMENTS_RAW||op==Z_ARRAY_LOAD_CACHED||op==Z_INDEX_KEEP||op==Z_INDEX_AGAIN);
}
static int flush(int wait);
static zu8 *record(Z op,Z payload){
 Z bytes=payload+8;zu8 *p;
 if(bytes>ZZ_LIMIT-16||zz_last_error)return 0;
 if(prof_sink||(nodraw&&dropped(op))){
   if(state.len+bytes>ZZ_LIMIT && !flush(0))return 0;
   if(!prof_sink)nodraw_bytes+=bytes;return packet.byte+state.len+8;
 }
 if(chunk_bytes&&state.len>16u&&state.len+bytes>chunk_bytes&&!flush(0))return 0;
 if(state.len+bytes>ZZ_LIMIT && !flush(0))return 0;
 if(op==Z_DRAW||op==Z_DRAW_RAW||op==Z_DRAW_N||op==Z_DRAW_PACKED)c1_col_ok=0;   /* Core1's current colour follows these vertices */
 p=packet.byte+state.len;zput(p,op);zput(p+4,bytes);state.len+=bytes;ZZ_TRACE_RECORD(op,p+8,payload);return p+8;
}
/* A8.19: an array without a colour array is drawn with the current colour (glColor): Core1 is told it
   when it has another one */
static void sync_colour(void){
 zu8 *p;Z i;
 if(c1_col_ok&&c1_col[0]==state.rgba[0]&&c1_col[1]==state.rgba[1]&&c1_col[2]==state.rgba[2]&&c1_col[3]==state.rgba[3])return;
 p=record(Z_COLOR,16);if(!p)return;
 for(i=0;i<4u;i++){zput(p+4u*i,state.rgba[i]);c1_col[i]=state.rgba[i];}
 if(!prof_sink)c1_col_ok=1;
}
/* ---- A8.12: a second texture unit, emulated.  RasterIX has one TMU.  GLQuake (its default
   "fake multitexture") draws each world polygon once with unit 0 = surface texture and unit 1 = lightmap
   (GL_MODULATE), giving unit-1 coordinates with GLMultiTexCoord2fARB.  A8 sent unit-1 binds and enables
   to the single unit: the lightmap replaced the surface (white walls) and the unit-1 glDisable turned
   texturing off for what followed (white weapon).  Now unit-1 state stays on the 68k; a polygon drawn
   with unit 1 enabled and MODULATE goes out once with unit 0, and is queued for a second pass of its
   lightmap: dst = dst * texel (blend ZERO, SRC_COLOR, depth writes off, same depth test, so only the
   visible fragments of the surface are touched).  The queue goes out, grouped by lightmap (one bind per
   lightmap), before any command that changes the transform or the fixed-function state, at the
   presentation and when full.
   A8.13: the pass repeats the record kinds of the unit-0 draw (Z_DRAW batch, or locked range + indices,
   or gathered vertices) with the same positions and triangle order, under GL_REPLACE: Core1 then sets up
   the very same triangles and the pass meets the stored depth exactly.  A8.12 sent every pass as
   Z_DRAW_ARRAYS; triangles clipped at the screen edges came out slightly different and their edges
   kept the unlit texture (bright diagonal lines and dots on the A4000). */
static int raw_ok(void){return !(zz_ctx_flags()&ZZ_ARM_NORAW)&&raw_order_ok();}
static int mt_active(void){return mt_on1&&mt_bound1&&mt_env1==0x2100u&&app_tex_on&&!(zz_ctx_flags()&ZZ_ARM_NOLMPASS);}
static zu8 *record(Z op,Z payload);
static Z mt_pflags(void){return (app_blend_on?1u:0u)|(app_depthmask?2u:0u)|((zz_ctx_flags()&ZZ_ARM_LMNEAREST)?4u:0u);}
static void mt_copy(zu8 *d,const zu8 *src,Z n){Z i;for(i=0;i<n;i++)d[i]=src[i];}
/* A8.28: the unit-1 pass loads its own vertices on Core1 and leaves nothing resident.  When it runs while
   the application still holds its arrays locked (several glDrawElements under one glLockArrays, with a
   matrix or a state changed in between: the pass must go out before the change), the locked range is
   loaded again behind it, by the same code as glLockArrays: with the vertex cache that is 8 bytes.
   GLQuake draws once per lock and never came here. */
static void impl_GLLockArrays_body(GLcontext c,GLuint first,GLsizei count);
static Z mt_relocks;
static void mt_relock(void){
 Z first=state.lock_first,count=state.lock_count;
 if(!count)return;
 mt_relocks++;
 impl_GLLockArrays_body(&state,(GLuint)first,(GLsizei)count);
}
static void mt_flush_body(void){
 Z i,k,done[MT_MAX_PRIMS/32u+1u];zu8 *p;
 if(c1_pending&&!mt_flushing){   /* A8.17: Core1's queue, under the state it was queued in */
   c1_pending=0;mt_flushes++;
   p=record(Z_MT_FLUSH,20);
   if(p){zput(p,mt_pflags());zput(p+4,app_env);zput(p+8,state.bound);zput(p+12,blend_sent_s);zput(p+16,blend_sent_d);}
   mt_relock();
 }
 if(!mt_nprims||mt_flushing)return;
 mt_flushing=1;mt_flushes++;
 for(i=0;i<MT_MAX_PRIMS/32u+1u;i++)done[i]=0;
 if(!app_blend_on){p=record(Z_ENABLE,8);if(p){zput(p,0x0BE2u);zput(p+4,1);}}
 p=record(Z_BLEND,8);if(p){zput(p,0);zput(p+4,0x300u);}
 if(app_depthmask){p=record(Z_DEPTHMASK,4);if(p)zput(p,0);}
 if(app_env!=0x1E01u){p=record(Z_TEXENV,8);if(p){zput(p,0x2200u);zput(p+4,0x1E01u);}}   /* the lightmap texel as it is */
 for(i=0;i<mt_nprims;i++){
   Z name;
   if(done[i>>5]&(1u<<(i&31u)))continue;
   name=mt_prim[i].name;
   p=record(Z_BIND,4);if(p)zput(p,name);
   if(zz_ctx_flags()&ZZ_ARM_LMNEAREST){p=record(Z_TEXPAR,8);if(p){zput(p,0x2800u);zput(p+4,0x2600u);}}
   for(k=i;k<mt_nprims;k++){
     const zu8 *src;Z na,nb;
     if((done[k>>5]&(1u<<(k&31u)))||mt_prim[k].name!=name)continue;
     done[k>>5]|=1u<<(k&31u);src=mt_buf.b+mt_prim[k].off;na=mt_prim[k].na;nb=mt_prim[k].nb;
     /* the same record kinds, vertices and triangle order as the unit-0 draw: Core1 sets up the same
        triangles, so the pass meets the stored depth exactly (no lit edges) */
     if(mt_prim[k].kind==MT_DRAW){
       p=record(Z_DRAW,8+36*na);if(p){zput(p,mt_prim[k].mode);zput(p+4,na);mt_copy(p+8,src,36*na);}
     }else if(mt_prim[k].kind==MT_INDEXED){
       p=record(Z_ARRAY_LOAD,8+20*na);if(p){zput(p,ZZ_VF_POS3|ZZ_VF_UV);zput(p+4,na);mt_copy(p+8,src,20*na);}
       p=record(Z_DRAW_ELEMENTS,8+((nb*2u+3u)&~3u));if(p){zput(p,mt_prim[k].mode);zput(p+4,nb);mt_copy(p+8,src+20*na,(nb*2u+3u)&~3u);}
       record(Z_ARRAY_UNLOCK,0);
     }else{
       Z per=(ZZ_LIMIT-32u)/20u,done_v=0;
       if(per>ZZ_MAX_ARRAY_VERTS)per=ZZ_MAX_ARRAY_VERTS;
       if(mt_prim[k].mode==4u)per-=per%3u;else if(mt_prim[k].mode==7u)per-=per%4u;
       while(done_v<na){
         Z n=na-done_v;if(n>per)n=per;
         p=record(Z_DRAW_ARRAYS,12+20*n);if(p){zput(p,mt_prim[k].mode);zput(p+4,ZZ_VF_POS3|ZZ_VF_UV);zput(p+8,n);mt_copy(p+12,src+20*done_v,20*n);}
         done_v+=n;
       }
     }
   }
 }
 /* the application's state back */
 p=record(Z_BIND,4);if(p)zput(p,state.bound);
 if(app_env!=0x1E01u){p=record(Z_TEXENV,8);if(p){zput(p,0x2200u);zput(p+4,app_env);}}
 p=record(Z_BLEND,8);if(p){zput(p,blend_sent_s==0xFFFFFFFFu?1u:blend_sent_s);zput(p+4,blend_sent_d==0xFFFFFFFFu?0u:blend_sent_d);}
 if(!app_blend_on){p=record(Z_ENABLE,8);if(p){zput(p,0x0BE2u);zput(p+4,0);}}
 if(app_depthmask){p=record(Z_DEPTHMASK,4);if(p)zput(p,1);}
 mt_nprims=mt_used=0;mt_flushing=0;
 mt_relock();
}
static void mt_flush(void){if(!mt_nprims&&!c1_pending)return;P_IN(PC_MT);mt_flush_body();P_OUT();}
/* room for one more primitive of `bytes` payload bytes (flushing the queue when full) */
static zu8 *mt_reserve(Z kind,Z mode,Z bytes,Z na,Z nb){
 zu8 *d;
 if(bytes>MT_BUF_BYTES){mt_overflow++;return 0;}
 if(mt_nprims>=MT_MAX_PRIMS||mt_used+bytes>MT_BUF_BYTES){mt_overflow++;mt_flush();}
 d=mt_buf.b+mt_used;
 mt_prim[mt_nprims].name=mt_bound1;mt_prim[mt_nprims].kind=kind;mt_prim[mt_nprims].mode=mode;
 mt_prim[mt_nprims].off=mt_used;mt_prim[mt_nprims].na=na;mt_prim[mt_nprims].nb=nb;mt_nprims++;
 mt_used+=(bytes+3u)&~3u;mt_polys++;
 return d;
}
static void mt_queue_body(void){
 /* the immediate batch about to go out with unit 0, for the unit-1 pass */
 Z i;zu8 *d;
 if(state.count<3)return;
 d=mt_reserve(MT_DRAW,state.mode,36*state.count,state.count,0);if(!d)return;
 for(i=0;i<state.count;i++,d+=36){
   zput(d,state.verts[i][0]);zput(d+4,state.verts[i][1]);zput(d+8,state.verts[i][2]);
   zput(d+12,0x3F800000u);zput(d+16,0x3F800000u);zput(d+20,0x3F800000u);zput(d+24,0x3F800000u);
   zput(d+28,state.verts[i][12]);zput(d+32,state.verts[i][13]);
 }
}
static void mt_queue(void){P_IN(PC_MT);mt_queue_body();P_OUT();}
/* the position of client vertex i (GL_FLOAT or, A8.12, GL_INT) */
static void array_position(Z i,float *o){
 const zu8 *b=state.array_pos+i*state.array_pos_stride;
 if(state.array_pos_int){const long *v=(const long *)(const void *)b;o[0]=(float)v[0];o[1]=(float)v[1];o[2]=state.array_pos_size>=3?(float)v[2]:0.0f;}
 else{const float *v=(const float *)(const void *)b;o[0]=v[0];o[1]=v[1];o[2]=state.array_pos_size>=3?v[2]:0.0f;}
}
static zu8 *mt_put_vertex(zu8 *d,Z k){
 float pos[3];const float *t=(const float *)(const void *)(state.array_uv1+k*state.array_uv1_stride);
 array_position(k,pos);
 zput(d,zfloat(pos[0]));zput(d+4,zfloat(pos[1]));zput(d+8,zfloat(pos[2]));zput(d+12,zfloat(t[0]));zput(d+16,zfloat(t[1]));
 return d+20;
}
static Z mt_index(const zu8 *ib,Z itype,Z i){
 return itype==ZZ_GL_UNSIGNED_BYTE?ib[i]:itype==ZZ_GL_UNSIGNED_SHORT?((const unsigned short *)(const void *)ib)[i]:((const Z *)(const void *)ib)[i];
}
static int mt_array_on(void){return mt_active()&&state.array_uv1_on&&state.array_uv1&&state.array_pos;}
/* A8.13: a resident draw (locked range, rebased indices): the pass repeats it with the unit-1 coordinates */
static void mt_queue_indexed_body(Z mode,Z n,const zu8 *ib,Z itype){
 Z i,nv=state.lock_count;zu8 *d;
 if(!mt_array_on()||n<3)return;
 d=mt_reserve(MT_INDEXED,mode,20*nv+((n*2u+3u)&~3u),nv,n);if(!d)return;
 for(i=0;i<nv;i++)d=mt_put_vertex(d,state.lock_first+i);
 for(i=0;i<n;i++,d+=2)zput16(d,mt_index(ib,itype,i)-state.lock_first);
 if(n&1u)zput16(d,0);
}
static void mt_queue_indexed(Z mode,Z n,const zu8 *ib,Z itype){P_IN(PC_MT);mt_queue_indexed_body(mode,n,ib,itype);P_OUT();}
/* a gathered draw (Z_DRAW_ARRAYS): the pass gathers the same vertices with the unit-1 coordinates */
static void mt_queue_array_body(Z mode,Z count,const zu8 *ib,Z itype,Z first){
 Z i;zu8 *d;
 if(!mt_array_on()||count<3)return;
 d=mt_reserve(MT_ARRAYS,mode,20*count,count,0);if(!d)return;
 for(i=0;i<count;i++)d=mt_put_vertex(d,ib?mt_index(ib,itype,i):first+i);
}
static void mt_queue_array(Z mode,Z count,const zu8 *ib,Z itype,Z first){P_IN(PC_MT);mt_queue_array_body(mode,count,ib,itype,first);P_OUT();}
static void command(GLcontext c,Z op,Z n,const Z *args){
 zu8 *p;Z i;if(!valid(c)||state.begun){err(0x502);return;}
 /* A8.12: the queued unit-1 pass goes out before anything that changes how it would be drawn */
 if((mt_nprims||c1_pending)&&op!=Z_BIND&&op!=Z_TEXENV&&op!=Z_TEXPAR)mt_flush();
 p=record(op,n*4);if(p)for(i=0;i<n;i++)zput(p+4*i,args[i]);
}
static Z pending_present;   /* A5: a direct presentation published, ACK not yet awaited */
/* A7: MGLEnableSync.  The presentations are paced to 60 per second on the EClock: a frame is published
   no earlier than its slot, slots 1/60 s apart (period split over 60 slots so that 60 slots make exactly
   one second).  It is a rate cap, not a beam synchronisation: the copy into the P96 bitmap is not tied to
   the ZZ9000 scan-out, as it is not without the cap. */
static Z sync_on,sync_next,sync_acc,sync_waits,sync_late,sync_hz;
static void sync_pace(void){
 Z now,period,hz;
 if(!sync_on)return;
 hz=zz_eclock_hz();if(!hz)return;
 period=hz/60u;sync_acc+=hz%60u;if(sync_acc>=60u){sync_acc-=60u;period++;}
 now=zz_clock();
 if(!sync_next)sync_next=now;
 if((int)(sync_next-now)>0){zz_spin_until(sync_next);sync_waits++;}
 else if(now-sync_next>period){sync_next=now;sync_late++;}   /* more than one slot late: restart the cadence */
 sync_next+=period;
}
void impl_MGLEnableSync(GLcontext c,GLboolean enable){
 if(!valid(c)){err(0x502);return;}
 sync_on=enable?1u:0u;sync_next=0;sync_acc=0;if(sync_on)sync_hz=60u;
}
static int size_ok(int w,int h){return (w==640&&h==480)||(w==320&&h==240)||(w==800&&h==600);}   /* A8.26: 800x600 */
static void *ctx_create(int w,int h);
void *impl_MGLCreateContext(int offx,int offy,int w,int h){
 if(owner!=os_task(0)||ctx_open||offx||offy||!size_ok(w,h))return 0;
 return ctx_create(w,h);
}
/* A8.27: a context in a window the application opened itself (SDL 1.2's MiniGL backend).  The picture
   is the inner area of the window, of one of the three context sizes; the window, its screen and its
   pointer stay the application's (platform.c, adopt.h).  A window of another size is refused: the
   application gets no context, as from any MiniGL that cannot serve the window. */
void *impl_MGLCreateContextFromWindow(struct Window *window){
 Z w=0,h=0;
 if(owner!=os_task(0)||ctx_open||!window)return 0;
 if(!zz_adopt_size((Ptr)window,&w,&h)){zz_adopt_refused(0,0);return 0;}
 if(!size_ok((int)w,(int)h)){zz_adopt_refused(w,h);return 0;}   /* the request is withdrawn */
 return ctx_create((int)w,(int)h);   /* the open that follows takes the request */
}
static void *ctx_create(int w,int h){
 Z i;zu8 *d=(zu8 *)&state;
 for(i=0;i<sizeof state;i++)d[i]=0;
 if(!zz_platform_open(w,h)){zz_platform_close();return 0;}
 state.width=w;state.height=h;state.len=16;for(i=0;i<4;i++)state.rgba[i]=zfloat(1);
 state.nrm[0]=zfloat(0);state.nrm[1]=zfloat(0);state.nrm[2]=zfloat(1);
 pending_present=0;sync_on=sync_next=sync_acc=sync_waits=sync_late=sync_hz=0;mx_reset();
 show_pending=show_seq=show_busy=show_mid=show_forced=show_polls=show_ticks=0;
 hb_on=(Z)zz_ctx_armed();hb_frames=hb_err_seen=0;if(hb_on)hb_line("hb_start",1);
 bb_open();
 imm_points_dropped=imm_split_records=0;imm_points_drawn=0;points_off=(zz_ctx_flags()&ZZ_ARM_NOPOINTS)?1u:0u;c1_point_size=zfloat(1.0f);img_mips_dropped=img_small_scaled=img_converted=0;img_reduced=img_reduced_w=img_reduced_h=img_sub_unaligned=0;big_buf=0;{Z k;for(k=0;k<ZZ_MAX_TEXTURES;k++)tex_red[k]=0;}
 {Z i;for(i=0;i<ZZ_MAX_TEXTURES/8;i++)lm_names[i]=0;}
 blend_req_s=1;blend_req_d=0;blend_sent_s=blend_sent_d=0xFFFFFFFFu;
 chunk_bytes=(zz_ctx_armed()&&!(zz_ctx_flags()&ZZ_ARM_ONEPACKET))?((zz_ctx_flags()&ZZ_ARM_CHUNK32)?ZZ_CHUNK_BYTES_A817:ZZ_CHUNK_BYTES):0u;
 nodraw=(zz_ctx_flags()&ZZ_ARM_NODRAW)!=0;nodraw_bytes=0;proj_ortho=1u;f3d_seen=f3d_frames=f3d_ticks=f3d_last=0;sw_first=sw_last=sw_n=0;
 tmu_active=0x84C0u;state.client_unit=0x84C0u;array_type_first=0;mt_bound1=mt_on1=0;mt_env1=0x2100u;mt_uv1[0]=mt_uv1[1]=0;mt_nprims=mt_used=mt_flushes=mt_polys=mt_overflow=0;mt_relocks=0;unpack_align=pack_align=4u;pixelstore_calls=pixelstore_refused=0;loop_idle=0;loop_key=0;loop_run=loop_entries=loop_idles=loop_keys=loop_events=loop_auto=loop_ticks=0;mt_flushing=0;c1_pending=lock_uv1=raw_records=mt_core1=0;app_env=0x2100u;ub_unit_init();
 {Z i;for(i=0;i<VC_SLOTS;i++)vc_tab[i].len=0;for(i=0;i<IC_SLOTS;i++)ic_tab[i].len=0;ic_hits=ic_saved=0;c1_col_ok=0;for(i=0;i<ZZ_MAX_TEXTURES;i++){lm_sh[i]=0;lm_shw[i]=lm_shh[i]=0;lm_shb[i]=0;}}
 vc_head=vc_hits=vc_saved=lm_in=lm_out=0;vc_mirror=(raw_ok()&&!(zz_ctx_flags()&ZZ_ARM_NOVCACHE))?(zu8 *)os_alloc(ZZ_VC_BYTES,4u):0;   /* A8.17, MEMF_FAST; none: no cache */
 /* A8.21: the key tables of the GL_RGL hints (MEMF_FAST | MEMF_CLEAR); without them the hints are not announced */
 vc_lap=0;rgl_static=rgl_static_idx=rgl_dynamic=rgl_tag=0;rgl_gen=1;vs_top=0;
 rgl_hits=rgl_stores=rgl_tag_hits=rgl_tag_stores=rgl_idx_hits=rgl_idx_stores=rgl_dyn_loads=rgl_full=rgl_forgets=0;
 sv_tab=(vc_mirror&&!(zz_ctx_flags()&ZZ_ARM_NORGL))?(RglKey *)os_alloc((SV_SLOTS+SI_SLOTS+TG_SLOTS)*sizeof(RglKey),0x10004u):0;
 si_tab=sv_tab?sv_tab+SV_SLOTS:0;tg_tab=sv_tab?si_tab+SI_SLOTS:0;rgl_on=sv_tab!=0;
 app_blend_on=0;app_depthmask=1;app_tex_on=1;app_cull_on=flat_on=flat_prims=0;mv_viewport[0]=mv_viewport[1]=0;mv_viewport[2]=w;mv_viewport[3]=h;   /* A8.28: GL_VIEWPORT before any glViewport is the whole context */
 lm_blend_mult=cap_unknown=cap_unknown_first=0;bind_oor=bind_oor_first=0;
 zz_current=&state;ctx_open=1;
 prof_start(&state);   /* A8.14 */
 return &state;
}
/* A5: a direct presentation whose ACK has not been awaited yet.  The P96 bitmap stays locked
   (as it always did until the ACK); the application runs ahead and encodes the next frame
   meanwhile.  resolve_present() is called before the next lock, by GLFinish and at close. */
static void resolve_present(void){
 Z t;
 if(show_pending){show_forced++;show_run();}   /* A8.23: a closed frame is shown before anything that waits on the screen */
 if(!pending_present)return;
 pending_present=0;t=zz_clock();
 ZZ_BB_PHASE(21u);
 if(zz_wait())zz_fast_timing(zz_clock()-t);   /* the ACK wait only; the surface dump reads here, lock held */
 ZZ_BB_PHASE(22u);
 if(!zz_pinned)zz_fast_unlock();
 ZZ_BB_PHASE(23u);
}
void impl_MGLDeleteContext(GLcontext c){
 if(!valid(c))return;
 mt_flush();
 resolve_present();
 if(!zz_last_error){state.begun=0;record(Z_STOP,0);flush(1);}
 if(hb_on){hb_line("hb_close",0);hb_on=0;}
 ag_close();   /* A8.19 */
 if(zz_ctx_flags()&ZZ_ARM_PAULAGUARD)zz_num("audguard_installs",ag_installs);
 trap_close();
 if(zz_bb){zz_bb[3]=0;zz_bb[2]=ZZ_BB_LAYOUT|ZZ_BB_CLOSED;zz_bb_push();zz_bb=0;}   /* A8.4: clean end */
 if(zz_profile)zz_num("encode_Begin_End_ticks",state.ticks);
 zz_num("sync_cap_hz",sync_hz);zz_num("sync_waits",sync_waits);zz_num("sync_late_frames",sync_late);   /* A7 MGLEnableSync */
 zz_num("imm_points_drawn",imm_points_drawn);zz_num("imm_points_lines_dropped",imm_points_dropped);zz_num("imm_split_records",imm_split_records);   /* A8 */
 if(zz_ctx_flags()&ZZ_ARM_LATESHOW){zz_num("lateshow_mid_frame",show_mid);zz_num("lateshow_at_frame_end",show_forced);zz_num("lateshow_polls",show_polls);zz_num("lateshow_wait_us_total",zz_ticks_to_us(show_ticks));}
 zz_num("lightmap_blend_multiplied",lm_blend_mult);zz_num("mtex_polys",mt_polys);zz_num("mtex_flushes",mt_flushes);zz_num("mtex_relocks",mt_relocks);zz_num("mainloop_entries",loop_entries);if(loop_entries){zz_num("mainloop_idle_calls",loop_idles);zz_num("mainloop_events",loop_events);zz_num("mainloop_keys",loop_keys);zz_num("mainloop_autoexits",loop_auto);zz_num("mainloop_ms",zz_ticks_to_us(loop_ticks)/1000u);if(loop_ticks)zz_num("mainloop_idle_per_second_x10",(Z)(10000000ull*loop_idles/((unsigned long long)zz_ticks_to_us(loop_ticks)+1u)));}zz_num("flat_primitives",flat_prims);zz_num("pixelstore_calls",pixelstore_calls);zz_num("pixelstore_refused",pixelstore_refused);zz_num("unpack_alignment",unpack_align);zz_num("mtex_overflows",mt_overflow);zz_num("mtex_core1_prims",mt_core1);zz_num("raw_records",raw_records);zz_num("vcache_on",vc_mirror?1u:0u);zz_num("vcache_hits",vc_hits);zz_num("vcache_bytes_saved",vc_saved);zz_num("icache_hits",ic_hits);zz_num("icache_bytes_saved",ic_saved);zz_num("rgl_on",rgl_on);zz_num("rgl_static_stores",rgl_stores);zz_num("rgl_static_hits",rgl_hits);zz_num("rgl_tag_stores",rgl_tag_stores);zz_num("rgl_tag_hits",rgl_tag_hits);zz_num("rgl_index_stores",rgl_idx_stores);zz_num("rgl_index_hits",rgl_idx_hits);zz_num("rgl_dynamic_loads",rgl_dyn_loads);zz_num("rgl_static_bytes",vs_top);zz_num("rgl_no_room",rgl_full);zz_num("rgl_forgets",rgl_forgets);zz_num("lightmap_update_bytes_full",lm_in);zz_num("lightmap_update_bytes_sent",lm_out);zz_num("array_type_refused_first",array_type_first);zz_num("chunk_bytes",chunk_bytes);zz_num("frames_3d",f3d_frames);if(f3d_frames){zz_num("us_per_frame_3d",zz_ticks_to_us(f3d_ticks)/f3d_frames);zz_num("fps_3d_x10",(Z)(10000000ull*f3d_frames/((unsigned long long)zz_ticks_to_us(f3d_ticks)+1u)));}prof_report();if(sw_n>1&&zz_eclock_hz())zz_num("wall_us_per_frame",zz_ticks_to_us(sw_last-sw_first)/(sw_n-1u));if(nodraw)zz_num("nodraw_bytes_dropped",nodraw_bytes);zz_num("bind_name_out_of_range",bind_oor);if(bind_oor)zz_num("bind_name_out_of_range_first",bind_oor_first);zz_num("cap_unknown_ignored",cap_unknown);if(cap_unknown)zz_hex("cap_unknown_first",cap_unknown_first);
 zz_num("tex_mip_levels_dropped",img_mips_dropped);zz_num("tex_small_enlarged",img_small_scaled);zz_num("tex_converted",img_converted);
 zz_num("tex_big_reduced",img_reduced);zz_num("tex_big_largest_width",img_reduced_w);zz_num("tex_big_largest_height",img_reduced_h);zz_num("tex_sub_unaligned_dropped",img_sub_unaligned);   /* A8.27 */
 if(big_buf){os_free(big_buf,ZZ_BIG_BYTES);big_buf=0;}
 trace_dump();
 {Z i;for(i=0;i<ZZ_MAX_TEXTURES;i++)lm_drop(i);}if(vc_mirror){os_free(vc_mirror,ZZ_VC_BYTES);vc_mirror=0;}   /* A8.17 */
 rgl_on=0;if(sv_tab){os_free(sv_tab,(SV_SLOTS+SI_SLOTS+TG_SLOTS)*sizeof(RglKey));sv_tab=si_tab=tg_tab=0;}   /* A8.21 */
 zz_platform_close();zz_current=0;ctx_open=0;
}
static void impl_MGLSwitchDisplay_body(GLcontext c){
 Z target=0,pitch=0,probe0=0,probe1=0,t;
 zu8 *p;int locked;
 if(!valid(c)||state.begun){err(0x502);return;}
 ZZ_BB_PHASE(1u);
 {Z now=zz_clock();if(!sw_n)sw_first=now;sw_last=now;sw_n++;}   /* A8.13: wall time per frame */
 mt_flush();    /* A8.12 */
 heartbeat();   /* A8.3 */
 /* A5: the previous frame completes here, after this frame was encoded (its ACK, timing, unlock) */
 ZZ_BB_PHASE(20u);
 resolve_present();
 if(zz_offscreen()){
   /* OFFSCREEN timing: Core1 closes and fences the frame and flips its two
      colour buffers; nothing is copied, no P96 lock is taken.  Pipelined as A5 unless SYNC. */
   sync_pace();
   if(!zz_last_error && record(Z_PRESENT_NONE,0))flush(zz_pipelined()?0:1);
   return;
 }
 if(zz_ctx_armed()&&(zz_ctx_flags()&ZZ_ARM_LATESHOW)&&!(zz_ctx_flags()&ZZ_ARM_HOLDLOCK)&&zz_pipelined()&&zz_fast_verified()){
   /* A8.23 LATESHOW: the frame is closed, neither locked nor awaited; shown by a later call (show_run).
      The first frame goes the usual way below: it carries the probe of the bitmap. */
   ZZ_BB_PHASE(85u);
   sync_pace();
   if(!zz_last_error&&record(Z_PRESENT_NONE,0)&&flush(0)){show_pending=1;show_seq=state.seq;}
   ZZ_BB_PHASE(0u);
   return;
 }
 ZZ_BB_PHASE(30u);
 sync_pace();
 /* This lock belongs to the 68k screen and stays held until the ARM
    completion ACK of the frame. Never free the bitmap after a timeout. */
 ZZ_BB_PHASE(40u);
 locked=zz_fast_lock(&target,&pitch);
 ZZ_BB_PHASE(41u);
 if(locked && !zz_fast_verified()){
   if(zz_fast_probe(&probe0,&probe1)){
     p=record(Z_P96_PROBE,12);
     if(p){zput(p,target);zput(p+4,probe0);zput(p+8,probe1);}
     if(!p || !flush(1) || !zz_fast_probe_ok())locked=0;
   }else locked=0;
 }
 if(locked && !zz_last_error){
   p=record(Z_PRESENT_DIRECT,16);
   if(p){zput(p,target);zput(p+4,pitch);zput(p+8,zz_fast_format());
         zput(p+12,zz_fast_copy_mode());}
   t=zz_clock();
   ZZ_BB_PHASE(50u);
   if(p && zz_pipelined()){
     /* A5: publish and return; the ACK is awaited by resolve_present() */
     if(flush(0)){
       pending_present=1;
       /* A8.9: an armed context (GLQuake) gets its screen back before MGLSwitchDisplay returns: the P96
          lock of the visible bitmap is released once Core1 has copied the frame.  Held across the
          application's own time it blocked input.device and Intuition (IND_REMHANDLER at quit, the
          menu): the game froze outside the library with Core1 idle.  Packets stay pipelined; only the
          present waits.  ZZMiniGLLoad HOLDLOCK keeps the older lock behaviour. */
       if(zz_ctx_armed()&&!(zz_ctx_flags()&ZZ_ARM_HOLDLOCK))resolve_present();
       ZZ_BB_PHASE(0u);return;
     }
   }else if(p && flush(1)){
     zz_fast_timing(zz_clock()-t);
     zz_fast_unlock();
     ZZ_BB_PHASE(0u);
     return;
   }
 }
 /* The slow path is still valid and retains A1a semantics if the
    bitmap was not on the board, format was not PC RGB565, or the
    read-only mapping probe did not match. */
 ZZ_BB_PHASE(60u);
 if(!zz_pinned)zz_fast_unlock();
 if(!zz_last_error && record(Z_PRESENT,0) && flush(1))zz_present();
 ZZ_BB_PHASE(0u);
}
static void prof_frame(void);
void impl_MGLSwitchDisplay(GLcontext c){
 Z now;
 P_IN(PC_SYNC);impl_MGLSwitchDisplay_body(c);P_OUT();prof_frame();
 now=zz_clock();
 if(f3d_last&&f3d_seen&&now-f3d_last<zz_eclock_hz()*2u/5u){f3d_frames++;f3d_ticks+=now-f3d_last;}
 f3d_last=now;f3d_seen=0;
}
/* A8.14 PROFILE: light-call classes, calibration, per-frame accounting, report */
extern const ZZDispatch zz_dispatch;
static int name_is(const char *n,const char *pre){while(*pre){if(*n++!=*pre++)return 0;}return 1;}
static void prof_classes(void){
 Z i;
 for(i=0;i<205u;i++){
   const char *n=zz_slot_names[i];Z k=PCL_STATE;
   if(name_is(n,"GLVertex")&&!name_is(n,"GLVertexPointer"))k=PCL_VERTEX;
   else if((name_is(n,"GLTexCoord")&&!name_is(n,"GLTexCoordPointer"))||((name_is(n,"GLColor3")||name_is(n,"GLColor4")))||
           (name_is(n,"GLNormal3"))||name_is(n,"GLMultiTexCoord"))k=PCL_ATTR;
   else if(name_is(n,"GLEnd")||name_is(n,"GLDrawElements")||name_is(n,"GLDrawArrays")||name_is(n,"GLLockArrays")||
           name_is(n,"GLUnlockArrays")||name_is(n,"GLTexImage2D")||name_is(n,"GLTexSubImage2D")||name_is(n,"MGLSwitchDisplay"))k=PCL_TIMED;
   prof_class[i]=(zu8)k;
 }
}
static void prof_mark_now(Z now){
 Z i,io[3];
 for(i=0;i<PC_N;i++){prof_mark[i]=prof_acc[i];prof_reg_mark[i]=prof_regions[i];}
 for(i=0;i<4u;i++)prof_cls_mark[i]=0;
 for(i=0;i<205u;i++)prof_cls_mark[prof_class[i]]+=zz_calls[i];
 zz_io_ticks(io);for(i=0;i<3u;i++)prof_io_mark[i]=io[i];
 prof_frame_t0=now;
}
static void prof_frame(void){
 Z now,i,cls[4],io[3];
 if(!prof_on)return;
 now=zz_clock();prof_acc[prof_cur]+=now-prof_last;prof_last=now;
 if(prof_frame_t0&&f3d_seen&&now-prof_frame_t0<zz_eclock_hz()*2u/5u){
   for(i=0;i<4u;i++)cls[i]=0;
   for(i=0;i<205u;i++)cls[prof_class[i]]+=zz_calls[i];
   zz_io_ticks(io);
   for(i=0;i<PC_N;i++){prof_tot[i]+=prof_acc[i]-prof_mark[i];prof_reg_tot[i]+=prof_regions[i]-prof_reg_mark[i];}
   for(i=0;i<4u;i++)prof_cls_tot[i]+=cls[i]-prof_cls_mark[i];
   for(i=0;i<3u;i++)prof_io_tot[i]+=io[i]-prof_io_mark[i];
   prof_wall+=now-prof_frame_t0;prof_frames++;
 }else if(prof_frame_t0&&f3d_seen)prof_dropped++;
 prof_mark_now(now);
}
/* ticks x 1000 per call, through the real dispatch table, records going to a sink */
static void prof_calibrate(GLcontext c){
 static Z saved[205];
 Z i,t0,t1,n=1000u;
 const ZZDispatch *d=&zz_dispatch;
 for(i=0;i<205u;i++)saved[i]=zz_calls[i];
 prof_sink=1;
 t0=zz_clock();for(i=0;i<n;i++)(void)zz_clock();t1=zz_clock();cal_clock=(t1-t0)*1000u/n;
 t0=zz_clock();for(i=0;i<n;i++)d->GLHint(c,0x0C50u,0x1100u);t1=zz_clock();cal_dispatch=(t1-t0)*1000u/n;
 t0=zz_clock();for(i=0;i<n;i++)d->GLTexCoord2f(c,0.25f,0.75f);t1=zz_clock();cal_attr=(t1-t0)*1000u/n;
 t0=zz_clock();for(i=0;i<n;i++)d->GLShadeModel(c,0x1D01u);t1=zz_clock();cal_state=(t1-t0)*1000u/n;
 /* vertices: a triangle list in a sink, the batch emissions timed apart (PC_IMM) */
 for(i=0;i<PC_N;i++)prof_acc[i]=0;
 prof_cur=PC_CAL;prof_last=zz_clock();
 d->GLBegin(c,0x0004u);
 for(i=0;i<3u*n;i++)d->GLVertex4f(c,(float)(i&7u),(float)((i>>3)&7u),0.5f,1.0f);
 d->GLEnd(c);
 prof_acc[prof_cur]+=zz_clock()-prof_last;
 cal_vertex=prof_acc[PC_CAL]*1000u/(3u*n);
 prof_cur=PC_GAME;prof_sp=0;
 for(i=0;i<PC_N;i++){prof_acc[i]=0;prof_regions[i]=0;}
 state.uv[0]=state.uv[1]=0;
 prof_sink=0;
 for(i=0;i<205u;i++)zz_calls[i]=saved[i];
}
static void prof_start(GLcontext c){
 Z i;
 prof_on=0;prof_frames=prof_wall=prof_dropped=prof_frame_t0=0;prof_sp=0;prof_cur=PC_GAME;
 for(i=0;i<PC_N;i++)prof_acc[i]=prof_tot[i]=prof_regions[i]=prof_reg_tot[i]=0;
 for(i=0;i<4u;i++)prof_cls_tot[i]=0;for(i=0;i<3u;i++)prof_io_tot[i]=0;
 if(!(zz_ctx_flags()&ZZ_ARM_PROFILE)||!zz_eclock_hz())return;
 prof_classes();
 prof_on=1;prof_calibrate(c);
 prof_last=zz_clock();prof_mark_now(0);
}
static unsigned long long prof_us64(unsigned long long ticks){Z hz=zz_eclock_hz();return hz?ticks*1000000ull/hz:0;}
static void prof_num(const char *k,unsigned long long v){zz_num(k,(Z)v);}
static void prof_report(void){
 unsigned long long F,cls_all,disp,lite_v,lite_a,lite_s,over,game,imm,arr,tex,mt,xfer,sync,up,stw,ack,g3,g4,g5,wall;
 Z i;
 if(!prof_on)return;
 F=prof_frames;
 prof_num("prof_frames",F);prof_num("prof_frames_left_out",prof_dropped);
 prof_num("prof_cal_ns_clock_read",prof_us64(cal_clock)); /* ticks*1000 -> ns */
 prof_num("prof_cal_ns_call_dispatch",prof_us64(cal_dispatch));
 prof_num("prof_cal_ns_call_vertex",prof_us64(cal_vertex));
 prof_num("prof_cal_ns_call_attribute",prof_us64(cal_attr));
 prof_num("prof_cal_ns_call_state",prof_us64(cal_state));
 if(!F)return;
 cls_all=(unsigned long long)prof_cls_tot[0]+prof_cls_tot[1]+prof_cls_tot[2]+prof_cls_tot[3];
 prof_num("prof_calls_per_frame_vertex",prof_cls_tot[PCL_VERTEX]/F);prof_num("prof_calls_per_frame_attribute",prof_cls_tot[PCL_ATTR]/F);
 prof_num("prof_calls_per_frame_state",prof_cls_tot[PCL_STATE]/F);prof_num("prof_calls_per_frame_timed",prof_cls_tot[PCL_TIMED]/F);
 /* light-call estimates, ticks x 1000 */
 disp=cls_all*cal_dispatch;
 lite_v=prof_cls_tot[PCL_VERTEX]*(unsigned long long)(cal_vertex>cal_dispatch?cal_vertex-cal_dispatch:0);
 lite_a=prof_cls_tot[PCL_ATTR]*(unsigned long long)(cal_attr>cal_dispatch?cal_attr-cal_dispatch:0);
 lite_s=prof_cls_tot[PCL_STATE]*(unsigned long long)(cal_state>cal_dispatch?cal_state-cal_dispatch:0);
 over=0;for(i=1;i<PC_N;i++)over+=(unsigned long long)prof_reg_tot[i]*2u*cal_clock;
 /* timed regions, each corrected by one clock read; the other read of each region is taken off the game */
 #define CORR(k) ((unsigned long long)prof_tot[k]*1000u>(unsigned long long)prof_reg_tot[k]*cal_clock?(unsigned long long)prof_tot[k]*1000u-(unsigned long long)prof_reg_tot[k]*cal_clock:0)
 imm=CORR(PC_IMM);arr=CORR(PC_ARR);tex=CORR(PC_TEX);mt=CORR(PC_MT);xfer=CORR(PC_XFER);sync=CORR(PC_SYNC);
 #undef CORR
 game=(unsigned long long)prof_tot[PC_GAME]*1000u;
 {unsigned long long take=disp+lite_v+lite_a+lite_s+over/2u;game=game>take?game-take:0;}
 up=(unsigned long long)prof_io_tot[0]*1000u;stw=(unsigned long long)prof_io_tot[1]*1000u;ack=(unsigned long long)prof_io_tot[2]*1000u;
 wall=(unsigned long long)prof_wall*1000u;
 #define US(x) (prof_us64((x)/1000u)/F)
 prof_num("prof_us_frame",US(wall));
 prof_num("prof_us_quake_68k",US(game));
 prof_num("prof_us_dispatch",US(disp));
 prof_num("prof_us_encode_vertex_calls",US(lite_v));
 prof_num("prof_us_encode_attribute_calls",US(lite_a));
 prof_num("prof_us_encode_state_calls",US(lite_s));
 prof_num("prof_us_encode_immediate_batches",US(imm));
 prof_num("prof_us_encode_arrays",US(arr));
 prof_num("prof_us_texture_uploads",US(tex));
 prof_num("prof_us_multitexture",US(mt));
 prof_num("prof_us_transfer_total",US(xfer));
 prof_num("prof_us_transfer_upload",US(up));
 prof_num("prof_us_transfer_staging_wait",US(stw));
 prof_num("prof_us_present_total",US(sync));
 prof_num("prof_us_present_ack_wait",US(ack));
 prof_num("prof_us_profiler_overhead",US(over));
 g3=lite_v+lite_a+lite_s+imm+arr;g4=tex+mt;g5=xfer+sync;
 prof_num("prof_group1_quake_us",US(game));
 prof_num("prof_group2_dispatch_us",US(disp));
 prof_num("prof_group3_encode_us",US(g3));
 prof_num("prof_group4_texture_multitexture_us",US(g4));
 prof_num("prof_group5_transfer_sync_us",US(g5));
 if(wall){
   prof_num("prof_permille_group1",game*1000u/wall);prof_num("prof_permille_group2",disp*1000u/wall);
   prof_num("prof_permille_group3",g3*1000u/wall);prof_num("prof_permille_group4",g4*1000u/wall);
   prof_num("prof_permille_group5",g5*1000u/wall);prof_num("prof_permille_overhead",over*1000u/wall);
 }
 #undef US
}

/* ---- immediate mode: Z_DRAW (xyz rgba st) or, when texture generation is on, Z_DRAW_N (with the normal) ----
   A8: any number of vertices between GLBegin and GLEnd.  Core1 (ZZGL) takes at most ZZ_MAXVERT (64) per
   record, so a long primitive is cut where its meaning is kept: GL_TRIANGLES every 63 vertices and
   GL_QUADS every 64 (whole primitives), strips every 64 vertices (an even count, so the next record starts
   on the same winding) carrying the last two vertices over, fans and polygons carrying the pivot and the
   last vertex over.  GL_POINTS and the line modes are accepted and not drawn (counted). */
/* A8.28: MGL_FLATFAN and MGL_FLATSTRIP (MiniGL): a triangle fan or strip whose vertices are given in
   pixels of the context (x to the right, y downwards from the top left corner) and in depth (z from 0,
   nearest, to 1), outside the matrices, the viewport, the culling and the texture coordinate generation.
   Drawn as GL_TRIANGLE_FAN / GL_TRIANGLE_STRIP under a projection that maps those coordinates to the
   screen; the application's matrices, matrix mode, viewport and switches are put back at glEnd.  The
   depth is kept just inside the near and far planes (0.2 % of the range).  MiniGL does not clip these
   primitives; here they are clipped like any other. */
#define ZZ_MGL_FLATFAN 0x7013u
#define ZZ_MGL_FLATSTRIP 0x7014u
static void flat_begin(GLcontext c){
 Z i;
 flat_mm=mx_which;flat_po=proj_ortho;for(i=0;i<4;i++)flat_vp[i]=mv_viewport[i];
 mt_flush();
 impl_GLViewport(c,0,0,(GLsizei)state.width,(GLsizei)state.height);
 impl_GLMatrixMode(c,0x1701);impl_GLPushMatrix(c);impl_GLLoadIdentity(c);
 impl_GLOrtho(c,0.0,(double)state.width,(double)state.height,0.0,1.0/512.0,-1.0-1.0/512.0);
 impl_GLMatrixMode(c,0x1700);impl_GLPushMatrix(c);impl_GLLoadIdentity(c);
 if(app_cull_on){Z a[]={0x0B44u,0u};command(c,Z_ENABLE,2,a);}
 flat_tg=state.texgen_on;
 for(i=0;i<2u;i++)if(flat_tg&(1u<<i)){Z a[2];   /* GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T */
   a[0]=0x0C60u+i;a[1]=0u;command(c,Z_ENABLE,2,a);}
 state.texgen_on=0;flat_on=1;flat_prims++;
}
static void flat_end(GLcontext c){
 Z i;
 flat_on=0;
 mt_flush();
 for(i=0;i<2u;i++)if(flat_tg&(1u<<i)){Z a[2];a[0]=0x0C60u+i;a[1]=1u;command(c,Z_ENABLE,2,a);}
 state.texgen_on=flat_tg;
 if(app_cull_on){Z a[]={0x0B44u,1u};command(c,Z_ENABLE,2,a);}
 impl_GLPopMatrix(c);
 impl_GLMatrixMode(c,0x1701);impl_GLPopMatrix(c);
 impl_GLMatrixMode(c,flat_mm==1u?0x1701u:flat_mm==2u?0x1702u:0x1700u);
 impl_GLViewport(c,(GLint)flat_vp[0],(GLint)flat_vp[1],(GLsizei)flat_vp[2],(GLsizei)flat_vp[3]);
 proj_ortho=flat_po;
}
void impl_GLBegin(GLcontext c,GLenum mode){
 if(!valid(c)||state.begun){err(0x502);return;}
 if(mode==ZZ_MGL_FLATFAN||mode==ZZ_MGL_FLATSTRIP){flat_begin(c);mode=mode==ZZ_MGL_FLATFAN?6u:5u;}
 else if(mode>9){err(0x500);return;}
 state.begun=1;state.bad=state.count=0;state.mode=mode;if(zz_profile)state.begin_at=zz_clock();   /* A8.13: the EClock read (timer.device, CIA) only when profiling */
}
static int mt_active(void);static void mt_queue(void);
static void emit_batch_body(void){
 Z i,j;zu8 *p;
 if(state.bad||state.count<(state.mode?3u:1u))return;   /* A8.23: mode 0, points, from one vertex */
 if(state.mode&&mt_active()&&!state.texgen_on)mt_queue();   /* A8.12: unit-1 pass of this batch, deferred */
 if(state.texgen_on){
   p=record(Z_DRAW_N,8+48*state.count);
   if(p){zput(p,state.mode);zput(p+4,state.count);p+=8;for(i=0;i<state.count;i++)for(j=0;j<12;j++,p+=4)zput(p,state.verts[i][j]);}
 }else{
   if(raw_ok()){
     /* A8.17: own words; one colour for the batch when every vertex has it (20 bytes per vertex, not 36) */
     Z cst=1,k;
     for(i=1;i<state.count&&cst;i++)for(k=6;k<10;k++)if(state.verts[i][k]!=state.verts[0][k]){cst=0;break;}
     p=record(Z_DRAW_RAW,12u+(cst?16u:0u)+(cst?20u:36u)*state.count);
     if(p){
       zput(p,state.mode);zput(p+4,state.count);zput(p+8,RAW_BE|(cst?ZZ_RAW_CONST:0u));p+=12;
       if(cst){for(k=6;k<10;k++,p+=4)RPUT(p,state.verts[0][k]);}
       for(i=0;i<state.count;i++){
         const Z *v=state.verts[i];RPUT(p,v[0]);RPUT(p+4,v[1]);RPUT(p+8,v[2]);
         if(cst){RPUT(p+12,v[10]);RPUT(p+16,v[11]);p+=20;}
         else{RPUT(p+12,v[6]);RPUT(p+16,v[7]);RPUT(p+20,v[8]);RPUT(p+24,v[9]);RPUT(p+28,v[10]);RPUT(p+32,v[11]);p+=36;}
       }
       raw_records++;
     }
     return;
   }
   p=record(Z_DRAW,8+36*state.count);
   if(p){zput(p,state.mode);zput(p+4,state.count);p+=8;for(i=0;i<state.count;i++){zput(p,state.verts[i][0]);zput(p+4,state.verts[i][1]);zput(p+8,state.verts[i][2]);for(j=6;j<12;j++)zput(p+4*(j-3),state.verts[i][j]);p+=36;}}
 }
}
static void emit_batch(void){P_IN(PC_IMM);emit_batch_body();P_OUT();}
static void copy_vertex(Z to,Z from){Z j;for(j=0;j<14;j++)state.verts[to][j]=state.verts[from][j];}
static void batch_full(void){
 Z n=state.count,m=state.mode;
 if(!state.mode)imm_points_drawn+=state.count;
 emit_batch();imm_split_records++;
 if(m==5u||m==8u){copy_vertex(0,n-2);copy_vertex(1,n-1);state.count=2;}   /* strips: n is even, the winding carries on */
 else if(m==6u||m==9u){copy_vertex(1,n-1);state.count=2;}                  /* fan / polygon: pivot stays in 0 */
 else state.count=0;                                                          /* triangles (63) and quads (64): whole primitives */
}
static void vertex(float x,float y,float z){
 Z *v,i;if(!state.begun){err(0x502);return;}
 if(state.mode<4u&&(state.mode||points_off)){state.count++;return;}         /* lines: not drawn (A8.23: points are, unless NOPOINTS) */
 if(state.count>=(state.mode==4u?63u:ZZ_MAXVERT))batch_full();
 v=state.verts[state.count++];v[0]=zfloat(x);v[1]=zfloat(y);v[2]=zfloat(z);
 for(i=0;i<3;i++)v[3+i]=state.nrm[i];
 for(i=0;i<4;i++)v[6+i]=state.rgba[i];
 v[10]=state.uv[0];v[11]=state.uv[1];v[12]=mt_uv1[0];v[13]=mt_uv1[1];
}
static void impl_GLEnd_body(GLcontext c){
 if(!valid(c)||!state.begun){err(0x502);return;}
 state.begun=0;
 if(state.mode<4u&&(state.mode||points_off))imm_points_dropped+=state.count;
 else{if(!state.mode)imm_points_drawn+=state.count;emit_batch();}
 if(flat_on)flat_end(c);
 if(zz_profile)state.ticks+=zz_clock()-state.begin_at;
}
void impl_GLEnd(GLcontext c){P_IN(PC_IMM);impl_GLEnd_body(c);P_OUT();}
/* ---- client arrays ---- */
#define ZZ_GL_VERTEX_ARRAY 0x8074u
#define ZZ_GL_NORMAL_ARRAY 0x8075u
#define ZZ_GL_COLOR_ARRAY  0x8076u
#define ZZ_GL_TEXCOORD_ARRAY 0x8078u
#define ZZ_GL_INT 0x1404u
#define ZZ_MGL_UBYTE_BGRA 0x7018u
#define ZZ_MGL_UBYTE_ARGB 0x7019u
static void array_refused(Z type){if(!array_type_first)array_type_first=type;err(0x500);}
void impl_GLVertexPointer(GLcontext c,GLint size,GLenum type,GLsizei stride,const GLvoid *pointer){
 if(!valid(c)||state.begun){err(0x502);return;}
 if(type!=ZZ_GL_FLOAT&&type!=ZZ_GL_INT){array_refused(type);return;}
 if((size!=2&&size!=3&&size!=4) || stride<0 || stride>4096 || !pointer){err(0x501);return;}
 state.array_pos=(const zu8 *)pointer;state.array_pos_size=(Z)size;state.array_pos_stride=stride?(Z)stride:(Z)size*4u;
 state.array_pos_int=type==ZZ_GL_INT;
}
void impl_GLNormalPointer(GLcontext c,GLenum type,GLsizei stride,const GLvoid *pointer){
 if(!valid(c)||state.begun){err(0x502);return;}
 if(type!=ZZ_GL_FLOAT || stride<0 || stride>4096 || !pointer){err(0x501);return;}
 state.array_nrm=(const zu8 *)pointer;state.array_nrm_stride=stride?(Z)stride:3u*sizeof(float);
}
void impl_GLTexCoordPointer(GLcontext c,GLint size,GLenum type,GLsizei stride,const GLvoid *pointer){
 /* A8.12: sizes 2..4 (s and t are used: one TMU, perspective from the positions); the client unit picks the array */
 if(!valid(c)||state.begun){err(0x502);return;}
 if(type!=ZZ_GL_FLOAT){array_refused(type);return;}
 if(size<1 || size>4 || stride<0 || stride>4096 || !pointer){err(0x501);return;}
 if(state.client_unit==0x84C1u){state.array_uv1=(const zu8 *)pointer;state.array_uv1_stride=stride?(Z)stride:(Z)size*4u;return;}
 state.array_uv=(const zu8 *)pointer;state.array_uv_stride=stride?(Z)stride:(Z)size*4u;
}
void impl_GLColorPointer(GLcontext c,GLint size,GLenum type,GLsizei stride,const GLvoid *pointer){
 /* A8.12: also MiniGL's packed colours, one ULONG per vertex: MGL_UBYTE_ARGB (0xAARRGGBB), MGL_UBYTE_BGRA (0xBBGGRRAA) */
 if(!valid(c)||state.begun){err(0x502);return;}
 if(type!=ZZ_GL_FLOAT&&type!=ZZ_GL_UNSIGNED_BYTE&&type!=ZZ_MGL_UBYTE_ARGB&&type!=ZZ_MGL_UBYTE_BGRA){array_refused(type);return;}
 if((size!=3&&size!=4) || stride<0 || stride>4096 || !pointer){err(0x501);return;}
 state.array_col=(const zu8 *)pointer;state.array_col_size=(Z)size;state.array_col_ub=type==ZZ_GL_UNSIGNED_BYTE;
 state.array_col_kind=type==ZZ_MGL_UBYTE_ARGB?2u:type==ZZ_MGL_UBYTE_BGRA?3u:0u;
 if(state.array_col_kind)state.array_col_size=4;
 state.array_col_stride=stride?(Z)stride:(state.array_col_kind?4u:(Z)size*(state.array_col_ub?1u:sizeof(float)));
}
static void client_state(GLcontext c,GLenum cap,Z on){
 if(!valid(c)||state.begun){err(0x502);return;}
 if(cap==ZZ_GL_VERTEX_ARRAY)state.array_pos_on=on;
 else if(cap==ZZ_GL_TEXCOORD_ARRAY){if(state.client_unit==0x84C1u)state.array_uv1_on=on;else state.array_uv_on=on;}
 else if(cap==ZZ_GL_NORMAL_ARRAY)state.array_nrm_on=on;
 else if(cap==ZZ_GL_COLOR_ARRAY)state.array_col_on=on;
 else err(0x500);
}
void impl_GLEnableClientState(GLcontext c,GLenum cap){client_state(c,cap,1);}
void impl_GLDisableClientState(GLcontext c,GLenum cap){client_state(c,cap,0);}
/* the enabled attribute set, as a wire format mask (0 when the vertex array is missing) */
static Z array_format(void){
 Z fmt=ZZ_VF_POS3;
 if(!state.array_pos_on||!state.array_pos)return 0;
 if(state.array_nrm_on&&state.array_nrm)fmt|=ZZ_VF_NORMAL;
 if(state.array_uv_on&&state.array_uv)fmt|=ZZ_VF_UV;
 if(state.array_col_on&&state.array_col)fmt|=ZZ_VF_COLOR;
 return fmt;
}
/* one vertex i of the client arrays, packed in format order */
static zu8 *pack_vertex(zu8 *out,Z i,Z fmt){
 float v[3];array_position(i,v);
 zput(out,zfloat(v[0]));zput(out+4,zfloat(v[1]));zput(out+8,zfloat(v[2]));out+=12;
 if(fmt&ZZ_VF_NORMAL){const float *n=(const float *)(const void *)(state.array_nrm+i*state.array_nrm_stride);zput(out,zfloat(n[0]));zput(out+4,zfloat(n[1]));zput(out+8,zfloat(n[2]));out+=12;}
 if(fmt&ZZ_VF_UV){const float *t=(const float *)(const void *)(state.array_uv+i*state.array_uv_stride);zput(out,zfloat(t[0]));zput(out+4,zfloat(t[1]));out+=8;}
 if(fmt&ZZ_VF_COLOR){
   const zu8 *cb=state.array_col+i*state.array_col_stride;
   if(state.array_col_kind){
     Z w=*(const Z *)(const void *)cb,r,g,b,a;
     if(state.array_col_kind==2u){a=w>>24;r=(w>>16)&255u;g=(w>>8)&255u;b=w&255u;}else{b=w>>24;g=(w>>16)&255u;r=(w>>8)&255u;a=w&255u;}
     zput(out,zfloat(r/255.f));zput(out+4,zfloat(g/255.f));zput(out+8,zfloat(b/255.f));zput(out+12,zfloat(a/255.f));
   }
   else if(state.array_col_ub){zput(out,zfloat(cb[0]/255.f));zput(out+4,zfloat(cb[1]/255.f));zput(out+8,zfloat(cb[2]/255.f));zput(out+12,zfloat(state.array_col_size==4?cb[3]/255.f:1.f));}
   else{const float *cf=(const float *)(const void *)cb;zput(out,zfloat(cf[0]));zput(out+4,zfloat(cf[1]));zput(out+8,zfloat(cf[2]));zput(out+12,zfloat(state.array_col_size==4?cf[3]:1.f));}
   out+=16;
 }
 return out;
}
/* A8.17: the colour array as Core1 reads it raw */
static Z raw_colkind(void){
 if(state.array_col_kind==2u)return ZZ_RAWCOL_ARGB;
 if(state.array_col_kind==3u)return ZZ_RAWCOL_BGRA;
 if(state.array_col_ub)return state.array_col_size==4?ZZ_RAWCOL_RGBA8:ZZ_RAWCOL_RGB8;
 return ZZ_RAWCOL_FLOAT;
}
/* one vertex i of the client arrays, own words, layout of zraw_vertex_bytes */
static zu8 *pack_raw(zu8 *out,Z i,Z fmt,Z ck){
 const Z *w;
 if(state.array_pos_int){float v[3];array_position(i,v);RPUT(out,zfloat(v[0]));RPUT(out+4,zfloat(v[1]));RPUT(out+8,zfloat(v[2]));}
 else{w=(const Z *)(const void *)(state.array_pos+i*state.array_pos_stride);RPUT(out,w[0]);RPUT(out+4,w[1]);RPUT(out+8,state.array_pos_size>=3?w[2]:0u);}
 out+=12;
 if(fmt&ZZ_VF_NORMAL){w=(const Z *)(const void *)(state.array_nrm+i*state.array_nrm_stride);RPUT(out,w[0]);RPUT(out+4,w[1]);RPUT(out+8,w[2]);out+=12;}
 if(fmt&ZZ_VF_UV){w=(const Z *)(const void *)(state.array_uv+i*state.array_uv_stride);RPUT(out,w[0]);RPUT(out+4,w[1]);out+=8;}
 if(fmt&ZZ_VF_UV1){w=(const Z *)(const void *)(state.array_uv1+i*state.array_uv1_stride);RPUT(out,w[0]);RPUT(out+4,w[1]);out+=8;}
 if(fmt&ZZ_VF_COLOR){
   const zu8 *cb=state.array_col+i*state.array_col_stride;
   if(ck==ZZ_RAWCOL_FLOAT){w=(const Z *)(const void *)cb;RPUT(out,w[0]);RPUT(out+4,w[1]);RPUT(out+8,w[2]);RPUT(out+12,state.array_col_size==4?w[3]:0x3F800000u);out+=16;}
   else if(ck==ZZ_RAWCOL_ARGB||ck==ZZ_RAWCOL_BGRA){RPUT(out,*(const Z *)(const void *)cb);out+=4;}
   else{out[0]=cb[0];out[1]=cb[1];out[2]=cb[2];out[3]=ck==ZZ_RAWCOL_RGB8?(zu8)255:cb[3];out+=4;}
 }
 return out;
}
static int dropped(Z op);
/* ---- A8.18: a locked range as raw words, without a call nor a multiplication per vertex: written to the
   packet (raw_store), compared with the mirror of Core1's vertex cache (raw_same: nothing is written when
   Core1 already holds these very words), or written to both (raw_store2).  One body, three uses. ---- */
#if defined(ZZ_RAW_SWAP_TEST)
#define RAW16(k) ((unsigned short)((((k)&255u)<<8)|(((k)>>8)&255u)))
#else
#define RAW16(k) ((unsigned short)(k))
#endif
#define RAW_ADV(q,s) ((const Z *)(const void *)((const zu8 *)(q)+(s)))
#define RAW_BODY(W,WN) \
 const Z *pp=RAW_ADV(state.array_pos,first*state.array_pos_stride),*pn=0,*pu=0,*pu1=0;const zu8 *pc=0; \
 Z ps=state.array_pos_stride,ns=state.array_nrm_stride,us=state.array_uv_stride,u1s=state.array_uv1_stride,cs=state.array_col_stride; \
 Z z3=state.array_pos_size>=3,c4=state.array_col_size==4,i; \
 if(fmt&ZZ_VF_NORMAL)pn=RAW_ADV(state.array_nrm,first*ns); \
 if(fmt&ZZ_VF_UV)pu=RAW_ADV(state.array_uv,first*us); \
 if(fmt&ZZ_VF_UV1)pu1=RAW_ADV(state.array_uv1,first*u1s); \
 if(fmt&ZZ_VF_COLOR)pc=state.array_col+first*cs; \
 for(i=0;i<count;i++){ \
   W(pp[0]);W(pp[1]);if(z3){W(pp[2]);}else{W(0u);} pp=RAW_ADV(pp,ps); \
   if(pn){W(pn[0]);W(pn[1]);W(pn[2]);pn=RAW_ADV(pn,ns);} \
   if(pu){W(pu[0]);W(pu[1]);pu=RAW_ADV(pu,us);} \
   if(pu1){W(pu1[0]);W(pu1[1]);pu1=RAW_ADV(pu1,u1s);} \
   if(pc){ \
     const Z *w=(const Z *)(const void *)pc; \
     if(ck==ZZ_RAWCOL_ARGB||ck==ZZ_RAWCOL_BGRA){W(w[0]);} \
     else if(ck==ZZ_RAWCOL_RGBA8){WN(w[0]);} \
     else if(ck==ZZ_RAWCOL_RGB8){union{Z u;zu8 b[4];}e;e.u=w[0];e.b[3]=(zu8)255;WN(e.u);} \
     else{W(w[0]);W(w[1]);W(w[2]);if(c4){W(w[3]);}else{W(0x3F800000u);}} \
     pc+=cs; \
   } \
 }
#define RAW_W_STORE(x)  {*o++=RAWW((Z)(x));}
#define RAW_WN_STORE(x) {*o++=(Z)(x);}
#define RAW_W_SAME(x)   {if(*m++!=RAWW((Z)(x)))return 0;}
#define RAW_WN_SAME(x)  {if(*m++!=(Z)(x))return 0;}
#define RAW_W_BOTH(x)   {Z t_=RAWW((Z)(x));*o++=t_;*m++=t_;}
#define RAW_WN_BOTH(x)  {Z t_=(Z)(x);*o++=t_;*m++=t_;}
/* A8.19: the two layouts GLQuake locks, as loops that keep everything in registers (the general body
   above holds five source pointers and their strides: on the 68060 half of them live on the stack):
   world batches (xyz, s t, s1 t1 of unit 1) and alias models (xyz, s t, with or without one colour word). */
#define RAW_FAST(W) \
 const Z *pp=RAW_ADV(state.array_pos,first*state.array_pos_stride),*pu=RAW_ADV(state.array_uv,first*state.array_uv_stride); \
 Z ps=state.array_pos_stride,us=state.array_uv_stride; \
 if(fmt==(ZZ_VF_POS3|ZZ_VF_UV|ZZ_VF_UV1)){ \
   const Z *p1=RAW_ADV(state.array_uv1,first*state.array_uv1_stride);Z u1s=state.array_uv1_stride; \
   for(;count;count--){W(pp[0]);W(pp[1]);W(pp[2]);W(pu[0]);W(pu[1]);W(p1[0]);W(p1[1]);pp=RAW_ADV(pp,ps);pu=RAW_ADV(pu,us);p1=RAW_ADV(p1,u1s);} \
 }else if(fmt==(ZZ_VF_POS3|ZZ_VF_UV)){ \
   for(;count;count--){W(pp[0]);W(pp[1]);W(pp[2]);W(pu[0]);W(pu[1]);pp=RAW_ADV(pp,ps);pu=RAW_ADV(pu,us);} \
 }else{ \
   const Z *pc=RAW_ADV(state.array_col,first*state.array_col_stride);Z cs=state.array_col_stride; \
   for(;count;count--){W(pp[0]);W(pp[1]);W(pp[2]);W(pu[0]);W(pu[1]);W(pc[0]);pp=RAW_ADV(pp,ps);pu=RAW_ADV(pu,us);pc=RAW_ADV(pc,cs);} \
 }
static int raw_fast_layout(Z fmt,Z ck){
 if(state.array_pos_size<3)return 0;
 return fmt==(ZZ_VF_POS3|ZZ_VF_UV|ZZ_VF_UV1) || fmt==(ZZ_VF_POS3|ZZ_VF_UV) || (fmt==(ZZ_VF_POS3|ZZ_VF_UV|ZZ_VF_COLOR)&&(ck==ZZ_RAWCOL_ARGB||ck==ZZ_RAWCOL_BGRA));
}
static void raw_fast_store(Z *o,Z first,Z count,Z fmt){RAW_FAST(RAW_W_STORE)}
static int raw_fast_same(const Z *m,Z first,Z count,Z fmt){RAW_FAST(RAW_W_SAME) return 1;}
static void raw_fast_store2(Z *o,Z *m,Z first,Z count,Z fmt){RAW_FAST(RAW_W_BOTH)}
static void raw_gen_store(Z *o,Z first,Z count,Z fmt,Z ck){RAW_BODY(RAW_W_STORE,RAW_WN_STORE)}
static int raw_gen_same(const Z *m,Z first,Z count,Z fmt,Z ck){RAW_BODY(RAW_W_SAME,RAW_WN_SAME) return 1;}
static void raw_gen_store2(Z *o,Z *m,Z first,Z count,Z fmt,Z ck){RAW_BODY(RAW_W_BOTH,RAW_WN_BOTH)}
static void raw_store(Z *o,Z first,Z count,Z fmt,Z ck){if(raw_fast_layout(fmt,ck))raw_fast_store(o,first,count,fmt);else raw_gen_store(o,first,count,fmt,ck);}
static int raw_same(const Z *m,Z first,Z count,Z fmt,Z ck){return raw_fast_layout(fmt,ck)?raw_fast_same(m,first,count,fmt):raw_gen_same(m,first,count,fmt,ck);}
static void raw_store2(Z *o,Z *m,Z first,Z count,Z fmt,Z ck){if(raw_fast_layout(fmt,ck))raw_fast_store2(o,m,first,count,fmt);else raw_gen_store2(o,m,first,count,fmt,ck);}
/* which slot of the cache table to look in: a few words of the range (the full comparison decides) */
static Z vc_key(Z first,Z count,Z fmt,Z fl){
 Z ps=state.array_pos_stride,h=count*0x9E3779B1u^fmt^(fl<<8);
 const Z *a=RAW_ADV(state.array_pos,first*ps),*b=RAW_ADV(a,(count-1u)*ps),*c=RAW_ADV(a,(count>>1)*ps);
 h=((h<<5)|(h>>27))^a[0];h=((h<<5)|(h>>27))^a[1];h=((h<<5)|(h>>27))^b[0];h=((h<<5)|(h>>27))^b[1];
 h=((h<<5)|(h>>27))^c[0];h=((h<<5)|(h>>27))^c[1];
 if(fmt&ZZ_VF_UV){const Z *u=RAW_ADV(state.array_uv,first*state.array_uv_stride);h=((h<<5)|(h>>27))^u[0];h=((h<<5)|(h>>27))^u[1];}
 return h^(h>>11);
}
static int rec_live(Z op){return !(prof_sink||(nodraw&&dropped(op)));}   /* the record really goes into the packet */
static void lm_drop(Z name){if(name<ZZ_MAX_TEXTURES&&lm_sh[name]){os_free(lm_sh[name],(Z)lm_shw[name]*lm_shh[name]*lm_shb[name]);lm_sh[name]=0;lm_shw[name]=lm_shh[name]=0;lm_shb[name]=0;}}
static void raw16_rows(zu8 *d,const zu8 *s,Z bytes){
 Z j;
#if defined(ZZ_RAW_SWAP_TEST)
 for(j=0;j<bytes;j+=2){d[j]=s[j+1];d[j+1]=s[j];}
#else
 if(!(((Z)s|(Z)d|bytes)&3u)){const Z *ws=(const Z *)(const void *)s;Z *wd=(Z *)(void *)d;for(j=0;j<bytes/4u;j++)wd[j]=ws[j];}
 else for(j=0;j<bytes;j++)d[j]=s[j];
#endif
}
/* an update of name against its shadow (ARGB4444 words, or since A8.21 GL_LUMINANCE bytes: bt bytes per
   texel): the changed runs of rows, cut to their changed columns (one Z_SUBIMAGE each); the shadow takes
   the new texels.  0: no shadow for it, send it whole. */
static int lm_sub(Z name,Z x,Z y,Z w,Z h,const zu8 *src,Z pitch,Z bt){
 zu8 *sh;Z sw,r=0,c,rb,re,c0,c1,i;
 if(name>=ZZ_MAX_TEXTURES||!(sh=lm_sh[name])||lm_shb[name]!=bt||(nodraw&&dropped(Z_SUBIMAGE))||prof_sink)return 0;
 sw=lm_shw[name];if(x+w>sw||y+h>lm_shh[name])return 0;
 lm_in+=24u+w*h*bt;
 while(r<h){
   /* a run: consecutive rows with a change, and the columns that changed in them */
   c0=w;c1=0;rb=r;
   for(;r<h;r++){
     Z lo=w,hi=0;
     if(bt==2u){
       const unsigned short *a=(const unsigned short *)(const void *)(src+r*pitch),*b=(const unsigned short *)(const void *)(sh+((y+r)*sw+x)*2u);
       for(c=0;c<w;c++)if(a[c]!=b[c]){lo=c;break;}
       if(lo==w)break;
       for(c=w;c-->lo;)if(a[c]!=b[c]){hi=c;break;}
     }else{
       const zu8 *a=src+r*pitch,*b=sh+(y+r)*sw+x;
       for(c=0;c<w;c++)if(a[c]!=b[c]){lo=c;break;}
       if(lo==w)break;
       for(c=w;c-->lo;)if(a[c]!=b[c]){hi=c;break;}
     }
     if(lo<c0)c0=lo;if(hi>c1)c1=hi;
   }
   re=r;
   if(re>rb){
     Z cw=c1-c0+1u,n=cw*(re-rb)*bt;zu8 *p=record(Z_SUBIMAGE,24u+((n+3u)&~3u));
     if(!p)return 1;
     zput(p,cw);zput(p+4,re-rb);zput(p+8,bt==2u?(RAW_BE?ZZ_TEX_ARGB4444_BE:ZZ_TEX_ARGB4444_LE):1u);zput(p+12,x+c0);zput(p+16,y+rb);zput(p+20,n);p+=24;
     for(i=rb;i<re;i++){
       const zu8 *s=src+i*pitch+c0*bt;zu8 *dsh=sh+((y+i)*sw+x+c0)*bt;Z k;
       if(bt==2u)raw16_rows(p+(i-rb)*cw*2u,s,cw*2u);
       else{zu8 *d=p+(i-rb)*cw;for(k=0;k<cw;k++)d[k]=s[k];}
       for(k=0;k<cw*bt;k++)dsh[k]=s[k];
     }
     for(i=n;i<((n+3u)&~3u);i++)p[i]=0;
     lm_out+=24u+n;if(bt==2u)raw_records++;
   }
   else r++;   /* an unchanged row */
 }
 return 1;
}
static void send_raw(Z mode,Z fmt,Z count,const zu8 *indices,Z itype,Z first){
 Z ck=raw_colkind(),fl=((fmt&ZZ_VF_COLOR)?ck:0u)|RAW_BE,vb=zraw_vertex_bytes(fmt,fl),per=(ZZ_LIMIT-48u)/vb,done=0;
 if(per>ZZ_RAW_MAX_VERTS)per=ZZ_RAW_MAX_VERTS;
 if(mode==4u)per-=per%3u; else if(mode==7u)per-=per%4u;
 while(done<count){
   Z n=count-done,i;zu8 *p,*out;
   if(n>per)n=per;
   p=record(Z_DRAW_ARRAYS_RAW,16u+n*vb);if(!p)return;
   zput(p,mode);zput(p+4,fmt);zput(p+8,fl);zput(p+12,n);out=p+16;
   for(i=0;i<n;i++){
     Z k=first+done+i;
     if(indices){
       if(itype==ZZ_GL_UNSIGNED_BYTE)k=indices[done+i];
       else if(itype==ZZ_GL_UNSIGNED_SHORT)k=((const unsigned short *)(const void *)indices)[done+i];
       else k=((const Z *)(const void *)indices)[done+i];
     }
     out=pack_raw(out,k,fmt,ck);
   }
   done+=n;raw_records++;
 }
}
/* the referenced vertices travel with the call (Z_DRAW_ARRAYS); several records when needed */
static void send_vertices(Z mode,Z fmt,Z count,const zu8 *indices,Z itype,Z first){
 Z vb=zvertex_bytes(fmt),per=(ZZ_LIMIT-32u)/vb,done=0;
 if(raw_ok()&&(count<=ZZ_RAW_MAX_VERTS||mode==4u||mode==7u)){send_raw(mode,fmt,count,indices,itype,first);return;}
 if(per>ZZ_MAX_ARRAY_VERTS)per=ZZ_MAX_ARRAY_VERTS;
 /* a strip, fan, polygon or quad strip cannot be cut: refuse what one packet cannot carry */
 if(count>per && mode!=4u && mode!=7u){err(0x505);return;}
 if(mode==4u)per-=per%3u; else if(mode==7u)per-=per%4u;
 while(done<count){
   Z n=count-done,i;zu8 *p,*out;
   if(n>per)n=per;
   p=record(Z_DRAW_ARRAYS,12u+n*vb);if(!p)return;
   zput(p,mode);zput(p+4,fmt);zput(p+8,n);out=p+12;
   for(i=0;i<n;i++){
     Z k=first+done+i;
     if(indices){
       if(itype==ZZ_GL_UNSIGNED_BYTE)k=indices[done+i];
       else if(itype==ZZ_GL_UNSIGNED_SHORT)k=((const unsigned short *)(const void *)indices)[done+i];
       else k=((const Z *)(const void *)indices)[done+i];
     }
     out=pack_vertex(out,k,fmt);
   }
   done+=n;
 }
}
static void impl_GLDrawArrays_body(GLcontext c,GLenum mode,GLint first,GLsizei count){
 Z fmt;
 if(!valid(c)||state.begun){err(0x502);return;}
 if(mode>9u || first<0 || count<0){err(0x501);return;}
 if(!count)return;
 fmt=array_format();if(!fmt){err(0x502);return;}
 if(!(fmt&ZZ_VF_COLOR))sync_colour();
 send_vertices(mode,fmt,(Z)count,0,0,(Z)first);
 mt_queue_array(mode,(Z)count,0,0,(Z)first);
}
void impl_GLDrawArrays(GLcontext c,GLenum mode,GLint first,GLsizei count){P_IN(PC_ARR);impl_GLDrawArrays_body(c,mode,first,count);P_OUT();}
static void impl_GLLockArrays_body(GLcontext c,GLuint first,GLsizei count){
 Z fmt,vb,i;zu8 *p,*out;
 if(!valid(c)||state.begun){err(0x502);return;}
 if(count<=0)return;
 state.lock_first=0;state.lock_count=0;lock_uv1=0;
 fmt=array_format();if(!fmt)return;        /* a hint: nothing resident, GLDrawElements gathers */
 if(raw_ok()){
   /* A8.17: own words, with the unit-1 coordinates when the lightmap pass will want them */
   Z ck=raw_colkind(),rf=fmt|(mt_array_on()?ZZ_VF_UV1:0u),fl=((fmt&ZZ_VF_COLOR)?ck:0u)|RAW_BE;
   vb=zraw_vertex_bytes(rf,fl);
   Z len,hdr[3];
   if((Z)count>ZZ_RAW_MAX_VERTS || 16u+(Z)count*vb>ZZ_LIMIT-32u)return;
   len=12u+(Z)count*vb;
   zput((zu8 *)hdr,rf);zput((zu8 *)hdr+4,fl);zput((zu8 *)hdr+8,(Z)count);   /* the record from fmt on, as Core1 keeps it */
   int cached=vc_mirror&&!state.array_pos_int&&rec_live(Z_ARRAY_LOAD_RAW);
   if(cached&&rgl_on&&rgl_dynamic){cached=0;rgl_dyn_loads++;}   /* A8.21: new at every draw: sent, not kept */
   else if(cached&&rgl_on&&rgl_static&&!(rf&ZZ_VF_NORMAL)&&(rf&(ZZ_VF_UV1|ZZ_VF_COLOR))!=(ZZ_VF_UV1|ZZ_VF_COLOR)){
     /* A8.21: declared unchanging: known by its addresses, not read again once Core1 has it */
     Z k[RGL_K],off;Z *o;RglKey *e,*slot;
     k[0]=(Z)(unsigned long)state.array_pos;
     k[1]=(rf&ZZ_VF_UV)?(Z)(unsigned long)state.array_uv:0u;
     k[2]=(rf&ZZ_VF_UV1)?(Z)(unsigned long)state.array_uv1:(rf&ZZ_VF_COLOR)?(Z)(unsigned long)state.array_col:0u;
     k[3]=(Z)first;k[4]=(Z)count;
     k[5]=rf|(state.array_pos_size<<5)|(fl<<8)|(((rf&ZZ_VF_UV1)?state.array_uv1_stride:(rf&ZZ_VF_COLOR)?state.array_col_stride:0u)<<18)|(((rf&ZZ_VF_COLOR)&&state.array_col_size==4)?0x80000000u:0u);
     k[6]=state.array_pos_stride|(((rf&ZZ_VF_UV)?state.array_uv_stride:0u)<<16);k[7]=rgl_tag;k[8]=k[9]=0;   /* only what this format reads */
     if(rgl_tag){
       e=rgl_find(tg_tab,TG_SLOTS,k,&slot);
       if(e&&vc_intact(e)){
         p=record(Z_ARRAY_LOAD_CACHED,8);if(!p)return;
         zput(p,e->off);zput(p+4,len);rgl_tag_hits++;
       }else{
         Z *m;
         if(!e)e=slot?slot:tg_tab+(rgl_hash(k)&(TG_SLOTS-1u));   /* no free place: the first one is given up */
         p=record(Z_ARRAY_LOAD_RAW,4u+len);if(!p)return;
         off=vc_alloc(len);
         zput(p,off);o=(Z *)(void *)(p+4);m=(Z *)(void *)(vc_mirror+off);
         o[0]=m[0]=hdr[0];o[1]=m[1]=hdr[1];o[2]=m[2]=hdr[2];
         raw_store2(o+3,m+3,first,(Z)count,rf,ck);
         rgl_put(e,k,off,len);rgl_tag_stores++;
       }
       state.lock_first=first;state.lock_count=(Z)count;state.lock_fmt=fmt;lock_uv1=(rf&ZZ_VF_UV1)?1u:0u;raw_records++;
       return;
     }
     e=rgl_find(sv_tab,SV_SLOTS,k,&slot);
     if(e||(slot&&vs_top+len<=ZZ_VS_BYTES)){
       if(e){
         p=record(Z_ARRAY_LOAD_CACHED,8);if(!p)return;
         zput(p,ZZ_VC_BYTES+e->off);zput(p+4,len);rgl_hits++;
       }else{
         p=record(Z_ARRAY_LOAD_RAW,4u+len);if(!p)return;
         zput(p,ZZ_VC_BYTES+vs_top);o=(Z *)(void *)(p+4);
         o[0]=hdr[0];o[1]=hdr[1];o[2]=hdr[2];
         raw_store(o+3,first,(Z)count,rf,ck);
         rgl_put(slot,k,vs_top,len);vs_top+=len;rgl_stores++;
       }
       state.lock_first=first;state.lock_count=(Z)count;state.lock_fmt=fmt;lock_uv1=(rf&ZZ_VF_UV1)?1u:0u;raw_records++;
       return;
     }
     rgl_full++;   /* no room: compared like any other range */
   }
   if(cached){
     /* A8.18: Core1's vertex cache.  The range is first compared, word for word, with what the mirror
        holds for it: equal, nothing is packed and 8 bytes leave; otherwise it is written to the packet
        and to the mirror together, at the offset the record tells Core1. */
     Z key=vc_key(first,(Z)count,rf,fl),s=key&(VC_SLOTS-1u),off;Z *o,*m;
     if(vc_tab[s].len==len&&vc_tab[s].key==key){
       const Z *mm=(const Z *)(const void *)(vc_mirror+vc_tab[s].off);
       if(mm[0]==hdr[0]&&mm[1]==hdr[1]&&mm[2]==hdr[2]&&raw_same(mm+3,first,(Z)count,rf,ck)){
         p=record(Z_ARRAY_LOAD_CACHED,8);if(!p)return;
         zput(p,vc_tab[s].off);zput(p+4,len);vc_hits++;vc_saved+=len+4u-8u;
         state.lock_first=first;state.lock_count=(Z)count;state.lock_fmt=fmt;lock_uv1=(rf&ZZ_VF_UV1)?1u:0u;raw_records++;
         return;
       }
     }
     p=record(Z_ARRAY_LOAD_RAW,4u+len);if(!p)return;
     off=vc_alloc(len);
     zput(p,off);o=(Z *)(void *)(p+4);m=(Z *)(void *)(vc_mirror+off);
     o[0]=m[0]=hdr[0];o[1]=m[1]=hdr[1];o[2]=m[2]=hdr[2];
     raw_store2(o+3,m+3,first,(Z)count,rf,ck);
     vc_tab[s].key=key;vc_tab[s].len=len;vc_tab[s].off=off;
   }else{
     p=record(Z_ARRAY_LOAD_RAW,4u+len);if(!p)return;
     zput(p,ZZ_VC_NONE);zput(p+4,rf);zput(p+8,fl);zput(p+12,(Z)count);
     if(state.array_pos_int){out=p+16;for(i=0;i<(Z)count;i++)out=pack_raw(out,first+i,rf,ck);}
     else raw_store((Z *)(void *)(p+16),first,(Z)count,rf,ck);
   }
   state.lock_first=first;state.lock_count=(Z)count;state.lock_fmt=fmt;lock_uv1=(rf&ZZ_VF_UV1)?1u:0u;raw_records++;
   return;
 }
 vb=zvertex_bytes(fmt);
 if((Z)count>ZZ_MAX_RESIDENT_VERTS || (Z)count*vb>ZZ_LIMIT-32u)return;   /* too large to keep: ordinary path */
 p=record(Z_ARRAY_LOAD,8u+(Z)count*vb);if(!p)return;
 zput(p,fmt);zput(p+4,(Z)count);out=p+8;
 for(i=0;i<(Z)count;i++)out=pack_vertex(out,first+i,fmt);
 state.lock_first=first;state.lock_count=(Z)count;state.lock_fmt=fmt;
}
void impl_GLLockArrays(GLcontext c,GLuint first,GLsizei count){P_IN(PC_ARR);impl_GLLockArrays_body(c,first,count);P_OUT();}
static void impl_GLUnlockArrays_body(GLcontext c){
 if(!valid(c)||state.begun){err(0x502);return;}
 if(state.lock_count){record(Z_ARRAY_UNLOCK,0);state.lock_count=0;}
}
void impl_GLUnlockArrays(GLcontext c){P_IN(PC_ARR);impl_GLUnlockArrays_body(c);P_OUT();}
static void impl_GLDrawElements_body(GLcontext c,GLenum mode,GLsizei count,GLenum type,const GLvoid *indices){
 Z i,n;zu8 *p;const zu8 *ib=(const zu8 *)indices;
 if(!valid(c)||state.begun){err(0x502);return;}
 if(mode>9u || count<0 || !indices || (type!=ZZ_GL_UNSIGNED_BYTE&&type!=ZZ_GL_UNSIGNED_SHORT&&type!=ZZ_GL_UNSIGNED_INT)){err(0x501);return;}
 if(!count)return;
 n=(Z)count;
 if(!(array_format()&ZZ_VF_COLOR))sync_colour();
 if(state.lock_count && raw_ok() && n<=(ZZ_LIMIT-64u)/2u && array_format()==state.lock_fmt){
   /* A8.18: resident range, one pass: the indices rebased to the lock leave as the 68k's own words
      (Core1 reorders them); one outside the lock and the record is taken back for the ordinary path */
   int mt=n>=3u&&lock_uv1&&mt_array_on()&&mt_bound1<ZZ_MAX_TEXTURES&&state.bound<ZZ_MAX_TEXTURES;
   Z op=mt?Z_DRAW_ELEMENTS_MT:Z_DRAW_ELEMENTS_RAW,hb=mt?32u:12u,rec=hb+((n*2u+3u)&~3u),lf=state.lock_first,lc=state.lock_count,bad=0;
   int live=rec_live(op),keep=live&&vc_mirror&&rec>=32u;unsigned short *d;zu8 *kp=0;
   Z sk[RGL_K];RglKey *sslot=0;int sidx=live&&rgl_on&&rgl_static_idx;
   if(sidx){
     /* A8.21: an index list declared unchanging: its record is known by the list's address, the lock and
        the state words it carries; once Core1 has it, it is named, not read */
     RglKey *e;
     sk[0]=(Z)(unsigned long)ib;sk[1]=n;sk[2]=mode|((Z)type<<4)|(mt?0x100000u:0u);sk[3]=lf;sk[4]=lc;
     if(mt){sk[5]=mt_bound1|(state.bound<<16);sk[6]=mt_pflags();sk[7]=app_env;sk[8]=blend_sent_s;sk[9]=blend_sent_d;}
     else sk[5]=sk[6]=sk[7]=sk[8]=sk[9]=0;
     e=rgl_find(si_tab,SI_SLOTS,sk,&sslot);
     if(e){
       p=record(Z_INDEX_AGAIN,8);if(!p)return;
       zput(p,ZZ_VC_BYTES+e->off);zput(p+4,e->len);
       if(mt){c1_pending++;mt_polys++;mt_core1++;}else mt_queue_indexed(mode,n,ib,type);
       raw_records++;rgl_idx_hits++;
       return;
     }
     if(sslot&&vs_top+4u+rec<=ZZ_VS_BYTES)keep=1;else{sslot=0;rgl_full++;}
   }
   if(keep){kp=record(Z_INDEX_KEEP,8u+rec);if(!kp)return;p=kp+8;}   /* offset, op, then the record as below */
   else{p=record(op,rec);if(!p)return;}
   d=(unsigned short *)(void *)(p+hb);
   if(type==ZZ_GL_UNSIGNED_SHORT){const unsigned short *s=(const unsigned short *)(const void *)ib;for(i=0;i<n;i++){Z k=(Z)s[i]-lf;if(k>=lc)bad=1;d[i]=RAW16(k);}}
   else if(type==ZZ_GL_UNSIGNED_INT){const Z *s=(const Z *)(const void *)ib;for(i=0;i<n;i++){Z k=s[i]-lf;if(k>=lc)bad=1;d[i]=RAW16(k);}}
   else{for(i=0;i<n;i++){Z k=(Z)ib[i]-lf;if(k>=lc)bad=1;d[i]=RAW16(k);}}
   if(n&1u)d[n]=0;
   if(!bad){
     if(mt){
       zput(p,mode);zput(p+4,n);zput(p+8,mt_bound1);zput(p+12,mt_pflags()|RAW_BE);zput(p+16,app_env);zput(p+20,state.bound);zput(p+24,blend_sent_s);zput(p+28,blend_sent_d);
       c1_pending++;mt_polys++;mt_core1++;
     }else{
       zput(p,mode);zput(p+4,n);zput(p+8,RAW_BE);
       mt_queue_indexed(mode,n,ib,type);
     }
     raw_records++;
     if(sslot){
       zput(kp+4,op);zput(kp,ZZ_VC_BYTES+vs_top);
       rgl_put(sslot,sk,vs_top,4u+rec);vs_top+=4u+rec;rgl_idx_stores++;
       return;
     }
     if(keep){
       /* the record from its op word on, compared with what the mirror holds for it: equal, Core1 has
          these bytes at that offset and 8 bytes leave; otherwise kept there and in the mirror */
       Z len=4u+rec,key,s,off,w=len>>2;const Z *a=(const Z *)(const void *)(kp+4);Z *m;
       zput(kp+4,op);
       key=(Z)(unsigned long)ib*0x9E3779B1u^(n<<16)^op^a[1]^(a[w>>1]<<7)^(a[w-1]<<13);key^=key>>11;s=key&(IC_SLOTS-1u);
       if(ic_tab[s].len==len&&ic_tab[s].key==key){
         const Z *mm=(const Z *)(const void *)(vc_mirror+ic_tab[s].off);
         for(i=0;i<w&&mm[i]==a[i];i++){}
         if(i==w){
           state.len-=16u+rec;ZZ_TRACE_RETRACT();p=record(Z_INDEX_AGAIN,8);if(!p)return;
           zput(p,ic_tab[s].off);zput(p+4,len);ic_hits++;ic_saved+=8u+rec;
           return;
         }
       }
       off=vc_alloc(len);
       zput(kp,off);m=(Z *)(void *)(vc_mirror+off);
       for(i=0;i<w;i++)m[i]=a[i];
       ic_tab[s].key=key;ic_tab[s].len=len;ic_tab[s].off=off;
     }
     return;
   }
   if(live){state.len-=8u+rec+(keep?8u:0u);ZZ_TRACE_RETRACT();}   /* nothing was added after it */
 }
 else if(state.lock_count && array_format()==state.lock_fmt && n<=(ZZ_LIMIT-32u)/2u){
   /* resident range: indices only, rebased to the lock */
   int inside=1;
   for(i=0;i<n&&inside;i++){
     Z k=type==ZZ_GL_UNSIGNED_BYTE?ib[i]:type==ZZ_GL_UNSIGNED_SHORT?((const unsigned short *)(const void *)ib)[i]:((const Z *)(const void *)ib)[i];
     if(k<state.lock_first||k-state.lock_first>=state.lock_count)inside=0;
   }
   if(inside&&n>=3u&&lock_uv1&&mt_array_on()&&n<=(ZZ_LIMIT-64u)/2u&&mt_bound1<ZZ_MAX_TEXTURES&&state.bound<ZZ_MAX_TEXTURES){
     /* A8.17: drawn, then queued by Core1 for the unit-1 pass (its xyz s1 t1 are resident there) */
     p=record(Z_DRAW_ELEMENTS_MT,32u+((n*2u+3u)&~3u));if(!p)return;
     zput(p,mode);zput(p+4,n);zput(p+8,mt_bound1);zput(p+12,mt_pflags());zput(p+16,app_env);zput(p+20,state.bound);zput(p+24,blend_sent_s);zput(p+28,blend_sent_d);p+=32;
     for(i=0;i<n;i++,p+=2){
       Z k=type==ZZ_GL_UNSIGNED_BYTE?ib[i]:type==ZZ_GL_UNSIGNED_SHORT?((const unsigned short *)(const void *)ib)[i]:((const Z *)(const void *)ib)[i];
       zput16(p,k-state.lock_first);
     }
     if(n&1u)zput16(p,0);
     c1_pending++;mt_polys++;mt_core1++;
     return;
   }
   if(inside){
     p=record(Z_DRAW_ELEMENTS,8u+((n*2u+3u)&~3u));if(!p)return;
     zput(p,mode);zput(p+4,n);p+=8;
     for(i=0;i<n;i++,p+=2){
       Z k=type==ZZ_GL_UNSIGNED_BYTE?ib[i]:type==ZZ_GL_UNSIGNED_SHORT?((const unsigned short *)(const void *)ib)[i]:((const Z *)(const void *)ib)[i];
       zput16(p,k-state.lock_first);
     }
     if(n&1u)zput16(p,0);
     mt_queue_indexed(mode,n,ib,type);
     return;
   }
 }
 {Z fmt=array_format();if(!fmt){err(0x502);return;}send_vertices(mode,fmt,n,ib,type,0);}
 mt_queue_array(mode,n,ib,type,0);
}
void impl_GLDrawElements(GLcontext c,GLenum mode,GLsizei count,GLenum type,const GLvoid *indices){P_IN(PC_ARR);impl_GLDrawElements_body(c,mode,count,type,indices);P_OUT();}
void impl_GLVertex4f(GLcontext c,float x,float y,float z,float w){if(valid(c)){if(w!=1.0f){err(0x501);state.bad=1;}else vertex(x,y,z);}}
void impl_GLVertex3fv(GLcontext c,float *v){if(valid(c)&&v)vertex(v[0],v[1],v[2]);}
void impl_GLVertex2fv(GLcontext c,float *v){if(valid(c)&&v)vertex(v[0],v[1],0);}
void impl_GLVertex4fv(GLcontext c,float *v){if(v)impl_GLVertex4f(c,v[0],v[1],v[2],v[3]);}
void impl_GLColor4f(GLcontext c,float r,float g,float b,float a){if(valid(c)){state.rgba[0]=zfloat(r);state.rgba[1]=zfloat(g);state.rgba[2]=zfloat(b);state.rgba[3]=zfloat(a);}}
void impl_GLColor3fv(GLcontext c,float *v){if(v)impl_GLColor4f(c,v[0],v[1],v[2],1);}
void impl_GLColor4fv(GLcontext c,float *v){if(v)impl_GLColor4f(c,v[0],v[1],v[2],v[3]);}
/* A8.18: n/255 from a table filled once with the same division (four divisions per call before) */
static Z ub_unit[256];
static void ub_unit_init(void){Z i;if(ub_unit[255])return;for(i=0;i<256u;i++)ub_unit[i]=zfloat((float)i/255.f);}
void impl_GLColor4ub(GLcontext c,GLubyte r,GLubyte g,GLubyte b,GLubyte a){if(valid(c)){state.rgba[0]=ub_unit[r];state.rgba[1]=ub_unit[g];state.rgba[2]=ub_unit[b];state.rgba[3]=ub_unit[a];}}
void impl_GLColor3ubv(GLcontext c,GLubyte *v){if(v)impl_GLColor4ub(c,v[0],v[1],v[2],255);}
void impl_GLColor4ubv(GLcontext c,GLubyte *v){if(v)impl_GLColor4ub(c,v[0],v[1],v[2],v[3]);}
void impl_GLTexCoord2f(GLcontext c,float s,float t){if(valid(c)){state.uv[0]=zfloat(s);state.uv[1]=zfloat(t);}}
void impl_GLTexCoord2fv(GLcontext c,float *v){if(v)impl_GLTexCoord2f(c,v[0],v[1]);}
void impl_GLNormal3f(GLcontext c,float x,float y,float z){if(valid(c)){state.nrm[0]=zfloat(x);state.nrm[1]=zfloat(y);state.nrm[2]=zfloat(z);}}
void impl_GLNormal3fv(GLcontext c,float *n){if(n)impl_GLNormal3f(c,n[0],n[1],n[2]);}
void impl_GLClear(GLcontext c,GLbitfield mask){Z a[1];a[0]=mask;command(c,Z_CLEAR,1,a);}
void impl_GLClearColor(GLcontext c,float r,float g,float b,float alpha){Z a[]={zfloat(r),zfloat(g),zfloat(b),zfloat(alpha)};command(c,Z_CLEARCOLOR,4,a);}
void impl_GLViewport(GLcontext c,GLint x,GLint y,GLsizei w,GLsizei h){Z a[]={x,y,(Z)w,(Z)h};mv_viewport[0]=x;mv_viewport[1]=y;mv_viewport[2]=w;mv_viewport[3]=h;command(c,Z_VIEW,4,a);}
void impl_GLMatrixMode(GLcontext c,GLenum mode){mx_mode(mode);command(c,Z_MATRIX,1,&mode);}
void impl_GLLoadIdentity(GLcontext c){mx_identity(mx_cur());command(c,Z_IDENTITY,0,0);}
void impl_GLTranslatef(GLcontext c,float x,float y,float z){Z a[]={zfloat(x),zfloat(y),zfloat(z)};float m[16];mx_identity(m);m[12]=x;m[13]=y;m[14]=z;mx_mul(mx_cur(),m);command(c,Z_MOVE,3,a);}
void impl_GLScalef(GLcontext c,float x,float y,float z){Z a[]={zfloat(x),zfloat(y),zfloat(z)};float m[16];mx_identity(m);m[0]=x;m[5]=y;m[10]=z;mx_mul(mx_cur(),m);command(c,Z_SCALE,3,a);}
void impl_GLRotatef(GLcontext c,float t,float x,float y,float z){Z a[]={zfloat(t),zfloat(x),zfloat(y),zfloat(z)};float m[16];mx_rotate(m,t,x,y,z);mx_mul(mx_cur(),m);command(c,Z_ROT,4,a);}
void impl_GLTranslated(GLcontext c,double x,double y,double z){impl_GLTranslatef(c,x,y,z);}
void impl_GLScaled(GLcontext c,double x,double y,double z){impl_GLScalef(c,x,y,z);}
void impl_GLRotated(GLcontext c,double t,double x,double y,double z){impl_GLRotatef(c,t,x,y,z);}
static void matrix16(GLcontext c,Z op,const float *m){Z a[16],i;if(!m){err(0x501);return;}for(i=0;i<16;i++)a[i]=zfloat(m[i]);command(c,op,16,a);}
void impl_GLLoadMatrixf(GLcontext c,const GLfloat *m){if(m)mx_copy(mx_cur(),m);matrix16(c,Z_LOADMATRIX,m);}
void impl_GLMultMatrixf(GLcontext c,const GLfloat *m){if(m)mx_mul(mx_cur(),m);matrix16(c,Z_MULTMATRIX,m);}
void impl_GLLoadMatrixd(GLcontext c,const GLdouble *m){float f[16];Z i;if(!m){err(0x501);return;}for(i=0;i<16;i++)f[i]=(float)m[i];impl_GLLoadMatrixf(c,f);}
void impl_GLMultMatrixd(GLcontext c,const GLdouble *m){float f[16];Z i;if(!m){err(0x501);return;}for(i=0;i<16;i++)f[i]=(float)m[i];impl_GLMultMatrixf(c,f);}
#define ZZ_GL_TEXTURE_GEN_S 0x0C60u
#define ZZ_GL_TEXTURE_GEN_T 0x0C61u
static int cap_known(Z cap){
 /* the capabilities the Core1 ZZGL accepts (zzglEnable / zzglDisable) */
 switch(cap){
  case 0x0DE1:case 0x0B71:case 0x0BE2:case 0x0BC0:case 0x0B44:case 0x0C11:case 0x0B90:case 0x0B60:case 0x8037:
  case 0x0C60:case 0x0C61:case 0x0C62:case 0x0C63:case 0x0BD0:case 0x0BA1:case 0x0B50:case 0x0B57:
  case 0x0B10:case 0x0B20:case 0x0B41:return 1;
 }
 return cap>=0x4000u&&cap<0x4008u;
}
void impl_MGLSetState(GLcontext c,GLenum cap,GLboolean on){
 Z a[]={cap,!!on};
 if(cap==0x0DE1u&&tmu_active==0x84C1u){if(valid(c))mt_on1=!!on;return;}   /* A8.12: unit-1 texturing */
 if(valid(c)&&!state.begun)mt_flush();   /* the queued unit-1 pass runs under the state it was queued in */
 if(cap==0x0DE1u)app_tex_on=!!on;
 if(cap==0x0BE2u)app_blend_on=!!on;
 if(cap==0x0B44u)app_cull_on=!!on;
 if(!cap_known(cap)){if(!cap_unknown++)cap_unknown_first=cap;if(zz_bb){zz_bb[18]=(unsigned short)(cap_unknown_first>>16);zz_bb[19]=(unsigned short)cap_unknown_first;}return;}
 if(cap==ZZ_GL_TEXTURE_GEN_S||cap==ZZ_GL_TEXTURE_GEN_T){
   /* the immediate-mode records carry the normal while generation is on */
   if(on)state.texgen_on|=1u<<(cap-ZZ_GL_TEXTURE_GEN_S);else state.texgen_on&=~(1u<<(cap-ZZ_GL_TEXTURE_GEN_S));
 }
 command(c,Z_ENABLE,2,a);
}
void impl_GLTexGeni(GLcontext c,GLenum coord,GLenum mode,GLenum map){
 Z a[6];a[0]=coord;a[1]=mode;a[2]=map;a[3]=0;a[4]=0;a[5]=0;
 if(coord<0x2000u||coord>0x2003u){err(0x500);return;}
 command(c,Z_TEXGEN,6,a);
}
void impl_GLTexGenfv(GLcontext c,GLenum coord,GLenum pname,const GLfloat *params){
 Z a[6];
 if(coord<0x2000u||coord>0x2003u||!params){err(0x500);return;}
 a[0]=coord;a[1]=pname;
 if(pname==0x2500u){a[2]=(Z)params[0];a[3]=a[4]=a[5]=0;}
 else{a[2]=zfloat(params[0]);a[3]=zfloat(params[1]);a[4]=zfloat(params[2]);a[5]=zfloat(params[3]);}
 command(c,Z_TEXGEN,6,a);
}
void impl_GLDepthFunc(GLcontext c,GLenum m){command(c,Z_DEPTHFUNC,1,&m);}
void impl_GLDepthMask(GLcontext c,GLboolean on){Z a=!!on;if(valid(c)&&!state.begun)mt_flush();app_depthmask=a;command(c,Z_DEPTHMASK,1,&a);}
void impl_GLBindTexture(GLcontext c,GLenum target,GLuint name){
 if(target!=0xde1 || name>=ZZ_MAX_TEXTURES){if(name>=ZZ_MAX_TEXTURES&&!bind_oor++)bind_oor_first=name;err(0x501);return;}
 if(!valid(c)||state.begun){err(0x502);return;}
 if(name)state.texnames[name>>3]|=(zu8)(1u<<(name&7u));
 if(tmu_active==0x84C1u){mt_bound1=name;return;}   /* A8.12: unit 1 stays on the 68k */
 if(name!=state.bound){command(c,Z_BIND,1,&name);state.bound=name;blend_apply(c);}
}
void impl_GLGenTextures(GLcontext c,GLsizei n,GLuint *textures){
 Z i,next=1;
 if(!valid(c)||!textures||n<0){err(0x501);return;}
 for(i=0;i<(Z)n;i++){
   while(next<ZZ_MAX_TEXTURES && (state.texnames[next>>3]&(1u<<(next&7u))))next++;
   if(next>=ZZ_MAX_TEXTURES){textures[i]=0;err(0x505);continue;}
   state.texnames[next>>3]|=(zu8)(1u<<(next&7u));textures[i]=next++;
 }
}
void impl_GLDeleteTextures(GLcontext c,GLsizei n,const GLuint *textures){
 Z i,k=0;zu8 *p;
 if(!valid(c)||state.begun||!textures||n<0){err(0x502);return;}
 while(k<(Z)n){
   Z m=(Z)n-k;if(m>256u)m=256u;
   p=record(Z_DELETE_TEX,4u+4u*m);if(!p)return;
   zput(p,m);
   for(i=0;i<m;i++){Z name=textures[k+i]<ZZ_MAX_TEXTURES?textures[k+i]:0;zput(p+4+4*i,name);lm_drop(name);tex_red[name]=0;state.texnames[name>>3]&=(zu8)~(1u<<(name&7u));if(name==state.bound)state.bound=0;}
   k+=m;
 }
}
/* A7: the window an application reads its input from (GLQuake, the demo).  A borderless backdrop
   window over the whole context screen, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY, opened on the first
   request only (the benchmark never asks: its screen stays exactly as in A6).  The previous frame is
   completed first: Intuition clears the window area of the bitmap the copy writes to. */
void *impl_MGLGetWindowHandle(GLcontext c){
 if(!valid(c)||state.begun){err(0x502);return 0;}
 resolve_present();
 return zz_window();
}
void *impl_MGLGetInputWindowHandle(GLcontext c){return impl_MGLGetWindowHandle(c);}
/* A8.28: the classic MiniGL main loop (the demonstration programs written for MiniGL 1.2 and for this
   library interface: mglIdleFunc, mglKeyFunc, mglMainLoop, mglExit).  MGLMainLoop reads the events of the
   context's window, hands each key that has a character to the key function, then calls the idle
   function once (the application draws a frame there), until MGLExit.  With no idle function it sleeps
   until the next event.  Mouse and special-key functions are not offered (MGLMouseFunc and
   MGLSpecialFunc stay unsupported entries).
   AUTOEXIT (ZZMiniGLLoad switch): the loop also returns by itself 15 seconds after it was entered, as if
   the application had called MGLExit: a program that ends on a key only can be tried from a script. */
#define ZZ_LOOP_AUTOEXIT_SECONDS 15u
void impl_MGLIdleFunc(GLcontext c,IdleFn i){if(!valid(c)){err(0x502);return;}loop_idle=i;}
void impl_MGLKeyFunc(GLcontext c,KeyHandlerFn k){if(!valid(c)){err(0x502);return;}loop_key=k;}
void impl_MGLExit(GLcontext c){(void)c;loop_run=0;}
void impl_MGLMainLoop(GLcontext c){
 Ptr w,port,m;Z t0,hz,cls,code,autoexit=zz_ctx_flags()&ZZ_ARM_AUTOEXIT;
 if(!valid(c)||state.begun||loop_run){err(0x502);return;}
 w=(Ptr)impl_MGLGetWindowHandle(c);
 port=w?*(Ptr *)((zu8 *)w+86):0;                     /* Window.UserPort */
 zz_window_keys(1);
 loop_run=1;loop_entries++;t0=zz_clock();hz=zz_eclock_hz();
 while(loop_run){
   while(port&&loop_run&&(m=os_getmsg(port))!=0){
     cls=*(Z *)((zu8 *)m+20);code=*(unsigned short *)((zu8 *)m+24);   /* IntuiMessage.Class, .Code */
     os_replymsg(m);loop_events++;
     if(cls==0x00200000u&&loop_key){loop_keys++;loop_key((char)code);}   /* IDCMP_VANILLAKEY */
   }
   if(!loop_run)break;
   if(loop_idle){loop_idles++;loop_idle();}
   else if(port&&!autoexit)os_waitport(port);
   else if(!port&&!autoexit)break;                    /* nothing to call and nothing to wait for */
   if(autoexit&&hz&&zz_clock()-t0>=ZZ_LOOP_AUTOEXIT_SECONDS*hz){loop_auto++;break;}
   if(autoexit&&!loop_idle)zz_spin_until(zz_clock()+hz/50u);
 }
 loop_run=0;loop_ticks+=zz_clock()-t0;
 if(valid(c))zz_window_keys(0);
}
/* A8.28: GL_UNPACK_ALIGNMENT is honoured by glTexImage2D and glTexSubImage2D (1, 2, 4 or 8; 4 until it is
   set, which is what the rows were read with so far).  GL_PACK_ALIGNMENT is recorded: nothing here writes
   an image into the application's memory with it.  The other parameters are accepted when they are given
   their initial value and refused with GL_INVALID_ENUM otherwise. */
void impl_GLPixelStorei(GLcontext c,GLenum pname,GLint param){
 if(!valid(c)||state.begun){err(0x502);return;}
 pixelstore_calls++;
 switch(pname){
  case 0x0CF5:case 0x0D05:                                  /* GL_UNPACK_ALIGNMENT, GL_PACK_ALIGNMENT */
   if(param!=1&&param!=2&&param!=4&&param!=8){err(0x501);return;}
   if(pname==0x0CF5)unpack_align=(Z)param;else pack_align=(Z)param;
   break;
  case 0x0CF0:case 0x0CF1:case 0x0CF2:case 0x0CF3:case 0x0CF4:      /* GL_UNPACK_SWAP_BYTES .. GL_UNPACK_SKIP_PIXELS */
  case 0x0D00:case 0x0D01:case 0x0D02:case 0x0D03:case 0x0D04:      /* GL_PACK_SWAP_BYTES .. GL_PACK_SKIP_PIXELS */
   if(param){pixelstore_refused++;err(0x500);}
   break;
  default:pixelstore_refused++;err(0x500);break;
 }
}
/* A8.28: gluPerspective, as a frustum (fovy in degrees, the whole vertical angle). */
void impl_GLFrustum(GLcontext c,double l,double r,double b,double t,double n,double f);
void impl_GLUPerspective(GLfloat fovy,GLfloat aspect,GLfloat znear,GLfloat zfar){
 GLcontext c=(GLcontext)&state;float sn,cs;double ymax,xmax;
 if(!valid(c)||state.begun){err(0x502);return;}
 zz_sincos_deg(fovy*0.5f,&sn,&cs);
 if(!(fovy>0.0f)||!(fovy<180.0f)||!(aspect>0.0f)||!(znear>0.0f)||!(zfar>znear)||!(cs>0.0f)){err(0x501);return;}
 ymax=(double)znear*(double)sn/(double)cs;xmax=ymax*(double)aspect;
 impl_GLFrustum(c,-xmax,xmax,-ymax,ymax,(double)znear,(double)zfar);
}
GLboolean impl_GLIsTexture(GLcontext c,GLuint t){if(!valid(c)||!t||t>=ZZ_MAX_TEXTURES)return 0;return (state.texnames[t>>3]>>(t&7u))&1u;}
/* A8: the source formats GLQuake uses, converted here to what the wire carries (RGB 3, RGBA 4 or
   LUMINANCE 1 bytes per texel): GL_RGB, GL_RGBA, GL_LUMINANCE as they are; GL_ALPHA and
   GL_LUMINANCE_ALPHA to RGBA; MiniGL's MGL_UNSIGNED_SHORT_4_4_4_4 (0x701B, big-endian ARGB4444 words,
   GLQuake's lightmaps) to RGB.  Rows follow GL_UNPACK_ALIGNMENT 4 (GLQuake never changes it).  Level 0
   only: the other mipmap levels are accepted and dropped (Core1 keeps one level).  A texture smaller than
   8 texels in one direction is enlarged by repetition to 8 (the TMU minimum).  At most 256 x 256. */
#define ZZ_MGL_ARGB4444 0x701Bu
/* A8.27: a level-0 image of up to 2048 x 2048 texels is accepted; what is larger than 256 in one direction
   is reduced there by 2, 4 or 8 before it leaves the 68k (the rasteriser's textures stop at 256 x 256, and
   the texture coordinates are fractions of the image, so the drawing is the same at a lower definition).
   Each texel of the reduced image is the mean of the block it replaces; with an alpha channel (GL_RGBA,
   GL_LUMINANCE_ALPHA) the colour is weighted by the alpha, so that the colour of transparent texels does
   not bleed into the edges (a block of one single alpha, as inside an opaque or an empty area, takes the
   plain mean).  GL_MAX_TEXTURE_SIZE stays 256: an application that asks is told what is really stored.
   A sub-image of a reduced texture is reduced the same way when its rectangle falls on whole blocks,
   otherwise dropped and counted.  ARGB4444 words (GLQuake's lightmaps) stay limited to 256. */
static void box_reduce(zu8 *d,const zu8 *s,Z w,Z h,Z pitch,Z nb,Z sx,Z sy,int weighted){
 Z ow=w>>sx,oh=h>>sy,fx=1u<<sx,fy=1u<<sy,sh=sx+sy,half=(1u<<sh)>>1,opitch=(ow*nb+3u)&~3u,x,y,i,j,k;
 for(y=0;y<oh;y++){
   const zu8 *r=s+(y<<sy)*pitch;zu8 *o=d+y*opitch;
   for(x=0;x<ow;x++,o+=nb){
     const zu8 *t=r+(x<<sx)*nb;Z acc[4];
     acc[0]=acc[1]=acc[2]=acc[3]=0;
     if(weighted){
       Z sa=0,a0=t[nb-1u],same=1;
       for(j=0;j<fy;j++){const zu8 *q=t+j*pitch;for(i=0;i<fx;i++,q+=nb){Z a=q[nb-1u];sa+=a;if(a!=a0)same=0;}}
       if(!same){
         for(j=0;j<fy;j++){const zu8 *q=t+j*pitch;for(i=0;i<fx;i++,q+=nb){Z a=q[nb-1u];for(k=0;k+1u<nb;k++)acc[k]+=q[k]*a;}}
         for(k=0;k+1u<nb;k++)o[k]=(zu8)((acc[k]+(sa>>1))/sa);
         o[nb-1u]=(zu8)((sa+half)>>sh);
         continue;
       }
     }
     for(j=0;j<fy;j++){const zu8 *q=t+j*pitch;for(i=0;i<fx;i++,q+=nb)for(k=0;k<nb;k++)acc[k]+=q[k];}
     for(k=0;k<nb;k++)o[k]=(zu8)((acc[k]+half)>>sh);
   }
 }
}
static void image(GLcontext c,Z op,Z target,int level,int x,int y,long w,long h,int border,Z fmt,Z type,const zu8 *src);
static void image_body(GLcontext c,Z op,Z target,int level,int x,int y,long w,long h,int border,Z fmt,Z type,const zu8 *src){
 Z inb,outb,n,row,pitch,i,j,k,ow,oh,rx=1,ry=1,raw4444,sx=0,sy=0;zu8 *p;
 if(!valid(c)||state.begun){err(0x502);return;}
 mt_flush();
 if(tmu_active==0x84C1u&&mt_bound1!=state.bound){
   /* A8.12: an upload with unit 1 active goes to unit 1's texture: bind it, upload, bind unit 0 back */
   Z save=state.bound,nm=mt_bound1;zu8 *b;
   mt_flush();
   b=record(Z_BIND,4);if(b)zput(b,nm);state.bound=nm;
   tmu_active=0x84C0u;image(c,op,target,level,x,y,w,h,border,fmt,type,src);tmu_active=0x84C1u;
   b=record(Z_BIND,4);if(b)zput(b,save);state.bound=save;
   blend_apply(c);   /* A8.13: the blend that goes with unit 0's texture again (the upload translated it for the lightmap) */
   return;
 }
 if(target!=0xde1||type!=0x1401){err(0x500);return;}
 switch(fmt){
  case 0x1907:inb=3;outb=3;break;                  /* GL_RGB */
  case 0x1908:inb=4;outb=4;break;                  /* GL_RGBA */
  case 0x1909:inb=1;outb=1;break;                  /* GL_LUMINANCE */
  case 0x1906:inb=1;outb=4;break;                  /* GL_ALPHA */
  case 0x190A:inb=2;outb=4;break;                  /* GL_LUMINANCE_ALPHA */
  case ZZ_MGL_ARGB4444:inb=2;outb=3;break;
  default:err(0x500);return;
 }
 if(level!=0){img_mips_dropped++;return;}
 if(border||w<1||w>ZZ_TEX_SRC_MAX||h<1||h>ZZ_TEX_SRC_MAX||x<0||y<0){err(0x501);return;}
 if(op==Z_IMAGE && ((w&(w-1))||(h&(h-1)))){err(0x501);return;}
 if(op==Z_SUBIMAGE && !src){err(0x501);return;}
 if(op==Z_IMAGE){
   /* A8.27: more than 256 texels in a direction: reduced there */
   while((w>>sx)>256)sx++;
   while((h>>sy)>256)sy++;
   if((sx||sy)&&fmt==ZZ_MGL_ARGB4444){err(0x501);return;}
   if(state.bound<ZZ_MAX_TEXTURES)tex_red[state.bound]=(zu8)(sx|(sy<<4));
   if(sx||sy){img_reduced++;if((Z)w>img_reduced_w)img_reduced_w=(Z)w;if((Z)h>img_reduced_h)img_reduced_h=(Z)h;}
 }else if(state.bound<ZZ_MAX_TEXTURES&&tex_red[state.bound]){
   sx=tex_red[state.bound]&15u;sy=tex_red[state.bound]>>4;
   if(fmt==ZZ_MGL_ARGB4444||(((Z)x|(Z)w)&((1u<<sx)-1u))||(((Z)y|(Z)h)&((1u<<sy)-1u))){img_sub_unaligned++;return;}
   x>>=sx;y>>=sy;
 }
 if(sx||sy){
   if(src){
     if(!big_buf)big_buf=(zu8 *)os_alloc(ZZ_BIG_BYTES,4u);   /* MEMF_FAST, kept until the context closes */
     if(!big_buf){err(0x505);return;}
     box_reduce(big_buf,src,(Z)w,(Z)h,src_pitch((Z)w,inb),inb,sx,sy,fmt==0x1908||fmt==0x190A);
     src=big_buf;
   }
   w>>=sx;h>>=sy;
 }
 if(w>256||h>256||x>256-w||y>256-h){err(0x501);return;}
 ow=(Z)w;oh=(Z)h;
 if(op==Z_IMAGE && (w<8||h<8)){if(w<8){rx=8u/(Z)w;ow=8;}if(h<8){ry=8u/(Z)h;oh=8;}img_small_scaled++;}
 raw4444=fmt==ZZ_MGL_ARGB4444&&rx==1u&&ry==1u&&src&&raw_ok();
 if(raw4444)outb=2;   /* A8.17: the words as they are, Core1 makes the RGB */
 if(inb!=outb||raw4444)img_converted++;
 row=ow*outb;pitch=(sx||sy)?(((Z)w*inb+3u)&~3u):src_pitch((Z)w,inb);n=row*oh;   /* a reduced image is in big_buf, rows on 4 bytes */
 if(op==Z_IMAGE&&state.bound<ZZ_MAX_TEXTURES){   /* A8.5: which names hold an ARGB4444 lightmap */
   if(fmt==ZZ_MGL_ARGB4444)lm_names[state.bound>>3]|=(zu8)(1u<<(state.bound&7u));else lm_names[state.bound>>3]&=(zu8)~(1u<<(state.bound&7u));
   blend_apply(c);
 }
 if(state.bound<ZZ_MAX_TEXTURES){
   Z nm=state.bound;
   /* A8.21: GL_LUMINANCE textures have a shadow too (GLQuake R200's 8-bit lightmaps): bt bytes per texel */
   Z bt=raw4444?2u:((fmt==0x1909u&&rx==1u&&ry==1u&&src&&raw_ok())?1u:0u);
   if(bt&&op==Z_SUBIMAGE&&lm_sub(nm,(Z)x,(Z)y,ow,oh,src,pitch,bt))return;   /* A8.17: changed rows only */
   if(!bt||(op==Z_IMAGE&&(lm_shw[nm]!=ow||lm_shh[nm]!=oh||lm_shb[nm]!=bt)))lm_drop(nm);
   if(bt&&op==Z_IMAGE&&!(nodraw&&dropped(op))){
     Z k;
     if(!lm_sh[nm]&&(lm_sh[nm]=(zu8 *)os_alloc(ow*oh*bt,4u))!=0){lm_shw[nm]=(unsigned short)ow;lm_shh[nm]=(unsigned short)oh;lm_shb[nm]=(zu8)bt;}   /* MEMF_FAST */
     if(lm_sh[nm])for(k=0;k<oh;k++){const zu8 *s=src+k*pitch;zu8 *d=lm_sh[nm]+k*ow*bt;Z j;for(j=0;j<ow*bt;j++)d[j]=s[j];}
   }
   else if(bt&&op==Z_SUBIMAGE)lm_drop(nm);   /* an update the shadow cannot follow */
 }
 p=record(op,24+((n+3)&~3u));if(!p)return;
 zput(p,ow);zput(p+4,oh);zput(p+8,raw4444?(RAW_BE?ZZ_TEX_ARGB4444_BE:ZZ_TEX_ARGB4444_LE):outb);zput(p+12,x);zput(p+16,y);zput(p+20,n);p+=24;
 if(raw4444){
   for(i=0;i<oh;i++)raw16_rows(p+i*row,src+i*pitch,row);
   raw_records++;
 }
 else if(src&&rx==1u&&ry==1u){
   /* A8.16: nothing enlarged (every lightmap update, almost every load): the same bytes as the general
      loop below, row by row, without its two divisions per texel */
   for(i=0;i<oh;i++){
     const zu8 *s=src+i*pitch;zu8 *d=p+i*row;
     if(fmt==ZZ_MGL_ARGB4444){
       const unsigned short *u=(const unsigned short *)(const void *)s;
       for(j=0;j<ow;j++,d+=3){Z v=u[j],r=(v>>8)&15u,g=(v>>4)&15u,b=v&15u;d[0]=(zu8)(r<<4|r);d[1]=(zu8)(g<<4|g);d[2]=(zu8)(b<<4|b);}
     }
     else if(fmt==0x1906){for(j=0;j<ow;j++,d+=4){d[0]=d[1]=d[2]=255;d[3]=s[j];}}
     else if(fmt==0x190A){for(j=0;j<ow;j++,d+=4,s+=2){d[0]=d[1]=d[2]=s[0];d[3]=s[1];}}
     else for(j=0;j<row;j++)d[j]=s[j];
   }
 }
 else for(i=0;i<oh;i++){
   const zu8 *s=src?src+(i/ry)*pitch:0;zu8 *d=p+i*row;
   for(j=0;j<ow;j++){
     const zu8 *t=s?s+(j/rx)*inb:0;
     if(!t){for(k=0;k<outb;k++)*d++=0;continue;}
     switch(fmt){
      case 0x1906:d[0]=d[1]=d[2]=255;d[3]=t[0];d+=4;break;
      case 0x190A:d[0]=d[1]=d[2]=t[0];d[3]=t[1];d+=4;break;
      case ZZ_MGL_ARGB4444:{Z v=*(const unsigned short *)(const void *)t;d[0]=(zu8)(((v>>8)&15u)*17u);d[1]=(zu8)(((v>>4)&15u)*17u);d[2]=(zu8)((v&15u)*17u);d+=3;break;}
      default:for(k=0;k<outb;k++)*d++=t[k];break;
     }
   }
 }
 for(i=n;i<((n+3)&~3u);i++)p[i]=0;
}
static void image(GLcontext c,Z op,Z target,int level,int x,int y,long w,long h,int border,Z fmt,Z type,const zu8 *src){P_IN(PC_TEX);image_body(c,op,target,level,x,y,w,h,border,fmt,type,src);P_OUT();}
void impl_GLTexImage2D(GLcontext c,GLenum t,GLint l,GLint fmt,GLsizei w,GLsizei h,GLint border,GLenum sf,GLenum type,const void *p){
 (void)fmt;   /* A8: the internal format (1..4, GL_RGB5, GL_RGBA4...) is a hint; the source format decides */
 image(c,Z_IMAGE,t,l,0,0,w,h,border,sf,type,p);
}
void impl_GLTexSubImage2D(GLcontext c,GLenum t,GLint l,GLint x,GLint y,GLsizei w,GLsizei h,GLenum sf,GLenum type,const void *p){image(c,Z_SUBIMAGE,t,l,x,y,w,h,0,sf,type,p);}
void impl_GLFlush(GLcontext c){if(valid(c)&&!state.begun){record(Z_FLUSH,0);flush(0);}else err(0x502);}
void impl_GLFinish(GLcontext c){if(valid(c)&&!state.begun){resolve_present();record(Z_FINISH,0);flush(1);}else err(0x502);}
GLenum impl_GLGetError(GLcontext c){
 Z e,g;if(!valid(c))return 0x502;e=state.error;state.error=0;
 if(e)return e;
 if(zz_last_error)return 0x502;
 /* A3: GL errors raised on Core1 are non-fatal and surface here once each */
 g=zz_metric(MH_LAST_GL);
 if(g && g!=state.reported_gl){state.reported_gl=g;return g;}
 return 0;
}
const GLubyte *impl_GLGetString(GLcontext c,GLenum name){
 if(!valid(c))return 0;
 switch(name){case 0x1f00:return (const GLubyte *)"XACP";case 0x1f01:return (const GLubyte *)"ZZ9000 RasterIX G4C";case 0x1f02:return (const GLubyte *)"1.1 ZZMiniGL A8 subset";case 0x1f03:return (const GLubyte *)(rgl_on?"GL_EXT_compiled_vertex_array GL_ZZ_texgen_frame GL_RGL_static_arrays GL_RGL_dynamic_arrays GL_RGL_arrays_tag":"GL_EXT_compiled_vertex_array GL_ZZ_texgen_frame");default:err(0x500);return 0;}
}
void impl_GLFrustum(GLcontext c,double l,double r,double b,double t,double n,double f){Z a[]={zfloat(l),zfloat(r),zfloat(b),zfloat(t),zfloat(n),zfloat(f)};mx_frustum((float)l,(float)r,(float)b,(float)t,(float)n,(float)f,0);proj_ortho=0;f3d_seen=1;command(c,Z_FRUSTUM,6,a);}
void impl_GLOrtho(GLcontext c,double l,double r,double b,double t,double n,double f){Z a[]={zfloat(l),zfloat(r),zfloat(b),zfloat(t),zfloat(n),zfloat(f)};mx_frustum((float)l,(float)r,(float)b,(float)t,(float)n,(float)f,1);proj_ortho=1;command(c,Z_ORTHO,6,a);}
void impl_GLPushMatrix(GLcontext c){mx_push();command(c,Z_PUSH,0,0);}void impl_GLPopMatrix(GLcontext c){mx_pop();command(c,Z_POP,0,0);}
static void blend_apply(GLcontext c){
 Z s=blend_req_s,d=blend_req_d,a[2];
 if(s==0x302u&&d==0x303u&&!(zz_ctx_flags()&ZZ_ARM_NOLMBLEND)&&state.bound<ZZ_MAX_TEXTURES&&(lm_names[state.bound>>3]>>(state.bound&7u)&1u)){s=0;d=0x300u;lm_blend_mult++;}
 if(s==blend_sent_s&&d==blend_sent_d)return;
 a[0]=s;a[1]=d;command(c,Z_BLEND,2,a);blend_sent_s=s;blend_sent_d=d;
}
void impl_GLBlendFunc(GLcontext c,GLenum s,GLenum d){blend_req_s=s;blend_req_d=d;blend_apply(c);}
void impl_GLAlphaFunc(GLcontext c,GLenum f,GLclampf ref){Z a[]={f,zfloat(ref)};command(c,Z_ALPHA,2,a);}
void impl_GLTexEnvi(GLcontext c,GLenum t,GLenum p,GLint x){Z a[]={p,x};if(t!=0x2300){err(0x500);return;}if(tmu_active==0x84C1u){if(p==0x2200u)mt_env1=(Z)x;return;}if(p==0x2200u&&valid(c))app_env=(Z)x;command(c,Z_TEXENV,2,a);}   /* TEXENV goes out at once (no flush): the pass restores the latest */
void impl_GLTexParameteri(GLcontext c,GLenum t,GLenum p,GLint x){
 Z a[]={p,x};
 if(t!=0xde1){err(0x500);return;}
 if((p==0x2800u||p==0x2801u)&&(zz_ctx_flags()&ZZ_ARM_TEXNEAREST))a[1]=0x2600u;   /* A8.13 TEXNEAREST */
 if(tmu_active==0x84C1u&&mt_bound1!=state.bound&&valid(c)&&!state.begun){
   /* A8.13: a parameter set with unit 1 active belongs to unit 1's texture (GLQuake's lightmaps) */
   zu8 *b;mt_flush();
   b=record(Z_BIND,4);if(b)zput(b,mt_bound1);
   b=record(Z_TEXPAR,8);if(b){zput(b,a[0]);zput(b+4,a[1]);}
   b=record(Z_BIND,4);if(b)zput(b,state.bound);
   return;
 }
 command(c,Z_TEXPAR,2,a);
}
void impl_GLDepthRange(GLcontext c,double n,double f){Z a[]={zfloat(n),zfloat(f)};command(c,Z_DEPTHRANGE,2,a);}
void impl_GLClearDepth(GLcontext c,double d){Z a[]={zfloat(d)};command(c,Z_CLEARD,1,a);}
void impl_GLShadeModel(GLcontext c,GLenum x){command(c,Z_SHADE,1,&x);}
void impl_GLCullFace(GLcontext c,GLenum x){command(c,Z_CULL,1,&x);}
void impl_GLFrontFace(GLcontext c,GLenum x){command(c,Z_FRONT,1,&x);}
void impl_GLScissor(GLcontext c,GLint x,GLint y,GLsizei w,GLsizei h){Z a[]={x,y,w,h};command(c,Z_SCISSOR,4,a);}

/* ==== A8: the MiniGL 29.1 entries GLQuake (Aminet MiniGL_Library_Quake1, ABI 5) calls ====
   Static scan of the executable: tools/glquake_slots.txt.  Kept minimal and honest: what this backend
   cannot do is accepted and ignored (counted by the trace), never faked as done. */
#define ZZ_GLROT_X 0x2000
#define ZZ_GLROT_Y 0x1000
#define ZZ_GLROT_Z 0x0800
static int axis_of(GLint xyz,float *x,float *y,float *z){
 *x=*y=*z=0.0f;
 if(xyz==ZZ_GLROT_X)*x=1.0f;else if(xyz==ZZ_GLROT_Y)*y=1.0f;else if(xyz==ZZ_GLROT_Z)*z=1.0f;else return 0;
 return 1;
}
void impl_GLRotatefEXT(GLcontext c,GLfloat angle,const GLint xyz){
 float x,y,z;if(!axis_of(xyz,&x,&y,&z)){err(0x500);return;}
 impl_GLRotatef(c,angle,x,y,z);
}
void impl_GLRotatefEXTs(GLcontext c,GLfloat sn,GLfloat cs,const GLint xyz){
 float x,y,z,m[16];if(!axis_of(xyz,&x,&y,&z)){err(0x500);return;}
 mx_rotate_sc(m,sn,cs,x,y,z);impl_GLMultMatrixf(c,m);
}
void impl_GLGetFloatv(GLcontext c,GLenum pname,GLfloat *v){
 Z i;if(!valid(c)||!v){err(0x502);return;}
 switch(pname){
  case 0x0BA6:mx_copy(v,mx_mv[mx_sp[0]]);break;          /* GL_MODELVIEW_MATRIX */
  case 0x0BA7:mx_copy(v,mx_pj[mx_sp[1]]);break;          /* GL_PROJECTION_MATRIX */
  case 0x0BA8:mx_copy(v,mx_tx[mx_sp[2]]);break;          /* GL_TEXTURE_MATRIX */
  case 0x0B00:for(i=0;i<4;i++)v[i]=zreal(state.rgba[i]);break;   /* GL_CURRENT_COLOR */
  default:v[0]=0.0f;err(0x500);break;
 }
}
void impl_GLGetIntegerv(GLcontext c,GLenum pname,GLint *v){
 Z i;if(!valid(c)||!v){err(0x502);return;}
 switch(pname){
  case 0x0D33:v[0]=256;break;                               /* GL_MAX_TEXTURE_SIZE */
  case 0x0BA2:for(i=0;i<4;i++)v[i]=(GLint)mv_viewport[i];break;   /* GL_VIEWPORT */
  case 0x0BA0:v[0]=mx_which==1u?0x1701:mx_which==2u?0x1702:0x1700;break;   /* GL_MATRIX_MODE */
  case 0x0D36:case 0x0D38:v[0]=(GLint)MX_DEPTH;break;      /* GL_MAX_MODELVIEW/PROJECTION_STACK_DEPTH */
  case 0x84E2:v[0]=1;break;                                 /* GL_MAX_TEXTURE_UNITS_ARB */
  case 0x8069:v[0]=(GLint)state.bound;break;                /* GL_TEXTURE_BINDING_2D */
  case 0x0D52:case 0x0D54:v[0]=5;break;                     /* GL_RED_BITS, GL_BLUE_BITS: the RGB565 colour buffer (A8.27) */
  case 0x0D53:v[0]=6;break;                                 /* GL_GREEN_BITS */
  case 0x0D55:case 0x0D57:v[0]=0;break;                     /* GL_ALPHA_BITS, GL_STENCIL_BITS */
  case 0x0D56:v[0]=16;break;                                /* GL_DEPTH_BITS */
  default:v[0]=0;err(0x500);break;
 }
}
/* one texture unit: unit 0 is the ordinary path; unit 1 is not available (the multitexture extension is
   not advertised, so GLQuake uses its own two-pass lightmaps) */
void impl_GLActiveTextureARB(GLcontext c,GLenum unit){if(valid(c))tmu_active=unit;}
void impl_GLClientActiveTextureARB(GLcontext c,GLenum unit){if(valid(c)&&(unit==0x84C0u||unit==0x84C1u))state.client_unit=unit;}
void impl_GLMultiTexCoord2fARB(GLcontext c,GLenum unit,GLfloat s,GLfloat t){if(unit==0x84C0u)impl_GLTexCoord2f(c,s,t);else if(unit==0x84C1u){mt_uv1[0]=zfloat(s);mt_uv1[1]=zfloat(t);}}
void impl_GLMultiTexCoord2fvARB(GLcontext c,GLenum unit,GLfloat *v){if(v)impl_GLMultiTexCoord2fARB(c,unit,v[0],v[1]);}
void impl_MGLDrawMultitexBuffer(GLcontext c,GLenum s,GLenum d,GLenum e){(void)c;(void)s;(void)d;(void)e;}
/* state the backend does not have: accepted, ignored (the trace shows how often) */
void impl_GLHint(GLcontext c,GLenum target,GLenum mode){
 /* A8.21: the GL_RGL hints (0x1100 GL_DONT_CARE, 0x1101 GL_FASTEST, 0x1102 GL_NICEST); the others change nothing */
 if(!valid(c)||!rgl_on)return;
 switch(target){
  case RGL_STATIC_ARRAYS:if(mode==0x1101u)rgl_forget();rgl_static=mode==0x1102u;rgl_tag=0;break;
  case RGL_STATIC_INDICES:if(mode==0x1101u)rgl_forget();rgl_static_idx=mode==0x1102u;break;
  case RGL_DYNAMIC_ARRAYS:rgl_dynamic=mode==0x1102u;break;
  case RGL_ARRAYS_TAG:rgl_tag=mode;break;
  default:break;
 }
}
void impl_GLPolygonMode(GLcontext c,GLenum face,GLenum mode){(void)c;(void)face;(void)mode;}
void impl_GLColorMask(GLcontext c,GLboolean r,GLboolean g,GLboolean b,GLboolean a){(void)c;(void)r;(void)g;(void)b;(void)a;}
void impl_GLDrawBuffer(GLcontext c,GLenum mode){(void)c;(void)mode;}
void impl_GLPointSize(GLcontext c,GLfloat size){
 /* A8.23: Core1 draws GL_POINTS as squares of this size; sent when it changes */
 Z w;zu8 *p;
 if(!valid(c)||state.begun){err(0x502);return;}
 if(!(size>0.0f)){err(0x501);return;}
 if(size>64.0f)size=64.0f;
 w=zfloat(size);if(w==c1_point_size||w<0x00800000u)return;
 p=record(Z_POINTSIZE,4);if(p){zput(p,w);if(!prof_sink)c1_point_size=w;}
}
void impl_GLFogf(GLcontext c,GLenum pname,GLfloat param){(void)c;(void)pname;(void)param;}
void impl_GLFogfv(GLcontext c,GLenum pname,GLfloat *param){(void)c;(void)pname;(void)param;}
void impl_MGLMinTriArea(GLcontext c,GLfloat area){(void)c;(void)area;}
void impl_MGLSetZOffset(GLcontext c,GLfloat offset){(void)c;(void)offset;}
/* A8.9: these calls precede work on the screen, its window or its pointer by the application: any held
   P96 lock is released first */
void impl_MGLLockMode(GLcontext c,GLenum mode){(void)mode;if(valid(c)&&!state.begun)resolve_present();}
/* the presentation locks its bitmap itself around each Core1 copy: the display lock of MiniGL is a no-op */
GLboolean impl_MGLLockDisplay(GLcontext c){return valid(c)?1u:0u;}
void impl_MGLUnlockDisplay(GLcontext c){if(valid(c)&&!state.begun)resolve_present();}
GLboolean impl_MGLResizeContext(GLcontext c,GLsizei w,GLsizei h){return (valid(c)&&(Z)w==state.width&&(Z)h==state.height)?1u:0u;}
void impl_MGLSetPointer(GLcontext c){if(valid(c)&&!state.begun){resolve_present();zz_pointer(1);}}     /* A8.20: the pointer is hidden */
void impl_MGLClearPointer(GLcontext c){if(valid(c)&&!state.begun){resolve_present();zz_pointer(0);}}
void impl_MGLWriteShotPPM(GLcontext c,char *name){(void)c;(void)name;}
/* display choices made before MGLCreateContext: 16-bit 320x240, 640x480 or 800x600 screens only, nothing to choose */
void impl_mglChooseGuardBand(GLboolean f){(void)f;}
void impl_mglChooseMtexBufferSize(int n){(void)n;}
void impl_mglChooseNumberOfBuffers(int n){(void)n;}
void impl_mglChoosePixelDepth(int d){(void)d;}
void impl_mglChooseVertexBufferSize(int n){(void)n;}
void impl_mglChooseWindowMode(GLboolean f){(void)f;}
void impl_mglProposeCloseDesktop(GLboolean f){(void)f;}
