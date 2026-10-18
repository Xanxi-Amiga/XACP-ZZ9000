/* ZZVGMPlay experimental arcade PCM backends. (C) 2026 ZZVGMPlay contributors.
 * Behavior follows public VGM 1.71 format and hardware descriptions.
 * No upstream emulator implementation is copied here.
 * Limitations: first chip only; 6295 ROM bank switching outside a single
 * contiguous VGM ROM image is unsupported; SegaPCM basic 315-5218 mode only.
 */
#include "arcade_pcm.h"
#include <stddef.h>

#define ARCADE_ROM_MAX (32u * 1024u * 1024u)

typedef struct {uint8_t *ptr;uint32_t size;} pcmrom_t;
static pcmrom_t sega_rom,oki_rom;
static uint8_t *rom_cursor,*rom_end;
static uint32_t out_rate,sega_clock_hz,oki_clock_hz;
static uint32_t sega_bankshift,sega_bankmask;
static uint64_t sega_phase,oki_phase,sega_step,oki_sample_step;
static int16_t sega_left,sega_right,oki_mono;

typedef struct {
 uint32_t addr;
 uint16_t loop;
 uint8_t end,freq,vol_l,vol_r,ctrl;
} sgvoice_t;
static sgvoice_t sg[16];

typedef struct {
 uint32_t start,nibble,total;
 int32_t signal;
 int8_t step;
 uint8_t volume,active;
} okvoice_t;
static okvoice_t ok[4];
static int pending_sample;
static const uint8_t oki_vol[16]={32,22,16,11,8,6,4,3,2,2,1,1,1,1,0,0};
static const int8_t oki_shift[8]={-1,-1,-1,-1,2,4,6,8};
static const int16_t oki_step[49]={
 16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
 107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
 494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552};

static uint8_t rom_byte(const pcmrom_t *r,uint32_t addr){return r->ptr&&addr<r->size?r->ptr[addr]:0xff;}

uint8_t *zz_arcade_alloc_rom(uint32_t size,uint8_t fill){
 uintptr_t a=((uintptr_t)rom_cursor+63u)&~(uintptr_t)63u;uint32_t i;
 if(a>(uintptr_t)rom_end||size>(uintptr_t)rom_end-a)return NULL;
 rom_cursor=(uint8_t*)(a+size);
 for(i=0;i<size;i++)((uint8_t*)a)[i]=fill;
 return (uint8_t*)a;
}

void zz_arcade_init(uint32_t rate,uint32_t sclock,uint32_t sintf,
                    uint32_t oclock,uint8_t op7,uint8_t *begin,uint8_t *end){
 unsigned i;
 out_rate=rate?rate:44100;sega_clock_hz=sclock;oki_clock_hz=oclock;
 rom_cursor=begin;rom_end=end;
 sega_rom.ptr=oki_rom.ptr=NULL;sega_rom.size=oki_rom.size=0;
 sega_bankshift=sintf&0xffu;
 sega_bankmask=(sintf>>8)&0xffu;
 if(!sega_bankshift||sega_bankshift>20u)sega_bankshift=12u;
 if(!sega_bankmask)sega_bankmask=0x70u;
 sega_phase=oki_phase=0;sega_left=sega_right=oki_mono=0;
 sega_step=sclock?(((uint64_t)sclock<<32)/(128u*out_rate)):0;
 oki_sample_step=oclock?(((uint64_t)oclock<<32)/((op7?132u:165u)*(uint64_t)out_rate)):0;
 pending_sample=-1;
 for(i=0;i<16;i++){
   sg[i].addr=0;sg[i].loop=0;sg[i].end=0;sg[i].freq=0;
   sg[i].vol_l=sg[i].vol_r=0;sg[i].ctrl=1;
 }
 for(i=0;i<4;i++){
  ok[i].start=ok[i].nibble=ok[i].total=0;
  ok[i].signal=0;ok[i].step=0;ok[i].volume=0;ok[i].active=0;
 }
}

int zz_arcade_rom_block(uint8_t type,uint32_t rom_size,uint32_t start,
                        const uint8_t *data,uint32_t size){
 pcmrom_t *r=(type==0x80u)?&sega_rom:((type==0x8bu)?&oki_rom:NULL);
 uint32_t i;
 if(!r||!data||!rom_size||rom_size>ARCADE_ROM_MAX||start>rom_size||size>rom_size-start)return 0;
 if(!r->ptr){
  r->ptr=zz_arcade_alloc_rom(rom_size,type==0x80u?0x80u:0xffu);
  if(!r->ptr)return 0;
  r->size=rom_size;
 }else if(rom_size!=r->size)return 0;
 for(i=0;i<size;i++)r->ptr[start+i]=data[i];
 return 1;
}

void zz_arcade_sega_write(uint16_t offset,uint8_t val){
 sgvoice_t *v;unsigned ch,b;
 if(offset>=0x100u||!sega_clock_hz)return;
 ch=(offset&0x7fu)>>3;b=offset&7u;
 v=&sg[ch];
 if(offset&0x80u){
  if(b==4u)v->addr=(v->addr&0xff00ffu)|((uint32_t)val<<8);
  else if(b==5u)v->addr=(v->addr&0x00ffffu)|((uint32_t)val<<16);
  else if(b==6u){v->ctrl=val;if(val&1u)v->addr&=0xffff00u;}
 }else{
  if(b==2u)v->vol_l=val&0x7fu;
  else if(b==3u)v->vol_r=val&0x7fu;
  else if(b==4u)v->loop=(uint16_t)((v->loop&0xff00u)|val);
  else if(b==5u)v->loop=(uint16_t)((v->loop&0x00ffu)|((uint16_t)val<<8));
  else if(b==6u)v->end=val;
  else if(b==7u)v->freq=val;
 }
}

