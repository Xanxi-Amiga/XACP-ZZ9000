/* blackbox.h - A8.4 crash record of an armed context (GLQuake), in ZZ9000 memory just below the control
 * block (ARM 0x0464F000, inside the exclusive blob corridor, outside the image, its BSS and .zz_nc).  The
 * Amiga reset does not clear the board memory: ZZMiniGLLoad reads the record of the previous run before it
 * starts the next one and writes it to PROGDIR:zzminigl_blackbox.txt.  16-bit words, written by the 68k
 * only (68k byte order; Core1 never reads them).
 *   0,1  magic ZB B4         2  layout 1, bit 15 set once MGLDeleteContext has run (clean end)
 *   3    phase inside MGLSwitchDisplay / flush (0 = outside)
 *   4,5  68k frames          6,7  EClock milliseconds at the last frame
 *   8    last returned call: (sequence & 255) << 8 | slot
 *   10..17  at the last frame: Core1 frames, GL errors, texture refusals, library error (hi, lo each)
 *   18,19   A8.5: first capability unknown to Core1 (MGLSetState), 0 if none
 *   20      A8.5: 0x5452 once a CPU exception of the owning task was copied (trap.S); 21 trap number,
 *           22 SR, 23,24 PC, 25 format/vector, 26,27 effective address and 28,29 FSLW (format 4 only),
 *           30,31 user stack pointer, 32..47 the 8 long words at the user stack pointer
 *   48..59  A8.5: the application's first three segments: start (2 words), size (2 words) each
 *   60..63  A8.5: minigl.library's first segment: start, size
 *   64..319 ring of the last 256 calls entered: (sequence >> 8 & 255) << 8 | slot, at index sequence & 255
 *   320..327 A8.19, ZZMiniGLLoad PAULAGUARD only: 320 = 0x4755 once the application's audio interrupt
 *           handler runs inside the guard (trap.S); 321 handler entries, 322 handler returns (written at
 *           interrupt level), 323,324 the 68k frame count (words 4,5) at the last entry, 325 installs */
#ifndef ZZ_BLACKBOX_H
#define ZZ_BLACKBOX_H
#define ZZ_BB_OFFSET  4096u        /* below the control block */
#define ZZ_BB_WORDS   336u
#define ZZ_BB_AUD     320u        /* A8.19: audio guard words */
#define ZZ_BB_AUD_ON  0x4755u
#define ZZ_BB_RING    64u
#define ZZ_BB_MAGIC0  0x5A42u
#define ZZ_BB_MAGIC1  0x4234u
#define ZZ_BB_LAYOUT  2u          /* A8.5: trap record and segments */
#define ZZ_BB_TRAP    0x5452u
#define ZZ_BB_CLOSED  0x8000u
extern volatile unsigned short *zz_bb;
extern unsigned int zz_bb_seq;
#define ZZ_BB_IN(i,s)  do{ s=++zz_bb_seq; if(zz_bb) zz_bb[ZZ_BB_RING+(s&255u)]=(unsigned short)((((s)>>8)&255u)<<8|(i)); }while(0)
#define ZZ_BB_OUT(i,s) do{ if(zz_bb) zz_bb[8]=(unsigned short)(((s)&255u)<<8|(i)); }while(0)
#define ZZ_BB_PHASE(v) do{ if(zz_bb) zz_bb[3]=(unsigned short)(v); }while(0)
#endif
