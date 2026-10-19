/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * zzq2_svtab.c - ZZQuake2 0.8: functions and monster moves of the
 * save games, by name.
 *
 * The tables are made at build time by Core1/zzq2_svtab.py from the
 * game objects (build/gen/zzq2_svtab_gen.c, sorted by name). The
 * patched copy of game/g_save.c asks here:
 *   zzsv_name(move, addr)  the name of a function (move 0) or of an
 *                          mmove_t (move 1), 0 if it has none;
 *   zzsv_addr(move, name)  its address in THIS blob, 0 if unknown.
 * Names are looked up by binary search; addresses through an index
 * sorted by address, made at the first call.
 *
 * ASCII only.
 */
#include <string.h>

typedef struct { const char *name; const void *addr; } zzsv_sym_t;

extern const zzsv_sym_t zzsv_func_tab[], zzsv_move_tab[];
extern const int zzsv_func_n, zzsv_move_n;
extern unsigned short zzsv_func_byaddr[], zzsv_move_byaddr[];

static int zzsv_ready;

static void zzsv_sort(const zzsv_sym_t *t, int n, unsigned short *ix)
{
    int i, j;
    for (i = 0; i < n; i++)
        ix[i] = (unsigned short)i;
    /* shell sort by address (about 1300 entries, once per session) */
    for (j = n / 2; j > 0; j /= 2)
        for (i = j; i < n; i++) {
            unsigned short v = ix[i];
            int k = i;
            while (k >= j && (const char *)t[ix[k - j]].addr > (const char *)t[v].addr) {
                ix[k] = ix[k - j];
                k -= j;
            }
            ix[k] = v;
        }
}

static void zzsv_init(void)
{
    zzsv_sort(zzsv_func_tab, zzsv_func_n, zzsv_func_byaddr);
    zzsv_sort(zzsv_move_tab, zzsv_move_n, zzsv_move_byaddr);
    zzsv_ready = 1;
}

const char *zzsv_name(int move, const void *addr)
{
    const zzsv_sym_t *t = move ? zzsv_move_tab : zzsv_func_tab;
    const unsigned short *ix = move ? zzsv_move_byaddr : zzsv_func_byaddr;
    int lo = 0, hi = (move ? zzsv_move_n : zzsv_func_n) - 1;
    if (!zzsv_ready)
        zzsv_init();
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *a = (const char *)t[ix[mid]].addr;
        if (a == (const char *)addr)
            return t[ix[mid]].name;
        if (a < (const char *)addr)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

void *zzsv_addr(int move, const char *name)
{
    const zzsv_sym_t *t = move ? zzsv_move_tab : zzsv_func_tab;
    int lo = 0, hi = (move ? zzsv_move_n : zzsv_func_n) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(t[mid].name, name);
        if (!c)
            return (void *)t[mid].addr;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}
