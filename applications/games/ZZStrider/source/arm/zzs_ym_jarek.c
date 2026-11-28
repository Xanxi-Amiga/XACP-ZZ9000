/*
 * zzs_ym_jarek.c - YM2151 backend using the Jarek Burczynski core (via FBNeo's ym2151.c).
 *
 * ZZStrider: this is zzr_ym_jarek.c from ZZRastan 1.0 with three changes:
 *   - the chip clock comes from zzs_game.h (3.579545 MHz on CPS-1, was 4 MHz on Rastan);
 *   - zzr_jarek_clocks_to_next() lets the sound board run the Z80 exactly up to the next
 *     timer overflow instead of polling in fixed slices;
 *   - the IRQ counter that lived in the C++ ymfm wrapper is kept here (ymfm is not built).
 * The original ZZRastan header follows.
 *
 * ---------------------------------------------------------------------------------------
 * zzr_ym_jarek.c - YM2151 backend using the Jarek Burczynski core (via FBNeo's ym2151.c).
 *
 * Replaces ymfm, which measured 16.5 ms/frame on this Cortex-A9 - the entire 60 Hz budget.
 * The Jarek core takes clock and output rate SEPARATELY (YM2151Init(num, base, clock, rate, cb)),
 * so asking for 22050 Hz is not "running the chip slowly": it recomputes its envelope/LFO/phase
 * increments from clock/rate. That also removes our decimator entirely.
 *
 * Deliberately NOT used from this core (our own validated versions stay in charge):
 *   - status/timers  : we keep zzr_ym_read_status() and our Timer A/B + Z80 IRQ path
 *   - port handler   : reg 0x1B bankswitch stays in zzrastan_sound.c (avoid double switching)
 *   - IRQ handler    : the Z80 keeps our proven IRQ line
 * Same plain C API as the ymfm wrapper, so zzrastan_sound.c is unchanged.
 * ASCII only.
 */
#include "ym2151.h"
#include "zzs_game.h"

/* --- J1b: Jarek is the SINGLE authority for timers, status and IRQ ---------------------------
 * We keep only the 4 MHz emulated clock, the two absolute deadlines, and the dispatch that calls
 * ym2151_timer_over() when one expires. That function does everything else inside the core:
 * reprogram the next period through our callback, set the status flag, apply the IRQ enable and
 * call the IRQ handler. Nothing here sets status bits or drives the IRQ by hand. */
#define ZZR_YM_CLOCK ZZS_YM_CLOCK

extern void zzr_z80_set_irq(int level);          /* implemented in zzs_sound.c */
static unsigned int g_irq_asserts, g_irq_clears;
static void zzr_ym_count_irq(int level) { if (level) g_irq_asserts++; else g_irq_clears++; }

static unsigned long long g_ym_clock;            /* monotonic, in chip clocks */
static unsigned long long g_deadline[2];
static int g_active[2];
static unsigned int g_sets[2], g_fires[2], g_period[2];

static void zzr_jarek_timer_cb(INT32 chip, INT32 timer, double period)
{
    (void)chip;
    if (timer < 0 || timer > 1) return;
    if (period <= 0.0) { g_active[timer] = 0; return; }
    {
        unsigned long long clocks =
            (unsigned long long)(period * (double)ZZR_YM_CLOCK + 0.5);
        g_deadline[timer] = g_ym_clock + clocks;
        g_active[timer] = 1;
        g_sets[timer]++;
        g_period[timer] = (unsigned int)clocks;
    }
}

static void zzr_jarek_irq_cb(int level)
{
    zzr_ym_count_irq(level);
    zzr_z80_set_irq(level);
}

/* Advance emulated time; fire every deadline inside the slice, earliest first. Re-arm happens
   inside ym2151_timer_over() via the callback, so this must tolerate re-entrancy. */
