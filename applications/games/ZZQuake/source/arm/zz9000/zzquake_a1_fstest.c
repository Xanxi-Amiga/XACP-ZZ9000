/* Shared-memory protocol state. */























#include "zzquake_config.h"

typedef unsigned int  u32;
typedef unsigned char u8;

extern volatile u32 *shared;
/* Sys_File* live in zzquake_fs_mem.c - the SAME functions Quake uses */
extern int  Sys_FileOpenRead(char *path, int *hndl);
extern int  Sys_FileRead(int handle, void *dest, int count);
extern void Sys_FileSeek(int handle, int position);
extern void Sys_FileClose(int handle);
extern void dcache_invalidate_range(u32 start, u32 len);  /* mmu.c */

#define A1_DIAG(v) do { shared[SH_DIAG] = (v); \
                        __asm__ volatile("dsb" ::: "memory"); } while (0)

/* CRC32, poly 0xEDB88320 (zlib/PKZIP). Table-free (bit loop) so it
   is tiny and matches the 68k side byte-for-byte. Same init/final
   xor (0xFFFFFFFF) both sides. */
static u32 crc32_range(const u8 *p, u32 len)
{
    u32 crc = 0xFFFFFFFFu, i;
    int b;
    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static u32 rd_le32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* case-insensitive compare of a dir name (<=56, nul-padded) to a
   C string */
static int name_eq(const char *dir, const char *want)
{
    int i;
    for (i = 0; i < 56; i++) {
        char a = dir[i], b = want[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        if (b == 0) return 1;
    }
    return 1;
}

/* find a lump by name in the pak directory. Returns 1 and fills
   *pos/*len if found. */
static int dir_find(const char *want, u32 *pos, u32 *len)
{
    u32 base  = shared[SH_PAK_ADDR];
    u32 ofs   = shared[SH_PAK_DIR_OFS];
    u32 count = shared[SH_PAK_DIR_COUNT];
    u32 i;
    const u8 *e = (const u8 *)(base + ofs);
    for (i = 0; i < count; i++, e += 64) {
        if (name_eq((const char *)e, want)) {
            *pos = rd_le32(e + 56);
            *len = rd_le32(e + 60);
            return 1;
        }
    }
    return 0;
}

/* publish a lump name (up to 60 chars) into a slot window for the
   68k log */
static void publish_name(int first_slot, const char *s)
{
    int i, b = 0;
    for (i = 0; i < 15; i++) {
        u32 w = 0; int k;
        for (k = 0; k < 4; k++) {
            u8 c = 0;
            if (s && s[b]) { c = (u8)s[b]; b++; }
            w |= ((u32)c) << (k * 8);
        }
        shared[first_slot + i] = w;
    }
}

void zzq_a1_fstest(void)
{
    char namebuf[64];
    const u8 *dir_e;
    u32 pos = 0, len = 0;
    int h, n1, n2, n3;
    u8 buf1[16], buf2[16];
    int i, chosen = 0;
    static const char *known[] = {
        "gfx/palette.lmp", "progs.dat", "gfx/conchars", 0
    };

    A1_DIAG(0xA1B0);

    /* Cache/MMU handling follows the validated Core1 memory contract. */


    dcache_invalidate_range(shared[SH_PAK_ADDR], shared[SH_PAK_SIZE]);
    __asm__ volatile("dsb" ::: "memory");

    /* PAK filesystem handling. */



    shared[SH_A1_CRC32] = crc32_range((const u8 *)shared[SH_PAK_ADDR],
                                      shared[SH_PAK_SIZE]);
    A1_DIAG(0xA1B1);

    /* Step 2b: pick a known lump, else fall back to the first dir entry */
    for (i = 0; known[i]; i++) {
        if (dir_find(known[i], &pos, &len)) {
            chosen = 1;
            for (n1 = 0; known[i][n1] && n1 < 63; n1++) namebuf[n1] = known[i][n1];
            namebuf[n1] = 0;
            break;
        }
    }
    if (!chosen) {
        /* first entry of the directory */
        dir_e = (const u8 *)(shared[SH_PAK_ADDR] + shared[SH_PAK_DIR_OFS]);
        for (i = 0; i < 56 && dir_e[i]; i++) namebuf[i] = (char)dir_e[i];
        namebuf[i] = 0;
        pos = rd_le32(dir_e + 56);
        len = rd_le32(dir_e + 60);
    }
    publish_name(SH_LAST_FILE_REQ, namebuf);
    shared[SH_A1_LUMP_POS] = pos;
    shared[SH_A1_LUMP_LEN] = len;
    A1_DIAG(0xA1B2);

    /* PAK filesystem handling. */



    {
        static char pakpath[] = "id1/pak0.pak";
        int flen = Sys_FileOpenRead(pakpath, &h);
        if (h < 0 || flen < 0) {
            shared[SH_A1_RESULT] = 0xE1;    /* open failed */
            shared[SH_ERROR] = ZZQ_FATAL_BAD_FILE;
            return;
        }
    }
    A1_DIAG(0xA1B3);

    /* Step 4: seek to lump, read 16, seek back, read 16 again */
    Sys_FileSeek(h, (int)pos);
    n1 = Sys_FileRead(h, buf1, 16);
    Sys_FileSeek(h, (int)pos);
    n2 = Sys_FileRead(h, buf2, 16);
    for (i = 0; i < 16; i++) {
        if (buf1[i] != buf2[i]) {
            shared[SH_A1_RESULT] = 0xE2;    /* seek/read incoherent */
            shared[SH_ERROR] = ZZQ_FATAL_BAD_FILE;
            Sys_FileClose(h);
            return;
        }
    }
    /* publish first 8 bytes read, for eyeballing */
    shared[SH_A1_FIRST8_LO] = rd_le32(buf1);
    shared[SH_A1_FIRST8_HI] = rd_le32(buf1 + 4);
    A1_DIAG(0xA1B4);

    /* Step 5: read last 16 bytes (bounds) */
    if (len >= 16) {
        Sys_FileSeek(h, (int)(pos + len - 16));
        n3 = Sys_FileRead(h, buf2, 16);
        if (n3 != 16) {
            shared[SH_A1_RESULT] = 0xE3;    /* short read at tail */
            shared[SH_ERROR] = ZZQ_FATAL_BAD_FILE;
            Sys_FileClose(h);
            return;
        }
    }
    Sys_FileClose(h);
    A1_DIAG(0xA1B5);

    /* Step 6: benign not-found */
    {
        static char nope[] = "does/not/exist.xyz";
        int hh = -1;
        int r = Sys_FileOpenRead(nope, &hh);
        if (r >= 0 || hh >= 0) {
            shared[SH_A1_RESULT] = 0xE4;    /* phantom file opened */
            shared[SH_ERROR] = ZZQ_FATAL_BAD_FILE;
            return;
        }
    }
    A1_DIAG(0xA1B6);

    /* PAK filesystem handling. */
    (void)n1; (void)n2;
    shared[SH_A1_RESULT] = 0x00;
    shared[SH_STATUS] = ZZQ_PAK_OK;
    __asm__ volatile("dsb" ::: "memory");
    shared[SH_STATUS] = ZZQ_FS_OK;
}
