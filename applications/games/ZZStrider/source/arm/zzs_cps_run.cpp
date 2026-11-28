/*
 * zzs_cps_run.cpp - CPS-1 machine control for ZZStrider: init, reset, one frame, draw, diagnostics.
 * Replaces FB Alpha's cps_run.cpp, ps.cpp and the generic part of d_cps1.cpp.
 *
 * Frame model (FBNeo's current Cps1Frame, which follows MAME): an emulated frame runs from the
 * START OF VBLANK (line 240) to the next one.
 *     frame start : assert IRQ2 (vblank interrupt, auto-acknowledged)
 *     ...           run the 68000 for one frame of cycles, in slices, with the sound board
 *                   brought up to date after every slice and before every sound latch write
 *     frame end   : latch the CPS registers and the sprite list (the hardware and MAME buffer
 *                   the sprites for one frame: the list latched now is drawn at the NEXT vblank)
 * The picture is composed right after the frame end, from that latched state, only when the
 * launcher has authorised a buffer (zzs_video.c) - skipping a render never disturbs the latch.
 *
 * FB Alpha 2012 used 259 scanlines, latched the sprites at line 16 and raised the interrupt in
 * the middle of its frame. Timing here comes from the crystals: 10 MHz, 262 lines, 59.637 Hz.
 *
 * Derived from FB Alpha 2012 / FBNeo cps_run.cpp. ASCII only.
 */
#include "cps.h"
#include "zzs.h"
#include "zzs_game.h"

extern "C" void zzs_sek_publish(void);
extern "C" void zzs_sek_note_bad(int is_write, UINT32 a);
extern "C" void zzs_burn_arena_reset(void);
extern "C" UINT32 zzs_sek_reg_a7(void);
extern UINT32 ZzsProfObjCycles, ZzsProfPalCycles, ZzsStarsDrawn, ZzsLayerOrder, ZzsDrawMask;
extern UINT32 ZzsPalUpdates, ZzsPalNonBlack;
extern UINT32 *ZzsPalARGB;

/* --- globals FB Alpha keeps in cps_run.cpp / ps.cpp ------------------------------------------ */
UINT8 CpsReset = 0;
UINT8 Cpi01A = 0, Cpi01C = 0, Cpi01E = 0;
INT32 CpsDrawSpritesInReverse = 0;
INT32 nIrqLine50, nIrqLine52;
INT32 nCpsNumScanlines = ZZS_VTOTAL;
INT32 Cps1VBlankIRQLine = 2;
UINT8 PsndCode = 0, PsndFade = 0;			/* sound latches read by the Z80 at 0xF008 / 0xF00A */

/* --- ZZStrider state ------------------------------------------------------------------------- */
#define ZZS_SLICES 10							/* as ZZRastan: ~600 sound/pump updates per second */

static INT32  nCpsCyclesExtra;
static UINT64 g_m68k_abs;						/* 68000 cycles emulated before the current frame */
static UINT32 g_prof_m68k_pmu;
static UINT32 g_stat_reg_writes, g_stat_port_reads, g_stat_sndcmd, g_stat_sndcmd_last, g_stat_sndfade;
static UINT32 g_last_pc, g_same_pc_frames;
static int    g_machine_ready;

extern "C" zzs_u32 g_zzs_snd_pmu_nested;	/* PMU cycles spent inside zzs_snd_sync() (zzs_sound.c) */

inline static UINT64 Z80ClockFor(UINT64 m68k_cycles)
{
	return (m68k_cycles * (UINT64)ZZS_Z80_CLOCK) / (UINT64)ZZS_M68K_CLOCK;
}

/* cps_rw.cpp calls this just before it stores a new sound code or fade value: bring the Z80 up
   to the 68000's present time first, exactly as FB Alpha does. The argument (cycles into the
   frame, computed from rounded per-frame counts) is ignored in favour of the absolute clock. */
INT32 PsndSyncZ80(INT32)
{
	if (!(ZZS_SH(ZZS_SH_RUN_FLAGS) & ZZS_RUNF_NO_SOUND) && ZZS_SH(ZZS_SH_SND_ENABLE))
		zzs_snd_sync(Z80ClockFor(g_m68k_abs + (UINT64)(UINT32)SekTotalCycles()));
	return 0;
}

