/* VFP must be enabled before compiled C code executes. */

 

















#ifndef ZZDF_LEGACY_WFE_PARK

/* ------------------------------------------------------------------ */
/* COOPERATIVE-RETURN ENTRY (default)                                                   */
/*                                                                    */
/* Firmware side (core2.c core1_loop, XX19c2):                        */
/*     L1 maintenance                                                  */
 
/*     str  sp, [0x00010000]                                           */
/*     core1_trampoline(&arm_run_env)   -> ... -> blx zzdf_entry       */
/* The push frame and the saved-SP cell are written AFTER the          */
/* firmware flush, so they sit dirty in Core1's L1. mmu_init_zzdf()    */
/* later INVALIDATES L1 by set/way (no clean) - that would drop them.  */
/* So, before anything else, clean by MVA (CP15, core-local, no PL310) */
/* the lines that hold: the firmware stack above SP, the cell at        */
/* 0x00010000 and the frame it points to, and zzdf_saved itself.       */
/* ------------------------------------------------------------------ */

     











    .section .text.zzdf_entry, "ax"
    .global  zzdf_entry
    .type    zzdf_entry, %function

zzdf_entry:
    ldr     r1, =zzdf_saved
    str     sp, [r1, #0]                  /* incoming SP (trampoline)  */
    str     lr, [r1, #4]                  /* return into trampoline    */
    mrc     p15, 0, r2, c1, c0, 0
    str     r2, [r1, #8]                  /* SCTLR                    */
    mrc     p15, 0, r2, c2, c0, 0
    str     r2, [r1, #12]                 /* TTBR0                    */
    mrc     p15, 0, r2, c2, c0, 2
    str     r2, [r1, #16]                 /* TTBCR                    */
    mrc     p15, 0, r2, c3, c0, 0
    str     r2, [r1, #20]                 /* DACR                     */
    mrc     p15, 0, r2, c12, c0, 0
    str     r2, [r1, #24]                 /* VBAR                     */
    mrc     p15, 0, r2, c1, c0, 2
    str     r2, [r1, #28]                 /* CPACR (original)         */
    mrc     p15, 0, r2, c1, c0, 1
    str     r2, [r1, #32]                 /* ACTLR                    */
    mrc     p15, 0, r2, c2, c0, 1
    str     r2, [r1, #40]                 /* TTBR1                    */
    mrs     r2, cpsr
    str     r2, [r1, #44]                 /* CPSR (mode, I, F)        */

    /* VFP: cp10/cp11 access is needed even to READ FPEXC, so grant
       it first, record the ORIGINAL FPEXC, then enable as usual. */
    mrc     p15, 0, r12, c1, c0, 2
    orr     r12, r12, #(0xF << 20)
    mcr     p15, 0, r12, c1, c0, 2
    isb
    vmrs    r2, fpexc
    str     r2, [r1, #36]                 /* FPEXC (original)         */
    mov     r12, #0x40000000
    vmsr    fpexc, r12
    isb
    vmrs    r2, fpscr
    str     r2, [r1, #48]                 /* FPSCR                    */
    add     r2, r1, #96
    vstmia  r2, {d8-d15}                  /* callee-saved VFP regs    */
    ldr     r2, =0x5A5AFEED
    str     r2, [r1, #52]                 /* MAGIC: capture complete  */

    /* ---- clean to the point of coherency, CP15 by MVA, local ---- */
     

    mov     r2, sp
    bic     r2, r2, #31
    mov     r3, #8
1:  mcr     p15, 0, r2, c7, c10, 1        /* DCCMVAC                  */
    add     r2, r2, #32
    subs    r3, r3, #1
    bne     1b
    /* (b) the saved-SP cell itself */
    mov     r2, #0x00010000
    mcr     p15, 0, r2, c7, c10, 1
    /* (c) the frame the cell points to, if it is plausibly just
           above us (0..4 KiB); 3 lines cover 52 bytes at any offset.
           The value is kept (+56) so C never re-reads 0x00010000. */
    ldr     r3, [r2]
    str     r3, [r1, #56]
    sub     r12, r3, sp
    cmp     r12, #0x1000
    bhs     2f
    bic     r3, r3, #31
    mcr     p15, 0, r3, c7, c10, 1
    add     r3, r3, #32
    mcr     p15, 0, r3, c7, c10, 1
    add     r3, r3, #32
    mcr     p15, 0, r3, c7, c10, 1
2:  /* (d) zzdf_saved: 6 lines */
    mov     r2, r1
    mov     r3, #6
3:  mcr     p15, 0, r2, c7, c10, 1
    add     r2, r2, #32
    subs    r3, r3, #1
    bne     3b
    dsb

    b       core1_entry                    

/* ------------------------------------------------------------------ */
/* TEARDOWN: hand Core1 back to core1_loop()               */
/*                                                                    */
/* Called from zzdf_return_teardown() (C) once the game loop is left, */
/* input and present are stopped, STATUS=0xFF is published and the    */
/* framebuffer is cleaned. Everything below is core-local CP15 work:  */
/* no PL310 access, no global maintenance, no firmware call.          */
 
 
/* switch (loaded BEFORE the switch: no memory read after it).        */
/* ------------------------------------------------------------------ */
    .global zzdf_return_to_firmware
    .type   zzdf_return_to_firmware, %function
zzdf_return_to_firmware:
    cpsid   if                            /* no exceptions while VBAR */
    dsb                                   /* and the MMU change hands */
    isb
    ldr     r1, =zzdf_saved
    ldr     r0, =0x04B00000               /* SHARED (Normal NC)       */
    ldr     r2, =0xC0DE0010
    str     r2, [r0, #(67*4)]             /* SH_RETURN_STATE: in asm  */
    dsb

    /* 1. D-cache off in OUR SCTLR, then clean+invalidate every L1 D
          line by set/way (4 ways x 256 sets x 32 B = 32 KiB). With
          C=0 nothing can re-dirty a line while we walk the cache. */
    mrc     p15, 0, r2, c1, c0, 0
    bic     r2, r2, #(1 << 2)
    mcr     p15, 0, r2, c1, c0, 0
    dsb
    isb
    mov     r4, #0                        /* way << 30                */
4:  mov     r5, #0                        /* set << 5                 */
5:  orr     r6, r5, r4
    mcr     p15, 0, r6, c7, c14, 2        /* DCCISW                   */
    add     r5, r5, #32
    cmp     r5, #(256 * 32)
    blo     5b
    add     r4, r4, #(1 << 30)
    cmp     r4, #0                        /* 4 ways: wraps to 0       */
    bne     4b
    dsb

    /* 2. L1 I-cache and branch predictor, local */
    mov     r2, #0
    mcr     p15, 0, r2, c7, c5, 0         /* ICIALLU                  */
    mcr     p15, 0, r2, c7, c5, 6         /* BPIALL                   */
    dsb
    isb
    ldr     r2, =0xC0DE0020
    str     r2, [r0, #(67*4)]             /* L1 clean+inv done        */
    dsb

    /* 3. VFP state back, then coprocessor access, ACTLR, VBAR.
          zzdf_saved reads are now non-cacheable (C=0) and hit memory
          which the set/way clean above brought up to date. */
    ldr     r2, [r1, #48]
    vmsr    fpscr, r2
    add     r2, r1, #96
    vldmia  r2, {d8-d15}
    ldr     r2, [r1, #36]
    vmsr    fpexc, r2                     /* FPEXC (original)         */
    isb
    ldr     r2, [r1, #28]
    mcr     p15, 0, r2, c1, c0, 2         /* CPACR                    */
    isb
    ldr     r2, [r1, #32]
    mcr     p15, 0, r2, c1, c0, 1         /* ACTLR                    */
    isb
    ldr     r2, [r1, #24]
    mcr     p15, 0, r2, c12, c0, 0        /* VBAR                     */
    isb

    /* 4. everything the final switch needs, in registers */
    ldr     r4,  [r1, #0]                 /* SP                       */
    ldr     r5,  [r1, #4]                 /* LR                       */
    ldr     r6,  [r1, #8]                 /* SCTLR                    */
    ldr     r7,  [r1, #12]                /* TTBR0                    */
    ldr     r8,  [r1, #16]                /* TTBCR                    */
    ldr     r9,  [r1, #20]                /* DACR                     */
    ldr     r10, [r1, #40]                /* TTBR1                    */
    ldr     r11, [r1, #44]                /* CPSR                     */

    /* 5. EXIT marker: the LAST access to SHARED, still under our map */
    ldr     r12, =0xC0DEBAD1
    str     r12, [r0, #(253*4)]           /* SH_EXIT_MARK             */
    ldr     r2, =0xC0DE0030
    str     r2, [r0, #(67*4)]             /* SH_RETURN_STATE: switching */
    dsb

    /* 6. our MMU off (flat identity), I-cache off; C is already off */
    mrc     p15, 0, r2, c1, c0, 0
    bic     r2, r2, #(1 << 0)
    bic     r2, r2, #(1 << 12)
    mcr     p15, 0, r2, c1, c0, 0
    dsb
    isb
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0         /* TLBIALL                  */
    mcr     p15, 0, r2, c7, c5, 6         /* BPIALL                   */
    mcr     p15, 0, r2, c7, c5, 0         /* ICIALLU                  */
    dsb
    isb

    /* 7. firmware translation regime */
    mcr     p15, 0, r9,  c3, c0, 0        /* DACR                     */
    mcr     p15, 0, r8,  c2, c0, 2        /* TTBCR                    */
    mcr     p15, 0, r10, c2, c0, 1        /* TTBR1                    */
    mcr     p15, 0, r7,  c2, c0, 0        /* TTBR0                    */
    isb
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0         /* TLBIALL                  */
    mcr     p15, 0, r2, c7, c5, 6         /* BPIALL                   */
    mcr     p15, 0, r2, c7, c5, 0         /* ICIALLU                  */
    dsb
    isb

    /* 8. firmware SCTLR: MMU, caches, everything as we found it */
    mcr     p15, 0, r6, c1, c0, 0
    dsb
    isb

    /* 9. incoming SP/LR, CPSR mode+I/F, and back into the trampoline.
          No memory access from here on. */
    mov     sp, r4
    mov     lr, r5
    msr     cpsr_c, r11
    bx      lr

    .size zzdf_return_to_firmware, . - zzdf_return_to_firmware

    .section .data
    .align 5
    .global zzdf_saved
zzdf_saved:
    .space 192
    .end

#else  /* -------- ARCHIVED legacy WFE park (ZZDF_LEGACY_WFE_PARK) --- */

    .section .text.zzdf_entry, "ax"
    .global  zzdf_entry
    .type    zzdf_entry, %function

zzdf_entry:

    ldr     r1, =zzdf_saved
    str     sp, [r1, #0]                  /* SP firmware              */
    str     lr, [r1, #4]                  /* Core1 execution path. */
    mrc     p15, 0, r2, c1, c0, 0
    str     r2, [r1, #8]                  /* SCTLR                    */
    mrc     p15, 0, r2, c2, c0, 0
    str     r2, [r1, #12]                 /* TTBR0                    */
    mrc     p15, 0, r2, c2, c0, 2
    str     r2, [r1, #16]                 /* TTBCR                    */
    mrc     p15, 0, r2, c3, c0, 0
    str     r2, [r1, #20]                 /* DACR                     */
    mrc     p15, 0, r2, c12, c0, 0
    str     r2, [r1, #24]                 /* VBAR                     */
    mrc     p15, 0, r2, c1, c0, 2
    str     r2, [r1, #28]                 /* CPACR                    */
    mrc     p15, 0, r2, c1, c0, 1
    str     r2, [r1, #32]                 /* ACTLR                    */

    /* VFP must be enabled before compiled C code executes. */
    mrc     p15, 0, r12, c1, c0, 2
    orr     r12, r12, #(0xF << 20)
    mcr     p15, 0, r12, c1, c0, 2
    isb
    mov     r12, #0x40000000
    vmsr    fpexc, r12
    isb

    b       core1_entry                    

    .global zzdf_return_to_firmware
    .type   zzdf_return_to_firmware, %function
zzdf_return_to_firmware:
    ldr     r1, =zzdf_saved
    dsb

    ldr     r2, [r1, #24]
    mcr     p15, 0, r2, c12, c0, 0
    isb

    /* Cache/MMU handling follows the validated Core1 memory contract. */

    /* Production uses single-shot resident mode. The restore path is
       compiled but bypassed; SH_ENABLE_RESTORE is ignored. */
    b       .Lno_return_ever
    ldr     r3, =0x04B00000
    ldr     r0, [r3, #(271*4)]            /* SH_ENABLE_RESTORE        */
    cmp     r0, #0
    beq     .Lskip_restore

    ldr     r2, [r1, #20]
    mcr     p15, 0, r2, c3, c0, 0         /* DACR                     */
    ldr     r2, [r1, #16]
    mcr     p15, 0, r2, c2, c0, 2         /* TTBCR                    */
    ldr     r2, [r1, #12]
    mcr     p15, 0, r2, c2, c0, 0         /* TTBR0                    */
    ldr     r2, [r1, #28]
    mcr     p15, 0, r2, c1, c0, 2         /* CPACR                    */
    isb
    ldr     r2, [r1, #8]
    mcr     p15, 0, r2, c1, c0, 0         /* SCTLR                    */
    dsb
    isb

.Lskip_restore:
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0         /* TLBIALL                  */
    mcr     p15, 0, r2, c7, c5, 0         /* ICIALLU                  */
    mcr     p15, 0, r2, c7, c5, 6         /* BPIALL                   */
    dsb
    isb


    ldr     r3, =0x04B00000
    ldr     r2, =0xC0DEBAD1
    str     r2, [r3, #(253*4)]
    dsb

    ldr     sp, [r1, #0]
    ldr     lr, [r1, #4]
    bx      lr

.Lno_return_ever:
    dsb
1:  wfe
    b       1b

    .size zzdf_return_to_firmware, . - zzdf_return_to_firmware

    .section .data
    .align 3
    .global zzdf_saved
zzdf_saved:
    .space 40
    .end

#endif /* ZZDF_LEGACY_WFE_PARK */
