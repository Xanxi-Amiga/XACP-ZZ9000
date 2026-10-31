/*
 * zzpico_stubs.c - no-op stubs for PicoDrive subsystems excluded from the
 * minimal Mega Drive v0 build: MegaCD, 32X, SVP, Sega Pico, cart-hw mappers,
 * SMS FM (OPLL), save-state hooks. Referenced by reachable code but never
 * exercised by a plain MD ROM in v0. Lets the blob link cleanly.
 * NOTE: special-mapper carts / MCD / 32X / SMS-FM are non-functional by design
 * in v0 - standard Mega Drive ROMs only. ASCII only.
 */
#include "pico_int.h"
#include "memory.h"
#include "emu2413/emu2413.h"

/* --- MegaCD --- */
#ifdef EMU_C68K
struct Cyclone PicoCpuCS68k;        /* sous-CPU Cyclone (MCD stube) */
#else
M68K_CONTEXT PicoCpuFS68k;          /* sous-CPU FAME (MCD stube) */
#endif
uptr s68k_read16_map[0x1000000 >> M68K_MEM_SHIFT];
mcd_state *Pico_mcd = 0;
PICO_INTERNAL void PicoInitMCD(void) {}
PICO_INTERNAL void PicoPowerMCD(void) {}
PICO_INTERNAL int  PicoResetMCD(void) { return 0; }
PICO_INTERNAL void PicoMemSetupCD(void) {}
void DmaSlowCell(u32 source, u32 a, int len, unsigned char inc)
{ (void)source;(void)a;(void)len;(void)inc; }

/* --- 32X ---
   Avec _ASM_MEMORY_C, memory.c fournit lui-meme ces stubs (bloc NO_32X+ASM)
   pour resoudre les refs de memory_arm.S -> ne pas les redefinir ici. */
#ifndef _ASM_MEMORY_C
u32  PicoRead8_32x(u32 a){ (void)a; return 0; }
u32  PicoRead16_32x(u32 a){ (void)a; return 0; }
void PicoWrite8_32x(u32 a, u32 d){ (void)a;(void)d; }
void PicoWrite16_32x(u32 a, u32 d){ (void)a;(void)d; }
#endif
int (*PicoScan32xBegin)(unsigned int num);
int (*PicoScan32xEnd)(unsigned int num);
void PicoDrawSetOutFormat32x(pdso_t which, int m){ (void)which;(void)m; }

/* --- SVP --- */
void PicoSVPInit(void) {}
void PicoSVPStartup(void) {}

/* --- Sega Pico --- */
PICO_INTERNAL int  PicoPicoIrqAck(int level){ (void)level; return 0; }
PICO_INTERNAL void PicoInitPico(void) {}
PICO_INTERNAL void PicoMemSetupPico(void) {}
PICO_INTERNAL void PicoReratePico(void) {}

/* --- save state hook --- */

/* --- cart hardware mappers (special carts; not supported in v0) --- */
void carthw_Xin1_startup(void){}
void carthw_flash_startup(void){}
void carthw_jcart_startup(void){}
void carthw_lk3_startup(void){}
void carthw_pier_startup(void){}
void carthw_radica_startup(void){}
void carthw_realtec_startup(void){}
void carthw_sf001_startup(void){}
void carthw_sf002_startup(void){}
void carthw_sf004_startup(void){}
void carthw_smw64_startup(void){}
void carthw_sprot_startup(void){}
void carthw_ssf2_startup(void){}
void carthw_sprot_new_location(unsigned int a, unsigned int mask, unsigned short val, int is_ro)
{ (void)a;(void)mask;(void)val;(void)is_ro; }

/* --- SMS FM (OPLL / emu2413), not used for MD --- */
OPLL *OPLL_new(unsigned int clk, unsigned int rate){ (void)clk;(void)rate; return 0; }
void  OPLL_delete(OPLL *o){ (void)o; }
void  OPLL_reset(OPLL *o){ (void)o; }
void  OPLL_setChipType(OPLL *o, int t){ (void)o;(void)t; }
void  OPLL_setRate(OPLL *o, unsigned int r){ (void)o;(void)r; }
int   OPLL_calc(OPLL *o){ (void)o; return 0; }
void  OPLL_writeReg(OPLL *o, unsigned int r, unsigned int v){ (void)o;(void)r;(void)v; }
void  OPLL_writeIO(OPLL *o, unsigned int a, unsigned int v){ (void)o;(void)a;(void)v; }

