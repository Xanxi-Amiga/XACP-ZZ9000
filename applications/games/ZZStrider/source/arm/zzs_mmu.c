/*
 * zzs_mmu.c - Core1 MMU for ZZStrider (firmware XX19d-RIX, XACP v1.8).
 *
 * TECHNIQUE: unchanged from zzrastan_mmu.c, itself copied from the validated ZZDoom mmu_init.c
 * (page table, descriptor bits, set/way clean, ACTLR.SMP, enable sequence, by-MVA maintenance).
 * CHANGED: only the section map, re-pointed to the ZZStrider plan (docs/MEMORY_MAP.md):
 *
 *   0x045-0x046 blob            WB/WA
 *   0x047       shared slots    NC
 *   0x04B       ROM chunk stage NC   (the SAME window is rewritten by the 68k for every chunk,
 *                                     so Core1 must never read it through a cache; NC is the
 *                                     rule already established for the ZZQuake/ZZMIDI staging)
 *   0x057       PCM ring        NC
 *   0x220-0x22F private 16 MiB  WB/WA (BSS + stack, ROM store, heap, CPS-1 work memory)
 *   0xF80-0xFFF peripherals     Device
 *   (rest)                      Strongly Ordered
 *   + the Picasso96 buffers, remapped WB/WA on demand by mmu_set_wbwa()
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

void dcache_clean_range(void *addr,uint32_t len){
    uint32_t s=(uint32_t)(uintptr_t)addr&~31U, e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;
    while(s<e){ asm volatile("mcr p15,0,%0,c7,c10,1"::"r"(s):"memory"); s+=32; } dsb();
}
void dcache_inval_range(void *addr,uint32_t len){
    uint32_t s=(uint32_t)(uintptr_t)addr&~31U, e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;
    while(s<e){ asm volatile("mcr p15,0,%0,c7,c6,1"::"r"(s):"memory"); s+=32; } dsb();
}

/* Remappe a chaud les sections de 1 MB couvrant addr..addr+len-1 en WB/WA (cache).
 * Indispensable pour ecrire VITE dans un framebuffer P96 : sans ca, ces sections
 * sont Strongly-Ordered par defaut et chaque ecriture pixel stalle l'ARM (3d
 * lent). A appeler une fois par nouvelle adresse de buffer. */
/* PL310 (L2C-310) of the Zynq, registers as in the validated ZZQuakeGL / ZZQuake2 blobs. */
#define PL310_BASE      0xF8F02000u
#define PL310_CTRL      (*(volatile uint32_t *)(PL310_BASE + 0x100u))
#define PL310_SYNC      (*(volatile uint32_t *)(PL310_BASE + 0x730u))
#define PL310_CLEAN_PA  (*(volatile uint32_t *)(PL310_BASE + 0x7B0u))

int g_zzs_l2_fix = 1;

uint32_t zzs_l2_enabled(void) { return PL310_CTRL & 1u; }

/* Remap the 1 MiB sections covering addr..addr+len-1 WB/WA. Called once per new P96 buffer.
 *
 * A2b. The page table lives in cached memory, while the table walks of this MMU are
 * non-cacheable (TTBR0 walk attribute bits are 0). A by-MVA clean (DCCMVAC) pushes the new
 * descriptor out of L1 only: on the Zynq it then sits dirty in the PL310 L2, and a walk reads
 * the OLD Strongly-Ordered descriptor from DDR, which the TLB then keeps. The firmware cleans
 * the whole L2 at every vblank, so the new value reaches DDR within one frame, but a walk made
 * in between leaves that buffer uncached for as long as the TLB holds the entry.
 * Measured in A2 (25/11): conversion 3-5 ms on two buffers, 16-20 ms on exactly one third.
 * Fix: clean the descriptor lines through the PL310 by physical address (identity map), wait
 * for the sync, THEN invalidate the TLB. */
