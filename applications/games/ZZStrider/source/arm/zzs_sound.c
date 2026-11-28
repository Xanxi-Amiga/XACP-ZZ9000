/*
 * zzs_sound.c - CPS-1 sound board for ZZStrider: Z80 + YM2151 + MSM6295.
 *
 * Hardware (MAME cps1.cpp, sub_map / cps1_10MHz, "verified on pcb"):
 *   Z80 @ 3.579545 MHz
 *   YM2151 @ 3.579545 MHz, IRQ -> Z80 INT, both outputs to mono at 0.35
 *   MSM6295 @ 1 MHz, pin 7 high (7575.76 Hz), to mono at 0.30
 *   0000-7FFF ROM            8000-BFFF banked ROM (2 banks of 0x4000 at file offset 0x8000)
 *   D000-D7FF RAM            F000/F001 YM2151       F002 MSM6295
 *   F004 bank select (bit 0) F006 MSM6295 pin 7 (bit 0)
 *   F008 sound command latch F00A sound fade latch  (written by the 68000 at 800181 / 800189)
 *
 * Infrastructure kept from zzrastan_sound.c: CZ80 with PicoDrive's direct read/write maps and
 * fetch table, the Jarek YM2151 backend as single authority for timers/status/IRQ, the
 * level-sensitive IRQ line pushed to CZ80 the instant it changes, the FM FIFO with "always
 * generate, drop the oldest on overflow", the discard-while-PCM-is-off rule, 64-bit profiling.
 *
 * What is different from ZZRastan:
 *   - no PC060HA: the CPS-1 latches are plain bytes with no handshake and no NMI;
 *   - MSM6295 instead of MSM5205;
 *   - scheduling: instead of ten fixed Z80 slices per frame, zzs_snd_sync(target) runs the Z80
 *     up to an ABSOLUTE clock, stopping at every YM2151 timer overflow so that the timer IRQ
 *     reaches the Z80 on the right cycle. It is called after each 68000 slice and, as in
 *     FB Alpha (PsndSyncZ80), just before every sound latch write.
 *
 * ASCII only.
 */
#include "zzs.h"
#include "zzs_game.h"
#include "zzs_msm6295.h"
#include "cz80.h"

/* YM2151 backend (zzs_ym_jarek.c) */
extern void zzr_jarek_reset(void);
extern void zzr_jarek_write_addr(unsigned char v);
extern void zzr_jarek_write_data(unsigned char v);
extern void zzr_jarek_generate(short *dst, unsigned int count);
extern int  zzr_jarek_advance(unsigned int clocks);
extern unsigned char zzr_jarek_status(void);
extern unsigned int zzr_jarek_stat(unsigned int i);
extern unsigned int zzr_jarek_clocks_to_next(void);

/* sound latches, owned by the 68000 side (zzs_cps_run.cpp, written by cps1/cps_rw.cpp) */
extern unsigned char PsndCode, PsndFade;

/* PicoDrive's CZ80 build resolves memory through global page tables instead of the callbacks.
   Each entry is (ptr >> 1); MAP_FLAG marks a handler. 1 KiB pages. */
#define ZZS_Z80_MEM_SHIFT 10
#define ZZS_Z80_PAGES     (0x10000 >> ZZS_Z80_MEM_SHIFT)
#define ZZS_MAP_FLAG      ((uptr)1 << (sizeof(uptr) * 8 - 1))
uptr z80_read_map [ZZS_Z80_PAGES];
uptr z80_write_map[ZZS_Z80_PAGES];

#define ZZS_OUT_RATE      ZZS_PCM_RATE
#define ZZS_BLOCK         64u            /* samples generated per call at most */
#define ZZS_MAX_CHUNK     4096u          /* Z80 cycles per step at most (~25 samples) */

static cz80_struc g_z80;
static zzs_u8  g_z80_ram[0x800];         /* D000-D7FF */
static const zzs_u8 *g_z80_rom;
static zzs_u32 g_bank;                   /* 0 or 1 */
static int     g_snd_ready;
static zzs_u64 g_z80_clock;              /* absolute Z80 clock reached */
static zzs_u32 g_out_acc;                /* fractional sample accumulator */

