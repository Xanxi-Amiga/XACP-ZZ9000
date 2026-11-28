/*
 * zzs_burn.cpp - the small part of the FB Alpha "burn" framework that the CPS-1 engine needs,
 * implemented on the ZZRastan Core1 infrastructure:
 *
 *   - the "Sek" 68000 bus (paged memory map + handlers) on top of the Cyclone 68000 core,
 *     with the same configuration and generated Cyclone.s as ZZRastan 1.0;
 *   - BurnMalloc as a bump allocator in the private arena;
 *   - the drawing target globals (the INDEX16 frame).
 *
 * Original ZZStrider code. The Sek interface semantics follow FB Alpha's m68000_intf.cpp so the
 * CPS-1 files compile and behave unchanged. The Cyclone glue (checkpc, IRQ acknowledge, host
 * pointer = membase + pc) follows zzrastan_cpu.c.
 *
 * ASCII only.
 */
#include "burnint.h"
#include "m68000_intf.h"
#include "z80_intf.h"
#include "zzs.h"
extern "C" {
#include "Cyclone.h"
}

/* ------------------------------------------------------------------------------------------
 * burn globals
 * ------------------------------------------------------------------------------------------ */
UINT8  *pBurnDraw = NULL;
INT32   nBurnPitch = ZZS_SRC_W * 2;
INT32   nBurnBpp = 2;
UINT8   nBurnLayer = 0xFF;
UINT32  nCurrentFrame = 0;
INT32   nMaxPlayers = 2;

static UINT32 g_arena_ptr = ZZS_ARM_ARENA;

UINT8 *BurnMalloc(INT32 size)
{
	UINT32 n, p;
	if (size <= 0)
		return NULL;
	n = ((UINT32)size + 63u) & ~63u;                    /* 64-byte granularity: 2 cache lines */
	p = g_arena_ptr;
	if (n > ZZS_ARM_ARENA_END - p)
		return NULL;
	g_arena_ptr = p + n;
	memset((void *)(uintptr_t)p, 0, n);
	ZZS_SH(ZZS_SH_ARENA_USED) = g_arena_ptr - ZZS_ARM_ARENA;
	return (UINT8 *)(uintptr_t)p;
}

void _BurnFree(void *ptr)
{
	(void)ptr;                                          /* the arena lives as long as the blob */
}

extern "C" void zzs_burn_arena_reset(void)
{
	g_arena_ptr = ZZS_ARM_ARENA;
	ZZS_SH(ZZS_SH_ARENA_USED) = 0u;
}

/* ------------------------------------------------------------------------------------------
 * Sek on Cyclone
 * ------------------------------------------------------------------------------------------ */
static struct SekExt g_sek_ext;
struct SekExt *pSekExt = &g_sek_ext;
INT32 nSekCyclesTotal = 0, nSekCyclesToDo = 0;

static struct Cyclone g_cyc;
static int g_cyc_running;                               /* inside CycloneRun */

/* A page of "BRA.S *" (0x60FE): where the 68000 is parked if it ever jumps into unmapped
   memory, so that Cyclone can never fetch outside a buffer we own. */
static UINT16 g_trap_page[SEK_PAGE_SIZE / 2];

static UINT32 g_stat_bad_reads, g_stat_bad_writes, g_stat_last_bad;
static UINT32 g_stat_illegal, g_stat_exec_bad, g_stat_exec_ram, g_stat_last_exec;
static UINT32 g_stat_reset_instr, g_stat_irq_acks;
static UINT32 g_irq_count[8];

#define FIND_R(a) (g_sek_ext.MemMap[(a) >> SEK_SHIFT])
#define FIND_W(a) (g_sek_ext.MemMap[((a) >> SEK_SHIFT) + SEK_WADD])
#define FIND_F(a) (g_sek_ext.MemMap[((a) >> SEK_SHIFT) + SEK_WADD * 2])

static UINT8 __fastcall DefReadByte(UINT32) { return 0; }
static void  __fastcall DefWriteByte(UINT32, UINT8) { }

inline static UINT16 HandlerReadWord(uintptr_t h, UINT32 a)
{
	if (g_sek_ext.ReadWord[h])
		return g_sek_ext.ReadWord[h](a);
	return (UINT16)((g_sek_ext.ReadByte[h](a) << 8) | g_sek_ext.ReadByte[h](a + 1));
}

