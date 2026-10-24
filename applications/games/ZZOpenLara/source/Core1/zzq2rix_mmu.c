/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2rix_mmu.c - ZZQuake2RIX: the Core1 MMU attributes the GPU path
 * needs, changed in the release's own level-1 table, at run time, with
 * the checks and the cache maintenance the arbitration of 14/10 asked
 * for (ZZQUAKE2RIX_MEMORY_MAP.md, section 4).  Nothing of the boot, the
 * FSBL or the firmware's MMU is touched: this is Core1's private table.
 *
 * What changes (20 + 2 sections, plus the GPU registers):
 *   0x3D8..0x3E7  texture pool        -> inner write-back / outer non-cacheable
 *   0x3E8..0x3EB  ring, Z, bind lists -> Normal non-cacheable
 *   0x066, 0x067  G4B GPU window      -> Normal non-cacheable (fences polled)
 *   0x830, 0x83C  VDMA, RasterIX ID + command FIFO -> Device (if unmapped)
 *   0xF80         SLCR / DMAC         -> Device (if unmapped)
 *
 * The sequence: read and verify TTBCR, TTBR0 (a table inside Core1's
 * own memory), SCTLR (MMU on, TEX remap off) and the current descriptors
 * (identity sections of Normal memory); clean + invalidate L1 (by MVA)
 * and the L2 PL310 (clean + invalidate by physical address) over the
 * ranges whose attributes change, so that no line - dirty or clean,
 * from this run, a previous run or a speculative access - survives the
 * change; then, per descriptor, break-before-make: fault, DSB, TLB
 * invalidate by MVA, DSB, new descriptor, clean of the table line,
 * DSB; finally TLBIALL, DSB, ISB, BPIALL.  The descriptors are read
 * back.  Only TEX/C/B change; domain, AP, S and XN are kept.
 *
 * L2 maintenance by address goes through the PL310's atomic operation
 * registers only (never a "by way" background operation), one line at
 * a time, with a wait for completion: the same operations the Xilinx
 * standalone library issues from either core.
 *
 * Restore (end of run, GPU idle): the original descriptors, same
 * sequence, after an L1 clean + invalidate of the ranges.
 *
 * Host build (ZZQ2RIX_HOST): nothing to do, returns 0.
 *
 * ASCII only.
 */

#include "zzq2rix_mmu.h"

#ifndef ZZQ2RIX_HOST

typedef unsigned int u32;

#define SEC_TYPE_MASK   0x3u
#define SEC_TYPE        0x2u
#define SEC_B           (1u << 2)
#define SEC_C           (1u << 3)
#define SEC_XN          (1u << 4)
#define SEC_DOMAIN(d)   ((u32)(d) << 5)
#define SEC_AP_RW       (0x3u << 10)
#define SEC_TEX(t)      ((u32)(t) << 12)
#define SEC_SUPER       (1u << 18)
#define SEC_ATTR_MASK   (SEC_TEX(7u) | SEC_C | SEC_B)

#define ATTR_IWB_ONC    (SEC_TEX(4u) | SEC_B)   /* inner WB-WA, outer non-cacheable (TRE = 0) */
#define ATTR_NC         (SEC_TEX(1u))           /* Normal non-cacheable */
#define ATTR_DEVICE     (SEC_B)                 /* Shareable Device */

#define PL310_BASE      0xF8F02000u
#define PL310_CTRL      (PL310_BASE + 0x100u)
#define PL310_SYNC      (PL310_BASE + 0x730u)
#define PL310_INV_PA    (PL310_BASE + 0x770u)
#define PL310_CLEAN_PA  (PL310_BASE + 0x7B0u)
#define PL310_CLINV_PA  (PL310_BASE + 0x7F0u)

static inline u32 rd_sctlr(void) { u32 v; __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v)); return v; }
static inline u32 rd_ttbcr(void) { u32 v; __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(v)); return v; }
static inline u32 rd_ttbr0(void) { u32 v; __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(v)); return v; }
static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }
static inline void tlbiall(void) { __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0) : "memory"); }
static inline void tlbimva(u32 mva) { __asm__ volatile("mcr p15, 0, %0, c8, c7, 1" :: "r"(mva) : "memory"); }
static inline void bpiall(void) { __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" :: "r"(0) : "memory"); }
static inline void dccmvac(u32 mva) { __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(mva) : "memory"); }
static inline void dccimvac(u32 mva) { __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(mva) : "memory"); }
static inline u32 mmio_rd(u32 a) { return *(volatile u32 *)a; }
static inline void mmio_wr(u32 a, u32 v) { *(volatile u32 *)a = v; }

