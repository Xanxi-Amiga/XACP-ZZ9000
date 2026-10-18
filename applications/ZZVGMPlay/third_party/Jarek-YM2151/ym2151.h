#ifndef ZZVGM_YM2151_H
#define ZZVGM_YM2151_H
#include "zzr_jarek_shim.h"
#define SAMPLE_BITS 16
#if SAMPLE_BITS == 16
typedef INT16 SAMP;
#endif
int YM2151Init(int num, int chipbase, int clock, int rate, void (*timer_cb)(INT32, INT32, double));
int ym2151_timer_over(int num, int timer);
void YM2151Shutdown(void);
void YM2151SetTimerInterleave(double d);
void YM2151ResetChip(int num);
void YM2151UpdateOne(int num, INT16 **buffers, int length);
void YM2151WriteReg(int n, int r, int v);
int YM2151ReadStatus(int n);
void YM2151SetIrqHandler(int n, void (*handler)(int irq));
void YM2151SetPortWriteHandler(int n, write8_handler handler);
#endif
