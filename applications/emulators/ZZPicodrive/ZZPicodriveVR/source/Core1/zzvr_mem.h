/*
 * zzvr_mem.h - XACP ARM Core1 memory layout for ZZPicoDrive.
 *
 * The Zorro-visible addresses match the XACP firmware interface.
 * Private allocations use the Core1 arena at 0x30000000..0x3F800000.
 * Shared offsets and framebuffer regions are defined in zzvr_shared.h.
 * The layout applies to XX19c and later compatible XACP firmware.
 */
#ifndef ZZVR_MEM_H
#define ZZVR_MEM_H

#define XACP_ARENA_BASE        0x30000000UL
#define XACP_ARENA_END         0x3F800000UL   /* guard starts here */

#define ZZVR_ARENA_BSS         0x30000000UL
#define ZZVR_ARENA_BSS_SIZE    0x01000000UL   /* 16 MiB */
#define ZZVR_ARENA_BSS_END     (ZZVR_ARENA_BSS + ZZVR_ARENA_BSS_SIZE)

#define ZZVR_ARENA_SBRK        0x31000000UL
#define ZZVR_ARENA_SBRK_SIZE   0x01000000UL   /* 16 MiB */
#define ZZVR_ARENA_SBRK_END    (ZZVR_ARENA_SBRK + ZZVR_ARENA_SBRK_SIZE)

#define ZZVR_ARENA_MMAP        0x32000000UL
#define ZZVR_ARENA_MMAP_SIZE   0x02000000UL   /* 32 MiB */
#define ZZVR_ARENA_MMAP_END    (ZZVR_ARENA_MMAP + ZZVR_ARENA_MMAP_SIZE)

#define ZZVR_ARENA_DRC         0x34000000UL
#define ZZVR_ARENA_DRC_SIZE    0x00800000UL   /* 8 MiB reserve, 4 MiB used by DRC_TCACHE_SIZE */
#define ZZVR_ARENA_DRC_END     (ZZVR_ARENA_DRC + ZZVR_ARENA_DRC_SIZE)

#define ZZVR_ARENA_RIX_RESERVED 0x3D800000UL  /* A3: not used by A1 */

/* compile-time checks: inside the arena, 1 MiB aligned, non-overlapping, ordered */
#define ZZVR_STATIC_ASSERT(name, cond) typedef char zzvr_static_assert_##name[(cond) ? 1 : -1]
ZZVR_STATIC_ASSERT(bss_in_arena,   ZZVR_ARENA_BSS >= XACP_ARENA_BASE && ZZVR_ARENA_BSS_END <= XACP_ARENA_END);
ZZVR_STATIC_ASSERT(sbrk_in_arena,  ZZVR_ARENA_SBRK_END <= XACP_ARENA_END);
ZZVR_STATIC_ASSERT(mmap_in_arena,  ZZVR_ARENA_MMAP_END <= XACP_ARENA_END);
ZZVR_STATIC_ASSERT(drc_in_arena,   ZZVR_ARENA_DRC_END <= XACP_ARENA_END);
ZZVR_STATIC_ASSERT(bss_before_sbrk, ZZVR_ARENA_BSS_END <= ZZVR_ARENA_SBRK);
ZZVR_STATIC_ASSERT(sbrk_before_mmap, ZZVR_ARENA_SBRK_END <= ZZVR_ARENA_MMAP);
ZZVR_STATIC_ASSERT(mmap_before_drc, ZZVR_ARENA_MMAP_END <= ZZVR_ARENA_DRC);
ZZVR_STATIC_ASSERT(drc_before_rix,  ZZVR_ARENA_DRC_END <= ZZVR_ARENA_RIX_RESERVED);
ZZVR_STATIC_ASSERT(mib_aligned, ((ZZVR_ARENA_BSS | ZZVR_ARENA_SBRK | ZZVR_ARENA_MMAP | ZZVR_ARENA_DRC
                                  | ZZVR_ARENA_BSS_SIZE | ZZVR_ARENA_SBRK_SIZE | ZZVR_ARENA_MMAP_SIZE
                                  | ZZVR_ARENA_DRC_SIZE) & 0x000FFFFFUL) == 0);
ZZVR_STATIC_ASSERT(pointers_fit_sh2_maps, ZZVR_ARENA_DRC_END < 0x80000000UL);
ZZVR_STATIC_ASSERT(no_v16_pool, ZZVR_ARENA_BSS >= 0x30000000UL);

#endif /* ZZVR_MEM_H */
