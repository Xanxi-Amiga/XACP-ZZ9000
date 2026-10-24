/* Shared-memory protocol state. */













#include <stdarg.h>
#include <stdio.h>     /* vsnprintf (newlib, string-only, safe) */
#include "zzquake_config.h"
#include "quakegeneric.h"
#include "quakekeys.h"

typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned char      u8;
typedef unsigned long long u64;

extern volatile u32 *shared;               /* zzquake_main.c */





static u32 g_work_t0 = 0;
static u32 g_last_render_seq = 0, g_gate_pass = 0;
static u32 g_gate_block = 0, g_fb_rejects = 0;
static u32 g_renders = 0, g_heavy = 0;
static u32 g_hist[ZZQ_HIST_BINS];
static u32 g_keyq_rd = 0;
typedef signed int s32;
typedef signed short s16;
typedef unsigned char u8;
static s32 g_mouse_last_x = 0, g_mouse_last_y = 0;
void zzq_publish_stats(void);



static u32 g_d1_sum_ms = 0, g_d2_sum_ms = 0, g_poll_sum_k = 0;
static u32 g_dn_count = 0, g_d1_max = 0, g_d2_max = 0;
static u32 g_d1_last = 0, g_d2_last = 0, g_poll_last = 0, g_wait_last = 0;
static u32 g_work_last = 0, g_work_max = 0;
static u32 g_dbwait_last = 0, g_dbwait_max = 0;
static u32 g_dbwaits = 0, g_dbtmo = 0;
static u32 g_fps_peak = 0;

void zzq_publish_stats(void)
{
    shared[SH_D1_SUM_MS]   = g_d1_sum_ms;
    shared[SH_D2_SUM_MS]   = g_d2_sum_ms;
    shared[SH_POLL_SUM_K]  = g_poll_sum_k;
    shared[SH_DN_COUNT]    = g_dn_count;
    shared[SH_D1_MAX]      = g_d1_max;
    shared[SH_D2_MAX]      = g_d2_max;
    shared[SH_D1_US]       = g_d1_last;
    shared[SH_D2_US]       = g_d2_last;
    shared[SH_POLL_COUNT]  = g_poll_last;
    shared[SH_WAIT_LAST_US]= g_wait_last;
    shared[SH_WORK_US]     = g_work_last;
    shared[SH_WORK_MAX]    = g_work_max;
    shared[SH_DB_WAIT_US]  = g_dbwait_last;
    shared[SH_DB_WAIT_MAX] = g_dbwait_max;
    shared[SH_DB_WAITS]    = g_dbwaits;
    shared[SH_DB_TIMEOUTS] = g_dbtmo;
    shared[SH_FPS_PEAK_X100] = g_fps_peak;
    shared[SH_GATE_PASS]     = g_gate_pass;
    shared[SH_GATE_BLOCK]    = g_gate_block;
    shared[SH_FB_REJECTS]    = g_fb_rejects;
    shared[SH_HEAVY_FRAMES]  = g_heavy;
    shared[SH_RENDERS]       = g_renders;
    zzq_ph_publish();
    zzq_s2_publish();
    { int h; for (h = 0; h < ZZQ_HIST_BINS; h++)
                 shared[SH_HIST_BASE + h] = g_hist[h]; }
}
extern u32 zzq_crc32(const u8 *p, u32 len);  /* zzquake_fs_mem.c */

/* was defined in sys_null.c (excluded from build). qboolean is an
   enum = int on ARM EABI. */
int isDedicated = 0;
extern volatile int  zzq_running;          /* zzquake_main.c */
extern void zzq_fatal_end(u32 code);       /* zzquake_main.c: publish
                                              error then 0xFF + WFE */
extern void dcache_clean_range(u32 start, u32 len);  /* zzquake_mmu.c,
                                              real ZZDoom signature */

/* ---- ARM Global Timer (same as ZZDoom blob) ---- */
#define SCU_BASE    0xF8F00000UL
#define GTIMER_LO   (*(volatile u32*)(SCU_BASE+0x0200))
#define GTIMER_HI   (*(volatile u32*)(SCU_BASE+0x0204))
#define GTIMER_HZ   333000000.0

static u64 gtimer_read(void)
{
    u32 hi1, lo, hi2;
    do { hi1 = GTIMER_HI; lo = GTIMER_LO; hi2 = GTIMER_HI; }
    while (hi1 != hi2);
    return ((u64)hi1 << 32) | (u64)lo;
}

u32 zzq_us_now(void)   /* microseconds, for profiling slots */
{
    return (u32)(gtimer_read() / 333ULL);
}

