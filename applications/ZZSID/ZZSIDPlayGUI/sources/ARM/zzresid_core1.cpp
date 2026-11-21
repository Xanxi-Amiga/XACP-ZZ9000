/*
 * zzresid_core1.cpp - ZZReSID Core1 free-running ring model
 *
 * Core1 renders continuously into a PCM ring, advancing SH_PCM_WRITE.
 * It never gets ahead of the 68k read cursor by more than the ring size
 * (backpressure: if the ring is full, Core1 spins until 68k drains).
 *
 * Builds:
 *   A: fixed 440Hz sine (ring/AHI test)
 *   C: full reSID render
 *
 * D-cache OFF (startup.S), so plain access + DSB barriers suffice.
 * All shared 32-bit fields via ARM32() byte-swap.
 *
 * ASCII-only.  Author: Xanxi / 2026
 */

#include <stdint.h>
#include "zzresid_shared.h"

#if defined(ZZRESID_BUILD_C)
#include "sid.h"
#endif

static inline uint32_t ARM32(uint32_t x) { return __builtin_bswap32(x); }
static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

static volatile uint32_t *g_shared = (volatile uint32_t *)ZZRESID_SHARED_ARM_ADDR;
static volatile uint32_t *g_ring   = (volatile uint32_t *)ZZRESID_PCM_ARM_ADDR;

static uint32_t g_sample_rate = 44100;
static uint32_t g_chip_model  = ZZRESID_CHIP_6581;
static int32_t  g_cps_fp      = 0;
static uint32_t g_delta_frac  = 0;
static uint32_t g_overruns    = 0;
static uint32_t g_filter_cfg  = ZZRESID_FILT_NONE;
static uint32_t g_generation  = 0;
static uint32_t g_setup_error = ZZRESID_ERR_NONE;
static uint32_t g_mode        = ZZRESID_MODE_TONE;

static inline uint32_t sh_read(int idx)  { return ARM32(g_shared[idx]); }
static inline void     sh_write(int idx, uint32_t v) { g_shared[idx] = ARM32(v); dsb(); }

/* Write one stereo sample into the ring at position pos (masked) */
static inline void ring_put(uint32_t pos, int16_t l, int16_t r)
{
    uint32_t stereo = ((uint32_t)(uint16_t)l << 16) | (uint32_t)(uint16_t)r;
    g_ring[pos & ZZRESID_RING_MASK] = ARM32(stereo);
}

/* =========================== BUILD A =========================== */
#if defined(ZZRESID_BUILD_A)
static uint32_t g_phase = 0;
static const int16_t sine16[16] = {
        0,  3061,  5657,  7391,  8000,  7391,  5657,  3061,
        0, -3061, -5657, -7391, -8000, -7391, -5657, -3061
};
static uint32_t g_applied_model = 0xFFFFFFFFu;
static void sid_setup(void) { g_cps_fp = 0; g_applied_model = g_chip_model; }

/* Render 'count' stereo samples into the ring starting at write_pos */
static uint32_t render_into_ring(uint32_t write_pos, uint32_t count)
{
    const uint32_t step = (uint32_t)(((uint64_t)440 * 16 * 65536) / 44100);
    for (uint32_t i = 0; i < count; i++) {
        int16_t s = sine16[(g_phase >> 16) & 0x0F];
        ring_put(write_pos + i, s, s);
        g_phase += step;
    }
    return count;
}
#endif

/* =========================== BUILD C =========================== */
#if defined(ZZRESID_BUILD_C)
/* Up to ZZRESID_MAX_SIDS chips. Index 0 is the left channel, index 1 the
 * right. Everything below keeps working unchanged when g_num_sids == 1. */
#define ZZ_RENDER_CHUNK_MAX 2048
static uint8_t     g_sid_storage[ZZRESID_MAX_SIDS][sizeof(reSID::SID)]
                       __attribute__((aligned(16)));
static reSID::SID *g_sid[ZZRESID_MAX_SIDS] = { 0, 0, 0 };
static uint32_t    g_num_sids = 1;
static uint16_t    g_sid_base[ZZRESID_MAX_SIDS] = { 0xD400u, 0xD420u, 0xD440u };
static int16_t     g_mono[ZZRESID_MAX_SIDS][ZZ_RENDER_CHUNK_MAX];
static uint32_t    g_mirror = 0;                        /* chip0 -> chip1 */
static uint8_t     g_sid_shadow_n[ZZRESID_MAX_SIDS][32];
static uint32_t    g_sid_writes_c[ZZRESID_MAX_SIDS];
static uint32_t    g_applied_model_n[ZZRESID_MAX_SIDS] = { 0xFFFFFFFFu,
                                                           0xFFFFFFFFu,
                                                           0xFFFFFFFFu };
inline void* operator new(unsigned int, void* p) throw() { return p; }
inline void  operator delete(void*, void*) throw() {}

/* What was ACTUALLY handed to reSID. Distinct from g_chip_model, which is
 * only the validated request: publishing the request as "active" is how a
 * hard-coded MOS6581 managed to report itself as MOS8580 for months. */
static uint32_t g_applied_model = 0xFFFFFFFFu;

static void sid_setup(void)
{
    /* A model change on a RESIDENT blob goes through here, because the
     * project reconfigures in place rather than relaunching. sid.h shows the
     * SID object owns voice[3], filter and extfilt BY VALUE, so nothing is
     * allocated and set_chip_model() is a plain forward of assignments - but
     * that is read from a header, not from the .cc. Rather than trust it,
     * rebuild the object whenever the model actually changes: placement new
     * over the same storage, so the state cannot be half 6581 half 8580.
     * The 17 MB filter tables are static, so this costs nothing after the
     * first construction. Same model, different filters: untouched path. */
    uint32_t i;

    /* All chips carry the same model in phase A0. */
    for (i = 0; i < ZZRESID_MAX_SIDS; i++) {
        if (g_sid[i] && g_applied_model_n[i] != 0xFFFFFFFFu &&
            g_applied_model_n[i] != g_chip_model) {
            /* Destructor before reusing the storage: correct by the book,
             * and safe here because we use SAMPLE_FAST, under which reSID
             * allocates nothing - its only heap member is the resampling
             * ring, which stays null and whose delete[] is a no-op. */
            g_sid[i]->~SID();
            g_sid[i] = 0;
        }
        if (i >= g_num_sids) {
            /* Chip not in use this run: leave it destroyed. */
            if (g_sid[i]) { g_sid[i]->~SID(); g_sid[i] = 0; }
            g_applied_model_n[i] = 0xFFFFFFFFu;
            continue;
        }
        if (!g_sid[i]) g_sid[i] = new (g_sid_storage[i]) reSID::SID();

        if (g_chip_model == ZZRESID_CHIP_8580) {
            g_sid[i]->set_chip_model(reSID::MOS8580);
            g_applied_model_n[i] = ZZRESID_CHIP_8580;
        } else {
            g_sid[i]->set_chip_model(reSID::MOS6581);
            g_applied_model_n[i] = ZZRESID_CHIP_6581;
        }

        g_sid[i]->set_sampling_parameters(
            (double)ZZRESID_CLOCK_PAL, reSID::SAMPLE_FAST,
            (double)g_sample_rate, -1.0, 0.97);
        g_sid[i]->reset();
        g_sid[i]->enable_filter(
            (g_filter_cfg & ZZRESID_FILT_INTERNAL) ? true : false);
        g_sid[i]->enable_external_filter(
            (g_filter_cfg & ZZRESID_FILT_EXTERNAL) ? true : false);
    }

    /* SH_ACTIVE_MODEL still describes chip 0, which is what every existing
     * single-chip diagnostic means by "the model". */
    g_applied_model = g_applied_model_n[0];
    g_cps_fp = (int32_t)(((uint64_t)ZZRESID_CLOCK_PAL << 16) / g_sample_rate);
}

