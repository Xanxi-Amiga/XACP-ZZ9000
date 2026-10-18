/*
 * ZZVGMPlay v1.0 DACSTREAM - Core1 multi-chip VGM/VGZ player
 * XACP 1.8 target; shared-memory layout remains the validated ZZReSID layout.
 *
 * A0r4 chips:
 *   YM3812 / OPL2       (Nuked-OPL3-fast)
 *   YMF262  / OPL3      (Nuked-OPL3-fast)
 *   SN76489 / T6W28-ish (native lightweight Core1 backend)
 *   YM2612 / YM3438     (GME GENS fast backend)
 *   YM2151 / YM2164     (Jarek/FBNeo fast backend; same core as ZZRastan)
 *   YM2413 / OPLL       (emu2413 v1.6.3; also VRC7 mode when flagged)
 *   AY-3-8910 / YM2149  (native lightweight Core1 backend)
 *   Game Boy DMG         (VGMPlay/MAME legacy core)
 *   NES 2A03/2A07 APU   (VGMPlay/Nofrendo legacy core, incl. C2 DPCM RAM)
 *   HuC6280 PSG          (Ootake/VGMPlay core; PC Engine/TurboGrafx-16)
 *
 * VGZ is inflated on Core1 with Mark Adler's puff() (raw deflate) after parsing
 * the gzip wrapper. Compressed source is staged at the top of the private arena,
 * leaving the base free for the uncompressed VGM.
 */

#include <stdint.h>
#include <stddef.h>
#include "zzvgm_shared.h"
#include "opl3.h"
#include "gens2612_wrap.h"
#include "jarek2151_wrap.h"
#include "emu2413_wrap.h"
#include "gb_dmg_wrap.h"
#include "nes_apu_wrap.h"
#include "huc6280_wrap.h"
#include "sn76489.h"
#include "ay8910.h"
#include "ay2149_ref.h"
#include "gzip_puff.h"
#include "arcade_pcm.h"
#include "neogeo2610.h"
#include "qsound_pcm.h"
#include "es5503_zz.h"
#include "k053260_zz.h"
#include "k054539_zz.h"
#include "k051649_zz.h"
#include "system18_backend.h"

static inline uint32_t ARM32(uint32_t x){return __builtin_bswap32(x);} 
#ifdef ZZVGM_S18_HOST
static inline void dsb(void){}
#else
static inline void dsb(void){__asm__ volatile("dsb":::"memory");}
#endif
static inline int16_t sat16(int64_t x){if(x>32767)x=32767;else if(x<-32768)x=-32768;return(int16_t)x;}

/* System 18 output-only gain. Apply to the complete stereo mix so the
 * relative levels of YM2612 #0/#1 and RF5C68 are not modified. The
 * general VGM chip-volume normalizer and non-RF5C68 tracks are untouched. */
#define ZZ_S18_OUTPUT_GAIN 2

/* Host-only metering: compiled out of the real ARM firmware. */
#ifdef ZZVGM_S18_HOST
static uint64_t g_s18_host_original_hash=UINT64_C(14695981039346656037);
static uint64_t g_s18_host_clipped_channels;
static uint64_t g_s18_host_pre_gain_clipped_channels;
static uint32_t g_s18_host_preclip_peak;
static inline void s18_host_hash_sample(int16_t v){
 uint16_t u=(uint16_t)v;
 g_s18_host_original_hash^=u&255u;
 g_s18_host_original_hash*=UINT64_C(1099511628211);
 g_s18_host_original_hash^=u>>8;
 g_s18_host_original_hash*=UINT64_C(1099511628211);
}
#endif

static volatile uint32_t * const g_shared=(volatile uint32_t *)ZZVGM_SHARED_ARM_ADDR;
static volatile uint32_t * const g_ring=(volatile uint32_t *)ZZVGM_PCM_ARM_ADDR;
static volatile uint32_t * const g_upload=(volatile uint32_t *)ZZVGM_UPLOAD_ARM_ADDR;
static uint8_t * const g_arena=(uint8_t *)ZZVGM_ARENA_BASE;

static inline uint32_t sh_read(unsigned i){return ARM32(g_shared[i]);}
static inline void sh_write(unsigned i,uint32_t v){g_shared[i]=ARM32(v);dsb();}
static inline uint32_t le32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);} 
static inline uint16_t le16(const uint8_t*p){return(uint16_t)(p[0]|((uint16_t)p[1]<<8));}

/* -------------------- MMU: same policy as A0r3/ZZReSID -------------------- */
#ifndef ZZVGM_S18_HOST
static uint32_t l1_table[4096] __attribute__((aligned(16384)));
#define SEC_TYPE 0x2u
#define SEC_AP_RW (3u<<10)
#define SEC_DOMAIN0 (0u<<5)
#define MT_NORMAL_WBWA ((1u<<12)|(1u<<3)|(1u<<2))
#define MT_NORMAL_NC ((1u<<12)|(0u<<3)|(0u<<2))
#define MT_DEVICE ((0u<<12)|(0u<<3)|(1u<<2))
static uint32_t make_section(uint32_t pa_mb,uint32_t mt){return(pa_mb<<20)|SEC_TYPE|SEC_AP_RW|SEC_DOMAIN0|mt;}
static void mmu_init_zzvgm(void){uint32_t i;{uint32_t s;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(s));s&=~(1u<<0);s&=~(1u<<2);s&=~(1u<<12);__asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(s):"memory");__asm__ volatile("dsb":::"memory");__asm__ volatile("isb":::"memory");}for(i=0;i<4096;i++)l1_table[i]=make_section(i,MT_DEVICE);l1_table[0x042]=make_section(0x042,MT_NORMAL_WBWA);l1_table[0x043]=make_section(0x043,MT_NORMAL_WBWA);l1_table[0x05A]=make_section(0x05A,MT_NORMAL_NC);for(i=0x300;i<=0x3F7;i++)l1_table[i]=make_section(i,MT_NORMAL_WBWA);__asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(1u));__asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(0u));{uint32_t t=((uint32_t)l1_table)|0x6Bu;__asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(t));}__asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u));__asm__ volatile("mcr p15,0,%0,c7,c5,6"::"r"(0u));__asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u));{uint32_t w,s;for(w=0;w<4;w++)for(s=0;s<256;s++){uint32_t sw=(w<<30)|(s<<5);__asm__ volatile("mcr p15,0,%0,c7,c6,2"::"r"(sw):"memory");}__asm__ volatile("dsb":::"memory");__asm__ volatile("isb":::"memory");}{uint32_t s;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(s));s|=(1u<<0)|(1u<<2)|(1u<<12)|(1u<<11);s&=~(1u<<1);__asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(s));__asm__ volatile("isb":::"memory");}}
static void cache_clean_invalidate_all(void){uint32_t w,s;__asm__ volatile("dsb":::"memory");for(w=0;w<4;w++)for(s=0;s<256;s++){uint32_t sw=(w<<30)|(s<<5);__asm__ volatile("mcr p15,0,%0,c7,c14,2"::"r"(sw):"memory");}__asm__ volatile("dsb":::"memory");__asm__ volatile("isb":::"memory");}

#endif /* !ZZVGM_S18_HOST */
/* -------------------- player state -------------------- */
static uint8_t *g_vgm;
static uint8_t *g_upload_dst;
static uint32_t g_upload_len,g_len,g_uploaded,g_pos,g_data,g_loop,g_wait;
static uint32_t g_write_pos,g_play_samples,g_loops,g_writes,g_done,g_error,g_render_calls;
static uint32_t g_chip_mask,g_file_flags,g_sample_rate;
static uint8_t g_system18;
static uint32_t g_vol_opn2,g_vol_rf;
/* Q8 libvgm outputs: three stereo stems, maximum 256 frames. Not on stack. */
static int32_t g_s18_fm0[512],g_s18_fm1[512],g_s18_pcm[512];
static uint32_t g_sn_clock,g_opn_clock,g_opm_clock,g_opll_clock,g_ay_clock;
static uint32_t g_gb_clock,g_nes_clock,g_pce_clock;
static uint32_t g_sega_clock,g_oki_clock,g_neo_clock,g_qsound_clock,g_es5503_clock,g_k053260_clock,g_k054539_clock;
static uint8_t *g_pcm_bank; static uint32_t g_pcm_bank_len,g_pcm_pos;

/* YM2612 DAC is intentionally mixed outside the GME GENS FM core.
 * GME's VGM player does the same: its GENS backend does not render reg 0x2A.
 * Classic GENS scale is (unsigned_sample - 0x80) << 5. */
static int32_t g_ym2612_dac_sample;
static uint8_t g_ym2612_dac_enabled;
static uint8_t g_ym2612_dac_left,g_ym2612_dac_right;

static opl3_chip g_opl;
static zz_sn76489_t g_sn;
static zz_ay8910_t g_ay;
static zz_ay2149_t g_ay_ref;
static uint8_t g_use_ay_ref;
static zz_es5503_t g_es5503;
static zz_k053260_t g_k053260;
static int16_t g_k053260_pcm[512];
static zz_k054539_t g_k054539[2];
static uint32_t g_k054539_count;
static int16_t g_k054539_pcm[2][512];
static zz_scc g_scc[2];
static uint32_t g_scc_count;
static int16_t g_scc_pcm[2][512];
static int16_t g_es5503_pcm[512];

/* VGM chip IDs used by the extended chip-volume list. */
#define VGMCHIP_SN76489 0x00u
#define VGMCHIP_YM2413  0x01u
#define VGMCHIP_YM2612  0x02u
#define VGMCHIP_RF5C68  0x05u
#define VGMCHIP_YM2151  0x03u
#define VGMCHIP_SEGAPCM 0x04u
#define VGMCHIP_OKI6295 0x17u
#define VGMCHIP_YM2610  0x08u
#define VGMCHIP_QSOUND  0x1fu
#define VGMCHIP_ES5503  0x24u
#define VGMCHIP_K053260 0x1Du
#define VGMCHIP_K054539 0x1Au
#define VGMCHIP_K051649 0x19u
#define VGMCHIP_YM3812  0x09u
#define VGMCHIP_YMF262  0x0Cu
#define VGMCHIP_AY8910  0x12u
#define VGMCHIP_GB_DMG  0x13u
#define VGMCHIP_NES_APU 0x14u
#define VGMCHIP_HUC6280 0x1Bu



/* -------------------- VGM DAC Stream Control (0x90..0x95) --------------------
 * Stream timing runs in the same 44.1 kHz sample domain as the VGM command
 * stream.  Writes are injected between render chunks so they take effect at
 * the correct sample boundary instead of being collapsed at the beginning of
 * an AHI buffer.
 *
 * Data banks 0x00..0x3F are built from all uncompressed 0x67 blocks seen so
 * far.  Multiple blocks append to one logical bank, matching the VGM spec and
 * making 0x95 fast calls possible without copying the VGM payload.
 */