inline static void HandlerWriteWord(uintptr_t h, UINT32 a, UINT16 d)
{
	if (g_sek_ext.WriteWord[h]) {
		g_sek_ext.WriteWord[h](a, d);
		return;
	}
	g_sek_ext.WriteByte[h](a, (UINT8)(d >> 8));
	g_sek_ext.WriteByte[h](a + 1, (UINT8)d);
}

/* --- handlers given to Cyclone. The address is NOT masked by the core (config). --- */
static unsigned int cyc_read8(unsigned int a)
{
	UINT8 *pr;
	a &= 0xFFFFFF;
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER)
		return pr[(a ^ 1) & SEK_PAGEM];
	return g_sek_ext.ReadByte[(uintptr_t)pr](a);
}

static unsigned int cyc_read16(unsigned int a)
{
	UINT8 *pr;
	a &= 0xFFFFFE;
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER)
		return *((UINT16 *)(pr + (a & SEK_PAGEM)));
	return HandlerReadWord((uintptr_t)pr, a);
}

static unsigned int cyc_read32(unsigned int a)
{
	UINT8 *pr;
	a &= 0xFFFFFE;
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER && (a & SEK_PAGEM) <= SEK_PAGE_SIZE - 4) {
		UINT16 *pw = (UINT16 *)(pr + (a & SEK_PAGEM));
		return ((UINT32)pw[0] << 16) | pw[1];
	}
	/* handler, or a long that straddles two pages */
	return (cyc_read16(a) << 16) | cyc_read16(a + 2);
}

static void cyc_write8(unsigned int a, unsigned char d)
{
	UINT8 *pr;
	a &= 0xFFFFFF;
	pr = FIND_W(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		pr[(a ^ 1) & SEK_PAGEM] = d;
		return;
	}
	g_sek_ext.WriteByte[(uintptr_t)pr](a, d);
}

static void cyc_write16(unsigned int a, unsigned short d)
{
	UINT8 *pr;
	a &= 0xFFFFFE;
	pr = FIND_W(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		*((UINT16 *)(pr + (a & SEK_PAGEM))) = d;
		return;
	}
	HandlerWriteWord((uintptr_t)pr, a, d);
}

static void cyc_write32(unsigned int a, unsigned int d)
{
	UINT8 *pr;
	a &= 0xFFFFFE;
	pr = FIND_W(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER && (a & SEK_PAGEM) <= SEK_PAGE_SIZE - 4) {
		UINT16 *pw = (UINT16 *)(pr + (a & SEK_PAGEM));
		pw[0] = (UINT16)(d >> 16);
		pw[1] = (UINT16)d;
		return;
	}
	cyc_write16(a, (unsigned short)(d >> 16));
	cyc_write16(a + 2, (unsigned short)d);
}

/* Cyclone keeps pc as a HOST pointer (membase + 68k pc) and fetches opcodes through it.
   Called on jumps: pick the host block that backs the new pc. */
static unsigned int cyc_checkpc(unsigned int pc)
{
	UINT32 a = (pc - (UINT32)g_cyc.membase) & 0xFFFFFF;
	UINT8 *pf = FIND_F(a);

	if ((uintptr_t)pf < SEK_MAXHANDLER) {
		/* Nothing executable there. Report it and park the CPU on the trap page. */
		g_stat_exec_bad++;
		g_stat_last_exec = a;
		zzs_fatal(ZZS_ERR_EXEC_UNMAPPED, a);
		g_cyc.membase = (int)((UINT32)(uintptr_t)g_trap_page - (a & ~(UINT32)SEK_PAGEM));
		return (UINT32)g_cyc.membase + (a & ~1u);
	}
	if (a >= 0x900000) {                                /* gfx RAM or work RAM: legal, noted */
		g_stat_exec_ram++;
		g_stat_last_exec = a;
	}
	g_cyc.membase = (int)((UINT32)(uintptr_t)pf - (a & ~(UINT32)SEK_PAGEM));
	return (UINT32)g_cyc.membase + a;
}

static int cyc_irq_ack(int level)
{
	(void)level;
	g_cyc.irq = 0;                                      /* SEK_IRQSTATUS_AUTO: cleared when taken */
	g_stat_irq_acks++;
	return CYCLONE_INT_ACK_AUTOVECTOR;
}

static void cyc_reset_callback(void)
{
	g_stat_reset_instr++;
	if (g_sek_ext.ResetCallback)
		g_sek_ext.ResetCallback();
}

