/*
 * zzresid_shared.h - ZZReSID Core1 shared ABI (ring PCM model)
 *
 * Free-running ring model (validated ZZPicoDrive/ZZMIDI pattern):
 *   Core1 renders continuously into a PCM ring, advancing SH_PCM_WRITE.
 *   68k reads the ring at AHI rate, advancing SH_PCM_READ.
 *   No per-frame handshake -> no morse gaps, continuous audio.
 *
 * Cache-line-safe: 68k-owned and ARM-owned fields in separate lines.
 * All 32-bit shared fields accessed with byte-swap on ARM (ARM32()).
 *
 * DDR (ZZPicoDrive/ZZDoom validated):
 *   Core1 blob     ARM 0x04200000  (fb + 0x04000000)
 *   Shared mailbox ARM 0x04300000  (fb + 0x04100000)
 *   PCM ring       ARM 0x04340000  (fb + 0x04140000)
 * Launch: REG_ARM_RUN_HI=0x0090 (0x0420), REG_ARM_RUN_LO=0x0092 (0x0000)
 *
 * ASCII-only.  Author: Xanxi / 2026
 */

#ifndef ZZRESID_SHARED_H
#define ZZRESID_SHARED_H

/* ---- DDR addresses (ARM view) ---- */
#define ZZRESID_BLOB_ARM_ADDR    0x04200000UL
#define ZZRESID_SHARED_ARM_ADDR  0x05A00000UL
#define ZZRESID_PCM_ARM_ADDR     0x05A40000UL
/* .sid data staging buffer, 64 KB. Same 1 MB MMU section as the mailbox and
 * the ring (0x05A), therefore Normal Non-Cacheable: no flushing needed. */
#define ZZRESID_SID_ARM_ADDR     0x05A50000UL
#define ZZRESID_SID_MAX_BYTES    65536UL

#define ZZRESID_BLOB_FB_OFFSET   0x04000000UL
#define ZZRESID_SHARED_FB_OFFSET 0x05800000UL
#define ZZRESID_PCM_FB_OFFSET    0x05840000UL
#define ZZRESID_SID_FB_OFFSET    0x05850000UL

#define ZZRESID_REG_ARM_RUN_HI   0x0090
#define ZZRESID_REG_ARM_RUN_LO   0x0092
#define ZZRESID_RUN_HI_VALUE     0x0420
#define ZZRESID_RUN_LO_VALUE     0x0000

/*
 * ---- Mailbox: uint32 array at ZZRESID_SHARED_ARM_ADDR ----
 * LINE 0 (0-7):  68k -> ARM
 * LINE 1 (8-15): ARM -> 68k
 * LINE 2+ (16+): SID register batch
 */

/* LINE 0: 68k -> ARM */
#define SH_CMD            0
#define SH_SAMPLE_RATE    1
#define SH_CHIP_MODEL     2
#define SH_REG_COUNT      3
#define SH_PCM_READ       4   /* 68k read cursor (in samples) */
#define SH_FILTER_CFG     5   /* filter selection, see ZZRESID_FILT_* */
#define SH_CANARY_68K     7

/* LINE 1: ARM -> 68k */
#define SH_MAGIC          8
#define SH_STATUS         9
#define SH_PCM_WRITE     10   /* Core1 write cursor (in samples) */
#define SH_HEARTBEAT     12
#define SH_ERROR         13
#define SH_RENDER_CYCLES 14
#define SH_CANARY_ARM    15

/* LINE 2 (16-23): ARM -> 68k diagnostics (render loop instrumentation) */
#define SH_DIAG_CAPACITY   16   /* requested capacity this render call     */
#define SH_DIAG_DELTA_FED  17   /* delta cycles fed to clock()             */
#define SH_DIAG_GOT_LAST   18   /* got from last clock() call              */
#define SH_DIAG_PRODUCED   19   /* total samples produced this call        */
#define SH_DIAG_ZEROPAD    20   /* zero_pad_count - MUST always be 0       */
#define SH_DIAG_PENDING    21   /* pending_cycles carried to next call     */
#define SH_DIAG_FRAC_FP    22   /* cycle_frac_fp (16.16 fractional)        */
#define SH_DIAG_CLOCKCALLS 23   /* number of clock() calls this render     */

