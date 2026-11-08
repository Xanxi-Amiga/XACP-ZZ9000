/*
 * zzvr_mmu.c - Core1 MMU for ZZVirtuaRacingRIX (XX19d-RIX / XACP v1.8).
 *
 * TECHNIQUE: the validated ZZPicoDriveMD 1.1 zzpico_mmu.c (itself from the
 * ZZDoom mmu_init: page table, descriptor bits, set/way clean, ACTLR.SMP,
 * enable sequence, by-MVA maintenance).  Unchanged code paths.
 * CHANGED: only the section map.
 *   - the v1.6 private pool sections (0x220-0x223 BSS, 0x228-0x22F heap) are
 *     NOT mapped any more: nothing of this blob lives in 0x22000000..0x30000000;
 *   - the XACP v1.7 Core1 arena 0x30000000..0x3F800000 (sections 0x300..0x3F7)
 *     is mapped Normal WB/WA, as the ZZQuake2RIX / ZZOpenLaraRIX blobs do
 *     (arena contract: each Core1 blob maps the arena itself);
 *   - the guard 0x3F800000..0x3FC00000 and the hardware top keep the default
 *     Strongly Ordered descriptor of this table (as every unlisted section in
 *     the 1.1 model) and are never accessed by this blob.
 *
 *   0x045-0x046 blob        WB/WA
 *   0x047       shared      NC
 *   0x04B-0x051 ROM staging WB/WA
 *   0x056       video       WB/WA
 *   0x057       audio       NC
 *   0x058-0x059 debug/marge WB/WA
 *   0x05F       Core1 stack WB/WA  (A1a2: the firmware gives the blob SP =
 *               0x06000000, growing down into this section; ZZQuake release
 *               maps it WB/WA for the same reason - "every push/pop crawls"
 *               otherwise; 1.1 left it Strongly Ordered, harmless for Cyclone,
 *               fatal for the C-only SH-2 interpreter and the DRC handlers)
 *   0x066-0x067 RasterIX GPU window Normal NC (A3: colour buffers FB0/FB1,
 *               chunk ring; fences polled), as ZZQuake2RIX / ZZOpenLaraRIX map it
 *   0x300-0x3F7 Core1 arena WB/WA (BSS, sbrk heap, mmap pool, DRC cache),
 *               except 0x3E8 Normal NC (A3a3: RasterIX chunk ring)
 *   (A3b3: 0x066-0x067 switched to 4 KiB pages at run time, mmu_rix_window_cached)
 *   0xF80-0xFFF peripherals Device
 *   (rest)                  Strongly Ordered (save buffer 0x07E00000 included,
 *                           accessed exactly as 1.1 does, with clean/inval)
 *
 * P96 backbuffers are remapped WB/WA at run time by mmu_set_wbwa() exactly as
 * in 1.1 (the launcher publishes their addresses).
 * ASCII only.
 */
#include <stdint.h>
#include "zzvr_mem.h"

static uint32_t __attribute__((aligned(0x4000))) page_table[4096];

#define SECT_TYPE      (1U<<1)
#define SECT_B         (1U<<2)
#define SECT_C         (1U<<3)
#define SECT_DOMAIN(d) ((uint32_t)(d)<<5)
#define SECT_AP_RW     (0x3U<<10)
#define SECT_TEX(t)    ((uint32_t)(t)<<12)
#define SECT_S         (1U<<16)
#define ATTR_STRONGLY_ORDERED (0U)
#define ATTR_DEVICE           (SECT_B)
#define ATTR_NORMAL_NC        (SECT_TEX(1U))
#define ATTR_NORMAL_WBWA      (SECT_TEX(1U)|SECT_C|SECT_B)
#define BASE_FLAGS (SECT_TYPE|SECT_DOMAIN(0)|SECT_AP_RW|SECT_S)
static inline uint32_t make_sect(uint32_t mb,uint32_t a){ return (mb<<20)|BASE_FLAGS|a; }