static zzs_u8  g_ym_reg;
static int     g_ym_irq;
static int     g_oki_pin7;

zzs_s16 g_fm_fifo[ZZS_FM_FIFO_SAMPLES];
zzs_u32 g_fm_wr, g_fm_rd;
static int g_pcm_was_on;

/* statistics */
static zzs_u32 g_ym_writes, g_ym_irqs, g_ym_keyon, g_bank_changes, g_latch_reads, g_z80_resets;
static zzs_u32 g_samples, g_peak, g_aq_drop_old, g_aq_highwater, g_aq_discarded;
zzs_u32 g_zzs_snd_pmu_nested;            /* running PMU total, read by zzs_cps_run.cpp */
static zzs_u64 g_prof_z80, g_prof_ym, g_prof_oki, g_prof_total;

/* ------------------------------------------------------------------------------------------ */
/* Called by the Jarek IRQ handler - the core is the single authority for the YM IRQ line.
   The Z80's IM1 handler acknowledges the timer in the middle of Cz80_Exec(); the line must
   follow at once or EI would re-enter the handler on a stale level (ZZRastan finding). */
void zzr_z80_set_irq(int level)
{
    if (level != g_ym_irq) {
        g_ym_irq = level;
        Cz80_Set_IRQ(&g_z80, 0, level ? 1 : 0);
        if (level) g_ym_irqs++;
    }
}

static void bank_map(void)
{
    /* MAME: configure_entries(0, 2, base + 0x10000, 0x4000) with the second half of the file
       loaded at 0x10000, i.e. bank n = file offset 0x8000 + n * 0x4000. */
    const zzs_u8 *bank = g_z80_rom + 0x8000u + g_bank * 0x4000u;
    zzs_u32 i;
    Cz80_Set_Fetch(&g_z80, 0x8000, 0xBFFF, (FPTR)bank);
    for (i = (0x8000u >> ZZS_Z80_MEM_SHIFT); i < (0xC000u >> ZZS_Z80_MEM_SHIFT); i++)
        z80_read_map[i] = ((uptr)(bank - 0x8000u) >> 1);
}

static UINT8 z80_read(UINT32 a)
{
    a &= 0xFFFFu;
    switch (a) {
    case 0xF000u: return 0xFFu;                  /* YM2151 data port reads back nothing */
    case 0xF001u: return zzr_jarek_status();
    case 0xF002u: return zzs_oki_read();
    case 0xF008u: g_latch_reads++; return PsndCode;
    case 0xF00Au: return PsndFade;
    default: break;
    }
    return 0u;
}

static void z80_write(UINT32 a, UINT8 d)
{
    a &= 0xFFFFu;
    switch (a) {
    case 0xF000u:
        g_ym_reg = d;
        zzr_jarek_write_addr(d);
        break;
    case 0xF001u:
        g_ym_writes++;
        if (g_ym_reg == 0x08u && (d & 0x78u) != 0u) g_ym_keyon++;
        zzr_jarek_write_data(d);                 /* may raise or clear the IRQ line at once */
        break;
    case 0xF002u:
        zzs_oki_write(d);
        break;
    case 0xF004u:
        {
            zzs_u32 nb = (zzs_u32)(d & 1u);
            if (nb != g_bank) {
                g_bank = nb;
                g_bank_changes++;
                bank_map();
            }
        }
        break;
    case 0xF006u:
        g_oki_pin7 = (d & 1u) ? 1 : 0;
        zzs_oki_set_pin7(g_oki_pin7);
        break;
    default:
        break;                                   /* ROM and unmapped space ignore writes */
    }
}

static void z80_hard_reset(void)
{
    Cz80_Reset(&g_z80);
    Cz80_Set_Reg(&g_z80, CZ80_SP, 0xFFFF);       /* as PicoDrive and ZZRastan do */
    g_ym_irq = 0;
    Cz80_Set_IRQ(&g_z80, 0, 0);
}

