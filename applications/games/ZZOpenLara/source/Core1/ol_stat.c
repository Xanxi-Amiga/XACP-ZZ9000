/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLara A2.7: the measurement report, written at the end of the run to PROGDIR:data/olstat.txt
 * through the release file service (zzq_fopen / zzq_fwrite / zzq_fclose of runtime/zzquake_fs_mem.c:
 * the 68k side writes the basename into its data directory, as for the saves).
 * ASCII only.
 */
#include <stddef.h>

typedef struct zzq_file zzq_file;
extern zzq_file *zzq_fopen(const char *path, const char *mode);
extern size_t zzq_fwrite(const void *src, size_t sz, size_t n, zzq_file *f);
extern int zzq_fclose(zzq_file *f);

/* 1 = written */
extern void ol_fs_read_at_quiesce(void);
int ol_stat_write(const char *name, const char *text, unsigned len)
{
    zzq_file *f;
    ol_fs_read_at_quiesce();   /* A2.21: no background request (video block) under a release one */
    f = zzq_fopen(name, "w");
    int ok;
    if (!f) return 0;
    ok = (zzq_fwrite(text, 1, len, f) == len);
    if (zzq_fclose(f) != 0) ok = 0;
    return ok;
}