/* =======================================================================
 * MOS8580 write pipeline.
 *
 * reSID, SID::write():
 *     if (sampling == SAMPLE_FAST && sid_model == MOS8580)
 *         write_pipeline = 1;        // deferred, applied by the next clock()
 *     else
 *         write();                   // immediate
 *
 * Each write overwrites write_address and bus_value, so a batch of writes
 * with no clock() in between collapses to the LAST one and every earlier
 * write is lost. That is exactly why the 8580 rendered rigorous silence
 * while the 6581, whose writes apply immediately, played perfectly: no
 * frequency, no gate, no envelope ever reached the voices.
 *
 * Flushing with one cycle PRESERVES reSID's deliberate one-cycle 8580
 * delay - some tunes use it to detect which chip they run on - rather than
 * bypassing it. The cycles spent are counted and handed back to the render
 * budget so the tune's time base does not drift.
 * ======================================================================= */
static uint32_t g_write_cycles = 0;

/* In multi-SID, clocking only the chip that was written would let the
 * chips drift apart: a cycle is global time, not per-chip time. So every
 * active chip advances by the same single cycle, and the cost is charged
 * once, globally, to the frame budget. */
/* Raw write to one chip: no clocking, no notion of C64 time. */
static inline int sid_raw_write(uint32_t chip, uint8_t reg, uint8_t val)
{
    if (chip >= g_num_sids || !g_sid[chip]) return 0;
    g_sid[chip]->write(reg, val);
    g_sid_shadow_n[chip][reg] = val;
    g_sid_writes_c[chip]++;
    return (g_applied_model_n[chip] == ZZRESID_CHIP_8580) ? 1 : 0;
}

/* ONE logical C64 write. It may reach two chips, but C64 time advances
 * once: counting a cycle per target would drift the tune's tempo and put
 * the chips out of step with each other. */
static inline void sid_write_chip(uint32_t chip, uint8_t reg, uint8_t val)
{
    uint32_t i;
    int need_clock;

    if (chip >= g_num_sids) return;

    need_clock = sid_raw_write(chip, reg, val);

    /* Two chips are hardware, not a property of the tune: a 1SID tune must
     * still come out of both, the way a two-chip cartridge behaves. The
     * launcher turns mirroring off when the tune addresses chip 1 itself. */
    if (chip == 0 && g_mirror && g_num_sids > 1)
        need_clock |= sid_raw_write(1, reg, val);

    if (need_clock) {
        for (i = 0; i < g_num_sids; i++)
            if (g_sid[i]) g_sid[i]->clock(1);
        g_write_cycles++;
    }
}

/* Which chip owns this C64 address, and at what register offset.
 * Masking to 5 bits unconditionally would alias $D420 onto chip 0, which
 * is exactly the bug this function exists to prevent. */
static int sid_find_chip(uint16_t addr, uint8_t *reg_out)
{
    uint32_t i;
    for (i = 0; i < g_num_sids; i++) {
        uint16_t base = g_sid_base[i];
        if (addr >= base && addr < (uint16_t)(base + 0x20u)) {
            *reg_out = (uint8_t)(addr - base);
            return (int)i;
        }
    }
    /* 1SID compatibility: the $D400-$D7FF mirrors still address chip 0
     * when no secondary base matches. */
    if (addr >= 0xD400u && addr <= 0xD7FFu) {
        *reg_out = (uint8_t)(addr & 0x1Fu);
        return 0;
    }
    return -1;
}

static void apply_reg_batch(uint32_t count)
{
    if (count > ZZRESID_MAX_REGS) count = ZZRESID_MAX_REGS;
    uint32_t clocked = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t w = ARM32(g_shared[SH_REG_BATCH_BASE + i]);
        uint8_t reg = ZZRESID_UNPACK_REG(w);
        uint8_t val = ZZRESID_UNPACK_VAL(w);
        uint16_t cyc = ZZRESID_UNPACK_CYC(w);
        /* reg is the offset from $D400, so bits 5-6 select the chip and
         * bits 0-4 the register. Chip 0 keeps the values 0x00-0x1C it has
         * always used, so existing batches are unaffected. */
        uint32_t chip = (uint32_t)(reg >> 5);
        uint8_t  r    = (uint8_t)(reg & 0x1Fu);
        if (r > 0x1C || chip >= g_num_sids) continue;
        if (cyc > clocked) {
            reSID::cycle_count d = (reSID::cycle_count)(cyc - clocked);
            uint32_t k;
            short dummy;
            for (k = 0; k < g_num_sids; k++)
                if (g_sid[k]) g_sid[k]->clock(d, &dummy, 0);
            clocked = cyc;
        }
        sid_write_chip(chip, r, val);
    }
}

/* ---- Render diagnostics (published to mailbox each call) ---- */
static uint32_t g_diag_capacity   = 0;
static uint32_t g_diag_delta_fed  = 0;
static uint32_t g_diag_got_last   = 0;
static uint32_t g_diag_produced   = 0;
static uint32_t g_diag_zeropad    = 0;   /* MUST stay 0 */
static uint32_t g_diag_clockcalls = 0;
/* OSC3 ($D41B) and ENV3 ($D41C) are reSID's own public read-backs: the RAW
 * oscillator output and the RAW envelope counter of voice 3, both taken
 * BEFORE the DAC and before any mixing. They split the silence question
 * three ways without reading a line of reSID source:
 *   osc3 moves, env3 moves, output 0 -> fault is in the DAC / mix stage
 *   osc3 moves, env3 stuck 0         -> fault is in the envelope
 *   osc3 stuck 0                     -> fault is in the waveform generator
 * Peaks are accumulated over the whole run. */
static uint32_t g_probe_osc3      = 0;
static uint32_t g_probe_env3      = 0;
static int32_t  g_diag_min_r      = 32767;
static int32_t  g_diag_max_r      = -32768;
static uint32_t g_diag_len_mismatch = 0;
static int32_t  g_diag_min        = 32767;
static int32_t  g_diag_max        = -32768;
static uint32_t g_diag_clip_pos   = 0;
static uint32_t g_diag_clip_neg   = 0;

/* Cycle state preserved ACROSS calls (rules 6 and 7) */
static reSID::cycle_count g_pending_cycles = 0;   /* unconsumed delta */
/* g_delta_frac holds the 16.16 fractional cycle remainder */

/*
 * Render up to 'count' stereo frames into the ring.
 *
 * reSID contract:
 *   int clock(cycle_count& delta_t, short* buf, int n, int interleave=1)
 *   - cycle-budget driven: consumes from delta_t (BY REFERENCE, decremented)
 *   - n is buffer CAPACITY, not a guaranteed output count
 *   - returns the number of samples ACTUALLY produced (may be < n)
 *   - stops early if the buffer fills; leftover delta_t must be preserved
 *
 * Rules enforced here:
 *   4. never zero-pad when got < want
 *   5. write only the samples actually produced
 *   6. preserve fractional cycles across calls (g_delta_frac)
 *   7. preserve unconsumed delta cycles (g_pending_cycles)
 *
 * Returns the number of frames actually written.
 */