#define ZZ_PCM_BANK_COUNT 0x40u
#define ZZ_PCM_BLOCK_MAX  512u
#define ZZ_DAC_STREAM_COUNT 0xFFu
#define ZZ_NO_BLOCK 0xFFFFu

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t bank_ofs;
    uint16_t next;
    uint8_t bank;
} zz_pcm_block_t;

typedef struct {
    uint8_t configured;
    uint8_t playing;
    uint8_t chip_type;
    uint8_t chip2;
    uint8_t port;
    uint8_t reg;
    uint8_t bank;
    uint8_t step_size;
    uint8_t step_base;
    uint8_t cmd_size;
    uint8_t loop;
    uint8_t reverse;
    uint32_t frequency;
    uint32_t phase;
    uint32_t data_start;
    uint32_t data_step;
    uint32_t cmds_total;
    uint32_t cmds_left;
    uint32_t real_pos;
    uint16_t cache_block;
} zz_dac_stream_t;

static zz_pcm_block_t g_pcm_blocks[ZZ_PCM_BLOCK_MAX];
static uint16_t g_pcm_block_used;
static uint16_t g_bank_first[ZZ_PCM_BANK_COUNT],g_bank_last[ZZ_PCM_BANK_COUNT],g_bank_blocks[ZZ_PCM_BANK_COUNT];
static uint32_t g_bank_len[ZZ_PCM_BANK_COUNT];
static zz_dac_stream_t g_dac_stream[ZZ_DAC_STREAM_COUNT];
static uint8_t g_pce_selected_channel;

static void pcm_banks_reset(void){
    unsigned i;
    g_pcm_block_used=0;
    for(i=0;i<ZZ_PCM_BANK_COUNT;i++){
        g_bank_first[i]=g_bank_last[i]=ZZ_NO_BLOCK;
        g_bank_blocks[i]=0;
        g_bank_len[i]=0;
    }
}

static int pcm_bank_add(uint8_t bank,const uint8_t *data,uint32_t size){
    uint16_t idx;
    if(bank>=ZZ_PCM_BANK_COUNT||!data||!size||g_pcm_block_used>=ZZ_PCM_BLOCK_MAX)return 0;
    idx=g_pcm_block_used++;
    g_pcm_blocks[idx].data=data;
    g_pcm_blocks[idx].size=size;
    g_pcm_blocks[idx].bank_ofs=g_bank_len[bank];
    g_pcm_blocks[idx].next=ZZ_NO_BLOCK;
    g_pcm_blocks[idx].bank=bank;
    if(g_bank_last[bank]!=ZZ_NO_BLOCK)g_pcm_blocks[g_bank_last[bank]].next=idx;
    else g_bank_first[bank]=idx;
    g_bank_last[bank]=idx;
    g_bank_blocks[bank]++;
    g_bank_len[bank]+=size;
    return 1;
}

static uint16_t pcm_bank_find_block(uint8_t bank,uint32_t pos,uint16_t hint){
    uint16_t idx;
    if(bank>=ZZ_PCM_BANK_COUNT||pos>=g_bank_len[bank])return ZZ_NO_BLOCK;
    idx=hint;
    if(idx!=ZZ_NO_BLOCK&&idx<g_pcm_block_used&&g_pcm_blocks[idx].bank==bank&&
       pos>=g_pcm_blocks[idx].bank_ofs&&pos-g_pcm_blocks[idx].bank_ofs<g_pcm_blocks[idx].size)return idx;
    idx=g_bank_first[bank];
    while(idx!=ZZ_NO_BLOCK){
        zz_pcm_block_t *b=&g_pcm_blocks[idx];
        if(pos>=b->bank_ofs&&pos-b->bank_ofs<b->size)return idx;
        idx=b->next;
    }
    return ZZ_NO_BLOCK;
}

static int pcm_bank_read(uint8_t bank,uint32_t pos,uint8_t *dst,uint32_t len,uint16_t *cache){
    uint32_t done=0;
    uint16_t idx=cache?*cache:ZZ_NO_BLOCK;
    if(!dst||bank>=ZZ_PCM_BANK_COUNT||pos>g_bank_len[bank]||len>g_bank_len[bank]-pos)return 0;
    while(done<len){
        zz_pcm_block_t *b;
        uint32_t bo,n;
        idx=pcm_bank_find_block(bank,pos+done,idx);
        if(idx==ZZ_NO_BLOCK)return 0;
        b=&g_pcm_blocks[idx]; bo=(pos+done)-b->bank_ofs; n=b->size-bo;
        if(n>len-done)n=len-done;
        {uint32_t i;for(i=0;i<n;i++)dst[done+i]=b->data[bo+i];}
        done+=n;
        if(done<len)idx=b->next;
    }
    if(cache)*cache=idx;
    return 1;
}

static int pcm_bank_block_info(uint8_t bank,uint16_t block_id,uint32_t *ofs,uint32_t *len){
    uint16_t idx,n=0;
    if(bank>=ZZ_PCM_BANK_COUNT)return 0;
    idx=g_bank_first[bank];
    while(idx!=ZZ_NO_BLOCK){
        if(n==block_id){if(ofs)*ofs=g_pcm_blocks[idx].bank_ofs;if(len)*len=g_pcm_blocks[idx].size;return 1;}
        n++;idx=g_pcm_blocks[idx].next;
    }
    return 0;
}

static void dac_streams_reset(void){
    unsigned i;
    for(i=0;i<ZZ_DAC_STREAM_COUNT;i++){
        zz_dac_stream_t *s=&g_dac_stream[i];
        s->configured=s->playing=0;s->chip_type=0xFF;s->chip2=0;s->port=s->reg=0;
        s->bank=0xFF;s->step_size=1;s->step_base=0;s->cmd_size=1;s->loop=s->reverse=0;
        s->frequency=s->phase=s->data_start=0;s->data_step=1;s->cmds_total=s->cmds_left=s->real_pos=0;
        s->cache_block=ZZ_NO_BLOCK;
    }
}

static uint8_t dac_cmd_size(uint8_t chip,uint16_t cmd){
    if(chip==VGMCHIP_SN76489)return(cmd&0x0010u)?1u:2u;
    return 1u;
}

static int dac_chip_supported(uint8_t chip){
    switch(chip){
    case VGMCHIP_SN76489:case VGMCHIP_YM2413:case VGMCHIP_YM2612:case VGMCHIP_YM2151:
    case VGMCHIP_YM3812:case VGMCHIP_YMF262:case VGMCHIP_AY8910:case VGMCHIP_GB_DMG:
    case VGMCHIP_NES_APU:case VGMCHIP_HUC6280:case VGMCHIP_SEGAPCM:case VGMCHIP_OKI6295:case VGMCHIP_YM2610:return 1;
    default:return 0;
    }
}

static void dac_stream_setup(uint8_t id,uint8_t type,uint8_t port,uint8_t reg){
    zz_dac_stream_t *s;uint16_t cmd;
    if(id==0xFFu)return;
    s=&g_dac_stream[id];
    s->chip2=(type&0x80u)?1u:0u;s->chip_type=type&0x7Fu;s->port=port;s->reg=reg;
    cmd=((uint16_t)port<<8)|reg;
    s->cmd_size=dac_cmd_size(s->chip_type,cmd);
    s->configured=(uint8_t)(!s->chip2&&dac_chip_supported(s->chip_type));
    s->playing=0;s->phase=0;s->cache_block=ZZ_NO_BLOCK;
    if(!s->step_size)s->step_size=1;
    s->data_step=(uint32_t)s->cmd_size*s->step_size;
}

static void dac_stream_set_data(uint8_t id,uint8_t bank,uint8_t step,uint8_t base){
    zz_dac_stream_t *s;if(id==0xFFu)return;s=&g_dac_stream[id];
    s->bank=bank;s->step_size=step?step:1u;s->step_base=base;
    s->data_step=(uint32_t)s->cmd_size*s->step_size;s->cache_block=ZZ_NO_BLOCK;
}
static void dac_stream_set_freq(uint8_t id,uint32_t freq){if(id!=0xFFu){g_dac_stream[id].frequency=freq;g_dac_stream[id].phase=0;}}
static void dac_stream_stop_one(uint8_t id){if(id!=0xFFu)g_dac_stream[id].playing=0;}
static void dac_stream_stop(uint8_t id){unsigned i;if(id==0xFFu){for(i=0;i<ZZ_DAC_STREAM_COUNT;i++)g_dac_stream[i].playing=0;}else dac_stream_stop_one(id);}

static void dac_stream_start(uint8_t id,uint32_t pos,uint8_t mode,uint32_t length){
    zz_dac_stream_t *s;uint32_t basebytes,cmds=0,origin;
    if(id==0xFFu)return;
    s=&g_dac_stream[id];
    if(!s->configured||s->bank>=ZZ_PCM_BANK_COUNT||!s->data_step||!s->frequency)return;
    basebytes=(uint32_t)s->cmd_size*s->step_base;
    if(pos!=0xFFFFFFFFu){
        s->data_start=pos+basebytes;
        if(s->data_start>g_bank_len[s->bank])s->data_start=g_bank_len[s->bank];
    }
    switch(mode&0x0Fu){
    case 0x00:cmds=s->cmds_total;break;
    case 0x01:cmds=length;break;
    case 0x02:cmds=(uint32_t)(((uint64_t)1000u*(uint64_t)length)/(uint64_t)s->frequency);break;
    case 0x03:
        origin=(s->data_start>=basebytes)?s->data_start-basebytes:0;
        cmds=(origin<g_bank_len[s->bank])?(g_bank_len[s->bank]-origin)/s->data_step:0;break;
    case 0x0F:cmds=length/s->data_step;break;
    default:cmds=0;break;
    }
    s->cmds_total=cmds;s->cmds_left=cmds;s->reverse=(mode&0x10u)?1u:0u;s->loop=(mode&0x80u)?1u:0u;
    s->real_pos=(s->reverse&&cmds)?(cmds-1u)*s->data_step:0u;s->phase=0;s->cache_block=ZZ_NO_BLOCK;
    s->playing=cmds?1u:0u;
}

static void dac_stream_fast_start(uint8_t id,uint16_t block,uint8_t flags){
    zz_dac_stream_t *s;uint32_t ofs,len;uint8_t mode;
    if(id==0xFFu)return;
    s=&g_dac_stream[id];
    if(s->bank>=ZZ_PCM_BANK_COUNT)return;
    if(!pcm_bank_block_info(s->bank,block,&ofs,&len))return;
    mode=0x0Fu;if(flags&0x10u)mode|=0x10u;if(flags&0x01u)mode|=0x80u;
    dac_stream_start(id,ofs,mode,len);
}

