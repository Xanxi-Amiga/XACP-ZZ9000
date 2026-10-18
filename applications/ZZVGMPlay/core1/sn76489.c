#include "sn76489.h"
#include <string.h>

static const int16_t voltab[16] = {
    8192,6507,5168,4105,3261,2591,2058,1635,
    1299,1032,820,651,517,411,326,0
};

static void recalc_tone(zz_sn76489_t *s, unsigned ch)
{
    uint32_t n=s->tone[ch] & 0x3ffu;
    uint64_t num;
    if (!n) n=0x400u;
    /* tone frequency = clock / (32*N); phase high bit is square polarity */
    num=((uint64_t)s->clock << 32);
    s->inc[ch]=(uint32_t)(num / ((uint64_t)32u*n*s->rate));
}
static void recalc_noise(zz_sn76489_t *s)
{
    uint32_t div;
    switch (s->noise_ctrl & 3u) {
        default: div=512u; break;
        case 1: div=1024u; break;
        case 2: div=2048u; break;
        case 3:
            s->noise_inc=s->inc[2];
            return;
    }
    s->noise_inc=(uint32_t)(((uint64_t)s->clock << 32)/((uint64_t)div*s->rate));
}
void zz_sn76489_reset(zz_sn76489_t *s, uint32_t clock, uint32_t rate, uint16_t feedback, uint8_t shift_width)
{
    unsigned i;
    memset(s,0,sizeof(*s));
    s->clock=clock?clock:3579545u;
    s->rate=rate?rate:44100u;
    s->feedback=feedback?feedback:0x0009u;
    if (shift_width<8 || shift_width>16) shift_width=16;
    s->width_mask=(uint16_t)((shift_width==16)?0x8000u:(1u<<(shift_width-1)));
    s->lfsr=s->width_mask;
    s->stereo=0xff;
    s->latched=0;
    for(i=0;i<3;i++){s->tone[i]=1;recalc_tone(s,i);}
    for(i=0;i<4;i++)s->vol[i]=15;
    recalc_noise(s);
}
void zz_sn76489_stereo(zz_sn76489_t *s, uint8_t mask){s->stereo=mask;}
void zz_sn76489_write(zz_sn76489_t *s, uint8_t data)
{
    uint8_t reg;
    if (data&0x80u) {
        s->latched=(data>>4)&7u;
        reg=s->latched;
        if ((reg&1u)==0 && reg<6) {
            unsigned ch=reg>>1;
            s->tone[ch]=(uint16_t)((s->tone[ch]&0x3f0u)|(data&0x0fu));
            recalc_tone(s,ch); if(ch==2 && (s->noise_ctrl&3u)==3) recalc_noise(s);
        } else if (reg==6) {
            s->noise_ctrl=data&7u; s->lfsr=s->width_mask; recalc_noise(s);
        } else {
            s->vol[reg>>1]=data&0x0fu;
        }
    } else {
        reg=s->latched;
        if ((reg&1u)==0 && reg<6) {
            unsigned ch=reg>>1;
            s->tone[ch]=(uint16_t)((s->tone[ch]&0x00fu)|((uint16_t)(data&0x3fu)<<4));
            recalc_tone(s,ch); if(ch==2 && (s->noise_ctrl&3u)==3) recalc_noise(s);
        } else if (reg==6) {
            s->noise_ctrl=data&7u; s->lfsr=s->width_mask; recalc_noise(s);
        } else s->vol[reg>>1]=data&0x0fu;
    }
}
static unsigned parity16(uint16_t v)
{
    v^=v>>8;v^=v>>4;v^=v>>2;v^=v>>1;return v&1u;
}
void zz_sn76489_render(zz_sn76489_t *s, int16_t *out, uint32_t frames)
{
    uint32_t i; unsigned ch;
    for(i=0;i<frames;i++) {
        int32_t l=0,r=0;
        for(ch=0;ch<3;ch++) {
            int v;
            s->phase[ch]+=s->inc[ch];
            v=(s->phase[ch]&0x80000000u)?voltab[s->vol[ch]]:-voltab[s->vol[ch]];
            if (s->stereo&(1u<<(4+ch))) l+=v;
            if (s->stereo&(1u<<ch)) r+=v;
        }
        {
            uint32_t old=s->noise_phase;
            int v;
            s->noise_phase+=s->noise_inc;
            if (s->noise_phase<old) {
                unsigned fb=(s->noise_ctrl&4u)?parity16((uint16_t)(s->lfsr&s->feedback)):(s->lfsr&1u);
                s->lfsr=(uint16_t)((s->lfsr>>1)|(fb?s->width_mask:0));
            }
            v=(s->lfsr&1u)?voltab[s->vol[3]]:-voltab[s->vol[3]];
            if (s->stereo&0x80u) l+=v;
            if (s->stereo&0x08u) r+=v;
        }
        if(l>32767)l=32767;if(l<-32768)l=-32768;
        if(r>32767)r=32767;if(r<-32768)r=-32768;
        out[i*2]=(int16_t)l;out[i*2+1]=(int16_t)r;
    }
}
