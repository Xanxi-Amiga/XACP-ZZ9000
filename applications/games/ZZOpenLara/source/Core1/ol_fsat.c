/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
/* ZZOpenLaraRIX A2.19: reading part of a large file of PROGDIR:data through the release file service.
 *
 * The release service (runtime/zzquake_fs_mem.c + fs_service of the launcher) reads a whole file of
 * 512 KiB at most into one of the four exchange slots (ZZQ_FILEBUF_AT, ARM 0x06E00000, non-cacheable).
 * The music container is about 50 MB: the ZZOpenLaraRIX launcher answers one more command,
 * FS_CMD_READAT (4): SH_FS_OFFSET / SH_FS_CHUNK = byte offset / length (at most ZZQ_FILEBUF_SIZE), the
 * answer is SH_FS_SIZE = bytes read and SH_FS_EXISTS = size of the whole file. Same request block, same
 * sequence / acknowledge rule as the release requests (Core1 writes SEQ last, the 68k writes ACK last).
 *
 * The runtime's own requests are synchronous and come from the same thread (the engine), so the two
 * never overlap. The exchange slot is one the runtime does not hold (SH_FS_SLOTMASK).
 * Used at level load only (Core1/ol_music.c): the wait may be long, the timeout is 20 s per chunk.
 * ASCII only.
 */
#include "zzquake_config.h"

typedef unsigned int u32;
typedef unsigned char u8;

extern volatile u32 *shared;
extern u32 zzq_us_now(void);

#define OL_FS_CMD_READAT  4u

static u32 g_req, g_fail, g_us;

static void publish_name(const char *name)
{
    u8 buf[ZZQ_FNAME_MAX];
    u32 i, k;
    for (i = 0; i < ZZQ_FNAME_MAX; i++) buf[i] = 0;
    for (i = 0; i < ZZQ_FNAME_MAX - 1 && name[i]; i++) buf[i] = (u8)name[i];
    for (i = 0; i < ZZQ_FNAME_MAX / 4; i++) {
        u32 w = 0;
        for (k = 0; k < 4; k++) w |= (u32)buf[i * 4 + k] << (k * 8);
        shared[SH_FS_NAME + i] = w;
    }
}

static int free_slot(void)
{
    u32 m = shared[SH_FS_SLOTMASK];
    int s;
    for (s = ZZQ_FILEBUF_SLOTS - 1; s >= 0; s--)
        if (!(m & (1u << s))) return s;
    return -1;
}

/* A2.21: one request in the background (the next block of a video while the current one plays). Only
   one at a time; any other file request of Core1 waits for it first (ol_fs_read_at_quiesce: here, in
   ol_fs_read_at, and in Core1/ol_fs.c before the release service is used). */
static int g_as_on;
static u32 g_as_seq, g_as_len, g_as_t0;
static int g_as_slot;
static u32 g_as_req, g_as_fail;

int ol_fs_read_at_async(const char *name, unsigned off, unsigned len)
{
    int slot;
    if (g_as_on || len == 0 || len > ZZQ_FILEBUF_SIZE) return -1;
    slot = free_slot();
    if (slot < 0) return -1;
    publish_name(name);
    shared[SH_FS_SIZE]   = 0;
    shared[SH_FS_OFFSET] = off;
    shared[SH_FS_CHUNK]  = len;
    shared[SH_FS_ERR]    = 0;
    shared[SH_FS_EXISTS] = 0;
    shared[SH_FS_SLOT]   = (u32)slot;
    shared[SH_FS_CMD]    = OL_FS_CMD_READAT;
    __asm__ volatile("dsb sy" ::: "memory");
    g_as_seq = shared[SH_FS_SEQ] + 1u;
    shared[SH_FS_SEQ] = g_as_seq;              /* trigger last */
    __asm__ volatile("dsb sy" ::: "memory");
    g_as_on = 1; g_as_len = len; g_as_slot = slot; g_as_t0 = zzq_us_now();
    g_as_req++;
    return 0;
}

static void copy_slot(int slot, u8 *out, u32 got)
{
    const volatile u8 *src = (const volatile u8 *)ZZQ_FILEBUF_AT(slot);
    u32 i;
    if ((((u32)(unsigned long)out) & 3u) == 0) {
        const volatile u32 *s4 = (const volatile u32 *)src;
        u32 *d4 = (u32 *)out;
        for (i = 0; i < got / 4u; i++) d4[i] = s4[i];
        for (i = got & ~3u; i < got; i++) out[i] = src[i];
    } else {
        for (i = 0; i < got; i++) out[i] = src[i];
    }
}