int zzs_snd_init(void)
{
    zzs_u32 i;

    g_snd_ready = 0;
    g_z80_rom = g_zzs_z80;
    for (i = 0; i < sizeof(g_z80_ram); i++) g_z80_ram[i] = 0;
    g_bank = 0;
    g_z80_clock = 0;
    g_out_acc = 0;
    g_ym_reg = 0;
    g_ym_irq = 0;
    g_oki_pin7 = ZZS_OKI_PIN7_HIGH;
    g_fm_wr = g_fm_rd = 0;
    g_pcm_was_on = 0;
    g_ym_writes = g_ym_irqs = g_ym_keyon = g_bank_changes = g_latch_reads = g_z80_resets = 0;
    g_samples = g_peak = g_aq_drop_old = g_aq_highwater = g_aq_discarded = 0;
    g_prof_z80 = g_prof_ym = g_prof_oki = g_prof_total = 0;

    /* DIRECT maps for plain memory, handlers only where they are needed. CZ80 decodes a
       non-flagged entry as (value << 1) + address, so we store (base - page_base) >> 1. */
    for (i = 0; i < ZZS_Z80_PAGES; i++) {
        zzs_u32 page = i << ZZS_Z80_MEM_SHIFT;
        if (page < 0x8000u)
            z80_read_map[i] = ((uptr)(g_z80_rom) >> 1);
        else if (page < 0xC000u)
            z80_read_map[i] = ((uptr)(g_z80_rom + 0x8000u + g_bank * 0x4000u - 0x8000u) >> 1);
        else if (page >= 0xD000u && page < 0xD800u)
            z80_read_map[i] = (((uptr)g_z80_ram - 0xD000u) >> 1);
        else
            z80_read_map[i] = ((uptr)z80_read >> 1) | ZZS_MAP_FLAG;

        if (page >= 0xD000u && page < 0xD800u)
            z80_write_map[i] = (((uptr)g_z80_ram - 0xD000u) >> 1);
        else
            z80_write_map[i] = ((uptr)z80_write >> 1) | ZZS_MAP_FLAG;
    }

    zzr_jarek_reset();
    zzs_oki_init(g_zzs_oki, ZZS_OKI_SIZE, ZZS_OKI_CLOCK, g_oki_pin7, ZZS_OUT_RATE);

    Cz80_Init(&g_z80);
    Cz80_Set_ReadB(&g_z80, z80_read);
    Cz80_Set_WriteB(&g_z80, z80_write);

    /* CZ80 has a SECOND table for OPCODE FETCH, separate from z80_read_map. Cz80_Init() fills
       it with 0xFF = RST 38h; every executable area must be set explicitly (ZZRastan finding).
       The areas FB Alpha maps "in case it tries to fetch" elsewhere keep CZ80's RST 38h. */
    Cz80_Set_Fetch(&g_z80, 0x0000, 0x7FFF, (FPTR)g_z80_rom);
    bank_map();
    Cz80_Set_Fetch(&g_z80, 0xD000, 0xD7FF, (FPTR)g_z80_ram);

    z80_hard_reset();
    g_snd_ready = 1;
    return 0;
}

void zzs_snd_reset_z80(void)
{
    if (!g_snd_ready) return;
    z80_hard_reset();
    g_z80_resets++;
}

/* Generate the audio that belongs to 'clocks' of emulated chip time. Generation is
   UNCONDITIONAL (phases, envelopes and ADPCM positions must advance with emulated time); what
   happens to the samples depends on whether the PCM output is up. */
/* Production rate control. The game is paced by the CPU clock, AHI plays at the rate of its
   own hardware: the two differ by a fraction of a percent (measured on the A4000: about
   35 samples per second too many). Instead of throwing queued samples away, the number of
   samples produced per emulated second follows the queue level, within +/- 1.5 %: pitch is
   unchanged (the chips are still rendered at 22050 Hz), only the chip time drifts by that
   fraction against the game. Called once per frame, after the frame's samples are queued. */
/* A2c: one AHI buffer (512) in the ring + one frame of production (370, made in a burst during
   the emulation) + margin = 1024 samples, 46 ms (A2b: 1412, 64 ms). */