static uint32_t render_into_ring(uint32_t write_pos, uint32_t count)
{
    uint32_t produced = 0;
    uint32_t i;

    /* Clamp first: the cycle budget is derived from count, so clamping
     * afterwards would hand reSID a budget for samples we cannot store. */
    if (count > ZZ_RENDER_CHUNK_MAX) count = ZZ_RENDER_CHUNK_MAX;

    g_diag_capacity   = count;
    g_diag_zeropad    = 0;
    g_diag_clockcalls = 0;
    g_diag_got_last   = 0;

    /* Cycle budget for 'count' samples, plus any fractional carry.
     * Add whatever cycles were left unconsumed from the previous call. */
    uint64_t total  = (uint64_t)count * (uint32_t)g_cps_fp + g_delta_frac;
    reSID::cycle_count budget = (reSID::cycle_count)(total >> 16);
    g_delta_frac = (uint32_t)(total & 0xFFFF);
    budget += g_pending_cycles;
    g_pending_cycles = 0;

    /* Cycles already burnt flushing 8580 pipelined writes belong to this
     * frame: charge them here, carrying any excess rather than letting the
     * budget go negative. Zero in 6581, about 23 per PAL frame in 8580 -
     * roughly 0.1% of 19705. */
    if (g_write_cycles) {
        if ((uint32_t)budget > g_write_cycles) {
            budget -= (reSID::cycle_count)g_write_cycles;
            g_write_cycles = 0;
        } else {
            g_write_cycles -= (uint32_t)budget;
            budget = 0;
        }
    }

    g_diag_delta_fed = (uint32_t)budget;

    /* Each chip gets its OWN copy of the budget. clock() takes it by
     * reference and decrements it, so sharing one variable would starve
     * every chip after the first. */
    {
        uint32_t n[ZZRESID_MAX_SIDS];
        uint32_t nmin = count;

        for (i = 0; i < g_num_sids; i++) {
            reSID::cycle_count b = budget;
            uint32_t got_total = 0;

            while (b > 0 && got_total < count) {
                reSID::cycle_count before = b;
                int got = g_sid[i]->clock(b, &g_mono[i][got_total],
                                          (int)(count - got_total));
                if (i == 0) g_diag_clockcalls++;
                if (got < 0) got = 0;
                if (i == 0) g_diag_got_last = (uint32_t)got;
                got_total += (uint32_t)got;
                /* Forward-progress guard: no cycles consumed AND nothing
                 * produced means we would spin. Preserve the budget. */
                if (got == 0 && b == before) break;
            }
            n[i] = got_total;
            if (got_total < nmin) nmin = got_total;

            /* Rule 7: the reference chip decides what carries over. */
            if (i == 0) g_pending_cycles = b;

            {
                uint32_t o3 = (uint32_t)g_sid[i]->read(0x1B);
                uint32_t e3 = (uint32_t)g_sid[i]->read(0x1C);
                if (o3 > g_probe_osc3) g_probe_osc3 = o3;
                if (e3 > g_probe_env3) g_probe_env3 = e3;
            }
        }

        /* Two chips fed identical parameters must produce identical counts.
         * If they ever differ they are drifting apart, so record it rather
         * than absorb it silently. */
        if (g_num_sids > 1 && n[0] != n[1]) g_diag_len_mismatch++;

        /* Rule 5: write ONLY what was produced. Rule 4: no zero padding. */
        for (produced = 0; produced < nmin; produced++) {
            int16_t l = g_mono[0][produced];
            int16_t r = (g_num_sids > 1) ? g_mono[1][produced] : l;

            if (l > g_diag_max) g_diag_max = l;
            if (l < g_diag_min) g_diag_min = l;
            if (r > g_diag_max_r) g_diag_max_r = r;
            if (r < g_diag_min_r) g_diag_min_r = r;
            if (l >= 32767 || r >= 32767) g_diag_clip_pos++;
            if (l <= -32768 || r <= -32768) g_diag_clip_neg++;

            ring_put(write_pos + produced, l, r);
        }
    }

    g_diag_produced = produced;
    return produced;
}

#endif


/* Read the requested configuration, validate it, and publish BOTH what was
 * asked for and what was actually applied. Out-of-range values are clamped
 * but always reported through SH_SETUP_ERROR - never silently swallowed. */
static void read_and_publish_config(void)
{
    uint32_t req_model  = sh_read(SH_CHIP_MODEL);
    uint32_t req_filter = sh_read(SH_FILTER_CFG);
    uint32_t req_rate   = sh_read(SH_SAMPLE_RATE);

    g_setup_error = ZZRESID_ERR_NONE;

    if (req_model > ZZRESID_CHIP_8580) {
        g_chip_model  = ZZRESID_CHIP_6581;
        g_setup_error = ZZRESID_ERR_BAD_MODEL;
    } else {
        g_chip_model = req_model;
    }

    if (req_filter > (ZZRESID_FILT_INTERNAL | ZZRESID_FILT_EXTERNAL)) {
        g_filter_cfg  = ZZRESID_FILT_NONE;
        if (g_setup_error == ZZRESID_ERR_NONE)
            g_setup_error = ZZRESID_ERR_BAD_FILTER;
    } else {
        g_filter_cfg = req_filter;
    }

    if (req_rate < 8000 || req_rate > 96000) {
        g_sample_rate = 44100;
        if (g_setup_error == ZZRESID_ERR_NONE)
            g_setup_error = ZZRESID_ERR_BAD_RATE;
    } else {
        g_sample_rate = req_rate;
    }

    /* Multi-SID. Out-of-range values are clamped, and SH_ACTIVE_NUM_SIDS
     * reports what was really applied so a clamp is visible rather than
     * guessed at - the same discipline that exposed the chip-model bug. */
    {
        uint32_t req_n    = sh_read(SH_NUM_SIDS);
        uint32_t req_b1   = sh_read(SH_SID1_BASE);

        if (req_n < 1u) req_n = 1u;
        if (req_n > (uint32_t)ZZRESID_MAX_SIDS) req_n = (uint32_t)ZZRESID_MAX_SIDS;

        /* A second chip needs a base that is inside the I/O page, aligned
         * on 32 bytes, and not overlapping chip 0. */
        if (req_n > 1u) {
            if (req_b1 < 0xD420u || req_b1 > 0xDFE0u || (req_b1 & 0x1Fu) != 0u ||
                (req_b1 >= 0xD800u && req_b1 < 0xDE00u))
                req_b1 = 0xD420u;
            g_sid_base[1] = (uint16_t)req_b1;
        }
        g_num_sids = req_n;
        g_mirror   = (sh_read(SH_SID_MIRROR) && req_n > 1u) ? 1u : 0u;
    }

    sh_write(SH_REQUESTED_MODEL, req_model);
    sh_write(SH_ACTIVE_FILTER,   g_filter_cfg);
    sh_write(SH_SETUP_ERROR,     g_setup_error);
    /* SH_ACTIVE_MODEL is deliberately NOT written here: at this point
     * nothing has been applied yet. publish_applied_model() does it after
     * sid_setup() has really called reSID. */
}

/* Call only after sid_setup(). Publishes what reSID was actually given, so a
 * mismatch with SH_REQUESTED_MODEL is visible instead of being papered over. */
static void publish_applied_model(void)
{
    sh_write(SH_ACTIVE_MODEL, g_applied_model);
}


