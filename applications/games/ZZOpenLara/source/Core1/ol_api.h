/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
#ifndef OL_API_H
#define OL_API_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void ol_fail(const char *stage, int a, int b) __attribute__((noreturn));
void ol_init(const void *data, unsigned size, const char *name);
void ol_tick(void);
void ol_shutdown(void);
unsigned ol_clock_us(void);
int ol_key(int *down, int *key);
void ol_mouse(int *x, int *y);
void ol_stop(void);
void ol_diag(unsigned stage);   /* A2: SH_DIAG = 'O','L',0,stage */
#ifdef __cplusplus
}
#endif
#endif
