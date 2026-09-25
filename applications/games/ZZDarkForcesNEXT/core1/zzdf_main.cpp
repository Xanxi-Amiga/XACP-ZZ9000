 










#include <TFE_System/types.h>

extern "C" {
#include "zzdf_config.h"

extern void mmu_init_zzdf(u32 fb_arm);
/* mmu_disable / zzdf_return_to_firmware are deliberately NOT declared
   here: Core1 reuse is unsafe, see zzdf_end_park(). */
extern void dcache_clean_range(u32 start, u32 len);
extern void zzdf_rb_clean_last(void);   /* platform/zzdf_renderbackend.cpp */
extern void dcache_invalidate_range(u32 start, u32 len);
extern void zz_install_vectors(void);
extern void zzdf_fs_init(void);
extern u32  zzdf_fs_count(void);
extern void zzdf_log_puts(const char *s);
extern u32  zzdf_us_now(void);
extern unsigned long long zzdf_gtimer_read(void);
extern void __libc_init_array(void);

extern u32 _bss_start, _bss_end;
extern u32 __init_array_start, __init_array_end;
#ifndef ZZDF_LEGACY_WFE_PARK
/* Cooperative return (core1/zzdf_return.c): publishes the captured
   firmware context and, at the end, hands Core1 back to core1_loop()
   instead of parking. Production default since 17.09.2026. */
extern void zzdf_return_publish_saved(void);
extern void zzdf_return_teardown(void);
#endif
}

volatile u32 *shared = (volatile u32 *)ZZDF_SHARED_ARM;

#define DIAG(x) do { shared[SH_DIAG] = (x); \
    __asm__ volatile("dsb":::"memory"); } while(0)

/* ------------------------------------------------------------------ */
/* Termination: SINGLE-SHOT RESIDENT PARK                             */
/*                                                                    */
 
/* run, giving Core1 back is not safe. WFE park let a second cycle    */
/* start but the C++ runtime aborted (DIAG D010, ERROR E0000003);     */
/* cooperative CP15 restore was worse - the second cycle never even   */
/* reached zzdf_entry - and afterwards JuliaV2, the reference Core1   */
/* application, no longer ran either in the same machine session.     */
/*                                                                    */
/* So Core1 reuse is UNSAFE / DEFERRED. We do NOT hand Core1 back:    */
/* no zzdf_return_to_firmware(), no CP15 restore, no MMU teardown.    */
/* We publish the final state, keep OUR mapping live and stable, and  */
/* park forever. The machine is powered off normally when the user is */
/* done. The launcher refuses a second run via the session cookies.   */
/* ------------------------------------------------------------------ */
static void zzdf_end_park(void)
{
    __asm__ volatile("dsb":::"memory");
    shared[SH_STATUS] = 0xFF;
    __asm__ volatile("dsb":::"memory");

    /* clean the buffer we last rendered into - in triple-buffer mode
       SH_FB_ADDR is only the initial MMU anchor, not the live target */
    zzdf_rb_clean_last();
    if (shared[SH_FB_ADDR] && shared[SH_FB_PITCH])
        dcache_clean_range(shared[SH_FB_ADDR],
                           shared[SH_FB_HEIGHT] * shared[SH_FB_PITCH]);
    __asm__ volatile("dsb":::"memory");

#ifndef ZZDF_LEGACY_WFE_PARK
    /* The game loop is left, input and present are over, STATUS=0xFF is
       out and the framebuffer is clean. Hand Core1 back to
       core1_loop(). Returns only if the hand-back is refused (gate in
       zzdf_return.c, reason in SH_RETURN_STATE): then park. */
    zzdf_return_teardown();
#endif

    /* MMU stays ON. Nothing is restored. Park. */
    for (;;) { __asm__ volatile("wfe"); }
}

extern "C" void zzdf_fatal_end(u32 code)
{
    shared[SH_ERROR] = code;
    /* No mmu_disable(): our mapping must stay valid while parked. */
    zzdf_end_park();
}

/* ------------------------------------------------------------------ */
/* Heap for newlib _sbrk (malloc, operator new, STL, TFE regions)     */
/*                                                                    */
/* DYNAMIC: the launcher inventories every asset before ARM_RUN,      */
/* aligns the end of the assets up to 1 MiB and publishes it in       */
/* SH_HEAP_BASE; SH_HEAP_END is always 0x3F800000 (the guard). The    */
/* heap is therefore "everything left in the 248 MiB super-zone",     */
/* typically well over 100 MiB. The launcher refuses to start if that */
/* would be under 64 MiB.                                             */
/*                                                                    */
/* Bounds are validated ONCE at heap init, before any malloc: a base  */
/* outside the super-zone, unaligned, or an end that is not the guard */
/* is fatal (park) - never a silent write somewhere else. The old     */
/* fixed 16 MiB window at 0x07000000 is no longer used: TFE           */
/* game_init() alone needs two 8 MiB regions plus their headers.      */
/* ------------------------------------------------------------------ */
static char *heap_ptr;
static char *heap_base;
static char *heap_end;