/* 1 = done (dst filled, *got bytes), 0 = not yet, -1 = failed or nothing pending */
int ol_fs_read_at_poll(void *dst, unsigned *got)
{
    u32 n;
    if (!g_as_on) return -1;
    if (shared[SH_FS_ACK] != g_as_seq) {
        if ((u32)(zzq_us_now() - g_as_t0) > 20000000u) { g_as_on = 0; g_as_fail++; return -1; }
        return 0;
    }
    __asm__ volatile("dmb sy" ::: "memory");
    g_as_on = 0;
    if (shared[SH_FS_ERR] != 0) { g_as_fail++; return -1; }
    n = shared[SH_FS_SIZE];
    if (n > g_as_len) n = g_as_len;
    if (dst) copy_slot(g_as_slot, (u8 *)dst, n);
    if (got) *got = n;
    return 1;
}

int ol_fs_read_at_pending(void) { return g_as_on; }

/* waits for the background request, its data dropped */
void ol_fs_read_at_quiesce(void)
{
    while (g_as_on) {
        volatile u32 k;
        if (ol_fs_read_at_poll(0, 0) != 0) break;
        for (k = 0; k < 1000u; k++) { __asm__ volatile("nop"); }
    }
}

/* Reads len bytes at off of PROGDIR:data/<name> into dst. Returns the number of bytes read (less than
   len at the end of the file), -1 on error. *total (may be 0) = size of the file. */
int ol_fs_read_at(const char *name, unsigned off, void *dst, unsigned len, unsigned *total)
{
    u8 *out = (u8 *)dst;
    unsigned done = 0;
    int slot;
    u32 t0;
    ol_fs_read_at_quiesce();   /* A2.21: never two requests at once */
    slot = free_slot();
    t0 = zzq_us_now();
    if (slot < 0) { g_fail++; return -1; }
    while (done < len) {
        u32 want = len - done, seq, start, got, i;
        const volatile u8 *src;
        if (want > ZZQ_FILEBUF_SIZE) want = ZZQ_FILEBUF_SIZE;
        publish_name(name);
        shared[SH_FS_SIZE]   = 0;
        shared[SH_FS_OFFSET] = off + done;
        shared[SH_FS_CHUNK]  = want;
        shared[SH_FS_ERR]    = 0;
        shared[SH_FS_EXISTS] = 0;
        shared[SH_FS_SLOT]   = (u32)slot;
        shared[SH_FS_CMD]    = OL_FS_CMD_READAT;
        __asm__ volatile("dsb sy" ::: "memory");
        seq = shared[SH_FS_SEQ] + 1u;
        shared[SH_FS_SEQ] = seq;               /* trigger last */
        __asm__ volatile("dsb sy" ::: "memory");
        g_req++;
        start = zzq_us_now();
        while (shared[SH_FS_ACK] != seq) {
            volatile u32 k;
            for (k = 0; k < 1000u; k++) { __asm__ volatile("nop"); }
            if ((u32)(zzq_us_now() - start) > 20000000u) { g_fail++; return -1; }
        }
        __asm__ volatile("dmb sy" ::: "memory");
        if (shared[SH_FS_ERR] != 0) { g_fail++; return -1; }
        got = shared[SH_FS_SIZE];
        if (total) *total = shared[SH_FS_EXISTS];
        if (got > want) got = want;
        src = (const volatile u8 *)ZZQ_FILEBUF_AT(slot);
        if ((((u32)(unsigned long)out + done) & 3u) == 0) {
            const volatile u32 *s4 = (const volatile u32 *)src;
            u32 *d4 = (u32 *)(out + done);
            for (i = 0; i < got / 4u; i++) d4[i] = s4[i];
            for (i = got & ~3u; i < got; i++) out[done + i] = src[i];
        } else {
            for (i = 0; i < got; i++) out[done + i] = src[i];
        }
        done += got;
        if (got < want) break;                 /* end of the file */
    }
    g_us += zzq_us_now() - t0;
    return (int)done;
}

void ol_fs_read_at_stats(unsigned *requests, unsigned *failures, unsigned *us)
{
    if (requests) *requests = g_req;
    if (failures) *failures = g_fail;
    if (us) *us = g_us;
}
