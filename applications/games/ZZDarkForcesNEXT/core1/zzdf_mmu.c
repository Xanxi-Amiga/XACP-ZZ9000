 















typedef unsigned int u32;

 








u32 l1_table[4096]
    __attribute__((aligned(16384)))
    __attribute__((section(".data.mmu")))
    = { 0xDEADBEEFu };

#define SEC_TYPE       0x2
#define SEC_AP_RW      (3u<<10)
#define SEC_DOMAIN0    (0u<<5)

#define MT_NORMAL_WBWA ((1u<<12) | (1u<<3) | (1u<<2))
#define MT_NORMAL_NC   ((1u<<12) | (0u<<3) | (0u<<2))
#define MT_DEVICE      ((0u<<12) | (0u<<3) | (1u<<2))
#define MT_SO          ((0u<<12) | (0u<<3) | (0u<<2))

#define SEC_SHAREABLE  (1u<<16)

 








#ifdef ZZDF_RETURN_OUTER_NC
#define MT_PRIVATE     ((4u<<12) | (1u<<3) | (1u<<2))
#else
#define MT_PRIVATE     MT_NORMAL_WBWA
#endif

static u32 make_section(u32 pa_mb, u32 memtype, int shareable)
{
    u32 d = (pa_mb << 20) | SEC_TYPE | SEC_AP_RW | SEC_DOMAIN0 | memtype;
    if(shareable) d |= SEC_SHAREABLE;
    return d;
}

void mmu_init_zzdf(u32 fb_arm)
{
    u32 i;
    u32 fb_sec = (fb_arm >> 20);

    {   /* MMU + caches off before rewriting the table */
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr &= ~(1u<<0);
        sctlr &= ~(1u<<2);
        sctlr &= ~(1u<<12);
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr):"memory");
        __asm__ volatile("dsb":::"memory");
        __asm__ volatile("isb":::"memory");
    }

    /* SAFE DEFAULT: everything Device. Protects CPU0 firmware (OCM),
       FPGA registers, Zorro bridge. */
    for(i=0;i<4096;i++)
        l1_table[i] = make_section(i, MT_DEVICE, 0);

    /* Blob (code+rodata+data+bss): cached */
    l1_table[0x049] = make_section(0x049, MT_PRIVATE, 0);
    l1_table[0x04A] = make_section(0x04A, MT_PRIVATE, 0);

    /* Staging 0x045-0x048: NC (mailbox-adjacent, 68k-written) */
    for(i=0x045;i<=0x048;i++)
        l1_table[i] = make_section(i, MT_NORMAL_NC, 0);

    /* PCM ring: Normal non-cacheable because Core1 writes while the 68k reads. */
    l1_table[0x044] = make_section(0x044, MT_NORMAL_NC, 0);

    /* Shared mailbox: NC - zero flush/invalidate contract */
    l1_table[0x04B] = make_section(0x04B, MT_NORMAL_NC, 0);

    /* Core1 stack window (firmware SP = 0x06000000, grows down).
       Must be WB/WA or every push/pop crawls. Nothing else here. */
    /* Blob BSS window: the engine's .bss (renderer states etc) lives
       at 0x05000000, outside the 2MB loaded-image reserve. Cached. */
    for (i = 0x050; i <= 0x05E; i++)
    {
        l1_table[i] = make_section(i, MT_PRIVATE, 0);
    }

    l1_table[0x05F] = make_section(0x05F, MT_PRIVATE, 0);

    /* Legacy heap window 0x070-0x07F (16MB). Kept mapped for this
       revision; _sbrk no longer uses it - the heap lives in the high
       DDR super-zone above the assets (see zzdf_config.h). */
    for(i=0x070;i<=0x07F;i++)
        l1_table[i] = make_section(i, MT_PRIVATE, 0);

    /* Asset arena 0x300-0x3F7: DARK.GOB and friends, loaded by the
       68k BEFORE Core1 start, then read-many by the engine: cached.
       0x3F8+ (firmware guard) stays Device - never mapped Normal. */
#ifndef ZZDF_RETURN_OUTER_NC
    {
        u32 s;
        for (s = 0x300; s < 0x3F8; s++)
            l1_table[s] = make_section(s, MT_NORMAL_WBWA, 0);
    }
#else
    {   /* assets keep WB/WA (read-mostly, identical bytes on every
           launch); the dynamic heap above SH_HEAP_BASE is private */
        u32 s;
        u32 hb = ((volatile u32 *)0x04B00000u)[57];   /* SH_HEAP_BASE */
        u32 hs = (hb >= 0x30000000u && hb < 0x3F800000u) ? (hb >> 20)
                                                          : 0x300u;
        for (s = 0x300; s < hs; s++)
            l1_table[s] = make_section(s, MT_NORMAL_WBWA, 0);
        for (s = hs; s < 0x3F8; s++)
            l1_table[s] = make_section(s, MT_PRIVATE, 0);
    }
#endif

     














    if(fb_arm){
        unsigned int k;
        for(k = 0; k < 8u; k++)
            l1_table[fb_sec+k] = make_section(fb_sec+k, MT_NORMAL_WBWA, 0);
    }

    /* --- Program MMU --- */
    __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(0x00000001u));
    __asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(0u));
    {
        u32 ttbr0 = ((u32)l1_table) | 0x6Bu;
        __asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(ttbr0));
    }
    __asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u)); /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,6"::"r"(0u)); /* BPIALL  */
    __asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u)); /* ICIALLU */

    {   /* invalidate D-cache by set/way before enable */
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

    {   /* Enable MMU, D-cache, I-cache, branch pred. A=0. */
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr |=  (1u<<0);
        sctlr |=  (1u<<2);
        sctlr |=  (1u<<12);
        sctlr |=  (1u<<11);
        sctlr &= ~(1u<<1);
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr));
        __asm__ volatile("isb":::"memory");
    }
}

void dcache_clean_range(u32 start, u32 len)
{
    u32 a = start & ~31u;
    u32 end = (start + len + 31u) & ~31u;
    for(; a < end; a += 32)
        __asm__ volatile("mcr p15,0,%0,c7,c10,1"::"r"(a):"memory");
    __asm__ volatile("dsb":::"memory");
}

void dcache_invalidate_range(u32 start, u32 len)
{
    u32 a = start & ~31u;
    u32 end = (start + len + 31u) & ~31u;
    for(; a < end; a += 32)
        __asm__ volatile("mcr p15,0,%0,c7,c6,1"::"r"(a):"memory");
    __asm__ volatile("dsb":::"memory");
}

void mmu_disable(void)
{
    u32 sctlr;
    __asm__ volatile("dsb":::"memory");
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
    sctlr &= ~(1u<<0);
    sctlr &= ~(1u<<2);
    sctlr &= ~(1u<<12);
    __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr));
    __asm__ volatile("isb":::"memory");
    __asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u));
    __asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u));
    __asm__ volatile("dsb":::"memory");
    __asm__ volatile("isb":::"memory");
}