static void dac_write_ym2612(unsigned port,uint8_t reg,uint8_t val){
    if(g_system18){zz_s18_fm_write(0u,port,reg,val);return;}
    if(port==0u&&reg==0x2Au)g_ym2612_dac_sample=((int32_t)val-0x80)<<5;
    else{
        if(port==0u&&reg==0x2Bu)g_ym2612_dac_enabled=(val&0x80u)?1u:0u;
        if(port==1u&&reg==0xB6u){g_ym2612_dac_left=(val&0x80u)?1u:0u;g_ym2612_dac_right=(val&0x40u)?1u:0u;}
        zz_gens2612_write(port,reg,val);
    }
}

static void dac_stream_send(zz_dac_stream_t *s){
    uint8_t d[2]={0,0};uint32_t p;
    if(!s->playing||!s->configured||s->bank>=ZZ_PCM_BANK_COUNT)return;
    p=s->data_start+s->real_pos;
    if(!pcm_bank_read(s->bank,p,d,s->cmd_size,&s->cache_block)){s->playing=0;return;}
    switch(s->chip_type){
    case VGMCHIP_SN76489:
        if(s->cmd_size==1u)zz_sn76489_write(&g_sn,(uint8_t)((s->reg&0xF0u)|(d[0]&0x0Fu)));
        else{uint8_t c=(uint8_t)((s->reg&0xF0u)|(d[0]&0x0Fu));uint8_t q=(uint8_t)(((d[1]&3u)<<4)|((d[0]&0xF0u)>>4));zz_sn76489_write(&g_sn,c);zz_sn76489_write(&g_sn,q);}break;
    case VGMCHIP_YM2413:zz_emu2413_write(s->reg,d[0]);break;
    case VGMCHIP_YM2612:dac_write_ym2612(s->port&1u,s->reg,d[0]);break;
    case VGMCHIP_YM2151:zz_jarek2151_write(s->reg,d[0]);break;
    case VGMCHIP_YM3812:OPL3_WriteRegBuffered(&g_opl,s->reg,d[0]);break;
    case VGMCHIP_YMF262:OPL3_WriteRegBuffered(&g_opl,(uint16_t)(((s->port&1u)?0x100u:0u)|s->reg),d[0]);break;
    case VGMCHIP_AY8910:if(g_use_ay_ref)zz_ay2149_write(&g_ay_ref,s->reg,d[0]);else zz_ay8910_write(&g_ay,s->reg,d[0]);break;
    case VGMCHIP_SEGAPCM:zz_arcade_sega_write((uint16_t)(((uint16_t)s->port<<8u)|s->reg),d[0]);break;
    case VGMCHIP_OKI6295:zz_arcade_oki_write(s->reg,d[0]);break;
    case VGMCHIP_YM2610:zz_neo_write(s->port&1u,s->reg,d[0]);break;
    case VGMCHIP_GB_DMG:zz_gb_dmg_write(s->reg,d[0]);break;
    case VGMCHIP_NES_APU:zz_nes_apu_write(s->reg,d[0]);break;
    case VGMCHIP_HUC6280:{uint8_t old=g_pce_selected_channel;if(s->port!=0xFFu){zz_huc6280_write((uint8_t)(s->reg>>4),s->port);g_pce_selected_channel=s->port;}zz_huc6280_write((uint8_t)(s->reg&0x0Fu),d[0]);if(s->port!=0xFFu&&old!=s->port){zz_huc6280_write((uint8_t)(s->reg>>4),old);g_pce_selected_channel=old;}}break;
    default:break;
    }
    g_writes++;
    if(s->cmds_left)s->cmds_left--;
    if(!s->cmds_left){
        if(s->loop&&s->cmds_total){s->cmds_left=s->cmds_total;s->real_pos=s->reverse?(s->cmds_total-1u)*s->data_step:0u;s->cache_block=ZZ_NO_BLOCK;}
        else{s->playing=0;return;}
    }else if(s->reverse)s->real_pos-=s->data_step;else s->real_pos+=s->data_step;
}

static uint32_t dac_stream_next_event(uint32_t limit){
    uint32_t best=limit;unsigned i;
    for(i=0;i<ZZ_DAC_STREAM_COUNT;i++){
        zz_dac_stream_t *s=&g_dac_stream[i];uint32_t k;uint64_t rem;
        if(!s->playing||!s->configured||!s->frequency)continue;
        rem=(s->phase<g_sample_rate)?(uint64_t)(g_sample_rate-s->phase):1u;
        k=(uint32_t)((rem+s->frequency-1u)/s->frequency);if(!k)k=1;if(k<best)best=k;
    }
    return best?best:1u;
}

static void dac_stream_advance(uint32_t samples){
    unsigned i;
    for(i=0;i<ZZ_DAC_STREAM_COUNT;i++){
        zz_dac_stream_t *s=&g_dac_stream[i];uint64_t ph;
        if(!s->playing||!s->configured||!s->frequency)continue;
        ph=(uint64_t)s->phase+(uint64_t)s->frequency*samples;
        while(ph>=g_sample_rate&&s->playing){ph-=g_sample_rate;dac_stream_send(s);}
        s->phase=(uint32_t)ph;
    }
}

/* Keep new-console render scratch out of the Core1 stack. */
static int16_t g_gb_pcm[512],g_nes_pcm[512],g_pce_pcm[512],g_sega_pcm[512],g_oki_pcm[512],g_neo_pcm[512],g_neo_ay_pcm[512],g_qsound_pcm[512];

/* -------------------- libvgm-compatible chip volume model --------------------
 * Values are 8.8 fixed-point and mirror ValleyBell/libvgm _CHIP_VOLUME[] and
 * _PB_VOL_AMNT[] for the chips currently supported by ZZVGMPlay.
 *
 * Stage 1: each backend sample is multiplied by its chip volume.
 * Stage 2: the active-chip set is passed through libvgm's overall power-of-two
 *          normalizer (target window >0x180 and <=0x300).
 * Stage 3: VGM extra-header per-chip volume overrides are honoured when present.
 *
 * This deliberately replaces the old ad-hoc *2 gains in render_some().
 */
#define VOL_SN76489  0x080u
#define VOL_YM2413   0x200u
#define VOL_YM2612   0x100u
#define VOL_YM2151   0x100u
#define VOL_SEGAPCM  0x080u
#define VOL_OKI6295  0x100u
#define VOL_YM2610   0x100u
#define VOL_QSOUND   0x100u
#define VOL_ES5503   0x100u
#define VOL_K053260  0x100u
#define VOL_K054539  0x100u
#define VOL_K051649  0x100u /* libvgm adjusted default SCC volume */
#define VOL_OPL2     0x100u
#define VOL_OPL3     0x100u
#define VOL_AY8910   0x100u
#define VOL_GB_DMG   0x0C0u
#define VOL_NES_APU  0x100u
#define VOL_HUC6280  0x100u

#define PB_SN76489   0x100u
/* Mega Drive uses two sound generators with different backend PCM scales.
 * This relative correction is applied only for YM2612 + SN76489 pairs, after
 * the shared volume normalizer, and never to stand-alone SN (SMS/Game Gear).
 * 0x80 / 0x100 = -6.02 dB relative to the A05b Mega Drive SN level.
 */
#define MD_SN_RELATIVE_Q8 0x080u
/* Optional SMS YM2413 listening balance, AFTER libvgm normalisation.
 * Q8 0x100=original, 0x180=+3.52 dB, 0x1C0=+4.86 dB, 0x200=+6.02 dB.
 * This must not alter VRC7 or the validated Mega Drive SN/YM2612 balance. */
#define SMS_YM2413_LISTEN_GAIN_Q8 0x200u
#define PB_YM2413    0x080u
#define PB_YM2612    0x100u
#define PB_YM2151    0x100u
#define PB_SEGAPCM   0x100u
#define PB_OKI6295   0x100u
#define PB_YM2610    0x100u
#define PB_QSOUND    0x100u
#define PB_ES5503    0x100u
#define PB_K053260   0x100u
#define PB_K054539   0x100u
#define PB_K051649   0x100u
#define PB_OPL2      0x200u
#define PB_OPL3      0x200u
#define PB_AY8910    0x200u
#define PB_GB_DMG    0x400u
#define PB_NES_APU   0x200u
#define PB_HUC6280   0x100u

static uint32_t g_vol_sn,g_vol_opll,g_vol_opn,g_vol_opm,g_vol_opl,g_vol_ay;
static uint32_t g_vol_gb,g_vol_nes,g_vol_pce;
static uint32_t g_vol_sega,g_vol_oki,g_vol_neo,g_vol_qsound,g_vol_es5503,g_vol_k053260,g_vol_k054539,g_vol_k051649;

static inline uint32_t mul_q8u(uint32_t a,uint32_t b){return(a*b+0x80u)>>8;}
static inline int32_t mul_q8s(int32_t s,uint32_t v){return(int32_t)(((int64_t)s*(int64_t)v)>>8);}

static uint32_t xhdr_chip_volume_for(uint8_t chip_type,uint32_t defvol,unsigned instance){
 uint32_t xhofs,xhlen,volofs,count,i;
 if(g_data<0xC0u||g_len<0xC0u)return defvol;
 xhofs=le32(g_vgm+0xBC);
 if(!xhofs)return defvol;
 xhofs+=0xBCu;
 if(xhofs+4u>g_len)return defvol;
 xhlen=le32(g_vgm+xhofs);
 if(xhlen<0x0Cu||xhofs+0x0Cu>g_len)return defvol;
 volofs=le32(g_vgm+xhofs+0x08u);
 if(!volofs)return defvol;
 volofs+=xhofs+0x08u;
 if(volofs>=g_len)return defvol;
 count=g_vgm[volofs++];
 for(i=0;i<count;i++){
  uint8_t typ,flags;uint16_t data;
  if(volofs+4u>g_len)break;
  typ=g_vgm[volofs+0];flags=g_vgm[volofs+1];data=le16(g_vgm+volofs+2);volofs+=4u;
  /* VGM extra-header flags bit 0 selects the second instance. */
  if(typ==chip_type&&((flags&0x01u)==(instance&1u))){
   if(data&0x8000u)return mul_q8u(defvol,(uint32_t)(data&0x7FFFu));
   return data;
  }
 }
 return defvol;
}
static uint32_t xhdr_chip_volume(uint8_t chip_type,uint32_t defvol){
 return xhdr_chip_volume_for(chip_type,defvol,0);
}

static void scale_all_volumes_mul(uint32_t factor){
 g_vol_opn2*=factor;g_vol_rf*=factor;
 g_vol_sn*=factor;g_vol_opll*=factor;g_vol_opn*=factor;g_vol_opm*=factor;g_vol_opl*=factor;
 g_vol_ay*=factor;g_vol_gb*=factor;g_vol_nes*=factor;g_vol_pce*=factor;
 g_vol_sega*=factor;g_vol_oki*=factor;g_vol_neo*=factor;g_vol_qsound*=factor;g_vol_es5503*=factor;g_vol_k053260*=factor;g_vol_k054539*=factor;g_vol_k051649*=factor;
}
static void scale_all_volumes_div(uint32_t factor){
 if(!factor)return;
 g_vol_opn2/=factor;g_vol_rf/=factor;
 g_vol_sn/=factor;g_vol_opll/=factor;g_vol_opn/=factor;g_vol_opm/=factor;g_vol_opl/=factor;
 g_vol_ay/=factor;g_vol_gb/=factor;g_vol_nes/=factor;g_vol_pce/=factor;
 g_vol_sega/=factor;g_vol_oki/=factor;g_vol_neo/=factor;g_vol_qsound/=factor;g_vol_es5503/=factor;g_vol_k053260/=factor;g_vol_k054539/=factor;g_vol_k051649/=factor;
}

