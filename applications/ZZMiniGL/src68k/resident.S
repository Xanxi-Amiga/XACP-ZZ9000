.text
.even
.globl _lib_first
_lib_first:
 moveq #-1,%d0
 rts
.even
tag:
 .word 0x4afc
 .long tag,endtag
 .byte 128,29,9,0
 .long libname,identity,auto_init
endtag:
libname: .asciz "minigl.library"
identity: .asciz "ZZMiniGL XACP A8.29 29.1 (22.11.2026)"
 .asciz "$VER: minigl.library 29.1 (22.11.2026) ZZMiniGL XACP A8.29"
.even
auto_init:
 .long 38,vectors,0,init
vectors:
 .long libopen,libclose,expunge,reserved,table,arm,-1
init:
 move.l %a6,_Sys
 move.l %d0,%a1
 move.l %a0,34(%a1)
 move.b #9,8(%a1)
 move.l #libname,10(%a1)
 move.b #6,14(%a1)
 move.w #29,20(%a1)
 move.w #1,22(%a1)
 move.l #identity,24(%a1)
 rts
libopen:
 move.l %a6,-(%sp)
 jsr _zz_libopen
 addq.l #4,%sp
 tst.l %d0
 beq.s reserved
 addq.w #1,32(%a6)
 bclr #3,14(%a6)
 move.l %a6,%d0
 rts
libclose:
 tst.w 32(%a6)
 beq.s reserved
 subq.w #1,32(%a6)
 bne.s reserved
 jsr _zz_libclosed
 btst #3,14(%a6)
 bne.s expunge
reserved:
 moveq #0,%d0
 rts
expunge:
 tst.w 32(%a6)
 bne.s postpone
 tst.l _zz_pinned
 bne.s postpone
 movem.l %d2/%a5/%a6,-(%sp)
 move.l %a6,%a5
 move.l 34(%a5),%d2
 move.l _Sys,%a6
 move.l %a5,%a1
 jsr -252(%a6)
 moveq #0,%d0
 move.w 16(%a5),%d0
 move.l %a5,%a1
 suba.l %d0,%a1
 moveq #0,%d1
 move.w 18(%a5),%d1
 add.l %d1,%d0
 jsr -210(%a6)
 move.l %d2,%d0
 movem.l (%sp)+,%d2/%a5/%a6
 rts
postpone:
 bset #3,14(%a6)
 moveq #0,%d0
 rts
/* A8.2: private LVO -36, called by ZZMiniGLLoad only: authorises the next MGLCreateContext once */
arm:
 move.l %d1,-(%sp)               /* A8.7: d1 = diagnostic switches from ZZMiniGLLoad */
 jsr _zz_arm
 addq.l #4,%sp
 rts
table:
 move.l #_zz_dispatch,%d0
 move.l %d0,%a0
 rts
