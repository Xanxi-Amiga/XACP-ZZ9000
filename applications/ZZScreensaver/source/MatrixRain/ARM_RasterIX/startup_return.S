/* Cooperative Core1 payload entry/return. Saves firmware context and bx lr.
 *
 * D2c: the payload installs its own exception vectors for the duration of the
 * run (VBAR, SCTLR.V cleared, banked stacks for ABT/UND/IRQ/FIQ) and restores
 * the firmware's at exit.  An undefined instruction, prefetch abort or data
 * abort records PC/FAR/FSR/SPSR/type in exc_info, then takes the normal return
 * path on a fresh stack through zzgear_exception() (status FATAL, error
 * RIXE_EXCEPTION + type): the launcher prints where Core1 died instead of
 * Core1 falling into the firmware's handlers (D2b: "reset loop", empty log).
 * IRQ/FIQ are not expected (Core1 runs with them masked; the private timer
 * wakes WFI without a taken interrupt): if one is taken it is counted, and the
 * interrupted context resumes with that interrupt masked.
 */
.syntax unified
.arm
.section .data
.align 2
fw_sp:     .word 0
fw_vbar:   .word 0
fw_cpsr:   .word 0
fw_sp_abt: .word 0
fw_sp_und: .word 0
fw_sp_irq: .word 0
fw_sp_fiq: .word 0

.section .bss
.align 3
exc_stacks: .space 1024          /* 256 bytes per exception mode: ABT, UND, IRQ, FIQ */

/* ---- vector table: 32-byte aligned, ARM state ---- */
.section .text.vectors,"ax"
.align 5
.global zz_vectors
zz_vectors:
    ldr pc, =vec_reset
    ldr pc, =vec_undef
    ldr pc, =vec_svc
    ldr pc, =vec_pabt
    ldr pc, =vec_dabt
    ldr pc, =vec_reserved
    ldr pc, =vec_irq
    ldr pc, =vec_fiq
.ltorg

