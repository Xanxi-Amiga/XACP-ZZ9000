/* Host tests for the Core1 PCM ring. */
 































#include <stdio.h>
#include <string.h>

#include "../core1/zzdf_pcm.h"

/* The two constants the ring is built on. Mirrored rather than
   included: zzdf_config.h wants the ARM memory map. build_next.py
   checks the real ones at build time. */
#define RING_SIZE   0x10000u
#define RING_MASK   (RING_SIZE - 1u)
#define BLOCK       256u                 /* stereo frames             */
#define BLOCK_BYTES (BLOCK * 2u * 2u)
#define TARGET      (BLOCK_BYTES * 3u)   /* zzdf_audio.cpp            */
#define AHI_BYTES   BLOCK_BYTES          /* one AHI buffer            */
#define READY       (AHI_BYTES * 2u)     /* zzdf_ahi_ready()          */

static unsigned char g_ring[RING_SIZE];

static int g_pass = 0, g_fail = 0;
static void ok(int cond, const char *what)
{
	if (cond) { g_pass++; }
	else      { g_fail++; printf("  FAIL: %s\n", what); }
}

/* Exactly what the 68k does: one UWORD read through the window, which
   returns the two bytes interpreted big-endian. Signed, as AHI reads
   them. */
static int read_as_68k(unsigned int pos)
{
	unsigned int hi = g_ring[pos & RING_MASK];
	unsigned int lo = g_ring[(pos + 1u) & RING_MASK];
	int v = (int)((hi << 8) | lo);
	if (v & 0x8000) { v -= 0x10000; }
	return v;
}

/* ---- 1 + 2: round trip and byte layout --------------------------- */
static void test_roundtrip(void)
{
	static const int vals[] = {
		0, 1, -1, 127, -128, 255, -256, 1000, -1000,
		0x0100, 0x00FF, 0x7FFF, -32768, 12345, -12345
	};
	unsigned int n = sizeof(vals) / sizeof(vals[0]);
	unsigned int i, j, pos = 0;

	memset(g_ring, 0xAA, sizeof(g_ring));

	for (i = 0; i < n; i++)
	{
		for (j = 0; j < n; j++)
		{
			unsigned int p = pos;
			pos = zzdf_pcm_put(g_ring, pos, RING_MASK, vals[i], vals[j]);
			ok(read_as_68k(p)      == vals[i], "left sample survives the window");
			ok(read_as_68k(p + 2u) == vals[j], "right sample survives the window");
			/* layout: high byte FIRST, which is what AHI plays */
			ok(g_ring[p]      == (unsigned char)((vals[i] >> 8) & 0xFF),
			   "left high byte is first");
			ok(g_ring[p + 1u] == (unsigned char)(vals[i] & 0xFF),
			   "left low byte is second");
		}
	}
	ok(pos == (n * n * 4u) % RING_SIZE, "write position advanced by 4 per frame");
}

/* ---- 1b: the clamp ----------------------------------------------- */
static void test_clamp(void)
{
	ok(zzdf_pcm_clamp(0.0f)   == 0,      "silence is zero");
	ok(zzdf_pcm_clamp(1.0f)   == 32767,  "full scale positive");
	ok(zzdf_pcm_clamp(-1.0f)  == -32767, "full scale negative");
	ok(zzdf_pcm_clamp(2.0f)   == 32767,  "over range clamps high");
	ok(zzdf_pcm_clamp(-2.0f)  == -32768, "over range clamps low");
	ok(zzdf_pcm_clamp(0.5f)   == 16383,  "half scale");
}

