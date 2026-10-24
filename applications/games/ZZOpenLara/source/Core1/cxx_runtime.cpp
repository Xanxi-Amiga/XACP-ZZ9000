/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
#include <stddef.h>
#include "ol_api.h"
extern "C" void *zzq2_malloc(size_t);
extern "C" void zzq2_free(void*);
/* A2: zero-filled, as the fresh pages the upstream platforms hand out. OpenLara reads members it never
   wrote (the 128 reserved entities of TR::Level, the texels no texture fills): on Core1 they would hold
   whatever an earlier block left there. */
extern "C" void *memset(void*,int,size_t);
static void *zeroed(size_t n){void*p=zzq2_malloc(n);if(p&&n)memset(p,0,n);return p;}
void* operator new(size_t n){return zeroed(n);}
void* operator new[](size_t n){return zeroed(n);}
void operator delete(void*p) noexcept {zzq2_free(p);}
void operator delete[](void*p) noexcept {zzq2_free(p);}
void operator delete(void*p,size_t) noexcept {zzq2_free(p);}
void operator delete[](void*p,size_t) noexcept {zzq2_free(p);}
extern "C" void __cxa_pure_virtual(){ol_fail("E231 PURE VIRTUAL",0,0);}
extern "C" void __cxa_bad_array_new_length(){ol_fail("E232 ARRAY SIZE",0,0);}
extern "C" int __aeabi_atexit(void*,void(*)(void*),void*){return 0;}
void *__dso_handle;
