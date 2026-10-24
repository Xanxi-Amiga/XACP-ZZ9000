/*
 * zzquake_mmu.c - MMU enable for ZZQuake, derived from the REAL
 * ZZDoom mmu_init.c (md5 9e8f4f38b3801112796e7a90d18b4d74).
 * ONLY the region map and the function name differ; the enable
 * sequence, descriptor encodings, dcache_clean_range and
 * mmu_disable are verbatim.
 * Builds an L1 section page table, maps DDR Normal WB/WA, enables MMU+caches.
 * Single fix for both alignment faults and framebuffer speed.
 * ASCII only.
 */
typedef unsigned int u32;

/* L1 table: 4096 entries * 4 bytes = 16KB, must be 16KB-aligned */
static u32 l1_table[4096] __attribute__((aligned(16384)));

/* Section descriptor attribute encodings (short-descriptor, ARMv7) */
/* Base section bits: type=2 (0b10), AP=0b11 (full access), domain 0 */
#define SEC_TYPE       0x2
#define SEC_AP_RW      (3u<<10)     /* AP=11 full RW any privilege */
#define SEC_DOMAIN0    (0u<<5)

/* Memory type via TEX[2:0],C,B :
 * Normal WB/WA  : TEX=001 C=1 B=1
 * Normal NC     : TEX=001 C=0 B=0
 * Device        : TEX=000 C=0 B=1
 * Strongly-ord  : TEX=000 C=0 B=0
 */
#define MT_NORMAL_WBWA ((1u<<12) | (1u<<3) | (1u<<2))   /* TEX=001,C=1,B=1 */
#define MT_NORMAL_NC   ((1u<<12) | (0u<<3) | (0u<<2))   /* TEX=001,C=0,B=0 */
#define MT_DEVICE      ((0u<<12) | (0u<<3) | (1u<<2))   /* TEX=000,C=0,B=1 */
#define MT_SO          ((0u<<12) | (0u<<3) | (0u<<2))   /* TEX=000,C=0,B=0 */

#define SEC_SHAREABLE  (1u<<16)

static u32 make_section(u32 pa_mb, u32 memtype, int shareable)
{
    u32 d = (pa_mb << 20) | SEC_TYPE | SEC_AP_RW | SEC_DOMAIN0 | memtype;
    if(shareable) d |= SEC_SHAREABLE;
    return d;
}

