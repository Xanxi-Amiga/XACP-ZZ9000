/*
 * zzdf_crumbs.cpp - breadcrumb + first-frame evidence, called from the
 * instrumented engine copy. See zzdf_crumbs.h. ASCII only.
 */
#include "zzdf_crumbs.h"
#include <cstring>
#include <cstdio>

extern "C" {
#include "zzdf_config.h"
    extern void zzdf_log_puts(const char *s);
}

typedef unsigned int u32;
typedef unsigned char u8;

static volatile u32* s_sh = (volatile u32*)ZZDF_SHARED_ARM;
static int s_frame_seen = 0;

extern "C" void zzdf_crumb(u32 diag, const char *msg)
{
	s_sh[SH_DIAG] = diag;
	__asm__ volatile("dsb":::"memory");
	if (msg) {
		zzdf_log_puts("[ZZDF] ");
		zzdf_log_puts(msg);
		zzdf_log_puts("\n");
	}
}

/* CRC32 IEEE, table-free */
static u32 crc32_buf(const void* data, u32 len)
{
	const u8* p = (const u8*)data;
	u32 crc = 0xFFFFFFFFu;
	u32 i; int k;
	for (i = 0; i < len; i++) {
		crc ^= p[i];
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

extern "C" int zzdf_first_frame_seen(void) { return s_frame_seen; }

extern "C" void zzdf_log_heap(const char *what)
{
	char line[96];
	sprintf(line, "[ZZDF] heap after %s = %u (break %08X)\n", what,
	        (unsigned)s_sh[SH_HEAP_USED], (unsigned)s_sh[SH_HEAP_BREAK]);
	zzdf_log_puts(line);
}

extern "C" void zzdf_first_frame_evidence(const u8* fb, u32 w, u32 h,
                                          const u32* pal)
{
	u32 pixels = w * h;
	u8  seen[256];
	u32 distinct = 0;
	u32 i, s, k;
	char line[128];

	if (s_frame_seen || !fb) return;
	s_frame_seen = 1;

	memset(seen, 0, sizeof(seen));
	for (i = 0; i < pixels; i++)
		if (!seen[fb[i]]) { seen[fb[i]] = 1; distinct++; }

	s_sh[SH_FB_CRC32]      = crc32_buf(fb, pixels);
	s_sh[SH_PAL_CRC32]     = pal ? crc32_buf(pal, 256 * sizeof(u32)) : 0;
	s_sh[SH_FB_NONUNIFORM] = distinct;

	for (s = 0; s < 4; s++) {
		u32 word = 0;
		for (k = 0; k < 4; k++) {
			u32 idx = ((s * 4 + k) * pixels) / 16 + (pixels / 32);
			if (idx >= pixels) idx = pixels - 1;
			word |= ((u32)fb[idx]) << (k * 8);
		}
		s_sh[SH_FB_SAMPLE0 + s] = word;
	}

	sprintf(line, "first real SECBASE frame crc=%08X distinct=%u palcrc=%08X",
	        (unsigned)s_sh[SH_FB_CRC32], (unsigned)distinct,
	        (unsigned)s_sh[SH_PAL_CRC32]);
	zzdf_crumb(ZZDF_E_FIRST_FB, line);
	if (distinct <= 1)
		zzdf_log_puts("[ZZDF] WARNING: frame is uniform - nothing drawn\n");
}

 





extern "C" {
    extern void zzdf_log_puts(const char *s);
    static const char *s_extDataName = "(unknown)";

    void zzdf_extdata_mark(const char *name)
    {
        s_extDataName = name ? name : "(unknown)";
    }

    void zzdf_extdata_fail(void)
    {
        zzdf_log_puts("[ZZDF] EXTERNAL DATA MISSING: ");
        zzdf_log_puts(s_extDataName);
        zzdf_log_puts(" - engine falls back to compiled defaults\n");
    }
}