static int zzdf_heap_init(void)
{
    u32 base = shared[SH_HEAP_BASE];
    u32 end  = shared[SH_HEAP_END];

    if (base < ZZDF_HIGH_DDR_BASE)            return 0;
    if (base & (ZZDF_HEAP_ALIGN - 1))          return 0;
    if (end != ZZDF_HEAP_END)                 return 0;
    if (base >= end)                          return 0;
    if (end - base < ZZDF_HEAP_MIN)           return 0;

    heap_base = (char *)base;
    heap_end  = (char *)end;
    heap_ptr  = heap_base;
    shared[SH_HEAP_BREAK] = base;
    shared[SH_HEAP_USED]  = 0;
    return 1;
}

extern "C" void *_sbrk(int incr)
{
    char *old;
    if (heap_ptr == 0 || heap_end == 0) {
        /* _sbrk before zzdf_heap_init(): the runtime is not up yet */
        shared[SH_ERROR] = ZZDF_FATAL_HEAP_BOUNDS;
        return (void *)-1;
    }
    old = heap_ptr;
    if (incr < 0) {
        if ((u32)(-incr) > (u32)(heap_ptr - heap_base)) {
            shared[SH_ERROR] = ZZDF_FATAL_OUT_OF_MEM;
            return (void *)-1;
        }
    } else {
        if ((u32)incr > (u32)(heap_end - heap_ptr)) {
            shared[SH_ERROR] = ZZDF_FATAL_OUT_OF_MEM;
            return (void *)-1;
        }
    }
    heap_ptr += incr;
    shared[SH_HEAP_BREAK] = (u32)heap_ptr;
    shared[SH_HEAP_USED]  = (u32)(heap_ptr - heap_base);
    return old;
}

/* ------------------------------------------------------------------ */
/* VFP self-test: compiled hard-float - if VFP were off we would      */
/* already have faulted; publish a computed token as positive proof.  */
/* ------------------------------------------------------------------ */
static void float_selftest(void)
{
    volatile float a = 3.5f, b = 2.0f;
    volatile double d = 1.0;
    if (a * b == 7.0f && d + d == 2.0) shared[SH_FLOAT_DIAG] = ZZDF_FLOAT_OK;
    else                               shared[SH_FLOAT_DIAG] = 0xBADF10A7u;
}

/* ------------------------------------------------------------------ */
 
/* ------------------------------------------------------------------ */
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/RClassic_Fixed/rclassicFixed.h>
#include <TFE_Jedi/Renderer/RClassic_Float/rclassicFloat.h>
#include <vector>
#include <string>

static void n0_probe(void)
{
    shared[SH_PROBE_RESULT] = 0;

    /* stage 1: libstdc++ heap objects */
    DIAG(0xD010);
    {
        std::vector<int> v;
        for (int i = 0; i < 1000; i++) v.push_back(i * 3);
        std::string s("zzdarkforces-next-n0");
        if (v[999] != 2997 || s.size() != 20) {
            shared[SH_PROBE_RESULT] = 2;
            shared[SH_PROBE_DETAIL] = (u32)v[999];
            return;
        }
    }

    /* stage 2: construct + destroy a real TFE object */
    DIAG(0xD011);
    {
        MemoryRegion* r =
            TFE_Memory::region_create("n0probe", 1u << 20);
        if (!r) { shared[SH_PROBE_RESULT] = 3; return; }
        void* p = TFE_Memory::region_alloc(r, 65536);
        if (!p) { shared[SH_PROBE_RESULT] = 4; return; }
        for (u32 i = 0; i < 65536; i += 4096) ((u8*)p)[i] = (u8)i;
        TFE_Memory::region_destroy(r);
    }

    /* stage 3: both software renderers linked - take real function
       addresses so the linker cannot GC them and publish a mask. */
    DIAG(0xD012);
    {
        void (*fx)() = &TFE_Jedi::RClassic_Fixed::resetState;
        void (*fl)() = &TFE_Jedi::RClassic_Float::resetState;
        u32 mask = 0;
        if (fx) mask |= 1u;
        if (fl) mask |= 2u;
        shared[SH_RENDERERS_SEEN] = mask;
        if (mask != 3u) { shared[SH_PROBE_RESULT] = 5; return; }
    }

    DIAG(0xD013);
    shared[SH_PROBE_RESULT] = 1;   /* PASS */
}

