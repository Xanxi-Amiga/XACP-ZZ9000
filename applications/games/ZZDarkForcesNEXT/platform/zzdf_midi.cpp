/* MIDI bridge and cooperative TFE MIDI scheduler. */
#include <TFE_System/types.h>
#include <cstring>

#include "zzdf_midi_device.h"

extern "C" {
#include "zzdf_config.h"
	extern unsigned int zzdf_us_now(void);
	extern void zzdf_log_puts(const char *s);
}

/* One pass of upstream's midi thread body. build_next.py turns
   midiUpdateFunc's while-loop into a single pass for us. */
namespace TFE_MidiPlayer { int midiUpdateFunc(void* userData); }

namespace
{
	volatile u32* s_sh   = (volatile u32*)ZZDF_SHARED_ARM;
	volatile u32* s_ring = (volatile u32*)ZZDF_MIDI_ARM;

	u32 s_head    = 0;      /* ours; the slot is a publication */
	u32 s_dropped = 0;
	u32 s_sent    = 0;
	u32 s_rNon = 0, s_rNoff = 0, s_rCC = 0, s_rProg = 0;    
}

/* The one substitution. Everything above it is upstream's. */
void zzdf_midi_ring_put(u8 status, u8 d1, u8 d2)
{
	if (!s_sh[SH_MIDI_ON]) { return; }

	u32 tail = s_sh[SH_MIDI_TAIL] & 0xFFFFu;
	u32 next = (s_head + 1u) % ZZDF_MIDI_LEN;
	if (next == tail)
	{
		/* Full means the launcher stopped draining. Drop rather than
		   block: stalling the engine so the music can keep up would be
		   the wrong trade. Counted, because a dropped Note Off is
		   exactly how a note sticks. */
		s_dropped++;
		s_sh[SH_MIDI_DROPPED] = s_dropped;
		return;
	}
	{
		 

		u32 w   = ((u32)status << 24) | ((u32)d1 << 16) | ((u32)d2 << 8);
		u32 par = w >> 8;
		par ^= par >> 16; par ^= par >> 8; par ^= par >> 4;
		par ^= par >> 2;  par ^= par >> 1;
		w |= (((s_sent >> 10) & 0x7Fu) << 1) | (par & 1u);
		s_ring[s_head] = w;
	}
	switch (status & 0xF0u)
	{
		case 0x80u: s_rNoff++; s_sh[SH_RING_NOFF] = s_rNoff; break;
		case 0x90u: if (d2) { s_rNon++;  s_sh[SH_RING_NON]  = s_rNon;  }
		            else    { s_rNoff++; s_sh[SH_RING_NOFF] = s_rNoff; }
		            break;
		case 0xB0u: s_rCC++;   s_sh[SH_RING_CC]   = s_rCC;   break;
		case 0xC0u: s_rProg++; s_sh[SH_RING_PROG] = s_rProg; break;
		default: break;
	}
	/* The entry must reach DDR before the head that advertises it: the
	   68k reads a u32 as two UWORDs and could otherwise see half of
	   one. */
	__asm__ volatile("dsb" ::: "memory");
	s_head = next;
	s_sh[SH_MIDI_HEAD] = s_head;
	s_sent++;
	/* Against SH_MIDI_SENT, which the 68k writes after camd has taken
	   the message, this is a loss check across the boundary. It is NOT
	   a count of sounding notes - upstream owns note state now. */
	s_sh[SH_MIDI_QUEUED] = s_sent;
}

namespace TFE_Audio
{
	void ZZDFRingMidiDevice::exit() {}

	void ZZDFRingMidiDevice::getOutputName(s32 index, char* buffer, u32 maxLength)
	{
		(void)index;
		if (!buffer || !maxLength) { return; }
		const char* n = "CAMD out.0";
		u32 i = 0;
		while (n[i] && i < maxLength - 1) { buffer[i] = n[i]; i++; }
		buffer[i] = 0;
	}

	void ZZDFRingMidiDevice::message(u8 type, u8 arg1, u8 arg2)
	{
		zzdf_midi_ring_put(type, arg1, arg2);
	}

	/* Upstream passes len = 2 for a program change and 3 otherwise.
	   A CAMD message word carries the length implicitly in its status
	   byte, so the third byte is simply left at zero. */
	void ZZDFRingMidiDevice::message(const u8* msg, u32 len)
	{
		if (!msg || len < 2) { return; }
		zzdf_midi_ring_put(msg[0], msg[1], (len >= 3) ? msg[2] : 0);
	}

