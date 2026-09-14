/* Hunk allocation diagnostics. */














#include <stdlib.h>
#include "quakedef.h"
#include "zzquake_config.h"

typedef unsigned int u32;
extern volatile u32 *shared;

/* Cache/MMU handling follows the validated Core1 memory contract. */

#define ZZQ_ARGV_MAX 12
static char *zzq_argv[ZZQ_ARGV_MAX];

void QG_Create(int argc, char *argv[])
{
    static quakeparms_t parms;

    /* Hunk size driven by the launcher so we can bisect without a
       rebuild. 0 = the upstream default of 8 MB (the size that got
       us to 9 frames). */
    {
        u32 mb = shared[SH_HUNK_MB];
        if (mb == 0) mb = 8;
        if (mb > 14) mb = 14;          /* heap is 16 MB total */
        parms.memsize = (int)(mb * 1024u * 1024u);
    }
    

    { extern void zzq_diag_stage_d(void); zzq_diag_stage_d(); }
    parms.membase = malloc(parms.memsize);
    parms.basedir = ".";

    /* Publish what we really got. A NULL membase used to walk straight
       into Memory_Init(NULL, ...) and corrupt everything silently. */
    shared[SH_HUNK_SIZE] = (u32)parms.memsize;
    shared[SH_HUNK_BASE] = (u32)(unsigned long)parms.membase;

    if (!parms.membase) {
        shared[SH_ERROR] = ZZQ_FATAL_OUT_OF_MEM;
        Sys_Error("ZZQ: hunk malloc failed (%d bytes)", parms.memsize);
        return;                     /* not reached */
    }

    /* Cache/MMU handling follows the validated Core1 memory contract. */





    {
        static char scbuf[16];
        u32 kb = shared[SH_SURFCACHE_KB];
        int an = 0, ai;
        





        for (ai = 0; ai < argc && an < ZZQ_ARGV_MAX - 6; ai++)
            zzq_argv[an++] = argv[ai];
        if (an == 0) zzq_argv[an++] = "zzquake";
        if (kb) {
            u32 v = kb; int n = 0, i;
            char tmp[12];
            if (v > 8192u) v = 8192u;          /* garde-fou heap */
            if (v == 0) v = 1;
            while (v > 0 && n < 11) { tmp[n++] = (char)('0'+(v%10)); v/=10; }
            for (i = 0; i < n; i++) scbuf[i] = tmp[n-1-i];
            scbuf[n] = 0;
            zzq_argv[an++] = "-surfcachesize";
            zzq_argv[an++] = scbuf;
        }
        if (shared[SH_ENABLE_TIMEDEMO]) {
            


            zzq_argv[an++] = "+timedemo";
            zzq_argv[an++] = "demo1";
        }
        

        zzq_argv[an] = 0; argc = an; argv = zzq_argv;
    }

    COM_InitArgv(argc, argv);
    parms.argc = com_argc;
    parms.argv = com_argv;

    zzq_float_printf_test();        /* before anything uses cvars */
    shared[SH_DIAG] = 0xA021;       /* about to enter Host_Init */
    Host_Init(&parms);
    shared[SH_DIAG] = 0xA022;       /* Host_Init returned */
}

void QG_Tick(double duration)
{
    Host_Frame(duration);
}

/* Cache/MMU handling follows the validated Core1 memory contract. */



void zzq_publish_engine_state(void)
{
    shared[SH_SV_ACTIVE]    = (u32)sv.active;
    shared[SH_DEMOPLAYBACK] = (u32)cls.demoplayback;
    shared[SH_DEMONUM]      = (u32)cls.demonum;

    /* Hunk allocation diagnostics. */






    {
        extern cvar_t r_maxsurfs, r_maxedges;
        extern int    r_cnumsurfs;
        shared[SH_CV_MAXSURFS]  = (u32)(int)r_maxsurfs.value;
        shared[SH_CV_MAXEDGES]  = (u32)(int)r_maxedges.value;
        shared[SH_CV_CNUMSURFS] = (u32)r_cnumsurfs;
    }
}

/* ---- hooks used by the patched engine copies ---- */
int zzq_alias_stage = 0;





double zzq_min_frametime(void)
{
    u32 f = shared[SH_ENGINE_MAXFPS];
    if (f == 0) return 0.0;          
    return 1.0 / (double)f;
}




void zzq_note_command(const char *text)
{
    int i, b = 0, k;
    shared[SH_CMD_COUNT]++;
    for (i = 0; i < SH_LAST_CMD_LEN; i++) {
        u32 w = 0;
        for (k = 0; k < 4; k++) {
            unsigned char ch = 0;
            if (text && text[b] && text[b] != '\n') { ch = (unsigned char)text[b]; b++; }
            w |= ((u32)ch) << (k * 8);
        }
        shared[SH_LAST_CMD + i] = w;
    }
}

void zzq_surfcache_got(int bytes)
{
    shared[SH_SURFCACHE_GOT] = (u32)bytes;
}

/* Timedemo results are exported to the launcher. */

