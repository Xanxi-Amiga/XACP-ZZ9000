/*
 * XACP Memory Map v1.7 -- XX19a1
 *
 * PURELY ADDITIVE revision on top of XACP v1.6 (XX19a). Nothing below
 * 0x30000000 changes: same registers, same opcodes, same low DDR map,
 * same ZZMIDI corridor and private pool, same legacy Core1 zones, same
 * ARM_RUN behaviour. v1.6 constants keep living in their existing
 * headers (zz_midi_sf2.h etc.) and are deliberately NOT duplicated
 * here. This header only defines the new v1.7 section, for the SDK and
 * NEW consumers.
 *
 * What v1.7 adds: the ARM-only LARGE CORE1 ARENA.
 *
 *   0x30000000 - 0x3F800000   248 MiB  XACP_ARM_LARGE_CORE1 (usable)
 *   0x3F800000 - 0x3FC00000     4 MiB  GUARD (never allocate)
 *   0x3FC00000 - 0x40000000     4 MiB  firmware/hardware reserved
 *                                      (audio I2S, Ethernet BD/frames,
 *                                      boot ROM, USB/SD buffers -- see
 *                                      memorymap.h, all >= 0x3FC00000)
 *
 * This space was previously wasted: its only users were the SMUSH
 * Codec37/47 scratch buffers hardcoded at 0x30000000/0x30800000/
 * 0x31000000 (+16 MiB per decoder). XX19a1 removes the SMUSH
 * Codec1/37/47 implementation from the build (codec37.c, codec47.c,
 * compression.c). The ACC_CMPTYPE_* ABI values in gfx.h are NOT
 * renumbered: SMUSH requests are accepted and ignored (deprecated,
 * unsupported). IMA ADPCM and every other decompression path are
 * unchanged.
 *
 * CONSUMER CONTRACT (mandatory):
 *  1. ARM-only. The arena is physically unreachable from the Amiga
 *     side (Zorro window ends far below); do not attempt to expose it.
 *     Host data reaches the arena via an existing host-visible staging
 *     zone, then an ARM-side copy.
 *  2. MMU: Core0 firmware already maps all DDR Normal WB cacheable --
 *     NO Core0 MMU change. Each Core1 blob using the arena MUST map
 *     0x30000000-0x3F800000 itself as Normal WB in its own translation
 *     table (same pattern as the ZZMIDI private pool mapping).
 *  3. Never allocate in the GUARD or in the reserved top 4 MiB.
 *  4. Coexistence with the v1.6 private pool (0x22000000-0x30000000)
 *     is the consumer's responsibility; the arena does not replace it.
 *  5. XACP opcode 0x0340 remains NOT IMPLEMENTED in XX19a1 (ZZBench
 *     v1.3 relies on it returning ERROR as a ping).
 */

#ifndef XACP_MEMORY_MAP_V1_7_H
#define XACP_MEMORY_MAP_V1_7_H

/* XACP v1.7 -- ARM-only large Core1 arena */
#define XACP_ARM_LARGE_CORE1_BASE      0x30000000UL
#define XACP_ARM_LARGE_CORE1_SIZE      0x0F800000UL  /* 248 MiB */
#define XACP_ARM_LARGE_CORE1_END       0x3F800000UL

#define XACP_ARM_LARGE_GUARD_BASE      0x3F800000UL
#define XACP_ARM_LARGE_GUARD_END       0x3FC00000UL  /* 4 MiB guard */

#define XACP_ARM_HW_RESERVED_BASE      0x3FC00000UL
#define XACP_ARM_HW_RESERVED_END       0x40000000UL  /* 4 MiB, fixed HW */

/* Compile-time safety */
typedef char xacp_v17_arena_arith[
    (XACP_ARM_LARGE_CORE1_BASE + XACP_ARM_LARGE_CORE1_SIZE ==
     XACP_ARM_LARGE_CORE1_END) ? 1 : -1];
typedef char xacp_v17_arena_after_v16_pool[
    (XACP_ARM_LARGE_CORE1_BASE >= 0x30000000UL) ? 1 : -1];
typedef char xacp_v17_guard_contiguous[
    (XACP_ARM_LARGE_CORE1_END == XACP_ARM_LARGE_GUARD_BASE) ? 1 : -1];
typedef char xacp_v17_hw_top[
    (XACP_ARM_LARGE_GUARD_END == XACP_ARM_HW_RESERVED_BASE &&
     XACP_ARM_HW_RESERVED_END == 0x40000000UL) ? 1 : -1];

#endif /* XACP_MEMORY_MAP_V1_7_H */
