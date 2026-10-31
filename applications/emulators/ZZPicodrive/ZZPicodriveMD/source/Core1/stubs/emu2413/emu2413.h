#ifndef ZZPICO_EMU2413_STUB_H
#define ZZPICO_EMU2413_STUB_H
typedef struct __OPLL OPLL;
OPLL *OPLL_new(unsigned int clk, unsigned int rate);
void  OPLL_delete(OPLL*);
void  OPLL_reset(OPLL*);
void  OPLL_setChipType(OPLL*, int);
int   OPLL_calc(OPLL*);
void  OPLL_writeReg(OPLL*, unsigned int, unsigned int);
void  OPLL_writeIO(OPLL*, unsigned int, unsigned int);
#endif
