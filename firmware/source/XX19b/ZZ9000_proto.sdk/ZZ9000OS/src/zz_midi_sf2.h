/*
 * zz_midi_sf2.h -- XACP OP_MIDI_SF2 handler for ZZ9000 firmware
 *
 * XACP v1.6 ZZMIDI service interface for TinySoundFont/TinyMidiLoader.
 * Supports file playback, realtime MIDI and chunked SF2/MIDI uploads.
 *
 * All shared DDR fields are big-endian.
 * ARM uses CTRL_RD / CTRL_WR (defined in zz_midi_sf2.c).
 * Amiga uses CTRL_RD32 / CTRL_WR32 with XMID_OFF_xxx offsets.
 *
 * Build: include in main.c after ax.h.
 * Add zz_midi_sf2.c to the firmware Makefile.
 * tsf.h and tml.h must be in the firmware include path.
 *
 * ASCII-only source file -- no UTF-8 characters.
 */

#ifndef ZZ_MIDI_SF2_H
#define ZZ_MIDI_SF2_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * XACP opcode
 * --------------------------------------------------------------------- */
#ifndef OP_MIDI_SF2
#define OP_MIDI_SF2  0x0120
#endif

/* -----------------------------------------------------------------------
 * Sub-commands
 * --------------------------------------------------------------------- */
#define XMID_CMD_RESET        0
#define XMID_CMD_SELFTEST     1
#define XMID_CMD_LOAD_SF2     2   /* legacy single-chunk path (<=16MB only) */
#define XMID_CMD_LOAD_MIDI    3
#define XMID_CMD_START        4
#define XMID_CMD_STOP         5
#define XMID_CMD_PAUSE        6
#define XMID_CMD_RESUME       7
#define XMID_CMD_SET_VOLUME   8
#define XMID_CMD_STATUS       9
#define XMID_CMD_RENDER_POLL  10
#define XMID_CMD_POOL_TEST    11

/* SF2 chunked upload commands. */
#define XMID_CMD_SF2_BEGIN        12
#define XMID_CMD_SF2_CHUNK        13
#define XMID_CMD_SF2_COMMIT_LOAD  14
#define XMID_CMD_SF2_FAKE_COMMIT  15

/* CAMD realtime commands. */
#define XMID_CMD_REALTIME_INIT    16  /* arm: init TSF for realtime, no MIDI file */
#define XMID_CMD_EVENT_BATCH      17  /* arm: inject batch of MIDI events into TSF */
#define XMID_CMD_ALL_NOTES_OFF    18  /* arm: send all-notes-off on all channels */
#define XMID_CMD_REALTIME_STOP    19  /* arm: stop realtime render, silence */

/* MIDI chunked upload commands. */
#define XMID_CMD_MIDI_BEGIN       20  /* announce total MIDI size (midi_size) */
#define XMID_CMD_MIDI_CHUNK       21  /* copy one chunk upload buf -> private */

/* -----------------------------------------------------------------------
 * Engine states
 * --------------------------------------------------------------------- */
#define XMID_STATE_EMPTY    0u
#define XMID_STATE_READY    1u
#define XMID_STATE_PLAYING  2u
#define XMID_STATE_PAUSED   3u
#define XMID_STATE_DONE     4u
#define XMID_STATE_ERROR    0x80000000u

/* SF2 upload sub-states (stored in sf2_state field) */
#define XMID_SF2_STATE_EMPTY      0u  /* no SF2 loaded or in progress */
#define XMID_SF2_STATE_UPLOADING  1u  /* SF2_BEGIN accepted, chunks expected */
#define XMID_SF2_STATE_CHUNK_DONE 2u  /* one chunk copied to pool, more may follow */
#define XMID_SF2_STATE_READY      3u  /* tsf_load_memory completed OK */
#define XMID_SF2_STATE_ERROR      4u  /* upload or tsf_load failed */

/* -----------------------------------------------------------------------
 * Error codes
 * --------------------------------------------------------------------- */
