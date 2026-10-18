/*
 * XACP Memory Map v1.8 - image-service shared window
 *
 * Copyright (C) 2026 Xanxi
 * SPDX-License-Identifier: 0BSD
 *
 * v1.8 extends v1.7.  It does NOT change the v1.7 Core1 arena.
 * It assigns one previously unowned Zorro-visible MiB immediately after
 * the v1.6 ZZMIDI shared corridor to the XACP image service.
 *
 * Addressing:
 *   fb = board + 0x00010000
 *   ARM physical = 0x00200000 + fb-relative offset
 *
 * Image transport window:
 *   Amiga/fb-relative : 0x06300000 .. 0x06400000
 *   ARM physical      : 0x06500000 .. 0x06600000
 *   size              : 1 MiB
 *
 * Ownership rule:
 *   This window is transport/staging memory only.  The image service reuses
 *   it for FEED input and GET_TILE output.  No decoded full image is kept
 *   here.  Full decoded images live in bounded Core0/newlib heap allocations.
 */
#ifndef XACP_MEMORY_MAP_V1_8_H
#define XACP_MEMORY_MAP_V1_8_H

#include "xacp_memory_map_v1_7.h"

#ifndef XACP_ARM_FB_PHYS_BASE
#define XACP_ARM_FB_PHYS_BASE               0x00200000UL
#endif

#define XACP_VERSION_1_8_U32                 0x00010800UL

/* Canonical end of the XACP v1.6 ZZMIDI Zorro-visible corridor. */
#define XACP_V16_XMID_SHARED_END             0x06300000UL

#define XACP_IMAGE_SHARED_OFFSET             0x06300000UL
#define XACP_IMAGE_SHARED_SIZE               0x00100000UL
#define XACP_IMAGE_SHARED_END                (XACP_IMAGE_SHARED_OFFSET + XACP_IMAGE_SHARED_SIZE)

#define XACP_IMAGE_SHARED_ARM_BASE           (XACP_ARM_FB_PHYS_BASE + XACP_IMAGE_SHARED_OFFSET)
#define XACP_IMAGE_SHARED_ARM_END            (XACP_ARM_FB_PHYS_BASE + XACP_IMAGE_SHARED_END)

/* RasterIX memory area (legacy XACP_GPU_G4B symbol names). FPGA enforces the full 2 MiB
 * aperture ARM 0x06600000..0x06800000.  ABI 1.0 reserves the last 64 KiB
 * as a command staging page range and exposes the lower 0x1F0000 bytes as
 * validated framebuffer/texture memory. */
#define XACP_GPU_G4B_FB_OFFSET               0x06400000UL
#define XACP_GPU_G4B_ARM_BASE                0x06600000UL
#define XACP_GPU_G4B_ARM_HW_END              0x06800000UL
#define XACP_GPU_G4B_POOL_ARM_END            0x067F0000UL
#define XACP_GPU_G4B_STAGE_FB_OFFSET          0x065F0000UL
#define XACP_GPU_G4B_STAGE_ARM_BASE           0x067F0000UL
#define XACP_GPU_G4B_STAGE_SIZE               0x00010000UL

/* Published application boundaries used by the v1.8 audit.  These are not
 * allocations owned by the image service; they are compile-time fences. */
#define XACP_ARM_ZZQUAKE_FILEBUF_BASE        0x06E00000UL
#define XACP_ARM_HIGH_SAVE_BASE              0x07E00000UL
#define XACP_ARM_CORE0_HEAP_BASE             0x08000000UL

/* Conservative direct-P96 destination ceiling.  The image service accepts
 * DIRECT_P96 only below fb+0x03000000, keeping it far below XACP command and
 * service windows.  The Amiga client must additionally prove the destination
 * came from a P96 bitmap on the ZZ9000. */
#define XACP_IMAGE_DIRECT_P96_FB_END         0x03000000UL

#define XACP_V18_STATIC_ASSERT(name, cond) \
    typedef char xacp_v18_static_assert_##name[(cond) ? 1 : -1]

XACP_V18_STATIC_ASSERT(image_starts_at_xmid_end,
    XACP_IMAGE_SHARED_OFFSET == XACP_V16_XMID_SHARED_END);
XACP_V18_STATIC_ASSERT(image_window_is_1mb,
    XACP_IMAGE_SHARED_SIZE == 0x00100000UL);
XACP_V18_STATIC_ASSERT(image_arm_base_is_06500000,
    XACP_IMAGE_SHARED_ARM_BASE == 0x06500000UL);
XACP_V18_STATIC_ASSERT(image_arm_end_is_06600000,
    XACP_IMAGE_SHARED_ARM_END == 0x06600000UL);
XACP_V18_STATIC_ASSERT(gpu_starts_after_image,
    XACP_GPU_G4B_ARM_BASE == XACP_IMAGE_SHARED_ARM_END);
XACP_V18_STATIC_ASSERT(gpu_stage_inside_hw_aperture,
    XACP_GPU_G4B_STAGE_ARM_BASE + XACP_GPU_G4B_STAGE_SIZE == XACP_GPU_G4B_ARM_HW_END);
XACP_V18_STATIC_ASSERT(image_before_zzquake_filebuf,
    XACP_IMAGE_SHARED_ARM_END <= XACP_ARM_ZZQUAKE_FILEBUF_BASE);
XACP_V18_STATIC_ASSERT(image_before_high_saves,
    XACP_IMAGE_SHARED_ARM_END <= XACP_ARM_HIGH_SAVE_BASE);
XACP_V18_STATIC_ASSERT(image_before_core0_heap,
    XACP_IMAGE_SHARED_ARM_END <= XACP_ARM_CORE0_HEAP_BASE);
XACP_V18_STATIC_ASSERT(direct_p96_below_image_window,
    XACP_IMAGE_DIRECT_P96_FB_END <= XACP_IMAGE_SHARED_OFFSET);

#endif /* XACP_MEMORY_MAP_V1_8_H */