#define ZZS_SND_CUSHION 512u                 /* samples kept in the FIFO */
#define ZZS_SND_TARGET  1024u
#define ZZS_SND_TRIM_ABOVE (ZZS_SND_TARGET + 1024u)   /* beyond this, cut the excess at once */
#define ZZS_SND_RATE_SPAN 330                /* +/- 1.5 % of 22050 */
static zzs_u32 g_out_rate = ZZS_OUT_RATE;
static int     g_backlog_f = (int)ZZS_SND_TARGET;
static zzs_u32 g_rate_min = ZZS_OUT_RATE, g_rate_max = ZZS_OUT_RATE;
static zzs_u32 g_backlog_min = 0xFFFFFFFFu, g_backlog_max, g_maxbl_frame, g_first_drop, g_trims, g_prestart;

void zzs_snd_push_silence(zzs_u32 n)
{
    zzs_u32 used = (g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u);
    if (!g_pcm_was_on || used >= ZZS_SND_TARGET) return;       /* keep the queue at its level */
    if (n > ZZS_SND_TARGET - used) n = ZZS_SND_TARGET - used;
    while (n--) { g_fm_fifo[g_fm_wr] = 0; g_fm_wr = (g_fm_wr + 1u) & (ZZS_FM_FIFO_SAMPLES - 1u); }
    zzs_audio_pump();
}

void zzs_snd_rate_control(void)
{
    zzs_u32 backlog;
    int adj;

    if (!g_pcm_was_on) { g_out_rate = ZZS_OUT_RATE; return; }
    backlog = ((g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u)) + zzs_audio_ring_used() / 2u;
    g_backlog_f += ((int)backlog - g_backlog_f) / 8;         /* smooth the 512-sample AHI steps */
    if (zzs_audio_ahi_started() && g_backlog_f > (int)ZZS_SND_TRIM_ABOVE) {
        /* an abnormal excess (the 68k could not play for a while): cut it in one go instead of
           playing ~200 ms late for ten seconds; one audible jump, counted */
        zzs_u32 fifo = (g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u);
        zzs_u32 cut = (zzs_u32)g_backlog_f - ZZS_SND_TARGET;
        if (cut > fifo) cut = fifo;
        g_fm_rd = (g_fm_rd + cut) & (ZZS_FM_FIFO_SAMPLES - 1u);
        g_backlog_f -= (int)cut;
        g_trims++;
    }
    adj = ((int)ZZS_SND_TARGET - g_backlog_f) / 4;
    if (adj > ZZS_SND_RATE_SPAN) adj = ZZS_SND_RATE_SPAN;
    if (adj < -ZZS_SND_RATE_SPAN) adj = -ZZS_SND_RATE_SPAN;
    g_out_rate = (zzs_u32)((int)ZZS_OUT_RATE + adj);
    if (g_out_rate < g_rate_min) g_rate_min = g_out_rate;
    if (g_out_rate > g_rate_max) g_rate_max = g_out_rate;
    if (backlog < g_backlog_min) g_backlog_min = backlog;
    if (backlog > g_backlog_max) { g_backlog_max = backlog; g_maxbl_frame = g_zzs_frame_no; }
}

