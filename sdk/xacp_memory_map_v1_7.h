/*
 * XACP Memory Map v1.7 -- XX19b
 *
 * XACP v1.7 keeps the v1.6 layout below 0x30000000 unchanged and
 * defines a high ARM-only arena for Core1 applications.
 *
 *   0x30000000 - 0x3F800000   248 MiB  Core1 arena
 *   0x3F800000 - 0x3FC00000     4 MiB  guard
 *   0x3FC00000 - 0x40000000     4 MiB  firmware/hardware reserved
 *
 * The arena is not directly accessible through the Amiga Zorro window.
 * Core1 applications must map it in their own MMU tables and must not
 * allocate in the guard or reserved region.
 */

#ifndef XACP_MEMORY_MAP_V1_7_H
#define XACP_MEMORY_MAP_V1_7_H

#define XACP_ARM_LARGE_CORE1_BASE      0x30000000UL
#define XACP_ARM_LARGE_CORE1_SIZE      0x0F800000UL  /* 248 MiB */
#define XACP_ARM_LARGE_CORE1_END       0x3F800000UL

#define XACP_ARM_LARGE_GUARD_BASE      0x3F800000UL
#define XACP_ARM_LARGE_GUARD_END       0x3FC00000UL  /* 4 MiB guard */

#define XACP_ARM_HW_RESERVED_BASE      0x3FC00000UL
#define XACP_ARM_HW_RESERVED_END       0x40000000UL  /* 4 MiB */

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
