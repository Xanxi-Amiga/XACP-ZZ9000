#ifndef ZZ_RASTERIX_SUBMIT_H
#define ZZ_RASTERIX_SUBMIT_H

/* RasterIX/XACP GPU command submit ABI 1.0 */
#define ZZGPU_OP_QUERY             0x0600UL
#define ZZGPU_OP_TEST2             0x0601UL
#define ZZGPU_OP_SUBMIT            0x0603UL
#define ZZGPU_CAP_QUERY            0x00000001UL
#define ZZGPU_CAP_TEST2            0x00000002UL
#define ZZGPU_CAP_SUBMIT           0x00000008UL
#define ZZGPU_SUBMIT_ABI           0x00010000UL

/* 68k framebuffer-relative staging offset. */
#define ZZGPU_STAGE_FB_OFFSET      0x065F0000UL
#define ZZGPU_STAGE_SIZE           0x00010000UL
#define ZZGPU_SUBMIT_MAX_BYTES     0x00004000UL

/* ARM physical addresses accepted inside RasterIX commands. */
#define ZZGPU_POOL_BASE_ARM        0x06600000UL
#define ZZGPU_POOL_END_ARM         0x067F0000UL
#define ZZGPU_TEXTURE_PAGE_SIZE    2048UL

#endif
