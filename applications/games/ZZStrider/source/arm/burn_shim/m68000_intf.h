#ifndef ZZS_M68000_INTF_H
#define ZZS_M68000_INTF_H
/*
 * m68000_intf.h - "Sek" 68000 interface of FB Alpha, re-implemented for ZZStrider on top of the
 * Cyclone 68000 core already validated in ZZRastan (zzs_burn.cpp). Same names and the same
 * semantics as the FB Alpha interface so the CPS-1 files compile unchanged:
 *   - 24-bit bus, 1 KiB pages, three maps (read / write / fetch);
 *   - a map entry is either a host pointer (direct access) or a small handler index;
 *   - 68000 memory is kept as HOST-ORDER 16-bit words, i.e. the byte at 68k address A lives at
 *     host offset A ^ 1. This is also exactly what Cyclone needs to fetch opcodes directly.
 * Original ZZStrider code. ASCII only.
 */
#include <stdint.h>

#define SEK_BITS        (10)
#define SEK_PAGE_COUNT  (1 << (24 - SEK_BITS))
#define SEK_SHIFT       (SEK_BITS)
#define SEK_PAGE_SIZE   (1 << SEK_BITS)
#define SEK_PAGEM       (SEK_PAGE_SIZE - 1)
#define SEK_WADD        (SEK_PAGE_COUNT)
#define SEK_MASK        (SEK_WADD - 1)
#define SEK_MAXHANDLER  (10)

typedef UINT8  (__fastcall *pSekReadByteHandler)(UINT32 a);
typedef void   (__fastcall *pSekWriteByteHandler)(UINT32 a, UINT8 d);
typedef UINT16 (__fastcall *pSekReadWordHandler)(UINT32 a);
typedef void   (__fastcall *pSekWriteWordHandler)(UINT32 a, UINT16 d);
typedef UINT32 (__fastcall *pSekReadLongHandler)(UINT32 a);
typedef void   (__fastcall *pSekWriteLongHandler)(UINT32 a, UINT32 d);
typedef INT32  (__fastcall *pSekResetCallback)();

struct SekExt {
	UINT8 *MemMap[SEK_PAGE_COUNT * 3];              /* read, write, fetch */
	pSekReadByteHandler  ReadByte[SEK_MAXHANDLER];
	pSekWriteByteHandler WriteByte[SEK_MAXHANDLER];
	pSekReadWordHandler  ReadWord[SEK_MAXHANDLER];
	pSekWriteWordHandler WriteWord[SEK_MAXHANDLER];
	pSekReadLongHandler  ReadLong[SEK_MAXHANDLER];
	pSekWriteLongHandler WriteLong[SEK_MAXHANDLER];
	pSekResetCallback    ResetCallback;
};

#define SEK_DEF_READ_WORD(i, a) { UINT16 d; d = (UINT16)(pSekExt->ReadByte[i](a) << 8); d |= (UINT16)(pSekExt->ReadByte[i]((a) + 1)); return d; }
#define SEK_DEF_WRITE_WORD(i, a, d) { pSekExt->WriteByte[i]((a),(UINT8)((d) >> 8)); pSekExt->WriteByte[i]((a) + 1, (UINT8)((d) & 0xff)); }

extern struct SekExt *pSekExt;
extern INT32 nSekCyclesTotal, nSekCyclesToDo;

INT32 SekInit(INT32 nCount, INT32 nCPUType);
INT32 SekExit();
void  SekNewFrame();
void  SekOpen(const INT32 i);
void  SekClose();

#define SEK_IRQSTATUS_NONE (0x0000)
#define SEK_IRQSTATUS_AUTO (0x2000)
#define SEK_IRQSTATUS_ACK  (0x1000)

void  SekSetIRQLine(const INT32 line, const INT32 status);
void  SekReset();
INT32 SekRun(const INT32 nCycles);
INT32 SekRemainingCycles(void);         /* cycles left in the slice being executed, 0 outside */

inline static INT32 SekIdle(INT32 nCycles)
{
	nSekCyclesTotal += nCycles;
	return nCycles;
}

inline static INT32 SekTotalCycles(void)
{
	return nSekCyclesTotal + nSekCyclesToDo - SekRemainingCycles();
}

#define SM_READ  (1)
#define SM_WRITE (2)
#define SM_FETCH (4)
#define SM_ROM   (SM_READ | SM_FETCH)
#define SM_RAM   (SM_READ | SM_WRITE | SM_FETCH)

INT32 SekMapMemory(UINT8 *pMemory, UINT32 nStart, UINT32 nEnd, INT32 nType);
INT32 SekMapHandler(uintptr_t nHandler, UINT32 nStart, UINT32 nEnd, INT32 nType);

INT32 SekSetReadByteHandler(INT32 i, pSekReadByteHandler pHandler);
INT32 SekSetWriteByteHandler(INT32 i, pSekWriteByteHandler pHandler);
INT32 SekSetReadWordHandler(INT32 i, pSekReadWordHandler pHandler);
INT32 SekSetWriteWordHandler(INT32 i, pSekWriteWordHandler pHandler);
INT32 SekSetReadLongHandler(INT32 i, pSekReadLongHandler pHandler);
INT32 SekSetWriteLongHandler(INT32 i, pSekWriteLongHandler pHandler);
INT32 SekSetResetCallback(pSekResetCallback pCallback);

INT32 SekGetPC(INT32 n);
inline static INT32 SekScan(INT32 nAction) { (void)nAction; return 0; }

#endif /* ZZS_M68000_INTF_H */
