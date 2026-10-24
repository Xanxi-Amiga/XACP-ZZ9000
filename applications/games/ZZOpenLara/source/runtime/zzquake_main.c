/* VFP must be enabled before compiled C code executes. */

























#include "zzquake_config.h"
#include "quakegeneric.h"

typedef unsigned int       u32;
typedef unsigned char      u8;
typedef unsigned long long u64;

/* ---- external bricks (zzquake_mmu.c, derived from real ZZDoom) ---- */
extern void mmu_init_zzquake(u32 fb_arm);
extern void zzq_s2_pmu_init(void);
extern void Host_Shutdown(void);      /* Optional renderer profiling hooks. */
extern void dcache_invalidate_range(u32 start, u32 len);
extern void dcache_clean_range(u32 start, u32 len);
extern void zz_install_vectors(void);   /* zzquake_abort.S */
extern void zzq_a1_fstest(void);        /* zzquake_a1_fstest.c */
/* mmu_disable() EXISTS in zzquake_mmu.c but is DELIBERATELY NEVER
   CALLED - see zzq_end_wfe() below. */

/* ---- shared table ---- */
volatile u32 *shared = (volatile u32 *)ZZQ_SHARED_ARM;
volatile int  zzq_running = 1;



volatile unsigned int zzq_canary = 0x5A5AA5A5u;

extern u32  zzq_us_now(void);                  /* zzquake_platform.c */
extern double Sys_FloatTime(void);

#define DIAG(v) do { shared[SH_DIAG] = (v); \
                     __asm__ volatile("dsb" ::: "memory"); } while (0)

/* ------------------------------------------------------------------ */
/* Mandatory terminal states                                          */
/* ------------------------------------------------------------------ */

/* Cache/MMU handling follows the validated Core1 memory contract. */














extern void zzq_return_to_firmware(void);   /* zzquake_entry.S */

/* Cache/MMU handling follows the validated Core1 memory contract. */













extern void zzq_publish_stats(void);   /* zzquake_platform.c */

static void zzq_end_wfe(void)
{
    zzq_publish_stats();       
    

    shared[SH_RETURN_OK] = ZZQ_RETURN_OK;
    __asm__ volatile("dsb" ::: "memory");
    shared[SH_STATUS] = ZZQ_STOPPED;           /* 0xFF, le 68k l'attend */
    __asm__ volatile("dsb" ::: "memory");

    /* Framebuffer ownership is synchronized with the 68k launcher. */

    if (shared[SH_FB_ADDR] && shared[SH_FB_PITCH])
        dcache_clean_range(shared[SH_FB_ADDR],
                           shared[SH_FB_HEIGHT] * shared[SH_FB_PITCH]);
    __asm__ volatile("dsb" ::: "memory");

    










    if (shared[SH_ENABLE_COOP])
        zzq_return_to_firmware();              

    
    while (1) { __asm__ volatile("wfe"); }
}

void zzq_fatal_end(u32 code)
{
    shared[SH_ERROR] = code;
    zzq_end_wfe();                             /* relaunchable */
}

/* ------------------------------------------------------------------ */
/* Heap: fixed DDR arena for newlib _sbrk (malloc in QG_Create,       */
/* surfcache in vid_null.c, zone/hunk inside the engine)              */
/* ------------------------------------------------------------------ */

static char *heap_ptr;   /* set in core1_entry step 3 */

void *_sbrk(int incr)
{
    char *old;
    if (heap_ptr == 0) heap_ptr = (char *)ZZQ_HEAP_ARM;
    old = heap_ptr;
    if ((u32)(heap_ptr + incr) > (ZZQ_HEAP_ARM + ZZQ_HEAP_SIZE_A0)) {
        shared[SH_ERROR] = ZZQ_FATAL_OUT_OF_MEM;
        return (void *)-1;
    }
    heap_ptr += incr;
    shared[SH_HEAP_USED] = (u32)heap_ptr - ZZQ_HEAP_ARM;
    if (shared[SH_SBRK_FIRST] == 0) {      
        shared[SH_SBRK_FIRST] = (u32)old;
        shared[SH_SBRK_INCR]  = (u32)incr;
    }
    return old;
}

/* ------------------------------------------------------------------ */
/* VFP self-test (audit #4): compiled hard-float, so if VFP were not  */

/* instruction and DIAG freezes at 0xA002 - immediate diagnosis.      */
/* ------------------------------------------------------------------ */

static void float_selftest(void)
{
    volatile float a = 1.5f, b = 2.0f;
    volatile float c;
    DIAG(0xA002);
    c = a * b;
    if (c == 3.0f) shared[SH_FLOAT_DIAG] = ZZQ_FLOAT_OK;
    else           shared[SH_FLOAT_DIAG] = 0xBAD0F10A;
}

/* ------------------------------------------------------------------ */
/* High-DDR probe (audit #2): validates 0x08000000..0x09000000 before */
/* any future heap extension to 32MB. Write pattern, clean+invalidate */
/* by MVA to force a DDR round trip, read back.                       */
/* ------------------------------------------------------------------ */