/* Forward declaration: defined below the VGM command parser. */
static uint32_t read_clock_raw(uint32_t off);

static void setup_chip_volumes(void){
 uint32_t overall=0,factor;
 g_vol_sn  =(g_chip_mask&ZZVGM_CMASK_SN76489)?xhdr_chip_volume(VGMCHIP_SN76489,VOL_SN76489):0;
 g_vol_opll=(g_chip_mask&ZZVGM_CMASK_YM2413)? xhdr_chip_volume(VGMCHIP_YM2413,VOL_YM2413):0;
 g_vol_opn =(g_chip_mask&ZZVGM_CMASK_YM2612)? xhdr_chip_volume(VGMCHIP_YM2612,g_system18?(VOL_YM2612/2u):VOL_YM2612):0;
 g_vol_opn2=(g_system18&&(g_chip_mask&ZZVGM_CMASK_YM2612_DUAL))?xhdr_chip_volume_for(VGMCHIP_YM2612,VOL_YM2612/2u,1):0;
 g_vol_rf=g_system18?xhdr_chip_volume(VGMCHIP_RF5C68,0x0B0u):0;
 g_vol_opm =(g_chip_mask&ZZVGM_CMASK_YM2151)? xhdr_chip_volume(VGMCHIP_YM2151,VOL_YM2151):0;
 if(g_chip_mask&ZZVGM_CMASK_OPL3)g_vol_opl=xhdr_chip_volume(VGMCHIP_YMF262,VOL_OPL3);
 else if(g_chip_mask&ZZVGM_CMASK_OPL2)g_vol_opl=xhdr_chip_volume(VGMCHIP_YM3812,VOL_OPL2);
 else g_vol_opl=0;
 g_vol_ay  =(g_chip_mask&ZZVGM_CMASK_AY8910)? xhdr_chip_volume(VGMCHIP_AY8910,VOL_AY8910):0;
 g_vol_gb  =(g_chip_mask&ZZVGM_CMASK_GB_DMG)? xhdr_chip_volume(VGMCHIP_GB_DMG,VOL_GB_DMG):0;
 g_vol_nes =(g_chip_mask&ZZVGM_CMASK_NES_APU)?xhdr_chip_volume(VGMCHIP_NES_APU,VOL_NES_APU):0;
 g_vol_pce =(g_chip_mask&ZZVGM_CMASK_HUC6280)?xhdr_chip_volume(VGMCHIP_HUC6280,VOL_HUC6280):0;
 g_vol_sega=(g_chip_mask&ZZVGM_CMASK_SEGAPCM)?xhdr_chip_volume(VGMCHIP_SEGAPCM,VOL_SEGAPCM):0;
 g_vol_oki=(g_chip_mask&ZZVGM_CMASK_OKI6295)?xhdr_chip_volume(VGMCHIP_OKI6295,VOL_OKI6295):0;
 g_vol_neo=(g_chip_mask&ZZVGM_CMASK_YM2610)?xhdr_chip_volume(VGMCHIP_YM2610,VOL_YM2610):0;
 g_vol_qsound=(g_chip_mask&ZZVGM_CMASK_QSOUND)?xhdr_chip_volume(VGMCHIP_QSOUND,VOL_QSOUND):0;g_vol_es5503=(g_chip_mask&ZZVGM_CMASK_ES5503)?xhdr_chip_volume(VGMCHIP_ES5503,VOL_ES5503):0;
 g_vol_k053260=(g_chip_mask&ZZVGM_CMASK_K053260)?xhdr_chip_volume(VGMCHIP_K053260,VOL_K053260):0;
 g_vol_k054539=(g_chip_mask&ZZVGM_CMASK_K054539)?xhdr_chip_volume(VGMCHIP_K054539,VOL_K054539)/g_k054539_count:0;
 g_vol_k051649=(g_chip_mask&ZZVGM_CMASK_K051649)?xhdr_chip_volume(VGMCHIP_K051649,VOL_K051649)/g_scc_count:0;
 if(g_vol_sn)overall+=mul_q8u(g_vol_sn,PB_SN76489);
 if(g_vol_opll)overall+=mul_q8u(g_vol_opll,PB_YM2413);
 if(g_vol_opn)overall+=mul_q8u(g_vol_opn,PB_YM2612);
 if(g_vol_opn2)overall+=mul_q8u(g_vol_opn2,PB_YM2612);
 if(g_vol_rf)overall+=g_vol_rf;
 if(g_vol_opm)overall+=mul_q8u(g_vol_opm,PB_YM2151);
 if(g_vol_opl)overall+=mul_q8u(g_vol_opl,(g_chip_mask&ZZVGM_CMASK_OPL3)?PB_OPL3:PB_OPL2);
 if(g_vol_ay)overall+=mul_q8u(g_vol_ay,PB_AY8910);
 if(g_vol_gb)overall+=mul_q8u(g_vol_gb,PB_GB_DMG);
 if(g_vol_nes)overall+=mul_q8u(g_vol_nes,PB_NES_APU);
 if(g_vol_pce)overall+=mul_q8u(g_vol_pce,PB_HUC6280);
 if(g_vol_sega)overall+=mul_q8u(g_vol_sega,PB_SEGAPCM);
 if(g_vol_oki)overall+=mul_q8u(g_vol_oki,PB_OKI6295);
 if(g_vol_neo)overall+=mul_q8u(g_vol_neo,PB_YM2610);
 if(g_vol_qsound)overall+=mul_q8u(g_vol_qsound,PB_QSOUND);if(g_vol_es5503)overall+=mul_q8u(g_vol_es5503,PB_ES5503);
 if(g_vol_k053260)overall+=mul_q8u(g_vol_k053260,PB_K053260);
 if(g_vol_k054539)overall+=g_k054539_count*mul_q8u(g_vol_k054539,PB_K054539);
 if(g_vol_k051649)overall+=g_scc_count*mul_q8u(g_vol_k051649,PB_K051649);
 if(!overall)return;
 if(overall<=0x180u){
  factor=1u;
  while(overall<=0x180u){factor<<=1;overall<<=1;}
  scale_all_volumes_mul(factor);
 }else if(overall>0x300u){
  factor=1u;
  while(overall>0x300u){factor<<=1;overall>>=1;}
  scale_all_volumes_div(factor);
 }
 /* Per-chip relative level: do not feed this correction back into the
  * overall chip normalizer; otherwise it may cancel the attenuation.
  * The raw SN backend and standalone PSG level are left untouched.
  */
 if((g_chip_mask & (ZZVGM_CMASK_SN76489|ZZVGM_CMASK_YM2612)) ==
    (ZZVGM_CMASK_SN76489|ZZVGM_CMASK_YM2612) && !g_system18)
  g_vol_sn=mul_q8u(g_vol_sn,MD_SN_RELATIVE_Q8);
 /* Apply YM2413 only, never VRC7 (VGM header high clock bit).
  * Keep outside the shared normalizer to preserve the chosen gain. */
 if(g_vol_opll && !(read_clock_raw(0x10u)&0x80000000u))
  g_vol_opll=mul_q8u(g_vol_opll,SMS_YM2413_LISTEN_GAIN_Q8);
}

static void ring_put(uint32_t pos,int16_t l,int16_t r){uint32_t v=((uint32_t)(uint16_t)l<<16)|(uint16_t)r;g_ring[pos&ZZVGM_RING_MASK]=ARM32(v);} 
static uint32_t ring_free(void){uint32_t rd=sh_read(VSH_PCM_READ),used=g_write_pos-rd;if(used>=ZZVGM_RING_FRAMES)return 0;return ZZVGM_RING_FRAMES-used;}
static int need(uint32_t n){return g_pos<=g_len&&n<=g_len-g_pos;}

/* YM2151 rendering is provided by jarek2151_wrap.c. */

static int skip_fixed_command(uint8_t c){uint32_t n;if((c>=0x30&&c<=0x3f))n=1;else if((c>=0x51&&c<=0x5f)||(c>=0xA0&&c<=0xBF))n=2;else if(c>=0xC0&&c<=0xDF)n=3;else if(c==0xE0||c==0xE1)n=4;else if(c==0x90||c==0x91||c==0x95)n=4;else if(c==0x92)n=5;else if(c==0x93)n=10;else if(c==0x94)n=1;else return 0;if(!need(n)){g_error=ZZVGM_ERR_TRUNCATED;return-1;}g_pos+=n;return 1;}