	void ZZDFRingMidiDevice::noteAllOff()
	{
		 












		for (u32 c = 0; c < 16u; c++)
		{
			zzdf_midi_ring_put((u8)(0xB0 | c), 64,  0);
			zzdf_midi_ring_put((u8)(0xB0 | c), 123, 0);
			zzdf_midi_ring_put((u8)(0xB0 | c), 120, 0);
			zzdf_midi_ring_put((u8)(0xB0 | c), 121, 0);
		}
	}
}

 

















namespace { u32 s_imuseSteps = 0; }

extern "C" void zzdf_imuse_step(void)
{
	s_imuseSteps++;
	s_sh[SH_IMUSE_TICKS] = s_imuseSteps;
}

 





namespace { u32 s_stepDropped = 0, s_stepBursts = 0; }

extern "C" int zzdf_midi_maxstep(void)
{
	u32 v = s_sh[SH_MIDI_MAXSTEP] & 0xFFu;
	if (v < 1u || v > 32u) { v = (u32)ZZDF_MIDI_MAXSTEP_DEF; }
	return (int)v;
}

 
extern "C" int zzdf_midi_use_sf2(void)
{
	return (s_sh[SH_MIDI_DEVICE] == 1u) ? 1 : 0;
}

extern "C" void zzdf_sf2_loaded(int ok)
{
	s_sh[SH_SF2_STATUS] = ok ? 1u : 2u;
}

extern "C" void zzdf_sf2_voices(int active)
{
	u32 v = (active > 0) ? (u32)active : 0u;
	s_sh[SH_SF2_VOICES_NOW] = v;
	if (v > s_sh[SH_SF2_VOICES_PEAK]) { s_sh[SH_SF2_VOICES_PEAK] = v; }
	if (v) { s_sh[SH_SF2_BLOCKS_BUSY] = s_sh[SH_SF2_BLOCKS_BUSY] + 1u; }
}

 
extern "C" int zzdf_midi_dropdebt(void)
{
	return (s_sh[SH_MIDI_MAXSTEP] & ZZDF_MIDI_DROPDEBT_BIT) ? 1 : 0;
}

 
namespace { u32 s_offSupp = 0, s_offElse = 0, s_offNoData = 0, s_offLogN = 0; }

extern "C" void zzdf_imoff_check(int logical, int note, int phys,
                                 int curHeld, unsigned int allMask,
                                 int sustain)
{
	if (curHeld) { return; }
	s_offSupp++;
	s_sh[SH_IMOFF_SUPP] = s_offSupp;

	u32 cur   = (u32)phys & 0x0Fu;
	u32 other = allMask & ~(1u << cur) & 0xFFFFu;
	u32 oc    = 0x0Fu;
	if (other)
	{
		s_offElse++;
		s_sh[SH_IMOFF_ELSE] = s_offElse;
		for (oc = 0; oc < 16u; oc++) { if (other & (1u << oc)) { break; } }
	}
	if (other && s_offLogN < (u32)ZZDF_IMOFF_LOG)
	{
		static u32 s_t0Off = 0; static int s_t0OffSet = 0;
		u32 nowUs = zzdf_us_now();
		if (!s_t0OffSet) { s_t0Off = nowUs; s_t0OffSet = 1; }
		u32 secs = (nowUs - s_t0Off) / 1000000u;
		if (secs > 4095u) { secs = 4095u; }
		s_sh[SH_IMOFF_LOG0 + s_offLogN] =
			  ((u32)note & 0x7Fu)
			| (cur << 7)
			| ((oc & 0x0Fu) << 11)
			| (((u32)logical & 0x0Fu) << 15)
			| ((sustain ? 1u : 0u) << 19)
			| (secs << 20);
		s_offLogN++;
		s_sh[SH_IMOFF_LOGN] = s_offLogN;
	}
}

extern "C" void zzdf_imoff_nodata(int logical, int note)
{
	(void)logical; (void)note;
	s_offNoData++;
	s_sh[SH_IMOFF_NODATA] = s_offNoData;
}

extern "C" void zzdf_midi_step_dropped(int n)
{
	if (n <= 0) { return; }
	s_stepBursts++;
	s_stepDropped += (u32)n;
	s_sh[SH_MIDI_STEP_BURST] = s_stepBursts;
	s_sh[SH_MIDI_STEP_DROP]  = s_stepDropped;
	if ((u32)n > s_sh[SH_SCHED_DROP_MAX]) { s_sh[SH_SCHED_DROP_MAX] = (u32)n; }
}

 