#define XMID_ERR_NONE                0u
#define XMID_ERR_BAD_MAGIC           1u
#define XMID_ERR_BAD_SF2             2u
#define XMID_ERR_BAD_MIDI            3u
#define XMID_ERR_NO_HEAP             4u
#define XMID_ERR_TSF_FAIL            5u
#define XMID_ERR_TML_FAIL            6u
#define XMID_ERR_FPU_FAIL            7u
#define XMID_ERR_UNDERRUN            8u
/* SF2 chunked-upload errors. */
#define XMID_ERR_SF2_TOO_LARGE        9u
#define XMID_ERR_BAD_OFFSET          10u
#define XMID_ERR_BAD_SIZE            11u
#define XMID_ERR_BAD_STATE           12u
#define XMID_ERR_UPLOAD_INCOMPLETE   13u
#define XMID_ERR_TSF_LOAD_FAILED     14u
#define XMID_ERR_BAD_PRIVATE_POOL_ADDR 15u
/* Client ABI version or control-structure mismatch. */
#define XMID_ERR_BAD_ABI             16u

/* -----------------------------------------------------------------------
 * DDR memory layout (offsets relative to framebuffer base)
 *
 * Amiga : fb = board + MNT_FB_BASE (0x00010000)
 * ARM : video_state->framebuffer
 *
 * XACP existing zones (DO NOT OVERLAP):
 * 0x04000000 XACP command block
 * 0x04002000 StreamControl (MP3)
 * 0x04100000 MP3 input ring (512 KB)
 * 0x04200000 MP3 PCM output (1 MB) <-- MP3 only, never use for MIDI
 *
 * ZZ-MIDI corridor -- XACP v1.6 (XX19a). All ZZMIDI shared zones live in
 * fb+0x06000000 .. fb+0x06300000 (ARM 0x06200000 .. 0x06500000), placed
 * right AFTER the ZZDoom Core1 heap release interval and far below the
 * ZZDoom save buffer:
 * 0x06000000 XMID_CTRL_OFFSET control block (208 B, 64 KB rsvd)
 * 0x06010000 XMID_FIFO_OFFSET realtime event FIFO (~8 KB)
 * 0x06100000 XMID_PCM_RING_OFFSET MIDI PCM output ring (1 MB)
 * 0x06200000 XMID_UPLOAD_OFFSET shared transfer buffer (1 MB)
 * SF2/MIDI raw files and the TSF/TML heap live in ARM PRIVATE DDR
 * (0x23000000-0x30000000), never in the fb window.
 *
 * PCM ring uses monotone counters:
 * available = pcm_write_total - pcm_read_total
 * write_pos = pcm_write_total % XMID_PCM_RING_SIZE
 * read_pos = pcm_read_total % XMID_PCM_RING_SIZE
 * --------------------------------------------------------------------- */
#define XMID_CTRL_OFFSET      0x06000000UL
#define XMID_PCM_RING_OFFSET  0x06100000UL
#define XMID_PCM_RING_SIZE    (1UL  * 1024UL * 1024UL)   /* 1 MB */

/* -----------------------------------------------------------------------
 * Shared upload corridor (framebuffer-relative)
 *
 * SF2 and MIDI data are transferred through the 1 MB shared upload buffer
 * and copied into ARM-private DDR before parsing. No complete SF2 or MIDI
 * file is kept in shared staging memory.
 * --------------------------------------------------------------------- */
#define XMID_UPLOAD_OFFSET       0x06200000UL  /* 1 MB shared transfer buf */
#define XMID_UPLOAD_SIZE         (1UL * 1024UL * 1024UL)
#define XMID_SHARED_END          0x06300000UL  /* fb-rel corridor end */

#define XMID_SF2_MAX_SIZE        (32UL * 1024UL * 1024UL)  /* 32 MB REAL */
#define XMID_MIDI_MAX_SIZE       (6UL  * 1024UL * 1024UL)  /* 6 MB */

#define XMID_SF2_CHUNK_MAX       XMID_UPLOAD_SIZE
#define XMID_MIDI_CHUNK_MAX      XMID_UPLOAD_SIZE

/* -----------------------------------------------------------------------
 * EVENT STAGING -- CAMD realtime (fb-relative)
 *
 * 68k writes MIDI events here; ARM reads and injects into TSF.
 * v1.6: shares the FIFO base inside the ZZMIDI corridor.
 *
 * fb+0x06010000 event staging base (== XMID_FIFO_OFFSET)
 * fb+0x06011000 event staging end (4 KB = 512 events max)
 *
 * Layout in staging:
 * [0x00] ULONG event_count -- number of valid events
 * [0x04] XMID_Event[512] -- event array
 *
 * XMID_Event: 8 bytes per event (status/data1/data2/reserved + timestamp)
 * --------------------------------------------------------------------- */
