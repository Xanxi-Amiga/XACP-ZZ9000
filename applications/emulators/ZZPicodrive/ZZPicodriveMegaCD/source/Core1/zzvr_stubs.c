/*
 * zzvr_stubs.c - optional PicoDrive interfaces not needed by
 * ZZPicoDriveMegaCD. Mega-CD and 32X emulation are linked from their
 * full PicoDrive implementations, not implemented as stubs.
 */
#include "pico_int.h"
#include "memory.h"
#include "emu2413/emu2413.h"

/* --- MegaCD: compiled in (pico/cd/, Mega-CD edition) --- */

/* --- SVP: compiled in (pico/carthw/svp/, VR edition) --- */

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

/* crc32: real zlib (pico/cd/libchdr/deps/zlib-1.3.1) */

/* ================= Phase 2 : symboles tires par PicoFrame ================= */
#include "cd/megasd.h"
#include "zlib.h"

/* MEGASD state (reference par sound.c) */

/* frontend callback : emu_video_mode_change est defini dans zzpico_core1.c
   (record-only) depuis la Phase 3a. */

/* MegaCD frame + PCM + mp3/ogg (exclus) */
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
/* Sega Pico: referenced by the state.c load path (never a Pico state) */
picohw_state PicoPicohw;
void PicoPicoPCMLoad(void *b, int l){ (void)b;(void)l; }
int  PicoPicoPCMSave(void *b, int l){ (void)b;(void)l; return 0; }
/* SMS FM state (NO_SMS build, referenced by state.c) */
size_t ym2413_pack_state(void *b, size_t n){ (void)b;(void)n; return 0; }
void ym2413_unpack_state(const void *b, size_t n){ (void)b;(void)n; }
/* CD audio tracks in MP3 / OGG: not supported (CUE/BIN, ISO + WAV, CHD) */
void mp3_start_play(void *f, int pos){ (void)f;(void)pos; }
void ogg_start_play(void *f, int off){ (void)f;(void)off; }
/* SMS timing (NO_SMS build), referenced by PicoLoopPrepare */
void PicoPrepareMS(void){}
/* zip ROMs: not supported (the zip loader of cart.c is reachable through pm_open) */
#include "unzip/unzip.h"
ZIP *openzip(const char *p){ (void)p; return 0; }
struct zipent *readzip(ZIP *z){ (void)z; return 0; }
int seekcompresszip(ZIP *z, struct zipent *e){ (void)z;(void)e; return -1; }
void closezip(ZIP *z){ (void)z; }
void ogg_stop_play(void){}
int  mp3_get_bitrate(void *f, int size){ (void)f;(void)size; return -1; }
int  ogg_get_length(void *f){ (void)f; return -1; }