namespace {
	u32 s_schedExec = 0, s_lateMax = 0, s_lastTickUs = 0;
	u32 s_ivlMax = 0, s_ivlMin = 0xFFFFFFFFu;
	unsigned long long s_lateSum = 0;
	int s_haveTick = 0;
}

extern "C" void zzdf_sched_tick(unsigned int lateUs)
{
	u32 now = zzdf_us_now();
	s_schedExec++;
	s_lateSum += lateUs;
	if (lateUs > s_lateMax) { s_lateMax = lateUs; s_sh[SH_SCHED_LATE_MAX] = s_lateMax; }
	s_sh[SH_SCHED_EXEC]     = s_schedExec;
	s_sh[SH_SCHED_LATE_AVG] = (u32)(s_lateSum / s_schedExec);

	if (s_haveTick)
	{
		u32 ivl = now - s_lastTickUs;
		u32 bin = (ivl <  5000u) ? SH_SCHED_HIST0 :
		          (ivl <  8000u) ? SH_SCHED_HIST1 :
		          (ivl < 15000u) ? SH_SCHED_HIST2 :
		          (ivl < 30000u) ? SH_SCHED_HIST3 : SH_SCHED_HIST4;
		s_sh[bin] = s_sh[bin] + 1u;
		if (ivl > s_ivlMax) { s_ivlMax = ivl; s_sh[SH_SCHED_IVL_MAX] = ivl; }
		if (ivl < s_ivlMin) { s_ivlMin = ivl; s_sh[SH_SCHED_IVL_MIN] = ivl; }
	}
	s_lastTickUs = now;
	s_haveTick = 1;
}

/* A pass that executed more than one callback. At MIDISTEP=1 this is
   impossible by construction and MUST read 0; if it does not, the time
   model is not the one we think we are testing. */
extern "C" void zzdf_sched_multi(void)
{
	s_sh[SH_SCHED_MULTI] = s_sh[SH_SCHED_MULTI] + 1u;
}

 




namespace {

	 







	u32 s_n15Jumps = 0, s_n15Captured = 0, s_n15Released = 0;
	u32 s_n15Active = 0, s_n15Peak = 0, s_n15AllocFail = 0;
	u32 s_n15Purged = 0, s_n15NetSafety = 0, s_n15LogN = 0;
	u32 s_n15OffOrphan = 0, s_n15OnDouble = 0, s_n15InstrLogN = 0;

	 



	inline u32 n15_secs()
	{
		u32 s = zzdf_us_now() / 1000000u;
		return (s > 2047u) ? 2047u : s;
	}
}

 




extern "C" void zzdf_sus_jump(void)
{
	s_n15Jumps++;
	s_sh[SH_SUS_JUMPS] = s_n15Jumps;
}

extern "C" void zzdf_sus_captured(void)
{
	s_n15Captured++;
	s_n15Active++;
	if (s_n15Active > s_n15Peak)
	{
		s_n15Peak = s_n15Active;
		s_sh[SH_SUS_ACTIVE_PEAK] = s_n15Peak;
	}
	s_sh[SH_SUS_CAPTURED] = s_n15Captured;
	s_sh[SH_SUS_ACTIVE]   = s_n15Active;
}

extern "C" void zzdf_sus_released(void)
{
	s_n15Released++;
	if (s_n15Active) { s_n15Active--; }
	s_sh[SH_SUS_RELEASED] = s_n15Released;
	s_sh[SH_SUS_ACTIVE]   = s_n15Active;
}

extern "C" void zzdf_sus_purged(void)
{
	s_n15Purged++;
	if (s_n15Active) { s_n15Active--; }
	s_sh[SH_SUS_PURGED] = s_n15Purged;
	s_sh[SH_SUS_ACTIVE] = s_n15Active;
}

extern "C" void zzdf_sus_net_safety(void)
{
	s_n15NetSafety++;
	s_sh[SH_SUS_NET_SAFETY] = s_n15NetSafety;
}

 




extern "C" void zzdf_sus_alloc_fail(int logChan, int physChan, int note)
{
	s_n15AllocFail++;
	s_sh[SH_SUS_ALLOC_FAIL] = s_n15AllocFail;
	if (s_n15LogN < (u32)ZZDF_SUS_LOG)
	{
		u32 e = ((u32)(note     & 0x7F))
		      | ((u32)(logChan  & 0x0F) << 7)
		      | ((u32)(physChan & 0x0F) << 11)
		      | ((s_n15Active   & 0x3F) << 15)
		      | (n15_secs() << 21);
		s_sh[SH_SUS_LOG0 + s_n15LogN] = e;
		s_n15LogN++;
		s_sh[SH_SUS_LOGN] = s_n15LogN;
	}
}