/* --- globals + zlib crc32 referenced by reachable code --- */
OPLL *opll = 0;
u32 pcd_base_address = 0;

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len)
{
    static unsigned long tab[256];
    static int init = 0;
    unsigned int i, j; unsigned long c;
    if(!init){
        for(i=0;i<256;i++){ c=i;
            for(j=0;j<8;j++) c = (c&1) ? (0xEDB88320UL ^ (c>>1)) : (c>>1);
            tab[i]=c; }
        init=1;
    }
    crc = crc ^ 0xFFFFFFFFUL;
    for(i=0;i<len;i++) crc = tab[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

/* ================= Phase 2 : symboles tires par PicoFrame ================= */
#include "cd/megasd.h"
#include "zlib.h"

/* MEGASD state (reference par sound.c) */
struct megasd Pico_msd;

/* frontend callback : emu_video_mode_change est defini dans zzpico_core1.c
   (record-only) depuis la Phase 3a. */

/* MegaCD frame + PCM + mp3 (exclus) */
PICO_INTERNAL void PicoFrameMCD(void) {}
void pcd_pcm_update(s32 *buffer, int length, int stereo){ (void)buffer;(void)length;(void)stereo; }
void mp3_update(s32 *buffer, int length, int stereo){ (void)buffer;(void)length;(void)stereo; }

/* Sega Pico PCM (exclu) */
PICO_INTERNAL void PicoPicoPCMUpdate(short *buffer, int length, int stereo)
{ (void)buffer;(void)length;(void)stereo; }

/* SMS draw (exclu, NO_SMS) */

/* zlib inflate : chemin loader zip/cso jamais exerce (ROM via PicoCartInsert) */
int inflateInit2(z_stream *s, int w){ (void)s;(void)w; return -1; }
int inflate(z_stream *s, int f){ (void)s;(void)f; return -1; }
int inflateEnd(z_stream *s){ (void)s; return 0; }
/* MegaCD / Sega Pico : references par le chemin de load de state.c (Genesis only,
   ces etats ne sont jamais charges). Stubs pour satisfaire le link. */
unsigned int pcd_event_times[PCD_EVENT_COUNT];
picohw_state PicoPicohw;
int cdc_context_load_old(unsigned char *state){ (void)state; return 0; }
int cdd_context_load_old(unsigned char *state){ (void)state; return 0; }
/* --- MegaCD/32X/SMS/Pico : chemin save/load state.c, jamais exerce (Genesis only) --- */
void Pico32xShutdown(void){}
void Pico32xStartup(void){}
void PicoPicoPCMLoad(void *b, int l){ (void)b;(void)l; }
int  PicoPicoPCMSave(void *b, int l){ (void)b;(void)l; return 0; }
unsigned int SekCycleAimS68k=0, SekCycleCntS68k=0;
int cdc_context_load(unsigned char *st){ (void)st; return 0; }
int cdc_context_save(unsigned char *st){ (void)st; return 0; }
int cdd_context_load(unsigned char *st){ (void)st; return 0; }
int cdd_context_save(unsigned char *st){ (void)st; return 0; }
int gfx_context_load(const unsigned char *st){ (void)st; return 0; }
int gfx_context_save(unsigned char *st){ (void)st; return 0; }
void pcd_state_loaded(void){}
void wram_1M_to_2M(unsigned char *m){ (void)m; }
void wram_2M_to_1M(unsigned char *m){ (void)m; }
size_t ym2413_pack_state(void *b, size_t n){ (void)b;(void)n; return 0; }
void ym2413_unpack_state(const void *b, size_t n){ (void)b;(void)n; }

/* PicoDrawSetOutBuf (generique) reference la variante 32X (exclue) */
void PicoDrawSetOutBuf32X(void *dest, int increment){ (void)dest;(void)increment; }

/* --- SMS FM wrapper (ym2413.c non compile ; FM off au MVP) --- */
void YM2413_regWrite(unsigned d){ (void)d; }
void YM2413_dataWrite(unsigned d){ (void)d; }