double Sys_FloatTime(void)
{
    return (double)gtimer_read() / GTIMER_HZ;
}

/* ------------------------------------------------------------------ */
/* Text helpers: copy ASCII strings into shared slot windows          */
/* ------------------------------------------------------------------ */

static void copy_to_slots(int first_slot, int nslots, const char *s)
{
    int i, b = 0;
    for (i = 0; i < nslots; i++) {
        u32 w = 0;
        int k;
        for (k = 0; k < 4; k++) {
            u8 c = 0;
            if (s && s[b]) { c = (u8)s[b]; b++; }
            w |= ((u32)c) << (k * 8);
        }
        shared[first_slot + i] = w;
    }
}

void zzq_record_file_req(const char *path)
{
    copy_to_slots(SH_LAST_FILE_REQ, SH_LAST_FILE_LEN, path);
}

/* ------------------------------------------------------------------ */
/* Sys_* misc                                                         */
/* ------------------------------------------------------------------ */

/* BUFFER SIZES - do not shrink.
   Quake formats into char msg[MAXPRINTMSG] = 4096 (console.c) and
   hands us the result. Our old static char buf[256] + vsprintf (no
   bound) overflowed straight into .bss on any message longer than
   256 bytes, clobbering whatever globals followed - including
   function pointers. That produced exactly the observed failure:
   undefined instruction, PC full of ASCII, Thumb bit set, and a
   crash site that moved whenever the memory layout changed.
   Every formatter below is now bounded with vsnprintf. */
#define ZZQ_MSGBUF 4096