/* Device-side note-state consistency counters. */
extern "C" void zzdf_instr_off_orphan(int channel, int note)
{
	(void)channel; (void)note;
	s_n15OffOrphan++;
	s_sh[SH_INSTR_OFF_ORPHAN] = s_n15OffOrphan;
}

extern "C" void zzdf_instr_on_double(int channel, int note)
{
	s_n15OnDouble++;
	s_sh[SH_INSTR_ON_DOUBLE] = s_n15OnDouble;
	if (s_n15InstrLogN < (u32)ZZDF_INSTR_LOG)
	{
		u32 e = ((u32)(note    & 0x7F))
		      | ((u32)(channel & 0x0F) << 7)
		      | (n15_secs() << 11);
		s_sh[SH_INSTR_LOG0 + s_n15InstrLogN] = e;
		s_n15InstrLogN++;
		s_sh[SH_INSTR_LOGN] = s_n15InstrLogN;
	}
}

 


















namespace {
	 


	volatile u32* const s_noRing = (volatile u32*)ZZDF_NOTEON_RING_ARM;

	u32 s_noTotal = 0, s_noDrum = 0, s_noMel = 0, s_noOther = 0;
	u32 s_dblDrum = 0, s_dblMel = 0, s_dblOther = 0;
	u32 s_ringW = 0, s_ringN = 0;
	u32 s_frozen = 0, s_tailLeft = 0;
	u32 s_assignGen = 0;
	u32 s_t0 = 0;                
	int s_t0Set = 0;

	 



	int s_pSrc = ZZDF_NOTEON_SRC_OTHER;
	int s_pLog = 0x0F, s_pPhys = 0x0F, s_pShared = 0;
	int s_pMask = 0, s_pMask2 = 0, s_pPlayer = 0xFF;
	int s_pArmed = 0;

	inline u32 no_ms()
	{
		u32 now = zzdf_us_now();
		if (!s_t0Set) { s_t0 = now; s_t0Set = 1; }
		return (now - s_t0) / 1000u;
	}
}

 



extern "C" void zzdf_assign_bump(void)
{
	s_assignGen++;
	s_sh[SH_ASSIGN_GEN] = s_assignGen;
}

 




extern "C" void zzdf_noteon_prepare(int src, int logChan, int physChan,
                                    int shared, int maskBefore,
                                    int mask2Before, int playerId)
{
	s_pSrc = src; s_pLog = logChan; s_pPhys = physChan;
	s_pShared = shared; s_pMask = maskBefore; s_pMask2 = mask2Before;
	s_pPlayer = playerId;
	s_pArmed = 1;
}

 




