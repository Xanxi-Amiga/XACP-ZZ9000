/*
 * zzs_main.c - ZZStrider Core1 main loop.
 *
 * Kept from zzrastan_main.c (ZZRastan 1.0): boot order (zero BSS, MMU, shared defaults, vectors,
 * PMU), the command mailbox (cmd_seq / ack_seq, no WFE while polling), the heartbeat, the
 * PMU-clocked pacer with catch-up bounded to one frame, pause, the 64-bit profiling sums, and
 * the terminal state STATUS = STOPPED followed by WFE for ever.
 *
 * Changed: the frame period is the CPS-1 one (59.637 Hz), ROMs arrive in chunks, and a failure
 * leaves the mailbox alive so that the launcher can read the diagnostics and send STOP.
 *
 * ASCII only.
 */
#include "zzs.h"
#include "zzs_game.h"

#define ZZS_BUILD_ID 0x20261126u   /* release 1.0 */

extern char _bss_start, _bss_end;
extern char _zzs_stack_bottom, _zzs_stack_top;
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

static int     g_running;
static int     g_machine_inited;
static zzs_u32 g_last_cmd_seq;

static zzs_u32 g_next_deadline, g_run_t0;
static zzs_u32 g_pace_waits, g_pace_catchup, g_pace_hard, g_pace_max_late;
static zzs_u64 g_pf_work, g_pf_pace, g_pf_cyc, g_pf_pal;
static zzs_u32 g_pf_frames, g_pf_work_max;

#define STACK_FILL 0xA5A5A5A5u

/* ------------------------------------------------------------------------------------------ */
static void zero_bss(void)
{
    zzs_u32 *p = (zzs_u32 *)(void *)&_bss_start;
    zzs_u32 *e = (zzs_u32 *)(void *)&_bss_end;
    while (p < e)
        *p++ = 0u;
}

static void run_init_array(void)
{
    void (**f)(void);
    for (f = __init_array_start; f != __init_array_end; f++)
        if (*f) (*f)();
}

/* Paint the unused part of the private stack so that its high-water mark can be measured. */
static void stack_paint(void)
{
    zzs_u32 *p = (zzs_u32 *)(void *)&_zzs_stack_bottom;
    zzs_u32 *e;
    zzs_u32 sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    e = (zzs_u32 *)((sp - 1024u) & ~3u);
    while (p < e)
        *p++ = STACK_FILL;
}

static void stack_measure(void)
{
    const zzs_u32 *p = (const zzs_u32 *)(const void *)&_zzs_stack_bottom;
    const zzs_u32 *e = (const zzs_u32 *)(const void *)&_zzs_stack_top;
    zzs_u32 n = 0;
    while (p < e && *p == STACK_FILL) { p++; n += 4u; }
    ZZS_SH(ZZS_SH_STACK_FREE_MIN) = n;
}

static void shared_defaults(void)
{
    zzs_u32 i;
    for (i = 0; i < ZZS_SH_SLOT_COUNT; i++)
        ZZS_SH(i) = 0u;

    ZZS_SH(ZZS_SH_MAGIC) = ZZS_MAGIC;
    ZZS_SH(ZZS_SH_VERSION) = ZZS_VERSION;
    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_BOOTING;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_BOOT;
    ZZS_SH(ZZS_SH_BUILD_ID) = ZZS_BUILD_ID;
    ZZS_SH(ZZS_SH_DIP_A) = ZZS_DIP_A_DEFAULT;
    ZZS_SH(ZZS_SH_DIP_B) = ZZS_DIP_B_DEFAULT;
    ZZS_SH(ZZS_SH_DIP_C) = ZZS_DIP_C_DEFAULT;
    ZZS_SH(ZZS_SH_PACE_PERIOD) = ZZS_PACE_CYCLES_FRAME;
    ZZS_SH(ZZS_SH_STACK_TOP) = (zzs_u32)(unsigned long)&_zzs_stack_top;
    ZZS_SH(ZZS_SH_ROM_BAD_FILE) = 0xFFFFFFFFu;
}

void zzs_fatal(zzs_u32 error, zzs_u32 aux)
{
    g_running = 0;
    ZZS_SH(ZZS_SH_ERROR_AUX) = aux;
    ZZS_SH(ZZS_SH_ERROR) = error;
    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_ERROR;
    __asm__ volatile("dsb" ::: "memory");
}