vec_reset:      mov r0, lr;         mov r1, #0; mov r2, #0;        mov r12, #0; b exc_common
vec_undef:      sub r0, lr, #4;     mov r1, #0; mov r2, #0;        mov r12, #1; b exc_common
vec_svc:        sub r0, lr, #4;     mov r1, #0; mov r2, #0;        mov r12, #2; b exc_common
vec_pabt:       sub r0, lr, #4;     mrc p15,0,r1,c6,c0,2;  mrc p15,0,r2,c5,c0,1;  mov r12, #3; b exc_common   /* IFAR, IFSR */
vec_dabt:       sub r0, lr, #8;     mrc p15,0,r1,c6,c0,0;  mrc p15,0,r2,c5,c0,0;  mov r12, #4; b exc_common   /* DFAR, DFSR */
vec_reserved:   mov r0, lr;         mov r1, #0; mov r2, #0;        mov r12, #5; b exc_common
vec_irq:
    sub lr, lr, #4
    push {r0, r1}
    ldr r0, =exc_info
    ldr r1, [r0, #24]
    add r1, r1, #1
    str r1, [r0, #24]
    mrs r0, spsr
    orr r0, r0, #0x80                /* resume with IRQ masked */
    msr spsr_cxsf, r0
    pop {r0, r1}
    movs pc, lr
vec_fiq:
    sub lr, lr, #4
    push {r0, r1}
    ldr r0, =exc_info
    ldr r1, [r0, #28]
    add r1, r1, #1
    str r1, [r0, #28]
    mrs r0, spsr
    orr r0, r0, #0x40                /* resume with FIQ masked */
    msr spsr_cxsf, r0
    pop {r0, r1}
    movs pc, lr

/* r0 = faulting pc, r1 = FAR, r2 = FSR, r12 = type; in ABT/UND/SVC mode, interrupts masked */
exc_common:
    mrs r3, spsr
    ldr lr, =exc_info
    stmia lr, {r0-r3, r12}           /* [0] pc [1] far [2] fsr [3] spsr [4] type */
    ldr r0, =fw_cpsr                 /* back to the mode the payload runs in, IRQ/FIQ masked, ARM state */
    ldr r0, [r0]
    orr r0, r0, #0xC0
    bic r0, r0, #0x20
    msr cpsr_c, r0
    ldr sp, =_stack_top              /* fresh stack: the faulting context is abandoned */
    bl zzgear_exception              /* SD_EXC_*, frame diagnostic, stats, status FATAL, caches cleaned */
    b core1_return
.ltorg

.section .text.core1_entry,"ax"
.global core1_entry
.type core1_entry,%function
.extern zzgear_core1_main
.extern zzgear_exception
.extern zzheap_reset
.extern exc_info
core1_entry:
    push {r0-r12,lr}
    mrc p15,0,r4,c1,c0,0      /* SCTLR */
    mrc p15,0,r5,c2,c0,0      /* TTBR0 */
    mrc p15,0,r6,c2,c0,2      /* TTBCR */
    mrc p15,0,r7,c3,c0,0      /* DACR */
    mrc p15,0,r8,c1,c0,2      /* CPACR */
    vmrs r9,FPEXC
    mrc p15,0,r10,c1,c0,1     /* ACTLR */
    push {r4-r10}
    ldr r11,=fw_sp
    str sp,[r11]
    ldr sp,=_stack_top

    /* ---- exception vectors (D2c): save the firmware's VBAR, mode and banked stacks ---- */
    mrs r1, cpsr
    ldr r0, =fw_cpsr
    str r1, [r0]
    mrc p15,0,r0,c12,c0,0     /* VBAR */
    ldr r2, =fw_vbar
    str r0, [r2]
    cps #0x17                 /* ABT */
    ldr r0, =fw_sp_abt
    str sp, [r0]
    ldr sp, =exc_stacks+256
    cps #0x1B                 /* UND */
    ldr r0, =fw_sp_und
    str sp, [r0]
    ldr sp, =exc_stacks+512
    cps #0x12                 /* IRQ */
    ldr r0, =fw_sp_irq
    str sp, [r0]
    ldr sp, =exc_stacks+768
    cps #0x11                 /* FIQ (banks r8-r14: only r0-r7 are used here) */
    ldr r0, =fw_sp_fiq
    str sp, [r0]
    ldr sp, =exc_stacks+1024
    msr cpsr_c, r1            /* back to the firmware's mode and interrupt masks */
    ldr r0, =zz_vectors
    mcr p15,0,r0,c12,c0,0     /* VBAR = ours */
    mrc p15,0,r0,c1,c0,0
    bic r0, r0, #(1<<13)      /* SCTLR.V = 0: low vectors, VBAR used (restored from r4 at exit) */
    mcr p15,0,r0,c1,c0,0
    isb
    ldr r0, =exc_info         /* no record from a previous run */
    mov r1, #0
    mov r2, #8
1:  str r1, [r0], #4
    subs r2, r2, #1
    bne 1b

    mrc p15,0,r0,c1,c0,2
    orr r0,r0,#(0xF<<20)
    mcr p15,0,r0,c1,c0,2
    isb
    vmrs r0,FPEXC
    orr r0,r0,#(1<<30)
    vmsr FPEXC,r0
    isb

    ldr r0,=_bss_start
    ldr r1,=_bss_end
    mov r2,#0
1:  cmp r0,r1
    bhs 2f
    str r2,[r0],#4
    b 1b
2:
    bl zzheap_reset
    mov r0,#0
    bl zzgear_core1_main

.global core1_return
core1_return:
    /* Proof that C teardown completed and assembly return path started. */
    ldr r0,=0x0470001C       /* RIX_SHARED_ARM + SH_EXIT_MARK*4 (rix_shared.h: slot 7); checked by tools/verify.py */
    ldr r1,=0xC0DEBAD1
    str r1,[r0]
    dsb

    ldr r11,=fw_sp
    ldr sp,[r11]
    pop {r4-r10}
    /* ---- exception vectors: the firmware's VBAR and banked stacks back ---- */
    mrs r1, cpsr
    ldr r0, =fw_vbar
    ldr r0, [r0]
    mcr p15,0,r0,c12,c0,0
    cps #0x17
    ldr r0, =fw_sp_abt
    ldr sp, [r0]
    cps #0x1B
    ldr r0, =fw_sp_und
    ldr sp, [r0]
    cps #0x12
    ldr r0, =fw_sp_irq
    ldr sp, [r0]
    cps #0x11
    ldr r0, =fw_sp_fiq
    ldr sp, [r0]
    ldr r1, =fw_cpsr          /* the firmware's mode and interrupt masks exactly as at entry (the exception path masked them) */
    ldr r1, [r1]
    msr cpsr_c, r1
    isb
    vmsr FPEXC,r9
    mcr p15,0,r8,c1,c0,2
    mcr p15,0,r10,c1,c0,1
    isb
    mov r0,#0
    mcr p15,0,r0,c7,c5,0
    mcr p15,0,r0,c7,c5,6
    mcr p15,0,r0,c8,c7,0
    dsb
    isb
    mcr p15,0,r7,c3,c0,0
    mcr p15,0,r6,c2,c0,2
    mcr p15,0,r5,c2,c0,0
    dsb
    isb
    mcr p15,0,r4,c1,c0,0      /* SCTLR incl. the firmware's V bit */
    dsb
    isb
    pop {r0-r12,lr}
    bx lr
3:  wfe
    b 3b
.size core1_entry,.-core1_entry
