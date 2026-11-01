/*
 * zzvr_mem.h - ZZVirtuaRacingRIX Core1 memory map (single source of truth)
 *
 * Firmware: XX19d-RIX (XACP v1.8). Nothing below is a firmware change.
 *
 * 1. Zorro-visible XACP zones, as ZZPicoDriveMD 1.1 uses them (unchanged,
 *    see zzvr_shared.h): blob 0x04500000 (2 MiB window), shared NC
 *    0x04700000, ROM 0x04B00000 (7 MiB), video 0x05600000, audio NC
 *    0x05700000, debug 0x05800000, save buffer 0x07E00000.
 *
 * 2. Application private memory: ONLY the XACP v1.7 Core1 arena
 *    0x30000000..0x3F800000 (248 MiB, ARM-only, mapped by this blob as
 *    Normal WB/WA like ZZQuake2RIX / ZZOpenLaraRIX).  Nothing is placed in
 *    the v1.6 private pool 0x22000000..0x30000000 any more (ZZMIDI zone).
 *
 *      0x30000000  BSS        16 MiB  .bss of the blob (NOLOAD, zeroed at entry)
 *      0x31000000  SBRK heap  16 MiB  newlib _sbrk (malloc of the core)
 *      0x32000000  MMAP pool  32 MiB  plat_mmap (Pico32xMem, ROM mirrors...)
 *      0x34000000  DRC tcache  8 MiB  SH-2 recompiler code cache (A1b)
 *      0x34800000  free ...
 *      0x3D800000  reserved for the RasterIX objects of A3 (texture pool,
 *                  ring, Z, bind lists: ZZQuake2RIX placements, not used here)
 *      0x3F800000  firmware guard, never mapped, never touched
 *
 * Every region is a multiple of 1 MiB (MMU sections) and the ranges are
 * checked at compile time below; the linker script checks the BSS end.
 *
 * All addresses are < 0x80000000: PicoDrive's SH-2 memory maps store
 * pointers as (ptr >> 1) with the handler flag in bit 31 (MAP_MEMORY /
 * MAP_HANDLER), so ROM (0x04B00000), SDRAM and DRAM (inside Pico32xMem in
 * the MMAP pool) and the DRC cache all stay representable.
 *
 * ASCII only.
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
