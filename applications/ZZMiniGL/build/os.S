.text
.even
.globl _os_sys
_os_sys:
 move.l 4.w,%d0
 rts
.globl _get_table
_get_table:
 move.l %a6,-(%sp)
 move.l 8(%sp),%a6
 jsr -30(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_openlib
_os_openlib:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 move.l 12(%sp),%d0
 jsr -552(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_closelib
_os_closelib:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -414(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_task
_os_task:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -294(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_find
_os_find:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%a1
 jsr -276(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_forbid
_os_forbid:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 jsr -132(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_permit
_os_permit:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 jsr -138(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_cache
_os_cache:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%d0
 move.l 16(%sp),%d1
 jsr -642(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_alloc
_os_alloc:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%d0
 move.l 12(%sp),%d1
 jsr -198(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_free
_os_free:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 move.l 12(%sp),%d0
 jsr -210(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_port
_os_port:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 jsr -666(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_delport
_os_delport:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 jsr -672(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_creq
_os_creq:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%d0
 jsr -654(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_dreq
_os_dreq:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 jsr -660(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_opendev
_os_opendev:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%d0
 move.l 16(%sp),%a1
 move.l 20(%sp),%d1
 jsr -444(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_closedev
_os_closedev:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -450(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_findport
_os_findport:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -390(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_addport
_os_addport:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -354(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_remport
_os_remport:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -360(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_setintvec
_os_setintvec:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%d0
 move.l 12(%sp),%a1
 jsr -162(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_getmsg
_os_getmsg:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 jsr -372(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_doio
_os_doio:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -456(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_replymsg
_os_replymsg:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a1
 jsr -378(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_waitport
_os_waitport:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%a0
 jsr -384(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_setsignal
_os_setsignal:
 move.l %a6,-(%sp)
 move.l _Sys,%a6
 move.l 8(%sp),%d0
 move.l 12(%sp),%d1
 jsr -306(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _int_openwindow
_int_openwindow:
 move.l %a6,-(%sp)
 move.l _Int,%a6
 move.l 8(%sp),%a0
 jsr -204(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _int_closewindow
_int_closewindow:
 move.l %a6,-(%sp)
 move.l _Int,%a6
 move.l 8(%sp),%a0
 jsr -72(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _int_setpointer
_int_setpointer:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l _Int,%a6
 move.l 16(%sp),%a0
 move.l 20(%sp),%a1
 move.l 24(%sp),%d0
 move.l 28(%sp),%d1
 move.l 32(%sp),%d2
 move.l 36(%sp),%d3
 jsr -270(%a6)
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _int_clearpointer
_int_clearpointer:
 move.l %a6,-(%sp)
 move.l _Int,%a6
 move.l 8(%sp),%a0
 jsr -60(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _int_modifyidcmp
_int_modifyidcmp:
 move.l %a6,-(%sp)
 move.l _Int,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%d0
 jsr -150(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _os_board
_os_board:
 move.l %a6,-(%sp)
 move.l _Exp,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%d0
 move.l 16(%sp),%d1
 jsr -72(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _clockread
_clockread:
 move.l %a6,-(%sp)
 move.l _Timer,%a6
 move.l 8(%sp),%a0
 jsr -60(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_output
_dos_output:
 move.l %a6,-(%sp)
 move.l _Dos,%a6
 jsr -60(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_write
_dos_write:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l _Dos,%a6
 move.l 16(%sp),%d1
 move.l 20(%sp),%d2
 move.l 24(%sp),%d3
 jsr -48(%a6)
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_delay
_dos_delay:
 move.l %a6,-(%sp)
 move.l _Dos,%a6
 move.l 8(%sp),%d1
 jsr -198(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_open
_dos_open:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l _Dos,%a6
 move.l 12(%sp),%d1
 move.l 16(%sp),%d2
 jsr -30(%a6)
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_close
_dos_close:
 move.l %a6,-(%sp)
 move.l _Dos,%a6
 move.l 8(%sp),%d1
 jsr -36(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_setvar
_dos_setvar:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l %d4,-(%sp)
 move.l _Dos,%a6
 move.l 20(%sp),%d1
 move.l 24(%sp),%d2
 move.l 28(%sp),%d3
 move.l 32(%sp),%d4
 jsr -900(%a6)
 move.l (%sp)+,%d4
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_getvar
_dos_getvar:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l %d4,-(%sp)
 move.l _Dos,%a6
 move.l 20(%sp),%d1
 move.l 24(%sp),%d2
 move.l 28(%sp),%d3
 move.l 32(%sp),%d4
 jsr -906(%a6)
 move.l (%sp)+,%d4
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_delvar
_dos_delvar:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l _Dos,%a6
 move.l 12(%sp),%d1
 move.l 16(%sp),%d2
 jsr -912(%a6)
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_selectoutput
_dos_selectoutput:
 move.l %a6,-(%sp)
 move.l _Dos,%a6
 move.l 8(%sp),%d1
 jsr -882(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _dos_seek
_dos_seek:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l _Dos,%a6
 move.l 16(%sp),%d1
 move.l 20(%sp),%d2
 move.l 24(%sp),%d3
 jsr -66(%a6)
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _p96_lock
_p96_lock:
 move.l %a6,-(%sp)
 move.l _P96,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%a1
 move.l 16(%sp),%d0
 jsr -48(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _p96_unlock
_p96_unlock:
 move.l %a6,-(%sp)
 move.l _P96,%a6
 move.l 8(%sp),%a0
 move.l 12(%sp),%d0
 jsr -54(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _p96_best
_p96_best:
 move.l %a6,-(%sp)
 move.l _P96,%a6
 move.l 8(%sp),%a0
 jsr -60(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _p96_open
_p96_open:
 move.l %a6,-(%sp)
 move.l _P96,%a6
 move.l 8(%sp),%a0
 jsr -90(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _p96_close
_p96_close:
 move.l %a6,-(%sp)
 move.l _P96,%a6
 move.l 8(%sp),%a0
 jsr -96(%a6)
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
.globl _p96_write
_p96_write:
 move.l %a6,-(%sp)
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l %d4,-(%sp)
 move.l %d5,-(%sp)
 move.l _P96,%a6
 move.l 24(%sp),%a0
 move.l 28(%sp),%d0
 move.l 32(%sp),%d1
 move.l 36(%sp),%a1
 move.l 40(%sp),%d2
 move.l 44(%sp),%d3
 move.l 48(%sp),%d4
 move.l 52(%sp),%d5
 jsr -102(%a6)
 move.l (%sp)+,%d5
 move.l (%sp)+,%d4
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.l (%sp)+,%a6
 move.l %d0,%a0
 rts
