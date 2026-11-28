/*
 * zzs_msm6295.c - OKI MSM6295 (4-voice ADPCM) for ZZStrider. Replaces ZZRastan's MSM5205.
 *
 * Behaviour follows MAME's okim6295.cpp and okiadpcm.cpp (BSD-3-Clause, see
 * licenses/MAME-BSD-3-Clause.txt):
 *     Copyright (c) Mirko Buffoni, Aaron Giles      (okim6295)
 *     Copyright (c) Andrew Gardner, Aaron Giles     (okiadpcm)
 *   - sample rate = clock / 132 (pin 7 high) or clock / 165 (pin 7 low)
 *   - phrase table: 8 bytes per phrase at phrase * 8: 3 bytes start, 3 bytes stop (18 bits)
 *   - command: 1xxxxxxx selects a phrase, the next byte is vvvvaaaa (voice mask, attenuation);
 *              0vvvv000 stops voices; a voice that is still playing ignores a start
 *   - status : 0xF0 | one bit per playing voice
 *   - ADPCM  : 49 steps of floor(16 * 1.1^n), 12-bit signed output, index shift -1,-1,-1,-1,2,4,6,8
 *   - volume : 0x20,0x16,0x10,0x0b,0x08,0x06,0x04,0x03,0x02 (/32), silent above index 8
 * The chip runs at its own rate; the output is brought to the PCM rate by linear interpolation
 * between successive chip samples (the board filters the DAC output; MAME resamples its stream).
 *
 * ASCII only.
 */
#include "zzs_msm6295.h"

#define OKI_VOICES 4

typedef struct oki_voice_s {
    int          playing;
    unsigned int base;          /* start address in the sample ROM */
    unsigned int sample;        /* nibble counter */
    unsigned int count;         /* nibbles to play */
    int          signal;        /* ADPCM accumulator, -2048..2047 */
    int          step;          /* 0..48 */
    int          volume;        /* 0..32 */
} oki_voice_t;

static const signed char g_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const unsigned char g_volume_table[16] = {
    0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03, 0x02, 0, 0, 0, 0, 0, 0, 0
};
static int g_diff_lookup[49 * 16];
static int g_tables_ready;

static oki_voice_t g_voice[OKI_VOICES];
static const unsigned char *g_rom;
static unsigned int g_rom_mask;
static unsigned int g_clock;
static int          g_pin7;
static int          g_command;          /* -1 = none pending */
static unsigned int g_out_rate;
static unsigned int g_divisor;          /* 132 or 165 */
/* chip sample period in units of 1/(clock) s = divisor; output period = clock/out_rate.
   Phase accumulator: acc += clock per output sample; a chip sample every divisor * out_rate. */
static unsigned int g_acc, g_period;
static int          g_prev, g_cur;      /* last two chip-rate mixes */

unsigned int g_oki_writes, g_oki_starts, g_oki_rejects;

