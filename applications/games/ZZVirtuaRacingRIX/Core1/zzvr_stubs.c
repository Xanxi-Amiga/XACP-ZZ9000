/*
 * zzvr_stubs.c - no-op stubs for the PicoDrive subsystems excluded from the
 * ZZVirtuaRacingRIX build: MegaCD, SVP, Sega Pico, cart-hw mappers, SMS FM
 * (OPLL), zlib.  From ZZPicoDriveMD 1.1 zzpico_stubs.c with EVERY 32X stub
 * removed: the 32X (two SH-2, VDP, PWM, DMA) is now compiled in
 * (pico/32x/, cpu/sh2/).  Mega Drive and 32X cartridges only.
 * ASCII only.
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
/* referenced by the 32X code paths (MCD+32X combinations) and memory.c of this tree */
uptr s68k_read8_map[0x1000000 >> M68K_MEM_SHIFT];
int  pcd_sync_s68k(unsigned int m68k_target, int m68k_poll_sync){ (void)m68k_target;(void)m68k_poll_sync; return 0; }
void pcd_prepare_frame(void) {}
void pcd_run_cpus(int m68k_cycles){ (void)m68k_cycles; }
u32  PicoRead8_mcd_io(u32 a){ (void)a; return 0; }
u32  PicoRead16_mcd_io(u32 a){ (void)a; return 0; }
void PicoWrite8_mcd_io(u32 a, u32 d){ (void)a;(void)d; }
void PicoWrite16_mcd_io(u32 a, u32 d){ (void)a;(void)d; }

/* --- SVP --- */
void PicoSVPInit(void) {}
void PicoSVPStartup(void) {}

/* --- Sega Pico --- */
PICO_INTERNAL int  PicoPicoIrqAck(int level){ (void)level; return 0; }
PICO_INTERNAL void PicoInitPico(void) {}
PICO_INTERNAL void PicoMemSetupPico(void) {}
PICO_INTERNAL void PicoReratePico(void) {}

/* --- save state hook --- */

/* --- cart hardware mappers (special carts; not supported) ---
   SSF2 mapper state: the 32X memory code installs its banked-ROM handlers only
   when carthw_ssf2_active is set (bank_switch_rom_sh2); inactive, the SH-2s read
   the ROM directly (MAP_MEMORY(Pico.rom)).  Identity banks kept for safety. */
int carthw_ssf2_active = 0;
unsigned char carthw_ssf2_banks[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
void carthw_ssf2_write8(u32 a, u32 d){ (void)a;(void)d; }
void carthw_ssf2_write16(u32 a, u32 d){ (void)a;(void)d; }
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

/* MegaCD frame + PCM + mp3/ogg (exclus) */
PICO_INTERNAL void PicoFrameMCD(void) {}
void pcd_pcm_update(s32 *buffer, int length, int stereo){ (void)buffer;(void)length;(void)stereo; }
void mp3_update(s32 *buffer, int length, int stereo){ (void)buffer;(void)length;(void)stereo; }
void ogg_update(s32 *buffer, int length, int stereo){ (void)buffer;(void)length;(void)stereo; }
/* VGM logging (pico/sound/vgm.c, irixxxx 2.x): not compiled, no-ops */
void vgm_reset(void) {}
void vgm_frame(void) {}

/* Sega Pico PCM (exclu) */
PICO_INTERNAL void PicoPicoPCMUpdate(short *buffer, int length, int stereo)
{ (void)buffer;(void)length;(void)stereo; }

/* SMS draw (exclu, NO_SMS) */
void PicoDoHighPal555SMS(void) {}
void PicoDrawSetOutputSMS(pdso_t which){ (void)which; }

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
/* --- MegaCD/SMS/Pico : chemin save/load state.c, jamais exerce --- */
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