/* =======================================================================
 * MMU + D-cache
 *
 * On ARMv7-A the D-cache does NOT work with the MMU off: with SCTLR.M=0
 * every data access is Strongly-Ordered whatever SCTLR.C says. Enabling
 * the D-cache therefore means enabling the MMU with a translation table.
 *
 * Sequence, descriptor encodings and the set/way invalidate below are
 * taken verbatim from the validated ZZQuake / ZZDoom mmu_init. ONLY the
 * region map differs:
 *
 *   sections 0x042..0x059  blob code/data/bss (24 MB)  Normal WB/WA
 *   section  0x05A         mailbox + PCM ring          Normal NC
 *   everything else                                    Device
 *
 * Mailbox and ring are Normal Non-Cacheable, so 68k<->ARM coherency is
 * free on the ARM side and no flush/invalidate is needed there. The 68k
 * still needs its own CacheClearE - that is the 68060 cache, unrelated.
 *
 * Device everywhere else protects the Core0 firmware in OCM, the FPGA
 * registers and the Zorro bridge from being cached.
 *
 * NOTE on teardown: the blob no longer ends in a terminal WFE. It returns,
 * which is how core1_loop() in the firmware expects a payload to give Core1
 * back (see startup.S). The MMU is NOT disabled here; startup.S restores
 * the firmware's SCTLR/TTBR0/TTBCR/DACR on the way out, which is what
 * switches the MMU back. ZZQuake's random resets came from disabling the
 * MMU and then halting, with no cache maintenance and no state restore -
 * a different thing entirely. This function still turns the MMU and caches
 * off on entry, so a blob starting after a run that left them on is safe.
 * ======================================================================= */

static uint32_t l1_table[4096] __attribute__((aligned(16384)));

#define SEC_TYPE       0x2u
#define SEC_AP_RW      (3u<<10)
#define SEC_DOMAIN0    (0u<<5)
#define MT_NORMAL_WBWA ((1u<<12) | (1u<<3) | (1u<<2))   /* TEX=001,C=1,B=1 */
#define MT_NORMAL_NC   ((1u<<12) | (0u<<3) | (0u<<2))   /* TEX=001,C=0,B=0 */
#define MT_DEVICE      ((0u<<12) | (0u<<3) | (1u<<2))   /* TEX=000,C=0,B=1 */

static uint32_t make_section(uint32_t pa_mb, uint32_t memtype)
{
    return (pa_mb << 20) | SEC_TYPE | SEC_AP_RW | SEC_DOMAIN0 | memtype;
}

static void mmu_init_zzresid(void)
{
    uint32_t i;

    /* Turn MMU and caches OFF first: we may arrive with them still on. */
    {
        uint32_t sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr &= ~(1u<<0);    /* M  MMU off     */
        sctlr &= ~(1u<<2);    /* C  D-cache off */
        sctlr &= ~(1u<<12);   /* I  I-cache off */
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr):"memory");
        __asm__ volatile("dsb":::"memory");
        __asm__ volatile("isb":::"memory");
    }

    /* Safe default: everything Device. */
    for (i = 0; i < 4096u; i++)
        l1_table[i] = make_section(i, MT_DEVICE);

    /* Blob code + data + bss + stack: 0x04200000..0x059FFFFF, cacheable.
     * This is what makes the residfp filter tables fast. */
    for (i = 0x042u; i <= 0x059u; i++)
        l1_table[i] = make_section(i, MT_NORMAL_WBWA);

    /* Mailbox (0x05A00000) and PCM ring (0x05A40000) share section 0x05A.
     * Normal Non-Cacheable so the 68k sees our writes with no flushing. */
    l1_table[0x05Au] = make_section(0x05Au, MT_NORMAL_NC);

    /* DACR: domain 0 = client */
    __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(0x00000001u));
    /* TTBCR = 0 (TTBR0 only) */
    __asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(0u));
    {
        uint32_t ttbr0 = ((uint32_t)l1_table) | 0x6Bu;  /* WB walk attrs */
        __asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(ttbr0));
    }
    __asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(0u)); /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,6"::"r"(0u)); /* BPIALL  */
    __asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(0u)); /* ICIALLU */

    /* Invalidate the whole L1 D-cache by set/way, with the D-cache still
     * OFF. INVALIDATE, never clean: cleaning would write stale lines from
     * a previous run back over freshly loaded memory.
     * Cortex-A9 L1D: 4 ways x 256 sets, 32-byte lines. */
    {
        uint32_t way, set;
        for (way = 0; way < 4u; way++)
            for (set = 0; set < 256u; set++) {
                uint32_t sw = (way << 30) | (set << 5);
                __asm__ volatile("mcr p15,0,%0,c7,c6,2"::"r"(sw):"memory");
            }
        __asm__ volatile("dsb":::"memory");
    }
    __asm__ volatile("isb":::"memory");

    /* Enable MMU, D-cache, I-cache, branch prediction. Alignment check off. */
    {
        uint32_t sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sctlr |=  (1u<<0);   /* M */
        sctlr |=  (1u<<2);   /* C */
        sctlr |=  (1u<<12);  /* I */
        sctlr |=  (1u<<11);  /* Z */
        sctlr &= ~(1u<<1);   /* A off */
        __asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(sctlr));
        __asm__ volatile("isb":::"memory");
    }
}


/* Clean AND invalidate the whole L1 D-cache by set/way. Called just before
 * returning to the firmware, while the MMU is still enabled - a set/way
 * operation is meaningless once SCTLR.M is clear. Unlike the entry path,
 * this one CLEANS: our dirty lines must reach DDR before the firmware runs
 * again with its own translation table.
 * Cortex-A9 L1D: 4 ways x 256 sets, 32-byte lines. */
static void cache_clean_invalidate_all(void)
{
    uint32_t way, set;
    __asm__ volatile("dsb":::"memory");
    for (way = 0; way < 4u; way++)
        for (set = 0; set < 256u; set++) {
            uint32_t sw = (way << 30) | (set << 5);
            /* DCCISW - clean and invalidate by set/way */
            __asm__ volatile("mcr p15,0,%0,c7,c14,2"::"r"(sw):"memory");
        }
    __asm__ volatile("dsb":::"memory");
    __asm__ volatile("isb":::"memory");
}


/* =======================================================================
 * Bounded heap.
 *
 * The linker used to hand newlib an open-ended heap starting after the
 * stack, with nothing between it and the shared mailbox at 0x05A00000. An
 * allocation large enough would have walked straight over the protocol -
 * and the project archive records Filter::set_chip_model(MOS8580) asking
 * for 256 KB+ on the old Core0 build, so that path does allocate.
 *
 * _sbrk now refuses to cross _heap_limit and returns -1 like a normal
 * out-of-memory, so a failure is a clean malloc failure instead of silent
 * corruption. Peak usage is published for the 68k to read.
 * ======================================================================= */
extern "C" char  _heap_start;
extern "C" char  _heap_limit;
static char     *g_brk = 0;
static uint32_t  g_heap_peak = 0;
static uint32_t  g_heap_fail = 0;

extern "C" void *_sbrk(int incr)
{
    char *prev;
    if (!g_brk) g_brk = &_heap_start;
    prev = g_brk;
    if (incr > 0 && (g_brk + incr) > &_heap_limit) {
        g_heap_fail++;
        return (void *)-1;
    }
    g_brk += incr;
    {
        uint32_t used = (uint32_t)(g_brk - &_heap_start);
        if (used > g_heap_peak) g_heap_peak = used;
    }
    return (void *)prev;
}


#if defined(ZZRESID_BUILD_C)
/* =======================================================================
 * 6502 / PSID player
 *
 * A .sid file is not a register dump, it is 6502 machine code: the
 * musician's player routine. To get sound out of it we run that code -
 * init() once, then play() fifty times a second - and capture what it
 * writes to $D400-$D7FF. Those writes go straight into reSID.
 *
 * The 6502 runs here on Core1 rather than on the 68k so that CPU and SID
 * share one clock: play() then exactly one PAL frame of samples.
 *
 * Scope: PSID at 50 Hz. RSID needs full C64 interrupt emulation and is
 * deliberately out of scope for now.
 * ======================================================================= */