/* --- handler 0 wrappers: keep cps_rw.cpp untouched, add the A1 diagnostics around it --------- */
static UINT8 __fastcall ZzsReadByte(UINT32 a)
{
	if ((a & 0xFF8000) == 0x800000)
		g_stat_port_reads++;
	else
		zzs_sek_note_bad(0, a);
	return CpsReadByte(a);
}

static void __fastcall ZzsWriteByte(UINT32 a, UINT8 d)
{
	if ((a & 0xFF8000) == 0x800000) {
		UINT32 ia = a & 0x1FF;
		if (ia >= 0x100) {
			g_stat_reg_writes++;
			if (ia == 0x181) { g_stat_sndcmd++; g_stat_sndcmd_last = d; }
			if (ia == 0x189) g_stat_sndfade++;
		}
	} else {
		zzs_sek_note_bad(1, a);
	}
	CpsWriteByte(a, d);
}

/* ------------------------------------------------------------------------------------------ */
extern "C" int zzs_machine_init(void)
{
	g_machine_ready = 0;
	zzs_burn_arena_reset();

	if (CpsInit())						/* board data from the decoded ROM store */
		return 1;

	SekInit(0, 0x68000);
	if (CpsMemInit())					/* video RAM, work RAM, registers; 68000 memory map */
		return 1;
	/* same handlers as CpsMemInit installed, with the diagnostics wrapped around the bytes */
	SekSetReadByteHandler(0, ZzsReadByte);
	SekSetWriteByteHandler(0, ZzsWriteByte);

	CpsRwInit();
	if (CpsPalInit())
		return 1;
	if (CpsObjInit())
		return 1;
	if (zzs_snd_init())					/* Z80 + YM2151 + MSM6295 */
		return 1;

	DrawFnInit();
	Cps1VBlankIRQLine = 2;

	g_stat_reg_writes = g_stat_port_reads = 0;
	g_stat_sndcmd = g_stat_sndcmd_last = g_stat_sndfade = 0;
	g_machine_ready = 1;
	return 0;
}

extern "C" int zzs_machine_reset(void)
{
	if (!g_machine_ready)
		return 1;

	SekOpen(0);
	SekReset();
	SekClose();
	zzs_snd_reset_z80();

	nCpsCyclesExtra = 0;
	g_m68k_abs = 0;
	nCurrentFrame = 0;
	PsndCode = 0; PsndFade = 0;
	g_prof_m68k_pmu = 0;
	g_last_pc = 0xFFFFFFFFu;
	g_same_pc_frames = 0;
	ZZS_SH(ZZS_SH_PCTRACE_COUNT) = 0;

	/* The reset vectors published by zzs_rom_commit() must be what the CPU really loaded. */
	if ((UINT32)SekGetPC(0) != ZZS_SH(ZZS_SH_RESET_PC) || zzs_sek_reg_a7() != ZZS_SH(ZZS_SH_RESET_SSP))
		return 1;
	return 0;
}

static void LatchInputs(void)
{
	UINT32 p1 = ZZS_SH(ZZS_SH_INPUT_P1), p2 = ZZS_SH(ZZS_SH_INPUT_P2), sys = ZZS_SH(ZZS_SH_INPUT_SYS);
	INT32 i;

	/* FB Alpha keeps one byte per switch and packs them in CpsRwGetInp(); the ports are read
	   inverted (active low), so the shared words are active HIGH. */
	for (i = 0; i < 8; i++) {
		CpsInp001[i] = (UINT8)((p1 >> i) & 1);		/* 0x800001: player 1 */
		CpsInp000[i] = (UINT8)((p2 >> i) & 1);		/* 0x800000: player 2 */
		CpsInp018[i] = (UINT8)((sys >> i) & 1);		/* 0x800018: coins, start, service */
	}
	/* DIP switches: the shared slots hold the RAW port values; CpsReadPort returns ~Cpi01x. */
	Cpi01A = (UINT8)~ZZS_SH(ZZS_SH_DIP_A);
	Cpi01C = (UINT8)~ZZS_SH(ZZS_SH_DIP_B);
	Cpi01E = (UINT8)~ZZS_SH(ZZS_SH_DIP_C);

	CpsRwGetInp();
}

