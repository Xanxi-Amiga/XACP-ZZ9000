| crt0.S - reset vectors and startup of the SYNTHETIC 68000 test program (tests only).
| This is not Capcom code: it exists so that the Core1 blob can be exercised without any
| commercial ROM. ASCII only.
    .section .vectors,"ax"
    .long 0x00FFFF00            | initial SSP (work RAM)
    .long _start                | initial PC
    .rept 24
    .long _trap
    .endr
    .long _irq2                 | vector 26: level 2 autovector = CPS-1 vblank
    .rept 229
    .long _trap
    .endr

    .text
    .globl _start
_start:
    move.w  #0x2700,%sr
    lea     0x00FFFF00,%sp
    lea     __bss_start,%a0
    lea     __bss_end,%a1
1:  cmp.l   %a1,%a0
    bcc.s   2f
    clr.w   (%a0)+
    bra.s   1b
2:  jsr     main
3:  bra.s   3b

_irq2:
    addq.w  #1,vbl
    rte

_trap:
    move.w  #0xDEAD,0x00FF8100
4:  bra.s   4b
