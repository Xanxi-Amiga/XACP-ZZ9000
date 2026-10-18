/*
 * Konami K051649 SCC / K052539 SCC+ synthesis for ZZVGMPlay.
 * Algorithm / register mapping adapted from MAME/libvgm:
 * https://github.com/ValleyBell/libvgm/blob/master/emu/cores/k051649.c
 * MAME core copyright Bryan McPhail, BSD-3-Clause.
 * Adaptation: direct integer fixed-point phase stepping at PCM output rate;
 * 64-bit division occurs on frequency REGISTER WRITES only, not per sample.
 */
#include "k051649_zz.h"
/* No platform headers required by the bare-metal renderer. */

/* MAME's stepping at chip clock / 16 is 2^21/(freq+1) in Q16.
 * At arbitrary output rate: 2*clock*2^16/((freq+1)*output_rate).
 * Channel frequency < 9 is treated as stopped (as in MAME).
 */
static void update_step(zz_scc *s, zz_scc_voice *v){
    uint64_t divisor;
    if(v->freq < 9 || !s->clock_hz || !s->rate_hz){v->inc=0;return;}
    divisor=(uint64_t)(v->freq + 1u) * s->rate_hz;
    v->inc=(uint32_t)((((uint64_t)s->clock_hz<<17) + divisor/2u) / divisor);
}
void zz_scc_init(zz_scc *s, uint32_t clock_hz, uint32_t rate_hz, int scc_plus){
    __builtin_memset(s,0,sizeof(*s));
    s->clock_hz=clock_hz;
    s->rate_hz=rate_hz ? rate_hz : 44100u;
    s->scc_plus=scc_plus ? 1u : 0u;
}
void zz_scc_write(zz_scc *s, uint8_t port, uint8_t offset, uint8_t data){
    unsigned ch,i;
    switch(port){
      case 0x00: /* SCC wave RAM */
      case 0x04: /* SCC+ wave RAM */
        if(s->test_reg & 0x40u || (!s->scc_plus && (s->test_reg&0x80u) && offset>=0x60u)) return;
        if(!s->scc_plus){
            if(offset>=0x80u)return;
            ch=offset>>5;
            if(ch==3u){ /* SCC channels 4 and 5 share the same waveform */
                s->voice[3].wave[offset&31u]=(int8_t)data;
                s->voice[4].wave[offset&31u]=(int8_t)data;
            } else s->voice[ch].wave[offset&31u]=(int8_t)data;
        } else if(offset<0xa0u){
            s->voice[offset>>5].wave[offset&31u]=(int8_t)data;
        }
        break;
      case 0x01: /* Frequency: two registers per voice */
        if(offset>=10u)return;
        ch=offset>>1;
        if(offset&1u)s->voice[ch].freq=(uint16_t)((s->voice[ch].freq&0x0ffu)|((uint16_t)(data&0x0fu)<<8));
        else s->voice[ch].freq=(uint16_t)((s->voice[ch].freq&0xf00u)|data);
        s->voice[ch].phase &= 0xFFFF0000u; /* VGMPlay/openMSX frequency-write behavior */
        if(s->test_reg&0x20u)s->voice[ch].phase=~0u;
        else if(s->voice[ch].freq<9u)s->voice[ch].phase|=0xFFFFu;
        update_step(s,&s->voice[ch]);
        break;
      case 0x02: /* Volumes 0..15 */
        if(offset<5u)s->voice[offset].vol=data&15u;
        break;
      case 0x03: /* Key mask for 5 voices */
        for(i=0;i<5u;i++)s->voice[i].key=(data>>i)&1u;
        break;
      case 0x05: /* Test/deformation register */
        s->test_reg=data;
        break;
      default:break;
    }
}
void zz_scc_render(zz_scc *s, int16_t *pcm_stereo, uint32_t frames){
    uint32_t i;
    unsigned ch;
    for(i=0;i<frames;i++){
        int32_t total=0;
        for(ch=0;ch<5u;ch++){
            zz_scc_voice *v=&s->voice[ch];
            if(!v->inc)continue;
            v->phase+=v->inc;
            if(v->key && v->vol){
                int32_t wav=v->wave[(v->phase>>16)&31u];
                /* Reference: (wave*volume >> 4)*32, signed arithmetic. */
                total+=((wav * (int32_t)v->vol)>>4)*32;
            }
        }
        if(total>32767)total=32767;
        if(total<-32768)total=-32768;
        pcm_stereo[i*2]=(int16_t)total;
        pcm_stereo[i*2+1]=(int16_t)total; /* SCC audio is mono */
    }
}