static void generate_for(zzs_u32 clocks)
{
    zzs_u32 n, acc;

    /* clocks <= ZZS_MAX_CHUNK + one instruction, so clocks * g_out_rate fits 32 bits with room */
    acc = g_out_acc + clocks * g_out_rate;
    n = acc / ZZS_YM_CLOCK;
    g_out_acc = acc - n * ZZS_YM_CLOCK;

    while (n != 0u) {
        zzs_s16 ym[ZZS_BLOCK];
        int     oki[ZZS_BLOCK];
        zzs_u32 k = (n > ZZS_BLOCK) ? ZZS_BLOCK : n, i, used, free_sp, t0, t1;

        t0 = zzs_pmu_cycles();
        zzr_jarek_generate(ym, k);               /* (L + R) / 2 at the output rate */
        t1 = zzs_pmu_cycles();
        g_prof_ym += (zzs_u64)(t1 - t0);
        zzs_oki_render(oki, k);
        g_prof_oki += (zzs_u64)(zzs_pmu_cycles() - t1);

        /* MAME routes: YM2151 0.35 per output into mono, MSM6295 0.30.
             YM  : 0.35 * (L + R)       = 0.70 * ym[i]
             OKI : 0.30 * mix / 65536 full scale, mix = signal12 * vol32 -> 0.30 * mix / 2 */
        for (i = 0; i < k; i++) {
            int v = ((int)ym[i] * 70) / 100 + (oki[i] * 15) / 100;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            ym[i] = (zzs_s16)v;
            { zzs_u32 a = (zzs_u32)(v < 0 ? -v : v); if (a > g_peak) g_peak = a; }
        }
        g_samples += k;
        n -= k;

        if (!ZZS_SH(ZZS_SH_PCM_ENABLE)) {
            g_aq_discarded += k;                 /* advance the chips, keep no backlog */
            g_pcm_was_on = 0;
            continue;
        }
        if (!g_pcm_was_on) {                     /* PCM 0 -> 1: start a clean window */
            zzs_u32 z;
            g_pcm_was_on = 1;
            /* Start with the cushion already queued (silence) so that the first AHI pulls
               find a full buffer; the rate control then holds that level. */
            for (z = 0; z < ZZS_SND_CUSHION; z++) g_fm_fifo[z] = 0;
            g_fm_rd = 0;
            g_fm_wr = ZZS_SND_CUSHION;
            g_backlog_f = (int)ZZS_SND_TARGET;
            g_out_rate = ZZS_OUT_RATE;
            ZZS_SH(ZZS_SH_PCM_WRITE) = ZZS_SH(ZZS_SH_PCM_READ);
        }
        used = (g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u);
        if (!zzs_audio_ahi_started() && used + k > ZZS_SND_CUSHION) {
            /* AHI not playing yet: keep only the cushion, so that playback starts in step */
            zzs_u32 drop = used + k - ZZS_SND_CUSHION;
            if (drop > used) drop = used;
            g_fm_rd = (g_fm_rd + drop) & (ZZS_FM_FIFO_SAMPLES - 1u);
            g_prestart += drop;
            used -= drop;
        }
        free_sp = (ZZS_FM_FIFO_SAMPLES - 1u) - used;
        if (free_sp < k) {                       /* overflow: drop the OLDEST queued audio */
            if (!g_first_drop) g_first_drop = g_zzs_frame_no ? g_zzs_frame_no : 1u;
            zzs_u32 drop = k - free_sp;
            g_fm_rd = (g_fm_rd + drop) & (ZZS_FM_FIFO_SAMPLES - 1u);
            g_aq_drop_old += drop;
        }
        for (i = 0; i < k; i++) {
            g_fm_fifo[g_fm_wr] = ym[i];
            g_fm_wr = (g_fm_wr + 1u) & (ZZS_FM_FIFO_SAMPLES - 1u);
        }
        used = (g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u);
        if (used > g_aq_highwater) g_aq_highwater = used;
    }
}

void zzs_snd_sync(zzs_u64 target)
{
    zzs_u32 t_in;

    if (!g_snd_ready) return;
    if (target <= g_z80_clock) return;
    t_in = zzs_pmu_cycles();

    while (g_z80_clock < target) {
        zzs_u64 left = target - g_z80_clock;
        zzs_u32 chunk = (left > ZZS_MAX_CHUNK) ? ZZS_MAX_CHUNK : (zzs_u32)left;
        zzs_u32 to_timer = zzr_jarek_clocks_to_next();
        zzs_u32 t0;
        INT32   ran;

        if (to_timer != 0u && chunk > to_timer)
            chunk = to_timer;                    /* stop exactly where the timer overflows */

        t0 = zzs_pmu_cycles();
        ran = Cz80_Exec(&g_z80, (INT32)chunk);   /* may overshoot by one instruction */
        g_prof_z80 += (zzs_u64)(zzs_pmu_cycles() - t0);
        if (ran <= 0) ran = (INT32)chunk;

        g_z80_clock += (zzs_u64)(zzs_u32)ran;
        zzr_jarek_advance((zzs_u32)ran);         /* fires timers, drives the IRQ line */
        generate_for((zzs_u32)ran);
    }

    {
        zzs_u32 d = zzs_pmu_cycles() - t_in;
        g_zzs_snd_pmu_nested += d;
        g_prof_total += (zzs_u64)d;
    }
}

