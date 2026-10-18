/* Konami 053260 KDSC engine. Derived behavioral model from
   MAME/libvgm work of Ernesto Corvi & Alex W. Jackson, BSD-3-Clause.
   Copyright (c) 2026 ZZVGMPlay contributors (this integration).
   Preserves KDSC pre-increment start behavior and 4-bit delta codec. */
#include "k053260_zz.h"
#include <stddef.h>
#include <string.h>
/* A06b: VGM data-block ROM size is the *declared* ROM size. Some
   authentic K053260 VGMs (TMNT Pizza Power) declare 4 MiB even though
   the KDSC chip itself addresses only 21 bits (2 MiB). Never reject a
   valid lower 2 MiB ROM chunk solely because of the declared size.
   Keep the declaration for consistency checks, but allocate and address
   only the reachable memory range. Unreachable blocks fail explicitly. */
#define K053260_VGM_ROM_DECL_MAX (4u*1024u*1024u)
static const int16_t pan[8][2]={{0,0},{127,0},{116,52},{104,73},{90,90},
                                   {73,104},{52,116},{0,127}};
static const int8_t kadpcm[16]={0,1,2,4,8,16,32,64,-128,-64,-32,-16,-8,-4,-2,-1};
static int16_t clip16(int32_t x){return x>32767?32767:(x< -32768?-32768:(int16_t)x);}
static void update_pan(zz_k053260_voice_t *v){
 v->pan_gain[0]=(int32_t)v->volume*pan[v->pan&7u][0];
 v->pan_gain[1]=(int32_t)v->volume*pan[v->pan&7u][1];
}
void zz_k053260_init(zz_k053260_t *c,uint32_t clock,uint32_t rate){
 unsigned i;memset(c,0,sizeof(*c));c->clock=clock;c->rate=rate?rate:44100u;
 for(i=0;i<4;i++)update_pan(&c->voice[i]);
}
int zz_k053260_rom(zz_k053260_t *c,uint32_t rom_size,uint32_t offset,
                   const uint8_t *data,uint32_t size,
                   uint8_t *(*alloc_rom)(uint32_t size,uint8_t fill)){
 unsigned i;uint32_t mask,physical_size;
 if(!c||!data||!alloc_rom||!rom_size||rom_size>K053260_VGM_ROM_DECL_MAX||
    offset>rom_size||size>rom_size-offset)return 0;
 physical_size=(rom_size>ZZ_K053260_ROM_MAX)?ZZ_K053260_ROM_MAX:rom_size;
 /* Reject writes beyond the 21-bit address bus; do not wrap or overflow. */
 if(offset>physical_size||size>physical_size-offset)return 0;
 if(!c->rom){
  c->rom=alloc_rom(physical_size,0xffu);if(!c->rom)return 0;
  c->rom_size=rom_size; /* nominal VGM ROM size, may be 4 MiB */
  /* The read mask is based on the physically accessible region, never
     on a 4 MiB nominal ROM size. */
  mask=1;while(mask<physical_size)mask<<=1;c->rom_mask=mask-1;
 }else if(c->rom_size!=rom_size)return 0;
 for(i=0;i<size;i++)c->rom[offset+i]=data[i];
 return 1;
}
static uint8_t rom_read(const zz_k053260_t *c,uint32_t addr){
 uint32_t a=addr&c->rom_mask;
 return(c->rom&&a<c->rom_size)?c->rom[a]:0xffu;
}
void zz_k053260_write(zz_k053260_t *c,uint8_t address,uint8_t value){
 unsigned i;address&=0x3fu;
 if(address>=0x08u&&address<=0x27u){
  zz_k053260_voice_t *v=&c->voice[(address-8u)/8u];
  switch((address-8u)&7u){
   case 0:v->pitch=(v->pitch&0xf00u)|value;break;
   case 1:v->pitch=(v->pitch&0x0ffu)|((uint16_t)(value&15u)<<8);break;
   case 2:v->length=(v->length&0xff00u)|value;break;
   case 3:v->length=(v->length&0x00ffu)|((uint16_t)value<<8);break;
   case 4:v->start=(v->start&0x1fff00u)|value;break;
   case 5:v->start=(v->start&0x1f00ffu)|((uint32_t)value<<8);break;
   case 6:v->start=(v->start&0x00ffffu)|((uint32_t)(value&31u)<<16);break;
   case 7:v->volume=value&0x7fu;update_pan(v);break;
  }return;
 }
 switch(address){
 case 2:case 3:c->ports[address]=value;break;
 case 0x28:{uint8_t rising=(uint8_t)(value&~c->keyon);
  for(i=0;i<4;i++){
   zz_k053260_voice_t *v=&c->voice[i];v->reverse=(value>>(i+4))&1u;
   if(rising&(1u<<i)){
    v->pos=v->adpcm?1u:0u;v->counter=0xffffu;v->output=0;v->playing=1;
   }else if(!(value&(1u<<i))){v->pos=0;v->output=0;v->playing=0;}
  }c->keyon=value;break;
 }
 case 0x2a:for(i=0;i<4;i++){
  c->voice[i].loop=(value>>i)&1u;c->voice[i].adpcm=(value>>(i+4))&1u;
 }break;
 case 0x2c:case 0x2d:{unsigned v=(address==0x2cu)?0u:2u;
  c->voice[v].pan=value&7u;update_pan(&c->voice[v]);
  c->voice[v+1].pan=(value>>3)&7u;update_pan(&c->voice[v+1]);break;
 }
 case 0x2f:c->mode=value;break;
 default:break;
 }
}
/* Cycle counter is fed master-clock cycles per 44.1 kHz output frame.
   Model is the reference's 12-bit upcounter: sample when counter>=4096,
   subtract 4096 and add 12-bit pitch, with preincrement. */