static void dc_civac(u32 a)      /* clean+invalidate by MVA */
{
    __asm__ volatile("mcr p15,0,%0,c7,c14,1" :: "r"(a & ~31u) : "memory");
}

static void ddr_probe(void)
{
    u32 addr, k;
    DIAG(0xA003);
    shared[SH_PROBE_RESULT] = 0;

    for (addr = ZZQ_PROBE_START; addr < ZZQ_PROBE_END;
         addr += 0x00100000UL) {
        volatile u32 *p = (volatile u32 *)addr;
        for (k = 0; k < 8; k++) p[k] = addr ^ (0xC0DE0000UL + k);
        for (k = 0; k < 8; k++) dc_civac(addr + k * 4);
        __asm__ volatile("dsb" ::: "memory");
        for (k = 0; k < 8; k++) {
            if (p[k] != (addr ^ (0xC0DE0000UL + k))) {
                shared[SH_PROBE_RESULT]    = 2;
                shared[SH_PROBE_FAIL_ADDR] = addr + k * 4;
                return;
            }
        }
    }
    shared[SH_PROBE_RESULT] = 1;
}

/* ------------------------------------------------------------------ */
/* PAK validation                                                     */
/* ------------------------------------------------------------------ */

/* Explicit little-endian reader. The pak is LE, ARM is LE, so a u32
   load would work - but this makes the intent unambiguous and is
   what every bounds check below uses. */
static u32 read_le32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* PAK filesystem handling. */




static u32 pak_rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int pak_check_one(u32 addr, u32 size)
{
    const u8 *p;
    u32 dir_ofs, dir_len;

    DIAG(0xA010);
    if (size == 0) { shared[SH_ERROR] = ZZQ_FATAL_NO_PAK; return 0; }
    if (addr < ZZQ_PACK_ARENA_BASE ||
        size > (ZZQ_PACK_ARENA_END - ZZQ_PACK_ARENA_BASE) ||
        addr > ZZQ_PACK_ARENA_END - size ||
        addr + size < addr) {              /* debordement 32 bits */
        shared[SH_ERROR] = ZZQ_FATAL_PAK_TOO_BIG;
        return 0;
    }

    p = (const u8 *)addr;
    if (p[0] != 'P' || p[1] != 'A' || p[2] != 'C' || p[3] != 'K') {
        shared[SH_ERROR] = ZZQ_FATAL_BAD_PAK;
        shared[SH_PAK_MAGIC_SEEN] = ((u32)p[0]) | ((u32)p[1] << 8)
                                  | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        return 0;
    }

    dir_ofs = pak_rd32(p + 4);
    dir_len = pak_rd32(p + 8);
    shared[SH_PAK_DIR_OFS] = dir_ofs;

    if (dir_len == 0 || (dir_len % 64u) != 0u ||
        dir_ofs > size || dir_len > size || dir_ofs > size - dir_len) {
        shared[SH_ERROR] = ZZQ_FATAL_BAD_PAK;
        return 0;
    }
    shared[SH_PAK_DIR_COUNT] = dir_len / 64u;
    DIAG(0xA011);
    return 1;
}

static int pak_check(void)
{
    /* PAK filesystem handling. */


    if (!pak_check_one(shared[SH_PACK0_ADDR], shared[SH_PACK0_SIZE]))
        return 0;
    if (shared[SH_PACK_COUNT] > 1 &&
        !pak_check_one(shared[SH_PACK1_ADDR], shared[SH_PACK1_SIZE]))
        return 0;
    if (shared[SH_PACK_COUNT] > 2 &&
        !pak_check_one(shared[SH_PACK2_ADDR], shared[SH_PACK2_SIZE]))
        return 0;
    {   /* PAK filesystem handling. */
        u32 m;
        for (m = 0; m < shared[SH_MOD_COUNT] && m < ZZQ_MODPACK_MAX; m++)
            if (!pak_check_one(shared[SH_MOD_ADDR + m], shared[SH_MOD_SIZE + m]))
                return 0;
    }
    return 1;
}


/* ------------------------------------------------------------------
 * A2a: framebuffer self-test. Draws a known pattern straight into the
 * VRAM address the 68k handed us, with NO Quake involved at all.
 * Same philosophy as A1 for the filesystem: prove one path alone.
 *
 * Pattern (320x240 assumed, uses the real width/height/pitch slots):
 *   - top third    : pure RED     (0x00FF0000)
 *   - middle third : pure GREEN   (0x0000FF00)
 *   - bottom third : pure BLUE    (0x000000FF)
 *   - a WHITE 16px border all around
 *   - a black/white checkerboard in the centre
 * If this shows up, the display path is proven end to end and any
 * remaining black screen is Quake's problem, not the framebuffer's.
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------
 * A2-TIMERDIAG : Global Timer observation, STRICTLY READ-ONLY.
 *
 * The ARM Global Timer (SCU 0xF8F00200/204, control 0xF8F00208) is
 * SHARED by both Cortex-A9 cores, and Core0 already reads it
 * (ZZMIDI calls XTime_GetTime). Writing GTIMER_CTRL, or worse
 * resetting the counter, would move the time reference under Core0's
 * feet. So this function only READS, and we decide what to do after
 * seeing the numbers:
 *   CTRL.EN=0 and delta=0 -> timer genuinely stopped
 *   CTRL.EN=1 and delta>0 -> timer fine, my Sys_FloatTime was wrong
 *   CTRL.EN=1 and delta=0 -> access/secure/mapping problem on Core1
 * ------------------------------------------------------------------ */
