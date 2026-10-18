/*
 * XACP Image Service ABI v1.3
 *
 * Copyright (C) 2026 Xanxi
 * SPDX-License-Identifier: 0BSD
 *
 * Deliberately C89-friendly on AmigaOS so the same public header can be used
 * from SAS/C, VBCC and GCC clients.  The firmware side uses stdint.h.
 */
#ifndef XACP_IMAGE_SERVICE_H
#define XACP_IMAGE_SERVICE_H

#if defined(__amigaos__) || defined(AMIGA) || defined(__SASC) || defined(__VBCC__)
#include <exec/types.h>
typedef ULONG xacp_img_u32;
#else
#include <stdint.h>
typedef uint32_t xacp_img_u32;
#endif

#define XACP_IMAGE_OPCODE                    0x0220UL
#define XACP_IMAGE_SERVICE_VERSION           0x00010003UL /* 1.3 */

/* Subcommands in command.param1. */
#define XACP_IMAGE_CMD_QUERY                 0UL
#define XACP_IMAGE_CMD_DECODE_LEGACY         1UL /* compatibility / diagnostics */
#define XACP_IMAGE_CMD_BEGIN                 2UL
#define XACP_IMAGE_CMD_FEED                  3UL
#define XACP_IMAGE_CMD_DECODE                4UL
#define XACP_IMAGE_CMD_GET_TILE              5UL
#define XACP_IMAGE_CMD_DIRECT_P96            6UL
#define XACP_IMAGE_CMD_CLOSE                 7UL
#define XACP_IMAGE_CMD_CANCEL                8UL

/* QUERY result2 capabilities. */
#define XACP_IMAGE_CAP_JPEG                  (1UL << 0)
#define XACP_IMAGE_CAP_PNG                   (1UL << 1)
#define XACP_IMAGE_CAP_RGBA8                 (1UL << 8)
#define XACP_IMAGE_CAP_BGRA8                 (1UL << 9)
#define XACP_IMAGE_CAP_SESSION               (1UL << 16)
#define XACP_IMAGE_CAP_CHUNK_FEED            (1UL << 17)
#define XACP_IMAGE_CAP_TILED_OUTPUT          (1UL << 18)
#define XACP_IMAGE_CAP_DIRECT_P96            (1UL << 19)
#define XACP_IMAGE_CAP_CANCEL                (1UL << 20)
#define XACP_IMAGE_CAP_SCALE_FIT             (1UL << 21)
#define XACP_IMAGE_CAP_JPEG_ROW_STREAM       (1UL << 22)

#define XACP_IMAGE_FMT_UNKNOWN               0UL
#define XACP_IMAGE_FMT_JPEG                  1UL
#define XACP_IMAGE_FMT_PNG                   2UL

#define XACP_IMAGE_OUT_RGBA8                 0UL
#define XACP_IMAGE_OUT_BGRA8                 1UL

#define XACP_IMAGE_FEED_FINAL                (1UL << 0)

#define XACP_IMAGE_STATUS_IDLE               0UL
#define XACP_IMAGE_STATUS_BUSY               1UL
#define XACP_IMAGE_STATUS_DONE               2UL
#define XACP_IMAGE_STATUS_ERROR              3UL

#define XACP_IMAGE_MAX_INPUT_BYTES           (32UL * 1024UL * 1024UL)
#define XACP_IMAGE_MAX_WIDTH                 8192UL
#define XACP_IMAGE_MAX_HEIGHT                8192UL
/* General JPEG/source ceiling: 24 MiPixels, matching the practical camera
 * target used by current ZZ9000 image work. Expensive progressive sampling
 * layouts can still be rejected by the independent decoder-memory budget. */
#define XACP_IMAGE_MAX_PIXELS                (24UL * 1024UL * 1024UL)
/* stb_image's PNG path still materialises full inflate/output buffers. Keep a
 * separate conservative PNG ceiling until a dedicated streaming PNG backend
 * is substituted. This does not affect WarpJPEG integration. */
#define XACP_IMAGE_PNG_MAX_PIXELS            (8UL * 1024UL * 1024UL)
#define XACP_IMAGE_MAX_OUTPUT_BYTES          (XACP_IMAGE_MAX_PIXELS * 4UL)
/* Input spool (max 32 MiB) + decoder allocations (max 72 MiB) leaves at
 * least 24 MiB of the 128 MiB Core0 newlib heap nominally outside the image
 * service's own hard budgets. */
#define XACP_IMAGE_DECODER_HEAP_BUDGET       (72UL * 1024UL * 1024UL)

/* Service-specific errors in command.error. */
#define XACP_IMAGE_ERR_NONE                  0x0000UL
#define XACP_IMAGE_ERR_BAD_SUBCOMMAND        0x2201UL
#define XACP_IMAGE_ERR_BUSY                  0x2202UL
#define XACP_IMAGE_ERR_BOUNDS                0x2203UL
#define XACP_IMAGE_ERR_TOO_LARGE             0x2204UL
#define XACP_IMAGE_ERR_BAD_FORMAT            0x2205UL
#define XACP_IMAGE_ERR_OOM                   0x2206UL
#define XACP_IMAGE_ERR_OUTPUT_TOO_SMALL      0x2207UL
#define XACP_IMAGE_ERR_DECODE                0x2208UL
#define XACP_IMAGE_ERR_OVERFLOW              0x2209UL
#define XACP_IMAGE_ERR_OVERLAP               0x220AUL
#define XACP_IMAGE_ERR_DISABLED              0x220BUL
#define XACP_IMAGE_ERR_ALLOCATOR_STATE       0x220CUL
#define XACP_IMAGE_ERR_BAD_STATE             0x220DUL
#define XACP_IMAGE_ERR_SEQUENCE              0x220EUL
#define XACP_IMAGE_ERR_INCOMPLETE_INPUT      0x220FUL
#define XACP_IMAGE_ERR_BAD_P96_DEST          0x2210UL
#define XACP_IMAGE_ERR_SCALE                  0x2211UL

/* DECODE request (v1.3):
 *   param2 = maximum output width  (0 = native)
 *   param3 = maximum output height (0 = native)
 * The service never upscales and preserves aspect ratio.
 *
 * DECODE reply:
 *   result1 = output width
 *   result2 = output height
 *   output_offset = source width
 *   output_max_size = source height
 *   param2 = source components
 *   param3 = output RGBA byte count
 *   param4 = source format
 *
 * JPEG GET_TILE is sequential in v1.3: y must equal the next output row.
 */

/* Existing XACP command layout.  All fields are naturally big-endian when
 * written by a 68k Amiga.  The ARM firmware swaps them at the boundary. */
typedef struct XACPImageCommand {
    xacp_img_u32 opcode;
    xacp_img_u32 input_offset;
    xacp_img_u32 input_size;
    xacp_img_u32 output_offset;
    xacp_img_u32 output_max_size;
    xacp_img_u32 param1;
    xacp_img_u32 param2;
    xacp_img_u32 param3;
    xacp_img_u32 param4;
    xacp_img_u32 result1;
    xacp_img_u32 result2;
    xacp_img_u32 status;
    xacp_img_u32 error;
} XACPImageCommand;

#endif /* XACP_IMAGE_SERVICE_H */