#define XMID_EVENT_STAGING_OFFSET  0x06010000UL
#define XMID_EVENT_STAGING_SIZE    (4UL * 1024UL)   /* 4 KB, 512 events max */
#define XMID_EVENT_MAX_BATCH       64u              /* max events per batch */

/* MIDI event for EVENT_BATCH (8 bytes, packed) */
typedef struct {
    uint8_t  status;     /* MIDI status byte (0x80-0xFF) */
    uint8_t  data1;      /* first data byte */
    uint8_t  data2;      /* second data byte (0 if not needed) */
    uint8_t  reserved;   /* pad to 4 bytes */
    uint32_t sample_time;/* sample offset (0 = immediate) */
} XMID_Event;            /* 8 bytes */

/* ---------------------------------------------------------------------
 * Realtime MIDI FIFO
 *
 * Single producer (68k) / single consumer (ARM), at fb+0x06010000.
 * write_idx and read_idx occupy separate 32-byte cache lines to avoid
 * false sharing between the two processors.
 *
 * 0x00 magic 'ZMFF'
 * 0x04 version
 * 0x08 fifo_size
 * 0x0C dropped
 * 0x20 write_idx (68k-owned cache line)
 * 0x40 read_idx (ARM-owned cache line)
 * 0x60 events[] (8 bytes per event)
 *
 * All 32-bit fields are big-endian.
 * --------------------------------------------------------------------- */
#define XMID_FIFO_OFFSET    0x06010000UL
#define XMID_FIFO_MAGIC     0x5A4D4646UL   /* 'ZMFF' */
#define XMID_FIFO_VERSION   2u             /* FIFO layout version */
#define XMID_FIFO_SLOTS     1024u
#define XMID_FIFO_EVENTS_OFF 0x60u         /* events[] start offset */

/* Header field byte offsets (relative to XMID_FIFO_OFFSET) */
#define XMID_FIFO_OFF_MAGIC      0x00u
#define XMID_FIFO_OFF_VERSION    0x04u
#define XMID_FIFO_OFF_SIZE       0x08u
#define XMID_FIFO_OFF_DROPPED    0x0Cu
#define XMID_FIFO_OFF_WRITE_IDX  0x20u     /* own cache line */
#define XMID_FIFO_OFF_READ_IDX   0x40u     /* own cache line */
/* total span: 0x60 + 1024*8 = 0x2060 (~8KB), fits before PCM ring */
#define XMID_FIFO_SPAN      (XMID_FIFO_EVENTS_OFF + XMID_FIFO_SLOTS * 8u)

/* Max events processed per drain pass: bounds the loop so a corrupt
 * write/read index can never spin the ARM into a watchdog reset. */
#define XMID_FIFO_DRAIN_MAX  512u

/* Compile-time checks for the XACP v1.6 shared corridor and known
 * application allocations. ARM physical = 0x00200000 + fb_offset. */
#define XMID_ARM_FB_BASE           0x00200000UL
#define XMID_ZZDOOM_HEAP_END_ARM   0x06200000UL
#define XMID_ZZDOOM_SAVE_BASE_ARM  0x07E00000UL
#define XMID_ZZPICO_PRIVATE_END    0x23000000UL

typedef char xmid_v16_ctrl_fifo_packed[
    (XMID_CTRL_OFFSET + 0x10000UL == XMID_FIFO_OFFSET) ? 1 : -1];
typedef char xmid_v16_fifo_before_pcm[
    (XMID_FIFO_OFFSET + XMID_FIFO_SPAN <= XMID_PCM_RING_OFFSET) ? 1 : -1];
typedef char xmid_v16_pcm_before_upload[
    (XMID_PCM_RING_OFFSET + XMID_PCM_RING_SIZE <= XMID_UPLOAD_OFFSET) ? 1 : -1];
typedef char xmid_v16_upload_in_corridor[
    (XMID_UPLOAD_OFFSET + XMID_UPLOAD_SIZE <= XMID_SHARED_END) ? 1 : -1];
typedef char xmid_v16_corridor_after_zzdoom_heap[
    (XMID_ARM_FB_BASE + XMID_CTRL_OFFSET >= XMID_ZZDOOM_HEAP_END_ARM) ? 1 : -1];
typedef char xmid_v16_corridor_before_zzdoom_saves[
    (XMID_ARM_FB_BASE + XMID_SHARED_END <= XMID_ZZDOOM_SAVE_BASE_ARM) ? 1 : -1];