void zzq_timedemo_result(int frames, float seconds)
{
    long ms = (long)(seconds * 1000.0f);
    if (ms < 1) ms = 1;
    shared[SH_TD_FRAMES]  = (u32)frames;
    shared[SH_TD_TIME_MS] = (u32)ms;
    shared[SH_TD_FPS_X100]= (u32)(((long)frames * 100000L) / ms);
    shared[SH_TD_DONE]    = 1;
}

/* Replacement for sprintf("%f") inside Cvar_SetValue. This newlib
   multilib formats variadic floats wrongly (measured on hardware:
   sprintf("%f", 800.0) yields "-0.000000"), which turned every
   numeric cvar into 0. We format by hand, no variadic float. */
void zzq_ftoa_prec(char *dst, int cap, float v, int prec)
{
    int neg = 0, i = 0, j, n, k;
    long ip, fp, scale = 1;
    char tmp[16];
    if (cap < 4) { if (cap > 0) dst[0] = 0; return; }
    if (prec < 0) prec = 0;
    if (prec > 6) prec = 6;
    for (k = 0; k < prec; k++) scale *= 10;
    if (v != v) { dst[0]='n'; dst[1]='a'; dst[2]='n'; dst[3]=0; return; }
    if (v < 0.0f) { neg = 1; v = -v; }
    if (v > 2000000000.0f) v = 2000000000.0f;
    ip = (long)v;
    fp = (long)((v - (float)ip) * (float)scale + 0.5f);
    if (fp >= scale) { fp = 0; ip++; }
    if (neg) dst[i++] = '-';
    n = 0;
    if (ip == 0) tmp[n++] = '0';
    while (ip > 0 && n < 15) { tmp[n++] = (char)('0' + (ip % 10)); ip /= 10; }
    for (j = n - 1; j >= 0 && i < cap - 2; j--) dst[i++] = tmp[j];
    if (prec > 0 && i < cap - 2) {
        dst[i++] = '.';
        for (j = prec - 1; j >= 0 && i < cap - 1; j--) {
            long d = fp;
            for (k = 0; k < j; k++) d /= 10;
            dst[i++] = (char)('0' + (d % 10));
        }
    }
    dst[i] = 0;                 /* ALWAYS terminated - the missing
                                   terminator from the broken %f was
                                   what sent strlen() and the console
                                   version string into the weeds. */
}

void zzq_ftoa(char *dst, int cap, float v)
{
    zzq_ftoa_prec(dst, cap, v, 6);
}

void zzq_hunk_fail(const char *name, int raw, int adj,
                   int low, int high, int total)
{
    int i, b = 0, k;
    for (i = 0; i < 4; i++) {
        u32 w = 0;
        for (k = 0; k < 4; k++) {
            unsigned char c = 0;
            if (name && name[b]) { c = (unsigned char)name[b]; b++; }
            w |= ((u32)c) << (k * 8);
        }
        shared[SH_HUNKF_NAME + i] = w;
    }
    shared[SH_HUNKF_RAW]   = (u32)raw;
    shared[SH_HUNKF_ADJ]   = (u32)adj;
    shared[SH_HUNKF_LOW]   = (u32)low;
    shared[SH_HUNKF_HIGH]  = (u32)high;
    shared[SH_HUNKF_SIZE]  = (u32)total;
    shared[SH_ALIAS_STAGE] = (u32)zzq_alias_stage;
}

int zzq_cvarfix_enabled(void)
{
    return (int)shared[SH_ENABLE_CVARFIX];
}

void zzq_cvar_snapshot(int which)
{
    extern cvar_t r_maxsurfs, r_maxedges;
    union { float f; u32 u; } cs, ce;
    cs.f = r_maxsurfs.value;
    ce.f = r_maxedges.value;
    if (which == 1) {
        int i, b = 0, k;
        const char *s = r_maxsurfs.string;
        for (i = 0; i < 4; i++) {
            u32 w = 0;
            for (k = 0; k < 4; k++) {
                unsigned char c = 0;
                if (s && s[b]) { c = (unsigned char)s[b]; b++; }
                w |= ((u32)c) << (k * 8);
            }
            shared[SH_CV1_SURFS_STR + i] = w;
        }
        shared[SH_CV1_SURFS_RAW] = cs.u;
        shared[SH_CV1_EDGES_RAW] = ce.u;
    } else {
        shared[SH_CV2_SURFS_RAW] = cs.u;
        shared[SH_CV2_EDGES_RAW] = ce.u;
    }
}

/* Does sprintf("%f") work on this target? Cvar_SetValue depends on
   it entirely. Buffer pre-filled with 0xA5 so an untouched buffer is
   obvious, and we publish sprintf's return value too. */
void zzq_float_printf_test(void)
{
    char buf[32];
    int i, n;
    for (i = 0; i < 32; i++) buf[i] = (char)0xA5;
    n = sprintf(buf, "%f", 800.0);
    shared[SH_FTEST_RET] = (u32)n;
    buf[31] = 0;
    for (i = 0; i < 4; i++)
        shared[SH_FTEST_STR + i] =
            ((u32)(unsigned char)buf[i*4+0])       |
            ((u32)(unsigned char)buf[i*4+1] << 8)  |
            ((u32)(unsigned char)buf[i*4+2] << 16) |
            ((u32)(unsigned char)buf[i*4+3] << 24);
    shared[SH_FTEST_BACK] = (u32)(int)Q_atof(buf);
}
