#ifndef ZZPICO_FAULT_H
#define ZZPICO_FAULT_H
/* DIAG : first-fault recorder, hooke a l'entree de execute_exception (famec.c). */
void zzpico_first_fault_hook(void *ctx, int vect, unsigned oldpc, unsigned oldsr);
#endif