extern "C" void zzs_machine_frame(void)
{
	INT32 i, nNext;
	int sound_on = !(ZZS_SH(ZZS_SH_RUN_FLAGS) & ZZS_RUNF_NO_SOUND) && ZZS_SH(ZZS_SH_SND_ENABLE);

	LatchInputs();

	SekOpen(0);
	SekNewFrame();
	SekIdle(nCpsCyclesExtra);			/* cycles the previous frame ran past its end */

	SekSetIRQLine(Cps1VBlankIRQLine, SEK_IRQSTATUS_AUTO);	/* vblank: the frame starts at line 240 */

	for (i = 0; i < ZZS_SLICES; i++) {
		UINT32 t0, nested0;

		nNext = (INT32)(((INT64)(i + 1) * nCpsCycles) / ZZS_SLICES);

		t0 = zzs_pmu_cycles();
		nested0 = g_zzs_snd_pmu_nested;
		SekRun(nNext - SekTotalCycles());
		/* time inside CycloneRun minus the sound board time spent in latch-write syncs */
		g_prof_m68k_pmu += (zzs_pmu_cycles() - t0) - (g_zzs_snd_pmu_nested - nested0);

		if (sound_on)
			zzs_snd_sync(Z80ClockFor(g_m68k_abs + (UINT64)(UINT32)SekTotalCycles()));
		zzs_audio_pump();
	}

	/* Next vblank: latch the registers and the sprite list. */
	memcpy(CpsSaveReg[0], CpsReg, 0x100);
	CpsObjGet();

	nCpsCyclesExtra = SekTotalCycles() - nCpsCycles;
	g_m68k_abs += (UINT64)(UINT32)nCpsCycles;
	SekClose();

	nCurrentFrame++;

	/* A1: PC at the end of each of the first frames, and a "stuck" detector */
	{
		UINT32 pc = (UINT32)SekGetPC(0);
		UINT32 n = ZZS_SH(ZZS_SH_PCTRACE_COUNT);
		if (n < ZZS_PCTRACE_MAX) {
			ZZS_SH(ZZS_SH_PCTRACE0 + n) = pc;
			ZZS_SH(ZZS_SH_PCTRACE_COUNT) = n + 1;
		}
		if (pc == g_last_pc) g_same_pc_frames++; else g_same_pc_frames = 0;
		g_last_pc = pc;
	}
}

extern "C" void zzs_machine_draw(zzs_u16 *index_fb)
{
	pBurnDraw = (UINT8 *)index_fb;
	nBurnPitch = ZZS_SRC_W * 2;
	nBurnBpp = 2;
	CpsDraw();
}

extern "C" const zzs_u32 *zzs_machine_palette(void)
{
	return ZzsPalARGB;
}

extern "C" zzs_u32 zzs_machine_take_obj_cycles(void) { UINT32 v = ZzsProfObjCycles; ZzsProfObjCycles = 0; return v; }
extern "C" zzs_u32 zzs_machine_take_pal_cycles(void) { UINT32 v = ZzsProfPalCycles; ZzsProfPalCycles = 0; return v; }
extern "C" zzs_u32 zzs_machine_take_m68k_cycles_pmu(void) { UINT32 v = g_prof_m68k_pmu; g_prof_m68k_pmu = 0; return v; }

inline static UINT32 Reg16(const UINT8 *base, INT32 off)
{
	return (UINT32)BURN_ENDIAN_SWAP_INT16(*((const UINT16 *)(base + off)));
}

/* Sprites in the list the game is building (same end and blank rules as CpsObjGet). */
static UINT32 CountObjects(void)
{
	INT32 nOff = BURN_ENDIAN_SWAP_INT16(*((UINT16 *)(CpsReg + 0x00))) << 8;
	UINT8 *Get;
	UINT32 n = 0;
	INT32 i;

	nOff &= 0xfff800;
	Get = CpsFindGfxRam(nOff, 0x800);
	if (!Get)
		return 0xFFFFFFFFu;				/* the object base register points outside gfx RAM */
	for (i = 0; i < 0x100; i++, Get += 8) {
		UINT16 *ps = (UINT16 *)Get;
		if (BURN_ENDIAN_SWAP_INT16(ps[3]) >= 0xff00)
			break;
		if ((BURN_ENDIAN_SWAP_INT16(ps[0]) | BURN_ENDIAN_SWAP_INT16(ps[3])) == 0)
			continue;
		n++;
	}
	return n;
}

