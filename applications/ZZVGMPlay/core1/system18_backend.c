/* ZZVGMPlay System 18 integration. Copyright (c) Xanxi 2026.
 * Chip engines and sample-rate conversion are the pinned libvgm sources.
 * The 256 KiB working pool lives in the existing Core1 private arena.
 */
#include "system18_backend.h"
#include <string.h>
#define SNDDEV_YM2612
#include "../third_party/libvgm-system18/emu/cores/fmopn.h"
#include "../third_party/libvgm-system18/emu/Resampler.h"

void *zz_s18_rf_create(uint32_t clock);
void zz_s18_rf_reset(void *chip);
void zz_s18_rf_reg(void *chip, uint8_t reg, uint8_t value);
void zz_s18_rf_mem(void *chip, uint16_t offset, uint8_t value);
void zz_s18_rf_block(void *chip, uint32_t offset, uint32_t length, const uint8_t *data);
void zz_s18_rf_update(void *chip, uint32_t frames, DEV_SMPL **outputs);

static uint8_t *pool_base;
static size_t pool_size, pool_pos;
static void *fm[2], *rf;
static unsigned fm_count;
static uint8_t memory_bank;
static RESMPL_STATE resamplers[3];

void *zz_s18_malloc(size_t bytes) {
    size_t at=(pool_pos+63u)&~(size_t)63u;
    if (!pool_base || at>pool_size || bytes>pool_size-at) return NULL;
    pool_pos=at+bytes;
    return pool_base+at;
}
void *zz_s18_calloc(size_t count, size_t bytes) {
    void *p;
    if (count && bytes>SIZE_MAX/count) return NULL;
    bytes*=count;
    p=zz_s18_malloc(bytes);
    if (p) memset(p,0,bytes);
    return p;
}
void zz_s18_free(void *p) {(void)p;}
size_t zz_s18_pool_used(void) {return pool_pos;}

static void connect_resampler(unsigned i, void *chip, DEVFUNC_UPDATE update,
                              uint32_t native_rate, uint32_t output_rate) {
    RESMPL_STATE *r=&resamplers[i];
    memset(r,0,sizeof(*r));
    r->smpRateSrc=native_rate;
    r->StreamUpdate=update;
    r->su_DataPtr=chip;
    Resmpl_SetVals(r,RSMODE_LINEAR,0x100,output_rate);
    Resmpl_Init(r);
}

int zz_s18_init(uint32_t fm_clock, unsigned count, uint32_t pcm_clock,
               uint32_t rate, void *pool, size_t bytes) {
    unsigned i;
    uint32_t fm_rate=fm_clock/144u, pcm_rate=pcm_clock/384u;
    if (!pool || bytes<ZZ_S18_POOL_BYTES || !pcm_rate || !rate || count>2u ||
        (count && !fm_rate) || pcm_rate>rate*3u || (count && fm_rate>rate*3u))
        return 0;
    pool_base=pool;pool_size=bytes;pool_pos=0;
    fm_count=count;fm[0]=fm[1]=NULL;rf=NULL;memory_bank=0;
    for (i=0;i<count;i++) {
        fm[i]=ym2612_init(NULL,fm_clock,fm_rate,NULL,NULL);
        if (!fm[i]) return 0;
        ym2612_set_options(fm[i],0); /* GPGX default, pseudo-stereo disabled. */
        ym2612_reset_chip(fm[i]);
        connect_resampler(i,fm[i],ym2612_update_one,fm_rate,rate);
    }
    rf=zz_s18_rf_create(pcm_clock);
    if (!rf) return 0;
    zz_s18_rf_reset(rf);
    connect_resampler(2,rf,zz_s18_rf_update,pcm_rate,rate);
    return pool_pos<=pool_size;
}

void zz_s18_fm_write(unsigned chip, unsigned port, uint8_t reg, uint8_t value) {
    unsigned a=(port&1u)*2u;
    if (chip>=fm_count) return;
    ym2612_write(fm[chip],(UINT8)a,reg);
    ym2612_write(fm[chip],(UINT8)(a+1u),value);
}
void zz_s18_rf_write(uint8_t reg,uint8_t value) {
    if (!rf) return;
    if (reg==7 && !(value&0x40u)) memory_bank=value&15u;
    zz_s18_rf_reg(rf,reg,value);
}
uint32_t zz_s18_rf_bank(void) {return (uint32_t)memory_bank<<12;}
void zz_s18_rf_byte(uint16_t offset,uint8_t value) {
    if (rf) zz_s18_rf_mem(rf,offset,value);
}
void zz_s18_rf_ram(uint32_t offset,const uint8_t *data,uint32_t bytes) {
    if (rf && data) zz_s18_rf_block(rf,offset,bytes,data);
}
void zz_s18_render(int32_t *a,int32_t *b,int32_t *c,uint32_t frames) {
    int32_t *outputs[3]={a,b,c};unsigned i;
    if (frames>256u) return;
    for(i=0;i<3;i++) {
        memset(outputs[i],0,frames*2u*sizeof(int32_t));
        if ((i<fm_count) || (i==2 && rf))
            Resmpl_Execute(&resamplers[i],frames,(WAVE_32BS *)outputs[i]);
    }
}