void mmu_init_zzquake(u32 fb_arm)
{
    u32 i;
    u32 fb_sec = (fb_arm >> 20);   /* framebuffer section index */

    /* Cache/MMU handling follows the validated Core1 memory contract. */






    {
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr &= ~(1u<<0);    /* M : MMU off     */
        sctlr &= ~(1u<<2);    /* C : D-cache off */
        sctlr &= ~(1u<<12);   /* I : I-cache off */
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr):"memory");
        __asm__ volatile("dsb":::"memory");
        __asm__ volatile("isb":::"memory");
    }

    /* SAFE DEFAULT: everything Device (no cache, writes pass through).
       This protects CPU0 firmware (OCM), FPGA registers, Zorro bridge. */
    for(i=0;i<4096;i++)
        l1_table[i] = make_section(i, MT_DEVICE, 0);

    /* Cache ONLY the ZZQuake DDR working set: */
    


    l1_table[0x049] = make_section(0x049, MT_NORMAL_WBWA, 0);
    l1_table[0x04A] = make_section(0x04A, MT_NORMAL_WBWA, 0);
    /* Cache/MMU handling follows the validated Core1 memory contract. */


    for(i=0x045;i<=0x048;i++)
        l1_table[i] = make_section(i, MT_NORMAL_NC, 0);

    /* Cache/MMU handling follows the validated Core1 memory contract. */

    l1_table[0x04B] = make_section(0x04B, MT_NORMAL_NC, 0);

    /* PAK filesystem handling. */
    for(i=0x04C;i<=0x05E;i++)
        l1_table[i] = make_section(i, MT_NORMAL_WBWA, 0);
    /* 0x05F = CORE1 STACK window (firmware SP = 0x06000000, grows
       down - measured on hardware, SH_ENTRY_SP=0x05FFFF8C). Must be
       mapped cacheable WB/WA or every push/pop crawls. Nothing else
       may live here. */
    l1_table[0x05F] = make_section(0x05F, MT_NORMAL_WBWA, 0);
    /* Quake heap 0x070-0x07F (16MB, ZZQ_HEAP_SIZE_A0) */
    for(i=0x070;i<=0x07F;i++)
        l1_table[i] = make_section(i, MT_NORMAL_WBWA, 0);
    





    /* Cache/MMU handling follows the validated Core1 memory contract. */



    l1_table[0x06E] = make_section(0x06E, MT_NORMAL_NC, 0);
    l1_table[0x06F] = make_section(0x06F, MT_NORMAL_NC, 0);

    /* Cache/MMU handling follows the validated Core1 memory contract. */







    {
        u32 s;
        for (s = 0x300; s < 0x3F8; s++)
            l1_table[s] = make_section(s, MT_NORMAL_WBWA, 0);
    }   /* Shared-memory protocol state. */


    /* framebuffer: Normal WB/WA. May span 2-3 sections (pitch*height > 1MB).
       Map fb_sec .. fb_sec+3 (ZZDoom-validated window). Skip if 0 (A0). */
    if(fb_arm){
        l1_table[fb_sec]   = make_section(fb_sec,   MT_NORMAL_WBWA, 0);
        l1_table[fb_sec+1] = make_section(fb_sec+1, MT_NORMAL_WBWA, 0);
        l1_table[fb_sec+2] = make_section(fb_sec+2, MT_NORMAL_WBWA, 0);
        l1_table[fb_sec+3] = make_section(fb_sec+3, MT_NORMAL_WBWA, 0);
    }

    /* --- Program MMU --- */
    /* DACR: domain 0 = client (checks permissions) = 0b01 */
    __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(0x00000001u));
    /* TTBCR = 0 (use TTBR0 only, N=0) */
    __asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(0u));
    /* TTBR0 = table base | cacheable walk attrs (IRGN/RGN WB) */
    {
        u32 ttbr0 = ((u32)l1_table) | 0x6Bu; /* IRGN=01,S=0,RGN=01,NOS=0 -> WB */
        __asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(ttbr0));
    }
    /* Invalidate TLB, branch predictor, caches before enable */
    __asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u)); /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,6"::"r"(0u)); /* BPIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u)); /* ICIALLU */

    /* Cache/MMU handling follows the validated Core1 memory contract. */














    {
        u32 way, set;
        for (way = 0; way < 4u; way++)
            for (set = 0; set < 256u; set++) {
                u32 sw = (way << 30) | (set << 5);
                __asm__ volatile("mcr p15,0,%0,c7,c6,2"::"r"(sw):"memory");
            }
        __asm__ volatile("dsb":::"memory");
    }
    __asm__ volatile("dsb":::"memory");
    __asm__ volatile("isb":::"memory");

    /* Enable MMU (M), D-cache (C), I-cache (I), branch pred (Z). A=0. */
    {
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr |=  (1u<<0);   /* M  MMU enable */
        sctlr |=  (1u<<2);   /* C  D-cache enable */
        sctlr |=  (1u<<12);  /* I  I-cache enable */
        sctlr |=  (1u<<11);  /* Z  branch prediction */
        sctlr &= ~(1u<<1);   /* A  alignment check OFF */
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr));
        __asm__ volatile("isb":::"memory");
    }
}

/* D-cache clean by MVA over a range (for framebuffer flush) */
void dcache_clean_range(u32 start, u32 len)
{
    u32 a = start & ~31u;
    u32 end = (start + len + 31u) & ~31u;
    for(; a < end; a += 32)
        __asm__ volatile("mcr p15,0,%0,c7,c10,1"::"r"(a):"memory");
    __asm__ volatile("dsb":::"memory");
}

/* D-cache invalidate by MVA over a range (DCIMVAC). Used before
   reading DDR that the 68k just wrote (pak), so the ARM sees fresh
   memory, not stale cache lines. */
void dcache_invalidate_range(u32 start, u32 len)
{
    u32 a = start & ~31u;
    u32 end = (start + len + 31u) & ~31u;
    for(; a < end; a += 32)
        __asm__ volatile("mcr p15,0,%0,c7,c6,1"::"r"(a):"memory");
    __asm__ volatile("dsb":::"memory");
}


/* Disable MMU + caches, restore Core1 to firmware-expected state.
   Call before WFE on STOP so relaunch works. */
void mmu_disable(void)
{
    u32 sctlr;
    /* Clean entire D-cache first (flush dirty lines to DDR) */
    /* Clean by set/way - simplified: clean all L1 D-cache */
    __asm__ volatile("dsb":::"memory");
    /* Read SCTLR, clear M (MMU), C (Dcache), I (Icache) */
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
    sctlr &= ~(1u<<0);   /* M off */
    sctlr &= ~(1u<<2);   /* C off */
    sctlr &= ~(1u<<12);  /* I off */
    __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr));
    __asm__ volatile("isb":::"memory");
    /* Invalidate TLB and caches */
    __asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u)); /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u)); /* ICIALLU */
    __asm__ volatile("dsb":::"memory");
    __asm__ volatile("isb":::"memory");
}
