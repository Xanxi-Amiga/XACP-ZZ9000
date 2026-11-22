#include "wire.h"
/* Reject nonfinite floats before any descriptor can be handed to RasterIX. */
static int finite_words(const zu8 *data,zu32 count){
 zu32 i;
 for(i=0;i<count;i++)if((zget(data+4*i)&0x7f800000u)==0x7f800000u)return 0;
 return 1;
}
/* A8.17: the float words of raw vertices (the packed colour word is not a float) */
static int raw_finite(const zu8 *data,zu32 count,zu32 fmt,zu32 flags){
 zu32 vb=zraw_vertex_bytes(fmt,flags),fw=vb/4u,i,k,be=flags&ZZ_RAW_BE;
 if((fmt&ZZ_VF_COLOR)&&(flags&0xFFu)!=ZZ_RAWCOL_FLOAT)fw--;   /* the colour word comes last */
 for(i=0;i<count;i++,data+=vb)for(k=0;k<fw;k++){zu32 w=zget(data+4u*k);if(be)w=zswap32(w);if((w&0x7f800000u)==0x7f800000u)return 0;}
 return 1;
}
static int raw_flags_ok(zu32 fmt,zu32 flags){
 if(flags&~(0xFFu|ZZ_RAW_BE))return 0;
 if((fmt&ZZ_VF_COLOR)&&(flags&0xFFu)>ZZ_RAWCOL_RGB8)return 0;
 return 1;
}
/* A8.17: a kept Z_ARRAY_LOAD_RAW (from fmt on), checked again before it is loaded from the cache */
int zraw_load_ok(const zu8 *p,zu32 n){
 zu32 fmt,flags,count;
 if(n<12u)return 0;
 fmt=zget(p);flags=zget(p+4);count=zget(p+8);
 if(!(fmt&ZZ_VF_POS3)||(fmt&~ZZ_VF_RAWALL)||!raw_flags_ok(fmt,flags)||count<1u||count>ZZ_RAW_MAX_VERTS)return 0;
 return n==12u+count*zraw_vertex_bytes(fmt,flags);
}
static int extent(zu32 op,const zu8 *p,zu32 n);
/* A8.19: a kept index record: the op word, then the payload of Z_DRAW_ELEMENTS_RAW or Z_DRAW_ELEMENTS_MT */
int zkept_draw_ok(const zu8 *p,zu32 n){
 zu32 op;
 if(n<16u||(n&3u))return 0;
 op=zget(p);
 if(op!=Z_DRAW_ELEMENTS_RAW&&op!=Z_DRAW_ELEMENTS_MT)return 0;
 return extent(op,p+4,n-4u)==0;
}
zu32 zkept_draw_bytes(const zu8 *p){
 return 4u+(zget(p)==Z_DRAW_ELEMENTS_MT?32u:12u)+((zget(p+8)*2u+3u)&~3u);
}
static int valid_mode(zu32 mode){return mode<=9u;}   /* GL_POINTS..GL_POLYGON; A8.27: the points and lines of array draws are drawn (zzgl.c draw_point, draw_line) */
static int extent(zu32 op,const zu8 *p,zu32 n){
 zu32 count,bpp,mode,w,h,fmt,vb;
 static const zu8 words[]={0,1,4,4,1,0,3,4,3,2,1,1,1,0,0,0,0,0,6,6,0,0,2,2,2,2,2,1,1,1,4,1,0,0};
 if(op==Z_P96_PROBE || op==Z_PRESENT_DIRECT || op==Z_SHOW){
   zu32 a, bytes, pitch;
   /* The direct present record includes the pixel layout and a bounded
      copy strategy. A1c/v3 forbids the old 12-byte direct packet. */
   if(n != (op==Z_P96_PROBE ? 12u : 16u))return ZE_SIZE;
   a=zget(p);
   if((a&3u)||a<ZZ_P96_LOW_ARM||a>=ZZ_P96_HIGH_ARM)return ZE_ARG;
   if(op==Z_P96_PROBE)return a<=ZZ_P96_HIGH_ARM-8u?0:ZE_ARG;
   pitch=zget(p+4);
   if(op==Z_PRESENT_DIRECT||op==Z_SHOW){
     if(zget(p+8)!=ZZ_P96_FMT_RGB565_PC &&
        zget(p+8)!=ZZ_P96_FMT_RGB565_BE)return ZE_ARG;
     if(zget(p+12)!=ZZ_P96_COPY_SCALAR &&
        zget(p+12)!=ZZ_P96_COPY_BURST32 &&
        zget(p+12)!=ZZ_P96_COPY_DMA)return ZE_ARG;   /* A5 */
   }
   /* The smallest extent a picture may have here; the runtime validates the precise bounds of the
      context's size (up to 800x600 since A8.26). */
   if(pitch<640u||pitch>4096u||(pitch&3u))return ZE_ARG;
   bytes=(480u-1u)*pitch+640u*2u;
   if(a>ZZ_P96_HIGH_ARM-bytes)return ZE_ARG;
   return 0;
 }
 if(op==Z_DRAW_PACKED){
   if(n<8u)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);
   if(mode!=4u||count<3u||count>ZZ_MAX_ARRAY_VERTS||(count%3u))return ZE_ARG;
   if(n!=8u+count*20u)return ZE_SIZE;
   return finite_words(p+8u,count*5u)?0:ZE_ARG;
 }
 /* ---- A3 records ---- */
 if(op==Z_ARRAY_LOAD){
   if(n<8u)return ZE_SIZE;
   fmt=zget(p);count=zget(p+4);
   if(!(fmt&ZZ_VF_POS3)||(fmt&~ZZ_VF_ALL)||count<1u||count>ZZ_MAX_RESIDENT_VERTS)return ZE_ARG;
   vb=zvertex_bytes(fmt);
   if(n!=8u+count*vb)return ZE_SIZE;
   return finite_words(p+8u,count*vb/4u)?0:ZE_ARG;
 }
 /* ---- A8.17 raw records ---- */
 if(op==Z_ARRAY_LOAD_CACHED){
   if(n!=8u)return ZE_SIZE;
   if(!zkept_range_ok(zget(p),zget(p+4)))return ZE_ARG;
   return 0;   /* the kept record is checked again when it is loaded */
 }
 if(op==Z_ARRAY_LOAD_RAW){
   zu32 st=zget(p);
   if(n<16u)return ZE_SIZE;
   if(st!=ZZ_VC_NONE&&!zkept_range_ok(st,n-4u))return ZE_ARG;
   p+=4;n-=4;
 }
 if(op==Z_ARRAY_LOAD_RAW||op==Z_DRAW_ARRAYS_RAW){
   zu32 h=op==Z_DRAW_ARRAYS_RAW?16u:12u,flags;const zu8 *q=op==Z_DRAW_ARRAYS_RAW?p+4:p;
   if(n<h)return ZE_SIZE;
   if(op==Z_DRAW_ARRAYS_RAW&&!valid_mode(zget(p)))return ZE_ARG;
   fmt=zget(q);flags=zget(q+4);count=zget(q+8);
   if(!(fmt&ZZ_VF_POS3)||(fmt&~ZZ_VF_RAWALL)||!raw_flags_ok(fmt,flags)||count<1u||count>ZZ_RAW_MAX_VERTS)return ZE_ARG;
   if(op==Z_DRAW_ARRAYS_RAW&&(fmt&ZZ_VF_UV1))return ZE_ARG;
   vb=zraw_vertex_bytes(fmt,flags);
   if(n!=h+count*vb)return ZE_SIZE;
   return raw_finite(p+h,count,fmt,flags)?0:ZE_ARG;
 }
 if(op==Z_DRAW_ELEMENTS_MT){
   if(n<32u)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);
   if(!valid_mode(mode)||count<1u||count>((ZZ_LIMIT-64u)/2u))return ZE_ARG;
   if(zget(p+8)>=ZZ_MAX_TEXTURES||zget(p+20)>=ZZ_MAX_TEXTURES)return ZE_ARG;
   if(n!=32u+((count*2u+3u)&~3u))return ZE_SIZE;
   return 0;
 }
 if(op==Z_DRAW_ELEMENTS_RAW){
   if(n<12u)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);
   if(!valid_mode(mode)||count<1u||count>((ZZ_LIMIT-64u)/2u)||(zget(p+8)&~ZZ_RAW_BE))return ZE_ARG;
   if(n!=12u+((count*2u+3u)&~3u))return ZE_SIZE;
   return 0;
 }
 if(op==Z_COLOR){
   if(n!=16u)return ZE_SIZE;
   return finite_words(p,4u)?0:ZE_ARG;
 }
 if(op==Z_INDEX_KEEP){
   if(n<20u)return ZE_SIZE;
   if(!zkept_range_ok(zget(p),n-4u))return ZE_ARG;
   return zkept_draw_ok(p+4,n-4u)?0:ZE_ARG;
 }
 if(op==Z_INDEX_AGAIN){
   if(n!=8u)return ZE_SIZE;
   if(zget(p+4)<16u||!zkept_range_ok(zget(p),zget(p+4)))return ZE_ARG;
   return 0;   /* the kept record is checked again when it is executed */
 }
 if(op==Z_MT_FLUSH){
   if(n!=20u)return ZE_SIZE;
   return zget(p+8)<ZZ_MAX_TEXTURES?0:ZE_ARG;
 }
 if(op==Z_DRAW_RAW){
   zu32 flags,cst,i,k,be,per;const zu8 *q;
   if(n<12u)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);flags=zget(p+8);
   if(count>ZZ_MAXVERT||count<(mode?3u:1u)||(mode&&mode<4u)||mode>9u||(flags&~(ZZ_RAW_BE|ZZ_RAW_CONST)))return ZE_ARG;   /* A8.23: mode 0, points, from one vertex */
   cst=flags&ZZ_RAW_CONST;be=flags&ZZ_RAW_BE;per=cst?20u:36u;
   if(n!=12u+(cst?16u:0u)+count*per)return ZE_SIZE;
   for(q=p+12,i=0;i<(n-12u)/4u;i++,q+=4){zu32 w=zget(q);if(be)w=zswap32(w);if((w&0x7f800000u)==0x7f800000u)return ZE_ARG;}
   (void)k;return 0;
 }
 if(op==Z_ARRAY_UNLOCK)return n==0u?0:ZE_SIZE;
 if(op==Z_PRESENT_NONE)return n==0u?0:ZE_SIZE;   /* A7 */
 if(op==Z_POINTSIZE){   /* A8.23: a finite size above 0, at most 64 */
   zu32 w;
   if(n!=4u)return ZE_SIZE;
   w=zget(p);   /* positive floats order as their bits: 0x00800000 the smallest normal, 0x42800000 = 64.0 */
   return (w>=0x00800000u&&w<=0x42800000u)?0:ZE_ARG;
 }
 if(op==Z_DRAW_ELEMENTS){
   if(n<8u)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);
   if(!valid_mode(mode)||count<1u||count>((ZZ_LIMIT-32u)/2u))return ZE_ARG;
   if(n!=8u+((count*2u+3u)&~3u))return ZE_SIZE;
   return 0;   /* index range is checked against the resident store at execution */
 }
 if(op==Z_DRAW_ARRAYS){
   if(n<12u)return ZE_SIZE;
   mode=zget(p);fmt=zget(p+4);count=zget(p+8);
   if(!valid_mode(mode)||!(fmt&ZZ_VF_POS3)||(fmt&~ZZ_VF_ALL)||count<1u||count>ZZ_MAX_ARRAY_VERTS)return ZE_ARG;
   vb=zvertex_bytes(fmt);
   if(count>(ZZ_LIMIT-32u)/vb)return ZE_ARG;
   if(n!=12u+count*vb)return ZE_SIZE;
   return finite_words(p+12u,count*vb/4u)?0:ZE_ARG;
 }
 if(op==Z_TEXGEN){
   if(n!=24u)return ZE_SIZE;
   if(zget(p)!=0x2000u&&zget(p)!=0x2001u&&zget(p)!=0x2002u&&zget(p)!=0x2003u)return ZE_ARG;   /* GL_S..GL_Q */
   if(zget(p+4)==0x2500u||zget(p+4)==0x5A5Au)return 0;   /* GL_TEXTURE_GEN_MODE / GL_TEXGEN_MODELVIEW_ZZ: an enum or 0/1, checked by ZZGL */
   if(zget(p+4)!=0x2501u&&zget(p+4)!=0x2502u)return ZE_ARG; /* GL_OBJECT_PLANE / GL_EYE_PLANE */
   return finite_words(p+8u,4u)?0:ZE_ARG;
 }
 if(op==Z_DRAW_N){
   if(n<8u)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);
   if(count>ZZ_MAXVERT||count<1u||!valid_mode(mode))return ZE_ARG;
   if(n!=8u+count*48u)return ZE_SIZE;
   return finite_words(p+8u,count*12u)?0:ZE_ARG;
 }
 if(op==Z_LOADMATRIX||op==Z_MULTMATRIX){
   if(n!=64u)return ZE_SIZE;
   return finite_words(p,16u)?0:ZE_ARG;
 }
 if(op==Z_DELETE_TEX){
   zu32 i;
   if(n<4u)return ZE_SIZE;
   count=zget(p);
   if(count<1u||count>256u||n!=4u+count*4u)return ZE_SIZE;
   for(i=0;i<count;i++)if(zget(p+4u+4u*i)>=ZZ_MAX_TEXTURES)return ZE_ARG;
   return 0;
 }
 if(!op || op>=sizeof words)return ZE_OP;
 if(op==Z_IMAGE || op==Z_SUBIMAGE){
   if(n<24)return ZE_SIZE;
   w=zget(p);h=zget(p+4);bpp=zget(p+8);
   if(!w||!h||w>256||h>256||(bpp!=1&&bpp!=3&&bpp!=4&&bpp!=ZZ_TEX_ARGB4444_LE&&bpp!=ZZ_TEX_ARGB4444_BE))return ZE_ARG;   /* A8: 1 = GL_LUMINANCE */
   if(bpp>4u)bpp=2u;   /* A8.17: ARGB4444 words */
   if(op==Z_IMAGE && ((w&(w-1)) || (h&(h-1))))return ZE_ARG;
   if(zget(p+12)>256u-w||zget(p+16)>256u-h)return ZE_ARG;
   if(op==Z_IMAGE && (zget(p+12)||zget(p+16)))return ZE_ARG;
   if(zget(p+20)!=w*h*bpp)return ZE_SIZE;
   return n==24+((w*h*bpp+3u)&~3u)?0:ZE_SIZE;
 }
 if(op==Z_DRAW){
   if(n<8)return ZE_SIZE;
   mode=zget(p);count=zget(p+4);
   if(count>ZZ_MAXVERT||count<(mode?3u:1u)||(mode&&mode<4)||mode>9)return ZE_ARG;   /* A8.23: mode 0, points */
   if(n!=8+count*36u)return ZE_SIZE;
   return finite_words(p+8,count*9u)?0:ZE_ARG;
 }
 if(n!=words[op]*4u)return ZE_SIZE;
 switch(op){
 case Z_CLEARCOLOR:case Z_MOVE:case Z_ROT:case Z_SCALE:case Z_FRUSTUM:case Z_ORTHO:case Z_DEPTHRANGE:case Z_CLEARD:
   if(!finite_words(p,n/4))return ZE_ARG;break;
 case Z_ALPHA:if(!finite_words(p+4,1))return ZE_ARG;break;
 case Z_SCISSOR:
   if(zget(p)>800 || zget(p+4)>600 || zget(p+8)>800 || zget(p+12)>600)return ZE_ARG;   /* A8.26: 800x600 the largest; ZZGL clamps to the buffer */
   break;
 }
 if(op==Z_BIND && zget(p)>=ZZ_MAX_TEXTURES)return ZE_ARG;
 if(op==Z_VIEW && (zget(p)>800u||zget(p+4)>600u||zget(p+8)>800u-zget(p)||zget(p+12)>600u-zget(p+4)))return ZE_ARG;   /* A8.26 */
 return 0;
}
/* A8.8: the record a refused packet stopped at (Core1 publishes it with the error) */
zu32 zz_wire_fail_op,zz_wire_fail_at,zz_wire_fail_word;
int zpacket(const zu8 *s,zu32 n,zu32 seq,zz_visit visit,void *ctx){
 zu32 at,op,len,pass;int rc;
 zz_wire_fail_op=zz_wire_fail_at=zz_wire_fail_word=0;
 if(!s||n<16||n>ZZ_LIMIT||(n&3))return ZE_SIZE;
 if(zget(s)!=ZZ_MAGIC||zget(s+4)!=ZZ_PROTO||zget(s+12)!=n)return ZE_HEADER;
 if(!seq||zget(s+8)!=seq)return ZE_SEQ;
 for(pass=0;pass<2;pass++){
   for(at=16;at<n;at+=len){
     if(n-at<8)return ZE_SIZE;
     op=zget(s+at);len=zget(s+at+4);
     if(len<8||len>n-at||(len&3))return ZE_SIZE;
     if((op==Z_PRESENT||op==Z_PRESENT_DIRECT||op==Z_PRESENT_NONE||op==Z_SHOW||op==Z_STOP||op==Z_FINISH)&&at+len!=n)return ZE_ARG;
     rc=pass?(visit?visit(op,s+at+8,ctx):0):extent(op,s+at+8,len-8);
     if(rc){zz_wire_fail_op=op|(pass<<16)|(len<<20);zz_wire_fail_at=at;zz_wire_fail_word=len>=12?zget(s+at+8):0;return rc;}
   }
 }
 return 0;
}
