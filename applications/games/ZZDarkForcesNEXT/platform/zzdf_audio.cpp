/* Core1 audio mixer and PCM ring producer. */
#include <TFE_System/types.h>
#include <TFE_System/math.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include "zzdf_config.h"
#include "zzdf_pcm.h"
	extern void zzdf_log_puts(const char *s);
	extern unsigned int zzdf_us_now(void);
}

namespace
{
	volatile u32* s_sh  = (volatile u32*)ZZDF_SHARED_ARM;
	volatile u8*  s_ring = (volatile u8*)ZZDF_PCM_ARM;

	/* The ARM owns WRITE_POS and keeps its own authoritative copy: the
	   shared slot is a publication, never a source of truth. Reading
	   back a slot the other processor can see is how a single-writer
	   protocol quietly stops being one. */
	u32 s_writePos = 0;

	bool s_started  = false;
	bool s_paused   = false;
	f32  s_volume   = 1.0f;

	AudioThreadCallback s_threadCb = nullptr;
	AudioDirectCallback s_directCb = nullptr;

	/* Blocks still to emit as silence after a clear - upstream's
	   BUFFERED_SILENT_FRAME_COUNT idea, but sized to OUR buffer depth
	   rather than copied. See bufferedAudioClear() below. */
	s32 s_silentBlocks = 0;

	/* iMuse writes bufferSize + IM_AUDIO_OVERSAMPLE stereo frames, the
	   two extra being the next frame's first samples kept for upstream's
	   interpolation. We do not interpolate, but the callback writes them
	   regardless, so the buffer must hold them or it overruns. */
	const u32 c_extraFrames = 2;
	f32 s_mix[(ZZDF_PCM_BLOCK + c_extraFrames) * ZZDF_PCM_CHANNELS];

	/* Upstream's soundHeadroom, kept so the level matches desktop TFE. */
	const f32 c_soundHeadroom = 0.7f;

	u32 s_usSum = 0, s_usN = 0, s_usMax = 0;

	inline u32 ringUsed(u32 wr, u32 rd)
	{
		return zzdf_pcm_used(wr, rd, (u32)ZZDF_PCM_MASK);
	}

