#ifndef ZZS_SHARED_H
#define ZZS_SHARED_H

/*
 * zzs_shared.h - ZZStrider shared contract between the Amiga launcher and the Core1 blob.
 *
 * ONE header for BOTH sides. The shared block is an array of 32-bit slots; every field is
 * addressed by its slot INDEX, never by a struct member, so the 68k launcher and the ARM blob
 * can no longer disagree about an offset (ZZRastan had a struct on one side and a hand-written
 * list of offsets on the other).
 *
 *   ARM : ZZS_SH(i)            -> volatile u32 lvalue (little-endian, Normal Non-Cacheable)
 *   68k : rd32(shared, i * 4)  -> byte-swapped 16-bit halves, as in ZZRastan
 *
 * Derived from zzrastan_shared.h (ZZRastan 1.0 baseline B). Slots 2, 8, 26, 27 and 28 are the
 * ARM exception-vector ABI and MUST keep these indices (zzs_vectors.S hard-codes them).
 *
 * ASCII only. Valid C89 and C++.
 */

#define ZZS_MAGIC            0x5A535452UL   /* 'ZSTR' */
#define ZZS_VERSION          0x00010000UL   /* release 1.0 (contract unchanged since revision 6, A2e) */

/* ------------------------------------------------------------------------------------------
 * Memory plan - firmware XX19d-RIX, XACP v1.8.   See docs/MEMORY_MAP.md for the audit.
 *
 *   fb  = board + 0x00010000 (Amiga side)        ARM physical = 0x00200000 + fb-relative offset
 *
 * Zorro-visible (the 68k may write these through the fb window):
 *   ARM 0x04500000..0x046F0000  blob            fb+0x04300000   Core1: WB/WA
 *   ARM 0x04700000..0x04700FE0  shared slots    fb+0x04500000   Core1: Non-Cacheable
 *   ARM 0x04B00000..0x04B10000  ROM chunk stage fb+0x04900000   Core1: Non-Cacheable
 *   ARM 0x05700000..0x05780000  PCM ring        fb+0x05500000   Core1: Non-Cacheable
 * ARM-private (never touched by the 68k), all inside the 16 MiB "established Core1 private"
 * window 0x22000000..0x23000000 of xacp_memory_map_v1_6.h, below the ZZMIDI pool:
 *   ARM 0x22000000..0x22400000  BSS + private stack                       WB/WA
 *   ARM 0x22400000..0x22C00000  decoded ROM store (gfx, 68k, Z80, ADPCM)  WB/WA
 *   ARM 0x22C00000..0x22D00000  newlib heap (sbrk)                        WB/WA
 *   ARM 0x22D00000..0x23000000  CPS-1 work memory (BurnMalloc arena)      WB/WA
 * ------------------------------------------------------------------------------------------ */
#define ZZS_ARM_FB_PHYS      0x00200000UL

#define ZZS_ARM_BLOB         0x04500000UL
#define ZZS_ARM_BLOB_END     0x046F0000UL
#define ZZS_ARM_SHARED       0x04700000UL
#define ZZS_ARM_STAGE        0x04B00000UL
#define ZZS_ARM_PCM          0x05700000UL

#define ZZS_FB_BLOB          0x04300000UL
#define ZZS_FB_SHARED        0x04500000UL
#define ZZS_FB_STAGE         0x04900000UL
#define ZZS_FB_PCM           0x05500000UL

#define ZZS_BLOB_MAX         0x001F0000UL   /* blob must end below the shared section */
#define ZZS_SHARED_LIMIT     0x00000FE0UL   /* firmware forensic marker lives at 0x04700FE0 */
#define ZZS_STAGE_CHUNK      0x00010000UL   /* 64 KiB per ROM chunk */
#define ZZS_PCM_SIZE         0x00080000UL   /* 512 KiB ring */
#define ZZS_PCM_RATE         22050UL