static void play_voice(zz_k053260_t *c,zz_k053260_voice_t *v,int32_t *l,int32_t *r,
                       uint32_t cycles){
 uint32_t count=(v->counter==0xffffu)?0x1000u:(uint32_t)v->counter+cycles;
 unsigned guard=0;
 while(count>=0x1000u&&guard++<4096u){
  uint32_t bytepos,addr;uint8_t s;
  count=count-0x1000u+(uint32_t)v->pitch;
  bytepos=(++v->pos)>>(v->adpcm?1u:0u);
  if(bytepos>v->length){
   if(v->loop){v->pos=0;v->output=0;bytepos=0;}
   else{v->playing=0;v->counter=(uint16_t)count;return;}
  }
  addr=v->start+(v->reverse?(0u-bytepos):bytepos);
  s=rom_read(c,addr);
  if(v->adpcm){
   if(v->pos&1u)s>>=4;
   v->output=(int8_t)((int32_t)v->output+kadpcm[s&15u]);
  }else v->output=(int8_t)s;
 }
 v->counter=(uint16_t)count;
 *l+=((int32_t)v->output*v->pan_gain[0])>>7;
 *r+=((int32_t)v->output*v->pan_gain[1])>>7;
}
void zz_k053260_render(zz_k053260_t *c,int16_t *pcm,uint32_t frames){
 uint32_t i;unsigned j;
 for(i=0;i<frames;i++){
  int32_t l=0,r=0;
  c->cycle_frac+=(uint64_t)c->clock;
  /* clock is 3.579 MHz typically.  Keep fractional clock/rate over output frames. */
  {
   uint32_t cycles=(uint32_t)(c->cycle_frac/c->rate);
   c->cycle_frac-=(uint64_t)cycles*c->rate;
   if((c->mode&2u)&&c->rom){
    for(j=0;j<4;j++)if(c->voice[j].playing)play_voice(c,&c->voice[j],&l,&r,cycles);
   }
  }
  /* A06c: restore the KDSC output level, previously divided by four.
     The K053260 pan table above is a 7-bit approximation to MAME's 1.16
     coefficients. This output is intentionally ~6 dB below the full MAME
     core scale, preserving headroom for mixed YM2151 + K053260 VGMs.
     No non-linear limiter, no change to other chip volumes. */
  pcm[2*i]=clip16(l);pcm[2*i+1]=clip16(r);
 }
}