/* -----------------------------------------------------------------------
 * XACP v1.6 ARM-private allocation
 *
 * 0x20000000-0x201F0000 overlaps ZZ9000 Z3 Fast RAM
 * 0x201F0000-0x22000000 reserved guard region
 * 0x22000000-0x23000000 ZZPicoDrive/Core1 private area
 * 0x23000000-0x25000000 raw SF2 copy (32 MB reservation)
 * 0x25000000-0x25600000 raw MIDI copy (6 MB)
 * 0x25600000-0x2F600000 TSF/TML heap (160 MB)
 * 0x2F600000-0x30000000 guard region (10 MB)
 * 0x30000000-0x33000000 reserved for SMUSH codecs
 *
 * Only 0x22000000-0x30000000 is mapped as private cacheable memory.
 * --------------------------------------------------------------------- */
#define XMID_RAW_SF2_BASE_ABS      0x23000000UL  /* raw SF2 private copy */
#define XMID_SF2_POOL_SIZE         (32UL * 1024UL * 1024UL) /* 32 MB */
#define XMID_RAW_MIDI_BASE_ABS     0x25000000UL  /* raw MIDI private copy */
#define XMID_MIDI_POOL_SIZE        (6UL  * 1024UL * 1024UL) /* 6 MB */
#define XMID_HEAP_BASE_ABS         0x25600000UL  /* heap TSF/TML */
#define XMID_HEAP_SIZE             (160u * 1024u * 1024u)  /* 160 MB */
#define XMID_HEAP_END_ABS          0x2F600000UL  /* heap end / guard start */
#define XMID_PRIVATE_POOL_BASE_ABS 0x23000000UL  /* ZZMIDI ownership floor */
#define XMID_PRIVATE_POOL_END_ABS  0x30000000UL  /* pool end */

/* Compile-time safety -- XACP v1.6 private pool. */
typedef char xmid_v16_pool_above_zzpico[
    (XMID_RAW_SF2_BASE_ABS >= XMID_ZZPICO_PRIVATE_END) ? 1 : -1];
typedef char xmid_v16_sf2_before_midi_private[
    (XMID_RAW_SF2_BASE_ABS + XMID_SF2_POOL_SIZE <= XMID_RAW_MIDI_BASE_ABS) ? 1 : -1];
typedef char xmid_v16_midi_before_heap[
    (XMID_RAW_MIDI_BASE_ABS + XMID_MIDI_POOL_SIZE <= XMID_HEAP_BASE_ABS) ? 1 : -1];
typedef char xmid_v16_heap_within_guard[
    (XMID_HEAP_BASE_ABS + XMID_HEAP_SIZE <= XMID_HEAP_END_ABS) ? 1 : -1];
typedef char xmid_v16_guard_within_pool[
    (XMID_HEAP_END_ABS <= XMID_PRIVATE_POOL_END_ABS) ? 1 : -1];

/* Guard: reject any pool address below the safe base.
 * 0x20000000-0x201F0000 overlaps ZZ9000 Z3 RAM exposed to AmigaOS.
 * 0x201F0000-0x22000000 is the no-man's-land guard band. */
#define XMID_POOL_FORBIDDEN_BASE   0x20000000UL
#define XMID_POOL_SAFE_BASE        0x22000000UL

/* MMU range for the private cacheable pool. The lower Z3-visible range is
 * intentionally excluded. */
#define XACP_POOL_MMU_BASE    0x22000000UL  /* safe pool start */
#define XACP_POOL_MMU_END     0x30000000UL  /* stop before SMUSH codecs */


/* -----------------------------------------------------------------------
 * Render parameters
 *
 * XMID_RENDER_BLOCK_FRAMES controls the TSF render quantum.
 * XMID_RENDER_MAX_BLOCKS_PER_POLL bounds work performed in one idle poll.
 * XMID_TARGET_FRAMES is the target PCM ring fill for file playback.
 * --------------------------------------------------------------------- */
#define XMID_RENDER_BLOCK_FRAMES          512u
#define XMID_RENDER_MAX_BLOCKS_PER_POLL     4u
#define XMID_TARGET_FRAMES              16384u

/* Realtime mode uses a smaller PCM lookahead than file playback to reduce
 * latency for externally timed MIDI events. */
