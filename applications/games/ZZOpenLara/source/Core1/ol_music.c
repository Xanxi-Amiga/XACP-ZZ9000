/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLaraRIX A2.19: the music of the game, played by the engine's own mixer on Core1.
 *
 * The tracks are in one container, PROGDIR:data/ol_music.pak (a Quake PAK, as ol_level.pak), made on the
 * PC by tools/ol_music.py from the user's own discs: entries "track_NN.zzm", NN = OpenLara's track
 * number (the PSX disc's numbering). ZZM1 = 16-byte header ("ZZM1", rate, channels, frames, little
 * endian) + IMA ADPCM, 4 bits per sample (openlara/src/sound.h, struct ZZM).
 *
 * At every level load the engine passes the set of tracks the level can play (level.h, prepareMusic:
 * the ambient track, the SOUNDTRACK triggers of the floor data, the secret jingle, the voices of the
 * enemies present). Tracks no longer needed are freed, missing ones are read through the file service
 * (Core1/ol_fsat.c); a track kept from the previous level is not read again. The engine then finds
 * them in memory (gameflow.h, getGameTrack) and plays them exactly as on the PC: looped ambient, cues
 * played once, the secret jingle and the voices over the music, cutscenes timed by their track.
 * No file: no music, nothing else changes.
 * ASCII only.
 */
#include <stddef.h>
#include <string.h>

typedef unsigned int u32;
typedef unsigned char u8;

extern void *zzq2_malloc(size_t size);
extern void  zzq2_free(void *p);
extern int   zzq_printf(const char *fmt, ...);
extern u32   zzq_us_now(void);
extern int   ol_fs_read_at(const char *name, unsigned off, void *dst, unsigned len, unsigned *total);

#define OL_MUS_FILE   "ol_music.pak"
#define OL_MUS_TRACKS 256
#define OL_MUS_DIRMAX 128

static int g_state;                    /* 0 not looked at, 1 container present, -1 absent / bad */
static u32 g_off[OL_MUS_TRACKS], g_size[OL_MUS_TRACKS];
static u8 *g_data[OL_MUS_TRACKS];
static u32 g_count, g_bytes, g_loads, g_load_bytes, g_load_us, g_fail;

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* "track_NN.zzm" -> NN, -1 otherwise */
static int track_of(const char *n)
{
    int v = 0, i;
    if (strncmp(n, "track_", 6)) return -1;
    for (i = 6; n[i] >= '0' && n[i] <= '9'; i++) v = v * 10 + (n[i] - '0');
    if (i == 6 || i > 9 || strcmp(n + i, ".zzm") || v <= 0 || v >= OL_MUS_TRACKS) return -1;
    return v;
}

static void open_container(void)
{
    u8 hdr[12];
    static u8 dir[OL_MUS_DIRMAX * 64];
    unsigned total = 0;
    u32 doff, dlen, i;
    g_state = -1;
    if (ol_fs_read_at(OL_MUS_FILE, 0, hdr, 12, &total) != 12 || memcmp(hdr, "PACK", 4)) {
        zzq_printf("OL MUSIC: no %s (no music)\n", OL_MUS_FILE);
        return;
    }
    doff = le32(hdr + 4); dlen = le32(hdr + 8);
    if (dlen % 64 || dlen > sizeof dir || doff > total || dlen > total - doff) {
        zzq_printf("OL MUSIC: %s: bad directory\n", OL_MUS_FILE);
        return;
    }
    if (ol_fs_read_at(OL_MUS_FILE, doff, dir, dlen, 0) != (int)dlen) {
        zzq_printf("OL MUSIC: %s: directory not read\n", OL_MUS_FILE);
        return;
    }
    for (i = 0; i < dlen / 64; i++) {
        char name[57];
        int t;
        u32 off = le32(dir + i * 64 + 56), sz = le32(dir + i * 64 + 60);
        memcpy(name, dir + i * 64, 56); name[56] = 0;
        t = track_of(name);
        if (t < 0 || sz < 16 || off > total || sz > total - off) continue;
        g_off[t] = off; g_size[t] = sz; g_count++;
    }
    g_state = 1;
    zzq_printf("OL MUSIC: %s, %u tracks, %u bytes\n", OL_MUS_FILE, g_count, total);
}

static int g_enabled = 1;

/* the sound is off (NOSOUND): nothing is read */
void ol_music_enable(int on) { g_enabled = on ? 1 : 0; }

/* mask: bit t = track t may be played by the level being loaded */
void ol_music_prepare(const u32 *mask)
{
    int t;
    u32 t0, n0;
    if (!g_enabled) return;
    if (g_state == 0) open_container();
    if (g_state < 0) return;
    for (t = 1; t < OL_MUS_TRACKS; t++) {
        int want = (mask[t >> 5] >> (t & 31)) & 1;
        if (g_data[t] && !want) { zzq2_free(g_data[t]); g_data[t] = 0; g_bytes -= g_size[t]; }
    }
    t0 = zzq_us_now(); n0 = g_load_bytes;
    for (t = 1; t < OL_MUS_TRACKS; t++) {
        int want = (mask[t >> 5] >> (t & 31)) & 1;
        if (!want || g_data[t] || !g_size[t]) continue;
        g_data[t] = (u8 *)zzq2_malloc(g_size[t]);
        if (!g_data[t]) { g_fail++; continue; }
        if (ol_fs_read_at(OL_MUS_FILE, g_off[t], g_data[t], g_size[t], 0) != (int)g_size[t] ||
            memcmp(g_data[t], "ZZM1", 4)) {
            zzq2_free(g_data[t]); g_data[t] = 0; g_fail++;
            zzq_printf("OL MUSIC: track %d not read\n", t);
            continue;
        }
        g_bytes += g_size[t]; g_loads++; g_load_bytes += g_size[t];
    }
    g_load_us += zzq_us_now() - t0;
    zzq_printf("OL MUSIC: level tracks in memory %u bytes, read now %u bytes in %u ms\n",
               g_bytes, g_load_bytes - n0, (zzq_us_now() - t0) / 1000u);
}

int ol_music_get(int track, const void **data, unsigned *size)
{
    if (track <= 0 || track >= OL_MUS_TRACKS || !g_data[track]) return 0;
    *data = g_data[track]; *size = g_size[track];
    return 1;
}

void ol_music_stats(unsigned *tracks, unsigned *bytes, unsigned *loads, unsigned *load_bytes, unsigned *load_ms, unsigned *fails)
{
    *tracks = g_count; *bytes = g_bytes; *loads = g_loads; *load_bytes = g_load_bytes;
    *load_ms = g_load_us / 1000u; *fails = g_fail;
}
