/*
 * zzpico_mmu.c - Core1 MMU for ZZPicoDrive (Core1 memory sections under XACP v1.8).
 *
 * ARMv7 short-descriptor section mappings. Includes cache maintenance,
 * SCTLR and ACTLR setup, and privileged device-memory attributes.
 *
 *   0x045-0x046 blob        WB/WA
 *   0x047       shared      NC
 *   0x04B-0x051 ROM staging WB/WA
 *   0x056       video       WB/WA
 *   0x057       audio       NC
 *   0x058-0x059 diagnostics WB/WA
 *   0x05F       Core1 stack WB/WA
 *   0x220-0x223 priv BSS    WB/WA (4 MB)
 *   0x228-0x22F priv heap   WB/WA
 *   0xF80-0xFFF peripherals Device
 *   (rest)                  Strongly Ordered
 * ASCII only.
 */
#include <stdint.h>

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

/* clean the whole L1 data cache by set/way (Cortex-A9: 4 ways x 256 sets),
   used before the blob parks.  L1 only: the L2 (PL310) is Core0's (XX19c2 rule). */
void mmu_dcache_clean_all(void){ dcache_clean_all_setway(); __asm__ volatile("dsb":::"memory"); }

void dcache_clean_range(void *addr,uint32_t len){
    uint32_t s=(uint32_t)(uintptr_t)addr&~31U, e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;
    while(s<e){ asm volatile("mcr p15,0,%0,c7,c10,1"::"r"(s):"memory"); s+=32; } dsb();
}
void dcache_inval_range(void *addr,uint32_t len){
    uint32_t s=(uint32_t)(uintptr_t)addr&~31U, e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;
    while(s<e){ asm volatile("mcr p15,0,%0,c7,c6,1"::"r"(s):"memory"); s+=32; } dsb();
}

/* Remappe a chaud les sections de 1 MB couvrant [addr,addr+len) en WB/WA (cache).
 * Indispensable pour ecrire VITE dans un framebuffer P96 : sans ca, ces sections
 * sont Strongly-Ordered par defaut et chaque ecriture pixel stalle l'ARM (3d
 * lent). A appeler une fois par nouvelle adresse de buffer. */
void mmu_set_wbwa(uint32_t addr,uint32_t len)
{
    uint32_t s=addr>>20, e=(addr+len-1)>>20, i;
    for(i=s;i<=e;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
    dcache_clean_range(page_table+s,(e-s+1)*4); dsb();
    tlb_flush_all(); icache_inval_all(); bp_inval_all(); dsb(); isb();
}

/* Diag : descripteur de section (1 MB) couvrant addr, tel qu'effectivement
 * dans la page table. Permet de lire les bits TEX/C/B (cacheabilite reelle). */
uint32_t mmu_get_desc(uint32_t addr){ return page_table[addr>>20]; }

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
    /* video 0x056 WB/WA (phase 4) */
    page_table[0x056]=make_sect(0x056,ATTR_NORMAL_WBWA);
    /* audio 0x057 NC (phase 5) */
    page_table[0x057]=make_sect(0x057,ATTR_NORMAL_NC);
    /* debug/margin 0x058-0x059 WB/WA */
    page_table[0x058]=make_sect(0x058,ATTR_NORMAL_WBWA);
    page_table[0x059]=make_sect(0x059,ATTR_NORMAL_WBWA);
    /* Core1 stack window 0x05F uses WB/WA.  The firmware starts CPU1
       with SP = 0x06000000 (core1_loop) and the blob keeps that stack; this
       section was left Strongly Ordered, so every push, pop and spilled local
       went to DDR uncached.  Measured on ZZPicoDriveSMS: IPC 0.23 -> 1.2, the
       emulation about 4 times faster.  Same mapping as ZZQuake / ZZDarkForces /
       ZZOpenLara.  Nothing else lives here; it is cleaned with the rest of L1
       before the park and Core0 flushes L2 at ARM_RUN=0, as for the BSS. */
    page_table[0x05F]=make_sect(0x05F,ATTR_NORMAL_WBWA);
    /* private BSS 0x220-0x223 WB/WA (4 MB) */
    for(i=0x220;i<=0x223;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
    /* private heap 0x228-0x22F WB/WA */
    for(i=0x228;i<=0x22F;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
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