extern "C" {
#include "fake6502.h"
}

static uint8_t  g_c64ram[65536];
static fake6502_context g_cpu;
static uint32_t g_sid_writes = 0;
static uint32_t g_sid_frames = 0;
static uint8_t  g_raster     = 0;
static int      g_tune_ready = 0;

/* =======================================================================
 * C64 interrupt model - ONLY for tunes whose play address is $0000.
 *
 * WHY THIS EXISTS
 * ---------------
 * A PSID tune hands the player an init routine and a play routine, and the
 * player calls play once per frame. A tune with play == $0000 does not work
 * that way: during init it installs its OWN interrupt handler and then
 * expects a C64 to keep running, with the IRQ arriving from CIA timer A or
 * from the raster. Arkanoid is one of these, and so is every RSID.
 *
 * Before this, such a tune ran its init and then nothing ever executed
 * again - sid_play_frame() calls cpu_call(play) only "if (play)". The init
 * writes left the chip in some state, that made a short noise, and that was
 * the whole of it.
 *
 * WHY IT CANNOT BREAK A TUNE THAT WORKS TODAY
 * -------------------------------------------
 * Everything here is behind g_irq_mode, which is set only when play == 0.
 * A tune with a play address takes exactly the branches it took before,
 * including the raster bump in sid_play_frame(), and never reaches a line
 * of this code. The memory hooks test g_irq_mode before any new case, so
 * even their branch order is unchanged for a working tune.
 *
 * WHAT IS MODELLED
 * ----------------
 * CIA 1 timer A, the raster compare, and the interrupt dispatch. That is
 * what SID tunes use. Nothing else of the CIA or the VIC is emulated, and
 * nothing here pretends to be a C64.
 * ======================================================================= */
#define PAL_CYCLES_PER_LINE   63u
#define PAL_LINES_PER_FRAME  312u
#define PAL_CYCLES_PER_FRAME (ZZRESID_CLOCK_PAL / ZZRESID_PAL_FPS)

static uint32_t g_irq_mode = 0;      /* the gate. 0 = nothing below runs. */

/* CIA 1 timer A */
static uint16_t g_cia_latch    = 0;
static int32_t  g_cia_count    = 0;
static uint8_t  g_cia_cra      = 0;
static uint8_t  g_cia_icr_mask = 0;  /* written to $DC0D: enabled sources */
static uint8_t  g_cia_icr_data = 0;  /* read from $DC0D: pending flags    */

/* VIC raster compare */
static uint16_t g_vic_raster_cmp = 0;
static uint8_t  g_vic_irq_en     = 0;
static uint8_t  g_vic_irq_data   = 0;
static uint16_t g_raster_line    = 0;
static uint32_t g_line_cycles    = 0;

static uint32_t g_irq_count   = 0;   /* published, see SH_IRQ_COUNT */
static uint32_t g_irq_runaway = 0;

static uint8_t cia_read(uint16_t a)
{
    switch (a & 0x0Fu) {
        case 0x04: return (uint8_t)(g_cia_count & 0xFFu);
        case 0x05: return (uint8_t)((g_cia_count >> 8) & 0xFFu);
        case 0x0D: {
            /* Reading $DC0D ACKNOWLEDGES: the flags clear on read. A tune
             * whose handler reads this register is relying on exactly that
             * to dismiss the interrupt, so getting it wrong means either a
             * handler that never returns or one that fires once. Bit 7 is
             * set when an enabled source is the cause. */
            uint8_t v = g_cia_icr_data;
            if (v & g_cia_icr_mask) v |= 0x80u;
            g_cia_icr_data = 0;
            return v;
        }
        case 0x0E: return g_cia_cra;
        default:   return 0;
    }
}

static void cia_write(uint16_t a, uint8_t v)
{
    switch (a & 0x0Fu) {
        case 0x04:
            g_cia_latch = (uint16_t)((g_cia_latch & 0xFF00u) | v);
            break;
        case 0x05:
            g_cia_latch = (uint16_t)((g_cia_latch & 0x00FFu) |
                                     ((uint16_t)v << 8));
            break;
        case 0x0D:
            /* Bit 7 says whether the other bits SET or CLEAR the mask. */
            if (v & 0x80u) g_cia_icr_mask |= (uint8_t)(v & 0x1Fu);
            else           g_cia_icr_mask &= (uint8_t)(~(v & 0x1Fu));
            break;
        case 0x0E:
            g_cia_cra = v;
            if (v & 0x10u) g_cia_count = (int32_t)g_cia_latch;  /* force load */
            break;
        default: break;
    }
}

/* Advance the timer and the raster by the cycles the CPU just spent. */
static void irq_tick(uint32_t cycles)
{
    if ((g_cia_cra & 0x01u) && g_cia_latch) {
        g_cia_count -= (int32_t)cycles;
        while (g_cia_count <= 0) {
            g_cia_count += (int32_t)g_cia_latch;
            g_cia_icr_data |= 0x01u;
        }
    }

    g_line_cycles += cycles;
    while (g_line_cycles >= PAL_CYCLES_PER_LINE) {
        g_line_cycles -= PAL_CYCLES_PER_LINE;
        if (++g_raster_line >= PAL_LINES_PER_FRAME) g_raster_line = 0;
        if (g_vic_irq_en && g_raster_line == g_vic_raster_cmp)
            g_vic_irq_data |= 0x01u;
        /* $D012 reads this, and some tunes poll it. */
        g_raster = (uint8_t)(g_raster_line & 0xFFu);
    }
}

static int irq_pending(void)
{
    if (g_cia_icr_data & g_cia_icr_mask)   return 1;
    if (g_vic_irq_data & g_vic_irq_en)     return 1;
    return 0;
}

/* One PAL frame of free-running CPU, with interrupts. */
static void cpu_run_irq_frame(void)
{
    uint32_t budget = PAL_CYCLES_PER_FRAME;
    uint32_t spent  = 0;
    uint32_t stall  = 0;

    while (spent < budget) {
        /* Bit 2 of the 6502 status register is I. fake6502_irq() pushes the
         * return state and vectors through $FFFE, but it does not decide
         * whether the interrupt is allowed - that is the caller's job. */
        if (irq_pending() && !(g_cpu.cpu.flags & 0x04u)) {
            fake6502_irq(&g_cpu);
            g_irq_count++;
        }

        g_cpu.emu.clockticks = 0;
        fake6502_step(&g_cpu);

        /* A jammed opcode burns no cycles, so the budget would never be
         * spent and this loop would never end. Bail instead of hanging
         * Core1, and record it - a silent hang here would look exactly
         * like the bug this code exists to fix. */
        if (g_cpu.emu.clockticks == 0) {
            if (++stall > 1000u) { g_irq_runaway++; break; }
        } else {
            stall = 0;
        }

        spent += g_cpu.emu.clockticks;
        irq_tick(g_cpu.emu.clockticks);
    }
}

/* Called at the end of sid_init_tune(), and ONLY when play == $0000.
 *
 * Everything it needs was written to C64 RAM by the tune's own init, which
 * ran before g_irq_mode was set and therefore went straight to memory the
 * ordinary way. So this reads back what the tune asked for rather than
 * guessing. */