/* ------------------------------------------------------------------ */
 
/* ------------------------------------------------------------------ */
extern "C" int zzdf_run_game(void);
extern "C" int zzdf_preload_service(volatile u32 *sh);

/* ------------------------------------------------------------------ */
static void zzdf_main_loop(void)
{
    u32 hb = 0;

    shared[SH_MAGIC]  = ZZDF_MAGIC;
    shared[SH_STATUS] = 0xAA;
    DIAG(0xD001);

    {
        u32 sp_now;
        __asm__ volatile("mov %0, sp" : "=r"(sp_now));
        shared[SH_SP_NOW] = sp_now;
    }

    float_selftest();
    DIAG(0xD002);

     





    if (shared[SH_PRELOAD_STATE] == ZZDF_PRELOAD_COPY) {
        DIAG(0xD005);
        if (zzdf_preload_service(shared) < 0) {
            shared[SH_ERROR] = ZZDF_FATAL_PRELOAD;
            zzdf_end_park();
        }
        DIAG(0xD006);
    }

    zzdf_fs_init();
    DIAG(0xD003);

    if (shared[SH_ENABLE_GAME]) {
        DIAG(0xD020);
         



        zzdf_run_game();
    } else {
        n0_probe();
    }

     


    if (!shared[SH_ENABLE_GAME]) DIAG(0xD004);
     




    if (shared[SH_ENABLE_GAME])
        zzdf_log_puts("[ZZDF] parked, waiting for the launcher (CMD=1)\n");
    while (shared[SH_CMD] != 1) {
        shared[SH_HB] = ++hb;
        shared[SH_TIME_LO] = zzdf_us_now();
        {
            u32 sp_now;
            __asm__ volatile("mov %0, sp" : "=r"(sp_now));
            shared[SH_SP_NOW] = sp_now;
        }
        /* NO wfe here: active poll (absolute rule) */
        for (volatile int d = 0; d < 200000; d++) { }
    }
    shared[SH_CMD] = 0;

     

    if (!shared[SH_ENABLE_GAME]) DIAG(0xD0FF);
    zzdf_end_park();
}

/* ------------------------------------------------------------------ */
extern "C" void core1_entry(void *env)
    __attribute__((section(".text.core1_entry")));

extern "C" void core1_entry(void *env)
{
    (void)env;

    {   /* SCTLR.A=0, unaligned allowed, before anything else */
        u32 sctlr;
        __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(sctlr));
        sctlr &= ~(1u << 1);
        __asm__ volatile("mcr p15,0,%0,c1,c0,0" :: "r"(sctlr));
        __asm__ volatile("isb");
    }

    ((volatile u32 *)ZZDF_SHARED_ARM)[SH_DIAG] = 0xD000;

     








    mmu_init_zzdf(((volatile u32 *)ZZDF_SHARED_ARM)[SH_FB_ADDR]);

    ((volatile u32 *)ZZDF_SHARED_ARM)[SH_DIAG] = 0xD011;

    {   /* now zero .bss under OUR WB/WA mapping */
        u32 *p = &_bss_start;
        while (p < &_bss_end) *p++ = 0;
    }

    ((volatile u32 *)ZZDF_SHARED_ARM)[SH_DIAG] = 0xD012;

    zz_install_vectors();

#ifndef ZZDF_LEGACY_WFE_PARK
    /* publish what zzdf_entry captured (SH_FW_*) before anything else
       can go wrong */
    zzdf_return_publish_saved();
#endif

     

    if (((volatile u32 *)ZZDF_SHARED_ARM)[SH_HEAP_BASE] == 0) {
        ((volatile u32 *)ZZDF_SHARED_ARM)[SH_HEAP_BASE] = ZZDF_HIGH_DDR_BASE;
        ((volatile u32 *)ZZDF_SHARED_ARM)[SH_HEAP_END]  = ZZDF_HEAP_END;
    }
    if (!zzdf_heap_init()) {
        ((volatile u32 *)ZZDF_SHARED_ARM)[SH_ERROR] = ZZDF_FATAL_HEAP_BOUNDS;
        ((volatile u32 *)ZZDF_SHARED_ARM)[SH_MAGIC] = ZZDF_MAGIC;
        ((volatile u32 *)ZZDF_SHARED_ARM)[SH_STATUS] = 0xFF;
        __asm__ volatile("dsb":::"memory");
        for (;;) { __asm__ volatile("wfe"); }
    }

    /* C++ static constructors (libstdc++ iostream init, TFE globals) */
    ((volatile u32 *)ZZDF_SHARED_ARM)[SH_CPP_CTORS] =
        (u32)(&__init_array_end - &__init_array_start);
    __libc_init_array();

    zzdf_main_loop();               /* never returns */
}
