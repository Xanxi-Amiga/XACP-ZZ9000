#ifndef ZZPICO_BUSTRACE_H
#define ZZPICO_BUSTRACE_H
/* DIAG p3a_diag_poll : trace bus 68k (VDP/Z80/I/O), zero impact fonctionnel.
   cat: 1=VDP ctl/status, 2=Z80 busreq, 3=Z80 reset, 4=I/O ports
   rw : 0=read 1=write   (le PC 68k est lu en interne via SekPc) */
#define BT_VDP     1
#define BT_BUSREQ  2
#define BT_ZRESET  3
#define BT_IO      4
void zzpico_bus_trace(int cat, int rw, unsigned addr, unsigned val);
#endif