/* INIT: prove that the private window is real, writable, distinct RAM before anything is
   stored there (an MMU or address mistake shows up here, with a clear error, instead of as a
   corrupted ROM later). */
static int ram_probe(zzs_u32 addr, zzs_u32 pattern)
{
    volatile zzs_u32 *p = (volatile zzs_u32 *)addr;
    zzs_u32 old = *p, got;
    *p = pattern;
    __asm__ volatile("dsb" ::: "memory");
    got = *p;
    *p = old;
    return got == pattern;
}

static void cmd_init(void)
{
    static const zzs_u32 probes[6] = {
        ZZS_ARM_ROMSTORE, ZZS_ARM_ROMSTORE_END - 4u, ZZS_ARM_SBRK,
        ZZS_ARM_ARENA, ZZS_ARM_ARENA_END - 4u, ZZS_ARM_STAGE
    };
    zzs_u32 i;
    for (i = 0; i < 6u; i++) {
        if (!ram_probe(probes[i], 0x5AA55AA5u ^ (i * 0x01010101u)) ||
            !ram_probe(probes[i], 0xA55AA55Au ^ (i * 0x01010101u))) {
            zzs_fatal(ZZS_ERR_MEMMAP, probes[i]);
            return;
        }
    }
    ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_NONE;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_INIT_OK;
    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_READY;
}

/* A2c - pacer locked on the display.
 *
 * The 640x480 output now runs at 25 MHz / (800 x 524), the very rate of the CPS-1, but Core1's
 * frame grid and the vblank keep an arbitrary phase. When a vblank falls inside the window where
 * frames finish (2.7 to 12 ms after the frame start, depending on the scene), two frames can be
 * ready within one vblank interval: the second waits a whole refresh, no buffer is free for the
 * next one, and that next frame is emulated but never drawn (41 frames in 5 min in A2b).
 * The launcher writes FLIP_SEQ as soon as it sees the firmware's PAN acknowledge (1 ms tick).
 * Core1 time-stamps that change and slews its frame grid by at most 100 us per frame, so that
 * the acknowledge is seen 1.5 ms after the frame start: the vblank then sits in the first
 * millisecond of the frame, where no frame is ever finished. The emulation keeps its rate on
 * average; only the phase moves. With the no60 preset (59.52 Hz) the same loop makes the game
 * follow the display, 0.2 % slower, instead of dropping frames. */
#define ZZS_VL_TARGET   (ZZS_PMU_PER_US * 1500u)
#define ZZS_VL_MAX_STEP (ZZS_PMU_PER_US * 100u)
zzs_u32 g_zzs_frame_no;
static zzs_u32 g_frame_start, g_vl_seq, g_vl_t, g_vl_samples, g_vl_locked;
static int g_vl_have, g_vl_err_f;
static zzs_s32 g_vl_adj_total;

void zzs_vlock_poll(void)
{
    zzs_u32 s = ZZS_SH(ZZS_SH_FLIP_SEQ);
    if (s != g_vl_seq) {
        g_vl_seq = s;
        g_vl_t = zzs_pmu_cycles();
        g_vl_have = 1;
    }
}