void zzs_snd_write_latch(int which, zzs_u8 value)
{
    if (which == 0) PsndCode = value; else PsndFade = value;
}

void zzs_snd_publish(void)
{
    zzs_u32 f = ZZS_SH(ZZS_SH_PROF_FRAMES);
    if (!g_snd_ready) return;
    if (f == 0u) f = 1u;

    ZZS_SH(ZZS_SH_Z80_PC) = Cz80_Get_Reg(&g_z80, CZ80_PC);
    ZZS_SH(ZZS_SH_Z80_CYCLES) = (zzs_u32)(g_z80_clock & 0xFFFFFFFFu);
    ZZS_SH(ZZS_SH_YM_WRITES) = g_ym_writes;
    ZZS_SH(ZZS_SH_YM_IRQS) = g_ym_irqs;
    ZZS_SH(ZZS_SH_YM_TA_FIRES) = zzr_jarek_stat(2);
    ZZS_SH(ZZS_SH_YM_TB_FIRES) = zzr_jarek_stat(3);
    ZZS_SH(ZZS_SH_YM_KEYON) = g_ym_keyon;
    ZZS_SH(ZZS_SH_OKI_WRITES) = g_oki_writes;
    ZZS_SH(ZZS_SH_OKI_STARTS) = g_oki_starts;
    ZZS_SH(ZZS_SH_OKI_STATUS) = zzs_oki_read();
    ZZS_SH(ZZS_SH_OKI_PIN7) = (zzs_u32)g_oki_pin7;
    ZZS_SH(ZZS_SH_Z80_BANK) = g_bank;
    ZZS_SH(ZZS_SH_Z80_BANK_CHANGES) = g_bank_changes;
    ZZS_SH(ZZS_SH_Z80_LATCH_READS) = g_latch_reads;
    ZZS_SH(ZZS_SH_Z80_RESETS) = g_z80_resets;
    ZZS_SH(ZZS_SH_SND_SAMPLES) = g_samples;
    ZZS_SH(ZZS_SH_SND_PEAK) = g_peak;
    ZZS_SH(ZZS_SH_AQ_DROP_OLD) = g_aq_drop_old;
    ZZS_SH(ZZS_SH_AQ_HIGHWATER) = g_aq_highwater;
    ZZS_SH(ZZS_SH_AQ_FIFO_USED) = (g_fm_wr - g_fm_rd) & (ZZS_FM_FIFO_SAMPLES - 1u);
    ZZS_SH(ZZS_SH_AQ_DISCARDED) = g_aq_discarded;
    ZZS_SH(ZZS_SH_SND_RATE_HZ) = g_out_rate;
    ZZS_SH(ZZS_SH_SND_RATE_MIN) = g_rate_min;
    ZZS_SH(ZZS_SH_SND_RATE_MAX) = g_rate_max;
    ZZS_SH(ZZS_SH_SND_BACKLOG) = (zzs_u32)g_backlog_f;
    ZZS_SH(ZZS_SH_SND_BACKLOG_MIN) = (g_backlog_min == 0xFFFFFFFFu) ? 0u : g_backlog_min;
    ZZS_SH(ZZS_SH_SND_BACKLOG_MAX) = g_backlog_max;
    ZZS_SH(ZZS_SH_SND_MAXBL_FRAME) = g_maxbl_frame;
    ZZS_SH(ZZS_SH_AQ_FIRST_DROP) = g_first_drop;
    ZZS_SH(ZZS_SH_AQ_TRIMS) = g_trims;
    ZZS_SH(ZZS_SH_AQ_PRESTART) = g_prestart;
    /* 64-bit sums -> microseconds per frame, so the 32-bit slots cannot overflow */
    ZZS_SH(ZZS_SH_PROF_Z80_US)   = (zzs_u32)(g_prof_z80   / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_YM_US)    = (zzs_u32)(g_prof_ym    / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_OKI_US)   = (zzs_u32)(g_prof_oki   / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_SOUND_US) = (zzs_u32)(g_prof_total / ZZS_PMU_PER_US / f);
}