#define ZZS_ARM_PRIV_BASE    0x22000000UL
#define ZZS_ARM_BSS          0x22000000UL
#define ZZS_ARM_BSS_END      0x22400000UL
#define ZZS_ARM_ROMSTORE     0x22400000UL
#define ZZS_ARM_ROMSTORE_END 0x22C00000UL
#define ZZS_ARM_SBRK         0x22C00000UL
#define ZZS_ARM_SBRK_END     0x22D00000UL
#define ZZS_ARM_ARENA        0x22D00000UL
#define ZZS_ARM_ARENA_END    0x23000000UL
#define ZZS_ARM_PRIV_END     0x23000000UL

#define ZZS_REG_ARM_RUN_HI   0x0090
#define ZZS_REG_ARM_RUN_LO   0x0092

/* Native CPS-1 picture and the Picasso96 screen the launcher opens for it. */
#define ZZS_SRC_W            384
#define ZZS_SRC_H            224
#define ZZS_SCREEN_W         640
#define ZZS_SCREEN_H         480

/* ------------------------------------------------------------------------------------------
 * Slot indices
 * ------------------------------------------------------------------------------------------ */
/* control block */
#define ZZS_SH_MAGIC             0
#define ZZS_SH_VERSION           1
#define ZZS_SH_STATUS            2    /* vector ABI */
#define ZZS_SH_COMMAND           3
#define ZZS_SH_CMD_SEQ           4
#define ZZS_SH_ACK_SEQ           5
#define ZZS_SH_HEARTBEAT         6
#define ZZS_SH_FRAME             7    /* emulated frames since START */
#define ZZS_SH_ERROR             8    /* vector ABI */
#define ZZS_SH_DIAG              9
#define ZZS_SH_ROM_FILE          10   /* 68k -> ARM: file index of the staged chunk */
#define ZZS_SH_ROM_OFFSET        11   /* 68k -> ARM: offset of the chunk inside that file */
#define ZZS_SH_ROM_LENGTH        12   /* 68k -> ARM: chunk length in bytes (even, <= 64 KiB) */
#define ZZS_SH_ROM_RESERVED      13
#define ZZS_SH_ROM_CRC           14   /* ARM -> 68k: running CRC32 of that file so far */
#define ZZS_SH_RESET_SSP         15
#define ZZS_SH_RESET_PC          16
#define ZZS_SH_INPUT_P1          17   /* active HIGH: R=01 L=02 D=04 U=08 B1=10 B2=20 B3=40 */
#define ZZS_SH_INPUT_P2          18
#define ZZS_SH_INPUT_SYS         19   /* active HIGH: COIN1=01 COIN2=02 SERVICE=04 START1=10 START2=20 */
#define ZZS_IN_RIGHT   0x01UL
#define ZZS_IN_LEFT    0x02UL
#define ZZS_IN_DOWN    0x04UL
#define ZZS_IN_UP      0x08UL
#define ZZS_IN_B1      0x10UL       /* Strider: attack */
#define ZZS_IN_B2      0x20UL       /* Strider: jump */
#define ZZS_IN_B3      0x40UL
#define ZZS_SYS_COIN1  0x01UL
#define ZZS_SYS_COIN2  0x02UL
#define ZZS_SYS_SERVICE 0x04UL
#define ZZS_SYS_START1 0x10UL
#define ZZS_SYS_START2 0x20UL
#define ZZS_SH_DIP_A             20   /* RAW port value as the 68000 reads it (active low) */
#define ZZS_SH_DIP_B             21
#define ZZS_SH_DIP_C             22
#define ZZS_SH_RUN_FLAGS         23   /* 68k -> ARM, see ZZS_RUNF_* */
#define ZZS_SH_ERROR_AUX         24
#define ZZS_SH_ROM_SET           25
#define ZZS_SH_FAULT_PC          26   /* vector ABI */
#define ZZS_SH_FAULT_FAR         27   /* vector ABI */
#define ZZS_SH_FAULT_FSR         28   /* vector ABI */
#define ZZS_SH_PAUSE             29   /* 68k -> ARM */
#define ZZS_SH_TRAINER           30   /* reserved for 1.0 */
#define ZZS_SH_BUILD_ID          31