static void vlock_frame_end(void)
{
    zzs_s32 e, adj;
    if (!g_vl_have) return;
    g_vl_have = 0;
    /* A2e: an ack seen while waiting at the end of the previous frame gives a negative value
       here (A2d rejected it, so the loop never engaged when the work was short): fold it. */
    e = (zzs_s32)(g_vl_t - g_frame_start);
    if (e < -(zzs_s32)(2u * ZZS_PACE_CYCLES_FRAME) || e > (zzs_s32)(2u * ZZS_PACE_CYCLES_FRAME)) return;
    while (e < 0) e += (zzs_s32)ZZS_PACE_CYCLES_FRAME;
    while (e >= (zzs_s32)ZZS_PACE_CYCLES_FRAME) e -= (zzs_s32)ZZS_PACE_CYCLES_FRAME;
    ZZS_SH(ZZS_SH_VL_PHASE_US) = (zzs_u32)e / ZZS_PMU_PER_US;
    e -= (zzs_s32)ZZS_VL_TARGET;
    if (e > (zzs_s32)(ZZS_PACE_CYCLES_FRAME / 2u)) e -= (zzs_s32)ZZS_PACE_CYCLES_FRAME;
    if (e < -(zzs_s32)(ZZS_PACE_CYCLES_FRAME / 2u)) e += (zzs_s32)ZZS_PACE_CYCLES_FRAME;
    g_vl_err_f += (e - g_vl_err_f) / 8;
    g_vl_samples++;
    if (g_vl_err_f < (zzs_s32)(ZZS_PMU_PER_US * 500u) && g_vl_err_f > -(zzs_s32)(ZZS_PMU_PER_US * 500u)) g_vl_locked++;
    ZZS_SH(ZZS_SH_VL_ERR_US) = (zzs_u32)(g_vl_err_f / (zzs_s32)ZZS_PMU_PER_US);
    ZZS_SH(ZZS_SH_VL_SAMPLES) = g_vl_samples;
    ZZS_SH(ZZS_SH_VL_LOCKED) = g_vl_locked;
    if (ZZS_SH(ZZS_SH_RUN_FLAGS) & ZZS_RUNF_NO_VLOCK) { ZZS_SH(ZZS_SH_VL_ENABLED) = 0; return; }
    ZZS_SH(ZZS_SH_VL_ENABLED) = 1;
    /* ack later than wanted -> start the next frames later (and the reverse) */
    adj = g_vl_err_f / 4;
    if (adj > (zzs_s32)ZZS_VL_MAX_STEP) adj = (zzs_s32)ZZS_VL_MAX_STEP;
    if (adj < -(zzs_s32)ZZS_VL_MAX_STEP) adj = -(zzs_s32)ZZS_VL_MAX_STEP;
    g_next_deadline += (zzs_u32)adj;
    g_vl_adj_total += adj / (zzs_s32)ZZS_PMU_PER_US;
    ZZS_SH(ZZS_SH_VL_ADJ_US) = (zzs_u32)g_vl_adj_total;
}

static void pace_init(void)
{
    g_run_t0 = zzs_pmu_cycles();
    g_next_deadline = g_run_t0 + ZZS_PACE_CYCLES_FRAME;
    g_frame_start = g_run_t0;
    g_vl_seq = ZZS_SH(ZZS_SH_FLIP_SEQ);
    g_vl_have = 0; g_vl_err_f = 0; g_vl_samples = 0; g_vl_locked = 0; g_vl_adj_total = 0;
    g_zzs_frame_no = 0;
    g_pace_waits = g_pace_catchup = g_pace_hard = g_pace_max_late = 0;
}

/* ZZRastan "P60" pacer, unchanged: absolute deadline, wrap-safe, catch-up bounded to ONE frame.
   A light frame waits less and absorbs the previous overrun; only a lateness of a whole frame
   or more triggers a hard resync. It can never run faster than the arcade refresh. */
#define ZZS_PUMP_EVERY_CYCLES (ZZS_PMU_PER_US * 500u)      /* every 0.5 ms */

static void pace_frame(void)
{
    zzs_u32 now;
    zzs_s32 delta;

    vlock_frame_end();                   /* may move this frame's deadline by up to 100 us */
    now = zzs_pmu_cycles();
    delta = (zzs_s32)(g_next_deadline - now);

    if (delta > 0) {
        zzs_u32 next_pump = now;
        g_pace_waits++;
        do {
            now = zzs_pmu_cycles();
            if ((zzs_s32)(now - next_pump) >= 0) {        /* keep the PCM ring fed while waiting */
                zzs_audio_pump();
                next_pump = now + ZZS_PUMP_EVERY_CYCLES;
            }
        } while ((zzs_s32)(g_next_deadline - now) > 0);
    } else {
        zzs_u32 late = now - g_next_deadline;
        if (late > g_pace_max_late) g_pace_max_late = late;
        if (late >= ZZS_PACE_CYCLES_FRAME) {
            g_next_deadline = now;               /* too far behind: resynchronise */
            g_pace_hard++;
        } else {
            g_pace_catchup++;                    /* small overrun: absorbed next frame */
        }
    }
    g_frame_start = g_next_deadline;     /* nominal start of the next frame */
    g_next_deadline += ZZS_PACE_CYCLES_FRAME;

    ZZS_SH(ZZS_SH_PACE_WAITS) = g_pace_waits;
    ZZS_SH(ZZS_SH_PACE_CATCHUP) = g_pace_catchup;
    ZZS_SH(ZZS_SH_PACE_HARD) = g_pace_hard;
    ZZS_SH(ZZS_SH_PACE_MAX_LATE_US) = g_pace_max_late / ZZS_PMU_PER_US;
    ZZS_SH(ZZS_SH_TOTAL_RUN_CYC) = zzs_pmu_cycles() - g_run_t0;
}

