/*
 * AY-3-8910/YM2149 for ZZVGMPlay, adapted from:
 * emu2149 v1.42, https://github.com/digital-sound-antiques/emu2149
 * Copyright (C) 2001-2022 Mitsutaka Okazaki. MIT license, see
 * LICENSE_EMU2149_MIT.txt included alongside this adaptation.
 *
 * Reference semantics preserved: volume tables, register masks, envelope,
 * tone and noise LFSR. The output runs at chipclock/8 (pin26 permitting);
 * it is converted to 44.1kHz by an area-weighted linear-down resampler,
 * analogous to libvgm's Resmpl_Exec_LinearDown. No per-channel fake stereo.
 * No heap, no dynamic allocation and no floating point.
 */
#include "ay2149_ref.h"

static const uint8_t voltbl[2][32] = {
  {0x00,0x01,0x01,0x02,0x02,0x03,0x03,0x04,0x05,0x06,0x07,0x09,
   0x0b,0x0d,0x0f,0x12,0x16,0x1a,0x1f,0x25,0x2d,0x35,0x3f,0x4c,
   0x5a,0x6a,0x7f,0x97,0xb4,0xd6,0xff,0xff},
  {0x00,0x00,0x03,0x03,0x04,0x04,0x06,0x06,0x09,0x09,0x0d,0x0d,
   0x12,0x12,0x1d,0x1d,0x22,0x22,0x37,0x37,0x4d,0x4d,0x62,0x62,
   0x82,0x82,0xa6,0xa6,0xd0,0xd0,0xff,0xff}
};
static const uint8_t regmsk[16] =
 {0xff,0x0f,0xff,0x0f,0xff,0x0f,0x1f,0x3f,
  0x1f,0x1f,0x1f,0xff,0xff,0x0f,0xff,0xff};

void zz_ay2149_reset(zz_ay2149_t *p,uint32_t clock,uint32_t rate,uint8_t type,uint8_t flags){
    __builtin_memset(p,0,sizeof(*p));
    /* libvgm: chipType 0x10-0x13 and 0x20-0x22 mean YM2149-derived.
     * YM2149 pin26 low halves the master clock before /8. */
    p->is_ym=(type>=0x10u && type<=0x22u)?1u:0u;
    if(p->is_ym && (flags&0x10u))clock/=2u;
    p->native_rate=clock/8u;
    p->output_rate=rate?rate:44100u;
    if(!p->native_rate)p->native_rate=p->output_rate;
    p->noise_seed=0xffffu;
    p->env_pause=1u;
}
void zz_ay2149_write(zz_ay2149_t *p,uint8_t reg,uint8_t data){
    unsigned c;
    if(reg>15u)return;
    data &=regmsk[reg];p->reg[reg]=data;
    switch(reg){
      case 0:case 1:case 2:case 3:case 4:case 5:
        c=reg>>1;
        p->freq[c]=(uint16_t)(((p->reg[c*2+1]&15u)<<8)|p->reg[c*2]);
        break;
      case 6:p->noise_freq=data&31u;break;
      case 7:
        p->tmask[0]=data&1u;p->tmask[1]=data&2u;p->tmask[2]=data&4u;
        p->nmask[0]=data&8u;p->nmask[1]=data&16u;p->nmask[2]=data&32u;
        break;
      case 8:case 9:case 10:p->volume[reg-8]=(uint8_t)(data<<1);break;
      case 11:case 12:p->env_freq=(uint32_t)((p->reg[12]<<8)|p->reg[11]);break;
      case 13:
        p->env_continue=(data>>3)&1u;
        p->env_alternate=(data>>1)&1u;
        p->env_hold=data&1u;
        p->env_face=(data>>2)&1u;
        p->env_pause=0;
        p->env_ptr=p->env_face?0u:0x1fu;
        break;
      default:break;
    }
}
/* Matches emu2149 v1.42 update_output when incr == 1. Because this
 * function is called at chipclk/8, each invocation is one PSG tick.
 * A modulo counter in the high-rate domain avoids the dropped noise
 * transitions of the previous 44.1kHz AY backend.
 */
static int16_t ay_tick(zz_ay2149_t *p){
    unsigned i,noise;
    const uint8_t *vt=voltbl[p->is_ym?0:1];
    int32_t sum=0;
    p->env_count++;
    if(p->env_count>=p->env_freq){
        if(!p->env_pause){
            if(p->env_face)p->env_ptr=(p->env_ptr+1u)&0x3fu;
            else p->env_ptr=(p->env_ptr+0x3fu)&0x3fu;
        }
        if(p->env_ptr&0x20u){
            if(p->env_continue){
                if(p->env_alternate^p->env_hold)p->env_face^=1u;
                if(p->env_hold)p->env_pause=1;
                p->env_ptr=p->env_face?0u:0x1fu;
            }else{p->env_pause=1;p->env_ptr=0;}
        }
        if(p->env_freq>=1u)p->env_count-=p->env_freq;
        else p->env_count=0u;
    }
    p->noise_count++;
    if(p->noise_count>=p->noise_freq){
        p->noise_scaler^=1u;
        if(p->noise_scaler){
            if(p->noise_seed&1u)p->noise_seed^=0x24000u;
            p->noise_seed>>=1;
        }
        if(p->noise_freq>=1u)p->noise_count-=p->noise_freq;
        else p->noise_count=0;
    }
    noise=p->noise_seed&1u;
    for(i=0;i<3u;i++){
        p->count[i]++;
        if(p->count[i]>=p->freq[i]){
            p->edge[i]=!p->edge[i];
            if(p->freq[i]>=1u)p->count[i]-=p->freq[i];
            else p->count[i]=0;
        }
        if((p->tmask[i]||p->edge[i])&&(p->nmask[i]||noise)){
            const uint8_t v=p->volume[i];
            sum+=(int32_t)vt[(v&32u)?p->env_ptr:(v&31u)]<<4;
        }
    }
    return (int16_t)sum;
}
void zz_ay2149_render(zz_ay2149_t *p,int16_t *stereo,uint32_t frames){
    uint32_t i;
    const uint32_t src=p->native_rate,dst=p->output_rate;
    for(i=0;i<frames;i++){
        uint64_t sum=0;
        uint32_t remaining=src;
        while(remaining){
            uint32_t take;
            if(!p->sub_units){
                p->native_value=ay_tick(p);
                p->sub_units=dst;
            }
            take=(p->sub_units<remaining)?p->sub_units:remaining;
            sum+=(uint64_t)take*(uint16_t)p->native_value;
            p->sub_units-=take;
            remaining-=take;
        }
        /* avg of native samples over the output interval; centered mono.
         * Keep the reference DC-bias behavior of EMU2149. */
        {int16_t out=(int16_t)(sum/src);
         stereo[2u*i]=out;stereo[2u*i+1u]=out;}
    }
}