static void zzq_timer_diag(void)
{
    volatile u32 *gt_lo   = (volatile u32 *)0xF8F00200UL;
    volatile u32 *gt_hi   = (volatile u32 *)0xF8F00204UL;
    volatile u32 *gt_ctrl = (volatile u32 *)0xF8F00208UL;
    u32 lo0, hi0, lo1, hi1, pmcr;
    volatile u32 i;

    shared[SH_GT_CTRL] = *gt_ctrl;          /* read only */

    do { hi0 = *gt_hi; lo0 = *gt_lo; } while (hi0 != *gt_hi);
    for (i = 0; i < 100000u; i++) { __asm__ volatile("nop"); }
    do { hi1 = *gt_hi; lo1 = *gt_lo; } while (hi1 != *gt_hi);

    shared[SH_GT_T0_LO] = lo0;  shared[SH_GT_T0_HI] = hi0;
    shared[SH_GT_T1_LO] = lo1;  shared[SH_GT_T1_HI] = hi1;
    shared[SH_GT_DELTA] = lo1 - lo0;

    /* PMU PMCR, read only: tells us whether CCNT is a usable fallback */
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    shared[SH_GT_PMCR] = pmcr;
}


/* Core1 execution path. */




static void zzq_visibility_test(void)
{
    u32 round;
    shared[SH_VIS_DONE] = 0;
    for (round = 1; round <= 64u; round++) {   
        u32 guard = 0;
        shared[SH_VIS_ROUND] = round;
        
        while (shared[SH_VIS_GO] != round && guard < 20000000u) {
            volatile u32 k;
            for (k = 0; k < 200u; k++) { }
            guard++;
        }
        




        if (shared[SH_VIS_BULK] && round > 32u) {
            u32 fb = shared[SH_FB_ADDR];
            u32 pitch = shared[SH_FB_PITCH];
            if (fb && pitch) {
                u32 y, x, t0c;
                for (y = 0; y < 240u; y++) {
                    volatile u32 *d = (volatile u32 *)(fb + y * pitch);
                    for (x = 0; x < 320u; x++) d[x] = 0x00202020u + round;
                }
                t0c = zzq_us_now();
                dcache_clean_range(fb, 240u * pitch);
                shared[SH_VIS_CLEAN_US] = zzq_us_now() - t0c;
            }
        }
        __asm__ volatile("dsb sy" ::: "memory");
        shared[SH_VIS_TOKEN] = 0xA5000000u | round;
        __asm__ volatile("dsb sy" ::: "memory");
    }
    shared[SH_VIS_DONE] = 1;
}

/* ZZ9000 pixel: ARGB big-endian, so the ARM stores it byte-swapped */

/* Write-back probe: prove whether an ARM store to the framebuffer
   address actually sticks. Read-back is done through the same
   pointer, then the line is cleaned so the 68k can see it too. */
static void zzq_fb_writeprobe(void)
{
    u32 fb = shared[SH_FB_ADDR];
    volatile u32 *p;
    if (!fb) { shared[SH_FBPROBE_ADDR] = 0; return; }
    p = (volatile u32 *)fb;
    shared[SH_FBPROBE_ADDR]  = fb;
    shared[SH_FBPROBE_WROTE] = 0xA5C3F00Du;
    p[0] = 0xA5C3F00Du;
    p[1] = 0x5A3C0FF0u;
    dcache_clean_range(fb, 64);
    __asm__ volatile("dsb" ::: "memory");
    shared[SH_FBPROBE_READ]  = p[0];
    /* NO handshake here. Waiting on the shared block in a tight loop
       hammers non-cached memory across the interconnect and starves
       the 68k - that is what froze v19 into a reset loop, the same
       mechanism as the frozen mouse pointer earlier. The 68k-side
       comparison is simply unreliable once anything has drawn, and
       we do not need it: FBTEST already proves the address is right. */
}

/* Framebuffer byte order is B,G,R,A; an ARM u32 store writes the low
   byte first, so plain 0x00RRGGBB is correct. */
#define ZZQ_RGB(r,g,b)  (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

