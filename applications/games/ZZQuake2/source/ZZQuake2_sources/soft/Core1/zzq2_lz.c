/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_lz.c - ZZQuake2 0.7: the compressor of the save files.
 *
 * A byte-oriented LZ77 coder, written here from scratch. Its stream is
 * the LZ4 "block" layout (a public, simple format), so that a block can
 * be checked with any LZ4 block decoder on a PC; nothing of the LZ4
 * code is used.
 *
 *   sequence = token, [literal length bytes], literals,
 *              offset (2 bytes, little endian), [match length bytes]
 *   token    = literal count (high 4 bits) and match length - 4 (low
 *              4 bits); 15 in a field means "more bytes follow", each
 *              byte adding up to 255, the first byte below 255 ending
 *              the count
 *   the last sequence has literals only (no offset, no match)
 *
 * Why it suits a Quake II save: a .sav is the edicts of a map written
 * whole (892 bytes each) and is mostly zeros (84 % measured on base2 of
 * the full game); a .sv2 is 133120 bytes of configstrings, most of them
 * empty. Runs of zeros become one match each.
 *
 * Runs on Core1 (Cortex-A9), never on the 68k: the 68k only copies the
 * compressed bytes. No allocation here, no global state but one hash
 * table; every access is a byte access (-mno-unaligned-access).
 *
 * The decoder checks every length and every offset against both
 * buffers before it copies anything: a damaged file is refused (-1), it
 * can never write outside the output buffer nor read outside the input.
 *
 * ASCII only.
 */

#include <stddef.h>

typedef unsigned int  u32;
typedef unsigned char u8;

#define LZ_HASH_LOG   13
#define LZ_HASH_SIZE  (1u << LZ_HASH_LOG)
#define LZ_MIN_MATCH  4
#define LZ_LAST_LIT   5          /* the last 5 bytes are always literals */
#define LZ_MAX_OFF    65535u

static u32 g_lz_hash[LZ_HASH_SIZE];  /* position + 1 of a 4-byte string, 0 = none */

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 lz_hash(u32 v)
{
    return (v * 2654435761u) >> (32 - LZ_HASH_LOG);
}

/* Largest output of zzq2_lz_compress for n input bytes. */
u32 zzq2_lz_bound(u32 n)
{
    return n + n / 255u + 16u;
}

/* Writes a length continuation (the part of a field over 15). */
static u8 *put_len(u8 *op, u32 v)
{
    while (v >= 255u) { *op++ = 255u; v -= 255u; }
    *op++ = (u8)v;
    return op;
}

static u8 *put_seq(u8 *op, const u8 *lit, u32 nlit, u32 off, u32 mlen)
{
    u32 m = mlen - LZ_MIN_MATCH, k;
    u8 *tok = op++;
    *tok = (u8)(((nlit < 15u ? nlit : 15u) << 4) | (m < 15u ? m : 15u));
    if (nlit >= 15u) op = put_len(op, nlit - 15u);
    for (k = 0; k < nlit; k++) *op++ = lit[k];
    *op++ = (u8)(off & 0xFFu);
    *op++ = (u8)(off >> 8);
    if (m >= 15u) op = put_len(op, m - 15u);
    return op;
}

/* Compresses src[0..n) into dst (at least zzq2_lz_bound(n) bytes).
   Returns the number of bytes written. */
u32 zzq2_lz_compress(const u8 *src, u32 n, u8 *dst)
{
    u32 ip = 0, anchor = 0, k;
    u8 *op = dst;

    for (k = 0; k < LZ_HASH_SIZE; k++) g_lz_hash[k] = 0;

    if (n > LZ_LAST_LIT + LZ_MIN_MATCH) {
        u32 limit = n - LZ_LAST_LIT - LZ_MIN_MATCH;   /* last match start */
        while (ip <= limit) {
            u32 v = rd32(src + ip);
            u32 h = lz_hash(v);
            u32 ref = g_lz_hash[h];
            g_lz_hash[h] = ip + 1u;
            if (ref && ip - (ref - 1u) <= LZ_MAX_OFF && rd32(src + ref - 1u) == v) {
                u32 r = ref - 1u, mlen = LZ_MIN_MATCH;
                u32 mmax = n - LZ_LAST_LIT - ip;              /* keep the tail */
                while (mlen < mmax && src[r + mlen] == src[ip + mlen]) mlen++;
                op = put_seq(op, src + anchor, ip - anchor, ip - r, mlen);
                /* remember the positions inside the match, sparsely */
                for (k = ip + 1u; k + 4u <= ip + mlen && k <= limit; k += 2u)
                    g_lz_hash[lz_hash(rd32(src + k))] = k + 1u;
                ip += mlen;
                anchor = ip;
                continue;
            }
            ip++;
        }
    }
    /* last literals */
    {
        u32 nlit = n - anchor;
        u8 *tok = op++;
        *tok = (u8)((nlit < 15u ? nlit : 15u) << 4);
        if (nlit >= 15u) op = put_len(op, nlit - 15u);
        for (k = 0; k < nlit; k++) *op++ = src[anchor + k];
    }
    return (u32)(op - dst);
}

/* Reads a length continuation; -1 if the input ends first. */
static int get_len(const u8 **pp, const u8 *end, u32 *v)
{
    const u8 *p = *pp;
    u32 b;
    do {
        if (p >= end) return -1;
        b = *p++;
        if (*v > 0xFFFFFFFFu - b) return -1;
        *v += b;
    } while (b == 255u);
    *pp = p;
    return 0;
}

/* Decompresses src[0..n) into dst, which must receive EXACTLY out_n
   bytes. 0 on success, -1 if the stream is damaged or does not give
   out_n bytes. */
int zzq2_lz_decompress(const u8 *src, u32 n, u8 *dst, u32 out_n)
{
    const u8 *ip = src, *end = src + n;
    u32 op = 0;

    while (ip < end) {
        u32 tok = *ip++;
        u32 nlit = tok >> 4, mlen, off, k;
        if (nlit == 15u && get_len(&ip, end, &nlit)) return -1;
        if (nlit > (u32)(end - ip) || nlit > out_n - op) return -1;
        for (k = 0; k < nlit; k++) dst[op + k] = ip[k];
        ip += nlit;
        op += nlit;
        if (ip == end) break;                       /* last sequence */
        if (end - ip < 2) return -1;
        off = (u32)ip[0] | ((u32)ip[1] << 8);
        ip += 2;
        if (off == 0u || off > op) return -1;
        mlen = tok & 15u;
        if (mlen == 15u && get_len(&ip, end, &mlen)) return -1;
        mlen += LZ_MIN_MATCH;
        if (mlen > out_n - op) return -1;
        for (k = 0; k < mlen; k++) dst[op + k] = dst[op - off + k];   /* overlap allowed */
        op += mlen;
    }
    return op == out_n ? 0 : -1;
}