#define XMID_RT_TARGET_FRAMES            2048u
#define XMID_RT_MAX_BLOCKS_PER_POLL         1u
/* Realtime drain/render quantum. Events are applied immediately before the
 * corresponding short render interval. */
#define XMID_RT_QUANTUM_FRAMES            128u
#define XMID_RT_MAX_QUANTA_PER_POLL        16u

/* -----------------------------------------------------------------------
 * Default audio parameters
 * --------------------------------------------------------------------- */
#define XMID_DEFAULT_RATE      32000u
#define XMID_DEFAULT_CHANNELS  2u
#define XMID_DEFAULT_FORMAT    1u      /* signed 16-bit stereo interleaved */
#define XMID_DEFAULT_VOLUME    65536u  /* Q16 = 1.0 */
#define XMID_DEFAULT_VOICES    64u

/* -----------------------------------------------------------------------
 * Magic / version
 * --------------------------------------------------------------------- */
#define XMID_MAGIC    0x584D4944u  /* 'XMID' */
#define XMID_VERSION  2u   /* XACP v1.6 ABI */

/* -----------------------------------------------------------------------
 * FPU self-test level flags (debug3 bits [7:0])
 * --------------------------------------------------------------------- */
#define XMID_FPU_FLOAT_OK  0x01u
#define XMID_FPU_SINF_OK   0x02u
#define XMID_FPU_SENTINEL  0xD0BE00u
#define XMID_FPU_DONE      0xD0DE00u

/* -----------------------------------------------------------------------
 * XMID control block (big-endian in DDR, 140 bytes)
 * --------------------------------------------------------------------- */
typedef struct {
    uint32_t magic;               /* +0x00 */
    uint32_t version;             /* +0x04 */
    uint32_t struct_size;         /* +0x08 */
    uint32_t cmd_seq;             /* +0x0C Amiga increments before trigger */
    uint32_t done_seq;            /* +0x10 ARM copies cmd_seq when done */
    uint32_t subcmd;              /* +0x14 */
    uint32_t state;               /* +0x18 */
    uint32_t error;               /* +0x1C */

    uint32_t flags;               /* +0x20 reserved */
    uint32_t sample_rate;         /* +0x24 */
    uint32_t channels;            /* +0x28 */
    uint32_t pcm_format;          /* +0x2C */

    uint32_t sf2_offset;          /* +0x30 */
    uint32_t sf2_size;            /* +0x34 */
    uint32_t midi_offset;         /* +0x38 */
    uint32_t midi_size;           /* +0x3C */

    uint32_t pcm_ring_offset;     /* +0x40 */
    uint32_t pcm_ring_size_bytes; /* +0x44 */
    uint32_t pcm_write_total;     /* +0x48 [ARM writes] monotone */
    uint32_t pcm_read_total;      /* +0x4C [Amiga reads] monotone */

    uint32_t rendered_samples;    /* +0x50 */
    uint32_t play_ms;             /* +0x54 */
    uint32_t total_ms;            /* +0x58 */
    uint32_t active_voices;       /* +0x5C */

    uint32_t peak_abs;            /* +0x60 */
    uint32_t rms_last;            /* +0x64 */
    uint32_t underruns;           /* +0x68 */
    uint32_t heap_used;           /* +0x6C */
    uint32_t heap_highwater;      /* +0x70 */

    uint32_t volume_q16;          /* +0x74 */
    uint32_t max_voices;          /* +0x78 */

    uint32_t debug0;              /* +0x7C sinf bits / render_us_last */
    uint32_t debug1;              /* +0x80 sinf err / render_us_max */
    uint32_t debug2;              /* +0x84 float / ring_level_min */
    uint32_t debug3;              /* +0x88 fpu flags+sentinel */

    /* SF2 chunked-upload state. Written by ARM and read by 68k.
 * All fields are big-endian like the rest of the structure. */
    uint32_t sf2_total_size;      /* +0x8C total SF2 size announced by BEGIN */
    uint32_t sf2_uploaded_bytes;  /* +0x90 bytes copied to private pool so far */
    uint32_t sf2_chunk_offset;    /* +0x94 offset of last chunk in raw SF2 */
    uint32_t sf2_chunk_size;      /* +0x98 size of last chunk */
    uint32_t sf2_chunks_done;     /* +0x9C number of chunks processed */
    uint32_t sf2_state;           /* +0xA0 XMID_SF2_STATE_xxx */
    uint32_t sf2_progress_pct;    /* +0xA4 0-100 */
    /* Realtime batch counters; appended without changing prior offsets. */
    uint32_t arm_last_batch_count;/* +0xA8 */
    uint32_t arm_note_on_count;   /* +0xAC */
    uint32_t arm_note_off_count;  /* +0xB0 */
    uint32_t arm_pc_count;        /* +0xB4 */
    uint32_t arm_cc_count;        /* +0xB8 */
    uint32_t arm_pb_count;        /* +0xBC */
    uint32_t arm_ignored_count;   /* +0xC0 */
    uint32_t arm_last_event0;     /* +0xC4 */
    uint32_t arm_last_event1;     /* +0xC8 */
    uint32_t arm_last_event2;     /* +0xCC */
} XMID_Ctrl;                      /* 0xD0 = 208 bytes */