/* LINE 3 (24-31): ARM -> 68k sample statistics */
#define SH_DIAG_MIN_SAMPLE 24   /* min sample value (signed, as uint32)    */
#define SH_DIAG_MAX_SAMPLE 25   /* max sample value (signed, as uint32)    */
#define SH_DIAG_CLIP_POS   26   /* clip_pos_count                          */
#define SH_DIAG_CLIP_NEG   27   /* clip_neg_count                          */
#define SH_DIAG_RING_BEFORE 28  /* ring_write_before                       */
#define SH_DIAG_RING_AFTER  29  /* ring_write_after                        */
#define SH_DIAG_OVERRUNS   30   /* ring full events (backpressure hits)    */
#define SH_DIAG_RESERVED31 31

/* LINE 4 (32-39): ARM -> 68k reconfiguration status.
 * No silent fallback: the blob reports what it ACTUALLY applied, and
 * bumps SH_GENERATION on every accepted RECONFIG. The 68k must wait for
 * SH_GENERATION to change before believing a reconfigure succeeded. */
#define SH_GENERATION      32   /* incremented on each accepted setup   */
#define SH_ACTIVE_MODEL    33   /* chip model actually in use           */
#define SH_ACTIVE_FILTER   34   /* filter cfg actually applied          */
#define SH_SETUP_ERROR     35   /* ZZRESID_ERR_*, 0 = clean             */
#define SH_REQUESTED_MODEL 36   /* echo of what the 68k asked for       */
#define SH_SCTLR           37   /* SCTLR after MMU init - proof of state */
#define SH_SID_WRITES      38   /* ARM->68k: SID register writes done by the
                                 * 6502. If this stays 0 the tune is not
                                 * running - the fault is upstream of audio. */
#define SH_SID_FRAMES      39   /* ARM->68k: play() calls made               */

/* SCTLR bits worth checking in SH_SCTLR */
#define ZZRESID_SCTLR_M    (1u<<0)    /* MMU enabled     */
#define ZZRESID_SCTLR_C    (1u<<2)    /* D-cache enabled */
#define ZZRESID_SCTLR_I    (1u<<12)   /* I-cache enabled */

/* LINE 5 (40-47): 68k -> ARM, .sid tune parameters from the PSID header */
#define SH_SID_LOAD       40   /* C64 load address                          */
#define SH_SID_INIT       41   /* init routine address                      */
#define SH_SID_PLAY       42   /* play routine address                      */
#define SH_SID_SONG       43   /* subsong to play, 0-based                  */
#define SH_SID_LEN        44   /* byte count staged in the transfer buffer  */
#define SH_MODE           45   /* ZZRESID_MODE_*                            */
#define SH_ABI_VERSION    47   /* ARM->68k: ZZRESID_ABI_VERSION of the
                                * RUNNING blob. The launcher refuses to work
                                * with a blob built against another layout -
                                * a resident stale blob silently reading old
                                * addresses cost us several test cycles. */
#define SH_SID_FIRST      46   /* ARM->68k: first 4 bytes Core1 read back
                                * from the staging buffer. Proves the
                                * transfer landed. */
/* 47 reserved */

/* LINE 6+: SID register batch (68k -> ARM) */
#define SH_REG_BATCH_BASE 48
#define ZZRESID_MAX_REGS  256

/* Shadow copy of the 25 SID registers as last written by the 6502, one per
 * word. Placed after the register batch so nothing else shifts. This is the
 * only way to see what the tune is actually asking the chip to do. */
#define SH_SID_SHADOW_BASE (SH_REG_BATCH_BASE + ZZRESID_MAX_REGS)
#define ZZRESID_SID_REGS   25

/* ---- Multi-SID, appended at the END of the mailbox ----
 * Nothing above moves, so no address and no existing slot changes. Two
 * separate lines keep the cache-line discipline that has served this
 * protocol well: one owned by the 68k, one owned by the ARM. */