static uint32_t parse_until_wait(void){while(!g_done&&!g_error){uint8_t c;if(!need(1)){g_error=ZZVGM_ERR_TRUNCATED;break;}c=g_vgm[g_pos++];switch(c){
case 0x00: break;
case 0x31: if(!need(1)){g_error=ZZVGM_ERR_TRUNCATED;break;}if((g_chip_mask&ZZVGM_CMASK_AY8910)&&!(g_vgm[g_pos]&0x80u)&&!(g_vgm[g_pos]&0x40u))if(!g_use_ay_ref)zz_ay8910_stereo(&g_ay,g_vgm[g_pos]&0x3fu);g_pos++;g_writes++;break;
case 0x4F: if(!need(1)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_SN76489)zz_sn76489_stereo(&g_sn,g_vgm[g_pos]);g_pos++;g_writes++;break;
case 0x50: if(!need(1)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_SN76489)zz_sn76489_write(&g_sn,g_vgm[g_pos]);g_pos++;g_writes++;break;
case 0x51: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_YM2413)zz_emu2413_write(g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0x52: case 0x53: case 0xA2: case 0xA3:
 if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}
 if(g_system18){
   if(g_chip_mask&ZZVGM_CMASK_YM2612)
     zz_s18_fm_write((c>=0xA2)?1u:0u,c&1u,g_vgm[g_pos],g_vgm[g_pos+1]);
 }else if(c==0x52u||c==0x53u){
   if(g_chip_mask&ZZVGM_CMASK_YM2612)
     dac_write_ym2612((c==0x52)?0u:1u,g_vgm[g_pos],g_vgm[g_pos+1]);
 }
 g_pos+=2;g_writes++;break;
case 0x54: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_YM2151)zz_jarek2151_write(g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0x58: case 0x59: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_YM2610){unsigned port=(c==0x59u)?1u:0u;uint8_t reg=g_vgm[g_pos],val=g_vgm[g_pos+1];zz_neo_write(port,reg,val);
/* YM2610 FM mapped into the fast OPN2 engine as a temporary approximation. */
 if(reg>=0x20u&&!(port==1u&&reg<0x30u)&&!(g_chip_mask&ZZVGM_CMASK_YM2612))zz_gens2612_write(port,reg,val);
 }g_pos+=2;g_writes++;break;
case 0x5A: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_OPL2)OPL3_WriteRegBuffered(&g_opl,g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0x5E: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_OPL3)OPL3_WriteRegBuffered(&g_opl,g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0x5F: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_OPL3)OPL3_WriteRegBuffered(&g_opl,(uint16_t)(0x100u|g_vgm[g_pos]),g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0x61: if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}{uint32_t w=le16(g_vgm+g_pos);g_pos+=2;return w;}
case 0x62:return 735u;case 0x63:return 882u;
case 0x66: if((sh_read(VSH_FLAGS)&ZZVGM_FLAG_REPEAT)&&g_loop&&g_loop<g_len){g_pos=g_loop;g_loops++;break;}g_done=1;return UINT32_MAX;
case 0x67: if(!need(6)||g_vgm[g_pos]!=0x66){g_error=ZZVGM_ERR_BAD_CMD;break;}{uint8_t typ=g_vgm[g_pos+1];uint32_t rawsz=le32(g_vgm+g_pos+2),sz=rawsz&0x7fffffffu;g_pos+=6;if(!need(sz)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(typ<0x40u){pcm_bank_add(typ,g_vgm+g_pos,sz);if(typ==0x00u&&!g_pcm_bank){g_pcm_bank=g_vgm+g_pos;g_pcm_bank_len=sz;g_pcm_pos=0;}}/* compressed PCM blocks 0x40..0x7E are intentionally left for DACSTREAM2 */
else if(typ==0xC0u&&g_system18){
 if(rawsz&0x80000000u)g_error=ZZVGM_ERR_RF_UNSUPPORTED;
 else if(sz<2u)g_error=ZZVGM_ERR_RF_TRANSFER;
 else zz_s18_rf_ram(((uint32_t)le16(g_vgm+g_pos))|zz_s18_rf_bank(),g_vgm+g_pos+2u,sz-2u);
}else if((typ==0x80u||typ==0x8bu||typ==0x82u||typ==0x83u||typ==0x8fu||typ==0x8eu||typ==0x8cu)&&sz>=8u){uint32_t romsz=le32(g_vgm+g_pos),romoff=le32(g_vgm+g_pos+4);if((!(rawsz&0x80000000u)||typ==0x8cu)&&((typ==0x80u&&(g_chip_mask&ZZVGM_CMASK_SEGAPCM))||(typ==0x8bu&&(g_chip_mask&ZZVGM_CMASK_OKI6295))||((typ==0x82u||typ==0x83u)&&(g_chip_mask&ZZVGM_CMASK_YM2610))||(typ==0x8fu&&(g_chip_mask&ZZVGM_CMASK_QSOUND))||(typ==0x8eu&&(g_chip_mask&ZZVGM_CMASK_K053260))||(typ==0x8cu&&(g_chip_mask&ZZVGM_CMASK_K054539)&&((rawsz>>31)<g_k054539_count)))){if(!(typ==0x8cu?zz_k054539_rom(&g_k054539[(rawsz>>31)&1u],romsz,romoff,g_vgm+g_pos+8,sz-8u,zz_arcade_alloc_rom):typ==0x8eu?zz_k053260_rom(&g_k053260,romsz,romoff,g_vgm+g_pos+8,sz-8u,zz_arcade_alloc_rom):(typ==0x8fu?zz_qsound_rom(romsz,romoff,g_vgm+g_pos+8,sz-8u):((typ==0x82u||typ==0x83u)?zz_neo_rom(typ,romsz,romoff,g_vgm+g_pos+8,sz-8u):zz_arcade_rom_block(typ,romsz,romoff,g_vgm+g_pos+8,sz-8u))))){g_error=ZZVGM_ERR_ARCADE_ROM;}}}
else if(typ==0xE1u&&(g_chip_mask&ZZVGM_CMASK_ES5503)&&sz>=4u){if(!zz_es5503_ram(&g_es5503,le32(g_vgm+g_pos),g_vgm+g_pos+4,sz-4u))g_error=ZZVGM_ERR_ARCADE_ROM;}else if(typ==0xC2&&(g_chip_mask&ZZVGM_CMASK_NES_APU)&&sz>=2u){zz_nes_apu_ram_write(le16(g_vgm+g_pos),g_vgm+g_pos+2,sz-2u);}g_pos+=sz;}break;
case 0x68:if(!need(11)){g_error=ZZVGM_ERR_TRUNCATED;break;}{
  const uint8_t *cmd=g_vgm+g_pos;uint32_t s,d,n;
  if(cmd[0]!=0x66u){g_error=ZZVGM_ERR_BAD_CMD;break;}
  s=(uint32_t)cmd[2]|((uint32_t)cmd[3]<<8)|((uint32_t)cmd[4]<<16);
  d=(uint32_t)cmd[5]|((uint32_t)cmd[6]<<8)|((uint32_t)cmd[7]<<16);
  n=(uint32_t)cmd[8]|((uint32_t)cmd[9]<<8)|((uint32_t)cmd[10]<<16);
  if(g_system18&&(cmd[1]==0x01u||cmd[1]==0x81u)){
    uint8_t block[256];uint16_t cache=ZZ_NO_BLOCK;uint32_t k;
    if(cmd[1]==0x81u)g_error=ZZVGM_ERR_RF_UNSUPPORTED;
    if(!n)n=0x1000000u; /* VGM: zero transfer length denotes 16 MiB. */
    if(!g_error&&(s>g_bank_len[1]||n>g_bank_len[1]-s))g_error=ZZVGM_ERR_RF_TRANSFER;
    d|=zz_s18_rf_bank(); /* Apply bank once; wrapper accepts absolute address. */
    if(!g_error)for(k=0;k<n;k+=sizeof(block)){
      uint32_t take=n-k;if(take>sizeof(block))take=sizeof(block);
      if(!pcm_bank_read(1u,s+k,block,take,&cache)){g_error=ZZVGM_ERR_RF_TRANSFER;break;}
      zz_s18_rf_ram(d+k,block,take);
    }
  }
  if(cmd[1]==0x24u&&(g_chip_mask&ZZVGM_CMASK_ES5503)&&n>0&&n<=ZZ_ES5503_RAM_SIZE&&d<=ZZ_ES5503_RAM_SIZE-n){
    uint8_t block[128];uint16_t cache=ZZ_NO_BLOCK;uint32_t k;
    for(k=0;k<n;k+=sizeof(block)){uint32_t take=n-k;if(take>sizeof(block))take=sizeof(block);
      if(!pcm_bank_read(0x24u,s+k,block,take,&cache)||!zz_es5503_ram(&g_es5503,d+k,block,take)){g_error=ZZVGM_ERR_ARCADE_ROM;break;}
    }
  }
  g_pos+=11;
 }break;
case 0x90:if(!need(4)){g_error=ZZVGM_ERR_TRUNCATED;break;}dac_stream_setup(g_vgm[g_pos],g_vgm[g_pos+1],g_vgm[g_pos+2],g_vgm[g_pos+3]);g_pos+=4;break;
case 0x91:if(!need(4)){g_error=ZZVGM_ERR_TRUNCATED;break;}dac_stream_set_data(g_vgm[g_pos],g_vgm[g_pos+1],g_vgm[g_pos+2],g_vgm[g_pos+3]);g_pos+=4;break;
case 0x92:if(!need(5)){g_error=ZZVGM_ERR_TRUNCATED;break;}dac_stream_set_freq(g_vgm[g_pos],le32(g_vgm+g_pos+1));g_pos+=5;break;
case 0x93:if(!need(10)){g_error=ZZVGM_ERR_TRUNCATED;break;}dac_stream_start(g_vgm[g_pos],le32(g_vgm+g_pos+1),g_vgm[g_pos+5],le32(g_vgm+g_pos+6));g_pos+=10;break;
case 0x94:if(!need(1)){g_error=ZZVGM_ERR_TRUNCATED;break;}dac_stream_stop(g_vgm[g_pos]);g_pos+=1;break;
case 0x95:if(!need(4)){g_error=ZZVGM_ERR_TRUNCATED;break;}dac_stream_fast_start(g_vgm[g_pos],le16(g_vgm+g_pos+1),g_vgm[g_pos+3]);g_pos+=4;break;
case 0xA0:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_AY8910){if(g_use_ay_ref)zz_ay2149_write(&g_ay_ref,g_vgm[g_pos],g_vgm[g_pos+1]);else zz_ay8910_write(&g_ay,g_vgm[g_pos],g_vgm[g_pos+1]);}g_pos+=2;g_writes++;break;
case 0xB0:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}
 if(g_system18){
   if(g_vgm[g_pos]&0x80u)g_error=ZZVGM_ERR_RF_UNSUPPORTED;
   else zz_s18_rf_write(g_vgm[g_pos]&0x7fu,g_vgm[g_pos+1]);
 }g_pos+=2;g_writes++;break;
