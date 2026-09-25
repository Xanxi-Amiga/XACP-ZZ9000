/* Shared-memory protocol state. */




































    .equ SHARED,      0x04B00000
    .equ OFF_STATUS,  (1*4)
    .equ OFF_ERROR,   (6*4)
     



    .equ FAULT,       0x600
    .equ OFF_MARK,    (FAULT + 0x00)
    .equ OFF_FAR,     (FAULT + 0x04)
    .equ OFF_FSR,     (FAULT + 0x08)
    .equ OFF_PC,      (FAULT + 0x0C)
    .equ OFF_SPSR,    (FAULT + 0x10)
    .equ OFF_LR_USR,  (FAULT + 0x14)
    .equ OFF_SP_USR,  (FAULT + 0x18)
    .equ OFF_R0,      (FAULT + 0x1C)
    .equ OFF_R1,      (FAULT + 0x20)
    .equ OFF_R2,      (FAULT + 0x24)
    .equ OFF_R3,      (FAULT + 0x28)
    .equ OFF_R12,     (FAULT + 0x2C)
    .equ OFF_STK0,    (FAULT + 0x30)
    .equ OFF_STK1,    (FAULT + 0x34)
    .equ OFF_STK2,    (FAULT + 0x38)
    .equ OFF_STK3,    (FAULT + 0x3C)

    .equ ST_STOPPED,       0xFF
    .equ MARK_DABT,        0xBADD0DAB
    .equ MARK_PABT,        0xBADB00AF
    .equ MARK_UNDF,        0xBADB00DD
    .equ FATAL_UNDF,       0x86
    .equ FATAL_PABT,       0x87
    .equ FATAL_DABT,       0x88

 

























    .section .bss.zzdf_fault, "aw", %nobits
    .align 3
zz_fault_stack:
    .space 1024
zz_fault_stack_top:
zz_fault_once:
    .space 4

    .section .text.vectors, "ax"
    .align 5                      /* vector table must be 32-byte aligned */
    .global zz_vector_table
zz_vector_table:
    ldr pc, =zz_reset_h           /* 0x00 reset    */
    ldr pc, =zz_undef_h           /* 0x04 undef    */
    ldr pc, =zz_svc_h             /* 0x08 svc      */
    ldr pc, =zz_pabt_h            /* 0x0C prefetch */
    ldr pc, =zz_dabt_h            /* 0x10 data     */
    nop                           /* 0x14 reserved */
    ldr pc, =zz_irq_h             /* 0x18 irq      */
    ldr pc, =zz_fiq_h             /* 0x1C fiq      */

/* ---- Data abort ---- */
zz_dabt_h:
    ldr     r0, =SHARED
    ldr     r1, =MARK_DABT
    str     r1, [r0, #OFF_MARK]
    mrc     p15, 0, r1, c6, c0, 0     /* DFAR */
    str     r1, [r0, #OFF_FAR]
    mrc     p15, 0, r1, c5, c0, 0     /* DFSR */
    str     r1, [r0, #OFF_FSR]
    sub     r1, lr, #8                /* faulting PC */
    str     r1, [r0, #OFF_PC]
    mrs     r1, spsr
    str     r1, [r0, #OFF_SPSR]
    mov     r1, #FATAL_DABT
    str     r1, [r0, #OFF_ERROR]
    mov     r1, #ST_STOPPED
    str     r1, [r0, #OFF_STATUS]     /* >= 0x80: launcher sees fault */
    clrex                     /* drop any exclusive monitor state */
    dsb     sy                /* ARM: pending transactions are not
                                 guaranteed complete on entry to WFE.
                                 Drain the CPU before parking. */
    bl      zz_try_return              
1:  wfe
    b       1b

 



zz_try_return:
    ldr     r0, =zz_fault_once
    ldr     r1, [r0]
    cmp     r1, #0
    bxne    lr                        /* second time: park, no retry */
    mov     r1, #1
    str     r1, [r0]
    dsb     sy

    cpsid   if                        /* no interrupts during teardown */
    msr     cpsr_c, #0xD3             /* SVC, I+F masked */
    ldr     sp, =zz_fault_stack_top
    bl      zzdf_return_teardown      /* never returns if it accepts */

    /* If teardown is refused, leave the fault state published and park. */
    clrex
    dsb     sy
9:  wfe
    b       9b

/* ---- Prefetch abort ---- */
zz_pabt_h:
    




    str     r0, [sp, #-4]!
    ldr     r0, =SHARED
    ldr     r8, [sp]
    str     r8, [r0, #OFF_R0]
    ldr     r8, [sp], #4
    ldr     r1, =MARK_PABT
    str     r1, [r0, #OFF_MARK]
    mrc     p15, 0, r1, c6, c0, 2     /* IFAR */
    str     r1, [r0, #OFF_FAR]
    mrc     p15, 0, r1, c5, c0, 1     /* IFSR */
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
    bl      zz_try_return              
2:  wfe
    b       2b

/* ---- Undefined instruction ---- */
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
    bl      zz_try_return              
3:  wfe
    b       3b

/* ---- Unused vectors: park (should never fire in a bare blob) ---- */
zz_reset_h:
zz_svc_h:
zz_irq_h:
zz_fiq_h:
    clrex
    dsb     sy
4:  wfe
    b       4b

/* ---- Installer: SCTLR.V=0 then VBAR = table ---- */
    .global zz_install_vectors
    .type zz_install_vectors, %function
zz_install_vectors:
    mrc     p15, 0, r0, c1, c0, 0     /* SCTLR */
    bic     r0, r0, #(1 << 13)        /* V = 0: use VBAR, not 0xFFFF0000 */
    mcr     p15, 0, r0, c1, c0, 0
    isb
    ldr     r0, =zz_vector_table
    mcr     p15, 0, r0, c12, c0, 0    /* VBAR */
    isb
    bx      lr
    .size zz_install_vectors, . - zz_install_vectors
    .end
