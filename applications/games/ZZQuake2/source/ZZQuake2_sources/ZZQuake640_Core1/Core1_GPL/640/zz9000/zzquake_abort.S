/* SPDX-License-Identifier: GPL-2.0-or-later */

    .equ SHARED,      0x04B00000
    .equ OFF_STATUS,  (1*4)
    .equ OFF_ERROR,   (6*4)
    .equ OFF_MARK,    (92*4)
    .equ OFF_FAR,     (93*4)
    .equ OFF_FSR,     (94*4)
    .equ OFF_PC,      (95*4)
    .equ OFF_SPSR,    (96*4)

    .equ OFF_LR_USR,  (559*4)
    .equ OFF_SP_USR,  (560*4)
    .equ OFF_R0,      (561*4)
    .equ OFF_R1,      (562*4)
    .equ OFF_R2,      (563*4)
    .equ OFF_R3,      (564*4)
    .equ OFF_R12,     (565*4)
    .equ OFF_STK0,    (566*4)
    .equ OFF_STK1,    (567*4)
    .equ OFF_STK2,    (568*4)
    .equ OFF_STK3,    (569*4)

    .equ ST_STOPPED,       0xFF
    .equ MARK_DABT,        0xBADD0DAB
    .equ MARK_PABT,        0xBADB00AF
    .equ MARK_UNDF,        0xBADB00DD
    .equ FATAL_UNDF,       0x86
    .equ FATAL_PABT,       0x87
    .equ FATAL_DABT,       0x88

    .section .text.vectors, "ax"
    .align 5
    .global zz_vector_table
zz_vector_table:
    ldr pc, =zz_reset_h
    ldr pc, =zz_undef_h
    ldr pc, =zz_svc_h
    ldr pc, =zz_pabt_h
    ldr pc, =zz_dabt_h
    nop
    ldr pc, =zz_irq_h
    ldr pc, =zz_fiq_h

zz_dabt_h:
    ldr     r0, =SHARED
    ldr     r1, =MARK_DABT
    str     r1, [r0, #OFF_MARK]
    mrc     p15, 0, r1, c6, c0, 0
    str     r1, [r0, #OFF_FAR]
    mrc     p15, 0, r1, c5, c0, 0
    str     r1, [r0, #OFF_FSR]
    sub     r1, lr, #8
    str     r1, [r0, #OFF_PC]
    mrs     r1, spsr
    str     r1, [r0, #OFF_SPSR]
    mov     r1, #FATAL_DABT
    str     r1, [r0, #OFF_ERROR]
    mov     r1, #ST_STOPPED
    str     r1, [r0, #OFF_STATUS]
    clrex
    dsb     sy

1:  wfe
    b       1b

zz_pabt_h:

    str     r0, [sp, #-4]!
    ldr     r0, =SHARED
    ldr     r8, [sp]
    str     r8, [r0, #OFF_R0]
    ldr     r8, [sp], #4
    ldr     r1, =MARK_PABT
    str     r1, [r0, #OFF_MARK]
    mrc     p15, 0, r1, c6, c0, 2
    str     r1, [r0, #OFF_FAR]
    mrc     p15, 0, r1, c5, c0, 1
    str     r1, [r0, #OFF_FSR]
    sub     r1, lr, #4
    str     r1, [r0, #OFF_PC]
    mrs     r1, spsr
    str     r1, [r0, #OFF_SPSR]
    mov     r1, #FATAL_PABT
    str     r1, [r0, #OFF_ERROR]

    str     r1, [r0, #OFF_R1]
    str     r2, [r0, #OFF_R2]
    str     r3, [r0, #OFF_R3]
    str     r12, [r0, #OFF_R12]

    mrs     r2, cpsr
    orr     r3, r2, #0x1F
    msr     cpsr_c, r3
    mov     r1, lr
    mov     r3, sp
    msr     cpsr_c, r2
    str     r1, [r0, #OFF_LR_USR]
    str     r3, [r0, #OFF_SP_USR]

    ldr     r1, [r3, #0]
    str     r1, [r0, #OFF_STK0]
    ldr     r1, [r3, #4]
    str     r1, [r0, #OFF_STK1]
    ldr     r1, [r3, #8]
    str     r1, [r0, #OFF_STK2]
    ldr     r1, [r3, #12]
    str     r1, [r0, #OFF_STK3]

    mov     r1, #ST_STOPPED
    str     r1, [r0, #OFF_STATUS]
    clrex
    dsb     sy
2:  wfe
    b       2b

zz_undef_h:
    ldr     r0, =SHARED
    ldr     r1, =MARK_UNDF
    str     r1, [r0, #OFF_MARK]
    sub     r1, lr, #4
    str     r1, [r0, #OFF_PC]
    mrs     r1, spsr
    str     r1, [r0, #OFF_SPSR]
    mov     r1, #FATAL_UNDF
    str     r1, [r0, #OFF_ERROR]
    mov     r1, #ST_STOPPED
    str     r1, [r0, #OFF_STATUS]
    clrex
    dsb     sy
3:  wfe
    b       3b

zz_reset_h:
zz_svc_h:
zz_irq_h:
zz_fiq_h:
    clrex
    dsb     sy
4:  wfe
    b       4b

    .global zz_install_vectors
    .type zz_install_vectors, %function
zz_install_vectors:
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 13)
    mcr     p15, 0, r0, c1, c0, 0
    isb
    ldr     r0, =zz_vector_table
    mcr     p15, 0, r0, c12, c0, 0
    isb
    bx      lr
    .size zz_install_vectors, . - zz_install_vectors
    .end