case 0xB3:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_GB_DMG)zz_gb_dmg_write(g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0xB4:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_NES_APU)zz_nes_apu_write(g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0xB8:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if((g_chip_mask&ZZVGM_CMASK_OKI6295)&&!(g_vgm[g_pos]&0x80u))zz_arcade_oki_write(g_vgm[g_pos],g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0xBA:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if((g_chip_mask&ZZVGM_CMASK_K053260)&&!(g_vgm[g_pos]&0x80u))zz_k053260_write(&g_k053260,g_vgm[g_pos]&0x7fu,g_vgm[g_pos+1]);g_pos+=2;g_writes++;break;
case 0xD2:if(!need(3)){g_error=ZZVGM_ERR_TRUNCATED;break;}{unsigned chip=g_vgm[g_pos]>>7;uint8_t port=g_vgm[g_pos]&0x7fu;if((g_chip_mask&ZZVGM_CMASK_K051649)&&chip<g_scc_count)zz_scc_write(&g_scc[chip],port,g_vgm[g_pos+1],g_vgm[g_pos+2]);g_pos+=3;g_writes++;}break;
case 0xD3:if(!need(3)){g_error=ZZVGM_ERR_TRUNCATED;break;}{uint8_t chip=(uint8_t)(g_vgm[g_pos]>>7);uint16_t reg=((uint16_t)(g_vgm[g_pos]&0x7fu)<<8)|g_vgm[g_pos+1];if((g_chip_mask&ZZVGM_CMASK_K054539)&&chip<g_k054539_count)zz_k054539_write(&g_k054539[chip],reg,g_vgm[g_pos+2]);g_pos+=3;g_writes++;}break;
case 0xD5:if(!need(3)){g_error=ZZVGM_ERR_TRUNCATED;break;}if((g_chip_mask&ZZVGM_CMASK_ES5503)&&g_vgm[g_pos]==0u)zz_es5503_write(&g_es5503,g_vgm[g_pos+1],g_vgm[g_pos+2]);g_pos+=3;g_writes++;break;
case 0xC1:if(!need(3)){g_error=ZZVGM_ERR_TRUNCATED;break;}
 if(g_system18)zz_s18_rf_byte(le16(g_vgm+g_pos),g_vgm[g_pos+2]);
 g_pos+=3;g_writes++;break;
case 0xC4:if(!need(3)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_QSOUND)zz_qsound_write(g_vgm[g_pos+2],(uint16_t)(((uint16_t)g_vgm[g_pos]<<8u)|g_vgm[g_pos+1]));g_pos+=3;g_writes++;break;
case 0xC0:if(!need(3)){g_error=ZZVGM_ERR_TRUNCATED;break;}if((g_chip_mask&ZZVGM_CMASK_SEGAPCM)&&!(g_vgm[g_pos+1]&0x80u))zz_arcade_sega_write(le16(g_vgm+g_pos),g_vgm[g_pos+2]);g_pos+=3;g_writes++;break;
case 0xB9:if(!need(2)){g_error=ZZVGM_ERR_TRUNCATED;break;}if(g_chip_mask&ZZVGM_CMASK_HUC6280){zz_huc6280_write(g_vgm[g_pos],g_vgm[g_pos+1]);if((g_vgm[g_pos]&0x0Fu)==0u)g_pce_selected_channel=g_vgm[g_pos+1]&7u;}g_pos+=2;g_writes++;break;
case 0xE0:if(!need(4)){g_error=ZZVGM_ERR_TRUNCATED;break;}g_pcm_pos=le32(g_vgm+g_pos);g_pos+=4;break;
default:
 if((c&0xF0u)==0x70u)return(uint32_t)((c&15u)+1u);
 if((c&0xF0u)==0x80u){if(g_chip_mask&ZZVGM_CMASK_YM2612){uint8_t d;uint16_t cache=ZZ_NO_BLOCK;if(pcm_bank_read(0,g_pcm_pos,&d,1,&cache)){g_pcm_pos++;if(g_system18)zz_s18_fm_write(0u,0u,0x2Au,d);else g_ym2612_dac_sample=((int32_t)d-0x80)<<5;g_writes++;}}return(uint32_t)(c&15u);}
 {int s=skip_fixed_command(c);if(s==0){sh_write(VSH_BAD_CMD,c);g_error=ZZVGM_ERR_BAD_CMD;}}break;
}}return UINT32_MAX;}

static int upload_begin(void){uint32_t len=sh_read(VSH_VGM_LEN);uintptr_t top;if(len<18u){g_error=ZZVGM_ERR_TOO_SMALL;return 0;}if(len>ZZVGM_VGM_MAX_BYTES||len>ZZVGM_ARENA_SIZE){g_error=ZZVGM_ERR_TOO_LARGE;return 0;}g_upload_len=len;g_uploaded=0;g_file_flags=sh_read(VSH_FLAGS);if(g_file_flags&ZZVGM_FLAG_GZIP){top=((uintptr_t)ZZVGM_ARENA_END-len)&~(uintptr_t)63u;if(top<ZZVGM_ARENA_BASE+0x100000u){g_error=ZZVGM_ERR_TOO_LARGE;return 0;}g_upload_dst=(uint8_t*)top;}else g_upload_dst=g_arena;sh_write(VSH_UPLOAD_DONE,0);sh_write(VSH_UPLOAD_EXPECTED,len);sh_write(VSH_STATUS,ZZVGM_ST_UPLOADING);return 1;}
static int upload_chunk(void){uint32_t off=sh_read(VSH_UPLOAD_OFFSET),len=sh_read(VSH_UPLOAD_SIZE),words,w;if(off!=g_uploaded){g_error=ZZVGM_ERR_UPLOAD_ORDER;return 0;}if(!len||len>ZZVGM_UPLOAD_BYTES||off+len>g_upload_len){g_error=ZZVGM_ERR_UPLOAD_SIZE;return 0;}words=(len+3)>>2;for(w=0;w<words;w++){uint32_t v=ARM32(g_upload[w]),b=w<<2;if(b<len)g_upload_dst[off+b]=(uint8_t)(v>>24);if(b+1<len)g_upload_dst[off+b+1]=(uint8_t)(v>>16);if(b+2<len)g_upload_dst[off+b+2]=(uint8_t)(v>>8);if(b+3<len)g_upload_dst[off+b+3]=(uint8_t)v;}g_uploaded+=len;sh_write(VSH_UPLOAD_DONE,g_uploaded);return 1;}

/* Clock fields are valid only when they are actually inside the VGM header.
 * Extended chip clocks (e.g. YM3812 @ 0x50, YMF262 @ 0x5C, AY8910 @ 0x74)
 * do NOT exist when the data stream starts before those offsets. Reading past
 * g_data would interpret VGM commands as fake clock values, enabling phantom
 * chips and wasting Core1 time rendering them. */
static uint32_t read_clock_raw(uint32_t off){
    if(off+4u>g_data || off+4u>g_len)return 0;
    return le32(g_vgm+off);
}
static uint32_t read_clock(uint32_t off){return read_clock_raw(off)&0x3fffffffu;}
/* VGM <= 1.01 used the header field at 0x10 as the FM clock for
 * YM2413, YM2612 or YM2151. Identify which one from the command stream.
 * Data blocks did not exist in those versions, so a compact skipper is enough. */
static int legacy_fm_kind(void){
 uint32_t p=g_data;
 while(p<g_len){uint8_t c=g_vgm[p++];uint32_t n=0;
  if(c==0x51)return 2413;
  if(c==0x52||c==0x53)return 2612;
  if(c==0x54)return 2151;
  if(c==0x66)break;
  if(c==0x50||c==0x4f||(c>=0x30&&c<=0x3f))n=1;
  else if(c==0x61)n=2;
  else if(c==0x62||c==0x63||(c>=0x70&&c<=0x8f))n=0;
  else if((c>=0x55&&c<=0x5f)||(c>=0xa0&&c<=0xbf))n=2;
  else if(c>=0xc0&&c<=0xdf)n=3;
  else if(c==0xe0||c==0xe1)n=4;
  if(n>g_len-p)break;p+=n;
 }
 return 0;
}
static uint32_t primary_chip(uint32_t m){if(m&ZZVGM_CMASK_RF5C68)return ZZVGM_CHIP_RF5C68;if(m&ZZVGM_CMASK_K051649)return ZZVGM_CHIP_K051649;if(m&ZZVGM_CMASK_K054539)return ZZVGM_CHIP_K054539;if(m&ZZVGM_CMASK_K053260)return ZZVGM_CHIP_K053260;if(m&ZZVGM_CMASK_ES5503)return ZZVGM_CHIP_ES5503;if(m&ZZVGM_CMASK_QSOUND)return ZZVGM_CHIP_QSOUND;if(m&ZZVGM_CMASK_YM2610)return ZZVGM_CHIP_YM2610;if(m&ZZVGM_CMASK_SEGAPCM)return ZZVGM_CHIP_SEGAPCM;if(m&ZZVGM_CMASK_OKI6295)return ZZVGM_CHIP_OKI6295;if(m&ZZVGM_CMASK_NES_APU)return ZZVGM_CHIP_NES_APU;if(m&ZZVGM_CMASK_GB_DMG)return ZZVGM_CHIP_GB_DMG;if(m&ZZVGM_CMASK_HUC6280)return ZZVGM_CHIP_HUC6280;if(m&ZZVGM_CMASK_YM2612)return ZZVGM_CHIP_YM2612;if(m&ZZVGM_CMASK_YM2151)return ZZVGM_CHIP_YM2151;if(m&ZZVGM_CMASK_YM2413)return ZZVGM_CHIP_YM2413;if(m&ZZVGM_CMASK_OPL3)return ZZVGM_CHIP_OPL3;if(m&ZZVGM_CMASK_OPL2)return ZZVGM_CHIP_OPL2;if(m&ZZVGM_CMASK_SN76489)return ZZVGM_CHIP_SN76489;if(m&ZZVGM_CMASK_AY8910)return ZZVGM_CHIP_AY8910;return 0;}

