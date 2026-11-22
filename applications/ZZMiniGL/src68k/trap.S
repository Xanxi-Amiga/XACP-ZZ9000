/* trap.S - ZZMiniGL A8.5: tc_TrapCode of the task that owns an armed context (GLQuake).
 * Entered in supervisor mode by exec for a CPU exception of that task: (sp) = trap number (long), then
 * the exception frame (SR word, PC long, format/vector word; format 4 adds the effective address and the
 * 68060 fault status long word).  It copies them, the user stack pointer and the 8 long words at the user
 * stack pointer into the black box (blackbox.h, words 20..47), then continues with the previous trap code
 * unchanged (the usual guru follows).  Nothing is written when no black box is open. */
.text
.even
.globl _zz_trap_entry
_zz_trap_entry:
 movem.l %d0/%a0-%a1,-(%sp)
 move.l _zz_trap_bb,%d0
 beq.s 9f
 move.l %d0,%a0
 move.w 14(%sp),42(%a0)          /* trap number (low word of the long at 12(sp)) */
 move.w 16(%sp),44(%a0)          /* SR */
 move.w 18(%sp),46(%a0)          /* PC */
 move.w 20(%sp),48(%a0)
 move.w 22(%sp),50(%a0)          /* format / vector offset */
 move.w 24(%sp),52(%a0)          /* format 4: effective address */
 move.w 26(%sp),54(%a0)
 move.w 28(%sp),56(%a0)          /* format 4: fault status long word */
 move.w 30(%sp),58(%a0)
 move.l %usp,%a1
 move.l %a1,%d0
 swap %d0
 move.w %d0,60(%a0)
 swap %d0
 move.w %d0,62(%a0)
 move.w #0x5452,40(%a0)          /* "TR": the words above are complete */
 btst #0,%d0
 bne.s 9f                        /* odd user stack pointer: no stack copy */
 tst.l %d0
 beq.s 9f
 moveq #7,%d0
 lea 64(%a0),%a0
1: move.w (%a1)+,(%a0)+
 move.w (%a1)+,(%a0)+
 dbra %d0,1b
9:
 cpusha %dc                      /* the record reaches the board even if its space is cached copy-back */
 movem.l (%sp)+,%d0/%a0-%a1
 move.l _zz_trap_old,-(%sp)
 rts

/* A8.19 (ZZMiniGLLoad PAULAGUARD): an audio interrupt handler of the application, called with the whole
 * FPU state saved around it.  Entered by exec as the handler of the vector (SetIntVector): a1 = is_Data =
 * the guard's data: 0 the application's handler, 4 its is_Data, 8 the black box words of the guard (0 if
 * none: +2 entries, +4 returns, +6 the 68k frame count at the last entry, copied from the box's words 4,5).
 * d1, a0 and a6 reach the application's handler as exec gave them; d0, a1 and a5 are scratch for a handler. */
.globl _zz_aud_guard
_zz_aud_guard:
 fsave -(%sp)
 fmovem.l %fpcr/%fpsr/%fpiar,-(%sp)
 move.l %a2,-(%sp)
 move.l %a1,%a2
 move.l 8(%a2),%d0
 beq.s 1f
 move.l %d0,%a5
 addq.w #1,2(%a5)
 move.l -632(%a5),6(%a5)
1:
 move.l 4(%a2),%a1
 move.l (%a2),%a5
 jsr (%a5)
 move.l 8(%a2),%d0
 beq.s 2f
 move.l %d0,%a5
 addq.w #1,4(%a5)
2:
 move.l (%sp)+,%a2
 fmovem.l (%sp)+,%fpcr/%fpsr/%fpiar
 frestore (%sp)+
 rts