static void zzq_fb_test_frame(u32 phase)
{
    u32 fb    = shared[SH_FB_ADDR];
    u32 pitch = shared[SH_FB_PITCH];
    u32 w     = shared[SH_FB_WIDTH];
    u32 h     = shared[SH_FB_HEIGHT];
    u32 x, y, n = 0, bar;

    if (w == 0) w = 320;
    if (h == 0) h = 240;
    if (pitch == 0) pitch = w * 4u;    /* never divide the screen by 0 */
    if (!fb) { shared[SH_ERROR] = 0x8A; return; }

    bar = phase % w;   /* phase IS the bar position (px) */

    for (y = 0; y < h; y++) {
        u32 *dst = (u32 *)(fb + y * pitch);
        for (x = 0; x < w; x++) {
            u32 c;
            /* VERTICAL bands on purpose. The 68k reference pattern
               uses HORIZONTAL bands, so one glance tells us which of
               the two is actually on screen - the confusion that cost
               us several test cycles. */
            if (y >= bar && y < bar + 8)
                c = ZZQ_RGB(255, 0, 255);           /* magenta bar,
                                                       moves DOWN     */
            else if (x < 16 || x >= w - 16 || y < 16 || y >= h - 16)
                c = ZZQ_RGB(255, 255, 255);         /* white border   */
            else if (x < w / 3)      c = ZZQ_RGB(255, 0, 0);   /* red   */
            else if (x < 2 * w / 3)  c = ZZQ_RGB(0, 255, 0);   /* green */
            else                     c = ZZQ_RGB(0, 0, 255);   /* blue  */
            dst[x] = c;
            n++;
        }
    }
    dcache_clean_range(fb, h * pitch);
    shared[SH_FBTEST_WRITES] = n;
    shared[SH_FRAME]++;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

/* noinline: kept as a SECONDARY alarm only. Since A0.1 the VFP is
   enabled in asm before any C runs, so a VFP prologue here is no
   longer fatal. Keeping the boundary makes the disassembly check
   meaningful and costs nothing. */
static void zzq_main(void) __attribute__((noinline));

/* ---- entry: pure C, real ZZDoom model ---- */
void core1_entry(void *env) __attribute__((section(".text.core1_entry")));







extern u32 _text_start, _text_end, _data_start, _data_end;
extern unsigned long __malloc_sbrk_base;
extern unsigned long __malloc_av_[];
extern unsigned long __malloc_trim_threshold;
extern void *_impure_ptr;



static volatile u32 zzq_bss_probe;




static u32 zzq_cksum_bytes(const volatile u8 *p, u32 n)
{
    u32 s = 0, i;
    for (i = 0; i < n; i++)
        s = (s << 1) ^ (s >> 31) ^ (u32)p[i];
    return s;
}

static void zzq_cksum_pair(int slot_text, int slot_data)
{
    shared[slot_text] = zzq_cksum_bytes((const volatile u8 *)&_text_start,
                                        (u32)((u8 *)&_text_end - (u8 *)&_text_start));
    shared[slot_data] = zzq_cksum_bytes((const volatile u8 *)&_data_start,
                                        (u32)((u8 *)&_data_end - (u8 *)&_data_start));
}

/* Hunk allocation diagnostics. */


static void zzq_malloc_snap(int base)
{
    shared[base + 0] = (u32)__malloc_sbrk_base;
    shared[base + 1] = (u32)__malloc_av_[0];
    shared[base + 2] = (u32)__malloc_av_[1];
    shared[base + 3] = (u32)__malloc_av_[2];
    shared[base + 4] = (u32)__malloc_trim_threshold;
    shared[base + 5] = (u32)(u32)_impure_ptr;
    shared[base + 6] = (u32)heap_ptr;
}


void zzq_diag_stage_d(void)
{
    zzq_cksum_pair(SH_CK_D_TEXT, SH_CK_D_DATA);
    zzq_malloc_snap(SH_MS_D_SBRKBASE);
}

static void zzq_snap(int base)
{
    volatile u32 *sh = (volatile u32 *)ZZQ_SHARED_ARM;
    sh[base + 0] = (u32)zzq_running;
    sh[base + 1] = (u32)zzq_canary;
    sh[base + 2] = *(volatile u32 *)0x0424F000u;
    sh[base + 3] = *(volatile u32 *)0x0424F100u;
}

/* Cache/MMU handling follows the validated Core1 memory contract. */


static void zzq_image_sum_to(int slot)
{
    volatile u32 *sh = (volatile u32 *)ZZQ_SHARED_ARM;
    u32 n = sh[SH_IMG_SIZE] >> 2;
    const volatile u32 *p = (const volatile u32 *)ZZQ_BLOB_ARM;
    u32 s = 0, i;
    if (n == 0 || n > (0x200000u >> 2)) { sh[slot] = 0; return; }
    for (i = 0; i < n; i++) s = (s << 1) ^ (s >> 31) ^ p[i];
    sh[slot] = s;
}

void core1_entry(void *env)
{
    (void)env;

    /* VFP must be enabled before compiled C code executes. */


    {   /* step 1: SCTLR.A=0, unaligned allowed */
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(sctlr));
        sctlr &= ~(1u << 1);
        __asm__ volatile("mcr p15,0,%0,c1,c0,0" :: "r"(sctlr));
        __asm__ volatile("isb");
    }

    

    ((volatile u32 *)ZZQ_SHARED_ARM)[SH_CK_ENTRY]  = (u32)zzq_running;
    ((volatile u32 *)ZZQ_SHARED_ARM)[SH_CK_CANARY] = (u32)zzq_canary;
    zzq_image_sum_to(SH_IMG_SUM_ARM);   /* Cache/MMU handling follows the validated Core1 memory contract. */
    zzq_snap(SH_SNAP_A);
    

    ((volatile u32 *)ZZQ_SHARED_ARM)[SH_TEXT_LEN] =
        (u32)((u8 *)&_text_end - (u8 *)&_text_start);
    ((volatile u32 *)ZZQ_SHARED_ARM)[SH_DATA_LEN] =
        (u32)((u8 *)&_data_end - (u8 *)&_data_start);

    {   /* step 2: zero BSS (before mmu: l1_table[] is in .bss) */
        extern u32 _bss_start, _bss_end;
        u32 *p = &_bss_start;
        while (p < &_bss_end) *p++ = 0;
    }

    ((volatile u32 *)ZZQ_SHARED_ARM)[SH_CK_AFTER_BSS] = (u32)zzq_running;
    zzq_snap(SH_SNAP_B);

    /* Cache/MMU handling follows the validated Core1 memory contract. */




    zzq_bss_probe = ZZQ_BSSPROBE_MAGIC;
    ((volatile u32 *)ZZQ_SHARED_ARM)[SH_BSSPROBE_PRE] = zzq_bss_probe;
    heap_ptr = (char *)ZZQ_HEAP_ARM;       /* step 3 */

    zzq_main();                            /* never returns */
}