static int vgm_init(void){uint32_t ver,data_rel,loop_rel,opl2,opl3,sn,opll,opn,opm,ay,gb,nes,pce,opll_raw,opn_raw,opm_raw,gb_raw,pce_raw,sega,oki,oki_raw,neo,qsound,es5503,k053260,k054539,k054539_raw,scc,scc_raw,rf,rf_raw;if(g_uploaded!=g_upload_len){g_error=ZZVGM_ERR_UPLOAD_INCOMPLETE;return 0;}g_vgm=g_upload_dst;g_len=g_upload_len;if(g_file_flags&ZZVGM_FLAG_GZIP){uint32_t out=0,cap=(uint32_t)((uintptr_t)g_upload_dst-(uintptr_t)g_arena);int rc=zz_gzip_inflate(g_upload_dst,g_upload_len,g_arena,cap,&out);if(rc==-2){g_error=ZZVGM_ERR_GZIP_TOO_LARGE;return 0;}if(rc){g_error=ZZVGM_ERR_GZIP_INFLATE;return 0;}g_vgm=g_arena;g_len=out;sh_write(VSH_FILE_TYPE,ZZVGM_FILE_VGZ);sh_write(VSH_UNPACKED_LEN,out);}else{sh_write(VSH_FILE_TYPE,ZZVGM_FILE_VGM);sh_write(VSH_UNPACKED_LEN,g_len);}if(g_len<0x40||g_vgm[0]!='V'||g_vgm[1]!='g'||g_vgm[2]!='m'||g_vgm[3]!=' '){g_error=ZZVGM_ERR_BAD_MAGIC;return 0;}ver=le32(g_vgm+8);data_rel=(g_len>=0x38)?le32(g_vgm+0x34):0;g_data=data_rel?(0x34+data_rel):0x40;loop_rel=le32(g_vgm+0x1c);g_loop=loop_rel?(0x1c+loop_rel):0;if(g_data>=g_len||(g_loop&&g_loop>=g_len)){g_error=ZZVGM_ERR_BAD_OFFSET;return 0;}
 sn=read_clock(0x0c);opll_raw=read_clock_raw(0x10);opll=opll_raw&0x3fffffffu;opn_raw=read_clock_raw(0x2c);opm_raw=read_clock_raw(0x30);opn=opn_raw&0x3fffffffu;opm=opm_raw&0x3fffffffu;opl2=read_clock(0x50);opl3=read_clock(0x5c);ay=read_clock(0x74);gb_raw=read_clock_raw(0x80);gb=gb_raw&0x3fffffffu;nes=read_clock(0x84);pce_raw=read_clock_raw(0xA4);pce=pce_raw&0x3fffffffu;rf_raw=read_clock_raw(0x40u);rf=rf_raw&0x3fffffffu;sega=read_clock(0x38);oki_raw=read_clock_raw(0x98);oki=oki_raw&0x3fffffffu;neo=read_clock(0x4cu);/* VGM 1.61+: QSound clock is at 0xB4 only when header is long enough. */qsound=(ver>=0x00000161u&&g_data>=0xB8u&&g_len>=0xB8u)?read_clock(0xB4u):0;es5503=(ver>=0x00000171u&&g_data>=0xD8u&&g_len>=0xD8u)?read_clock(0xCCu):0;
 k053260=(ver>=0x00000161u&&g_data>=0xB0u&&g_len>=0xB0u)?read_clock(0xACu):0;
 scc_raw=(ver>=0x00000161u&&g_data>=0xA0u&&g_len>=0xA0u)?read_clock_raw(0x9Cu):0;
 scc=scc_raw&0x3fffffffu;g_scc_count=scc?((scc_raw&0x40000000u)?2u:1u):0u;
 k054539_raw=(ver>=0x00000161u&&g_data>=0xA4u&&g_len>=0xA4u)?read_clock_raw(0xA0u):0;
 k054539=k054539_raw&0x3fffffffu;g_k054539_count=k054539?((k054539_raw&0x40000000u)?2u:1u):0u;
 /* VGM 1.01 and older: 0x10 is the shared FM clock. Determine the target
  * from the actual write command (0x51/0x52-53/0x54). */
 if(ver<=0x00000101u&&opll&&!opn&&!opm){uint32_t legacy=opll;int k=legacy_fm_kind();opll=0;if(k==2612){opn=legacy;opn_raw=legacy;}else if(k==2151){opm=legacy;opm_raw=legacy;}else opll=legacy;}
 g_system18=rf?1u:0u;
 if(g_system18&&(rf_raw&0x40000000u)){g_error=ZZVGM_ERR_RF_UNSUPPORTED;return 0;}
 g_chip_mask=0;if(rf)g_chip_mask|=ZZVGM_CMASK_RF5C68;
 if(g_system18&&opn&&(opn_raw&0x40000000u))g_chip_mask|=ZZVGM_CMASK_YM2612_DUAL;
 if(sn)g_chip_mask|=ZZVGM_CMASK_SN76489;if(opll)g_chip_mask|=ZZVGM_CMASK_YM2413;if(opn)g_chip_mask|=ZZVGM_CMASK_YM2612;if(opm)g_chip_mask|=ZZVGM_CMASK_YM2151;if(opl2)g_chip_mask|=ZZVGM_CMASK_OPL2;if(opl3)g_chip_mask|=ZZVGM_CMASK_OPL3;if(ay)g_chip_mask|=ZZVGM_CMASK_AY8910;if(gb)g_chip_mask|=ZZVGM_CMASK_GB_DMG;if(nes)g_chip_mask|=ZZVGM_CMASK_NES_APU;if(pce)g_chip_mask|=ZZVGM_CMASK_HUC6280;if(sega)g_chip_mask|=ZZVGM_CMASK_SEGAPCM;if(oki)g_chip_mask|=ZZVGM_CMASK_OKI6295;if(neo)g_chip_mask|=ZZVGM_CMASK_YM2610;if(qsound)g_chip_mask|=ZZVGM_CMASK_QSOUND;if(es5503)g_chip_mask|=ZZVGM_CMASK_ES5503;if(k053260)g_chip_mask|=ZZVGM_CMASK_K053260;if(k054539)g_chip_mask|=ZZVGM_CMASK_K054539;if(scc)g_chip_mask|=ZZVGM_CMASK_K051649;if(!g_chip_mask){g_error=ZZVGM_ERR_NO_SUPPORTED_CHIP;return 0;}
 setup_chip_volumes();
 g_sample_rate=sh_read(VSH_SAMPLE_RATE);if(!g_sample_rate)g_sample_rate=44100;
 if(g_chip_mask&(ZZVGM_CMASK_OPL2|ZZVGM_CMASK_OPL3))OPL3_Reset(&g_opl,g_sample_rate);
 g_opn_clock=opn;if((opn&&!g_system18)||neo){if(!zz_gens2612_init((opn&&!g_system18)?opn:neo,g_sample_rate)){g_error=ZZVGM_ERR_YM2612_INIT;return 0;}}
 g_opm_clock=opm;if(opm){if(!zz_jarek2151_init(opm,g_sample_rate)){g_error=ZZVGM_ERR_YM2151_INIT;return 0;}}
 g_opll_clock=opll;if(opll){if(!zz_emu2413_init(opll,g_sample_rate,(opll_raw&0x80000000u)!=0)){g_error=ZZVGM_ERR_YM2413_INIT;return 0;}}
 g_sn_clock=sn;if(sn){uint16_t fb=(g_len>=0x2a)?le16(g_vgm+0x28):0x0009;uint8_t sw=(g_len>=0x2b)?g_vgm[0x2a]:16;zz_sn76489_reset(&g_sn,sn,g_sample_rate,fb,sw);}
 g_ay_clock=ay;g_use_ay_ref=(ay && scc)?1u:0u;
 if(ay){if(g_use_ay_ref)zz_ay2149_reset(&g_ay_ref,ay,g_sample_rate,
 (g_data>=0x7au&&g_len>=0x7au)?g_vgm[0x78u]:0u,
 (g_data>=0x7au&&g_len>=0x7au)?g_vgm[0x79u]:0u);
 else zz_ay8910_reset(&g_ay,ay,g_sample_rate);}
 g_gb_clock=gb;if(gb){if(!zz_gb_dmg_init(gb_raw,g_sample_rate)){g_error=ZZVGM_ERR_GB_INIT;return 0;}}
 g_nes_clock=nes;if(nes){if(!zz_nes_apu_init(nes,g_sample_rate)){g_error=ZZVGM_ERR_NES_INIT;return 0;}}
 g_pce_clock=pce;if(pce){if(!zz_huc6280_init(pce_raw,g_sample_rate)){g_error=ZZVGM_ERR_HUC6280_INIT;return 0;}}
 g_sega_clock=sega;g_oki_clock=oki;{uintptr_t romstart=((uintptr_t)g_arena+g_len+63u)&~(uintptr_t)63u;uintptr_t romend=(g_upload_dst==g_arena)?(uintptr_t)ZZVGM_ARENA_END:(uintptr_t)g_upload_dst;if(romstart>=romend&&(sega||oki||neo||qsound||k053260||k054539)){g_error=ZZVGM_ERR_ARCADE_ROM;return 0;}zz_arcade_init(g_sample_rate,sega,read_clock_raw(0x3c),oki,(oki_raw&0x80000000u)?1u:0u,(uint8_t*)romstart,(uint8_t*)romend);g_neo_clock=neo;if(neo)zz_neo_init(neo,g_sample_rate);g_qsound_clock=qsound;if(qsound)zz_qsound_init(qsound,g_sample_rate);
 if(scc){unsigned ci;for(ci=0;ci<g_scc_count;ci++)zz_scc_init(&g_scc[ci],scc,g_sample_rate,(scc_raw&0x80000000u)!=0u);}
 g_k053260_clock=k053260;if(k053260)zz_k053260_init(&g_k053260,k053260,g_sample_rate);g_k054539_clock=k054539;
 if(k054539){unsigned ci;for(ci=0;ci<g_k054539_count;ci++)zz_k054539_init(&g_k054539[ci],k054539,g_sample_rate);}g_es5503_clock=es5503;if(es5503)zz_es5503_init(&g_es5503,es5503,g_sample_rate,(g_data>=0xD5u&&g_len>0xD4u)?g_vgm[0xD4u]:2u);}
 if(g_system18){
   uint8_t *pool=zz_arcade_alloc_rom(ZZ_S18_POOL_BYTES,0);
   unsigned count=opn?((opn_raw&0x40000000u)?2u:1u):0u;
   if(!pool||!zz_s18_init(opn,count,rf,g_sample_rate,pool,ZZ_S18_POOL_BYTES)){
     g_error=ZZVGM_ERR_S18_INIT;return 0;
   }
 }
 g_pos=g_data;g_wait=0;g_write_pos=sh_read(VSH_PCM_READ);g_play_samples=0;g_loops=0;g_writes=0;g_done=0;g_render_calls=0;g_pcm_bank=NULL;g_pcm_bank_len=0;g_pcm_pos=0;g_ym2612_dac_sample=0;g_ym2612_dac_enabled=0;g_ym2612_dac_left=1;g_ym2612_dac_right=1;g_pce_selected_channel=0;pcm_banks_reset();dac_streams_reset();
 sh_write(VSH_VGM_VERSION,ver);sh_write(VSH_DATA_OFFSET,g_data);sh_write(VSH_LOOP_OFFSET,g_loop);sh_write(VSH_TOTAL_SAMPLES,le32(g_vgm+0x18));sh_write(VSH_CHIP,primary_chip(g_chip_mask));sh_write(VSH_CHIP_MASK,g_chip_mask);sh_write(VSH_PCM_WRITE,g_write_pos);sh_write(VSH_ERROR,0);sh_write(VSH_BAD_CMD,0);sh_write(VSH_PLAY_SAMPLES,0);sh_write(VSH_LOOP_COUNT,0);sh_write(VSH_OPL_WRITES,0);sh_write(VSH_RENDER_CALLS,0);sh_write(VSH_LAST_RENDER_FRAMES,0);return 1;}