void Sys_Error(char *error, ...)
{
    static char buf[ZZQ_MSGBUF];
    va_list ap;

    




    {
        u32 sp, i, n = 0;
        const u32 *s;
        __asm__ volatile("mov %0, sp" : "=r"(sp));
        s = (const u32 *)(sp & ~3u);
        for (i = 0; i < 768u && n < 8u; i++) {
            u32 v = s[i];
            if (v >= 0x04900000u && v < 0x04950000u) {   /* v134 */
                if (n == 0 || shared[SH_BT_BASE + n - 1] != v)
                    shared[SH_BT_BASE + n++] = v;
            }
        }
        shared[SH_BT_COUNT] = n;
        shared[SH_ERR_RET0] = sp;      /* SP at the error, for scale */
    }

    va_start(ap, error);
    vsnprintf(buf, sizeof(buf), error, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;

    /* ---- D1 : re-read the model buffer AT THE MOMENT OF FAILURE.
       Sys_Error belongs to us, so this costs no engine patch. If S
       and D0 were right and D1 is wrong, the temporary buffer was
       overwritten between the read and Mod_LoadAliasModel. */
    if (shared[SH_MDL_VALID] == 1 && shared[SH_MDL_DST]) {
        static const int off[8] = { 0, 4, 48, 52, 56, 60, 64, 68 };
        const u8 *d1 = (const u8 *)shared[SH_MDL_DST];
        int k;
        for (k = 0; k < 8; k++)
            shared[SH_MDL_D1_BASE + k] =
                ((u32)d1[off[k]]) | ((u32)d1[off[k]+1] << 8) |
                ((u32)d1[off[k]+2] << 16) | ((u32)d1[off[k]+3] << 24);
        shared[SH_MDL_CRC_D1] = zzq_crc32(d1, 84u);
        shared[SH_MDL_VALID]  = 2;
    }

    copy_to_slots(SH_LAST_ERR_MSG, SH_LAST_ERR_LEN, buf);
    shared[SH_ERR4] = ((u32)(u8)buf[0]) | ((u32)(u8)buf[1] << 8)
                    | ((u32)(u8)buf[2] << 16) | ((u32)(u8)buf[3] << 24);
    /* If the last recorded file request never succeeded, flag the
       error as file-related: distinguishes a fatal missing file
       (progs.dat, gfx/palette.lmp...) from benign config misses. */
    shared[SH_FILE_FATAL] =
        (shared[SH_NOTFOUND_BENIGN] > 0
         && shared[SH_LAST_FILE_REQ] != 0) ? 1 : 0;

    shared[SH_EXIT_REASON] = ZZQ_EXIT_FATAL;
    zzq_fatal_end(ZZQ_FATAL_SYS_ERROR);   /* does not return */
}


/* ------------------------------------------------------------------ */
/* WAD loading diagnostics. */
/* READ-ONLY: changes no behaviour. Runs once, piggy-backed on the    */
/* first Sys_Printf that happens after gfx.wad is loaded, so NO       */
/* engine file needs patching.                                        */
/* The decisive test is not "filepos & 3 != 0" alone, but a native    */
/* 32-bit read differing from a byte-by-byte read at the same address.*/
/* ------------------------------------------------------------------ */

typedef struct {
    int  filepos;
    int  disksize;
    int  size;
    char type, compression, pad1, pad2;
    char name[16];
} zzq_lumpinfo_t;          /* 32 bytes, matches wad.h exactly */

extern unsigned char   *wad_base;      /* wad.c globals */
extern int              wad_numlumps;
extern zzq_lumpinfo_t  *wad_lumps;

static u32 safe_le32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void zzq_wad_probe(void)
{
    const u8 *wb;
    int i, n;

    if (shared[SH_WAD_PROBE_DONE] == 1) return;   /* 2 = retry later */
    /* Report the reason instead of staying silent: 2 = wad not loaded
       yet, which is legitimate before W_LoadWadFile runs. */
    if (!wad_base || !wad_lumps || wad_numlumps <= 0) {
        shared[SH_WAD_PROBE_DONE] = 2;
        shared[SH_WAD_NUMLUMPS]   = (u32)wad_numlumps;
        shared[SH_WAD_BASE_A3]    = wad_base ? 0x10u : 0xFFu;  /* 0xFF=NULL */
        return;
    }

    wb = (const u8 *)wad_base;
    shared[SH_WAD_BASE_A3]  = ((u32)(unsigned long)wad_base) & 3u;
    shared[SH_WAD_LUMPS_A3] = ((u32)(unsigned long)wad_lumps) & 3u;
    shared[SH_WAD_TABOFS]   = (u32)((const u8 *)wad_lumps - wb);
    shared[SH_WAD_NUMLUMPS] = (u32)wad_numlumps;

    n = wad_numlumps;
    if (n > 1024) n = 1024;
    shared[SH_WAD_UNAL_COUNT] = 0;
    shared[SH_WAD_UNAL_IDX]   = 0xFFFFFFFFu;
    for (i = 0; i < n; i++) {
        u32 fp = (u32)wad_lumps[i].filepos;
        if (fp & 3u) {
            shared[SH_WAD_UNAL_COUNT]++;
            if (shared[SH_WAD_UNAL_IDX] == 0xFFFFFFFFu) {
                shared[SH_WAD_UNAL_IDX] = (u32)i;
                shared[SH_WAD_UNAL_POS] = fp;
            }
        }
    }

    /* First lump treated as a qpic: compare a native 32-bit read with
       a byte-by-byte read at the same address. A mismatch is the
       proof that unaligned access is misbehaving. */
    {
        const u8 *pic = wb + (u32)wad_lumps[0].filepos;
        const int *nat = (const int *)pic;
        shared[SH_WAD_PIC_A3]     = ((u32)(unsigned long)pic) & 3u;
        shared[SH_WAD_PIC_W_NAT]  = (u32)nat[0];
        shared[SH_WAD_PIC_H_NAT]  = (u32)nat[1];
        shared[SH_WAD_PIC_W_SAFE] = safe_le32(pic);
        shared[SH_WAD_PIC_H_SAFE] = safe_le32(pic + 4);
    }
    shared[SH_WAD_PROBE_DONE] = 1;   /* real measurement */
}

void Sys_Printf(char *fmt, ...)
{
    static char buf[ZZQ_MSGBUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    copy_to_slots(SH_LAST_PRINTF, SH_LAST_PRINTF_LEN, buf);
    zzq_wad_probe();          /* read-only, runs once */
}

int zzq_printf(const char *fmt, ...)     /* engine printf() lands here */
{
    static char buf[ZZQ_MSGBUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    copy_to_slots(SH_LAST_PRINTF, SH_LAST_PRINTF_LEN, buf);
    return 0;
}

void  Sys_Quit(void)              { zzq_running = 0; }
char *Sys_ConsoleInput(void)      { return 0; }
void  Sys_Sleep(void)             { }
/* Pump queued input while Quake is inside modal loops. */





























extern int  QG_GetKey(int *down, int *key);
extern void Key_Event(int key, int down);

void  Sys_SendKeyEvents(void)
{
    int down, key;
    while (QG_GetKey(&down, &key))
        Key_Event(key, down);
}
void  Sys_MakeCodeWriteable(unsigned long a, unsigned long l)
                                  { (void)a; (void)l; }
void  Sys_DebugLog(char *file, char *fmt, ...)
                                  { (void)file; (void)fmt; }
void  Sys_LowFPPrecision(void)    { }
void  Sys_HighFPPrecision(void)   { }
void  Sys_SetFPCW(void)           { }

/* ------------------------------------------------------------------ */
/* QG hooks: palette + framebuffer                                    */
/* ------------------------------------------------------------------ */

static u32 g_pal[256];   /* Quake index -> XRGB8888 (ZZ colormode 32) */
/* RGB565 byte order follows the non-PC P96 R5G6B5 format. */

static u16 g_pal16[256];

void QG_SetPalette(unsigned char palette[768])
{
    int i;
    for (i = 0; i < 256; i++) {
        u32 r = palette[i*3+0], g = palette[i*3+1], b = palette[i*3+2];
        /* PIXEL FORMAT (corrected): the framebuffer holds bytes in
           the order B,G,R,A. An ARM little-endian u32 store puts the
           low byte first, so the value to write is simply
           (R<<16)|(G<<8)|B - the plain 0x00RRGGBB.
           I briefly byte-swapped this after misreading a hardware
           test: the colours I was judging came from the 68k's own
           reference pattern, not from the ARM at all. */
        g_pal[i] = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
        /* RGB565 byte order follows the non-PC P96 R5G6B5 format. */






        {   u32 v = ((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3);
            g_pal16[i] = (u16)(((v >> 8) | (v << 8)) & 0xFFFFu);
        }
    }
    shared[SH_PALETTE_COUNT]++;
    if (shared[SH_STATUS] == ZZQ_QG_CREATE_OK)
        shared[SH_STATUS] = ZZQ_PALETTE_OK;
    /* Called on every damage/bonus flash via VID_ShiftPalette:
       must stay this cheap. No flush (g_pal is ARM-private). */
}

void QG_Init(void)
{
    shared[SH_DIAG] = 0xA050;
}

void QG_Quit(void)
{
    


    shared[SH_EXIT_REASON] = ZZQ_EXIT_QG_QUIT;
    zzq_running = 0;
    shared[SH_STATUS] = ZZQ_QUIT_REQUESTED;
}

/* Pump queued input while Quake is inside modal loops. */
extern int scr_drawdialog;   /* qboolean = enum = int, ARM EABI */

void QG_DrawFrame(void *pixels)
{
    u32 t0 = zzq_us_now();
    u8  *src = (u8 *)pixels;                 /* 320x240, pitch 320 */
    u32  fb    = shared[SH_FB_ADDR];         /* 68k updates per flip */
    u32  pitch = shared[SH_FB_PITCH];        /* bytes */

    /* A zero pitch collapses every row onto the first one and looks
       exactly like "nothing is drawn". Guard it. */
    if (pitch == 0)
        pitch = (u32)QUAKEGENERIC_RES_X *
                (shared[SH_FB_BPP] == 2u ? 2u : 4u);

    /* Framebuffer ownership is synchronized with the 68k launcher. */







    if (shared[SH_TRIPLE]) {
        if (shared[SH_RENDER_SEQ] == g_last_render_seq) {
            /* Framebuffer ownership is synchronized with the 68k launcher. */



















            if (scr_drawdialog) {
                u32 t0 = zzq_us_now();
                shared[SH_MODAL_GATEBLOCK]++;
                shared[SH_MODAL_WAITS]++;
                while (shared[SH_RENDER_SEQ] == g_last_render_seq) {
                    if ((u32)(zzq_us_now() - t0) > 100000u) {
                        


                        shared[SH_MODAL_TIMEOUTS]++;
                        g_gate_block++;
                        return;
                    }
                    __asm__ volatile("dsb sy" ::: "memory");
                }
                /* autorisation obtenue : on continue normalement */
            } else {
                g_gate_block++;        

                return;                /* pas encore autorise */
            }
        }
        g_last_render_seq = shared[SH_RENDER_SEQ];
        g_gate_pass++;                 
        fb = shared[SH_RENDER_FB];     
        




        if (fb < 0x00200000u || fb > 0x08000000u) {
            shared[SH_FB_LAST_BAD] = fb;
            g_fb_rejects++;
            return;                    
        }
        shared[SH_FB_USED] = fb;
    }
    u32 *dst;
    int  x, y;

    if (fb == 0) return;

    if (shared[SH_FB_BPP] == 2u) {
        /* RGB565 byte order follows the non-PC P96 R5G6B5 format. */




        u16 *d16;
        for (y = 0; y < QUAKEGENERIC_RES_Y; y++) {
            d16 = (u16 *)(fb + (u32)y * pitch);
            for (x = 0; x < QUAKEGENERIC_RES_X; x += 4) {
                d16[x+0] = g_pal16[src[x+0]];
                d16[x+1] = g_pal16[src[x+1]];
                d16[x+2] = g_pal16[src[x+2]];
                d16[x+3] = g_pal16[src[x+3]];
            }
            src += QUAKEGENERIC_RES_X;
        }
    } else {
        for (y = 0; y < QUAKEGENERIC_RES_Y; y++) {
            dst = (u32 *)(fb + (u32)y * pitch);
            for (x = 0; x < QUAKEGENERIC_RES_X; x += 4) {
                dst[x+0] = g_pal[src[x+0]];
                dst[x+1] = g_pal[src[x+1]];
                dst[x+2] = g_pal[src[x+2]];
                dst[x+3] = g_pal[src[x+3]];
            }
            src += QUAKEGENERIC_RES_X;
        }
    }
    shared[SH_CONVERT_US] = zzq_us_now() - t0;

    
    dcache_clean_range(fb, (u32)QUAKEGENERIC_RES_Y * pitch);

    shared[SH_FRAME]++;

    


    {
        static u32 s_last_us = 0;
        u32 now = zzq_us_now();
        if (s_last_us) {
            u32 d = now - s_last_us;
            if (d > 0 && d < 1000000u) {
                u32 fps100 = 100000000u / d;
                if (fps100 > g_fps_peak) g_fps_peak = fps100;
            }
        }
        s_last_us = now;
    }

    












    if (shared[SH_TRIPLE]) {
        


        u32 w = zzq_us_now() - g_work_t0;
        g_work_last = w;
        if (w > g_work_max) g_work_max = w;
        shared[SH_WORK_US] = w;
        /* Cache/MMU handling follows the validated Core1 memory contract. */





        g_renders++;
        if (w > 16810u) g_heavy++;     
        zzq_ph_frame(w);
        zzq_s2_frame(w);
        {   
            u32 bin = w / 2000u;
            if (bin >= ZZQ_HIST_BINS) bin = ZZQ_HIST_BINS - 1u;
            g_hist[bin]++;
        }
        if ((g_gate_pass & 31u) == 0u) zzq_publish_stats();
        shared[SH_RENDER_DONE] = g_last_render_seq;
        __asm__ volatile("dsb sy" ::: "memory");
        shared[SH_FRAME_READY] = g_last_render_seq;
        __asm__ volatile("dsb sy" ::: "memory");
        g_work_t0 = zzq_us_now();
        return;
    }

    if (shared[SH_DB_ENABLE]) {
        u32 seq = shared[SH_FRAME];
        





        {
            u32 w = zzq_us_now() - g_work_t0;
            g_work_last = w;
            if (w > g_work_max) g_work_max = w;
            shared[SH_WORK_US] = w;   
        }
        shared[SH_FRAME_READY] = seq;
        





        __asm__ volatile("dsb sy" ::: "memory");
        if (!shared[SH_TIMEDEMO_NOWAIT]) {
            u32 t0 = zzq_us_now();
            u32 t1 = 0;
            u32 polls = 0;
            u32 spin = 4000000u;
            



            /* Cache/MMU handling follows the validated Core1 memory contract. */










            {
                u32 bo = shared[SH_POLL_BACKOFF];
                if (bo == 0) bo = 400;      /* ~20 us par defaut */
                while (shared[SH_FLIP_SEQ] != seq && spin--) {
                    volatile u32 k;
                    polls++;
                    if (!t1 && shared[SH_READY_SEEN] == seq)
                        t1 = zzq_us_now();
                    for (k = 0; k < bo; k++) { }
                }
            }
            {
                u32 t2 = zzq_us_now();
                u32 d1, d2;
                if (!t1) t1 = t2;          
                d1 = t1 - t0;
                d2 = t2 - t1;
                g_d1_last = d1;
                g_d2_last = d2;
                g_poll_last = polls;
                /* Core1 execution path. */






                if ((g_dn_count & 63u) == 0u) zzq_publish_stats();
                


                /* Cache/MMU handling follows the validated Core1 memory contract. */







                if (spin != 0xFFFFFFFFu && d1 < 200000u && d2 < 200000u) {
                    g_d1_sum_ms += d1 / 1000u;
                    g_d2_sum_ms += d2 / 1000u;
                    g_poll_sum_k += polls / 1000u;
                    g_dn_count++;
                    if (d1 > g_d1_max) g_d1_max = d1;
                    if (d2 > g_d2_max) g_d2_max = d2;
                }
            }
            {
                u32 dt = zzq_us_now() - t0;
                g_wait_last = dt;
                g_work_t0 = zzq_us_now();      /* le travail reprend */
                g_dbwaits++;
                /* Cache/MMU handling follows the validated Core1 memory contract. */




                g_dbwait_last = dt;
                if (dt > g_dbwait_max) g_dbwait_max = dt;
                if (spin == 0xFFFFFFFFu) g_dbtmo++;
            }
        }
    } else {
        shared[SH_FRAME_READY]++;        
    }
    if (shared[SH_STATUS] == ZZQ_PALETTE_OK) {
        shared[SH_STATUS] = ZZQ_FIRST_FRAME_OK;
    } else if (shared[SH_STATUS] == ZZQ_FIRST_FRAME_OK &&
               shared[SH_FRAME] > 10) {
        shared[SH_STATUS] = ZZQ_RUNNING;
    }
    shared[SH_DRAW_US] = zzq_us_now() - t0;
}

/* ------------------------------------------------------------------ */
/* QG hooks: input                                                    */
/* Bitfield edge detection, same technique as the ZZDoom blob.        */
/* ------------------------------------------------------------------ */

static const struct { u32 bit; int qkey; } g_keymap[] = {
    { BTN_UP,         K_UPARROW    },
    { BTN_DOWN,       K_DOWNARROW  },
    { BTN_LEFT,       K_LEFTARROW  },
    { BTN_RIGHT,      K_RIGHTARROW },
    { BTN_SL,         ','          },
    { BTN_SR,         '.'          },
    { BTN_FIRE,       K_CTRL       },
    { BTN_USE,        K_SPACE      },
    { BTN_RUN,        K_SHIFT      },
    { BTN_ESC,        K_ESCAPE     },
    { BTN_ENTER,      K_ENTER      },
    { BTN_Y,          'y'          },
    { BTN_N,          'n'          },
    { BTN_STRAFE_MOD, K_ALT        },
    { BTN_MAP,        K_TAB        },
    { BTN_TILDE,      '`'          },
    { BTN_W1, '1' }, { BTN_W2, '2' }, { BTN_W3, '3' }, { BTN_W4, '4' },
    { BTN_W5, '5' }, { BTN_W6, '6' }, { BTN_W7, '7' }, { BTN_W8, '8' },
    { BTN_F1,  K_F1  }, { BTN_F2, K_F2 }, { BTN_F3, K_F3 },
    { BTN_F4,  K_F4  }, { BTN_F5, K_F5 }, { BTN_F6, K_F6 },
    { BTN_F10, K_F10 }, { BTN_PAUSE, K_PAUSE },
};
#define NKEYS ((int)(sizeof(g_keymap)/sizeof(g_keymap[0])))

static u32 g_prev_buttons = 0;
static int g_scan_idx     = 0;
static u32 g_cur, g_changed;

/* Return 1 + one event per call; 0 when the current diff is drained.
   in_null.c calls this in a while() loop each frame. */







int QG_GetKey(int *down, int *key)
{
    u32 wr = shared[SH_KEYQ_WR];
    u32 rd = g_keyq_rd;
    u32 ev;
    if (rd == wr) return 0;                    
    /* Pump queued input while Quake is inside modal loops. */




    if ((u32)(wr - rd) > (u32)ZZQ_KEYQ_SIZE) {
        g_keyq_rd = wr;
        shared[SH_KEYQ_RD] = wr;
        shared[SH_KEYQ_OVER]++;
        return 0;
    }
    ev = shared[SH_KEYQ_BASE + (rd & (ZZQ_KEYQ_SIZE - 1))];
    g_keyq_rd = rd + 1;
    shared[SH_KEYQ_RD] = g_keyq_rd;            
    *down = ZZQ_KEV_DOWN(ev);
    *key  = ZZQ_KEV_KEY(ev);
    shared[SH_KEY_EVENTS]++;
    return 1;
}






int QG_GetMouseMove_dummy;
void QG_GetMouseMove(int *x, int *y)
{
    s32 tx = (s32)shared[SH_MOUSE_TOT_X];
    s32 ty = (s32)shared[SH_MOUSE_TOT_Y];
    *x = (int)(tx - g_mouse_last_x);
    *y = (int)(ty - g_mouse_last_y);
    g_mouse_last_x = tx;
    g_mouse_last_y = ty;
}

void QG_GetJoyAxes(float *axes)
{
    int i;
    for (i = 0; i < QUAKEGENERIC_JOY_MAX_AXES; i++)
        axes[i] = 0.0f;      /* ALL six axes, explicitly (audit #6) */
}


/* Audio uses the shared PCM transport. */






static u32 g_pcm_phase_l = 0, g_pcm_phase_r = 0;
static u32 g_pcm_wpos = 0;
static u32 g_pcm_fill_min = 0xFFFFFFFFu, g_pcm_fill_max = 0;

/* Audio uses the shared PCM transport. */

static const s16 g_sin64[64] = {
     0,  3212,  6393,  9512, 12539, 15446, 18204, 20787,
 23170, 25330, 27245, 28898, 30273, 31357, 32138, 32610,
 32767, 32610, 32138, 31357, 30273, 28898, 27245, 25330,
 23170, 20787, 18204, 15446, 12539,  9512,  6393,  3212,
     0, -3212, -6393, -9512,-12539,-15446,-18204,-20787,
-23170,-25330,-27245,-28898,-30273,-31357,-32138,-32610,
-32767,-32610,-32138,-31357,-30273,-28898,-27245,-25330,
-23170,-20787,-18204,-15446,-12539, -9512, -6393, -3212
};

void zzq_pcm_fill(void)
{
    /* Audio uses the shared PCM transport. */




    if (1) return;
    volatile u8 *ring;
    u32 rd, used, space, want, i;

    if (!shared[SH_PCM_ENABLE]) return;
    ring = (volatile u8 *)ZZQ_PCM_ARM;

    rd = shared[SH_PCM_READ_POS];
    used = (g_pcm_wpos >= rd) ? (g_pcm_wpos - rd)
                              : (ZZQ_PCM_RING_SIZE - rd + g_pcm_wpos);
    if (used < g_pcm_fill_min) g_pcm_fill_min = used;
    if (used > g_pcm_fill_max) g_pcm_fill_max = used;

    


    if (used >= ZZQ_PCM_TARGET_FILL) goto publish;
    space = ZZQ_PCM_TARGET_FILL - used;
    want  = space & ~3u;

    for (i = 0; i < want; i += 4) {
        s16 l = g_sin64[(g_pcm_phase_l >> 8) & 63];
        s16 r = g_sin64[(g_pcm_phase_r >> 8) & 63];
        l = (s16)(l / 4);                 /* volume modere */
        r = (s16)(r / 4);
        
        ring[g_pcm_wpos + 0] = (u8)(l & 0xFF);
        ring[g_pcm_wpos + 1] = (u8)((l >> 8) & 0xFF);
        ring[g_pcm_wpos + 2] = (u8)(r & 0xFF);
        ring[g_pcm_wpos + 3] = (u8)((r >> 8) & 0xFF);
        g_pcm_wpos += 4;
        if (g_pcm_wpos >= ZZQ_PCM_RING_SIZE) g_pcm_wpos = 0;
        

        g_pcm_phase_l += (440u * 64u * 256u) / ZZQ_PCM_RATE;
        g_pcm_phase_r += (660u * 64u * 256u) / ZZQ_PCM_RATE;
    }

publish:
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_PCM_WRITE_POS] = g_pcm_wpos;
    shared[SH_PCM_FILL_MIN]  = (g_pcm_fill_min == 0xFFFFFFFFu) ? 0
                                                               : g_pcm_fill_min;
    shared[SH_PCM_FILL_MAX]  = g_pcm_fill_max;
}


/* Optional renderer profiling hooks. */











static u32 g_ph_t0[ZZQ_PH_COUNT];
static u32 g_ph_cur[ZZQ_PH_COUNT];        
static u32 g_ph_light[ZZQ_PH_COUNT], g_ph_heavy[ZZQ_PH_COUNT];
static u32 g_ph_max[ZZQ_PH_COUNT];
static u32 g_ph_nlight, g_ph_nheavy;

void zzq_ph_begin(int p)
{
    if (p >= 0 && p < ZZQ_PH_COUNT) g_ph_t0[p] = zzq_us_now();
}

void zzq_ph_end(int p)
{
    if (p >= 0 && p < ZZQ_PH_COUNT) {
        u32 d = zzq_us_now() - g_ph_t0[p];
        g_ph_cur[p] += d;
        




    }
}


void zzq_ph_frame(u32 work_us)
{
    int i;
    if (work_us <= 16810u) {
        g_ph_nlight++;
        for (i = 0; i < ZZQ_PH_COUNT; i++) g_ph_light[i] += g_ph_cur[i];
    } else if (work_us > 18000u) {
        g_ph_nheavy++;
        for (i = 0; i < ZZQ_PH_COUNT; i++) g_ph_heavy[i] += g_ph_cur[i];
    }
    

    for (i = 0; i < ZZQ_PH_COUNT; i++)
        if (g_ph_cur[i] > g_ph_max[i]) g_ph_max[i] = g_ph_cur[i];
    for (i = 0; i < ZZQ_PH_COUNT; i++) g_ph_cur[i] = 0;
}

void zzq_ph_publish(void)
{
    int i;
    for (i = 0; i < ZZQ_PH_COUNT; i++) {
        shared[SH_PROF_LIGHT + i] = g_ph_light[i];
        shared[SH_PROF_HEAVY + i] = g_ph_heavy[i];
        shared[SH_PROF_MAX   + i] = g_ph_max[i];
    }
    shared[SH_PROF_NLIGHT] = g_ph_nlight;
    shared[SH_PROF_NHEAVY] = g_ph_nheavy;
}


/* Optional renderer profiling hooks. */








static u32 g_s2_t0[ZZQ_S2_COUNT];
static u64 g_s2_cur[ZZQ_S2_COUNT];
static u64 g_s2_light[ZZQ_S2_COUNT], g_s2_heavy[ZZQ_S2_COUNT];
/* Optional renderer profiling hooks. */
static u32 g_pmu_dc = 0, g_pmu_dt = 0;
static u32 g_n_normal, g_n_spans, g_n_spanpix;
static u32 g_n_sky, g_n_turb, g_n_back, g_n_submodel;

/* Cache/MMU handling follows the validated Core1 memory contract. */













static inline u32 pmu_cycles(void)
{
    u32 v;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(v));
    return v;
}

/* Optional renderer profiling hooks. */








/* Cache/MMU handling follows the validated Core1 memory contract. */







void zzq_s2_begin(int p) { if ((u32)p < ZZQ_S2_COUNT) g_s2_t0[p] = zzq_us_now(); }
void zzq_s2_end(int p)
{
    if ((u32)p < ZZQ_S2_COUNT) g_s2_cur[p] += zzq_us_now() - g_s2_t0[p];
}

/* Cache/MMU handling follows the validated Core1 memory contract. */





struct zzq_span { int u, v, count; struct zzq_span *pnext; };








void zzq_s2_count(int kind, void *spans)
{
    struct zzq_span *sp;
#ifdef ZZQ_PRODUCTION
    


    (void)kind; (void)spans; return;
#endif
    u32 guard = 0;

    switch (kind) {
    case 0: g_n_normal++;   break;
    case 1: g_n_sky++;      break;
    case 2: g_n_turb++;     break;
    case 3: g_n_back++;     break;
    case 4: g_n_submodel++; return;   /* surcout : PAS de spans */
    default: return;
    }
    sp = (struct zzq_span *)spans;
    while (sp && guard++ < 4096u) {
        g_n_spans++;
        if (sp->count > 0 && sp->count < 4096) g_n_spanpix += (u32)sp->count;
        sp = sp->pnext;
    }
}

/* Optional renderer profiling hooks. */


/* Optional renderer profiling hooks. */



static u32 zzq_s2_us(u64 us) { return (u32)us; }

void zzq_s2_frame(u32 work_us)
{
    int i;
    if (work_us <= 16810u)
        for (i = 0; i < ZZQ_S2_COUNT; i++) g_s2_light[i] += g_s2_cur[i];
    else if (work_us > 18000u)
        for (i = 0; i < ZZQ_S2_COUNT; i++) g_s2_heavy[i] += g_s2_cur[i];
    for (i = 0; i < ZZQ_S2_COUNT; i++) g_s2_cur[i] = 0;
}

/* Optional renderer profiling hooks. */




void zzq_s2_pmu_init(void)
{
    u32 pmcr = 0, cnten = 0, c0, c1, t0, t1;
    volatile u32 spin;

    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 1" : "=r"(cnten));
    c0 = pmu_cycles();
    t0 = zzq_us_now();
    for (spin = 0; spin < 100000u; spin++) { }
    t1 = zzq_us_now();
    c1 = pmu_cycles();

    shared[SH_PMU_PMCR]  = pmcr;
    shared[SH_PMU_CNTEN] = cnten;
    shared[SH_PMU_C0]    = c0;
    shared[SH_PMU_C1]    = c1;
    shared[SH_PMU_GT0]   = t0;
    shared[SH_PMU_GT1]   = t1;
    g_pmu_dc = c1 - c0;
    g_pmu_dt = t1 - t0;
}

void zzq_s2_publish(void)
{
    int i;
    for (i = 0; i < ZZQ_S2_COUNT; i++) {
        /* Optional renderer profiling hooks. */





        shared[SH_S2_LIGHT + i] = zzq_s2_us(g_s2_light[i]);
        shared[SH_S2_HEAVY + i] = zzq_s2_us(g_s2_heavy[i]);
    }
    shared[SH_N_NORMAL]    = g_n_normal;
    shared[SH_N_SKY]       = g_n_sky;
    shared[SH_N_TURB]      = g_n_turb;
    shared[SH_N_BACK]      = g_n_back;
    shared[SH_N_SUBMODEL]  = g_n_submodel;
    shared[SH_N_SPANS]     = g_n_spans;
    shared[SH_N_SPANPIX_K] = g_n_spanpix / 1000u;
}
