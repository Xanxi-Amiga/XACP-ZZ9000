#ifndef ZZVGM_SHARED_H
#define ZZVGM_SHARED_H

/* ZZVGMPlay A0r4 shared ABI -- XACP 1.8 target.
 *
 * Memory ownership stays identical to validated ZZReSID/Core1 layout:
 *   mailbox  ARM 0x05A00000 / Amiga fb+0x05800000
 *   PCM ring ARM 0x05A40000 / Amiga fb+0x05840000
 *   upload   ARM 0x05A50000 / Amiga fb+0x05850000 (64 KiB)
 *
 * ZZMIDI/Core0 and datatype areas are untouched.
 */

#define ZZVGM_BLOB_ARM_ADDR       0x04200000UL
#define ZZVGM_SHARED_ARM_ADDR     0x05A00000UL
#define ZZVGM_PCM_ARM_ADDR        0x05A40000UL
#define ZZVGM_UPLOAD_ARM_ADDR     0x05A50000UL

#define ZZVGM_BLOB_FB_OFFSET      0x04000000UL
#define ZZVGM_SHARED_FB_OFFSET    0x05800000UL
#define ZZVGM_PCM_FB_OFFSET       0x05840000UL
#define ZZVGM_UPLOAD_FB_OFFSET    0x05850000UL

#define ZZVGM_UPLOAD_BYTES        (64UL * 1024UL)

#define ZZVGM_ARENA_BASE          0x30000000UL
#define ZZVGM_ARENA_END           0x3F800000UL
#define ZZVGM_ARENA_SIZE          (ZZVGM_ARENA_END - ZZVGM_ARENA_BASE)
#define ZZVGM_VGM_MAX_BYTES       (240UL * 1024UL * 1024UL)

#define ZZVGM_REG_ARM_RUN_HI      0x0090
#define ZZVGM_REG_ARM_RUN_LO      0x0092
#define ZZVGM_RUN_HI_VALUE        0x0420
#define ZZVGM_RUN_LO_VALUE        0x0000

/* 68k -> Core1 */
#define VSH_CMD                   0
#define VSH_SAMPLE_RATE           1
#define VSH_FLAGS                 2
#define VSH_VGM_LEN               3
#define VSH_PCM_READ              4
#define VSH_UPLOAD_OFFSET         5
#define VSH_UPLOAD_SIZE           6
#define VSH_CANARY_68K            7

/* Core1 -> 68k */
#define VSH_MAGIC                 8
#define VSH_STATUS                9
#define VSH_PCM_WRITE            10
#define VSH_ERROR                11
#define VSH_HEARTBEAT            12
#define VSH_CHIP                 13
#define VSH_BAD_CMD              14
#define VSH_VGM_VERSION          15
#define VSH_PLAY_SAMPLES         16
#define VSH_TOTAL_SAMPLES        17
#define VSH_LOOP_COUNT           18
#define VSH_OPL_WRITES           19   /* A0r4: total chip writes, name retained for ABI */
#define VSH_DATA_OFFSET          20
#define VSH_LOOP_OFFSET          21
#define VSH_UPLOAD_DONE          22
#define VSH_UPLOAD_EXPECTED      23
#define VSH_SCTLR                24
#define VSH_ABI_VERSION          25
#define VSH_RENDER_CALLS         26
#define VSH_LAST_RENDER_FRAMES   27
#define VSH_CHIP_MASK            28
#define VSH_FILE_TYPE            29
#define VSH_UNPACKED_LEN         30
#define VSH_CANARY_ARM           31

#define ZZVGM_SHARED_WORDS       32

#define ZZVGM_MAGIC              0x5A56474DUL /* 'ZVGM' */
#define ZZVGM_CANARY_68K         0x5636384BUL /* 'V68K' */
#define ZZVGM_CANARY_ARM         0x5641524DUL /* 'VARM' */
#define ZZVGM_ABI_VERSION        3UL

#define ZZVGM_CMD_NONE           0
#define ZZVGM_CMD_START          1
#define ZZVGM_CMD_STOP           2
#define ZZVGM_CMD_RESET          3
#define ZZVGM_CMD_UPLOAD_BEGIN   4
#define ZZVGM_CMD_UPLOAD_CHUNK   5

#define ZZVGM_ST_INIT            0
#define ZZVGM_ST_READY           1
#define ZZVGM_ST_UPLOADING       2
#define ZZVGM_ST_PLAYING         3
#define ZZVGM_ST_DONE            4
#define ZZVGM_ST_ERROR           0x80000000UL
#define ZZVGM_ST_STOPPED         0xFF