	/* Mix one block and store big-endian s16 samples for direct AHI copy. */
	void mixOneBlock()
	{
		const u32 frames = ZZDF_PCM_BLOCK;
		const u32 total  = (frames + c_extraFrames) * ZZDF_PCM_CHANNELS;
		u32 t0 = zzdf_us_now();

		memset(s_mix, 0, sizeof(f32) * total);

		const bool silent = (s_silentBlocks > 0);

		if (!s_paused)
		{
			if (s_threadCb) { s_threadCb(s_mix, frames, s_volume * c_soundHeadroom); }
			if (s_directCb) { s_directCb(s_mix, frames, s_volume * c_soundHeadroom); }
			 




			TFE_MidiPlayer::synthesizeMidi(s_mix, frames, !silent);
		}

		if (silent) { s_silentBlocks--; }

		u32 pos = s_writePos;
		const f32* src = s_mix;
		for (u32 i = 0; i < frames; i++, src += 2)
		{
			s32 l, r;
			if (silent)
			{
				l = 0; r = 0;
			}
			else
			{
				/* Same limiter as upstream (AUDIO_SIGMOID_TANH), so the
				   port sounds like desktop TFE rather than like a
				   different mix that happens to avoid overflow. */
				l = zzdf_pcm_clamp(TFE_Math::tanhf_series(src[0]));
				r = zzdf_pcm_clamp(TFE_Math::tanhf_series(src[1]));
			}

			/* big-endian s16, L then R - see zzdf_pcm.h for why, and
			   tests/test_audio.c for the proof. */
			pos = zzdf_pcm_put(s_ring, pos, (u32)ZZDF_PCM_MASK, l, r);
		}

		/* The data must be in DDR before the position that advertises it
		   is. NC memory is not cached, but the write buffer still needs
		   draining, and the 68k polls WRITE_POS with no other ordering
		   guarantee. */
		__asm__ volatile("dsb" ::: "memory");
		s_writePos = pos;
		s_sh[SH_PCM_WRITE_POS] = pos;
		s_sh[SH_PCM_BLOCKS]    = s_sh[SH_PCM_BLOCKS] + 1u;

		{
			u32 d = zzdf_us_now() - t0;
			s_usSum += d; s_usN++;
			if (d > s_usMax) { s_usMax = d; s_sh[SH_AUDIO_US_MAX] = d; }
			if (s_usN >= 256u)
			{
				s_sh[SH_AUDIO_US_AVG] = s_usSum / s_usN;
				s_usSum = 0; s_usN = 0;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* TFE_Audio: the eight functions this port actually calls.            */
/* ------------------------------------------------------------------ */
namespace TFE_Audio
{
	void setAudioThreadCallback(AudioThreadCallback callback)
	{
		s_threadCb = callback;
	}

	 








	void setDirectCallback(AudioDirectCallback callback)
	{
		if (callback)
		{
			zzdf_log_puts("[ZZDF] audio: direct callback refused "
			              "(expects 44100 Hz, this sink is 11025)\n");
		}
		s_directCb = nullptr;
	}

	void setVolume(f32 volume)
	{
		if (volume < 0.0f) { volume = 0.0f; }
		if (volume > 1.0f) { volume = 1.0f; }
		s_volume = volume;
	}

	f32 getVolume() { return s_volume; }

	void pause()  { s_paused = true;  }
	void resume() { s_paused = false; }

	/* Single processor, single flow of control: the mixer cannot run
	   while the caller holds this, because the mixer IS the caller a few
	   frames later. Nothing to serialise. */
	void lock()   {}
	void unlock() {}

	 










	void bufferedAudioClear()
	{
		s_silentBlocks = 6;   /* ~139 ms = our own buffer depth + 1 */
	}
}

/* ------------------------------------------------------------------ */
/* The pull driver, called from the game loop.                         */
/* ------------------------------------------------------------------ */
extern "C" void zzdf_audio_init(void)
{
	char line[96];

	s_writePos    = 0;
	s_started     = false;
	s_paused      = false;
	s_silentBlocks = 0;

	/* Start from silence rather than from whatever the previous Core1
	   tenant left in the window: the 68k begins pulling as soon as AHI
	   opens, and a ring full of stale bytes is a burst of noise. */
	{
		volatile u32* w = (volatile u32*)ZZDF_PCM_ARM;
		for (u32 i = 0; i < (u32)ZZDF_PCM_SIZE / 4u; i++) { w[i] = 0; }
	}
	__asm__ volatile("dsb" ::: "memory");

	s_sh[SH_PCM_BASE]      = (u32)ZZDF_PCM_ARM;
	s_sh[SH_PCM_SIZE]      = (u32)ZZDF_PCM_SIZE;
	s_sh[SH_PCM_RATE]      = ZZDF_PCM_RATE;
	s_sh[SH_PCM_CHANNELS]  = ZZDF_PCM_CHANNELS;
	s_sh[SH_PCM_WRITE_POS] = 0;
	s_sh[SH_PCM_BLOCKS]    = 0;
	s_sh[SH_PCM_FULL]      = 0;
	s_sh[SH_AUDIO_US_AVG]  = 0;
	s_sh[SH_AUDIO_US_MAX]  = 0;

	sprintf(line, "[ZZDF] PCM ring %u KiB @ %u Hz stereo s16 BE\n",
	        (unsigned)(ZZDF_PCM_SIZE >> 10), (unsigned)ZZDF_PCM_RATE);
	zzdf_log_puts(line);
}

extern "C" void zzdf_audio_pump(void)
{
	/* The launcher owns SH_AUDIO_ON: if AHI did not open, nothing here
	   runs and the game is exactly the silent build it was before. */
	if (!s_sh[SH_AUDIO_ON]) { return; }
	if (!s_threadCb && !s_directCb) { return; }
	s_started = true;

	 




















	const u32 target = ZZDF_PCM_BLOCK_BYTES * 3u;

	for (u32 guard = 0; guard < 8u; guard++)
	{
		u32 rd   = s_sh[SH_PCM_READ_POS] & 0xFFFFu;
		u32 used = ringUsed(s_writePos, rd);
		if (used >= target)
		{
			if (guard == 0) { s_sh[SH_PCM_FULL] = s_sh[SH_PCM_FULL] + 1u; }
			break;
		}
		/* Cannot happen while target is a small fraction of the ring,
		   but the ring must never actually fill: wr == rd has to mean
		   empty, or the 68k cannot tell full from empty. */
		if ((u32)ZZDF_PCM_MASK - used < ZZDF_PCM_BLOCK_BYTES) { break; }
		mixOneBlock();
	}
}

extern "C" void zzdf_audio_shutdown(void)
{
	s_threadCb = nullptr;
	s_directCb = nullptr;
	s_paused   = true;
}