static void irq_mode_arm(void)
{
    g_cia_latch = (uint16_t)(g_c64ram[0xDC04u] |
                             ((uint16_t)g_c64ram[0xDC05u] << 8));
    /* $4295 is the value the PSID specification names for a CIA-timed tune
     * that never sets one itself - about 60 Hz. */
    if (!g_cia_latch) g_cia_latch = 0x4295u;
    g_cia_count    = (int32_t)g_cia_latch;
    g_cia_cra      = (uint8_t)(g_c64ram[0xDC0Eu] | 0x01u);
    g_cia_icr_mask = 0x01u;
    g_cia_icr_data = 0;

    g_vic_raster_cmp = (uint16_t)(g_c64ram[0xD012u] |
                                  ((g_c64ram[0xD011u] & 0x80u) ? 0x100u : 0u));
    g_vic_irq_en   = (uint8_t)(g_c64ram[0xD01Au] & 0x01u);
    g_vic_irq_data = 0;
    g_raster_line  = 0;
    g_line_cycles  = 0;
    g_irq_count    = 0;
    g_irq_runaway  = 0;

    /* There is no KERNAL ROM here. A tune that installed $0314/$0315 is
     * expecting the KERNAL's dispatcher to read that vector, so give it
     * one: a three byte JMP ($0314) at the address the KERNAL uses, and
     * $FFFE pointed at it. Only done when the tune has not claimed $FFFE
     * for itself. */
    if (g_c64ram[0xFFFEu] == 0 && g_c64ram[0xFFFFu] == 0) {
        g_c64ram[0xFF48u] = 0x6Cu;   /* JMP ($0314) */
        g_c64ram[0xFF49u] = 0x14u;
        g_c64ram[0xFF4Au] = 0x03u;
        g_c64ram[0xFFFEu] = 0x48u;
        g_c64ram[0xFFFFu] = 0xFFu;
    }

    /* Handlers commonly end with JMP $EA31 or JMP $EA81, the KERNAL's own
     * interrupt exits. Without a ROM those land on zeroes, which the 6502
     * reads as BRK. An RTI there ends the interrupt properly instead. */
    g_c64ram[0xEA31u] = 0x40u;
    g_c64ram[0xEA7Eu] = 0x40u;
    g_c64ram[0xEA81u] = 0x40u;

    /* Between interrupts the CPU has to be executing something. A two byte
     * branch to itself costs three cycles a go and touches nothing. */
    g_c64ram[0xFFF0u] = 0x4Cu;   /* JMP $FFF0 */
    g_c64ram[0xFFF1u] = 0xF0u;
    g_c64ram[0xFFF2u] = 0xFFu;

    g_cpu.cpu.pc    = 0xFFF0u;
    g_cpu.cpu.s     = 0xFDu;
    g_cpu.cpu.flags = 0x20u;     /* I clear: interrupts are accepted */

    g_irq_mode = 1;
}

/* Memory hooks required by fake6502. */
extern "C" uint8_t fake6502_mem_read(fake6502_context *c, uint16_t address)
{
    (void)c;
    /* Ask the configured decoder, rather than limiting the SID hooks to
     * $D400-$D7FF: legal SID2 ranges also include $DE00-$DFFF. */
    {
        uint8_t reg;
        int chip = sid_find_chip(address, &reg);
        if (chip >= 0) {
            /* Some tunes use osc3/env3 ($D41B/$D41C) as noise sources. */
            return g_sid[chip] ? (uint8_t)g_sid[chip]->read(reg) : 0;
        }
    }
    /* Everything in this block is reachable only for a play == $0000 tune.
     * For every other tune the test fails and the branch order below is
     * exactly what it was. */
    if (g_irq_mode) {
        if (address >= 0xDC00u && address <= 0xDCFFu) return cia_read(address);
        if (address == 0xD019u) return (uint8_t)(g_vic_irq_data | 0x70u);
        if (address == 0xD01Au) return g_vic_irq_en;
    }
    if (address == 0xD012u) {
        /* Raster line. Must keep changing or tunes that poll it hang. */
        return g_raster;
    }
    if (address == 0xD011u) return 0x1Bu;
    return g_c64ram[address];
}

extern "C" void fake6502_mem_write(fake6502_context *c, uint16_t address, uint8_t val)
{
    (void)c;
    /* A second SID can live at $D500, $DE00 or $DF00. The SID decoder
     * already knows those windows; never bypass it in the write hook. */
    {
        uint8_t reg;
        int chip = sid_find_chip(address, &reg);
        if (chip >= 0) {
            if (reg <= 0x18u) {
                sid_write_chip((uint32_t)chip, reg, val);
                g_sid_writes++;
            }
            return;
        }
    }
    /* Same gate as the read hook: unreachable unless play == $0000. The
     * value is also kept in RAM so a tune that reads back what it wrote
     * sees it. */
    if (g_irq_mode) {
        if (address >= 0xDC00u && address <= 0xDCFFu) {
            cia_write(address, val);
            g_c64ram[address] = val;
            return;
        }
        if (address == 0xD019u) {          /* writing a 1 bit acknowledges */
            g_vic_irq_data &= (uint8_t)(~val);
            g_c64ram[address] = val;
            return;
        }
        if (address == 0xD01Au) {
            g_vic_irq_en = (uint8_t)(val & 0x01u);
            g_c64ram[address] = val;
            return;
        }
        if (address == 0xD012u) {
            g_vic_raster_cmp = (uint16_t)((g_vic_raster_cmp & 0x100u) | val);
            g_c64ram[address] = val;
            return;
        }
        if (address == 0xD011u) {
            g_vic_raster_cmp = (uint16_t)((g_vic_raster_cmp & 0x0FFu) |
                                          ((val & 0x80u) ? 0x100u : 0u));
            g_c64ram[address] = val;
            return;
        }
    }
    g_c64ram[address] = val;
}

/*
 * Call a 6502 routine and return when it does.
 *
 * Trick: push 0xFFFF as the return address, so the routine's final RTS
 * lands on 0x0000, which we use as the sentinel. A cycle ceiling stops a
 * runaway tune from wedging Core1 forever.
 */
static void cpu_call(uint16_t addr, uint8_t acc)
{
    const int CYCLE_CEILING = 250000;
    int spent = 0;

    g_cpu.cpu.pc    = addr;
    g_cpu.cpu.a     = acc;
    g_cpu.cpu.x     = 0;
    g_cpu.cpu.y     = 0;
    g_cpu.cpu.flags = 0x24u;
    g_cpu.cpu.s     = 0xFDu;
    g_c64ram[0x01FFu] = 0xFFu;   /* return address high */
    g_c64ram[0x01FEu] = 0xFFu;   /* return address low -> RTS goes to 0x0000 */

    while (spent < CYCLE_CEILING) {
        g_cpu.emu.clockticks = 0;
        fake6502_step(&g_cpu);
        spent += g_cpu.emu.clockticks;
        if (g_cpu.cpu.pc == 0x0000u) break;
    }
}

/* Copy the staged .sid bytes into C64 RAM at the load address. */
static void sid_load_tune(void)
{
    /* Read the staging buffer as 32-bit words, not bytes: only aligned
     * ULONG accesses cross the Zorro bridge reliably, and they arrive
     * byte-swapped, so ARM32() puts them back in file order. */
    volatile uint32_t *src = (volatile uint32_t *)ZZRESID_SID_ARM_ADDR;
    uint32_t load = sh_read(SH_SID_LOAD) & 0xFFFFu;
    uint32_t len  = sh_read(SH_SID_LEN);
    uint32_t i, w, words;

    if (len > ZZRESID_SID_MAX_BYTES) len = ZZRESID_SID_MAX_BYTES;
    if (load + len > 65536u) len = 65536u - load;

    for (i = 0; i < 65536u; i++) g_c64ram[i] = 0;

    words = (len + 3u) >> 2;
    for (w = 0; w < words; w++) {
        uint32_t v = ARM32(src[w]);
        uint32_t base = w << 2;
        if (base + 0 < len) g_c64ram[load + base + 0] = (uint8_t)(v >> 24);
        if (base + 1 < len) g_c64ram[load + base + 1] = (uint8_t)(v >> 16);
        if (base + 2 < len) g_c64ram[load + base + 2] = (uint8_t)(v >> 8);
        if (base + 3 < len) g_c64ram[load + base + 3] = (uint8_t)(v);
    }

    /* Publish the first word so the 68k can confirm the transfer landed. */
    sh_write(SH_SID_FIRST, ARM32(src[0]));

    g_sid_writes = 0;
    g_sid_frames = 0;
    g_tune_ready = 0;
}