static int cyc_unrecognized_callback(void)
{
	g_stat_illegal++;
	return 0;                                           /* take the 68000 exception, as hardware */
}

INT32 SekInit(INT32 nCount, INT32 nCPUType)
{
	INT32 i;
	(void)nCount; (void)nCPUType;

	memset(&g_sek_ext, 0, sizeof(g_sek_ext));
	for (i = 0; i < SEK_MAXHANDLER; i++) {
		g_sek_ext.ReadByte[i] = DefReadByte;
		g_sek_ext.WriteByte[i] = DefWriteByte;
	}
	for (i = 0; i < SEK_PAGE_SIZE / 2; i++)
		g_trap_page[i] = 0x60FE;

	CycloneInit();
	memset(&g_cyc, 0, sizeof(g_cyc));
	g_cyc.checkpc = cyc_checkpc;
	g_cyc.read8 = cyc_read8;
	g_cyc.read16 = cyc_read16;
	g_cyc.read32 = cyc_read32;
	g_cyc.write8 = cyc_write8;
	g_cyc.write16 = cyc_write16;
	g_cyc.write32 = cyc_write32;
	g_cyc.fetch8 = cyc_read8;
	g_cyc.fetch16 = cyc_read16;
	g_cyc.fetch32 = cyc_read32;
	g_cyc.IrqCallback = cyc_irq_ack;
	g_cyc.ResetCallback = cyc_reset_callback;
	g_cyc.UnrecognizedCallback = cyc_unrecognized_callback;

	nSekCyclesTotal = 0;
	nSekCyclesToDo = 0;
	g_cyc_running = 0;
	g_stat_bad_reads = g_stat_bad_writes = g_stat_last_bad = 0;
	g_stat_illegal = g_stat_exec_bad = g_stat_exec_ram = g_stat_last_exec = 0;
	g_stat_reset_instr = g_stat_irq_acks = 0;
	memset(g_irq_count, 0, sizeof(g_irq_count));
	return 0;
}

INT32 SekExit() { return 0; }
void  SekOpen(const INT32) { }
void  SekClose() { }

void SekNewFrame()
{
	nSekCyclesTotal = 0;
}

INT32 SekRemainingCycles(void)
{
	return g_cyc_running ? g_cyc.cycles : 0;
}

void SekReset()
{
	CycloneReset(&g_cyc);
}

void SekSetIRQLine(const INT32 line, const INT32 status)
{
	if (status == SEK_IRQSTATUS_NONE) {
		g_cyc.irq = 0;
		return;
	}
	g_cyc.irq = (unsigned char)(line & 7);
	g_irq_count[line & 7]++;
	/* Taken at the top of the next CycloneRun() if the interrupt mask allows it, otherwise
	   it stays pending until the 68000 lowers its mask (Cyclone re-checks on SR changes). */
}

INT32 SekRun(const INT32 nCycles)
{
	INT32 done;
	if (nCycles <= 0)
		return 0;
	nSekCyclesToDo = nCycles;
	g_cyc.cycles = nCycles;
	g_cyc_running = 1;
	CycloneRun(&g_cyc);
	g_cyc_running = 0;
	done = nCycles - g_cyc.cycles;
	nSekCyclesTotal += done;
	nSekCyclesToDo = 0;
	return done;
}

INT32 SekMapMemory(UINT8 *pMemory, UINT32 nStart, UINT32 nEnd, INT32 nType)
{
	UINT8 *Ptr = pMemory - nStart;
	UINT32 i;

	for (i = (nStart & ~(UINT32)SEK_PAGEM); i <= nEnd; i += SEK_PAGE_SIZE) {
		UINT8 *page = Ptr + i;                          /* host address of this page */
		if (nType & SM_READ)
			g_sek_ext.MemMap[i >> SEK_SHIFT] = page;
		if (nType & SM_WRITE)
			g_sek_ext.MemMap[(i >> SEK_SHIFT) + SEK_WADD] = page;
		if (nType & SM_FETCH)
			g_sek_ext.MemMap[(i >> SEK_SHIFT) + SEK_WADD * 2] = page;
	}
	return 0;
}