static void zzq_main(void)
{
    int i;
    u32 sp_now;

    /* wipe our slot window, then identify */
    /* Wipe our OUTPUT slots only. The 68k launcher writes several
       INPUT slots BEFORE issuing RUN (enables, pak addr/size, cmd,
       fb). Blanket-wiping 1..119 here erased them - that was the A1
       bug (SH_ENABLE_A1, SH_PAK_ADDR/SIZE cleared before being read).
       Preserve every launcher-written slot. */
    for (i = 1; i < ZZQ_SLOT_COUNT; i++) {
        


        if (i >= SH_MOD_NAME && i < SH_MOD_NAME + 16) continue;
        switch (i) {
            case SH_CMD:        case SH_PAK_ADDR:   case SH_PAK_SIZE:
            case SH_FB_ADDR:    case SH_ENABLE_QG:  case SH_ENABLE_PROBE:
            case SH_TEST_UDF:   case SH_ENABLE_A1:
            case SH_ENABLE_DEMO_OFF: case SH_HUNK_MB:
            case SH_ENABLE_CVARFIX:  case SH_SURFCACHE_KB:
            /* Audio uses the shared PCM transport. */


            case SH_PRELOAD_STATE:   case SH_PACK_COUNT:
            case SH_PACK0_ADDR:      case SH_PACK0_SIZE:
            case SH_PACK1_ADDR:      case SH_PACK1_SIZE:
            case SH_PACK2_ADDR:      case SH_PACK2_SIZE:
            case SH_MUSIC_ENABLE:
            case SH_GAME_MODE:      case SH_MOD_COUNT:
            case SH_MOD_ADDR + 0:   case SH_MOD_ADDR + 1:
            case SH_MOD_ADDR + 2:   case SH_MOD_ADDR + 3:
            case SH_MOD_SIZE + 0:   case SH_MOD_SIZE + 1:
            case SH_MOD_SIZE + 2:   case SH_MOD_SIZE + 3:
            /* PAK filesystem handling. */




            case SH_CLEAN_MODE:
            case SH_ENABLE_PASSIVE:  case SH_IMG_SIZE:
            case SH_VIS_GO:          case SH_ENABLE_VISTEST:
            case SH_VIS_BULK:
            case SH_ENABLE_COOP:      case SH_ENABLE_RESTORE:
            case SH_POLL_BACKOFF:
            case SH_DB_ENABLE:       case SH_TIMEDEMO_NOWAIT:
            case SH_TRIPLE:          case SH_RENDER_SEQ:
            case SH_KEYQ_WR:         case SH_PCM_ENABLE:
            case SH_FS_ACK:          case SH_FS_ERR:
            case SH_FS_SLOT:
            case SH_FS_EXISTS:
            case SH_PCM_READ_POS:
            /* Core1 execution path. */


            case SH_PCM_BASE_SLOT:   case SH_PCM_SIZE_SLOT:
            case SH_PCM_RATE:
            case SH_RENDER_FB:
            case SH_ENGINE_MAXFPS:
            case SH_FLIP_SEQ:
            case SH_IMG_SUM_68K:
            case SH_ENABLE_TIMEDEMO:
            case SH_NO_CACHEFIX:     case SH_ENABLE_FBTEST:
            /* Framebuffer ownership is synchronized with the 68k launcher. */





            case SH_FB_PITCH:   case SH_FB_WIDTH:   case SH_FB_HEIGHT:
            case SH_FB_BPP:
            case SH_FBPROBE_ACK:
                break;                 /* input from 68k: keep */
                /* Shared-memory protocol state. */









            default:
                if (i >= SH_KEYQ_BASE && i < SH_KEYQ_BASE + ZZQ_KEYQ_SIZE)
                    break;                 /* 68k event queue */
                shared[i] = 0;         /* output: clear */
        }
    }
    shared[SH_MAGIC]  = ZZQ_MAGIC;
    shared[SH_BUILD_ID] = ZZQ_BUILD_ID;   /* garde de version */
    shared[SH_STATUS] = ZZQ_INIT_START;
    DIAG(0xA001);

    /* publish the firmware-provided SP: tells us on real hardware
       where the stack lives and how much headroom Quake gets */
    __asm__ volatile("mov %0, sp" : "=r"(sp_now));
    shared[SH_ENTRY_SP] = sp_now;
    {   /* Core1 execution path. */

        extern u32 zzq_saved[];
        shared[SH_ENTRY_LR] = zzq_saved[1];
    }

    /* Shared-memory protocol state. */




    zz_install_vectors();
    DIAG(0xA1A1);   /* vectors installed, still alive (ZZDoom marker) */

    /* Optional self-test of the handlers: the 68k sets SH_TEST_UDF=1
       to make us execute a deliberate undefined instruction here and
       verify the abort path end to end. Default 0 = no fault. */
    if (shared[SH_TEST_UDF] == 1) {
        DIAG(0xA1FD);
        __asm__ volatile(".word 0xE7F000F0");  /* permanently UDF */
        /* never reached: zz_undef_h parks in WFE */
    }

    mmu_init_zzquake(shared[SH_FB_ADDR]);
    DIAG(0xA004);

    shared[SH_CK_AFTER_MMU] = (u32)zzq_running;
    zzq_snap(SH_SNAP_C);
    /* Cache/MMU handling follows the validated Core1 memory contract. */
    shared[SH_BSSPROBE_POST] = zzq_bss_probe;
    zzq_cksum_pair(SH_CK_C_TEXT, SH_CK_C_DATA);
    zzq_malloc_snap(SH_MS_C_SBRKBASE);
    zzq_s2_pmu_init();       /* Optional renderer profiling hooks. */
    
    zzq_image_sum_to(SH_IMG_SUM_ARM2);
    zzq_timer_diag();      /* read-only, runs in every mode */
    DIAG(0xA005);
    zzq_fb_writeprobe();   /* does an ARM store to the fb stick?     */
    DIAG(0xA006);

    float_selftest();
    if (shared[SH_ENABLE_PROBE])   /* DEFAULT OFF (guardrail #1):
                                      A0 first validates the vital
                                      signs with zero side effects */
        ddr_probe();

    shared[SH_HUNK_SIZE] = (u32)ZZQ_HUNK_SIZE;

    
    if (shared[SH_ENABLE_VISTEST]) {
        DIAG(0xA0D0);
        zzq_visibility_test();
        shared[SH_EXIT_REASON] = ZZQ_EXIT_MODE_END;
        zzq_end_wfe();
    }

    /* Core1 execution path. */



    /* Framebuffer ownership is synchronized with the 68k launcher. */




    if (shared[SH_ENABLE_PASSIVE] == ZZQ_PASSIVE_POSTCLEAN) {
        void *mb;
        DIAG(0xA0C2);
        shared[SH_PC_ENTERED] = ZZQ_PC_MAGIC_IN;
        shared[SH_PC_B_SBRK]  = (u32)__malloc_sbrk_base;
        shared[SH_PC_B_AV1]   = (u32)__malloc_av_[1];
        shared[SH_PC_B_AV2]   = (u32)__malloc_av_[2];
        shared[SH_PC_B_HEAP]  = (u32)heap_ptr;

        mb = malloc(8UL * 1024UL * 1024UL);   
        shared[SH_PC_MALLOC]  = (u32)mb;

        shared[SH_PC_A_SBRK]  = (u32)__malloc_sbrk_base;
        shared[SH_PC_A_AV1]   = (u32)__malloc_av_[1];
        shared[SH_PC_A_AV2]   = (u32)__malloc_av_[2];
        if (mb) free(mb);

        __asm__ volatile("dsb sy" ::: "memory");
        shared[SH_PC_DONE]     = ZZQ_PC_MAGIC_OUT;
        shared[SH_STATUS]      = ZZQ_A0_IDLE;
        shared[SH_EXIT_REASON] = ZZQ_EXIT_MODE_END;
        zzq_end_wfe();
        return;
    }

    if (shared[SH_ENABLE_PASSIVE]) {
        DIAG(0xA0C0);
        shared[SH_STATUS] = ZZQ_A0_IDLE;
        shared[SH_EXIT_REASON] = ZZQ_EXIT_MODE_END;
        zzq_end_wfe();
    }

    /* ---- A2a path: framebuffer self-test, NO Quake at all ---- */
    if (shared[SH_ENABLE_FBTEST]) {
        DIAG(0xA0B0);
        shared[SH_STATUS] = ZZQ_FIRST_FRAME_OK;
        {
            










            





            u32 bar = 0;
            u32 w = shared[SH_FB_HEIGHT] ? shared[SH_FB_HEIGHT] : 240u;
            while (shared[SH_CMD] != ZZQ_CMD_STOP) {
                volatile u32 s;
                zzq_fb_test_frame(bar);
                bar = (bar + 3u) % w;
                shared[SH_HB]++;
                for (s = 0; s < 300000u; s++) { }   /* ~20 fps */
            }
        }
        shared[SH_CMD] = ZZQ_CMD_NONE;
        zzq_end_wfe();
    }

    /* ---- A1 path: filesystem self-test, QG NEVER called ---- */
    /* Cache/MMU handling follows the validated Core1 memory contract. */






    if (shared[SH_PRELOAD_STATE] == ZZQ_PRELOAD_COPY) {
        u32 last_ack = 0;
        DIAG(0xA01E);
        shared[SH_PRELOAD_STATE] = ZZQ_PRELOAD_READY;
        __asm__ volatile("dsb sy" ::: "memory");
        for (;;) {
            u32 seq;
            if (shared[SH_PRELOAD_STATE] == ZZQ_PRELOAD_START) break;
            seq = shared[SH_PRELOAD_SEQ];
            if (seq != last_ack) {
                u32 src = shared[SH_PRELOAD_SRC];
                u32 dst = shared[SH_PRELOAD_DST];
                u32 sz  = shared[SH_PRELOAD_SIZE];
                

                if (dst < ZZQ_PACK_ARENA_BASE ||
                    sz  > (ZZQ_PACK_ARENA_END - ZZQ_PACK_ARENA_BASE) ||
                    dst > ZZQ_PACK_ARENA_END - sz ||
                    dst + sz < dst ||
                    src < ZZQ_STAGING_ARM ||
                    sz  > ZZQ_STAGING_SIZE ||
                    src > ZZQ_STAGING_ARM + ZZQ_STAGING_SIZE - sz) {
                    shared[SH_PRELOAD_ERR]   = dst;
                    shared[SH_PRELOAD_STATE] = ZZQ_PRELOAD_ERROR;
                    shared[SH_STATUS] = ZZQ_A0_IDLE;
                    zzq_end_wfe();
                    return;
                }
                {   /* Hunk allocation diagnostics. */




                    const volatile u32 *s32 = (const volatile u32 *)src;
                    volatile u32 *d32 = (volatile u32 *)dst;
                    u32 words = sz >> 2, k;
                    for (k = 0; k < words; k++) d32[k] = s32[k];
                    if (sz & 3u) {
                        const volatile u8 *s8 = (const volatile u8 *)(src + (words << 2));
                        volatile u8 *d8 = (volatile u8 *)(dst + (words << 2));
                        u32 t = sz & 3u;
                        for (k = 0; k < t; k++) d8[k] = s8[k];
                    }
                }
                __asm__ volatile("dsb sy" ::: "memory");
                last_ack = seq;
                shared[SH_PRELOAD_ACK] = seq;
                __asm__ volatile("dsb sy" ::: "memory");
            }
        }
        DIAG(0xA01F);
    }

    if (shared[SH_ENABLE_A1]) {
        if (!pak_check())          /* validates addr/size/PACK/dir */
            zzq_end_wfe();
        zzq_a1_fstest();           /* exercises Sys_File* only */
        /* idle so the 68k can read the result, then WFE */
        while (shared[SH_CMD] != ZZQ_CMD_STOP) {
            volatile int spin;
            for (spin = 0; spin < 100000; spin++) { }
            shared[SH_HB]++;
        }
        shared[SH_CMD] = ZZQ_CMD_NONE;
        zzq_end_wfe();
    }

    if (shared[SH_ENABLE_QG]) {
        /* ---- A2 path ---- */
        if (!pak_check())
            zzq_end_wfe();

        /* CRITICAL (bug found in A2 rev5): invalidate the D-cache over
           the WHOLE pak region before Quake reads a single byte.
           Caches stay ON at the WFE terminal (mmu_disable is
           deliberately never called), so stale lines from a previous
           run survive on this exact address range. Quake then read a
           mix of fresh and stale lines: the .mdl version field passed
           its check while numverts/numtris came back as garbage,
           producing "Hunk_Alloc: failed on 1766355424 bytes".
           The A1 path already did this - A2 never did. */
        /* Cache/MMU handling follows the validated Core1 memory contract. */






        /* Cache/MMU handling follows the validated Core1 memory contract. */






        if (shared[SH_CLEAN_MODE]) {
            void *mb;
            shared[SH_CLEAN_ENTERED] = 1;
            shared[SH_CLEAN_B_SBRK] = (u32)__malloc_sbrk_base;
            shared[SH_CLEAN_B_AV1]  = (u32)__malloc_av_[1];
            shared[SH_CLEAN_B_AV2]  = (u32)__malloc_av_[2];
            shared[SH_CLEAN_B_HEAP] = (u32)heap_ptr;

            mb = malloc(ZZQ_HUNK_SIZE);   


            shared[SH_CLEAN_MALLOC] = (u32)mb;

            shared[SH_CLEAN_A_SBRK] = (u32)__malloc_sbrk_base;
            shared[SH_CLEAN_A_AV1]  = (u32)__malloc_av_[1];
            shared[SH_CLEAN_A_AV2]  = (u32)__malloc_av_[2];
            if (mb) free(mb);

            shared[SH_CLEAN_DONE] = 1;
            shared[SH_EXIT_REASON] = ZZQ_EXIT_MODE_END;
            shared[SH_STATUS] = ZZQ_QUIT_REQUESTED;
            zzq_end_wfe();
            return;                       
        }


        DIAG(0xA020);
        shared[SH_CK_BEFORE_QG] = (u32)zzq_running;
        shared[SH_STATUS] = ZZQ_QG_CREATE_START;
        {
            


            static char *qargv[4];
            int qargc = 1;
            qargv[0] = "zzquake";
            if (shared[SH_GAME_MODE] == ZZQ_GAME_HIPNOTIC)
                qargv[qargc++] = "-hipnotic";
            else if (shared[SH_GAME_MODE] == ZZQ_GAME_ROGUE)
                qargv[qargc++] = "-rogue";
            else if (shared[SH_GAME_MODE] == ZZQ_GAME_MOD) {
                

                static char modname[68];
                int mi;
                for (mi = 0; mi < 16; mi++) {
                    u32 w = shared[SH_MOD_NAME + mi];
                    modname[mi*4+0] = (char)(w & 0xFF);
                    modname[mi*4+1] = (char)((w >> 8) & 0xFF);
                    modname[mi*4+2] = (char)((w >> 16) & 0xFF);
                    modname[mi*4+3] = (char)((w >> 24) & 0xFF);
                }
                modname[63] = 0;
                if (modname[0]) {
                    qargv[qargc++] = "-game";
                    qargv[qargc++] = modname;
                }
            }
            qargv[qargc] = 0;
            shared[SH_MP_ARGC] = (u32)qargc;
            shared[SH_MP_MODE_SEEN] = shared[SH_GAME_MODE];
            QG_Create(qargc, qargv);      /* Host_Init: loads palette,
                                             gfx.wad, progs... from the
                                             fake fs. Fatal errors go
                                             through Sys_Error. */
        }
        shared[SH_STATUS] = ZZQ_QG_CREATE_OK;
        DIAG(0xA030);
        /* Sanity: zzq_running must still be set here. If it is not,
           nothing legitimate cleared it - QG_Quit and the STOP path
           both stamp their own reason - so the flag was corrupted. */
        shared[SH_RUNNING_AT_LOOP] = (u32)zzq_running;

        {
            double oldtime = Sys_FloatTime();
            while (zzq_running) {
                double newtime = Sys_FloatTime();
                double dt = newtime - oldtime;
                u32 t0;
                oldtime = newtime;
                if (dt > 0.1) dt = 0.1;   /* same cap as sdl2 main */

                t0 = zzq_us_now();
                QG_Tick(dt);
                shared[SH_QG_TICK_US]    = zzq_us_now() - t0;
                shared[SH_FRAME_TOTAL_US]= shared[SH_QG_TICK_US];

                shared[SH_HB]++;
                if (shared[SH_CMD] == ZZQ_CMD_STOP) {
                    shared[SH_CMD] = ZZQ_CMD_NONE;
                    shared[SH_EXIT_REASON] = ZZQ_EXIT_CMD_STOP;
                    zzq_running = 0;
                }
            }
        }
        





        Host_Shutdown();
        if (!shared[SH_EXIT_REASON])
            shared[SH_EXIT_REASON] = ZZQ_EXIT_CREATE_RET;
        shared[SH_STATUS] = ZZQ_QUIT_REQUESTED;
    } else {
        /* ---- A0 path: platform validated, idle with heartbeat ---- */
        DIAG(0xA0FF);
        shared[SH_STATUS] = ZZQ_A0_IDLE;
        while (shared[SH_CMD] != ZZQ_CMD_STOP) {
            volatile int spin;
            u32 sp;
            for (spin = 0; spin < 100000; spin++) { }
            __asm__ volatile("mov %0, sp" : "=r"(sp));
            shared[SH_SP_NOW] = sp;
            shared[SH_HB]++;
        }
        shared[SH_CMD] = ZZQ_CMD_NONE;
    }

    zzq_end_wfe();
}
