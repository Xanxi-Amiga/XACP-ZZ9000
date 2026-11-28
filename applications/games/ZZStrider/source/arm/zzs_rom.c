/*
 * zzs_rom.c - ZZStrider ROM reception and decoding on Core1.
 *
 * Transport (replaces the ZZRastan fixed staging windows, which are too small for CPS-1):
 *   the launcher reads one ROM file at a time, copies 64 KiB at most into the single stage
 *   window (ARM 0x04B00000, Non-Cacheable) and sends ZZS_CMD_ROM_CHUNK {file, offset, length}.
 *   Core1 copies the chunk to a private cached bounce buffer, adds it to the running CRC32 of
 *   that file and DECODES IT ON THE FLY into the private ROM store at 0x22400000. No raw copy
 *   of the ROMs is kept and the Amiga never needs more than one file in RAM.
 *   Chunks of a file must arrive in order; when a file is complete its CRC32 is compared with
 *   the MAME/FBA value. ZZS_CMD_ROM_COMMIT succeeds only when all files matched.
 *
 * Decoding is the FB Alpha 2012 one, made incremental:
 *   68000  : CPS1_68K_PROGRAM_BYTESWAP pairs -> host-order words (Cps1LoadRoms, d_cps1.cpp)
 *   tiles  : CpsLoadOne()/CpsLoadTiles() bit separation (cps.cpp) - SepTable[] below
 *   stars  : CpsLoadStars() (cps.cpp): every second byte of the first 0x2000 bytes of 2 ROMs
 * Those parts derive from FB Alpha (see licenses/FBAlpha-License.txt); the transport, CRC and
 * bookkeeping are original ZZStrider code.
 *
 * ASCII only.
 */
#include "zzs.h"
#include "zzs_game.h"

/* The CRC of every file is ALWAYS enforced in a delivered blob. Only the host regression
   harness (tests/), which runs synthetic ROMs, builds this file with ZZS_ROM_CRC_CHECK=0. */
#ifndef ZZS_ROM_CRC_CHECK
#define ZZS_ROM_CRC_CHECK 1
#endif

/* ------------------------------------------------------------------------------------------ */
enum { K_PRG_EVEN, K_PRG_ODD, K_PRG_WORDS, K_GFX, K_Z80, K_OKI };

typedef struct zzs_romfile_s {
    const char *name;
    zzs_u32 size;
    zzs_u32 crc;
    zzs_u8  kind;
    zzs_u8  shift;      /* K_GFX: bit plane shift (0 or 2) */
    zzs_u8  star;       /* K_GFX: 1 or 2 = also feeds star field 0 / 1 */
    zzs_u32 base;       /* destination offset inside its region */
} zzs_romfile_t;

/* MAME "strider" (parent). Order = FB Alpha StriderRomDesc, which fixes the decode roles. */
static const zzs_romfile_t g_files[ZZS_ROM_FILE_COUNT] = {
    /* 68000: two ROM_LOAD16_BYTE pairs, then one ROM_LOAD16_WORD_SWAP */
    { "30.11f",    0x020000u, 0xda997474u, K_PRG_EVEN,  0, 0, 0x000000u },
    { "35.11h",    0x020000u, 0x5463aaa3u, K_PRG_ODD,   0, 0, 0x000000u },
    { "31.12f",    0x020000u, 0xd20786dbu, K_PRG_EVEN,  0, 0, 0x040000u },
    { "36.12h",    0x020000u, 0x21aa2863u, K_PRG_ODD,   0, 0, 0x040000u },
    { "st-14.8h",  0x080000u, 0x9b3cfc08u, K_PRG_WORDS, 0, 0, 0x080000u },
    /* tiles: CpsLoadTiles(CpsGfx + 0x000000, 5) and CpsLoadTiles(CpsGfx + 0x200000, 9) */
    { "st-2.8a",   0x080000u, 0x4eee9aeau, K_GFX, 0, 1, 0x000000u },
    { "st-11.10a", 0x080000u, 0x2d7f21e4u, K_GFX, 2, 0, 0x000000u },
    { "st-5.4a",   0x080000u, 0x7705aa46u, K_GFX, 0, 2, 0x000004u },
    { "st-9.6a",   0x080000u, 0x5b18b722u, K_GFX, 2, 0, 0x000004u },
    { "st-1.7a",   0x080000u, 0x005f000bu, K_GFX, 0, 0, 0x200000u },
    { "st-10.9a",  0x080000u, 0xb9441519u, K_GFX, 2, 0, 0x200000u },
    { "st-4.3a",   0x080000u, 0xb7d04e8bu, K_GFX, 0, 0, 0x200004u },
    { "st-8.5a",   0x080000u, 0x6b4713b4u, K_GFX, 2, 0, 0x200004u },
    /* Z80 */
    { "09.12b",    0x010000u, 0x2ed403bcu, K_Z80, 0, 0, 0x000000u },
    /* MSM6295 */
    { "18.11c",    0x020000u, 0x4386bc80u, K_OKI, 0, 0, 0x000000u },
    { "19.12c",    0x020000u, 0x444536d7u, K_OKI, 0, 0, 0x020000u }
};