static void render_chunk(uint32_t n,uint32_t ring_base){
 int16_t opl[512],opll[512],opn[512],opm[512],sn[512],ay[512];uint32_t i;
 if(g_chip_mask&(ZZVGM_CMASK_OPL2|ZZVGM_CMASK_OPL3))OPL3_GenerateStream(&g_opl,opl,n);
 if((g_chip_mask&(ZZVGM_CMASK_YM2612|ZZVGM_CMASK_YM2610))&&(!g_system18||(g_chip_mask&ZZVGM_CMASK_YM2610)))zz_gens2612_render(opn,n);
 if(g_system18)zz_s18_render(g_s18_fm0,g_s18_fm1,g_s18_pcm,n);
 if(g_chip_mask&ZZVGM_CMASK_YM2151)zz_jarek2151_render(opm,n);
 if(g_chip_mask&ZZVGM_CMASK_YM2413)zz_emu2413_render(opll,n);
 if(g_chip_mask&ZZVGM_CMASK_SN76489)zz_sn76489_render(&g_sn,sn,n);
 if(g_chip_mask&ZZVGM_CMASK_AY8910){if(g_use_ay_ref)zz_ay2149_render(&g_ay_ref,ay,n);else zz_ay8910_render(&g_ay,ay,n);}
 if(g_chip_mask&ZZVGM_CMASK_GB_DMG)zz_gb_dmg_render(g_gb_pcm,n);
 if(g_chip_mask&ZZVGM_CMASK_NES_APU)zz_nes_apu_render(g_nes_pcm,n);
 if(g_chip_mask&ZZVGM_CMASK_HUC6280)zz_huc6280_render(g_pce_pcm,n);
 if(g_chip_mask&(ZZVGM_CMASK_SEGAPCM|ZZVGM_CMASK_OKI6295))zz_arcade_render(g_sega_pcm,g_oki_pcm,n);
 if(g_chip_mask&ZZVGM_CMASK_YM2610){zz_neo_render(g_neo_pcm,n);zz_neo_render_ssg(g_neo_ay_pcm,n);}
 if(g_chip_mask&ZZVGM_CMASK_QSOUND)zz_qsound_render(g_qsound_pcm,n);
 if(g_chip_mask&ZZVGM_CMASK_K051649){unsigned ci;for(ci=0;ci<g_scc_count;ci++)zz_scc_render(&g_scc[ci],g_scc_pcm[ci],n);}
 if(g_chip_mask&ZZVGM_CMASK_K053260)zz_k053260_render(&g_k053260,g_k053260_pcm,n);
 if(g_chip_mask&ZZVGM_CMASK_K054539){uint32_t ci;for(ci=0;ci<g_k054539_count;ci++)zz_k054539_render(&g_k054539[ci],g_k054539_pcm[ci],n);} if(g_chip_mask&ZZVGM_CMASK_ES5503)zz_es5503_render(&g_es5503,g_es5503_pcm,n);
 for(i=0;i<n;i++){int64_t l=0,r=0;if(g_vol_opl){l+=2*mul_q8s(opl[i*2],g_vol_opl);r+=2*mul_q8s(opl[i*2+1],g_vol_opl);}if(g_system18){
 /* Imported resampler output is Q8. Multiply Q8 volume, divide by 2^16. */
 l+=((int64_t)g_s18_fm0[i*2]*g_vol_opn+(int64_t)g_s18_fm1[i*2]*g_vol_opn2+(int64_t)g_s18_pcm[i*2]*g_vol_rf)>>16;
 r+=((int64_t)g_s18_fm0[i*2+1]*g_vol_opn+(int64_t)g_s18_fm1[i*2+1]*g_vol_opn2+(int64_t)g_s18_pcm[i*2+1]*g_vol_rf)>>16;
 }else if(g_vol_opn){l+=mul_q8s(opn[i*2],g_vol_opn);r+=mul_q8s(opn[i*2+1],g_vol_opn);}if(!g_system18&&g_ym2612_dac_enabled&&g_vol_opn){int32_t d=mul_q8s(g_ym2612_dac_sample,g_vol_opn);if(g_ym2612_dac_left)l+=d;if(g_ym2612_dac_right)r+=d;}if(g_vol_opm){l+=mul_q8s(opm[i*2],g_vol_opm);r+=mul_q8s(opm[i*2+1],g_vol_opm);}if(g_vol_opll){l+=mul_q8s(opll[i*2],g_vol_opll);r+=mul_q8s(opll[i*2+1],g_vol_opll);}if(g_vol_sn){l+=mul_q8s(sn[i*2],g_vol_sn);r+=mul_q8s(sn[i*2+1],g_vol_sn);}if(g_vol_ay){l+=mul_q8s(ay[i*2],g_vol_ay);r+=mul_q8s(ay[i*2+1],g_vol_ay);}if(g_vol_gb){l+=mul_q8s(g_gb_pcm[i*2],g_vol_gb);r+=mul_q8s(g_gb_pcm[i*2+1],g_vol_gb);}if(g_vol_nes){l+=mul_q8s(g_nes_pcm[i*2],g_vol_nes);r+=mul_q8s(g_nes_pcm[i*2+1],g_vol_nes);}if(g_vol_pce){l+=mul_q8s(g_pce_pcm[i*2],g_vol_pce);r+=mul_q8s(g_pce_pcm[i*2+1],g_vol_pce);}if(g_vol_sega){l+=mul_q8s(g_sega_pcm[i*2],g_vol_sega);r+=mul_q8s(g_sega_pcm[i*2+1],g_vol_sega);}if(g_vol_oki){l+=mul_q8s(g_oki_pcm[i*2],g_vol_oki);r+=mul_q8s(g_oki_pcm[i*2+1],g_vol_oki);}
if(g_vol_qsound){l+=4*mul_q8s(g_qsound_pcm[i*2],g_vol_qsound);r+=4*mul_q8s(g_qsound_pcm[i*2+1],g_vol_qsound);}if(g_vol_es5503){l+=mul_q8s(g_es5503_pcm[i*2],g_vol_es5503);r+=mul_q8s(g_es5503_pcm[i*2+1],g_vol_es5503);}
if(g_vol_k051649){uint32_t ci;for(ci=0;ci<g_scc_count;ci++){l+=mul_q8s(g_scc_pcm[ci][i*2],g_vol_k051649);r+=mul_q8s(g_scc_pcm[ci][i*2+1],g_vol_k051649);}}
if(g_vol_k053260){l+=mul_q8s(g_k053260_pcm[i*2],g_vol_k053260);r+=mul_q8s(g_k053260_pcm[i*2+1],g_vol_k053260);}
if(g_vol_k054539){uint32_t ci;for(ci=0;ci<g_k054539_count;ci++){l+=mul_q8s(g_k054539_pcm[ci][i*2],g_vol_k054539);r+=mul_q8s(g_k054539_pcm[ci][i*2+1],g_vol_k054539);}}
if(g_vol_neo){l+=mul_q8s(g_neo_pcm[i*2],g_vol_neo);r+=mul_q8s(g_neo_pcm[i*2+1],g_vol_neo);l+=mul_q8s(g_neo_ay_pcm[i*2],g_vol_neo)/2;r+=mul_q8s(g_neo_ay_pcm[i*2+1],g_vol_neo)/2;if(!(g_chip_mask&ZZVGM_CMASK_YM2612)){l+=mul_q8s(opn[i*2],g_vol_neo);r+=mul_q8s(opn[i*2+1],g_vol_neo);}}
 if(g_system18){
#ifdef ZZVGM_S18_HOST
  /* Fingerprint the unboosted production mix against the Step 1 reference. */
  if(l>32767||l<-32768)g_s18_host_pre_gain_clipped_channels++;
  if(r>32767||r<-32768)g_s18_host_pre_gain_clipped_channels++;
  s18_host_hash_sample(sat16(l));s18_host_hash_sample(sat16(r));
#endif
  l*=ZZ_S18_OUTPUT_GAIN;
  r*=ZZ_S18_OUTPUT_GAIN;
#ifdef ZZVGM_S18_HOST
  /* Detect would-be clipping BEFORE sat16; host only, zero ARM overhead. */
  if(l>32767||l<-32768)g_s18_host_clipped_channels++;
  if(r>32767||r<-32768)g_s18_host_clipped_channels++;
  {uint64_t al=l<0?(uint64_t)(-l):(uint64_t)l;
   uint64_t ar=r<0?(uint64_t)(-r):(uint64_t)r;
   if(al>g_s18_host_preclip_peak)g_s18_host_preclip_peak=(uint32_t)al;
   if(ar>g_s18_host_preclip_peak)g_s18_host_preclip_peak=(uint32_t)ar;}
#endif
 }
 ring_put(ring_base+i,sat16(l),sat16(r));}
}

static void render_some(void){
 uint32_t freef,n,remain;
 freef=ring_free();if(freef<64||g_done||g_error)return;
 if(!g_wait){uint32_t w=parse_until_wait();if(w==UINT32_MAX)return;g_wait=w;if(!g_wait)return;}
 n=g_wait;if(n>256)n=256;if(n>=freef)n=freef-1;if(!n)return;
 remain=n;
 while(remain){uint32_t chunk=dac_stream_next_event(remain);if(chunk>remain)chunk=remain;if(!chunk)chunk=1;render_chunk(chunk,g_write_pos);g_write_pos+=chunk;dac_stream_advance(chunk);remain-=chunk;}
 g_wait-=n;g_play_samples+=n;g_render_calls++;sh_write(VSH_PCM_WRITE,g_write_pos);sh_write(VSH_PLAY_SAMPLES,g_play_samples);sh_write(VSH_LOOP_COUNT,g_loops);sh_write(VSH_OPL_WRITES,g_writes);sh_write(VSH_RENDER_CALLS,g_render_calls);sh_write(VSH_LAST_RENDER_FRAMES,n);
}

static void core1_relax_short(void){for(volatile unsigned d=0;d<20000u;d++)__asm__ volatile("nop");}

#ifndef ZZVGM_S18_HOST
void zzvgm_core1_main(void){uint32_t hb=0;unsigned i;mmu_init_zzvgm();for(i=8;i<ZZVGM_SHARED_WORDS;i++)sh_write(i,0);sh_write(VSH_MAGIC,ZZVGM_MAGIC);sh_write(VSH_ABI_VERSION,ZZVGM_ABI_VERSION);sh_write(VSH_CANARY_ARM,ZZVGM_CANARY_ARM);{uint32_t s;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(s));sh_write(VSH_SCTLR,s);}sh_write(VSH_STATUS,ZZVGM_ST_READY);for(;;){uint32_t cmd=sh_read(VSH_CMD);if(cmd==ZZVGM_CMD_STOP||cmd==ZZVGM_CMD_RESET){sh_write(VSH_CMD,ZZVGM_CMD_NONE);sh_write(VSH_STATUS,ZZVGM_ST_STOPPED);break;}if(cmd==ZZVGM_CMD_UPLOAD_BEGIN){g_error=0;if(!upload_begin()){sh_write(VSH_ERROR,g_error);sh_write(VSH_STATUS,ZZVGM_ST_ERROR);}sh_write(VSH_CMD,0);continue;}if(cmd==ZZVGM_CMD_UPLOAD_CHUNK){if(!upload_chunk()){sh_write(VSH_ERROR,g_error);sh_write(VSH_STATUS,ZZVGM_ST_ERROR);}sh_write(VSH_CMD,0);continue;}if(cmd==ZZVGM_CMD_START){g_error=0;if(!vgm_init()){sh_write(VSH_ERROR,g_error);sh_write(VSH_STATUS,ZZVGM_ST_ERROR);}else sh_write(VSH_STATUS,ZZVGM_ST_PLAYING);sh_write(VSH_CMD,0);continue;}if(sh_read(VSH_STATUS)==ZZVGM_ST_PLAYING){if(ring_free()<512u)core1_relax_short();else render_some();if(g_error){sh_write(VSH_ERROR,g_error);sh_write(VSH_STATUS,ZZVGM_ST_ERROR);}else if(g_done)sh_write(VSH_STATUS,ZZVGM_ST_DONE);}else for(volatile unsigned d=0;d<20000;d++){}hb++;if((hb&0x3fff)==0)sh_write(VSH_HEARTBEAT,hb);}cache_clean_invalidate_all();}

#endif /* !ZZVGM_S18_HOST */