static void cmd_start(void)
{
    if (!zzs_rom_ready()) {
        zzs_fatal(ZZS_ERR_NOT_READY, 0);
        return;
    }
    if (!g_machine_inited) {
        if (zzs_machine_init() != 0) {
            zzs_fatal(ZZS_ERR_ENGINE_INIT, ZZS_SH(ZZS_SH_ARENA_USED));
            return;
        }
        g_machine_inited = 1;
    }
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_ENGINE_OK;
    zzs_video_reset();
    zzs_audio_reset();
    if (zzs_machine_reset() != 0) {
        zzs_machine_publish();
        zzs_fatal(ZZS_ERR_CPU_RESET, ZZS_SH(ZZS_SH_M68K_PC));
        return;
    }
    /* The sound board runs from the first frame (ZZRastan finding: enabling it from the 68k
       across the Zorro bus races with the game's first sound command). This is the sound
       LOGIC; PCM output stays gated by PCM_ENABLE until AHI is up. */
    ZZS_SH(ZZS_SH_SND_ENABLE) = 1;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_CPU_RESET_OK;
    ZZS_SH(ZZS_SH_FRAME) = 0;
    ZZS_SH(ZZS_SH_PAUSED_FRAMES) = 0;
    g_pf_work = g_pf_pace = g_pf_cyc = g_pf_pal = 0;
    g_pf_frames = 0;
    g_pf_work_max = 0;
    ZZS_SH(ZZS_SH_PROF_FRAMES) = 0;
    zzs_machine_publish();
    pace_init();
    g_running = 1;
    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_RUNNING;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_RUNNING;
}

static void process_command(zzs_u32 cmd)
{
    switch (cmd) {
    case ZZS_CMD_INIT:
        cmd_init();
        break;
    case ZZS_CMD_ROM_BEGIN:
        g_running = 0;
        g_machine_inited = 0;
        zzs_rom_begin();
        break;
    case ZZS_CMD_ROM_CHUNK:
        (void)zzs_rom_chunk(ZZS_SH(ZZS_SH_ROM_FILE), ZZS_SH(ZZS_SH_ROM_OFFSET),
                            ZZS_SH(ZZS_SH_ROM_LENGTH));
        break;
    case ZZS_CMD_ROM_COMMIT:
        (void)zzs_rom_commit();
        break;
    case ZZS_CMD_START:
        cmd_start();
        break;
    case ZZS_CMD_SELFTEST:
        __asm__ volatile("udf #0");          /* proves the exception vectors report correctly */
        break;
    case ZZS_CMD_STOP:
        g_running = 0;
        stack_measure();
        ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_STOPPING;
        break;
    default:
        break;
    }
}

static zzs_u64 g_pf_ev[ZZS_PMU_NEV];     /* PMU event totals over the work time */
static zzs_u32 g_pf_late;