/* 68k -> ARM: what the launcher asks for */
#define SH_MULTI_BASE       (SH_SID_SHADOW_BASE + 32)
#define SH_NUM_SIDS         (SH_MULTI_BASE + 0)
#define SH_SID1_BASE        (SH_MULTI_BASE + 1)   /* C64 address, e.g. 0xD420 */
#define SH_SID2_BASE        (SH_MULTI_BASE + 2)   /* reserved for 3SID       */
/* Mirror chip 0's writes onto chip 1. Two chips are a property of the
 * EMULATED HARDWARE, not of the tune: a 1SID tune must still come out of
 * both, exactly as a two-chip cartridge behaves. Set to 0 for a tune that
 * addresses the second chip itself. */
#define SH_SID_MIRROR       (SH_MULTI_BASE + 3)
/* +4..+7 reserved */

/* ARM -> 68k: what was actually applied, and per-chip statistics.
 * SH_DIAG_MIN_SAMPLE / MAX_SAMPLE now describe the LEFT channel (SID0);
 * the right channel gets its own pair here. */
#define SH_MULTI_DIAG       (SH_MULTI_BASE + 8)
#define SH_ACTIVE_NUM_SIDS  (SH_MULTI_DIAG + 0)
#define SH_SID_WRITES1      (SH_MULTI_DIAG + 1)
#define SH_DIAG_MIN_R       (SH_MULTI_DIAG + 2)
#define SH_DIAG_MAX_R       (SH_MULTI_DIAG + 3)
#define SH_WRITE_CYCLES     (SH_MULTI_DIAG + 4)
#define SH_SID_WRITES2      (SH_MULTI_DIAG + 5)
/* Non-zero means the two chips produced a different number of samples in
 * one render: they would be drifting apart, which must never happen. */
#define SH_DIAG_LEN_MISMATCH (SH_MULTI_DIAG + 6)
/* Interrupts taken by the 6502 since the tune was initialised. Non-zero
 * only for a tune with play == $0000, which drives itself from its own IRQ
 * instead of a play routine. Zero while such a tune is loaded means the
 * interrupt model never fired and the tune is silent for that reason.
 *
 * This uses the slot that was already reserved here, so no address and no
 * existing slot moves and the ABI does not change: a blob that predates it
 * simply leaves the slot at zero, which reads as "no interrupts", which is
 * exactly what such a blob does. */
#define SH_IRQ_COUNT         (SH_MULTI_DIAG + 7)

#define ZZRESID_MAX_SIDS    3

/* Diagnostics for the play == $0000 interrupt path, written ONLY while
 * that path is active. Appended at the end: nothing above moves, and a
 * blob that predates them leaves them at zero. */
#define SH_IRQ_DIAG          (SH_MULTI_DIAG + 8)
/* Times the 6502 stopped consuming cycles and the frame had to be
 * abandoned. Non-zero means the CPU is jammed on an instruction, which is
 * what a tune that plays for a few seconds and then stops looks like. */
#define SH_IRQ_RUNAWAY       (SH_IRQ_DIAG + 0)
/* Where the 6502 was when the frame ended - the address to look at when
 * SH_IRQ_RUNAWAY starts counting. */
#define SH_IRQ_PC            (SH_IRQ_DIAG + 1)
/* +2..+3 reserved */

#define ZZRESID_SHARED_WORDS (SH_IRQ_DIAG + 4)

/* ---- PCM ring at ZZRESID_PCM_ARM_ADDR ----
 * S16 stereo interleaved, 4 bytes/sample: (L<<16)|R big-endian.
 * Ring size in SAMPLES (stereo frames). Power of 2 for cheap masking.
 * 8192 samples = 32KB = ~186ms at 44100Hz. */
#define ZZRESID_RING_SAMPLES 8192
#define ZZRESID_RING_MASK    (ZZRESID_RING_SAMPLES - 1)
#define ZZRESID_RING_BYTES   (ZZRESID_RING_SAMPLES * 4)

/* ---- Status ---- */
#define ZZRESID_ST_INIT      0
#define ZZRESID_ST_READY     1
#define ZZRESID_ST_RUNNING   2
#define ZZRESID_ST_STOPPED   0xFF