int zzr_jarek_advance(unsigned int clocks)
{
    unsigned long long end = g_ym_clock + (unsigned long long)clocks;
    int guard = 0;
    for (;;) {
        int best = -1, t;
        unsigned long long best_dl = 0;
        for (t = 0; t < 2; t++) {
            if (!g_active[t]) continue;
            if (g_deadline[t] > end) continue;
            if (best < 0 || g_deadline[t] < best_dl) { best = t; best_dl = g_deadline[t]; }
        }
        if (best < 0) break;
        g_ym_clock = best_dl;
        g_active[best] = 0;
        g_fires[best]++;
        ym2151_timer_over(0, best);
        if (++guard > 64) break;
    }
    g_ym_clock = end;
    return 0;
}

/* Chip clocks until the earliest armed timer overflows; 0 = no timer armed.
   The Z80 and the YM2151 share one crystal on CPS-1, so this is also a Z80 cycle count. */
unsigned int zzr_jarek_clocks_to_next(void)
{
    unsigned long long best = 0;
    int t, have = 0;
    for (t = 0; t < 2; t++) {
        if (!g_active[t]) continue;
        if (!have || g_deadline[t] < best) { best = g_deadline[t]; have = 1; }
    }
    if (!have) return 0;
    if (best <= g_ym_clock) return 1;
    if (best - g_ym_clock > 0x7FFFFFFFull) return 0x7FFFFFFFu;
    return (unsigned int)(best - g_ym_clock);
}

unsigned int zzr_jarek_stat(unsigned int i)
{
    switch (i) {
        case 0: return g_sets[0];  case 1: return g_sets[1];
        case 2: return g_fires[0]; case 3: return g_fires[1];
        case 4: return g_period[0];case 5: return g_period[1];
        case 6: return g_irq_asserts; case 7: return g_irq_clears;
        default: return 0;
    }
}

unsigned char zzr_jarek_status(void) { return (unsigned char)YM2151ReadStatus(0); }

#define ZZR_YM_RATE 22050

static int g_inited;

void zzr_jarek_reset(void)
{
    g_ym_clock = 0; g_active[0] = g_active[1] = 0;
    g_sets[0] = g_sets[1] = g_fires[0] = g_fires[1] = 0;
    g_irq_asserts = g_irq_clears = 0;
    if (!g_inited) {
        YM2151Init(1, 0, ZZR_YM_CLOCK, ZZR_YM_RATE, zzr_jarek_timer_cb);
        YM2151SetIrqHandler(0, zzr_jarek_irq_cb);
        g_inited = 1;
    }
    YM2151ResetChip(0);
}


/* The address latch lives in zzrastan_sound.c (it also drives our instrumentation), so we only
   need to remember it here to feed YM2151WriteReg(reg, value). */
static unsigned char g_addr;
void zzr_jarek_write_addr(unsigned char v) { g_addr = v; }
void zzr_jarek_write_data(unsigned char v) { YM2151WriteReg(0, g_addr, v); }

/* Status stays ours (proven): zzrastan_sound.c calls zzr_ym_read_status() which is implemented
   by the timer layer, not by this core. */

/* Native rate == output rate now, so the caller's decimator becomes a 1:1 copy. */


void zzr_jarek_generate(short *dst, unsigned int count)
{
    /* The core writes one buffer per output channel; Rastan is mono for us, so we render into a
       single buffer and let both "channels" point at it is NOT valid (it would sum twice), so we
       render L and R separately and average, matching what we did with ymfm. */
    static INT16 lbuf[256], rbuf[256];
    INT16 *bufs[2];
    unsigned int done = 0;
    while (done < count) {
        unsigned int n = count - done, i;
        if (n > 256u) n = 256u;
        bufs[0] = lbuf; bufs[1] = rbuf;
        YM2151UpdateOne(0, bufs, (int)n);
        for (i = 0; i < n; i++) {
            int v = ((int)lbuf[i] + (int)rbuf[i]) >> 1;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            dst[done + i] = (short)v;
        }
        done += n;
    }
}
