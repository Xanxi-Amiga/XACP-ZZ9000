#ifndef ZZS_H
#define ZZS_H
/*
 * zzs.h - internal interfaces of the ZZStrider Core1 blob. ASCII only.
 *
 * The blob is three layers:
 *   1. platform  : zzs_entry.S, zzs_vectors.S, zzs_mmu.c, zzs_platform.c, zzs_main.c, zzs_video.c,
 *                  zzs_audio.c - the ZZRastan 1.0 Core1 infrastructure, re-pointed.
 *   2. machine   : zzs_burn.cpp (68000 bus on Cyclone), zzs_cps_load.cpp, zzs_cps_run.cpp,
 *                  zzs_sound.c (Z80 + YM2151 + MSM6295), zzs_rom.c - the CPS-1 board.
 *   3. cps1/     : the CPS-1 video/IO engine from FB Alpha 2012 (see cps1/README.md).
 * Game specifics (ROM table, CPS-B id, mapper, clocks, stars) are confined to zzs_game.h.
 */
#include <stddef.h>
#include "zzs_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  zzs_u8;
typedef unsigned short zzs_u16;
typedef signed short   zzs_s16;
typedef unsigned int   zzs_u32;
typedef signed int     zzs_s32;
typedef unsigned long long zzs_u64;

#define ZZS_SH(i) (((volatile zzs_u32 *)ZZS_ARM_SHARED)[(i)])

/* --- platform ------------------------------------------------------------------------------ */
void    zzs_pmu_enable(void);                        /* zzs_mmu.c */
#define ZZS_PMU_NEV 6            /* instructions, branch misses, L1D refills, L1I refills, D stall, I stall */
void    zzs_pmu_events(zzs_u32 *v);                  /* reads the ZZS_PMU_NEV event counters */
zzs_u32 zzs_mmu_par(zzs_u32 addr);                   /* hardware translation of addr (PAR) */
zzs_u32 zzs_l2_enabled(void);
extern int g_zzs_l2_fix;
zzs_u32 zzs_actlr_one_way(int on);                   /* ACTLR bit 8, returns the new ACTLR */
void    zzs_snd_rate_control(void);                  /* once per frame, zzs_sound.c */
void    zzs_snd_push_silence(zzs_u32 n);             /* queue n silent samples (pause) */
zzs_u32 zzs_audio_ring_used(void);                   /* bytes queued in the PCM ring, zzs_audio.c */
int     zzs_audio_ahi_started(void);                 /* AHI has read the ring since PCM_ENABLE */
void    zzs_vlock_poll(void);                        /* zzs_main.c: watch the PAN acknowledges */
extern zzs_u32 g_zzs_frame_no;                       /* zzs_main.c: frames run since START */
zzs_u32 zzs_pmu_cycles(void);
void    mmu_init(void);
void    mmu_set_wbwa(zzs_u32 addr, zzs_u32 len);
zzs_u32 mmu_get_desc(zzs_u32 addr);
void    dcache_clean_range(void *addr, zzs_u32 len);
void    dcache_inval_range(void *addr, zzs_u32 len);
void    zz_install_vectors(void);
void    zzs_fatal(zzs_u32 error, zzs_u32 aux);       /* STATUS = ERROR, keeps serving commands */

#define ZZS_PMU_HZ            666666687u
#define ZZS_PMU_PER_US        666u

/* --- ROM store (zzs_rom.c) ------------------------------------------------------------------- */
void zzs_rom_begin(void);
int  zzs_rom_chunk(zzs_u32 file, zzs_u32 offset, zzs_u32 length);
int  zzs_rom_commit(void);
int  zzs_rom_ready(void);
zzs_u32 zzs_crc32_update(zzs_u32 crc, const zzs_u8 *p, zzs_u32 n);

/* Decoded ROM store, laid out as FB Alpha's CpsInit() does (gfx first, then the rest). */
extern zzs_u8 *g_zzs_gfx;      /* tiles, FB Alpha packed format, + star bytes at the end */
extern zzs_u8 *g_zzs_prg;      /* 68000 program, host-order words */
extern zzs_u8 *g_zzs_z80;      /* Z80 program */
extern zzs_u8 *g_zzs_oki;      /* MSM6295 samples */

/* --- machine (C++ side, zzs_cps_run.cpp) ----------------------------------------------------- */
int  zzs_machine_init(void);        /* after a successful ROM commit; 0 = ok */
int  zzs_machine_reset(void);       /* 68000 + Z80 reset; 0 = ok */
void zzs_machine_frame(void);       /* one emulated frame, vblank to vblank */
void zzs_machine_draw(zzs_u16 *index_fb);   /* compose the latched state into INDEX16 */
const zzs_u32 *zzs_machine_palette(void);   /* 0xC00 entries, 0xFFRRGGBB */
void zzs_machine_publish(void);     /* CPU / CPS register diagnostics into the shared slots */
zzs_u32 zzs_machine_take_obj_cycles(void);  /* PMU cycles spent on sprites since last call */
zzs_u32 zzs_machine_take_pal_cycles(void);
zzs_u32 zzs_machine_take_m68k_cycles_pmu(void);

/* --- sound board (zzs_sound.c) --------------------------------------------------------------- */
int  zzs_snd_init(void);
void zzs_snd_reset_z80(void);                 /* 68000 RESET instruction */
void zzs_snd_sync(zzs_u64 z80_clock_target);  /* run the board up to this absolute Z80 clock */
void zzs_snd_write_latch(int which, zzs_u8 value);   /* 0 = command, 1 = fade timer */
void zzs_snd_publish(void);
void zzs_audio_reset(void);
void zzs_audio_pump(void);

#define ZZS_FM_FIFO_SAMPLES 4096
extern zzs_s16 g_fm_fifo[ZZS_FM_FIFO_SAMPLES];
extern zzs_u32 g_fm_wr, g_fm_rd;

/* --- video (zzs_video.c) --------------------------------------------------------------------- */
void zzs_video_reset(void);
void zzs_video_present(void);

#ifdef __cplusplus
}
#endif

#endif /* ZZS_H */