static inline void write_ttbr0(uint32_t v){ asm volatile("mcr p15,0,%0,c2,c0,0"::"r"(v):"memory"); }
static inline void write_ttbcr(uint32_t v){ asm volatile("mcr p15,0,%0,c2,c0,2"::"r"(v):"memory"); }
static inline void write_dacr(uint32_t v){ asm volatile("mcr p15,0,%0,c3,c0,0"::"r"(v):"memory"); }
static inline uint32_t read_sctlr(void){ uint32_t v; asm volatile("mrc p15,0,%0,c1,c0,0":"=r"(v)); return v; }
static inline void write_sctlr(uint32_t v){ asm volatile("mcr p15,0,%0,c1,c0,0"::"r"(v):"memory"); }
static inline uint32_t read_actlr(void){ uint32_t v; asm volatile("mrc p15,0,%0,c1,c0,1":"=r"(v)); return v; }
static inline void write_actlr(uint32_t v){ asm volatile("mcr p15,0,%0,c1,c0,1"::"r"(v):"memory"); }
static inline void tlb_flush_all(void){ asm volatile("mcr p15,0,%0,c8,c7,0"::"r"(0):"memory"); }
static inline void icache_inval_all(void){ asm volatile("mcr p15,0,%0,c7,c5,0"::"r"(0):"memory"); }
static inline void bp_inval_all(void){ asm volatile("mcr p15,0,%0,c7,c5,6"::"r"(0):"memory"); }
static inline void dsb(void){ asm volatile("dsb":::"memory"); }
static inline void isb(void){ asm volatile("isb":::"memory"); }

static void dcache_clean_all_setway(void){
    for(uint32_t w=0;w<4;w++) for(uint32_t s=0;s<256;s++){
        uint32_t sw=(w<<30)|(s<<5); asm volatile("mcr p15,0,%0,c7,c10,2"::"r"(sw):"memory"); }
    dsb();
}
static void dcache_inval_all_setway(void){
    for(uint32_t w=0;w<4;w++) for(uint32_t s=0;s<256;s++){
        uint32_t sw=(w<<30)|(s<<5); asm volatile("mcr p15,0,%0,c7,c6,2"::"r"(sw):"memory"); }
    dsb();
}

void mmu_dcache_clean_all(void){ dcache_clean_all_setway(); dsb(); }   /* v1.2: before the park */

void dcache_clean_range(void *addr,uint32_t len){
    uint32_t s=(uint32_t)(uintptr_t)addr&~31U, e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;
    while(s<e){ asm volatile("mcr p15,0,%0,c7,c10,1"::"r"(s):"memory"); s+=32; } dsb();
}
void dcache_inval_range(void *addr,uint32_t len){
    uint32_t s=(uint32_t)(uintptr_t)addr&~31U, e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;
    while(s<e){ asm volatile("mcr p15,0,%0,c7,c6,1"::"r"(s):"memory"); s+=32; } dsb();
}

/* Code written by the CPU and then executed (SH-2 recompiler, A1b): clean the
 * data cache to the point of unification, invalidate the instruction cache
 * and the branch predictor over the range, then barriers.  PicoDrive calls it
 * cache_flush_d_inval_i(start, end) (cpu/drc/cmn.c, cpu/sh2/compiler.c). */
void cache_flush_d_inval_i(void *start, void *end)
{
    uint32_t s=(uint32_t)(uintptr_t)start&~31U, e=((uint32_t)(uintptr_t)end+31U)&~31U, p;
    for(p=s;p<e;p+=32) asm volatile("mcr p15,0,%0,c7,c11,1"::"r"(p):"memory");   /* DCCMVAU */
    dsb();
    for(p=s;p<e;p+=32) asm volatile("mcr p15,0,%0,c7,c5,1"::"r"(p):"memory");    /* ICIMVAU */
    bp_inval_all();
    dsb(); isb();
}