/* ---- 3: wrap ------------------------------------------------------ */
static void test_wrap(void)
{
	unsigned int pos, next;

	memset(g_ring, 0, sizeof(g_ring));

	/* a frame that starts two bytes before the end: L lands at the end,
	   R wraps to the start */
	pos  = RING_SIZE - 2u;
	next = zzdf_pcm_put(g_ring, pos, RING_MASK, 0x1234, 0x5678);
	ok(next == 2u, "write position wrapped to 2");
	ok(read_as_68k(RING_SIZE - 2u) == 0x1234, "left readable across the seam");
	ok(read_as_68k(0)              == 0x5678, "right readable after the wrap");
	ok(g_ring[RING_SIZE - 2u] == 0x12 && g_ring[RING_SIZE - 1u] == 0x34,
	   "seam bytes in order");
	ok(g_ring[0] == 0x56 && g_ring[1] == 0x78, "wrapped bytes in order");

	/* a whole block written at the very end must land contiguously in
	   the reader's view */
	{
		unsigned int i;
		pos = RING_SIZE - 8u;
		for (i = 0; i < 4u; i++)
		{
			pos = zzdf_pcm_put(g_ring, pos, RING_MASK, (int)(i + 1u), -(int)(i + 1u));
		}
		ok(pos == 8u, "four frames from -8 end at +8");
		for (i = 0; i < 4u; i++)
		{
			unsigned int p = (RING_SIZE - 8u + i * 4u) & RING_MASK;
			ok(read_as_68k(p)      ==  (int)(i + 1u), "L across the seam");
			ok(read_as_68k(p + 2u) == -(int)(i + 1u), "R across the seam");
		}
	}
}

/* ---- 4: position arithmetic -------------------------------------- */
static void test_used(void)
{
	ok(zzdf_pcm_used(0u, 0u, RING_MASK) == 0u, "equal positions mean empty");
	ok(zzdf_pcm_used(100u, 0u, RING_MASK) == 100u, "simple difference");
	ok(zzdf_pcm_used(0u, 100u, RING_MASK) == RING_SIZE - 100u,
	   "reader ahead means the writer wrapped");
	/* the case the 16-bit rule exists for: writer has wrapped past
	   65535 and the raw subtraction is negative */
	ok(zzdf_pcm_used(4u, RING_SIZE - 4u, RING_MASK) == 8u,
	   "difference wraps correctly across 16 bits");
	ok(zzdf_pcm_used(RING_MASK, 0u, RING_MASK) == RING_MASK,
	   "one byte short of full is the maximum");
	/* every legal position fits in 16 bits, which is what makes a slot
	   atomic through the window */
	ok(RING_SIZE <= 0x10000u, "ring positions fit in 16 bits");
	ok((RING_SIZE & (RING_SIZE - 1u)) == 0u, "ring size is a power of two");
}

/* ---- 5: the latency bound ---------------------------------------- */
static void test_target(void)
{
	unsigned int wr = 0, rd = 0, blocks = 0, guard;

	/* Simulate the pump with NOTHING being consumed - which is exactly
	   the situation during a level load, before AHI has started. The
	   point of the target is that the ARM stops there instead of
	   banking the whole ring. */
	for (guard = 0; guard < 1000u; guard++)
	{
		if (zzdf_pcm_used(wr, rd, RING_MASK) >= TARGET) { break; }
		wr = (wr + BLOCK_BYTES) & RING_MASK;
		blocks++;
	}
	ok(blocks == 3u, "pump stops after three blocks, not at the ring");
	ok(zzdf_pcm_used(wr, rd, RING_MASK) == TARGET, "exactly the target is banked");

	/* 70 ms banked, not 1.49 s. This is the number that decides how late
	   a gunshot is heard. */
	ok((TARGET * 1000u) / (11025u * 4u) == 69u, "target is ~70 ms of audio");
	ok((RING_SIZE * 1000u) / (11025u * 4u) > 1400u,
	   "the ring itself would have been ~1.5 s - the bug this avoids");

	/* AHI must not wait for more than the ARM ever banks, or playback
	   would never start. */
	ok(READY <= TARGET, "AHI start threshold is reachable");

	/* Once playing, one AHI buffer consumed must leave room for exactly
	   one more block: the steady state is one in, one out. */
	rd = (rd + AHI_BYTES) & RING_MASK;
	ok(zzdf_pcm_used(wr, rd, RING_MASK) == TARGET - AHI_BYTES,
	   "a consumed buffer frees exactly one block");
	ok(AHI_BYTES == BLOCK_BYTES, "AHI buffer and mix block are the same size");
}

int main(void)
{
	printf("PCM ring tests\n");
	test_roundtrip();
	test_clamp();
	test_wrap();
	test_used();
	test_target();
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
