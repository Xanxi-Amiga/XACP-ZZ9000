 





















#include "zzdf_config.h"

typedef unsigned int u32;

extern u32  zzdf_saved[];                        /* zzdf_entry.S, 192 B  */
extern void zzdf_return_to_firmware(void) __attribute__((noreturn));

#define SAVED_SP     0
#define SAVED_LR     1
#define SAVED_SCTLR  2
#define SAVED_TTBR0  3
#define SAVED_TTBCR  4
#define SAVED_DACR   5
#define SAVED_VBAR   6
#define SAVED_CPACR  7
#define SAVED_ACTLR  8
#define SAVED_FPEXC  9
#define SAVED_TTBR1  10
#define SAVED_CPSR   11
#define SAVED_FPSCR  12
#define SAVED_MAGIC  13
#define SAVED_FRAME  14   /* [0x00010000] read by the entry stub      */

static volatile u32 *sh = (volatile u32 *)ZZDF_SHARED_ARM;

/* ------------------------------------------------------------------ */
/* Called once by core1_entry after the MMU is up and .bss is zeroed: */
/* publish what the entry stub captured.                              */
/* ------------------------------------------------------------------ */
void zzdf_return_publish_saved(void)
{
    sh[SH_FW_SP]       = zzdf_saved[SAVED_SP];
    sh[SH_FW_LR]       = zzdf_saved[SAVED_LR];
    sh[SH_FW_SCTLR]    = zzdf_saved[SAVED_SCTLR];
    sh[SH_FW_CPSR]     = zzdf_saved[SAVED_CPSR];
    sh[SH_FW_TTBR0]    = zzdf_saved[SAVED_TTBR0];
    sh[SH_FW_VBAR]     = zzdf_saved[SAVED_VBAR];
    sh[SH_FW_FPEXC]    = zzdf_saved[SAVED_FPEXC];
    sh[SH_FW_CPACR]    = zzdf_saved[SAVED_CPACR];
    /* the cell core1_loop uses to restore its SP, as the entry stub
       read it under the firmware mapping (never re-read from here) */
    sh[SH_FW_FRAME_SP] = zzdf_saved[SAVED_FRAME];
    sh[SH_RETURN_STATE] = (zzdf_saved[SAVED_MAGIC] == ZZDF_SAVED_MAGIC)
                          ? ZZDF_RET_SAVED : ZZDF_RET_NO_MAGIC;
    __asm__ volatile("dsb":::"memory");
}

/* ------------------------------------------------------------------ */
/* Called by zzdf_end_park() after STATUS=0xFF and the framebuffer     */
/* clean. Returns ONLY if the hand-back is refused; the caller then    */
/* parks in WFE like the archived legacy build.                       */
/* ------------------------------------------------------------------ */
void zzdf_return_teardown(void)
{
    u32 cpsr, sp, lr;

    if (sh[SH_ENABLE_RESTORE] != 1u) {
        sh[SH_RETURN_STATE] = ZZDF_RET_REFUSED_ARM; return;
    }
    if (zzdf_saved[SAVED_MAGIC] != ZZDF_SAVED_MAGIC) {
        sh[SH_RETURN_STATE] = ZZDF_RET_REFUSED_MAG; return;
    }
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
     






    if ((cpsr & 0x1Fu) != 0x13u) {
        sh[SH_RETURN_STATE] = ZZDF_RET_REFUSED_MOD; return;
    }
    if ((zzdf_saved[SAVED_CPSR] & 0x1Fu) != 0x13u) {
        sh[SH_RETURN_STATE] = ZZDF_RET_REFUSED_MOD; return;
    }
    sp = zzdf_saved[SAVED_SP];
    lr = zzdf_saved[SAVED_LR];
    if (sp == 0u || (sp & 3u) != 0u || lr == 0u) {
        sh[SH_RETURN_STATE] = ZZDF_RET_REFUSED_CTX; return;
    }
    /* the frame core1_loop will pop must be just above the incoming SP */
    if (zzdf_saved[SAVED_FRAME] < sp || zzdf_saved[SAVED_FRAME] - sp >= 0x1000u) {
        sh[SH_RETURN_STATE] = ZZDF_RET_REFUSED_CTX; return;
    }

    sh[SH_RETURN_STATE] = ZZDF_RET_CHECKED;
    __asm__ volatile("dsb":::"memory");
    zzdf_return_to_firmware();                   /* never returns      */
}