/* Picasso96 handoff - same semantics as ZZRastan P2A (asynchronous triple buffering) */
#define ZZS_SH_P96_ENABLE        32
#define ZZS_SH_P96_BASE          33   /* ARM address of the buffer Core1 may draw into */
#define ZZS_SH_P96_PITCH         34   /* bytes per row */
#define ZZS_SH_P96_W             35   /* screen width in pixels */
#define ZZS_SH_P96_H             36   /* screen height in pixels */
#define ZZS_SH_FRAME_READY       37   /* ARM -> 68k */
#define ZZS_SH_FLIP_SEQ          38   /* 68k -> ARM: frame actually acked/displayed */
#define ZZS_SH_RENDER_SEQ        39   /* 68k -> ARM: "you may render this one" */
#define ZZS_SH_RENDER_DONE       40   /* ARM -> 68k */
#define ZZS_SH_RENDERED          41   /* frames really rendered by Core1 */

/* PCM ring - ZZRastan transport, S16 mono little-endian */
#define ZZS_SH_PCM_ENABLE        42
#define ZZS_SH_PCM_BASE          43
#define ZZS_SH_PCM_SIZE          44
#define ZZS_SH_PCM_WRITE         45
#define ZZS_SH_PCM_READ          46
#define ZZS_SH_PCM_RATE          47
#define ZZS_SH_PCM_UNDERRUNS     48
#define ZZS_SH_SND_ENABLE        49

/* 68000 state (A1 diagnostics) */
#define ZZS_SH_M68K_PC           50
#define ZZS_SH_M68K_SP           51
#define ZZS_SH_M68K_SR           52
#define ZZS_SH_M68K_CYCLES       53   /* low 32 bits of the running total */
#define ZZS_SH_SAME_PC_FRAMES    54
#define ZZS_SH_IRQ2_COUNT        55
#define ZZS_SH_ILLEGAL_COUNT     56
#define ZZS_SH_BAD_READS         57   /* reads that reached no device */
#define ZZS_SH_BAD_WRITES        58
#define ZZS_SH_LAST_BAD_ADDR     59
#define ZZS_SH_EXEC_BAD_COUNT    60   /* jumps into unmapped memory (fatal) */
#define ZZS_SH_LAST_EXEC_ADDR    61
#define ZZS_SH_RESET_INSTR       62   /* 68000 RESET opcodes executed */
#define ZZS_SH_CPU_STATE         63
#define ZZS_SH_EXEC_RAM_COUNT    64   /* jumps into RAM (legal, informational) */
#define ZZS_SH_IRQ2_ACKS         65

/* CPS-A / CPS-B registers as latched at the last vblank */
#define ZZS_SH_CPSA_OBJ          66
#define ZZS_SH_CPSA_SCR1         67
#define ZZS_SH_CPSA_SCR2         68
#define ZZS_SH_CPSA_SCR3         69
#define ZZS_SH_CPSA_OTHER        70
#define ZZS_SH_CPSA_PAL          71
#define ZZS_SH_CPSA_SCR1_XY      72   /* (x << 16) | y */
#define ZZS_SH_CPSA_SCR2_XY      73
#define ZZS_SH_CPSA_SCR3_XY      74
#define ZZS_SH_CPSA_STAR1_XY     75
#define ZZS_SH_CPSA_STAR2_XY     76
#define ZZS_SH_CPSA_VIDEOCTRL    77   /* (rowscroll start << 16) | video control */
#define ZZS_SH_CPSB_LAYERCTRL    78
#define ZZS_SH_CPSB_PRIO01       79   /* (mask1 << 16) | mask0 */
#define ZZS_SH_CPSB_PRIO23       80
#define ZZS_SH_CPSB_PALCTRL      81
#define ZZS_SH_CPS_REG_WRITES    82
#define ZZS_SH_PAL_UPDATES       83
#define ZZS_SH_PAL_NONBLACK      84
#define ZZS_SH_SNDCMD_WRITES     85
#define ZZS_SH_SNDCMD_LAST       86
#define ZZS_SH_SNDFADE_WRITES    87
#define ZZS_SH_PORT_READS        88
#define ZZS_SH_OBJ_COUNT         89   /* sprites latched at the last vblank */
#define ZZS_SH_STARS_DRAWN       90
#define ZZS_SH_LAYER_ORDER       91   /* Draw[0..3] packed, 4 bits each, 0xF = not drawn */
#define ZZS_SH_DRAW_MASK         92