extern "C" void zzs_machine_publish(void)
{
	const UINT8 *r;

	zzs_sek_publish();
	ZZS_SH(ZZS_SH_M68K_CYCLES) = (UINT32)(g_m68k_abs & 0xFFFFFFFFu);
	ZZS_SH(ZZS_SH_SAME_PC_FRAMES) = g_same_pc_frames;

	if (!g_machine_ready)
		return;

	r = CpsSaveReg[0];
	ZZS_SH(ZZS_SH_CPSA_OBJ)       = Reg16(r, 0x00);
	ZZS_SH(ZZS_SH_CPSA_SCR1)      = Reg16(r, 0x02);
	ZZS_SH(ZZS_SH_CPSA_SCR2)      = Reg16(r, 0x04);
	ZZS_SH(ZZS_SH_CPSA_SCR3)      = Reg16(r, 0x06);
	ZZS_SH(ZZS_SH_CPSA_OTHER)     = Reg16(r, 0x08);
	ZZS_SH(ZZS_SH_CPSA_PAL)       = Reg16(r, 0x0A);
	ZZS_SH(ZZS_SH_CPSA_SCR1_XY)   = (Reg16(r, 0x0C) << 16) | Reg16(r, 0x0E);
	ZZS_SH(ZZS_SH_CPSA_SCR2_XY)   = (Reg16(r, 0x10) << 16) | Reg16(r, 0x12);
	ZZS_SH(ZZS_SH_CPSA_SCR3_XY)   = (Reg16(r, 0x14) << 16) | Reg16(r, 0x16);
	ZZS_SH(ZZS_SH_CPSA_STAR1_XY)  = (Reg16(r, 0x18) << 16) | Reg16(r, 0x1A);
	ZZS_SH(ZZS_SH_CPSA_STAR2_XY)  = (Reg16(r, 0x1C) << 16) | Reg16(r, 0x1E);
	ZZS_SH(ZZS_SH_CPSA_VIDEOCTRL) = (Reg16(r, 0x20) << 16) | Reg16(r, 0x22);
	ZZS_SH(ZZS_SH_CPSB_LAYERCTRL) = Reg16(r, nCpsLcReg);
	ZZS_SH(ZZS_SH_CPSB_PRIO01)    = (Reg16(r, MaskAddr[1]) << 16) | Reg16(r, MaskAddr[0]);
	ZZS_SH(ZZS_SH_CPSB_PRIO23)    = (Reg16(r, MaskAddr[3]) << 16) | Reg16(r, MaskAddr[2]);
	ZZS_SH(ZZS_SH_CPSB_PALCTRL)   = (UINT32)r[nCpsPalCtrlReg];
	ZZS_SH(ZZS_SH_CPS_REG_WRITES) = g_stat_reg_writes;
	ZZS_SH(ZZS_SH_PAL_UPDATES)    = ZzsPalUpdates;
	ZZS_SH(ZZS_SH_PAL_NONBLACK)   = ZzsPalNonBlack;
	ZZS_SH(ZZS_SH_SNDCMD_WRITES)  = g_stat_sndcmd;
	ZZS_SH(ZZS_SH_SNDCMD_LAST)    = g_stat_sndcmd_last;
	ZZS_SH(ZZS_SH_SNDFADE_WRITES) = g_stat_sndfade;
	ZZS_SH(ZZS_SH_PORT_READS)     = g_stat_port_reads;
	ZZS_SH(ZZS_SH_OBJ_COUNT)      = CountObjects();
	ZZS_SH(ZZS_SH_STARS_DRAWN)    = ZzsStarsDrawn;
	ZZS_SH(ZZS_SH_LAYER_ORDER)    = ZzsLayerOrder;
	ZZS_SH(ZZS_SH_DRAW_MASK)      = ZzsDrawMask;
}