/* Run the tune's init routine for the requested subsong. */
static void sid_init_tune(void)
{
    uint16_t init = (uint16_t)(sh_read(SH_SID_INIT) & 0xFFFFu);
    uint8_t  song = (uint8_t)(sh_read(SH_SID_SONG) & 0xFFu);

    {
        uint32_t k;
        for (k = 0; k < g_num_sids; k++) {
            if (!g_sid[k]) continue;
            g_sid[k]->reset();
            g_sid[k]->enable_filter(
                (g_filter_cfg & ZZRESID_FILT_INTERNAL) ? true : false);
            g_sid[k]->enable_external_filter(
                (g_filter_cfg & ZZRESID_FILT_EXTERNAL) ? true : false);
        }
    }
    /* A subtune change must not inherit the previous subtune's timing or
     * statistics: a stale cycle fraction or pending-cycle carry would make
     * the new subtune start a fraction of a frame out of step, and stale
     * counters would make the diagnostics describe two subtunes at once. */
    g_delta_frac     = 0;
    g_pending_cycles = 0;
    g_write_cycles   = 0;
    g_sid_writes     = 0;
    g_sid_frames     = 0;
    g_sid_writes_c[0] = 0;
    g_sid_writes_c[1] = 0;
    g_sid_writes_c[2] = 0;
    g_diag_min       = 32767;
    g_diag_max       = -32768;
    g_diag_min_r     = 32767;
    g_diag_max_r     = -32768;
    g_diag_clip_pos  = 0;
    g_diag_clip_neg  = 0;
    g_diag_zeropad   = 0;
    g_diag_len_mismatch = 0;
    g_probe_osc3     = 0;
    g_probe_env3     = 0;
    {
        uint32_t k, r;
        for (k = 0; k < ZZRESID_MAX_SIDS; k++)
            for (r = 0; r < 32u; r++) g_sid_shadow_n[k][r] = 0;
    }

    /* The gate goes down BEFORE init runs. The tune's init writes to
     * $DC04, $DC0D, $0314 and so on must land in plain RAM exactly as they
     * did before, because irq_mode_arm() reads them back from there. */
    g_irq_mode = 0;

    fake6502_reset(&g_cpu);
    cpu_call(init, song);

    /* A tune with no play address drives itself from its own interrupt.
     * Only such a tune reaches this. */
    if ((uint16_t)(sh_read(SH_SID_PLAY) & 0xFFFFu) == 0u)
        irq_mode_arm();

    g_tune_ready = 1;
}

/* One video frame: advance the raster, run play(). */
static void sid_play_frame(void)
{
    uint16_t play = (uint16_t)(sh_read(SH_SID_PLAY) & 0xFFFFu);
    if (play) {
        /* Unchanged. This is the path every working tune takes, and the
         * raster bump stays where it was relative to the call. */
        g_raster = (uint8_t)(g_raster + 51u);
        cpu_call(play, 0);
    } else if (g_irq_mode) {
        /* No play routine: run the C64 for a frame and let the tune's own
         * interrupt do the work. irq_tick() drives g_raster here. */
        cpu_run_irq_frame();
    }
    g_sid_frames++;
}

#endif /* ZZRESID_BUILD_C - 6502 player */

