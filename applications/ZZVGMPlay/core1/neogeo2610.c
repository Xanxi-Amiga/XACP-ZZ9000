/* ZZVGMPlay Arcade A02: YM2610 ADPCM-A/B and SSG port layer.
 * Original compact implementation; experimental audio fidelity.
 * Port1 ADPCM-A / Port0 Delta-T ADPCM-B, VGM ROM types 82/83.
 * FM is routed separately via the existing GENS YM2612 (approximate).
 * No malloc or firmware changes.
 */
#include "neogeo2610.h"
#include "arcade_pcm.h"
#include <stddef.h>

#define NEO_ROM_LIMIT (64u*1024u*1024u)
#define NEO_FRAC (1ull<<32)

typedef struct {uint8_t *data;uint32_t length;} neo_rom_t;
typedef struct {
    uint32_t nib, end, start;
    int32_t accum, idx;
    uint8_t active, pan, level;
    uint32_t gain;
} neo_achan_t;
typedef struct {
    uint32_t nib, end, start, delta;
    uint64_t phase, step;
    int32_t accum, idx;
    uint8_t playing, repeat, pan, level;
} neo_bchan_t;
static neo_rom_t rom_a,rom_b;
static uint8_t ra[0x30],rb[0x20];
static neo_achan_t a[6];
static neo_bchan_t b;
static uint64_t phase_a,step_a;
static uint32_t neo_clock,neo_rate;
static int16_t last_l,last_r;
static zz_ay8910_t neo_ssg;
static const int16_t steps_a[49] = {
 16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
 73,80,88,97,107,118,130,143,157,173,190,209,230,253,
 279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552};