/* -----------------------------------------------------------------------
 * Control block field offsets (Amiga side)
 * --------------------------------------------------------------------- */
#define XMID_OFF_MAGIC              0x00u
#define XMID_OFF_VERSION            0x04u
#define XMID_OFF_STRUCT_SIZE        0x08u
#define XMID_OFF_CMD_SEQ            0x0Cu
#define XMID_OFF_DONE_SEQ           0x10u
#define XMID_OFF_SUBCMD             0x14u
#define XMID_OFF_STATE              0x18u
#define XMID_OFF_ERROR              0x1Cu
#define XMID_OFF_FLAGS              0x20u
#define XMID_OFF_SAMPLE_RATE        0x24u
#define XMID_OFF_CHANNELS           0x28u
#define XMID_OFF_PCM_FORMAT         0x2Cu
#define XMID_OFF_SF2_OFFSET         0x30u
#define XMID_OFF_SF2_SIZE           0x34u
#define XMID_OFF_MIDI_OFFSET        0x38u
#define XMID_OFF_MIDI_SIZE          0x3Cu
#define XMID_OFF_PCM_RING_OFFSET    0x40u
#define XMID_OFF_PCM_RING_SIZE      0x44u
#define XMID_OFF_PCM_WRITE_TOTAL    0x48u
#define XMID_OFF_PCM_READ_TOTAL     0x4Cu
#define XMID_OFF_RENDERED_SAMPLES   0x50u
#define XMID_OFF_PLAY_MS            0x54u
#define XMID_OFF_TOTAL_MS           0x58u
#define XMID_OFF_ACTIVE_VOICES      0x5Cu
#define XMID_OFF_PEAK_ABS           0x60u
#define XMID_OFF_RMS_LAST           0x64u
#define XMID_OFF_UNDERRUNS          0x68u
#define XMID_OFF_HEAP_USED          0x6Cu
#define XMID_OFF_HEAP_HIGHWATER     0x70u
#define XMID_OFF_VOLUME_Q16         0x74u
#define XMID_OFF_MAX_VOICES         0x78u
#define XMID_OFF_DEBUG0             0x7Cu
#define XMID_OFF_DEBUG1             0x80u
#define XMID_OFF_DEBUG2             0x84u
#define XMID_OFF_DEBUG3             0x88u
/* SF2 upload progress, read-only from the Amiga side. */
#define XMID_OFF_SF2_TOTAL_SIZE     0x8Cu
#define XMID_OFF_SF2_UPLOADED_BYTES 0x90u
#define XMID_OFF_SF2_CHUNK_OFFSET   0x94u
#define XMID_OFF_SF2_CHUNK_SIZE     0x98u
#define XMID_OFF_SF2_CHUNKS_DONE    0x9Cu
#define XMID_OFF_SF2_STATE          0xA0u
#define XMID_OFF_SF2_PROGRESS_PCT   0xA4u

/* -----------------------------------------------------------------------
 * Heap API (ARM firmware side only)
 * --------------------------------------------------------------------- */
void     xmid_reset_heap(void);
void    *xmid_malloc(size_t n);
void    *xmid_realloc(void *p, size_t n);
void     xmid_free(void *p);
uint32_t xmid_heap_used(void);
uint32_t xmid_heap_highwater(void);
int      xmid_heap_did_overflow(void);
void     xmid_heap_set_base(void *fb_base);

/* -----------------------------------------------------------------------
 * Entry points called from main.c
 * --------------------------------------------------------------------- */
void xmid_handle_opcode(void *framebuffer_base);
void xmid_idle_poll(void *framebuffer_base);

#endif /* ZZ_MIDI_SF2_H */