/* ROM transfer */
#define ZZS_SH_ROM_BYTES         93
#define ZZS_SH_ROM_CHUNKS        94
#define ZZS_SH_ROM_FILES_OK      95   /* bit n = file n complete with the expected CRC */
#define ZZS_SH_ROM_BAD_FILE      96
#define ZZS_SH_ROM_BAD_CRC       97
#define ZZS_SH_ROM_PRG_CRC       98   /* CRC32 of the assembled 1 MiB 68000 image */
#define ZZS_SH_ROM_GFX_CRC       99   /* CRC32 of the 4 MiB decoded tile store */

/* sound board */
#define ZZS_SH_Z80_PC            100
#define ZZS_SH_Z80_CYCLES        101
#define ZZS_SH_YM_WRITES         102
#define ZZS_SH_YM_IRQS           103
#define ZZS_SH_YM_TA_FIRES       104
#define ZZS_SH_YM_TB_FIRES       105
#define ZZS_SH_YM_KEYON          106
#define ZZS_SH_OKI_WRITES        107
#define ZZS_SH_OKI_STARTS        108
#define ZZS_SH_OKI_STATUS        109
#define ZZS_SH_Z80_BANK          110
#define ZZS_SH_Z80_BANK_CHANGES  111
#define ZZS_SH_SND_SAMPLES       112
#define ZZS_SH_SND_PEAK          113
#define ZZS_SH_AQ_DROP_OLD       114
#define ZZS_SH_AQ_HIGHWATER      115
#define ZZS_SH_AQ_FIFO_USED      116
#define ZZS_SH_RING_POST_AVG     117
#define ZZS_SH_AQ_DISCARDED      118
#define ZZS_SH_Z80_LATCH_READS   119
#define ZZS_SH_Z80_RESETS        120
#define ZZS_SH_OKI_PIN7          121