static const int8_t adjust_a[8]={-1,-1,-1,-1,2,5,7,9};
static const uint8_t adjust_b[8]={57,57,57,57,77,102,128,153};
static int clip(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
static int16_t s16(int32_t x){return (int16_t)clip(x,-32768,32767);}
static uint8_t get_rom(neo_rom_t *r,uint32_t addr){return r->data&&addr<r->length?r->data[addr]:0xff;}
/* Exponential 0.75 dB attenuation steps, Q15. Computed only on keyon. */
static uint32_t gain_075(unsigned attenuation){
    uint32_t g=32768u; unsigned i;
    if(attenuation>96)attenuation=96;
    for(i=0;i<attenuation;i++)g=(g*30100u+16384u)>>15; /* ~-0.74 dB */
    return g;
}
void zz_neo_init(uint32_t clock_hz,uint32_t output_hz){
    unsigned i;
    neo_clock=clock_hz;neo_rate=output_hz?output_hz:44100;
    rom_a.data=rom_b.data=NULL;rom_a.length=rom_b.length=0;
    for(i=0;i<sizeof(ra);i++)ra[i]=0;
    for(i=0;i<sizeof(rb);i++)rb[i]=0;
    for(i=0;i<6;i++){a[i].nib=a[i].start=a[i].end=0;a[i].accum=a[i].idx=0;a[i].active=a[i].pan=a[i].level=0;a[i].gain=0;}
    b.nib=b.start=b.end=b.delta=0;b.phase=b.step=0;
    b.accum=0;b.idx=127;b.playing=b.repeat=b.pan=b.level=0;
    phase_a=0;step_a=neo_clock?(((uint64_t)neo_clock<<32)/(432u*neo_rate)):0;
    last_l=last_r=0;
    /* YM2610 SSG is clocked at input/4; do not use the independent AY. */
    zz_ay8910_reset(&neo_ssg,neo_clock/4u,neo_rate);
}
int zz_neo_ssg_enabled(void){return neo_clock!=0;}
void zz_neo_render_ssg(int16_t *out_stereo,uint32_t frames){zz_ay8910_render(&neo_ssg,out_stereo,frames);}
int zz_neo_rom(uint8_t typ,uint32_t full,uint32_t dest,const uint8_t *data,uint32_t len){
    neo_rom_t *r=(typ==0x82u)?&rom_a:((typ==0x83u)?&rom_b:NULL);
    uint32_t i;
    if(!r||!data||!full||full>NEO_ROM_LIMIT||dest>full||len>full-dest)return 0;
    if(!r->data){
        r->data=zz_arcade_alloc_rom(full,0xffu);
        if(!r->data)return 0;
        r->length=full;
    }else if(r->length!=full)return 0;
    for(i=0;i<len;i++)r->data[dest+i]=data[i];
    return 1;
}
static void start_a(unsigned ch){
    uint32_t start=((uint32_t)ra[0x18u+ch]<<16)|((uint32_t)ra[0x10u+ch]<<8);
    uint32_t end=(((uint32_t)ra[0x28u+ch]<<8)|ra[0x20u+ch]);
    neo_achan_t *v=&a[ch];
    end=(end<<8)|255u;
    v->active=(rom_a.data&&start<=end&&start<rom_a.length)?1u:0u;
    v->start=start;v->end=end;v->nib=0;v->accum=0;v->idx=0;
    v->pan=ra[0x08u+ch]>>6;
    v->level=ra[0x08u+ch]&31u;v->gain=gain_075((31u-v->level)+(63u-(ra[1]&63u)));
}
static void start_b(void){
    uint32_t start=((uint32_t)rb[0x13]<<16)|((uint32_t)rb[0x12]<<8);
    uint32_t end=((((uint32_t)rb[0x15]<<8)|rb[0x14])<<8)|255u;
    b.playing=(rom_b.data&&start<=end&&start<rom_b.length)?1u:0u;
    b.start=start;b.end=end;b.nib=0;b.accum=0;b.idx=127;b.phase=0;
}
static void update_b_rate(void){
    b.delta=(uint32_t)rb[0x19]|((uint32_t)rb[0x1a]<<8);
    /* 55.56 kHz native Delta-T source at clock 8MHz. */
    b.step=neo_clock&&b.delta?((uint64_t)neo_clock*b.delta*65536ull)/(144u*neo_rate):0;
}
void zz_neo_write(unsigned port,uint8_t reg,uint8_t value){
    unsigned i;
    if(!neo_clock)return;
    if(port==0){
        if(reg<0x10){zz_ay8910_write(&neo_ssg,reg,value);return;}
        if(reg>=0x20)return;
        rb[reg]=value;
        if(reg==0x10){
            if(value&0x01u)b.playing=0;
            if(value&0x80u){b.repeat=(value&0x10u)!=0;start_b();}
        }else if(reg==0x11)b.pan=value>>6;
        else if(reg==0x19||reg==0x1a)update_b_rate();
        else if(reg==0x1b)b.level=value;
        return;
    }
    if(port!=1||reg>=0x30u)return;
    ra[reg]=value;
    if(reg==0){
        for(i=0;i<6;i++)if(value&(1u<<i)){
            if(value&0x80u)a[i].active=0;
            else start_a(i);
        }
    }else if(reg==1u){
        for(i=0;i<6;i++)a[i].gain=gain_075((31u-a[i].level)+(63u-(ra[1]&63u)));
    }else if(reg>=0x08u&&reg<=0x0du){
        neo_achan_t *v=&a[reg-0x08u];
        v->pan=value>>6;
        v->level=value&31u;
        v->gain=gain_075((31u-v->level)+(63u-(ra[1]&63u)));
    }
}
static int32_t decode_a(neo_achan_t *v,uint32_t ch){
    uint32_t pos=v->start+(v->nib>>1);uint8_t byte,n;
    int32_t diff,step,x;
    if(!v->active||pos>v->end||pos>=rom_a.length){v->active=0;return 0;}
    byte=get_rom(&rom_a,pos);n=(v->nib&1u)?byte&15u:byte>>4;v->nib++;
    step=steps_a[(unsigned)v->idx];
    diff=step>>3;if(n&1u)diff+=step>>2;if(n&2u)diff+=step>>1;if(n&4u)diff+=step;
    v->accum=clip(v->accum+((n&8u)?-diff:diff),-2048,2047);
    v->idx=clip(v->idx+adjust_a[n&7u],0,48);
    /* Channel gain and common total gain. Both regs are inverted attenuation. */
    x=v->accum<<3;
    x=(x*(int32_t)v->gain)>>15;
    (void)ch;
    return x;
}
static int32_t decode_b(void){
    uint32_t pos=b.start+(b.nib>>1);uint8_t byte,n;
    int32_t d;
    if(!b.playing)return 0;
    if(pos>b.end||pos>=rom_b.length){
        if(b.repeat){start_b();pos=b.start;if(!b.playing)return 0;}
        else{b.playing=0;return 0;}
    }
    byte=get_rom(&rom_b,pos);n=(b.nib&1u)?byte&15u:byte>>4;b.nib++;
    d=((int32_t)((n&7u)*2u+1u)*b.idx)>>3;
    b.accum=clip(b.accum+((n&8u)?-d:d),-32768,32767);
    b.idx=clip((b.idx*adjust_b[n&7u])/64,127,24576);
    return (b.accum*b.level)>>8;
}
void zz_neo_render(int16_t *out,uint32_t frames){
    uint32_t i;unsigned ch;
    int32_t l=0,r=0;
    for(i=0;i<frames;i++){
        l=last_l;r=last_r;
        phase_a+=step_a;
        while(phase_a>=NEO_FRAC){
            phase_a-=NEO_FRAC;l=0;r=0;
            for(ch=0;ch<6;ch++)if(a[ch].active){
                int32_t s=decode_a(&a[ch],ch);
                if(a[ch].pan&2u)l+=s;
                if(a[ch].pan&1u)r+=s;
            }
        }
        /* Delta-T uses independent rate and pan. Keep previous decoded
           signal until the next nibble; writes take effect at VGM boundaries. */
        if(b.playing&&b.step){
            b.phase+=b.step;
            while(b.phase>=NEO_FRAC){b.phase-=NEO_FRAC;decode_b();}
        }
        last_l=s16(l);last_r=s16(r);
        {int32_t bv=b.playing?(b.accum*b.level)>>8:0;
         if(b.pan&2u)l+=bv;
         if(b.pan&1u)r+=bv;
        }
        out[i*2]=s16(l);out[i*2+1]=s16(r);
    }
}
