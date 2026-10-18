/* ZZVGMPlay A05b Apple IIgs Ensoniq ES5503 DOC backend.
 * Independent C implementation cross-checked with R. Belmont's
 * ES5503 sound device (MAME 2.4.1) and libvgm, BSD-3-Clause.
 * See REFERENCE_LICENSE.txt in the patch.
 *
 * FIXES since A05 + LEVEL FIX1:
 *  1) MAME's (wtsize-1) phase-wrap semantics (Arkanoid tuning).
 *  2) Entirely linear DOC output gain, no hidden soft limiter.
 *  3) DOC-native to 44.1 kHz linear interpolation (not ZOH), plus
 *     a cheap 2-pole low-pass when the DOC rate exceeds AHI rate.
 *  4) Highest enabled oscillator x3, SWAP / CPU halt / SYNC-AM / IRQ
 *     semantics to the extent exposed by VGM write-only playback.
 * Hardware DOC output is still 8-bit wave samples, no fake dithering.
 */
#include <stdint.h>
#include <string.h>
#include "es5503_zz.h"

static const uint32_t wtmask[8]={0x1ff00u,0x1fe00u,0x1fc00u,0x1f800u,
                                 0x1f000u,0x1e000u,0x1c000u,0x18000u};
static const uint8_t resshift[8]={9,10,11,12,13,14,15,16};

/* Linear headroom: gain x(1/6) internally; main mixer x2 for single ES5503.
 * One max-volume voice boosted x3 on the final active DOC oscillator gives
 * <= 127*255*3/6*2 = 32385, just below signed 16-bit maximum.
 * Multiple simultaneous coherent voices can still clip downstream; this
 * intentionally does NOT dynamically modify waveform shape or compress.
 * Keep this divisor explicit for relative listening calibration.
 */
#define ZZ_ES_DOC_GAIN_DIVISOR 6

static int16_t clamp_s16(int32_t x){
 if(x>32767)return 32767;
 if(x<(-32767-1))return (-32767-1);
 return (int16_t)x;
}
static void recalc(zz_es5503_t *c){
 uint32_t new_rate=(c->clock/8u)/(c->enabled+2u);
 c->output_rate=new_rate?new_rate:1u;
 /* Pre-filter only when downsampling. Two inexpensive first-order stages.
  * This is not a brick-wall band-limit filter; it suppresses aliasing better
  * than ZOH / pure sample skipping. Fixed Q12 coefficient, computed rarely.
  */
 if(c->output_rate>c->rate){
  uint32_t alpha=(c->rate*2u*4096u)/c->output_rate;
  c->lp_alpha=(uint16_t)(alpha>4096u?4096u:(alpha?alpha:1u));
 }else c->lp_alpha=4096u;
}
void zz_es5503_init(zz_es5503_t *c,uint32_t clock,uint32_t rate,uint8_t outputs){
 unsigned i;
 memset(c,0,sizeof(*c));
 c->clock=clock?clock:7159090u;
 c->rate=rate?rate:44100u;
 c->outputs=(outputs>=1u&&outputs<=8u)?outputs:2u;
 c->enabled=1u;
 for(i=0;i<32u;i++){c->osc[i].control=1u;c->osc[i].data=0x80u;}
 recalc(c);
}

/* MAME halt_osc logic: type=1 for zero wave sample; type=0 for wave end
 * or explicit CPU halt. Passing new_ctrl prevents spurious IRQ on CPU halt.
 */
static void halt_voice(zz_es5503_t *c,unsigned index,unsigned type,
                       unsigned phase_shift,uint8_t new_ctrl){
 zz_es_osc_t *v=&c->osc[index],*partner=&c->osc[index^1u];
 unsigned mode=(v->control>>1u)&3u,partner_mode=(partner->control>>1u)&3u;
 uint32_t wave_size=256u<<v->wtsize;
 if(mode==2u){
  /* Historical MAME sync implementation: even oscillator can restart the
   * preceding odd oscillator; index 0 has no such partner. */
  if(!(index&1u)&&index>0u&&!(c->osc[index-1u].control&1u))
   c->osc[index-1u].accum=0;
  mode=0u;
 }
 if(mode || type)v->control|=1u;
 else v->accum-=((wave_size-1u)<<phase_shift);
 if(mode==3u){
  partner->control&=(uint8_t)~1u;
  partner->accum=0;
 }else if(partner_mode==3u && !(index&1u)){
  v->control&=(uint8_t)~1u;
  v->accum-=((wave_size-1u)<<phase_shift);
 }
 if((v->control&8u) && (type!=0u || (new_ctrl&8u))) v->irq=1;
}

void zz_es5503_write(zz_es5503_t *c,uint16_t reg,uint8_t data){
 zz_es_osc_t *v;unsigned idx;uint8_t old;
 if(reg>0xe2u)return;
 if(reg==0xe1u){
  c->enabled=(uint8_t)(((data>>1u)&31u)+1u);
  recalc(c);
  return;
 }
 if(reg>=0xe0u)return;
 idx=reg&31u;v=&c->osc[idx];
 switch(reg&0xe0u){
 case 0x00u:v->freq=(uint16_t)((v->freq&0xff00u)|data);break;
 case 0x20u:v->freq=(uint16_t)((v->freq&0xffu)|((uint16_t)data<<8));break;
 case 0x40u:v->vol=data;break;
 case 0x60u:break;
 case 0x80u:v->ptr=data;break;
 case 0xa0u:
  old=v->control;
  if((old&1u) && !(data&1u))v->accum=0u;
  /* MAME 2.4: CPU-driven halt in 1-shot/SWAP must also execute
   * the stop/start pairing logic (and respects new IRQ enable bit).
   */
  if(!(old&1u) && (data&1u) && (data&2u))
   halt_voice(c,idx,0u,(unsigned)resshift[v->resolution],data);
  v->control=data;
  break;
 case 0xc0u:
  v->bank=(data&0x40u)?1u:0u;
  v->wtsize=(uint8_t)((data>>3u)&7u);
  v->resolution=data&7u;
  break;
 default:break;
 }
}
int zz_es5503_ram(zz_es5503_t *c,uint32_t off,const uint8_t *data,uint32_t size){
 if(!c || (!data&&size) || off>ZZ_ES5503_RAM_SIZE || size>ZZ_ES5503_RAM_SIZE-off)
  return 0;
 if(size)memcpy(c->ram+off,data,size);
 return 1;
}