/* performance - every *_US slot is a MEASURED average in microseconds per frame */
#define ZZS_SH_PROF_FRAMES       122
#define ZZS_SH_PROF_WORK_US      123  /* everything before pacing */
#define ZZS_SH_PROF_PACE_US      124  /* time spent waiting in the pacer = margin */
#define ZZS_SH_PROF_CYCLONE_US   125
#define ZZS_SH_PROF_Z80_US       126
#define ZZS_SH_PROF_YM_US        127
#define ZZS_SH_PROF_OKI_US       128
#define ZZS_SH_PROF_SOUND_US     129  /* whole sound path incl. FIFO and ring pump */
#define ZZS_SH_PROF_VIDEO_US     130  /* per RENDERED frame */
#define ZZS_SH_PROF_PAL_US       131  /* palette rebuilds, per frame */
#define ZZS_SH_PROF_LAYERS_US    132  /* scroll layers + stars, per rendered frame */
#define ZZS_SH_PROF_SPRITES_US   133
#define ZZS_SH_PROF_CONVERT_US   134
#define ZZS_SH_PROF_CLEAN_US     135
#define ZZS_SH_PROF_WORK_MAX_US  136
#define ZZS_SH_PROF_VIDEO_MAX_US 137
#define ZZS_SH_PROF_WORK_LAST_US 138
#define ZZS_SH_PACE_WAITS        139
#define ZZS_SH_PACE_CATCHUP      140
#define ZZS_SH_PACE_HARD         141
#define ZZS_SH_PACE_MAX_LATE_US  142
#define ZZS_SH_PACE_PERIOD       143  /* NOMINAL constant, PMU cycles per frame - not a measure */
#define ZZS_SH_TOTAL_RUN_CYC     144
#define ZZS_SH_VP_CALLS          145
#define ZZS_SH_VP_GATE_PASS      146
#define ZZS_SH_VP_GATE_BLOCK     147  /* emulated frames NOT rendered: no buffer authorised */
#define ZZS_SH_VP_BLOCK_CONS_MAX 148
#define ZZS_SH_MMU_BSS_DESC      149
#define ZZS_SH_MMU_ROM_DESC      150
#define ZZS_SH_MMU_P96_DESC      151
#define ZZS_SH_MMU_STAGE_DESC    152
#define ZZS_SH_PAUSED_FRAMES     153
#define ZZS_SH_STACK_TOP         154
#define ZZS_SH_ARENA_USED        155
#define ZZS_SH_STACK_FREE_MIN    156  /* untouched bytes left at the bottom of the private stack */

/* PC trace: PC at the end of each of the first 32 emulated frames */
#define ZZS_SH_PCTRACE_COUNT     157
#define ZZS_SH_PCTRACE0          158
#define ZZS_PCTRACE_MAX          32