zzs_u8 *g_zzs_gfx = (zzs_u8 *)ZZS_STORE_GFX;
zzs_u8 *g_zzs_prg = (zzs_u8 *)ZZS_STORE_PRG;
zzs_u8 *g_zzs_z80 = (zzs_u8 *)ZZS_STORE_Z80;
zzs_u8 *g_zzs_oki = (zzs_u8 *)ZZS_STORE_OKI;

static zzs_u32 g_rx[ZZS_ROM_FILE_COUNT];        /* bytes received per file */
static zzs_u32 g_crc[ZZS_ROM_FILE_COUNT];       /* running CRC32 (pre-inversion) */
static zzs_u32 g_ok_mask;
static int     g_open;                          /* between BEGIN and COMMIT */
static int     g_ready;                         /* COMMIT succeeded */
static zzs_u32 g_bytes, g_chunks;

static zzs_u32 g_bounce_words[ZZS_STAGE_CHUNK / 4u];

/* ------------------------------------------------------------------------------------------ */
static zzs_u32 g_crctab[256];
static int     g_crctab_ready;

static void crc_init(void)
{
    zzs_u32 i, j, c;
    for (i = 0; i < 256u; i++) {
        c = i;
        for (j = 0; j < 8u; j++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crctab[i] = c;
    }
    g_crctab_ready = 1;
}

/* crc is the running value: start with 0xFFFFFFFF, invert at the end. */
zzs_u32 zzs_crc32_update(zzs_u32 crc, const zzs_u8 *p, zzs_u32 n)
{
    zzs_u32 i;
    if (!g_crctab_ready)
        crc_init();
    for (i = 0; i < n; i++)
        crc = g_crctab[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

/* --- FB Alpha cps.cpp: Separate() / SepTable[] ------------------------------------------------
 * Spreads the 8 bits of a ROM byte over 8 nibbles, one bit per pixel, inverted (pen 15 of the
 * ROM becomes pen 0 = transparent for the plotters). */
static zzs_u32 g_sep[256];
static int     g_sep_ready;

static zzs_u32 separate(zzs_u32 b)
{
    zzs_u32 a = b;                                          /* 00000000 00000000 00000000 11111111 */
    a = ((a & 0x000000F0u) << 12) | (a & 0x0000000Fu);      /* 00000000 00001111 00000000 00001111 */
    a = ((a & 0x000C000Cu) <<  6) | (a & 0x00030003u);      /* 00000011 00000011 00000011 00000011 */
    a = ((a & 0x02020202u) <<  3) | (a & 0x01010101u);      /* 00010001 00010001 00010001 00010001 */
    return a;
}

static void sep_init(void)
{
    zzs_u32 i;
    for (i = 0; i < 256u; i++)
        g_sep[i] = separate(255u - i);
    g_sep_ready = 1;
}

/* ------------------------------------------------------------------------------------------ */
void zzs_rom_begin(void)
{
    zzs_u32 i;
    zzs_u32 *w = (zzs_u32 *)ZZS_ARM_ROMSTORE;
    zzs_u32 n = (ZZS_STORE_USED_END - ZZS_ARM_ROMSTORE) / 4u;

    /* The tile decoder ORs bit planes into the store, so it MUST start from zero. */
    for (i = 0; i < n; i++)
        w[i] = 0u;

    for (i = 0; i < ZZS_ROM_FILE_COUNT; i++) {
        g_rx[i] = 0u;
        g_crc[i] = 0xFFFFFFFFu;
    }
    g_ok_mask = 0u;
    g_bytes = 0u;
    g_chunks = 0u;
    g_ready = 0;
    g_open = 1;
    if (!g_sep_ready)
        sep_init();

    ZZS_SH(ZZS_SH_ROM_BYTES) = 0u;
    ZZS_SH(ZZS_SH_ROM_CHUNKS) = 0u;
    ZZS_SH(ZZS_SH_ROM_FILES_OK) = 0u;
    ZZS_SH(ZZS_SH_ROM_BAD_FILE) = 0xFFFFFFFFu;
    ZZS_SH(ZZS_SH_ROM_BAD_CRC) = 0u;
    ZZS_SH(ZZS_SH_ROM_PRG_CRC) = 0u;
    ZZS_SH(ZZS_SH_ROM_GFX_CRC) = 0u;
    ZZS_SH(ZZS_SH_ROM_SET) = 0u;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_ROM_BEGIN;
}

static void decode_chunk(const zzs_romfile_t *f, zzs_u32 offset, const zzs_u8 *d, zzs_u32 n)
{
    zzs_u32 i;

    switch (f->kind) {
    case K_PRG_EVEN:    /* bytes of the EVEN 68000 addresses (high bytes): host offset + 1 */
        {
            zzs_u8 *dst = g_zzs_prg + f->base + offset * 2u + 1u;
            for (i = 0; i < n; i++)
                dst[i * 2u] = d[i];
        }
        break;
    case K_PRG_ODD:     /* bytes of the ODD 68000 addresses (low bytes): host offset + 0 */
        {
            zzs_u8 *dst = g_zzs_prg + f->base + offset * 2u;
            for (i = 0; i < n; i++)
                dst[i * 2u] = d[i];
        }
        break;
    case K_PRG_WORDS:   /* ROM_LOAD16_WORD_SWAP: the file already holds host-order words */
        {
            zzs_u8 *dst = g_zzs_prg + f->base + offset;
            for (i = 0; i < n; i++)
                dst[i] = d[i];
        }
        break;
    case K_GFX:         /* CpsLoadOne(Tile, nNum, nWord = 1, nShift) */
        {
            zzs_u32 *pt = (zzs_u32 *)(void *)(g_zzs_gfx + f->base + (offset / 2u) * 8u);
            zzs_u32 sh = f->shift;
            for (i = 0; i + 1u < n; i += 2u) {
                zzs_u32 pix = g_sep[d[i]] | (g_sep[d[i + 1u]] << 1);
                *pt |= pix << sh;
                pt += 2;                                /* next 8 pixels: + 8 bytes */
            }
            if (f->star != 0u && offset < ZZS_STAR_SIZE) {
                /* CpsLoadStars(): pStar[k] = rom[k << 1], k < 0x1000 */
                zzs_u8 *star = g_zzs_gfx + ZZS_GFX_SIZE + (zzs_u32)(f->star - 1u) * 0x1000u;
                for (i = 0; i < n && offset + i < ZZS_STAR_SIZE; i += 2u)
                    star[(offset + i) >> 1] = d[i];
            }
        }
        break;
    case K_Z80:
        {
            zzs_u8 *dst = g_zzs_z80 + f->base + offset;
            for (i = 0; i < n; i++)
                dst[i] = d[i];
        }
        break;
    default:            /* K_OKI */
        {
            zzs_u8 *dst = g_zzs_oki + f->base + offset;
            for (i = 0; i < n; i++)
                dst[i] = d[i];
        }
        break;
    }
}

/* returns 1 on success; on failure sets ERROR/ERROR_AUX (STATUS stays as it is: the launcher
   checks ERROR after every acknowledged chunk and aborts cleanly). */
int zzs_rom_chunk(zzs_u32 file, zzs_u32 offset, zzs_u32 length)
{
    const zzs_romfile_t *f;
    const volatile zzs_u32 *stage = (const volatile zzs_u32 *)ZZS_ARM_STAGE;
    zzs_u8 *bounce = (zzs_u8 *)(void *)g_bounce_words;
    zzs_u32 i, words;

    if (!g_open) {
        ZZS_SH(ZZS_SH_ERROR_AUX) = file;
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_STATE;
        return 0;
    }
    if (file >= ZZS_ROM_FILE_COUNT) {
        ZZS_SH(ZZS_SH_ERROR_AUX) = file;
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_BOUNDS;
        return 0;
    }
    f = &g_files[file];
    if (length == 0u || length > ZZS_STAGE_CHUNK || (length & 1u) != 0u || (offset & 1u) != 0u ||
        offset > f->size || length > f->size - offset) {
        ZZS_SH(ZZS_SH_ERROR_AUX) = file;
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_BOUNDS;
        return 0;
    }
    if (offset != g_rx[file]) {
        ZZS_SH(ZZS_SH_ERROR_AUX) = (file << 24) | (g_rx[file] & 0x00FFFFFFu);
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_ORDER;
        return 0;
    }

    /* Non-cacheable stage -> cached private bounce buffer, 32 bits at a time. */
    words = (length + 3u) / 4u;
    for (i = 0; i < words; i++)
        g_bounce_words[i] = stage[i];

    g_crc[file] = zzs_crc32_update(g_crc[file], bounce, length);
    decode_chunk(f, offset, bounce, length);
    g_rx[file] += length;
    g_bytes += length;
    g_chunks++;

    ZZS_SH(ZZS_SH_ROM_BYTES) = g_bytes;
    ZZS_SH(ZZS_SH_ROM_CHUNKS) = g_chunks;
    ZZS_SH(ZZS_SH_ROM_CRC) = g_crc[file] ^ 0xFFFFFFFFu;

    if (g_rx[file] == f->size) {
        zzs_u32 crc = g_crc[file] ^ 0xFFFFFFFFu;
        if (ZZS_ROM_CRC_CHECK && crc != f->crc) {
            ZZS_SH(ZZS_SH_ROM_BAD_FILE) = file;
            ZZS_SH(ZZS_SH_ROM_BAD_CRC) = crc;
            ZZS_SH(ZZS_SH_ERROR_AUX) = file;
            ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_CRC;
            return 0;
        }
        g_ok_mask |= (1u << file);
        ZZS_SH(ZZS_SH_ROM_FILES_OK) = g_ok_mask;
    }
    return 1;
}

static zzs_u32 prg_be32(zzs_u32 a)
{
    /* host-order words: the 68000 long at address a */
    const zzs_u16 *w = (const zzs_u16 *)(const void *)(g_zzs_prg + a);
    return ((zzs_u32)w[0] << 16) | (zzs_u32)w[1];
}

int zzs_rom_commit(void)
{
    zzs_u32 ssp, pc;

    if (!g_open) {
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_STATE;
        return 0;
    }
    if (g_ok_mask != ((1u << ZZS_ROM_FILE_COUNT) - 1u)) {
        ZZS_SH(ZZS_SH_ERROR_AUX) = g_ok_mask;
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_ROM_INCOMPLETE;
        return 0;
    }
    g_open = 0;

    ZZS_SH(ZZS_SH_ROM_PRG_CRC) = zzs_crc32_update(0xFFFFFFFFu, g_zzs_prg, ZZS_PRG_SIZE) ^ 0xFFFFFFFFu;
    ZZS_SH(ZZS_SH_ROM_GFX_CRC) = zzs_crc32_update(0xFFFFFFFFu, g_zzs_gfx, ZZS_GFX_SIZE) ^ 0xFFFFFFFFu;

    ssp = prg_be32(0u);
    pc  = prg_be32(4u);
    ZZS_SH(ZZS_SH_RESET_SSP) = ssp;
    ZZS_SH(ZZS_SH_RESET_PC) = pc;
    /* Work RAM is 0xFF0000..0xFFFFFF; the program lives below 1 MiB. With the CRCs right this
       can only fail if the decoder itself is wrong, which is exactly what it guards. */
    if (ssp < 0x00FF0000u || ssp > 0x01000000u || (ssp & 1u) != 0u ||
        pc >= ZZS_PRG_SIZE || (pc & 1u) != 0u) {
        ZZS_SH(ZZS_SH_ERROR_AUX) = pc;
        ZZS_SH(ZZS_SH_ERROR) = ZZS_ERR_BAD_VECTOR;
        return 0;
    }

    g_ready = 1;
    ZZS_SH(ZZS_SH_DIAG) = ZZS_DIAG_ROM_COMMIT_OK;
    return 1;
}

int zzs_rom_ready(void) { return g_ready; }