/* ---- Commands ---- */
#define ZZRESID_CMD_NONE     0
#define ZZRESID_CMD_START    1
#define ZZRESID_CMD_STOP     2
#define ZZRESID_CMD_RESET    4
/* Re-read SH_FILTER_CFG / SH_CHIP_MODEL and reconfigure reSID in place.
 * Lets a new launcher run change settings on an already-resident blob,
 * avoiding arm_app_run (not reentrant) and a power cycle. */
#define ZZRESID_CMD_RECONFIG 5
#define ZZRESID_CMD_LOAD_SID 6   /* copy staged bytes into C64 RAM          */
#define ZZRESID_CMD_SID_INIT 7   /* run the tune's init routine             */

/* ---- Render mode (SH_MODE) ----
 * TONE keeps the old fixed test note. TUNE runs the 6502 player: one play()
 * call then one PAL frame of samples, repeatedly. */
#define ZZRESID_MODE_TONE    0
#define ZZRESID_MODE_TUNE    1

/* PAL video frame = 19705 SID cycles = 882 samples at 44100 Hz */
#define ZZRESID_PAL_FPS      50

/* ---- Chip ---- */
#define ZZRESID_CHIP_6581    0
#define ZZRESID_CHIP_8580    1

/* ---- Filter configuration (SH_FILTER_CFG bitmask) ----
 * Read once at sid_setup(), before STATUS=READY.
 * Escalation order imposed for testing:
 *   0 = both off      (step 1, validated)
 *   2 = external only (step 2)
 *   3 = both on       (step 3)
 */
#define ZZRESID_FILT_NONE     0
#define ZZRESID_FILT_INTERNAL 1
#define ZZRESID_FILT_EXTERNAL 2

/* ---- Setup error codes (SH_SETUP_ERROR) ----
 * Reported, never silently swallowed. The value applied is still published
 * in SH_ACTIVE_MODEL / SH_ACTIVE_FILTER so the 68k can compare. */
#define ZZRESID_ERR_NONE       0
#define ZZRESID_ERR_BAD_MODEL  1   /* chip model out of range, clamped   */
#define ZZRESID_ERR_BAD_FILTER 2   /* filter cfg out of range, clamped   */
#define ZZRESID_ERR_BAD_RATE   3   /* sample rate out of range, clamped  */

/* Bump on ANY change to addresses or slot numbers in this file.
 * 4: multi-SID block appended. Nothing moved, but an ABI 3 blob would
 * leave the new slots unwritten, so the launcher must be able to refuse
 * it rather than read zeros as truth.
 * 5: SH_SID_MIRROR given meaning. The layout does not change, but an ABI 4
 * blob would ignore it and leave one channel silent with no explanation -
 * precisely the kind of quiet mismatch this guard exists to catch. Since the blob returns cleanly to the firmware's
 * core1_loop(), a bump costs a relaunch, not a power cycle. */
#define ZZRESID_ABI_VERSION  5

#define ZZRESID_MAGIC        0x5A534944UL
#define ZZRESID_CLOCK_PAL    985248UL
#define ZZRESID_CLOCK_NTSC  1022727UL
#define ZZRESID_CANARY_68K   0xC0DEC0DEUL
#define ZZRESID_CANARY_ARM   0xB10BB10BUL

#define ZZRESID_PACK_REG(reg, val, cyc) \
    (((uint32_t)(reg) << 24) | ((uint32_t)(val) << 16) | ((uint32_t)(cyc) & 0xFFFF))
#define ZZRESID_UNPACK_REG(w)  ((uint8_t)(((w) >> 24) & 0xFF))
#define ZZRESID_UNPACK_VAL(w)  ((uint8_t)(((w) >> 16) & 0xFF))
#define ZZRESID_UNPACK_CYC(w)  ((uint16_t)((w) & 0xFFFF))

/*
 * Build selection:
 *   ZZRESID_BUILD_A: fixed 440Hz, no reSID (ring test)
 *   ZZRESID_BUILD_C: full reSID render
 */
#if !defined(ZZRESID_BUILD_A) && !defined(ZZRESID_BUILD_C)
#define ZZRESID_BUILD_A 1
#endif

#endif /* ZZRESID_SHARED_H */