/* --- A2: conversion histogram, PMU event counters, sound rate control (ARM -> 68k) --- */
#define ZZS_SH_CONV_H0          190   /* conversions shorter than 3 ms */
#define ZZS_SH_CONV_H1          191   /*  3 ..  5 ms */
#define ZZS_SH_CONV_H2          192   /*  5 ..  8 ms */
#define ZZS_SH_CONV_H3          193   /*  8 .. 12 ms */
#define ZZS_SH_CONV_H4          194   /* 12 .. 16 ms */
#define ZZS_SH_CONV_H5          195   /* 16 ms and more */
#define ZZS_SH_WORK_LATE        196   /* frames whose work exceeded one frame period */
#define ZZS_SH_IPC_X100         197   /* instructions per cycle x 100, over the work time */
#define ZZS_SH_INSTR_K          198   /* instructions per frame, thousands */
#define ZZS_SH_L1D_REFILL       199   /* L1 data cache refills per frame */
#define ZZS_SH_BR_MISS          200   /* mispredicted branches per frame */
#define ZZS_SH_L1I_REFILL       201   /* L1 instruction cache refills per frame */
#define ZZS_SH_DSTALL_US        202   /* data-side stall time per frame, us */
#define ZZS_SH_ISTALL_US        203   /* instruction-side stall time per frame, us */
#define ZZS_SH_CONV_L1D         204   /* L1 data refills per conversion */
#define ZZS_SH_CONV_DSTALL_US   205   /* data-side stall time per conversion, us */
#define ZZS_SH_CONV_MAX_US      206   /* longest conversion, us */
#define ZZS_SH_CONV_METHOD      207   /* 0 plain, 1 one-way allocation, 2 one-way + prefetch */
#define ZZS_SH_VP_BLOCK_RUN     208   /* frames not rendered after the first rendered one */
#define ZZS_SH_SND_RATE_HZ      209   /* current production rate */
#define ZZS_SH_SND_RATE_MIN     210
#define ZZS_SH_SND_RATE_MAX     211
#define ZZS_SH_SND_BACKLOG      212   /* queued samples (FIFO + ring), filtered */
#define ZZS_SH_SND_BACKLOG_MIN  213
#define ZZS_SH_SND_BACKLOG_MAX  214
#define ZZS_SH_PUMP_CALLS       215   /* calls of the FIFO -> ring pump */
#define ZZS_SH_ACTLR            216   /* ACTLR as read during a conversion */
/* --- A2b: per Picasso96 buffer (k = 0..2, in the order Core1 first saw them) --- */
#define ZZS_SH_BUF_BASE(k)      (217 + 5 * (k))   /* ARM address of the buffer */
#define ZZS_SH_BUF_COUNT(k)     (218 + 5 * (k))   /* conversions into it */
#define ZZS_SH_BUF_AVG_US(k)    (219 + 5 * (k))
#define ZZS_SH_BUF_MAX_US(k)    (220 + 5 * (k))
#define ZZS_SH_BUF_PAR(k)       (221 + 5 * (k))   /* PAR of the hardware translation (ATS1CPR) */
#define ZZS_SH_L2_ENABLED       232   /* PL310 control register bit 0 */
#define ZZS_SH_L2_FIX           233   /* 1 = descriptors cleaned to DDR through the PL310 */
#define ZZS_SH_BUF_BAD(k)       (234 + (k))   /* conversions where that buffer was NOT WB/WA */
/* --- A2c: frame pacing locked on the display, sound latency --- */
#define ZZS_SH_VL_ENABLED       237   /* 1 = the pacer follows the PAN acknowledges */
#define ZZS_SH_VL_PHASE_US      238   /* last observed ack time after frame start, us */
#define ZZS_SH_VL_ERR_US        239   /* filtered phase error, us (signed) */
#define ZZS_SH_VL_SAMPLES       240   /* acks observed */
#define ZZS_SH_VL_ADJ_US        241   /* total correction applied to the pacer, us (signed) */
#define ZZS_SH_VL_LOCKED        242   /* frames with |error| < 500 us */
#define ZZS_SH_SND_MAXBL_FRAME  243   /* frame at which the sound backlog peaked */
#define ZZS_SH_AQ_FIRST_DROP    244   /* frame of the first FIFO overflow (0 = never) */
#define ZZS_SH_AQ_TRIMS         245   /* excess backlog trimmed at once */
#define ZZS_SH_AQ_PRESTART      246   /* samples not queued before AHI's first read */
#define ZZS_SH_AHI_START_FRAME  247   /* frame at which AHI read the ring for the first time */
#define ZZS_SH_SLOT_COUNT       248   /* 248 slots = 992 bytes */

/* ------------------------------------------------------------------------------------------ */
#define ZZS_ST_BOOTING   0
#define ZZS_ST_READY     1
#define ZZS_ST_RUNNING   2
#define ZZS_ST_STOPPING  3
#define ZZS_ST_ERROR     0xEE
#define ZZS_ST_STOPPED   0xFF

#define ZZS_CMD_NONE        0
#define ZZS_CMD_INIT        1
#define ZZS_CMD_ROM_BEGIN   2
#define ZZS_CMD_ROM_CHUNK   3
#define ZZS_CMD_ROM_COMMIT  4
#define ZZS_CMD_START       5
#define ZZS_CMD_STOP        6
#define ZZS_CMD_SELFTEST    7

#define ZZS_ERR_NONE            0
#define ZZS_ERR_BAD_MAGIC       1
#define ZZS_ERR_ROM_STATE       2    /* chunk outside BEGIN..COMMIT */
#define ZZS_ERR_ROM_BOUNDS      3    /* bad file index, offset, length or parity */
#define ZZS_ERR_ROM_ORDER       4    /* chunk is not the next one expected for that file */
#define ZZS_ERR_ROM_CRC         5    /* complete file, wrong CRC32 */
#define ZZS_ERR_ROM_INCOMPLETE  6    /* COMMIT with files missing */
#define ZZS_ERR_BAD_VECTOR      7
#define ZZS_ERR_MEMMAP          8
#define ZZS_ERR_ENGINE_INIT     9
#define ZZS_ERR_CPU_RESET       10
#define ZZS_ERR_EXEC_UNMAPPED   11
#define ZZS_ERR_DATA_ABORT      12   /* vector ABI */
#define ZZS_ERR_PREFETCH_ABORT  13   /* vector ABI */
#define ZZS_ERR_UNDEF_INSTR     14   /* vector ABI */
#define ZZS_ERR_NOT_READY       15   /* START before a successful COMMIT */
#define ZZS_ERR_P96_GEOMETRY    16