static void run_one_frame(void)
{
    zzs_u32 fw0, fw1, work, f, e0[ZZS_PMU_NEV], e1[ZZS_PMU_NEV], k;

    zzs_pmu_events(e0);
    fw0 = zzs_pmu_cycles();
    g_zzs_frame_no++;
    zzs_machine_frame();
    zzs_snd_rate_control();              /* after the frame's samples are queued */
    ZZS_SH(ZZS_SH_FRAME)++;
    zzs_video_present();
    zzs_audio_pump();
    fw1 = zzs_pmu_cycles();
    zzs_pmu_events(e1);
    work = fw1 - fw0;
    for (k = 0; k < ZZS_PMU_NEV; k++) g_pf_ev[k] += (zzs_u64)(e1[k] - e0[k]);
    if (work > ZZS_PACE_CYCLES_FRAME) g_pf_late++;

    g_pf_frames++;
    f = g_pf_frames;
    g_pf_work += (zzs_u64)work;
    g_pf_cyc  += (zzs_u64)zzs_machine_take_m68k_cycles_pmu();
    g_pf_pal  += (zzs_u64)zzs_machine_take_pal_cycles();
    if (work > g_pf_work_max) g_pf_work_max = work;

    if (ZZS_SH(ZZS_SH_STATUS) == ZZS_ST_ERROR) {
        g_running = 0;
    } else if (!(ZZS_SH(ZZS_SH_RUN_FLAGS) & ZZS_RUNF_NO_PACE)) {
        zzs_u32 pc0 = zzs_pmu_cycles();
        pace_frame();
        g_pf_pace += (zzs_u64)(zzs_pmu_cycles() - pc0);
    }

    ZZS_SH(ZZS_SH_PROF_FRAMES) = f;
    ZZS_SH(ZZS_SH_PROF_WORK_US)      = (zzs_u32)(g_pf_work / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_PACE_US)      = (zzs_u32)(g_pf_pace / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_CYCLONE_US)   = (zzs_u32)(g_pf_cyc  / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_PAL_US)       = (zzs_u32)(g_pf_pal  / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_PROF_WORK_MAX_US)  = g_pf_work_max / ZZS_PMU_PER_US;
    ZZS_SH(ZZS_SH_PROF_WORK_LAST_US) = work / ZZS_PMU_PER_US;
    ZZS_SH(ZZS_SH_WORK_LATE)   = g_pf_late;
    ZZS_SH(ZZS_SH_IPC_X100)    = g_pf_work ? (zzs_u32)((g_pf_ev[0] * 100u) / g_pf_work) : 0u;
    ZZS_SH(ZZS_SH_INSTR_K)     = (zzs_u32)(g_pf_ev[0] / f / 1000u);
    ZZS_SH(ZZS_SH_BR_MISS)     = (zzs_u32)(g_pf_ev[1] / f);
    ZZS_SH(ZZS_SH_L1D_REFILL)  = (zzs_u32)(g_pf_ev[2] / f);
    ZZS_SH(ZZS_SH_L1I_REFILL)  = (zzs_u32)(g_pf_ev[3] / f);
    ZZS_SH(ZZS_SH_DSTALL_US)   = (zzs_u32)(g_pf_ev[4] / ZZS_PMU_PER_US / f);
    ZZS_SH(ZZS_SH_ISTALL_US)   = (zzs_u32)(g_pf_ev[5] / ZZS_PMU_PER_US / f);
    zzs_snd_publish();
    zzs_machine_publish();
}

/* Entered from zzs_entry.S on the private stack, caches and MMU still off. */
void zzs_core1_main(void)
{
    zzs_u32 seq;

    zero_bss();
    mmu_init();
    stack_paint();
    shared_defaults();
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_MMU_OK;
    zz_install_vectors();
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_VECTORS_OK;
    zzs_pmu_enable();
    run_init_array();                        /* C++ static constructors, if any */

    g_running = 0;
    g_machine_inited = 0;
    g_last_cmd_seq = ZZS_SH(ZZS_SH_CMD_SEQ);
    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_READY;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_READY;
    __asm__ volatile("dsb" ::: "memory");

    for (;;) {
        ZZS_SH(ZZS_SH_HEARTBEAT)++;
        seq = ZZS_SH(ZZS_SH_CMD_SEQ);
        if (seq != g_last_cmd_seq) {
            g_last_cmd_seq = seq;
            process_command(ZZS_SH(ZZS_SH_COMMAND));
            ZZS_SH(ZZS_SH_ACK_SEQ) = seq;
            __asm__ volatile("dsb" ::: "memory");
        }

        if (ZZS_SH(ZZS_SH_STATUS) == ZZS_ST_STOPPING)
            break;

        if (g_running && ZZS_SH(ZZS_SH_STATUS) == ZZS_ST_RUNNING) {
            /* PAUSE: freeze the whole machine - 68000, Z80, YM2151, MSM6295 and the renderer -
               but keep pacing so we neither spin nor drift. The display holds the last
               completed frame and AHI pads silence. Nothing is reset. */
            if (ZZS_SH(ZZS_SH_PAUSE)) {
                ZZS_SH(ZZS_SH_PAUSED_FRAMES)++;
                zzs_snd_push_silence(370u);          /* one frame of silence: no AHI underrun */
                pace_frame();
                continue;
            }
            run_one_frame();
        }
    }

    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_STOPPED;
    __asm__ volatile("dsb" ::: "memory");
    for (;;) {
        __asm__ volatile("wfe");
    }
}