static void compute_tables(void)
{
    static const signed char nbl2bit[16][4] = {
        { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
        { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
    };
    /* floor(16 * (11/10)^step), step 0..48 - the classic OKI table, kept as integers so that
       no floating point is needed here */
    static const short stepval_table[49] = {
         16,  17,  19,  21,  23,  25,  28,  31,  34,  37,  41,  45,  50,  55,  60,  66,
         73,  80,  88,  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,1060,1166,1282,1411,
       1552
    };
    int step, nib;
    for (step = 0; step <= 48; step++) {
        int stepval = stepval_table[step];
        for (nib = 0; nib < 16; nib++) {
            g_diff_lookup[step * 16 + nib] = nbl2bit[nib][0] *
                (stepval     * nbl2bit[nib][1] +
                 stepval / 2 * nbl2bit[nib][2] +
                 stepval / 4 * nbl2bit[nib][3] +
                 stepval / 8);
        }
    }
    g_tables_ready = 1;
}

static void update_rate(void)
{
    g_divisor = g_pin7 ? 132u : 165u;
    g_period = g_divisor * g_out_rate;
    if (g_acc >= g_period)
        g_acc = 0;
}

void zzs_oki_reset(void)
{
    int v;
    for (v = 0; v < OKI_VOICES; v++) {
        g_voice[v].playing = 0;
        g_voice[v].base = 0;
        g_voice[v].sample = 0;
        g_voice[v].count = 0;
        g_voice[v].signal = 0;
        g_voice[v].step = 0;
        g_voice[v].volume = 0;
    }
    g_command = -1;
    g_acc = 0;
    g_prev = g_cur = 0;
}

void zzs_oki_init(const unsigned char *rom, unsigned int rom_size, unsigned int clock_hz,
                  int pin7_high, unsigned int out_rate)
{
    if (!g_tables_ready)
        compute_tables();
    g_rom = rom;
    g_rom_mask = rom_size - 1u;         /* 0x40000: power of two */
    g_clock = clock_hz;
    g_pin7 = pin7_high ? 1 : 0;
    g_out_rate = out_rate;
    g_oki_writes = g_oki_starts = g_oki_rejects = 0;
    zzs_oki_reset();
    update_rate();
}

void zzs_oki_set_pin7(int pin7_high)
{
    g_pin7 = pin7_high ? 1 : 0;
    update_rate();
}

unsigned char zzs_oki_read(void)
{
    unsigned char result = 0xF0;
    int v;
    for (v = 0; v < OKI_VOICES; v++)
        if (g_voice[v].playing)
            result |= (unsigned char)(1u << v);
    return result;
}

void zzs_oki_write(unsigned char command)
{
    int v;
    g_oki_writes++;

    if (g_command != -1) {
        /* second half: voice mask in the high nibble, attenuation in the low nibble */
        unsigned int voicemask = (unsigned int)command >> 4;
        for (v = 0; v < OKI_VOICES; v++, voicemask >>= 1) {
            if (voicemask & 1u) {
                oki_voice_t *voice = &g_voice[v];
                if (!voice->playing) {
                    unsigned int base = (unsigned int)g_command * 8u;
                    unsigned int start, stop;
                    start  = (unsigned int)g_rom[(base + 0u) & g_rom_mask] << 16;
                    start |= (unsigned int)g_rom[(base + 1u) & g_rom_mask] << 8;
                    start |= (unsigned int)g_rom[(base + 2u) & g_rom_mask];
                    start &= 0x3FFFFu;
                    stop   = (unsigned int)g_rom[(base + 3u) & g_rom_mask] << 16;
                    stop  |= (unsigned int)g_rom[(base + 4u) & g_rom_mask] << 8;
                    stop  |= (unsigned int)g_rom[(base + 5u) & g_rom_mask];
                    stop  &= 0x3FFFFu;
                    if (start < stop) {
                        voice->playing = 1;
                        voice->base = start;
                        voice->sample = 0;
                        voice->count = 2u * (stop - start + 1u);
                        voice->signal = 0;
                        voice->step = 0;
                        voice->volume = g_volume_table[command & 0x0F];
                        g_oki_starts++;
                    } else {
                        g_oki_rejects++;        /* invalid phrase */
                    }
                } else {
                    g_oki_rejects++;            /* voice busy: the start is ignored */
                }
            }
        }
        g_command = -1;
    } else if (command & 0x80) {
        g_command = command & 0x7F;
    } else {
        unsigned int voicemask = (unsigned int)command >> 3;
        for (v = 0; v < OKI_VOICES; v++, voicemask >>= 1)
            if (voicemask & 1u)
                g_voice[v].playing = 0;
    }
}

/* advance every playing voice by one chip sample and return the mix */
static int chip_clock(void)
{
    int v, mix = 0;
    for (v = 0; v < OKI_VOICES; v++) {
        oki_voice_t *voice = &g_voice[v];
        unsigned int nibble;
        if (!voice->playing)
            continue;
        nibble = (unsigned int)g_rom[(voice->base + voice->sample / 2u) & g_rom_mask]
                 >> (((voice->sample & 1u) << 2) ^ 4u);
        nibble &= 15u;
        voice->signal += g_diff_lookup[voice->step * 16 + (int)nibble];
        if (voice->signal > 2047) voice->signal = 2047;
        else if (voice->signal < -2048) voice->signal = -2048;
        voice->step += g_index_shift[nibble & 7u];
        if (voice->step > 48) voice->step = 48;
        else if (voice->step < 0) voice->step = 0;
        mix += voice->signal * voice->volume;
        if (++voice->sample >= voice->count)
            voice->playing = 0;
    }
    return mix;
}

void zzs_oki_render(int *dst, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; i++) {
        g_acc += g_clock;
        while (g_acc >= g_period) {
            g_acc -= g_period;
            g_prev = g_cur;
            g_cur = chip_clock();
        }
        /* linear interpolation prev -> cur, in 32 bits: |cur - prev| <= 2^19 (4 voices of
           +/-2048 x 32), fraction on 11 bits -> the product stays below 2^31 */
        {
            int frac = (int)((g_acc >> 8) * 2048u / ((g_period >> 8) ? (g_period >> 8) : 1u));
            dst[i] = g_prev + (((g_cur - g_prev) * frac) >> 11);
        }
    }
}
