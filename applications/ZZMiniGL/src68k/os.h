#ifndef ZZ_OS_H
#define ZZ_OS_H
#include "wire.h"
typedef zu32 Z;
typedef void *Ptr;
extern Ptr Sys, Dos, P96, Exp, Timer, Int;   /* A7: Int = intuition.library */
extern Z zz_last_error,zz_badslot;
extern const unsigned char zz_arm_image[];
extern const Z zz_arm_size;
Ptr os_sys(void);
Ptr os_openlib(const char *,Z);
void os_closelib(Ptr);
Ptr os_task(Ptr);
Ptr os_findport(const char *);
Ptr os_find(const void *,const char *);
void os_forbid(void),os_permit(void);
void os_cache(Ptr,Z,Z);
Ptr os_alloc(Z,Z);Ptr os_setintvec(Z,Ptr);   /* A8.19: exec SetIntVector */
void os_free(Ptr,Z);
Ptr os_port(void);void os_delport(Ptr);
Ptr os_creq(Ptr,Z);void os_dreq(Ptr);
Z os_opendev(const char *,Z,Ptr,Z);void os_closedev(Ptr);Z clockread(Z *);
Ptr os_board(Ptr,Z,Z);
Z dos_output(void),dos_write(Z,const void *,Z),dos_delay(Z);
Z dos_open(const char *,Z),dos_close(Z);   /* A4R: frame dump file */
Z dos_setvar(const char *,const char *,Z,Z),dos_getvar(const char *,char *,Z,Z),dos_delvar(const char *,Z);
Z p96_best(Z *);Ptr p96_open(Z *);void p96_close(Ptr);
void p96_write(const void *,Z,Z,void *,Z,Z,Z,Z);
Z p96_lock(Ptr,void *,Z);
/* A7: input window and Workbench start-up */
Ptr int_openwindow(void *);void int_closewindow(Ptr);
void int_setpointer(Ptr,void *,Z,Z,Z,Z);void int_clearpointer(Ptr);
Z int_modifyidcmp(Ptr,Z);void zz_window_keys(int);   /* A8.28: MGLMainLoop */
Ptr os_getmsg(Ptr);void os_replymsg(Ptr);Ptr os_waitport(Ptr);Z os_setsignal(Z,Z);Z os_doio(Ptr);
Z dos_selectoutput(Z);Z dos_seek(Z,Z,Z);int zz_ctx_armed(void);   /* A8.3 */
volatile unsigned short *zz_blackbox(void);void zz_bb_push(void);   /* A8.4 */
Z zz_ctx_flags(void);   /* A8.7 */
#define ZZ_ARM_NOTRAP 1u
#define ZZ_ARM_NOLMBLEND 2u
#define ZZ_ARM_NOBLACKBOX 4u
#define ZZ_ARM_HOLDLOCK 8u   /* A8.9 */
#define ZZ_ARM_ONEPACKET 16u   /* A8.13: the whole frame in one packet (A8.12 behaviour) */
#define ZZ_ARM_NODRAW 32u      /* A8.13: measurement: draws and texture data encoded then dropped */
#define ZZ_ARM_NOLMPASS 64u    /* A8.13: diagnosis: no unit-1 (lightmap) pass, surfaces unlit */
#define ZZ_ARM_LMNEAREST 128u  /* A8.13: diagnosis: the lightmap pass samples its texels without filtering */
#define ZZ_ARM_TEXNEAREST 256u /* A8.13: diagnosis: every texture magnified without filtering */
#define ZZ_ARM_PROFILE 512u    /* A8.14: per-frame split of the 68060 time (front.c, PROFILE) */
#define ZZ_ARM_NORAW 1024u     /* A8.17: diagnosis: the A8.16 records (68k conversions, 68k unit-1 pass) */
#define ZZ_ARM_NOVCACHE 2048u  /* A8.18: measurement: no Core1 vertex cache */
#define ZZ_ARM_PAULAGUARD 8192u /* A8.19: experiment: the application's own audio interrupt handler (GLQuake's Paula driver) runs
                                 with the whole FPU state saved around it, and its entries are counted in the black box */
#define ZZ_ARM_NORGL 16384u    /* A8.21: diagnosis: the GL_RGL array hints are not announced (GLQuake R200 then draws the classic way) */
#define ZZ_ARM_LATESHOW 32768u  /* A8.23: the frame is shown by a later call, when Core1 has finished it (front.c show_run) */
#define ZZ_ARM_NOPOINTS 65536u  /* A8.23: GL_POINTS are counted and not drawn, as until A8.22 */
#define ZZ_ARM_AUTOEXIT 131072u /* A8.28: MGLMainLoop returns by itself after 15 seconds (unattended trials of programs that end on a key) */
#define ZZ_ARM_CHUNK32 4096u   /* A8.18: measurement: packets of 32 KiB as A8.13..A8.17 (4 KiB otherwise); the switch is BIGCHUNK
                                 since A8.19 (the loader compares letters only: CHUNK32 was never recognised) */
void p96_unlock(Ptr,Z);
int zz_fast_lock(Z *target,Z *pitch);
void zz_fast_unlock(void);
int zz_fast_probe(Z *a,Z *b);
int zz_fast_probe_ok(void);
int zz_fast_verified(void);
Z zz_fast_format(void);
Z zz_fast_copy_mode(void);
void zz_fast_timing(Z ticks);
extern Z zz_force_safe;
Ptr get_table(Ptr);
Z zz_clock(void);
int zz_platform_open(Z w,Z h);
int zz_adopt_size(Ptr window,Z *w,Z *h);void zz_adopt_refused(Z w,Z h);   /* A8.27: the inner size of an application window; the next zz_platform_open adopts it */
void zz_platform_close(void);
int zz_send(const zu8 *buf,Z size,int wait);
int zz_wait(void),zz_present(void),zz_pipelined(void),zz_acked(Z seq);   /* zz_acked: Core1 has acknowledged that packet (A8.23) */
Z zz_ticks_to_us(Z);
int zz_offscreen(void);Ptr zz_window(void);void zz_pointer(int hide);   /* A8.20 */ Z zz_eclock_hz(void);void zz_spin_until(Z);   /* A7 */
void zz_io_ticks(Z *io);   /* A8.14 */
Z zz_metric(unsigned);
void zz_log(const char *s),zz_num(const char *s,Z n),zz_hex(const char *s,Z n);
void zz_summary(void);
extern Z zz_profile;
void zz_profile_add(Z start,Z transfer,Z group);
Z zz_transfer_ticks(void);
int zz_diag(void);
#endif
