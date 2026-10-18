/*
 * Capcom DL-1425 QSound HLE
 * Original algorithm and DSP tables: superctr (Ian Karlsson), with thanks to
 * Valley Bell (2018). BSD-3-Clause; see LICENSE in this directory.
 * Adaptations for ZZVGMPlay Core1: bounded ROM access; integer/shift and
 * DSP initialization guards; no hosted runtime dependency.
 * Reference: https://github.com/ValleyBell/qsound-hle
 */
#include "qsound.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#define QS_CLAMP(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
static const int16_t dry_table[33]={
 -16384,-16384,-16384,-16384,-16384,-16384,-16384,-16384,
 -16384,-16384,-16384,-16384,-16384,-16384,-16384,-16384,
 -16384,-14746,-13107,-11633,-10486,-9175,-8520,-7209,
 -6226,-5226,-4588,-3768,-3277,-2703,-2130,-1802,0};
static const int16_t wet_table[33]={
 0,-1638,-1966,-2458,-2949,-3441,-4096,-4669,
 -4915,-5120,-5489,-6144,-7537,-8831,-9339,-9830,
 -10240,-10322,-10486,-10568,-10650,-11796,-12288,-12288,
 -12534,-12648,-12780,-12829,-12943,-13107,-13418,-14090,-16384};
static const int16_t linear_table[33]={
 -16379,-16338,-16257,-16135,-15973,-15772,-15531,-15251,
 -14934,-14580,-14189,-13763,-13303,-12810,-12284,-11729,
 -11729,-11144,-10531,-9893,-9229,-8543,-7836,-7109,
 -6364,-5604,-4829,-4043,-3246,-2442,-1631,-817,0};