/* =========================== MAIN =========================== */
extern "C" void zzresid_core1_main(void)
{
    /* MMU + D-cache first: everything below runs cached, and the
     * mailbox/ring section is mapped Non-Cacheable. */
    mmu_init_zzresid();

    sh_write(SH_MAGIC, ZZRESID_MAGIC);
    sh_write(SH_ABI_VERSION, ZZRESID_ABI_VERSION);

    /* Publish SCTLR so the 68k can PROVE the MMU and D-cache are really on,
     * rather than us assuming it. */
    {
        uint32_t sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
        sh_write(SH_SCTLR, sctlr);
    }
    sh_write(SH_STATUS, ZZRESID_ST_INIT);
    sh_write(SH_ERROR, 0);
    sh_write(SH_HEARTBEAT, 0);
    sh_write(SH_PCM_WRITE, 0);
    sh_write(SH_CANARY_ARM, ZZRESID_CANARY_ARM);

    read_and_publish_config();
    g_mode = sh_read(SH_MODE);
    if (g_mode > ZZRESID_MODE_TUNE) g_mode = ZZRESID_MODE_TONE;
    sid_setup();
    publish_applied_model();
    g_generation++;
    sh_write(SH_GENERATION, g_generation);
    sh_write(SH_STATUS, ZZRESID_ST_READY);

    /* Wait for START */
    uint32_t hb = 0;
    while (sh_read(SH_CMD) != ZZRESID_CMD_START) {
        hb++;
        if ((hb & 0xFFFF) == 0) sh_write(SH_HEARTBEAT, hb);
        if (sh_read(SH_CMD) == ZZRESID_CMD_STOP) {
            sh_write(SH_STATUS, ZZRESID_ST_STOPPED);
            return;
        }
    }
    sh_write(SH_STATUS, ZZRESID_ST_RUNNING);

    /* Free-running render loop.
     * write_pos advances as we produce; we keep at most RING_SAMPLES
     * ahead of the 68k read cursor (backpressure). */
    uint32_t write_pos = 0;
    const uint32_t CHUNK = 256;  /* render granularity */

    for (;;) {
        uint32_t cmd = sh_read(SH_CMD);
        if (cmd == ZZRESID_CMD_STOP) break;

#if defined(ZZRESID_BUILD_C)
        if (cmd == ZZRESID_CMD_LOAD_SID) {
            sid_load_tune();
            sh_write(SH_SID_WRITES, 0);
            sh_write(SH_SID_FRAMES, 0);
            sh_write(SH_CMD, ZZRESID_CMD_NONE);
            continue;
        }

        if (cmd == ZZRESID_CMD_SID_INIT) {
            sid_init_tune();
            g_mode = ZZRESID_MODE_TUNE;
            /* Start the ring fresh so playback begins at the tune's start. */
            write_pos = sh_read(SH_PCM_READ);
            sh_write(SH_PCM_WRITE, write_pos);
            sh_write(SH_SID_WRITES, g_sid_writes);
            sh_write(SH_SID_FRAMES, g_sid_frames);
            sh_write(SH_IRQ_COUNT,  g_irq_count);
            sh_write(SH_CMD, ZZRESID_CMD_NONE);
            continue;
        }
#endif

        if (cmd == ZZRESID_CMD_RECONFIG) {
            /* A new launcher run wants different settings on this already
             * resident blob. Re-read config and rebuild reSID state. */
            read_and_publish_config();
            g_mode = sh_read(SH_MODE);
            if (g_mode > ZZRESID_MODE_TUNE) g_mode = ZZRESID_MODE_TONE;
            sid_setup();
    publish_applied_model();
            g_delta_frac = 0;
            g_overruns   = 0;
#if defined(ZZRESID_BUILD_C)
            g_pending_cycles = 0;
            g_probe_osc3     = 0;
            g_probe_env3     = 0;
            g_diag_min_r     = 32767;
            g_diag_max_r     = -32768;
            g_diag_len_mismatch = 0;
            g_sid_writes_c[0] = 0;
            g_sid_writes_c[1] = 0;
            g_sid_writes_c[2] = 0;
            g_diag_min       = 32767;
            g_diag_max       = -32768;
            g_diag_clip_pos  = 0;
            g_diag_clip_neg  = 0;
#endif
            /* Resync the write cursor to whatever the 68k has just set as
             * its read cursor, so the ring starts empty for the new run. */
            write_pos = sh_read(SH_PCM_READ);
            sh_write(SH_PCM_WRITE, write_pos);
            sh_write(SH_STATUS, ZZRESID_ST_RUNNING);
            /* Bump generation LAST: the 68k treats a changed generation as
             * proof that the reconfigure was really applied. */
            g_generation++;
            sh_write(SH_GENERATION, g_generation);
            sh_write(SH_CMD, ZZRESID_CMD_NONE);
            continue;
        }

#if defined(ZZRESID_BUILD_C)
        if (cmd == ZZRESID_CMD_RESET) {
            {
                uint32_t k;
                for (k = 0; k < g_num_sids; k++) {
                    if (!g_sid[k]) continue;
                    g_sid[k]->reset();
                    g_sid[k]->enable_filter(
                        (g_filter_cfg & ZZRESID_FILT_INTERNAL) ? true : false);
                    g_sid[k]->enable_external_filter(
                        (g_filter_cfg & ZZRESID_FILT_EXTERNAL) ? true : false);
                }
            }
            g_delta_frac = 0;
        }
        uint32_t regc = sh_read(SH_REG_COUNT);
        if (regc) { apply_reg_batch(regc); sh_write(SH_REG_COUNT, 0); }
#endif

        /* In TUNE mode we render whole PAL frames, because the tune's
         * play() routine must run exactly once per frame. In TONE mode we
         * keep the small free-running chunks. */
        uint32_t want = CHUNK;
#if defined(ZZRESID_BUILD_C)
        uint32_t frame_samples = g_sample_rate / ZZRESID_PAL_FPS;
        if (g_mode == ZZRESID_MODE_TUNE && g_tune_ready) want = frame_samples;
#endif

        /* Backpressure: don't overrun the 68k read cursor */
        uint32_t read_pos = sh_read(SH_PCM_READ);
        uint32_t used = write_pos - read_pos;   /* wraps correctly */
        if (used + want > ZZRESID_RING_SAMPLES) {
            /* ring full: spin briefly, let 68k drain */
            g_overruns++;
            /* No consumer draining: nap longer so an idle resident blob
             * does not spin Core1 at 100% (thermal). */
            for (volatile int d = 0; d < 20000; d++) { }
            continue;
        }

#if defined(ZZRESID_BUILD_C)
        /* One play() call, then exactly one frame of samples. The cycle
         * budget inside render_into_ring works out to the 19705 PAL cycles
         * a frame is worth, so CPU and SID stay in step. */
        if (g_mode == ZZRESID_MODE_TUNE && g_tune_ready) {
            sid_play_frame();
            sh_write(SH_SID_WRITES, g_sid_writes);
            sh_write(SH_SID_FRAMES, g_sid_frames);
            /* Gated. This is the render loop - it runs for every tune, every
             * frame, and sh_write() ends in a dsb(). Publishing an interrupt
             * count here unconditionally added a full data barrier fifty
             * times a second to tunes that have no interrupts at all, which
             * is precisely the kind of "harmless" addition to the common
             * path this project has been bitten by twice. */
            if (g_irq_mode) {
                sh_write(SH_IRQ_COUNT,   g_irq_count);
                sh_write(SH_IRQ_RUNAWAY, g_irq_runaway);
                sh_write(SH_IRQ_PC,      (uint32_t)g_cpu.cpu.pc);
            }
            for (uint32_t r = 0; r < ZZRESID_SID_REGS; r++)
                sh_write(SH_SID_SHADOW_BASE + r, g_sid_shadow_n[0][r]);
        }
#endif

        uint32_t before_pos = write_pos;
        uint32_t n = render_into_ring(write_pos, want);
        write_pos += n;                     /* only advance by real output */
        sh_write(SH_PCM_WRITE, write_pos);

#if defined(ZZRESID_BUILD_C)
        /* Publish render diagnostics */
        sh_write(SH_DIAG_CAPACITY,   g_diag_capacity);
        sh_write(SH_DIAG_DELTA_FED,  g_diag_delta_fed);
        sh_write(SH_DIAG_GOT_LAST,   g_diag_got_last);
        sh_write(SH_DIAG_PRODUCED,   g_diag_produced);
        sh_write(SH_DIAG_ZEROPAD,    g_diag_zeropad);
        sh_write(SH_DIAG_PENDING,    (uint32_t)g_pending_cycles);
        sh_write(SH_DIAG_FRAC_FP,    g_delta_frac);
        sh_write(SH_DIAG_CLOCKCALLS, g_diag_clockcalls);
        sh_write(SH_DIAG_MIN_SAMPLE, (uint32_t)g_diag_min);
        sh_write(SH_DIAG_MAX_SAMPLE, (uint32_t)g_diag_max);
        sh_write(SH_DIAG_CLIP_POS,   g_diag_clip_pos);
        sh_write(SH_DIAG_CLIP_NEG,   g_diag_clip_neg);
        /* Spare slots inside the existing 32-word shadow block: the block
         * holds 25 registers, so these move nothing and need no ABI bump. */
        sh_write(SH_SID_SHADOW_BASE + 25, g_probe_osc3);
        sh_write(SH_SID_SHADOW_BASE + 26, g_probe_env3);
        sh_write(SH_ACTIVE_NUM_SIDS,   g_num_sids);
        sh_write(SH_SID_WRITES1,       g_sid_writes_c[0]);
        sh_write(SH_SID_WRITES2,       g_sid_writes_c[1]);
        sh_write(SH_DIAG_MIN_R,        (uint32_t)g_diag_min_r);
        sh_write(SH_DIAG_MAX_R,        (uint32_t)g_diag_max_r);
        sh_write(SH_WRITE_CYCLES,      g_write_cycles);
        sh_write(SH_DIAG_LEN_MISMATCH, g_diag_len_mismatch);
        sh_write(SH_DIAG_RING_BEFORE, before_pos);
        sh_write(SH_DIAG_RING_AFTER,  write_pos);
        sh_write(SH_DIAG_OVERRUNS,   g_overruns);
        /* cycles actually consumed by reSID for this chunk */
        sh_write(SH_RENDER_CYCLES,
                 g_diag_delta_fed - (uint32_t)g_pending_cycles);
#else
        (void)before_pos;
#endif

        hb++;
        if ((hb & 0x3FF) == 0) sh_write(SH_HEARTBEAT, hb);
    }

    /* Publish the heap figures before we go: a non-zero fail count is the
     * signature of an allocation the blob refused rather than corrupting
     * the mailbox with. Slot 31 was reserved, so no ABI change. */
    sh_write(SH_DIAG_RESERVED31, (g_heap_peak & 0x00FFFFFFu) |
                                 ((g_heap_fail & 0xFFu) << 24));
    sh_write(SH_STATUS, ZZRESID_ST_STOPPED);
    dsb();

    /* Hand Core1 back. The D-cache must be clean while the MMU is still on;
     * startup.S restores the firmware's CP15 state and returns from here. */
    cache_clean_invalidate_all();
}