void mmu_set_wbwa(uint32_t addr,uint32_t len)
{
    uint32_t s=addr>>20, e=(addr+len-1)>>20, i, p;
    for(i=s;i<=e;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
    dcache_clean_range(page_table+s,(e-s+1)*4); dsb();
    if (g_zzs_l2_fix && (PL310_CTRL & 1u)) {
        uint32_t a=(uint32_t)(uintptr_t)(page_table+s)&~31U, b=(uint32_t)(uintptr_t)(page_table+e+1);
        for (p=a; p<b; p+=32u) { PL310_CLEAN_PA = p; while (PL310_CLEAN_PA & 1u) { } }
        PL310_SYNC = 0u; while (PL310_SYNC & 1u) { }
        dsb();
    }
    tlb_flush_all(); icache_inval_all(); bp_inval_all(); dsb(); isb();
}

/* What the MMU really uses for addr: stage 1 privileged read translation (ATS1CPR), PAR.
   PAR short format (ARMv7 ARM B4.1.112): bit 0 fault, [6:4] inner attributes (0 Non-cacheable,
   1 Strongly-ordered, 3 Device, 5 Write-Back Write-Allocate, 6 Write-Through, 7 Write-Back no
   Write-Allocate), [3:2] outer, bit 7 shareable, [31:12] physical address. */
uint32_t zzs_mmu_par(uint32_t addr)
{
    uint32_t par;
    asm volatile("mcr p15,0,%0,c7,c8,0"::"r"(addr):"memory");
    isb();
    asm volatile("mrc p15,0,%0,c7,c4,0":"=r"(par));
    return par;
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
    /* shared slots 0x047 NC */
    page_table[0x047]=make_sect(0x047,ATTR_NORMAL_NC);
    /* ROM chunk stage 0x04B NC */
    page_table[0x04B]=make_sect(0x04B,ATTR_NORMAL_NC);
    /* PCM ring 0x057 NC */
    page_table[0x057]=make_sect(0x057,ATTR_NORMAL_NC);
    /* private window 0x220-0x22F WB/WA (16 MiB) */
    for(i=0x220;i<=0x22F;i++) page_table[i]=make_sect(i,ATTR_NORMAL_WBWA);
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

/* PMU cycle counter (CCNT, divider off): the only clock of the blob. Kept here with the other
   privileged CP15 accesses so that zzs_main.c holds no hardware access of its own. */
/* The six event counters are set up exactly as in the ZZPicoDriveSMS blob, where they were
   validated on the board (DDI0388 table 11-5): 0x68 instructions through the rename stage,
   0x10 branches mispredicted, 0x03 L1 D refills, 0x01 L1 I refills, 0x61 data-side stall
   cycles, 0x60 instruction-side stall cycles. */
#define ZZS_PMU_NEV 6
static const uint32_t g_pmu_evt[ZZS_PMU_NEV] = { 0x68u, 0x10u, 0x03u, 0x01u, 0x61u, 0x60u };

void zzs_pmu_enable(void)
{
    uint32_t i, v = 1u | (1u << 2);                                   /* enable, reset CCNT */
    uint32_t en = 0x80000000u | ((1u << ZZS_PMU_NEV) - 1u);
    for (i = 0; i < ZZS_PMU_NEV; i++) {
        asm volatile("mcr p15,0,%0,c9,c12,5"::"r"(i));                /* PMSELR */
        asm volatile("isb");
        asm volatile("mcr p15,0,%0,c9,c13,1"::"r"(g_pmu_evt[i]));     /* PMXEVTYPER */
    }
    asm volatile("mcr p15,0,%0,c9,c12,0"::"r"(v));
    asm volatile("mcr p15,0,%0,c9,c12,1"::"r"(en));
}

void zzs_pmu_events(uint32_t *v)
{
    uint32_t i;
    for (i = 0; i < ZZS_PMU_NEV; i++) {
        asm volatile("mcr p15,0,%0,c9,c12,5"::"r"(i));
        asm volatile("isb");
        asm volatile("mrc p15,0,%0,c9,c13,2":"=r"(v[i]));             /* PMXEVCNTR */
    }
}

/* ACTLR bit 8, "Alloc in one way" (Cortex-A9 TRM 4.3.10): linefills allocate in one cache way
   only, "for use with memory copy operations to reduce cache pollution". Read-modify-write,
   as the TRM requires; every other bit (SMP, prefetch, the firmware's own) is preserved. */
uint32_t zzs_actlr_one_way(int on)
{
    uint32_t a = read_actlr();
    if (on) a |= (1U << 8); else a &= ~(1U << 8);
    write_actlr(a); isb();
    return a;
}

uint32_t zzs_pmu_cycles(void)
{
    uint32_t v;
    asm volatile("mrc p15,0,%0,c9,c13,0":"=r"(v));
    return v;
}
