/* Konami K054539 Core1 bare metal, modeled after libvgm/MAME
 * Ref: ValleyBell/libvgm/emu/cores/k054539.c (BSD-3-Clause, Olivier Galibert).
 * No dynamic memory except ROM allocated from validated arcade arena.
 * Voice modes 8-bit PCM / little-endian 16-bit PCM / nibble DPCM,
 * 8 voices, key-on start position latch, looping, reverse, 32K reverb RAM,
 * integer PCM inner loop, linear resampling at clock/384 -> AHI rate.
 */
#include "k054539_zz.h"
#include <math.h>
#include <string.h>

static int16_t sat16(int32_t v){return (int16_t)(v>32767?32767:(v<(-32768)?(-32768):v));}
static uint32_t addr24(const uint8_t *r){return (uint32_t)r[0]|((uint32_t)r[1]<<8)|((uint32_t)r[2]<<16);}
static uint8_t sample8(const zz_k054539_t *s,uint32_t p){
 if(!s->rom)return 0xff;
 p &= s->rom_mask;
 return p<s->rom_size?s->rom[p]:0xff;
}
static int regupdate(const zz_k054539_t *s){return !(s->regs[0x22f]&0x80u);}
static void keyoff(zz_k054539_t *s,unsigned ch){if(regupdate(s))s->regs[0x22c]&=(uint8_t)~(1u<<ch);}
static void keyon(zz_k054539_t *s,unsigned ch){
 if(regupdate(s)){
  s->regs[0x22c]|=(uint8_t)(1u<<ch);
  s->voice[ch].pos=0xffffffffu; /* next render resets accumulator */
 }
}
void zz_k054539_init(zz_k054539_t *s,uint32_t clock,uint32_t rate){
 unsigned i;
 memset(s,0,sizeof(*s));
 s->clock=clock?clock:18432000u;
 s->rate=rate?rate:44100u;
 /* Official libvgm table: vol 0=0 dB, 0x40=-36 dB;
  * reserve 4-voice normalization headroom, constant-power pan. */
 for(i=0;i<256;i++){
  double x=8192.0*pow(10.0,(-36.0*(double)i/64.0)/20.0);
  s->vol_q15[i]=(uint16_t)(x+0.5);
 }
 for(i=0;i<15;i++)s->pan_q15[i]=(uint16_t)(32767.0*sqrt((double)i/14.0)+0.5);
 for(i=0;i<8;i++)s->voice[i].pos=0xffffffffu;
}
int zz_k054539_rom(zz_k054539_t *s,uint32_t total,uint32_t offs,
                   const uint8_t *data,uint32_t len,
                   uint8_t *(*allocate)(uint32_t,uint8_t)){
 uint32_t m;
 if(!s||!data||!allocate||!total||total>ZZ_K054539_ROM_MAX||offs>total||len>total-offs)return 0;
 if(!s->rom){
  s->rom=allocate(total,0xff);if(!s->rom)return 0;
  s->rom_size=total;
  for(m=1;m<total;m<<=1){}s->rom_mask=m-1;
 }else if(s->rom_size!=total)return 0;
 memcpy(s->rom+offs,data,len);
 return 1;
}
void zz_k054539_write(zz_k054539_t *s,uint16_t off,uint8_t data){
 unsigned ch;
 int latch;
 if(off>=0x230u)return;
 latch=(s->regs[0x22f]&1u)!=0;
 if(latch && off<0x100u && ((off&31u)>=0x0cu) && ((off&31u)<=0x0eu)){
  s->latch[off>>5][(off&31u)-0x0cu]=data;
  return;
 }
 switch(off){
  case 0x214u:
   for(ch=0;ch<8;ch++)if(data&(1u<<ch)){
    if(latch){
     uint8_t *r=s->regs+(ch<<5)+0x0c;
     memcpy(r,s->latch[ch],3);
    }
    keyon(s,ch);
   }
   break;
  case 0x215u:for(ch=0;ch<8;ch++)if(data&(1u<<ch))keyoff(s,ch);break;
  case 0x22du:
   if(s->rom_addr==0x80u){
    uint32_t a=(s->cur_ptr&0x3fffu)|((s->cur_ptr&0x10000u)>>2);
    ((uint8_t*)s->revram)[a]=data;
   }
   s->cur_ptr=(s->cur_ptr+1u)&0x1ffffu;
   break;
  case 0x22eu:s->rom_addr=data;s->cur_ptr=0;break;
  default:break;
 }
 s->regs[off]=data;
}
/* Signed 16-bit reverb memory matches MAME, with wraparound writes. */
static void revadd(zz_k054539_t *s,uint32_t i,int32_t value){
 uint16_t u=(uint16_t)s->revram[i&0x1fffu];
 s->revram[i&0x1fffu]=(int16_t)(uint16_t)(u+(uint16_t)value);
}
static void native_frame(zz_k054539_t *s,int32_t *left,int32_t *right){
 static const int16_t dpcm[16]={0,256,512,1024,2048,4096,8192,16384,0,-16384,-8192,-4096,-2048,-1024,-512,-256};
 int32_t l=s->revram[s->revpos],r=l;
 unsigned ch;
 s->revram[s->revpos]=0;
 if(!s->rom||!(s->regs[0x22fu]&1u)){
  s->revpos=(s->revpos+1u)&0x1fffu;
  *left=0;*right=0;return;
 }
 for(ch=0;ch<8;ch++)if(s->regs[0x22cu]&(1u<<ch)){
  uint8_t *a=s->regs+0x20*ch,*b=s->regs+0x200+2*ch;
  zz_k054539_voice_t *v=&s->voice[ch];
  int32_t delta=(int32_t)addr24(a),fract,value,prev;
  int32_t incr=(b[0]&0x20u)?-1:1;
  uint32_t pos=addr24(a+0x0c);
  int32_t pan=(int32_t)a[5],revvol=(int32_t)a[3]+a[4],panl,panr;
  uint32_t delay=(uint32_t)((a[6]|((uint32_t)a[7]<<8))>>3);
  int32_t gl,gr,gv;
  unsigned limit=0;
  if(revvol>255)revvol=255;
  if(pan>=0x81&&pan<=0x8f)pan-=0x81;
  else if(pan>=0x11&&pan<=0x1f)pan-=0x11;
  else pan=7;
  panl=s->pan_q15[pan];panr=s->pan_q15[14-pan];
  gl=((int32_t)s->vol_q15[a[3]]*panl)>>15;
  gr=((int32_t)s->vol_q15[a[3]]*panr)>>15;
  gv=s->vol_q15[revvol]>>1;
  if(pos!=v->pos){fract=0;value=0;prev=0;}else{fract=v->frac;value=v->value;prev=v->prev;}
  if(incr<0)delta=-delta;
  switch(b[0]&0x0cu){
   case 0u: /* signed PCM 8 bit, 0x80 terminates */
   case 4u:{ /* signed PCM 16 bit, 0x8000 terminates */
    int32_t stride=(b[0]&4u)?2:1;
    fract+=delta;
    while((fract<0||fract>=0x10000)&&limit++<512u){
     int16_t sample;
     fract+=(incr<0)?0x10000:-0x10000;
     pos+=(uint32_t)(incr*stride);
     prev=value;
     if(stride==1)sample=(int16_t)((uint16_t)sample8(s,pos)<<8);
     else sample=(int16_t)((uint16_t)sample8(s,pos)|((uint16_t)sample8(s,pos+1u)<<8));
     if(sample==(int16_t)0x8000 && (b[1]&1u)){
      pos=addr24(a+8);
      if(stride==1)sample=(int16_t)((uint16_t)sample8(s,pos)<<8);
      else sample=(int16_t)((uint16_t)sample8(s,pos)|((uint16_t)sample8(s,pos+1u)<<8));
     }
     if(sample==(int16_t)0x8000){keyoff(s,ch);value=0;break;}
     value=sample;
    }
    break;
   }
   case 8u:{ /* signed accumulating 4-bit DPCM */
    uint32_t nibpos=pos<<1;
    fract<<=1;
    if(fract&0x10000){fract&=0xffff;nibpos|=1u;}
    fract+=delta;
    while((fract<0||fract>=0x10000)&&limit++<512u){
     uint8_t by;
     int32_t vv;
     fract+=(incr<0)?0x10000:-0x10000;
     nibpos+=(uint32_t)incr;
     prev=value;
     by=sample8(s,nibpos>>1);
     if(by==0x88u&&(b[1]&1u)){
      nibpos=addr24(a+8)<<1;
      by=sample8(s,nibpos>>1);
     }
     if(by==0x88u){keyoff(s,ch);value=0;break;}
     vv=(int32_t)prev+dpcm[(nibpos&1u)?(by>>4):(by&0xfu)];
     value=vv>32767?32767:(vv<(-32768)?(-32768):vv);
    }
    fract>>=1;
    if(nibpos&1u)fract|=0x8000;
    pos=nibpos>>1;
    break;
   }
   default:keyoff(s,ch);break;
  }
  l+=(int32_t)(((int64_t)value*gl)>>15);
  r+=(int32_t)(((int64_t)value*gr)>>15);
  revadd(s,(delay+((uint32_t)s->revpos<<1))&0x1fffu,(int32_t)(((int64_t)value*gv)>>15));
  v->pos=pos;v->frac=fract;v->value=value;v->prev=prev;
  if(regupdate(s)){a[0x0c]=(uint8_t)pos;a[0x0d]=(uint8_t)(pos>>8);a[0x0e]=(uint8_t)(pos>>16);}
 }
 s->revpos=(s->revpos+1u)&0x1fffu;
 *left=l;*right=r;
}
void zz_k054539_render(zz_k054539_t *s,int16_t *out,uint32_t frames){
 uint32_t i;
 uint64_t denom=(uint64_t)s->rate*384u;
 if(!denom)denom=44100u*384u;
 for(i=0;i<frames;i++){
  int64_t mixl,mixr;
  uint32_t frac;
  if(!s->primed){
   native_frame(s,&s->next_l,&s->next_r);
   s->prev_l=s->next_l;s->prev_r=s->next_r;s->primed=1u;
  }
  s->resamp_phase+=(uint64_t)s->clock;
  while(s->resamp_phase>=denom){
   s->resamp_phase-=denom;
   s->prev_l=s->next_l;s->prev_r=s->next_r;
   native_frame(s,&s->next_l,&s->next_r);
  }
  frac=(uint32_t)((s->resamp_phase<<16)/denom);
  mixl=(int64_t)s->prev_l+(((int64_t)(s->next_l-s->prev_l)*frac)>>16);
  mixr=(int64_t)s->prev_r+(((int64_t)(s->next_r-s->prev_r)*frac)>>16);
  out[2*i]=sat16((int32_t)mixl);out[2*i+1]=sat16((int32_t)mixr);
 }
}
