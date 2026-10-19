/* SPDX-License-Identifier: GPL-2.0-or-later */

typedef unsigned int u32;

static u32 l1_table[4096] __attribute__((aligned(16384)));

#define SEC_TYPE       0x2
#define SEC_AP_RW      (3u<<10)
#define SEC_DOMAIN0    (0u<<5)

#define MT_NORMAL_WBWA ((1u<<12) | (1u<<3) | (1u<<2))
#define MT_NORMAL_NC   ((1u<<12) | (0u<<3) | (0u<<2))
#define MT_DEVICE      ((0u<<12) | (0u<<3) | (1u<<2))
#define MT_SO          ((0u<<12) | (0u<<3) | (0u<<2))

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
    u32 fb_sec = (fb_arm >> 20);

    {
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr &= ~(1u<<0);
        sctlr &= ~(1u<<2);
        sctlr &= ~(1u<<12);
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr):"memory");
        __asm__ volatile("dsb":::"memory");
        __asm__ volatile("isb":::"memory");
    }

    for(i=0;i<4096;i++)
        l1_table[i] = make_section(i, MT_DEVICE, 0);

    l1_table[0x049] = make_section(0x049, MT_NORMAL_WBWA, 0);
    l1_table[0x04A] = make_section(0x04A, MT_NORMAL_WBWA, 0);

    for(i=0x045;i<=0x048;i++)
        l1_table[i] = make_section(i, MT_NORMAL_NC, 0);

    l1_table[0x04B] = make_section(0x04B, MT_NORMAL_NC, 0);

    for(i=0x04C;i<=0x05E;i++)
        l1_table[i] = make_section(i, MT_NORMAL_WBWA, 0);

    l1_table[0x05F] = make_section(0x05F, MT_NORMAL_WBWA, 0);

    for(i=0x070;i<=0x07F;i++)
        l1_table[i] = make_section(i, MT_NORMAL_WBWA, 0);

    l1_table[0x06E] = make_section(0x06E, MT_NORMAL_NC, 0);
    l1_table[0x06F] = make_section(0x06F, MT_NORMAL_NC, 0);

    {
        u32 s;
        for (s = 0x300; s < 0x3F8; s++)
            l1_table[s] = make_section(s, MT_NORMAL_WBWA, 0);
    }

    if(fb_arm){
        l1_table[fb_sec]   = make_section(fb_sec,   MT_NORMAL_WBWA, 0);
        l1_table[fb_sec+1] = make_section(fb_sec+1, MT_NORMAL_WBWA, 0);
        l1_table[fb_sec+2] = make_section(fb_sec+2, MT_NORMAL_WBWA, 0);
        l1_table[fb_sec+3] = make_section(fb_sec+3, MT_NORMAL_WBWA, 0);
    }

    __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(0x00000001u));

    __asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(0u));

    {
        u32 ttbr0 = ((u32)l1_table) | 0x6Bu;
        __asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(ttbr0));
    }

    __asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u));
    __asm__ volatile("mcr p15,0,%0,c7,c5,6"::"r"(0u));
    __asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u));

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

    {
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