/* release zzquake_platform.c: microseconds, Global Timer */
extern u32 zzq_us_now(void);

/* the sections this file changes */
typedef struct { u32 index; u32 attr; int mmio; } sec_change;
static const sec_change g_changes[] = {
    { 0x3D8, ATTR_IWB_ONC, 0 }, { 0x3D9, ATTR_IWB_ONC, 0 }, { 0x3DA, ATTR_IWB_ONC, 0 }, { 0x3DB, ATTR_IWB_ONC, 0 },
    { 0x3DC, ATTR_IWB_ONC, 0 }, { 0x3DD, ATTR_IWB_ONC, 0 }, { 0x3DE, ATTR_IWB_ONC, 0 }, { 0x3DF, ATTR_IWB_ONC, 0 },
    { 0x3E0, ATTR_IWB_ONC, 0 }, { 0x3E1, ATTR_IWB_ONC, 0 }, { 0x3E2, ATTR_IWB_ONC, 0 }, { 0x3E3, ATTR_IWB_ONC, 0 },
    { 0x3E4, ATTR_IWB_ONC, 0 }, { 0x3E5, ATTR_IWB_ONC, 0 }, { 0x3E6, ATTR_IWB_ONC, 0 }, { 0x3E7, ATTR_IWB_ONC, 0 },
    { 0x3E8, ATTR_NC, 0 }, { 0x3E9, ATTR_NC, 0 }, { 0x3EA, ATTR_NC, 0 }, { 0x3EB, ATTR_NC, 0 },
    { 0x066, ATTR_NC, 0 }, { 0x067, ATTR_NC, 0 },
    { 0x830, ATTR_DEVICE, 1 }, { 0x83C, ATTR_DEVICE, 1 }, { 0xF80, ATTR_DEVICE, 1 },
};
#define N_CHANGES ((int)(sizeof(g_changes) / sizeof(g_changes[0])))

static u32 g_orig[N_CHANGES];
static volatile u32 *g_table;
static int g_applied;

/* L1 clean+invalidate by MVA and L2 clean+invalidate by PA over [a, a + len) */
static void maintain_range(u32 a, u32 len, int l2)
{
    u32 p, e = a + len;
    for (p = a & ~31u; p < e; p += 32u) dccimvac(p);
    dsb();
    if (l2) {
        for (p = a & ~31u; p < e; p += 32u) {
            mmio_wr(PL310_CLINV_PA, p);
            while (mmio_rd(PL310_CLINV_PA) & 1u) { }
        }
        mmio_wr(PL310_SYNC, 0u);
        while (mmio_rd(PL310_SYNC) & 1u) { }
        dsb();
    }
}

static void write_desc(u32 index, u32 desc)
{
    g_table[index] = 0u;
    dsb();
    tlbimva(index << 20);
    dsb();
    g_table[index] = desc;
    dccmvac((u32)&g_table[index]);
    dsb();
}