INT32 SekMapHandler(uintptr_t nHandler, UINT32 nStart, UINT32 nEnd, INT32 nType)
{
	UINT32 i;
	for (i = (nStart & ~(UINT32)SEK_PAGEM); i <= nEnd; i += SEK_PAGE_SIZE) {
		if (nType & SM_READ)
			g_sek_ext.MemMap[i >> SEK_SHIFT] = (UINT8 *)nHandler;
		if (nType & SM_WRITE)
			g_sek_ext.MemMap[(i >> SEK_SHIFT) + SEK_WADD] = (UINT8 *)nHandler;
		if (nType & SM_FETCH)
			g_sek_ext.MemMap[(i >> SEK_SHIFT) + SEK_WADD * 2] = (UINT8 *)nHandler;
	}
	return 0;
}

INT32 SekSetReadByteHandler(INT32 i, pSekReadByteHandler p)   { if (i < 0 || i >= SEK_MAXHANDLER) return 1; g_sek_ext.ReadByte[i]  = p ? p : DefReadByte;  return 0; }
INT32 SekSetWriteByteHandler(INT32 i, pSekWriteByteHandler p) { if (i < 0 || i >= SEK_MAXHANDLER) return 1; g_sek_ext.WriteByte[i] = p ? p : DefWriteByte; return 0; }
INT32 SekSetReadWordHandler(INT32 i, pSekReadWordHandler p)   { if (i < 0 || i >= SEK_MAXHANDLER) return 1; g_sek_ext.ReadWord[i]  = p; return 0; }
INT32 SekSetWriteWordHandler(INT32 i, pSekWriteWordHandler p) { if (i < 0 || i >= SEK_MAXHANDLER) return 1; g_sek_ext.WriteWord[i] = p; return 0; }
INT32 SekSetReadLongHandler(INT32 i, pSekReadLongHandler p)   { if (i < 0 || i >= SEK_MAXHANDLER) return 1; g_sek_ext.ReadLong[i]  = p; return 0; }
INT32 SekSetWriteLongHandler(INT32 i, pSekWriteLongHandler p) { if (i < 0 || i >= SEK_MAXHANDLER) return 1; g_sek_ext.WriteLong[i] = p; return 0; }
INT32 SekSetResetCallback(pSekResetCallback p) { g_sek_ext.ResetCallback = p; return 0; }

INT32 SekGetPC(INT32)
{
	return (INT32)(g_cyc.pc - (UINT32)g_cyc.membase);
}

/* --- diagnostics for zzs_cps_run.cpp -------------------------------------------------------- */
extern "C" void zzs_sek_publish(void)
{
	ZZS_SH(ZZS_SH_M68K_PC) = g_cyc.pc - (UINT32)g_cyc.membase;
	ZZS_SH(ZZS_SH_M68K_SP) = g_cyc.a[7];
	ZZS_SH(ZZS_SH_M68K_SR) = CycloneGetSr(&g_cyc);
	ZZS_SH(ZZS_SH_CPU_STATE) = (UINT32)g_cyc.state_flags;
	ZZS_SH(ZZS_SH_IRQ2_COUNT) = g_irq_count[2];
	ZZS_SH(ZZS_SH_IRQ2_ACKS) = g_stat_irq_acks;
	ZZS_SH(ZZS_SH_ILLEGAL_COUNT) = g_stat_illegal;
	ZZS_SH(ZZS_SH_BAD_READS) = g_stat_bad_reads;
	ZZS_SH(ZZS_SH_BAD_WRITES) = g_stat_bad_writes;
	ZZS_SH(ZZS_SH_LAST_BAD_ADDR) = g_stat_last_bad;
	ZZS_SH(ZZS_SH_EXEC_BAD_COUNT) = g_stat_exec_bad;
	ZZS_SH(ZZS_SH_EXEC_RAM_COUNT) = g_stat_exec_ram;
	ZZS_SH(ZZS_SH_LAST_EXEC_ADDR) = g_stat_last_exec;
	ZZS_SH(ZZS_SH_RESET_INSTR) = g_stat_reset_instr;
}

extern "C" void zzs_sek_note_bad(int is_write, UINT32 a)
{
	if (is_write) g_stat_bad_writes++; else g_stat_bad_reads++;
	g_stat_last_bad = a;
}

extern "C" UINT32 zzs_sek_reg_a7(void) { return g_cyc.a[7]; }

/* ------------------------------------------------------------------------------------------
 * Z80 reset line (68000 RESET instruction) and the QSound / save-state entries that
 * cps_mem.cpp names but that Strider never reaches.
 * ------------------------------------------------------------------------------------------ */
void ZetReset()
{
	zzs_snd_reset_z80();
}

void  QsndSyncZ80() { }
INT32 QsndScan(INT32) { return 0; }
INT32 PsndScan(INT32) { return 0; }