#define ZZS_DIAG_BOOT           0xA000
#define ZZS_DIAG_MMU_OK         0xA010
#define ZZS_DIAG_VECTORS_OK     0xA020
#define ZZS_DIAG_READY          0xA030
#define ZZS_DIAG_INIT_OK        0xA100
#define ZZS_DIAG_ROM_BEGIN      0xA200
#define ZZS_DIAG_ROM_COMMIT_OK  0xA210
#define ZZS_DIAG_ENGINE_OK      0xA220
#define ZZS_DIAG_CPU_RESET_OK   0xA300
#define ZZS_DIAG_RUNNING        0xA310

/* RUN_FLAGS: bisect switches for hardware bring-up. All 0 = full emulation. */
#define ZZS_RUNF_NO_SOUND   0x0001   /* do not run the Z80 / YM2151 / MSM6295 */
#define ZZS_RUNF_NO_VIDEO   0x0002   /* run the game but never render */
#define ZZS_RUNF_NO_PACE    0x0004   /* free-run (benchmark only) */
#define ZZS_RUNF_CONV_PLAIN 0x0010   /* conversion as in A0/A1 (comparison only) */
#define ZZS_RUNF_CONV_NOPLD 0x0020   /* one-way allocation without software prefetch (comparison only) */
#define ZZS_RUNF_NO_L2FIX   0x0040   /* A2 remap: descriptors cleaned to L2 only (comparison only) */
#define ZZS_RUNF_NO_VLOCK   0x0080   /* free pacer, not locked on the display (comparison only) */

/* ------------------------------------------------------------------------------------------
 * ROM set: MAME "strider" = Strider (USA, B-Board 89624B-2), the parent set.
 * Names, sizes and CRC32 checked against MAME cps1.cpp and FBA d_cps1.cpp (identical).
 * The table itself lives in zzs_rom.c (ARM) and ZZStrider.c (68k: names and sizes only).
 * ------------------------------------------------------------------------------------------ */
#define ZZS_ROM_FILE_COUNT   16
/* The ROM files, in transfer order: X(index, name, size). One list for Core1 (zzs_rom.c adds the
   CRC and the decode role), the launcher and the GUI. MAME set "strider" (parent). */
#define ZZS_ROM_LIST(X) \
    X( 0, "30.11f",    0x020000UL) X( 1, "35.11h",    0x020000UL) \
    X( 2, "31.12f",    0x020000UL) X( 3, "36.12h",    0x020000UL) \
    X( 4, "st-14.8h",  0x080000UL) X( 5, "st-2.8a",   0x080000UL) \
    X( 6, "st-11.10a", 0x080000UL) X( 7, "st-5.4a",   0x080000UL) \
    X( 8, "st-9.6a",   0x080000UL) X( 9, "st-1.7a",   0x080000UL) \
    X(10, "st-10.9a",  0x080000UL) X(11, "st-4.3a",   0x080000UL) \
    X(12, "st-8.5a",   0x080000UL) X(13, "09.12b",    0x010000UL) \
    X(14, "18.11c",    0x020000UL) X(15, "19.12c",    0x020000UL)
#define ZZS_ROM_TOTAL_BYTES  0x00550000UL   /* 1 MiB + 4 MiB + 64 KiB + 256 KiB */

#endif /* ZZS_SHARED_H */