int zzq2rix_mmu_setup(zz_mmu_report *r)
{
    u32 sctlr, ttbcr, ttbr0, base, t0;
    int i, l2_on;

    sctlr = rd_sctlr(); ttbcr = rd_ttbcr(); ttbr0 = rd_ttbr0();
    r->sctlr = sctlr; r->ttbcr = ttbcr; r->ttbr0 = ttbr0; r->error = 0; r->bad_index = 0; r->bad_desc = 0;
    r->l1_us = 0; r->l2_us = 0; r->l2_done = 0;

    /* 1. the table: one L1 table (N = 0), MMU and caches on, TEX remap off,
          table inside Core1's own memory (blob window or .q2bss) */
    if ((ttbcr & 7u) != 0u) { r->error = ZZ_MMU_ERR_TTBCR; return r->error; }
    if ((sctlr & 1u) == 0u || (sctlr & (1u << 2)) == 0u) { r->error = ZZ_MMU_ERR_SCTLR; return r->error; }
    if (sctlr & (1u << 28)) { r->error = ZZ_MMU_ERR_TRE; return r->error; }
    base = ttbr0 & 0xFFFFC000u;
    r->table = base;
    if (!((base >= 0x04900000u && base < 0x04B00000u) || (base >= 0x3EC00000u && base < 0x3F800000u))) {
        r->error = ZZ_MMU_ERR_TABLE_PLACE; return r->error;
    }
    g_table = (volatile u32 *)base;

    /* 2. the descriptors as they are */
    for (i = 0; i < N_CHANGES; i++) {
        u32 idx = g_changes[i].index, d = g_table[idx];
        g_orig[i] = d;
        if (g_changes[i].mmio) {
            /* unmapped (fault), or an identity Device/Strongly-ordered section: both fine */
            if ((d & SEC_TYPE_MASK) == 0u) continue;
            if ((d & SEC_TYPE_MASK) != SEC_TYPE || (d & SEC_SUPER) || (d & 0xFFF00000u) != (idx << 20) || (d & SEC_C)) {
                r->error = ZZ_MMU_ERR_DESC; r->bad_index = idx; r->bad_desc = d; return r->error;
            }
            continue;
        }
        if ((d & SEC_TYPE_MASK) != SEC_TYPE || (d & SEC_SUPER) || (d & 0xFFF00000u) != (idx << 20)) {
            r->error = ZZ_MMU_ERR_DESC; r->bad_index = idx; r->bad_desc = d; return r->error;
        }
    }

    /* 3. cache maintenance over the ranges whose attributes change: the 20 MiB
          reserved under .q2bss and the 2 MiB GPU window */
    l2_on = (mmio_rd(PL310_CTRL) & 1u) ? 1 : 0;
    t0 = zzq_us_now();
    maintain_range(0x3D800000u, 0x01400000u, 0);
    maintain_range(0x06600000u, 0x00200000u, 0);
    r->l1_us = zzq_us_now() - t0;
    if (l2_on) {
        t0 = zzq_us_now();
        maintain_range(0x3D800000u, 0x01400000u, 1);
        maintain_range(0x06600000u, 0x00200000u, 1);
        r->l2_us = zzq_us_now() - t0;
        r->l2_done = 1;
    }

    /* 4. the new descriptors, break-before-make */
    for (i = 0; i < N_CHANGES; i++) {
        u32 idx = g_changes[i].index, d = g_orig[i], nd;
        if (g_changes[i].mmio) {
            if ((d & SEC_TYPE_MASK) != 0u) continue;          /* already a device mapping: kept */
            nd = (idx << 20) | SEC_TYPE | SEC_DOMAIN(0) | SEC_AP_RW | SEC_XN | ATTR_DEVICE;
        } else {
            nd = (d & ~SEC_ATTR_MASK) | g_changes[i].attr;
        }
        write_desc(idx, nd);
    }
    tlbiall(); dsb(); isb(); bpiall(); dsb(); isb();

    /* 5. read back */
    for (i = 0; i < N_CHANGES; i++) {
        u32 idx = g_changes[i].index, d = g_table[idx];
        if (g_changes[i].mmio) {
            if ((d & SEC_TYPE_MASK) != SEC_TYPE) { r->error = ZZ_MMU_ERR_VERIFY; r->bad_index = idx; r->bad_desc = d; return r->error; }
        } else if ((d & SEC_ATTR_MASK) != g_changes[i].attr) {
            r->error = ZZ_MMU_ERR_VERIFY; r->bad_index = idx; r->bad_desc = d; return r->error;
        }
    }
    g_applied = 1;
    return 0;
}

int zzq2rix_mmu_restore(void)
{
    int i;
    if (!g_applied) return 0;
    maintain_range(0x3D800000u, 0x01400000u, 0);
    maintain_range(0x06600000u, 0x00200000u, 0);
    for (i = 0; i < N_CHANGES; i++) {
        if (g_changes[i].mmio) continue;             /* the device mappings can stay */
        write_desc(g_changes[i].index, g_orig[i]);
    }
    tlbiall(); dsb(); isb(); bpiall(); dsb(); isb();
    g_applied = 0;
    return 0;
}

#else /* ZZQ2RIX_HOST */

int zzq2rix_mmu_setup(zz_mmu_report *r)
{
    r->sctlr = 0; r->ttbcr = 0; r->ttbr0 = 0; r->table = 0; r->error = 0; r->bad_index = 0; r->bad_desc = 0;
    r->l1_us = 0; r->l2_us = 0; r->l2_done = 0;
    return 0;
}
int zzq2rix_mmu_restore(void) { return 0; }

#endif
