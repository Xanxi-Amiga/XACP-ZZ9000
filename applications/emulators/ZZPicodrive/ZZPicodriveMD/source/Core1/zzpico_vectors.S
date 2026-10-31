/*
 * zzpico_vectors.S - vecteurs d'exception ARM Cortex-A9 pour le blob Core1.
 *
 * Exception vectors: UNDEF / PREFETCH / DATA ->
 *   ecrit ERROR + PC fautif + FAR/FSR dans shared, STATUS=ERROR, puis WFE.
 *
 * IMPORTANT : handlers PUR ASM, AUCUN usage de la pile. Les SP banques des
 * modes ABORT/UNDEF ne sont PAS initialises par le firmware ; appeler du C
 * (qui empile) crasherait de nouveau. On n'ecrit que dans shared (NC, donc
 * visible immediatement par le 68k) via adresses cablees, puis WFE.
 *
 * shared = ARM 0x04700000 (NC). Slots (byte offset = index*4) :
 *   STATUS  idx2  = 0x08    ERROR  idx8  = 0x20
 *   PC      idx26 = 0x68    FAR    idx27 = 0x6C    FSR  idx28 = 0x70
 * Codes ERROR : DATA_ABORT=6, PREFETCH_ABORT=7, UNDEF_INSTR=8.
 * STATUS_ERROR = 0xEE.
 *
 * ASCII only.
 */
    .syntax unified
    .arm

    .equ SHARED,     0x04700000
    .equ OFF_STATUS, 0x08
    .equ OFF_ERROR,  0x20
    .equ OFF_PC,     0x68
    .equ OFF_FAR,    0x6C
    .equ OFF_FSR,    0x70
    .equ ST_ERROR,   0xEE

    .section .text.vectors,"ax",%progbits
    .align 5                      /* VBAR exige un alignement 32 octets */
    .global zz_vector_table
zz_vector_table:
    b   zz_reset                  /* 0x00 reset    */
    b   zz_undef                  /* 0x04 undef    */
    b   zz_svc                    /* 0x08 svc      */
    b   zz_pabt                   /* 0x0C prefetch */
    b   zz_dabt                   /* 0x10 data     */
    b   zz_resv                   /* 0x14 reserved */
    b   zz_irq                    /* 0x18 irq      */
    b   zz_fiq                    /* 0x1C fiq      */

/* --- UNDEF : LR = instr fautive + 4 --- */
zz_undef:
    sub     lr, lr, #4
    ldr     r0, =SHARED
    mov     r1, #8                /* ERROR_UNDEF_INSTR */
    str     r1, [r0, #OFF_ERROR]
    str     lr, [r0, #OFF_PC]
    mov     r1, #ST_ERROR
    str     r1, [r0, #OFF_STATUS]
    dsb
1:  wfe
    b       1b

/* --- PREFETCH ABORT : LR = instr fautive + 4 ; IFAR/IFSR --- */
zz_pabt:
    sub     lr, lr, #4
    ldr     r0, =SHARED
    mov     r1, #7                /* ERROR_PREFETCH_ABORT */
    str     r1, [r0, #OFF_ERROR]
    str     lr, [r0, #OFF_PC]
    mrc     p15, 0, r2, c6, c0, 2 /* IFAR */
    str     r2, [r0, #OFF_FAR]
    mrc     p15, 0, r2, c5, c0, 1 /* IFSR */
    str     r2, [r0, #OFF_FSR]
    mov     r1, #ST_ERROR
    str     r1, [r0, #OFF_STATUS]
    dsb
1:  wfe
    b       1b

/* --- DATA ABORT : LR = instr fautive + 8 ; DFAR/DFSR --- */
zz_dabt:
    sub     lr, lr, #8
    ldr     r0, =SHARED
    mov     r1, #6                /* ERROR_DATA_ABORT */
    str     r1, [r0, #OFF_ERROR]
    str     lr, [r0, #OFF_PC]
    mrc     p15, 0, r2, c6, c0, 0 /* DFAR */
    str     r2, [r0, #OFF_FAR]
    mrc     p15, 0, r2, c5, c0, 0 /* DFSR */
    str     r2, [r0, #OFF_FSR]
    mov     r1, #ST_ERROR
    str     r1, [r0, #OFF_STATUS]
    dsb
1:  wfe
    b       1b

/* --- vecteurs benins (ne devraient pas se produire ; IRQ/FIQ non actives) --- */
zz_reset:
zz_svc:
zz_resv:
zz_irq:
zz_fiq:
    ldr     r0, =SHARED
    mov     r1, #ST_ERROR
    str     r1, [r0, #OFF_STATUS]
    dsb
1:  wfe
    b       1b

/* --- void zz_install_vectors(void) : VBAR = table ; SCTLR.V = 0 --- */
    .section .text.zz_install_vectors,"ax",%progbits
    .global zz_install_vectors
    .type   zz_install_vectors,%function
zz_install_vectors:
    ldr     r0, =zz_vector_table
    mcr     p15, 0, r0, c12, c0, 0    /* VBAR = &zz_vector_table */
    mrc     p15, 0, r1, c1, c0, 0     /* SCTLR */
    bic     r1, r1, #(1 << 13)        /* V = 0 : vecteurs bas relocalisables (VBAR) */
    mcr     p15, 0, r1, c1, c0, 0
    isb
    bx      lr
    .size   zz_install_vectors, .-zz_install_vectors
