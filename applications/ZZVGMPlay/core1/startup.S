/*
 * startup.S - ZZVGMPlay Core1 startup / clean hand-back.
 * Derived from the validated ZZReSID startup model.
 */
    .syntax unified
    .arm

    .section .data
    .align 2
fw_sp:
    .word 0

    .section .text.startup, "ax"
    .global _start
    .global zzvgm_core1_main

_start:
    push    {r0-r12, lr}

    mrc     p15, 0, r4, c1, c0, 0
    mrc     p15, 0, r5, c2, c0, 0
    mrc     p15, 0, r6, c2, c0, 2
    mrc     p15, 0, r7, c3, c0, 0
    mrc     p15, 0, r8, c1, c0, 2
    vmrs    r9, FPEXC
    push    {r4-r9}

    ldr     r10, =fw_sp
    str     sp, [r10]

    ldr     sp, =_stack_top

    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    isb

    vmrs    r0, FPEXC
    orr     r0, r0, #(1 << 30)
    vmsr    FPEXC, r0
    dsb

    mrc     p15, 0, r0, c1, c0, 0
    orr     r0, r0, #(1 << 12)
    orr     r0, r0, #(1 << 11)
    bic     r0, r0, #(1 << 2)
    mcr     p15, 0, r0, c1, c0, 0
    isb

    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    bcs     2f
    str     r2, [r0], #4
    b       1b
2:
    bl      zzvgm_core1_main

    ldr     r10, =fw_sp
    ldr     sp, [r10]
    pop     {r4-r9}

    vmsr    FPEXC, r9
    mcr     p15, 0, r8, c1, c0, 2
    isb

    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0
    mcr     p15, 0, r0, c7, c5, 6
    mcr     p15, 0, r0, c8, c7, 0
    dsb
    isb

    mcr     p15, 0, r7, c3, c0, 0
    mcr     p15, 0, r6, c2, c0, 2
    mcr     p15, 0, r5, c2, c0, 0
    dsb
    isb
    mcr     p15, 0, r4, c1, c0, 0
    dsb
    isb

    pop     {r0-r12, lr}
    bx      lr