static void sega_tick(void){
 int32_t l=0,r=0;unsigned i;
 if(!sega_rom.ptr){sega_left=sega_right=0;return;}
 for(i=0;i<16;i++){
  sgvoice_t *v=&sg[i];uint32_t bank,addr;int32_t s;
  if(v->ctrl&1u)continue;
  if((v->addr>>16u)==((v->end+1u)&0xffu)){
   if(v->ctrl&2u){v->ctrl|=1u;continue;}
   v->addr=(uint32_t)v->loop<<8u;
  }
  bank=((uint32_t)(v->ctrl&sega_bankmask))<<sega_bankshift;
  addr=bank+(v->addr>>8u);
  s=(int32_t)rom_byte(&sega_rom,addr)-128;
  l+=s*v->vol_l;r+=s*v->vol_r;
  v->addr=(v->addr+v->freq)&0xffffffu;
 }
 /* 16 voices would clip readily at full volume. Expected gain near the
  * standard libvgm source balance; final player still saturates. */
 sega_left=(l>32767)?32767:((l< -32768)?-32768:(int16_t)l);
 sega_right=(r>32767)?32767:((r< -32768)?-32768:(int16_t)r);
}

void zz_arcade_oki_write(uint8_t reg,uint8_t cmd){
 unsigned i;
 if(!oki_clock_hz||reg!=0u)return;
 if(pending_sample>=0){
  unsigned voices=(cmd>>4u)&15u;
  for(i=0;i<4;i++)if((voices&(1u<<i))&&!ok[i].active&&oki_rom.ptr){
   uint32_t base=(uint32_t)pending_sample*8u;
   uint32_t start=((uint32_t)rom_byte(&oki_rom,base)<<16u)|((uint32_t)rom_byte(&oki_rom,base+1u)<<8u)|rom_byte(&oki_rom,base+2u);
   uint32_t stop=((uint32_t)rom_byte(&oki_rom,base+3u)<<16u)|((uint32_t)rom_byte(&oki_rom,base+4u)<<8u)|rom_byte(&oki_rom,base+5u);
   start&=0x3ffffu;stop&=0x3ffffu;
   if(start<=stop&&stop<oki_rom.size){
    ok[i].active=1;ok[i].start=start;ok[i].nibble=0;
    ok[i].total=2u*(stop-start+1u);ok[i].signal= -2;ok[i].step=0;
    ok[i].volume=oki_vol[cmd&15u];
   }
  }
  pending_sample=-1;
 }else if(cmd&0x80u)pending_sample=cmd&0x7fu;
 else {unsigned mask=(cmd>>3u)&15u;for(i=0;i<4;i++)if(mask&(1u<<i))ok[i].active=0;}
}

static void oki_tick(void){
 int32_t mix=0;unsigned i;
 for(i=0;i<4;i++){
  okvoice_t *v=&ok[i];uint32_t byte_addr;uint8_t nib;
  int32_t d,step,signal;int ind;
  if(!v->active)continue;
  byte_addr=v->start+(v->nibble>>1u);
  if(byte_addr>=oki_rom.size){v->active=0;continue;}
  nib=rom_byte(&oki_rom,byte_addr);
  nib=(v->nibble&1u)?(nib&15u):(nib>>4u);
  step=oki_step[(unsigned)v->step];
  d=step>>3u;
  if(nib&1u)d+=step>>2u;
  if(nib&2u)d+=step>>1u;
  if(nib&4u)d+=step;
  signal=v->signal+((nib&8u)?-d:d);
  if(signal>2047)signal=2047;
  else if(signal< -2048)signal= -2048;
  v->signal=signal;
  ind=(int)v->step+oki_shift[nib&7u];
  if(ind<0)ind=0;else if(ind>48)ind=48;
  v->step=(int8_t)ind;
  mix+=signal*v->volume/2; /* 12-bit signal to useful 16-bit range */
  if(++v->nibble>=v->total)v->active=0;
 }
 oki_mono=(mix>32767)?32767:((mix< -32768)?-32768:(int16_t)mix);
}

void zz_arcade_render(int16_t *sega,int16_t *oki,uint32_t n){
 uint32_t i;
 for(i=0;i<n;i++){
  if(sega_step){sega_phase+=sega_step;while(sega_phase>=((uint64_t)1<<32)){sega_phase-=((uint64_t)1<<32);sega_tick();}}
  if(oki_sample_step){oki_phase+=oki_sample_step;while(oki_phase>=((uint64_t)1<<32)){oki_phase-=((uint64_t)1<<32);oki_tick();}}
  sega[2u*i]=sega_left;sega[2u*i+1u]=sega_right;
  oki[2u*i]=oki_mono;oki[2u*i+1u]=oki_mono;
 }
}