static void doc_tick(zz_es5503_t *c){
 int32_t left=0,right=0;
 unsigned i;
 for(i=0;i<c->enabled;i++){
  zz_es_osc_t *v=&c->osc[i];
  uint32_t base,position,index,wave_size;
  unsigned shift,mode,route;
  uint8_t sample;
  int32_t val;
  if(v->control&1u)continue;
  base=(((uint32_t)v->ptr<<8u)|(v->bank?0x10000u:0u))&wtmask[v->wtsize];
  wave_size=256u<<v->wtsize;
  shift=(unsigned)resshift[v->resolution]-(unsigned)v->wtsize;
  position=v->accum>>shift;
  index=base+(position&(wave_size-1u));
  sample=c->ram[index&0x1ffffu];
  v->data=sample;
  v->accum+=v->freq;
  if(!sample){
   halt_voice(c,i,1u,shift,v->control);
   continue;
  }
  mode=(v->control>>1u)&3u;
  if(mode==2u&&(i&1u)){
   /* SYNC/AM odd voice acts as volume source for next voice. */
   if(i<31u && !(c->osc[i+1u].control&1u))c->osc[i+1u].vol=sample;
  }else{
   val=((int32_t)sample-128)*(int32_t)v->vol;
   /* Real DOC / MAME 2.3.1: last enabled oscillator is 3x louder. */
   if(i==(unsigned)c->enabled-1u)val*=3;
   route=(v->control>>4u)&15u;
   if(c->outputs==1u){
    left+=(val*181)/256;
    right+=(val*181)/256;
   }else{
    /* VGM DOC channel count is normally 2 (or 1/4/8). MAME masks
     * the channel for powers of two; modulo works for 1..8 as well. */
    route%=c->outputs;
    if(route&1u)right+=val;else left+=val;
   }
  }
  /* MAME's end-of-wave test uses (wave_size-1), not wave_size.
   * This preserves fractional phase through loops, fixes Arkanoid tuning. */
  if(position >= wave_size-1u)
   halt_voice(c,i,0u,shift,v->control);
 }
 /* No compressor/soft-knee. Only a documented fixed linear attenuation. */
 c->last_l=clamp_s16(left/ZZ_ES_DOC_GAIN_DIVISOR);
 c->last_r=clamp_s16(right/ZZ_ES_DOC_GAIN_DIVISOR);
 if(c->lp_alpha!=4096u){
  /* Two Q2 IIR stages for downsampling, avoiding 32-bit overflow. */
  int32_t target_l=(int32_t)c->last_l*4;
  int32_t target_r=(int32_t)c->last_r*4;
  c->lp1_l+=((target_l-c->lp1_l)*(int32_t)c->lp_alpha)/4096;
  c->lp1_r+=((target_r-c->lp1_r)*(int32_t)c->lp_alpha)/4096;
  c->lp2_l+=((c->lp1_l-c->lp2_l)*(int32_t)c->lp_alpha)/4096;
  c->lp2_r+=((c->lp1_r-c->lp2_r)*(int32_t)c->lp_alpha)/4096;
  c->last_l=clamp_s16(c->lp2_l/4);
  c->last_r=clamp_s16(c->lp2_r/4);
 }else{
  c->lp1_l=(int32_t)c->last_l*4;
  c->lp1_r=(int32_t)c->last_r*4;
  c->lp2_l=c->lp1_l;
  c->lp2_r=c->lp1_r;
 }
}

void zz_es5503_render(zz_es5503_t *c,int16_t *out,uint32_t n){
 uint32_t i;
 if(!c||!out)return;
 if(!c->primed){
  doc_tick(c);
  c->prev_l=c->last_l;c->prev_r=c->last_r;
  c->primed=1u;
 }
 for(i=0;i<n;i++){
  /* First-order interpolation with a one-DOC-tick group delay.
   * Q12 interpolation keeps all operations 32-bit on Cortex-A9.
   */
  uint32_t fraction=(c->accum_rate<<12u)/c->rate;
  int32_t dl=(int32_t)c->last_l-(int32_t)c->prev_l;
  int32_t dr=(int32_t)c->last_r-(int32_t)c->prev_r;
  out[2u*i]=clamp_s16((int32_t)c->prev_l+(dl*(int32_t)fraction)/4096);
  out[2u*i+1u]=clamp_s16((int32_t)c->prev_r+(dr*(int32_t)fraction)/4096);
  c->accum_rate+=c->output_rate;
  if(c->accum_rate>=c->rate){
   uint32_t ticks=c->accum_rate/c->rate;
   c->accum_rate%=c->rate;
   /* Corrupt clocks: discard excessive DOC ticks rather than letting the
    * fractional phase remain invalid (and overrun subsequent frames). */
   if(ticks>32u)ticks=32u;
   while(ticks--){
    c->prev_l=c->last_l;c->prev_r=c->last_r;
    doc_tick(c);
   }
  }
 }
}
