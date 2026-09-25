 



















#include "zzdf_config.h"

typedef unsigned int u32;
typedef unsigned char u8;

extern u32  zzdf_fs_dirty_count(void);
extern int  zzdf_fs_dirty_entry(u32 idx, const char **name,
                                const void **data, u32 *size);
extern unsigned int zzdf_us_now(void);
extern void zzdf_log_puts(const char *s);

static volatile u32 *sh_wb = (volatile u32 *)ZZDF_SHARED_ARM;

static int wb_nameeq(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? (*a - 'A' + 'a') : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? (*b - 'A' + 'a') : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* The engine's own scratch, not the player's. Writing these back would
   put a growing log and a settings file we override at startup into the
   user's DATA directory every single run. */
static int wb_is_scratch(const char *name)
{
    return wb_nameeq(name, "the_force_engine_log.txt") ||
           wb_nameeq(name, "settings.ini");
}

/* Returns the number of files handed over. Bounded by a wall clock: a
   launcher that never acknowledges must not hang the teardown, because
   the teardown is what gets Core1 back to a reusable state. */
u32 zzdf_writeback_run(void)
{
    u32 n = zzdf_fs_dirty_count();
    u32 i, offered = 0, skipped = 0;
    u32 seq = 0;

    sh_wb[SH_WB_SEQ]     = 0;
    sh_wb[SH_WB_ACK]     = 0;
    sh_wb[SH_WB_SIZE]    = 0;
    sh_wb[SH_WB_COUNT]   = 0;
    sh_wb[SH_WB_DONE]    = 0;
    sh_wb[SH_WB_SKIPPED] = 0;

    /* Count what we will really offer first, so the launcher knows how
       many rounds to expect before the first one arrives. */
    for (i = 0; i < n; i++) {
        const char *name = 0; const void *data = 0; u32 size = 0;
        if (!zzdf_fs_dirty_entry(i, &name, &data, &size)) continue;
        if (!name || wb_is_scratch(name)) continue;
        if (size > ZZDF_WB_MAX) continue;
        offered++;
    }
    sh_wb[SH_WB_COUNT] = offered;
    __asm__ volatile("dsb":::"memory");

    if (!offered) { sh_wb[SH_WB_DONE] = 1; return 0; }

    for (i = 0; i < n; i++) {
        const char *name = 0; const void *data = 0; u32 size = 0;
        volatile u8 *stage = (volatile u8 *)ZZDF_STAGING_ARM;
        u32 k, t0;

        if (!zzdf_fs_dirty_entry(i, &name, &data, &size)) continue;
        if (!name || wb_is_scratch(name)) continue;
        if (size > ZZDF_WB_MAX) {
            /* Half a save file is worse than no save file. */
            skipped++;
            sh_wb[SH_WB_SKIPPED] = skipped;
            zzdf_log_puts("[WB] too large, not saved: ");
            zzdf_log_puts(name);
            zzdf_log_puts("\n");
            continue;
        }

        for (k = 0; k < ZZDF_WB_NAME; k++)
            stage[k] = (u8)((k < 23 && name[k]) ? name[k] : 0);
        for (k = 0; k < size; k++)
            stage[ZZDF_WB_NAME + k] = ((const u8 *)data)[k];

        sh_wb[SH_WB_SIZE] = size;
        __asm__ volatile("dsb":::"memory");
        seq++;
        sh_wb[SH_WB_SEQ] = seq;
        __asm__ volatile("dsb":::"memory");

        t0 = zzdf_us_now();
        while (sh_wb[SH_WB_ACK] != seq) {
            if (zzdf_us_now() - t0 > 3000000u) {
                zzdf_log_puts("[WB] launcher did not acknowledge, giving up\n");
                sh_wb[SH_WB_DONE] = 1;
                return seq - 1;
            }
        }
        zzdf_log_puts("[WB] saved ");
        zzdf_log_puts(name);
        zzdf_log_puts("\n");
    }

    sh_wb[SH_WB_DONE] = 1;
    __asm__ volatile("dsb":::"memory");
    return seq;
}