#define ZZVGM_FLAG_REPEAT        0x00000001UL
#define ZZVGM_FLAG_GZIP          0x00000002UL

#define ZZVGM_FILE_VGM           0
#define ZZVGM_FILE_VGZ           1

#define ZZVGM_CHIP_NONE          0
#define ZZVGM_CHIP_OPL2          2
#define ZZVGM_CHIP_OPL3          3
#define ZZVGM_CHIP_SN76489       4
#define ZZVGM_CHIP_YM2612        5
#define ZZVGM_CHIP_YM2151        6
#define ZZVGM_CHIP_AY8910        7
#define ZZVGM_CHIP_YM2413        8
#define ZZVGM_CHIP_GB_DMG        9
#define ZZVGM_CHIP_NES_APU       10
#define ZZVGM_CHIP_HUC6280       11
#define ZZVGM_CHIP_SEGAPCM       12
#define ZZVGM_CHIP_OKI6295       13
#define ZZVGM_CHIP_YM2610        14
#define ZZVGM_CHIP_QSOUND        15
#define ZZVGM_CHIP_ES5503        16
#define ZZVGM_CHIP_K053260       17
#define ZZVGM_CHIP_K054539       18
#define ZZVGM_CHIP_K051649       19
#define ZZVGM_CHIP_RF5C68        20

#define ZZVGM_CMASK_OPL2         (1UL << 0)
#define ZZVGM_CMASK_OPL3         (1UL << 1)
#define ZZVGM_CMASK_SN76489      (1UL << 2)
#define ZZVGM_CMASK_YM2612       (1UL << 3)
#define ZZVGM_CMASK_YM2151       (1UL << 4)
#define ZZVGM_CMASK_AY8910       (1UL << 5)
#define ZZVGM_CMASK_YM2413       (1UL << 6)
#define ZZVGM_CMASK_GB_DMG       (1UL << 7)
#define ZZVGM_CMASK_NES_APU      (1UL << 8)
#define ZZVGM_CMASK_HUC6280      (1UL << 9)
#define ZZVGM_CMASK_SEGAPCM      (1UL << 10)
#define ZZVGM_CMASK_OKI6295      (1UL << 11)
#define ZZVGM_CMASK_YM2610       (1UL << 12)
#define ZZVGM_CMASK_QSOUND       (1UL << 13)
#define ZZVGM_CMASK_ES5503       (1UL << 14)
#define ZZVGM_CMASK_K053260      (1UL << 15)
#define ZZVGM_CMASK_K054539      (1UL << 16)
#define ZZVGM_CMASK_K051649      (1UL << 17)
#define ZZVGM_CMASK_RF5C68       (1UL << 18)
#define ZZVGM_CMASK_YM2612_DUAL  (1UL << 19) /* display metadata only */

#define ZZVGM_ERR_NONE              0
#define ZZVGM_ERR_BAD_MAGIC         1
#define ZZVGM_ERR_TOO_SMALL         2
#define ZZVGM_ERR_BAD_OFFSET        3
#define ZZVGM_ERR_NO_SUPPORTED_CHIP 4
#define ZZVGM_ERR_BAD_CMD           5
#define ZZVGM_ERR_TRUNCATED         6
#define ZZVGM_ERR_TOO_LARGE         7
#define ZZVGM_ERR_UPLOAD_ORDER      8
#define ZZVGM_ERR_UPLOAD_SIZE       9
#define ZZVGM_ERR_UPLOAD_INCOMPLETE 10
#define ZZVGM_ERR_BAD_ABI           11
#define ZZVGM_ERR_BAD_GZIP          12
#define ZZVGM_ERR_GZIP_TOO_LARGE    13
#define ZZVGM_ERR_GZIP_INFLATE      14
#define ZZVGM_ERR_YM2612_INIT       15
#define ZZVGM_ERR_YM2151_INIT       16
#define ZZVGM_ERR_YM2413_INIT       17
#define ZZVGM_ERR_GB_INIT           18
#define ZZVGM_ERR_NES_INIT          19
#define ZZVGM_ERR_HUC6280_INIT      20
#define ZZVGM_ERR_ARCADE_ROM        21
#define ZZVGM_ERR_S18_INIT          22
#define ZZVGM_ERR_RF_UNSUPPORTED    23
#define ZZVGM_ERR_RF_TRANSFER       24

#define ZZVGM_RING_FRAMES        8192UL
#define ZZVGM_RING_MASK          (ZZVGM_RING_FRAMES - 1UL)
#define ZZVGM_RING_BYTES         (ZZVGM_RING_FRAMES * 4UL)

#endif
