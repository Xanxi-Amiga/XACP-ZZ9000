/* SPDX-License-Identifier: GPL-2.0-or-later */
/* zzq2rix_mmu.h - ZZQuake2RIX: Core1 MMU attributes for the GPU path
   (see zzq2rix_mmu.c).  ASCII only. */
#ifndef ZZQ2RIX_MMU_H
#define ZZQ2RIX_MMU_H

typedef struct {
    unsigned int sctlr, ttbcr, ttbr0, table;   /* as read */
    int error;                                 /* 0 or ZZ_MMU_ERR_* */
    unsigned int bad_index, bad_desc;          /* the descriptor that failed a check */
    unsigned int l1_us, l2_us;                 /* maintenance time */
    int l2_done;                               /* 1 when the PL310 was enabled and maintained */
} zz_mmu_report;

#define ZZ_MMU_ERR_TTBCR        1   /* TTBCR.N != 0: two tables */
#define ZZ_MMU_ERR_SCTLR        2   /* MMU or data cache off */
#define ZZ_MMU_ERR_TRE          3   /* TEX remap on: other attribute encodings */
#define ZZ_MMU_ERR_TABLE_PLACE  4   /* the table is not in Core1's own memory */
#define ZZ_MMU_ERR_DESC         5   /* a descriptor is not an identity section */
#define ZZ_MMU_ERR_VERIFY       6   /* a descriptor did not read back as written */

int zzq2rix_mmu_setup(zz_mmu_report *r);     /* 0 or the error (nothing changed on error) */
int zzq2rix_mmu_restore(void);               /* back to the release attributes (GPU idle) */

#endif
