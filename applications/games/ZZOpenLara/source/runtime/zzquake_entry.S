/* VFP must be enabled before compiled C code executes. */





















    .section .text.zzq_entry, "ax"
    .global  zzq_entry
    .type    zzq_entry, %function

zzq_entry:
    
    ldr     r1, =zzq_saved
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

    b       core1_entry                   /* r0 (env) preserve        */









    .global zzq_return_to_firmware
    .type   zzq_return_to_firmware, %function
zzq_return_to_firmware:
    ldr     r1, =zzq_saved
    dsb

    


    ldr     r2, [r1, #24]
    mcr     p15, 0, r2, c12, c0, 0
    isb

    /* Cache/MMU handling follows the validated Core1 memory contract. */






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

    .size zzq_return_to_firmware, . - zzq_return_to_firmware

    .section .data
    .align 3
    .global zzq_saved
zzq_saved:
    .space 40
    .end
