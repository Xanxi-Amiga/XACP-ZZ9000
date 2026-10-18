/* ZZVGMPlay A09 - Ricoh RF5C68 PCM backend.
 * Based on the register, stepping and sample sign semantics of
 * MAME/libvgm emu/cores/rf5c68.c (BSD-3-Clause).
 * Original engine authors Olivier Galibert, Aaron Giles.
 * Adaptation (c) Xanxi 2026.
 */
#include "rf5c68_zz.h"
#include <string.h>

static int16_t clip16(int32_t x){return (int16_t)(x>32767?32767:(x < -32768?-32768:x));}
void zz_rf5c68_reset(zz_rf5c68_t *s,uint32_t clock,uint32_t sample_rate){
    memset(s,0,sizeof(*s));
    s->native_rate=clock/384u;
    if(!s->native_rate)s->native_rate=1u;
    s->sample_rate=sample_rate?sample_rate:44100u;
    /* Exactly one tick at start: first audio frame can produce PCM. */
    s->tick_phase=s->sample_rate;
}
void zz_rf5c68_write(zz_rf5c68_t *s,uint8_t reg,uint8_t val){
    zz_rf5c68_voice_t *v=&s->v[s->channel_bank];
    unsigned i;
    switch(reg){
    case 0: v->env=val;break;
    case 1: v->pan=val;break;
    case 2: v->step=(uint16_t)((v->step&0xff00u)|val);break;
    case 3: v->step=(uint16_t)((v->step&0x00ffu)|((uint16_t)val<<8));break;
    case 4: v->loop=(uint16_t)((v->loop&0xff00u)|val);break;
    case 5: v->loop=(uint16_t)((v->loop&0x00ffu)|((uint16_t)val<<8));break;
    case 6: v->start=val;
            if(!v->enabled)v->addr=(uint32_t)v->start<<19;
            break;
    case 7: s->enabled=(val>>7)&1u;
            if(val&0x40u)s->channel_bank=val&7u;
            else s->memory_bank=val&15u;
            break;
    case 8:
            for(i=0;i<8;i++){
                s->v[i].enabled=((~val)>>i)&1u;
                if(!s->v[i].enabled)s->v[i].addr=(uint32_t)s->v[i].start<<19;
            }
            break;
    default:break;
    }
}
void zz_rf5c68_ram_byte(zz_rf5c68_t *s,uint16_t banked_offset,uint8_t val){
    s->ram[((uint32_t)s->memory_bank<<12)|(banked_offset&0xfffu)]=val;
}
void zz_rf5c68_ram_block(zz_rf5c68_t *s,uint32_t offset,const uint8_t *src,uint32_t count){
    if(!src||offset>=ZZ_RF5C68_RAM_SIZE)return;
    if(count>ZZ_RF5C68_RAM_SIZE-offset)count=ZZ_RF5C68_RAM_SIZE-offset;
    memcpy(s->ram+offset,src,count);
}
/* Render one native tick: precisely the RF5C68's sign-magnitude PCM output.
 * Like libvgm, enabled channels that hit 0xff repeatedly at their loop address
 * stop producing output, but keep their key-on bit unchanged. */
static void zz_rf5c68_tick(zz_rf5c68_t *s){
    int32_t l=0,r=0;unsigned ch;
    if(s->enabled){
        for(ch=0;ch<8;ch++){
            zz_rf5c68_voice_t *v=&s->v[ch];int32_t amp;
            uint8_t smp;
            if(!v->enabled)continue;
            smp=s->ram[(v->addr>>11)&0xffffu];
            if(smp==0xffu){
                v->addr=(uint32_t)v->loop<<11;
                smp=s->ram[(v->addr>>11)&0xffffu];
                if(smp==0xffu)continue;
            }
            v->addr+=v->step;
            /* sign-magnitude representation: 0x80..fe positive, 00..7f negative */
            amp=(smp&0x7fu);
            if(!(smp&0x80u))amp=-amp;
            l+=(amp*((v->pan&15u)*v->env))>>5;
            r+=(amp*(((v->pan>>4)&15u)*v->env))>>5;
        }
    }
    s->left=l;s->right=r;
}
void zz_rf5c68_render(zz_rf5c68_t *s,int16_t *out,uint32_t frames){
    uint32_t i;
    if(!out)return;
    for(i=0;i<frames;i++){
        /* Native rate = input clock / 384; hold between native ticks.
         * Division-free per-output-frame phase accumulator. */
        while(s->tick_phase>=s->sample_rate){
            zz_rf5c68_tick(s);
            s->tick_phase-=s->sample_rate;
        }
        out[2*i]=clip16(s->left);
        out[2*i+1]=clip16(s->right);
        s->tick_phase+=s->native_rate;
    }
}

/* Full-precision rendering: must not clip RF5C68 voices before combining
 * with the FM chips. The existing int16 API remains for the old tests. */
void zz_rf5c68_render32(zz_rf5c68_t *s,int32_t *out,uint32_t frames){
    uint32_t i;
    if(!out)return;
    for(i=0;i<frames;i++){
        while(s->tick_phase>=s->sample_rate){
            zz_rf5c68_tick(s);
            s->tick_phase-=s->sample_rate;
        }
        out[2*i]=s->left;
        out[2*i+1]=s->right;
        s->tick_phase+=s->native_rate;
    }
}
