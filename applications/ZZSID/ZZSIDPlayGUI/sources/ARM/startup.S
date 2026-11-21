/*
 * startup.S - ZZReSID Core1 bare-metal startup, with clean hand-back.
 *
 * WHY THIS FILE CHANGED
 * ---------------------
 * The firmware runs Core1 inside core1_loop(), a cooperative dispatcher:
 *
 *     while (1) {
 *         while (!core2_execute) usleep(1);
 *         core2_execute = 0;
 *         Xil_DCacheFlush(); Xil_ICacheInvalidate();
 *         push {r0-r12}; save sp at 0x00010000;
 *         core1_trampoline(&arm_run_env);     <- our blob runs here
 *         restore sp; pop {r0-r12};
 *     }
 *
 * The firmware therefore ALREADY owns the idle loop and the trampoline. A
 * payload gives Core1 back simply by RETURNING. There is no STOP API and
 * none is needed. The previous version of this file installed its own stack
 * over the firmware's, discarded lr, and ended in "wfe; b .", so Core1 could
 * never be handed back - which is the only reason a power cycle was needed
 * between blobs. That was a bug in our payload, not a hardware limit.
 *
 * WHAT THE RETURN PATH MUST RESTORE
 * ---------------------------------
 * core1_loop() flushes caches BEFORE calling us, not after, so cleaning up
 * is our job. We hand back exactly what we were given:
 *   - r0-r12, lr and sp             (r0 carried &arm_run_env)
 *   - SCTLR, TTBR0, TTBCR, DACR     (we install our own translation table)
 *   - CPACR, FPEXC                  (we turn the FPU on)
 * The D-cache is cleaned in C, with the MMU still enabled, before we reach
 * the return path - a clean by set/way is meaningless once SCTLR.M is clear.
 *
 * The order on the way out is deliberate: invalidate I-cache, branch
 * predictor and TLB, then put the translation registers back, and only then
 * SCTLR, because SCTLR is the switch that actually returns the MMU and
 * caches to the firmware's configuration.
 *
 * Blob loads at ARM 0x04200000, _start is the first instruction.
 *
 * ASCII-only.  Author: Xanxi / 2026
 */

    .syntax unified
    .arm

/* Parking slot for the firmware's stack pointer.
 * MUST live in .data: .bss is cleared below, which would wipe it. */
    .section .data
    .align  2
fw_sp:
    .word   0

    .section .text.startup, "ax"
    .global _start
    .global zzresid_core1_main

_start:
    /* ---- Save the firmware context on the FIRMWARE stack ----
     * Done before sp is touched, so the whole hand-back state travels on
     * the stack we were called with. */
    push    {r0-r12, lr}

    mrc     p15, 0, r4, c1, c0, 0   /* SCTLR */
    mrc     p15, 0, r5, c2, c0, 0   /* TTBR0 */
    mrc     p15, 0, r6, c2, c0, 2   /* TTBCR */
    mrc     p15, 0, r7, c3, c0, 0   /* DACR  */
    mrc     p15, 0, r8, c1, c0, 2   /* CPACR */
    vmrs    r9, FPEXC
    push    {r4-r9}

    ldr     r10, =fw_sp
    str     sp, [r10]

    /* ---- Our own stack ---- */
    ldr     sp, =_stack_top

    /* ---- Enable FPU (VFP/NEON) ---- */
    mrc     p15, 0, r0, c1, c0, 2   /* CPACR */
    orr     r0, r0, #(0xF << 20)    /* CP10+CP11 full access */
    mcr     p15, 0, r0, c1, c0, 2
    isb

    vmrs    r0, FPEXC
    orr     r0, r0, #(1 << 30)      /* EN */
    vmsr    FPEXC, r0
    dsb

    /* ---- SCTLR: I-cache + branch prediction ON, D-cache OFF ----
     * mmu_init() in the blob takes over and turns the D-cache on once a
     * translation table exists. */
    mrc     p15, 0, r0, c1, c0, 0
    orr     r0, r0, #(1 << 12)      /* I-cache */
    orr     r0, r0, #(1 << 11)      /* branch prediction */
    bic     r0, r0, #(1 << 2)       /* D-cache OFF */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* ---- Zero BSS (fw_sp is in .data, so it survives this) ---- */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    bcs     2f
    str     r2, [r0], #4
    b       1b
2:

    /* ---- Run the payload ----
     * zzresid_core1_main() returns on CMD_STOP, after publishing
     * STATUS=STOPPED and cleaning the D-cache by set/way. */
    bl      zzresid_core1_main

    /* ---- Hand Core1 back to core1_loop() ---- */
    ldr     r10, =fw_sp
    ldr     sp, [r10]
    pop     {r4-r9}                 /* SCTLR TTBR0 TTBCR DACR CPACR FPEXC */

    /* FPU back to the firmware's setting */
    vmsr    FPEXC, r9
    mcr     p15, 0, r8, c1, c0, 2   /* CPACR */
    isb

    /* Drop everything speculative that belongs to our address space */
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0   /* ICIALLU - invalidate I-cache       */
    mcr     p15, 0, r0, c7, c5, 6   /* BPIALL  - invalidate branch pred   */
    mcr     p15, 0, r0, c8, c7, 0   /* TLBIALL - invalidate TLB           */
    dsb
    isb

    /* Translation registers first, SCTLR last: SCTLR is the switch. */
    mcr     p15, 0, r7, c3, c0, 0   /* DACR  */
    mcr     p15, 0, r6, c2, c0, 2   /* TTBCR */
    mcr     p15, 0, r5, c2, c0, 0   /* TTBR0 */
    dsb
    isb
    mcr     p15, 0, r4, c1, c0, 0   /* SCTLR */
    dsb
    isb

    pop     {r0-r12, lr}
    bx      lr

    /* Unreachable. Kept so that a payload which somehow falls through does
     * not run into whatever follows in memory. */
3:
    wfe
    b       3b

    .end