static const int16_t filter_tables[5][95]={
 {0,0,0,6,44,-24,-53,-10,59,-40,-27,1,39,-27,56,127,174,36,-13,49,
 212,142,143,-73,-20,66,-108,-117,-399,-265,-392,-569,-473,-71,95,-319,-218,-230,331,638,
 449,477,-180,532,1107,750,9899,3828,-2418,1071,-176,191,-431,64,117,-150,-274,-97,-238,165,
 166,250,-19,4,37,204,186,-6,140,-77,-1,1,18,-10,-151,-149,-103,-9,55,23,
 -102,-97,-11,13,-48,-27,5,18,-61,-30,64,72,0,0,0},
 {0,0,0,85,24,-76,-123,-86,-29,-14,-20,-7,6,-28,-87,-89,-5,100,154,160,
 150,118,41,-48,-78,-23,59,83,-2,-176,-333,-344,-203,-66,-39,2,224,495,495,280,
 432,1340,2483,5377,1905,658,0,97,347,285,35,-95,-78,-82,-151,-192,-171,-149,-147,-113,
 -22,71,118,129,127,110,71,31,20,36,46,23,-27,-63,-53,-21,-19,-60,-92,-69,
 -12,25,29,30,40,41,29,30,46,39,-15,-74,0,0,0},
 {0,0,0,23,42,47,29,10,2,-14,-54,-92,-93,-70,-64,-77,-57,18,94,113,
 87,69,67,50,25,29,58,62,24,-39,-131,-256,-325,-234,-45,58,78,223,485,496,
 127,6,857,2283,2683,4928,1328,132,79,314,189,-80,-90,35,-21,-186,-195,-99,-136,-258,
 -189,82,257,185,53,41,84,68,38,63,77,14,-60,-71,-71,-120,-151,-84,14,29,
 -8,7,66,69,12,-3,54,92,52,-6,-15,-2,0,0,0},
 {0,0,0,2,-28,-37,-17,0,-9,-22,-3,35,52,39,20,7,-6,2,55,121,
 129,67,8,1,9,-6,-16,16,66,96,118,130,75,-47,-92,43,223,239,151,219,
 440,475,226,206,940,2100,2663,4980,865,49,-33,186,231,103,42,114,191,184,116,29,
 -47,-72,-21,60,96,68,31,32,63,87,76,39,7,14,55,85,67,18,-12,-3,
 21,34,29,6,-27,-49,-37,-2,16,0,-21,-16,0,0,0},
 {0,0,0,48,7,-22,-29,-10,24,54,59,29,-36,-117,-185,-213,-185,-99,13,90,
 83,24,-5,23,53,47,38,56,67,57,75,107,16,-242,-440,-355,-120,-33,-47,152,
 501,472,-57,-292,544,1937,2277,6145,1240,153,47,200,152,36,64,134,74,-82,-208,-266,
 -268,-188,-42,65,74,56,89,133,114,44,-3,-1,17,29,29,-2,-76,-156,-187,-151,
 -85,-31,-5,7,20,32,24,-5,-20,6,48,62,0,0,0}
};
static const int16_t filter_tables2[209]={
 /* f2e */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,
 /* f73 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,
 -371,-196,-268,-512,-303,-315,-184,-76,276,-256,298,196,990,236,1114,-126,4377,6549,791,
 /* fa0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,-16384,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint16_t adpcm_steps[16]={
 154,154,128,102,77,58,58,58,58,58,58,58,77,102,128,154};
enum {QS_INIT1=0x288,QS_INIT2=0x61a,QS_REFRESH1=0x039,
 QS_REFRESH2=0x04f,QS_NORMAL1=0x314,QS_NORMAL2=0x6b2};

static const int16_t *filter_source(uint16_t address, int n){
    int idx;
    if(address>=0xf2e && (uint32_t)address+(unsigned)n<=0xfff)
        return &filter_tables2[address-0xf2e];
    if(address<0xd53)return 0;
    idx=(address-0xd53)/95;
    if(idx>=0 && idx<5 && n<=95)
        return filter_tables[idx];
    return 0;
}
static void init_registers(struct qsound_chip *c){
    unsigned i;
    for(i=0;i<256;i++)c->register_map[i]=0;
    for(i=0;i<16;i++){
        c->register_map[(i<<3)+0]=&c->voice[(i+1u)&15u].bank;
        c->register_map[(i<<3)+1]=(uint16_t*)&c->voice[i].addr;
        c->register_map[(i<<3)+2]=&c->voice[i].rate;
        c->register_map[(i<<3)+3]=&c->voice[i].phase;
        c->register_map[(i<<3)+4]=(uint16_t*)&c->voice[i].loop_len;
        c->register_map[(i<<3)+5]=(uint16_t*)&c->voice[i].end_addr;
        c->register_map[(i<<3)+6]=(uint16_t*)&c->voice[i].volume;
        c->register_map[i+0x80]=&c->voice_pan[i];
        c->register_map[i+0xba]=(uint16_t*)&c->voice[i].echo;
    }
    for(i=0;i<3;i++){
        c->register_map[(i<<2)+0xca]=&c->adpcm[i].start_addr;
        c->register_map[(i<<2)+0xcb]=&c->adpcm[i].end_addr;
        c->register_map[(i<<2)+0xcc]=&c->adpcm[i].bank;
        c->register_map[(i<<2)+0xcd]=(uint16_t*)&c->adpcm[i].volume;
        c->register_map[0xd6+i]=&c->adpcm[i].flag;
        c->register_map[0x90+i]=&c->voice_pan[16+i];
    }
    c->register_map[0x93]=(uint16_t*)&c->echo.feedback;
    c->register_map[0xd9]=&c->echo.end_pos;
    c->register_map[0xe2]=&c->delay_update;
    c->register_map[0xe3]=&c->next_state;
    for(i=0;i<2;i++){
        c->register_map[(i<<1)+0xda]=(uint16_t*)&c->filter[i].table_pos;
        c->register_map[(i<<1)+0xde]=(uint16_t*)&c->wet[i].delay;
        c->register_map[(i<<1)+0xe4]=(uint16_t*)&c->wet[i].volume;
        c->register_map[(i<<1)+0xdb]=(uint16_t*)&c->alt_filter[i].table_pos;
        c->register_map[(i<<1)+0xdf]=(uint16_t*)&c->dry[i].delay;
        c->register_map[(i<<1)+0xe5]=(uint16_t*)&c->dry[i].volume;
    }
}
uint32_t qsound_hle_start(struct qsound_chip *c,uint32_t clock){
    unsigned i;
    memset(c,0,sizeof(*c));
    for(i=0;i<33;i++){
        c->pan_tables[0][0][i]=dry_table[i];
        c->pan_tables[1][0][i]=dry_table[32-i];
        c->pan_tables[0][1][i]=wet_table[i];
        c->pan_tables[1][1][i]=wet_table[32-i];
        c->pan_tables[0][0][i+0x30]=linear_table[i];
        c->pan_tables[1][0][i+0x30]=linear_table[32-i];
    }
    init_registers(c);
    return clock/(2u*1248u);
}
void qsound_hle_reset(struct qsound_chip *c){
    c->ready_flag=0;c->out[0]=c->out[1]=0;
    c->state=QS_INIT1;c->state_counter=0;
}
void qsound_hle_write(struct qsound_chip *c,uint8_t reg,uint16_t v){
    uint16_t *dst=c->register_map[reg];if(dst)*dst=v;
    c->ready_flag=0;
}
static inline int16_t sample_at(struct qsound_chip *c,uint16_t bank,uint16_t addr){
    uint32_t pos;
    uint8_t s;
    if(!c->rom_data||!c->rom_len||!(bank&0x8000u))return 0;
    pos=((uint32_t)(bank&0x7fffu)<<16)|addr;
    if(pos>=c->rom_len)return 0; /* Explicit bound; upstream assumes valid ROM. */
    s=c->rom_data[pos];
    return (int16_t)((s<<8)|s); /* Same PCM expansion as upstream/MAME. */
}
static void state_init(struct qsound_chip *c){
    unsigned i;int mode=c->state==QS_INIT2;
    if(c->state_counter>=2){c->state_counter=0;c->state=c->next_state;return;}
    if(c->state_counter==1){c->state_counter++;return;}
    memset(c->voice,0,sizeof(c->voice));
    memset(c->adpcm,0,sizeof(c->adpcm));
    memset(c->filter,0,sizeof(c->filter));
    memset(c->alt_filter,0,sizeof(c->alt_filter));
    memset(c->wet,0,sizeof(c->wet));
    memset(c->dry,0,sizeof(c->dry));
    memset(&c->echo,0,sizeof(c->echo));
    for(i=0;i<19;i++)c->voice_pan[i]=0x120u,c->voice_output[i]=0;
    for(i=0;i<16;i++)c->voice[i].bank=0x8000u;
    for(i=0;i<3;i++)c->adpcm[i].bank=0x8000u;
    if(!mode){
        c->wet[0].delay=0;c->dry[0].delay=46;
        c->wet[1].delay=0;c->dry[1].delay=48;
        c->filter[0].table_pos=0xdb2;c->filter[1].table_pos=0xe11;
        c->echo.end_pos=0x554+6;c->next_state=QS_REFRESH1;
    }else{
        c->wet[0].delay=1;c->dry[0].delay=0;
        c->wet[1].delay=0;c->dry[1].delay=0;
        c->filter[0].table_pos=0xf73;c->filter[1].table_pos=0xfa4;
        c->alt_filter[0].table_pos=0xf73;
        c->alt_filter[1].table_pos=0xfa4;
        c->echo.end_pos=0x53c+6;c->next_state=QS_REFRESH2;
    }
    for(i=0;i<2;i++){
        c->wet[i].volume=0x3fff;c->dry[i].volume=0x3fff;
    }
    c->delay_update=1;c->ready_flag=0;c->state_counter=1;
}
static void refresh(struct qsound_chip *c,int mode2){
    int i;const int16_t *t;
    for(i=0;i<2;i++){
        c->filter[i].delay_pos=0;
        c->filter[i].tap_count=mode2?45:95;
        t=filter_source((uint16_t)c->filter[i].table_pos,c->filter[i].tap_count);
        if(t)memcpy(c->filter[i].taps,t,(size_t)c->filter[i].tap_count*2u);
        if(mode2){
            c->alt_filter[i].delay_pos=0;c->alt_filter[i].tap_count=44;
            t=filter_source((uint16_t)c->alt_filter[i].table_pos,44);
            if(t)memcpy(c->alt_filter[i].taps,t,44u*2u);
        }
    }
    c->state=c->next_state=mode2?QS_NORMAL2:QS_NORMAL1;
}
static int16_t qs_sat16(int64_t n){return (int16_t)QS_CLAMP(n,-32768,32767);}
static int32_t qs_sat32(int64_t n){return (int32_t)QS_CLAMP(n,(int64_t)INT32_MIN,(int64_t)INT32_MAX);}
static int16_t pcm_update(struct qsound_chip *c,struct qsound_voice *v,int64_t *echo_input){
    int32_t phase;
    int16_t val=(int16_t)(((int32_t)v->volume*sample_at(c,v->bank,(uint16_t)v->addr))>>14);
    *echo_input += (int64_t)val*(int32_t)v->echo*4;
    phase=(int32_t)v->rate+((int32_t)v->addr*4096+(v->phase>>4));
    if((phase>>12)>=v->end_addr)phase-=(int32_t)v->loop_len*4096;
    phase=QS_CLAMP(phase,-0x8000000,0x7ffffff);
    v->addr=(int16_t)(phase>>12);
    v->phase=(uint16_t)((uint32_t)phase<<4);
    return val;
}
static void adpcm_update(struct qsound_chip *c,int vno,int nib){
    struct qsound_adpcm *v=&c->adpcm[vno];
    int32_t delta;int8_t step;
    if(!nib){
        if(v->cur_addr==v->end_addr)v->cur_vol=0;
        if(v->flag){
            c->voice_output[16+vno]=0;
            v->flag=0;v->step_size=10;v->cur_vol=v->volume;
            v->cur_addr=v->start_addr;
        }
        step=(int8_t)(sample_at(c,v->bank,v->cur_addr)>>8);
    }else{
        step=(int8_t)(sample_at(c,v->bank,v->cur_addr++)>>4);
    }
    step>>=4;
    delta=((1+abs((int)step*2))*v->step_size)>>1;
    if(step<=0)delta=-delta;
    delta=QS_CLAMP((int32_t)c->voice_output[16+vno]+delta,-32768,32767);
    c->voice_output[16+vno]=(int16_t)((delta*v->cur_vol)>>16);
    v->step_size=(int16_t)QS_CLAMP(((int)adpcm_steps[(int)8+step]*v->step_size)>>6,1,2000);
}
static int16_t echo_update(struct qsound_echo *e,int32_t input){
    int32_t old=e->delay_line[e->delay_pos];
    int32_t last=e->last_sample;
    int64_t news;
    e->last_sample=(int16_t)old;
    old=(old+last)>>1;
    news=(int64_t)input+((int64_t)old*e->feedback*4);
    e->delay_line[e->delay_pos++]=qs_sat16(news>>16);
    if(e->delay_pos>=e->length)e->delay_pos=0;
    return (int16_t)old;
}
static int32_t fir_update(struct qsound_fir *f,int16_t in){
    int64_t acc=0;int i;
    if(f->tap_count<1)return 0;
    for(i=0;i<f->tap_count-1;i++){
        acc-=(int64_t)f->taps[i]*f->delay_line[f->delay_pos++]*4;
        if(f->delay_pos>=f->tap_count-1)f->delay_pos=0;
    }
    acc-=(int64_t)f->taps[i]*in*4;
    f->delay_line[f->delay_pos++]=in;
    if(f->delay_pos>=f->tap_count-1)f->delay_pos=0;
    return qs_sat32(acc);
}
static int32_t delay_process(struct qsound_delay *d,int32_t input){
    int32_t out;
    d->delay_line[d->write_pos++]=qs_sat16((int64_t)input>>16);
    if(d->write_pos>=51)d->write_pos=0;
    out=(int32_t)d->delay_line[d->read_pos++]*d->volume;
    if(d->read_pos>=51)d->read_pos=0;
    return out;
}
static void delay_recalc(struct qsound_delay *d){
    int32_t pos=((int32_t)d->write_pos-d->delay)%51;
    if(pos<0)pos+=51;
    d->read_pos=(int16_t)pos;
}
static void state_normal(struct qsound_chip *c){
    int ch,i;int64_t echo_input=0;
    int16_t echo_out;
    c->ready_flag=0x80;
    c->echo.length=(int16_t)QS_CLAMP((int32_t)c->echo.end_pos-
        (c->state==QS_NORMAL2?0x53c:0x554),1,1024);
    for(i=0;i<16;i++)c->voice_output[i]=pcm_update(c,&c->voice[i],&echo_input);
    adpcm_update(c,c->state_counter%3,c->state_counter/3);
    echo_out=echo_update(&c->echo,qs_sat32(echo_input));
    for(ch=0;ch<2;ch++){
        int64_t wet=(ch==1)?(int64_t)echo_out*65536:0;
        int64_t dry=(ch==0)?(int64_t)echo_out*65536:0;
        int64_t output;
        for(i=0;i<19;i++){
            uint16_t idx=(uint16_t)(c->voice_pan[i]-0x110u);
            if(idx>97)idx=97;
            dry-=(int64_t)c->voice_output[i]*c->pan_tables[ch][0][idx]*4;
            wet-=(int64_t)c->voice_output[i]*c->pan_tables[ch][1][idx]*4;
        }
        wet=fir_update(&c->filter[ch],qs_sat16(wet>>16));
        if(c->state==QS_NORMAL2)
            dry=fir_update(&c->alt_filter[ch],qs_sat16(dry>>16));
        output=((int64_t)delay_process(&c->wet[ch],qs_sat32(wet))+
                delay_process(&c->dry[ch],qs_sat32(dry)))*4;
        output=(output+0x8000)&~(int64_t)0xffff;
        c->out[ch]=qs_sat16(output>>16);
        if(c->delay_update){delay_recalc(&c->wet[ch]);delay_recalc(&c->dry[ch]);}
    }
    c->delay_update=0;
    if(++c->state_counter>5){c->state_counter=0;c->state=c->next_state;}
}
void qsound_hle_step(struct qsound_chip *c){
    switch(c->state){
    default:
    case QS_INIT1:
    case QS_INIT2:state_init(c);break;
    case QS_REFRESH1:refresh(c,0);break;
    case QS_REFRESH2:refresh(c,1);break;
    case QS_NORMAL1:
    case QS_NORMAL2:state_normal(c);break;
    }
}