extern "C" void zzdf_noteon_event(int physChan, int note, int velocity,
                                  int trackerBefore)
{
	const int src      = s_pArmed ? s_pSrc : ZZDF_NOTEON_SRC_OTHER;
	const int logChan  = s_pArmed ? s_pLog : 0x0F;
	const int shared   = s_pArmed ? s_pShared : 0;
	const int mask     = s_pArmed ? s_pMask : 0;
	const int mask2    = s_pArmed ? s_pMask2 : 0;
	const int playerId = s_pArmed ? s_pPlayer : 0xFF;
	const int isDrum   = (src == ZZDF_NOTEON_SRC_DRUMOUT);
	const int isDouble = trackerBefore ? 1 : 0;
	u32 idx;

	s_pArmed = 0;

	s_noTotal++;
	s_sh[SH_NOTEON_TOTAL] = s_noTotal;
	if (src == ZZDF_NOTEON_SRC_DRUMOUT)       { s_noDrum++;  s_sh[SH_NOTEON_DRUM]    = s_noDrum;  }
	else if (src == ZZDF_NOTEON_SRC_IMHANDLE) { s_noMel++;   s_sh[SH_NOTEON_MELODIC] = s_noMel;   }
	else                                      { s_noOther++; s_sh[SH_NOTEON_OTHER]   = s_noOther; }

	if (isDouble)
	{
		if (isDrum)                               { s_dblDrum++;  s_sh[SH_DBL_DRUM]    = s_dblDrum;  }
		else if (src == ZZDF_NOTEON_SRC_IMHANDLE) { s_dblMel++;   s_sh[SH_DBL_MELODIC] = s_dblMel;   }
		else                                      { s_dblOther++; s_sh[SH_DBL_OTHER]   = s_dblOther; }

		 




		if (src == ZZDF_NOTEON_SRC_IMHANDLE &&
		    s_dblMel == 1u && !s_tailLeft && !s_frozen)
		{
			s_tailLeft = (u32)ZZDF_NOTEON_TAIL + 1u;
		}
	}

	if (s_frozen) { return; }

	idx = s_ringW * 4u;
	s_noRing[idx + 0] = no_ms();
	s_noRing[idx + 1] = ((u32)(note     & 0x7F))
	                | ((u32)(velocity & 0x7F) << 7)
	                | ((u32)(logChan  & 0x0F) << 14)
	                | ((u32)(physChan & 0x0F) << 18)
	                | ((u32)(src      & 0x03) << 22)
	                | ((u32)(isDrum        ? 1u : 0u) << 24)
	                | ((u32)(shared        ? 1u : 0u) << 25)
	                | ((u32)(mask          ? 1u : 0u) << 26)
	                | ((u32)(mask2         ? 1u : 0u) << 27)
	                | ((u32)(trackerBefore ? 1u : 0u) << 28)
	                | ((u32)(isDouble      ? 1u : 0u) << 29);
	s_noRing[idx + 2] = s_assignGen;
	s_noRing[idx + 3] = (u32)(playerId & 0xFF);

	s_ringW = (s_ringW + 1u) % (u32)ZZDF_NOTEON_RING;
	if (s_ringN < (u32)ZZDF_NOTEON_RING) { s_ringN++; }
	s_sh[SH_NOTEON_RING_W] = s_ringW;
	s_sh[SH_NOTEON_RING_N] = s_ringN;

	if (s_tailLeft)
	{
		s_tailLeft--;
		if (!s_tailLeft) { s_frozen = 1; s_sh[SH_NOTEON_FROZEN] = 1; }
	}
}

 







extern "C" void zzdf_midi_init(void)
{
	s_imuseSteps = 0;
	s_sh[SH_IMUSE_TICKS] = 0;
	s_head = 0; s_dropped = 0; s_sent = 0;
	s_sh[SH_MIDI_HEAD]     = 0;
	s_sh[SH_MIDI_DROPPED]  = 0;
	s_sh[SH_MIDI_QUEUED]   = 0;

	 

	s_sh[SH_SCHED_EXEC]     = 0;
	s_sh[SH_SCHED_LATE_MAX] = 0;
	s_sh[SH_SCHED_LATE_AVG] = 0;
	s_sh[SH_SCHED_MULTI]    = 0;
	s_sh[SH_SCHED_IVL_MAX]  = 0;
	s_sh[SH_SCHED_IVL_MIN]  = 0;
	s_sh[SH_SCHED_HIST0] = s_sh[SH_SCHED_HIST1] = s_sh[SH_SCHED_HIST2] = 0;
	s_sh[SH_SCHED_HIST3] = s_sh[SH_SCHED_HIST4] = 0;
	s_sh[SH_SCHED_DROP_MAX] = 0;

	 
	s_rNon = s_rNoff = s_rCC = s_rProg = 0;
	s_sh[SH_RING_NON] = s_sh[SH_RING_NOFF] = 0;
	s_sh[SH_RING_CC]  = s_sh[SH_RING_PROG] = 0;
	s_sh[SH_IMOFF_SUPP] = s_sh[SH_IMOFF_ELSE] = 0;
	s_sh[SH_IMOFF_NODATA] = s_sh[SH_IMOFF_LOGN] = 0;
	s_sh[SH_SF2_VOICES_NOW] = s_sh[SH_SF2_VOICES_PEAK] = 0;
	s_sh[SH_SF2_BLOCKS_BUSY] = 0;
	/* SH_SF2_STATUS is NOT cleared here: the device may already have
	   loaded before this runs; the launcher clears it before ARM_RUN. */
}

 







extern "C" void zzdf_midi_tick(void)
{
	static u32 s_lastUs = 0;
	static int s_have   = 0;
	u32 now = zzdf_us_now();

	if (!s_have) { s_lastUs = now; s_have = 1; return; }
	if (now - s_lastUs < 1000u) { return; }   /* 1 ms granularity */
	s_lastUs = now;

	TFE_MidiPlayer::midiUpdateFunc(nullptr);
}

extern "C" void zzdf_midi_shutdown(void)
{
	/* Silence first, while the launcher is still draining. */
	TFE_Audio::ZZDFRingMidiDevice dev;
	dev.noteAllOff();
}