/* Remap the 1 MB sections covering [addr,addr+len) as WB/WA (cached), at run
 * time.  Needed to write FAST into a P96 framebuffer: by default those sections
 * are Strongly Ordered and every pixel write stalls the ARM.  Once per new
 * buffer address (1.1 behaviour, unchanged). */
void mmu_set_wbwa(uint32_t addr,uint32_t len)
{
    uint32_t s=addr>>20, e=(addr+len-1)>>20, i;
    for(i=s;i<=e;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
    dcache_clean_range(page_table+s,(e-s+1)*4); dsb();
    tlb_flush_all(); icache_inval_all(); bp_inval_all(); dsb(); isb();
}

/* Diag: the section descriptor (1 MB) covering addr, as it is in the table. */
uint32_t mmu_get_desc(uint32_t addr){ return page_table[addr>>20]; }

/* A3b3: the GPU window 0x06600000..0x06800000 with 4 KiB pages, switched on at run
   time by zzvr_rix.c (after librixgpu's init and the colour calibration, which read
   the window uncached): the lines 0..479 of the colour buffers FB0 (0x06600000) and
   FB1 (0x06696500) inner write-back write-allocate / outer non-cacheable (Core1
   reads the GPU's 640 x 448 picture through L1, after invalidating it; never
   through L2, which the GPU's writes would not update); every 4 KiB page that holds
   a byte of a fence line (480) or of anything else (FB2, texture area, firmware
   staging at 0x067F0000) stays Normal non-cacheable.  A3b1/A3b2 had the colour
   buffers in the Core1 arena (0x3EA00000/0x3EB00000): the A4000 reset-looped in a
   race after 82 s with them, and never without the GPU path; OpenLara and Quake 2
   keep their colour buffers in this window.  Small-page descriptors (short
   format): XN, B, C, AP[1:0]=11 (full access), TEX, S. */
static uint32_t __attribute__((aligned(1024))) rix_l2[2][256];
#define SP_SMALL_XN   (2U|1U)
#define SP_B          (1U<<2)
#define SP_C          (1U<<3)
#define SP_AP_RW      (3U<<4)
#define SP_TEX(t)     ((uint32_t)(t)<<6)
#define SP_S          (1U<<10)
/* TEX[2]=1: outer (TEX[1:0]) NC, inner (C:B): A3b3 01 WB-WA; A8a 11 WB no write-allocate:
   Core1 still reads the GPU's lines through L1 (A6c path), but its writes to a line not in
   L1 (A7's prefill of 491 KB a picture, the HUD's pixels) go out without fetching it */
#define SP_IWB_ONC    (SP_TEX(4U)|SP_C|SP_B)
#define SP_NORMAL_NC  (SP_TEX(1U))
#define RIXW_FB0      0x06600000U
#define RIXW_FB1      0x06696500U
#define RIXW_LINES    614400U               /* 640 x 480 x 2: lines 0..479, the fence line after */
static int rixw_cached_page(uint32_t pa)
{
    uint32_t e=pa+4096U;
    if(pa>=RIXW_FB0 && e<=RIXW_FB0+RIXW_LINES) return 1;
    if(pa>=RIXW_FB1 && e<=RIXW_FB1+RIXW_LINES) return 1;
    return 0;
}
void mmu_rix_window_cached(void)
{
    uint32_t t, i;
    for(t=0;t<2;t++){
        uint32_t section=0x066U+t;
        for(i=0;i<256;i++){
            uint32_t pa=(section<<20)|(i<<12);
            rix_l2[t][i]=pa|SP_SMALL_XN|SP_AP_RW|SP_S|(rixw_cached_page(pa)?SP_IWB_ONC:SP_NORMAL_NC);
        }
    }
    dcache_clean_range(rix_l2,sizeof(rix_l2)); dsb();
    for(t=0;t<2;t++) page_table[0x066U+t]=((uint32_t)(uintptr_t)rix_l2[t] & 0xFFFFFC00U)|1U;   /* coarse table, domain 0 */
    dcache_clean_range(page_table+0x066,2*4); dsb();
    tlb_flush_all(); bp_inval_all(); dsb(); isb();
}

void mmu_init(void)
{
    uint32_t i;
    { uint32_t sctlr=read_sctlr();
      if(sctlr&(1U<<2)) dcache_clean_all_setway();
      sctlr&=~(1U<<0); sctlr&=~(1U<<2); write_sctlr(sctlr); dsb(); isb(); }
    dcache_inval_all_setway(); icache_inval_all(); bp_inval_all(); tlb_flush_all(); dsb(); isb();
    { uint32_t a=read_actlr(); a|=(1U<<6); write_actlr(a); dsb(); isb(); }   /* SMP */

    for(i=0;i<4096;i++) page_table[i]=make_sect(i,ATTR_STRONGLY_ORDERED);

    /* blob 0x045-0x046 WB/WA */
    page_table[0x045]=make_sect(0x045,ATTR_NORMAL_WBWA);
    page_table[0x046]=make_sect(0x046,ATTR_NORMAL_WBWA);
    /* shared 0x047 NC */
    page_table[0x047]=make_sect(0x047,ATTR_NORMAL_NC);
    /* ROM staging 0x04B-0x051 WB/WA */
    for(i=0x04B;i<=0x051;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
    /* video 0x056 WB/WA */
    page_table[0x056]=make_sect(0x056,ATTR_NORMAL_WBWA);
    /* audio 0x057 NC */
    page_table[0x057]=make_sect(0x057,ATTR_NORMAL_NC);
    /* debug/margin 0x058-0x059 WB/WA */
    page_table[0x058]=make_sect(0x058,ATTR_NORMAL_WBWA);
    page_table[0x059]=make_sect(0x059,ATTR_NORMAL_WBWA);
    /* Core1 stack window 0x05F (firmware SP 0x06000000, grows down): cached,
       as the ZZQuake release blob maps it (zzquake_mmu.c).  Nothing else lives
       here (XMID control block starts at fb+0x06000000 = ARM 0x06200000). */
    page_table[0x05F]=make_sect(0x05F,ATTR_NORMAL_WBWA);
    /* A3: RasterIX GPU window 0x066-0x067 Normal non-cacheable (zzvr_rix.c cleans
       and invalidates L2 over it by physical address before the first use) */
    page_table[0x066]=make_sect(0x066,ATTR_NORMAL_NC);
    page_table[0x067]=make_sect(0x067,ATTR_NORMAL_NC);
    /* XACP v1.7 Core1 arena 0x300-0x3F7 WB/WA (BSS, heaps, DRC cache) */
    for(i=(XACP_ARENA_BASE>>20);i<(XACP_ARENA_END>>20);i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
    /* A3a3: the RasterIX chunk ring 0x3E800000 (1 MiB section) Normal NC: the GPU
       fetches the lists Core1 writes there (ZZQuake2RIX / ZZOpenLaraRIX model) */
    page_table[ZZVR_ARENA_RIX_RING>>20]=make_sect(ZZVR_ARENA_RIX_RING>>20,ATTR_NORMAL_NC);

    /* peripherals 0xF80-0xFFF Device */
    for(i=0xF80;i<0x1000;i++) page_table[i]=make_sect(i,ATTR_DEVICE);

    dcache_clean_range(page_table,sizeof(page_table)); dsb();
    write_ttbcr(0); write_dacr(0x00000001U);
    write_ttbr0((uint32_t)(uintptr_t)page_table);
    tlb_flush_all(); icache_inval_all(); bp_inval_all(); dsb(); isb();
    { uint32_t sctlr=read_sctlr();
      sctlr|=(1U<<0); sctlr|=(1U<<2); sctlr|=(1U<<12); sctlr|=(1U<<22); sctlr&=~(1U<<1);
      write_sctlr(sctlr); }
    dsb(); isb();
}
