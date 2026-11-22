.text
.even
.globl _cli_first
_cli_first:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.l %d0,-(%sp)
 move.l %a0,-(%sp)
 jsr _cli
 addq.l #8,%sp
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
