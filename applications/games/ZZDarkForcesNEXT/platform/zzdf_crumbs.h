/*
 * zzdf_crumbs.h - breadcrumb hooks callable from the patched engine
 * copy (darkForcesMain.cpp, mission.cpp). C linkage so the call sites
 * stay one line each. ASCII only.
 */
#ifndef ZZDF_CRUMBS_H
#define ZZDF_CRUMBS_H

#ifdef __cplusplus
extern "C" {
#endif

/* publish a DIAG step + one short log line */
void zzdf_crumb(unsigned int diag, const char *msg);

/* first REAL mission frame: measures the indexed 320x200 buffer and
   the palette, publishes CRCs/distinct/samples, sets E090. Latches:
   only the first call does anything. */
void zzdf_first_frame_evidence(const unsigned char *fb, unsigned int w,
                               unsigned int h, const unsigned int *pal);

/* 1 once zzdf_first_frame_evidence has fired */
int zzdf_first_frame_seen(void);

 



void zzdf_cfgscreen_request(void);

/* log "[ZZDF] heap after <what> = N" from SH_HEAP_USED */
void zzdf_log_heap(const char *what);

#ifdef __cplusplus
}
#endif
#endif
