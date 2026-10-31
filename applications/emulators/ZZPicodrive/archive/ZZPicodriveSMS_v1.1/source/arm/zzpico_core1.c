/*
 * zzpico_core1.c - ZZPicoDrive Core1 entry (v0: command loop only)
 *
 * Memory authority: XX19 / XACP v1.5 (zzpico_shared.h). Core1 model authority:
 * ZZDoom (C entry .text.core1_entry, SP from firmware, STOP=0xFF+wfe, no return).
 *
 * v0: INIT -> verify ROM (D-cache invalidate + CRC32 byte-exact) ->
 *     PicoInit/PicoCartInsert/PicoPower/PicoReset. NO PicoFrame/render/audio.
 * Big .bss -> ARM private pool 0x22000000 (zzpico.ld). ASCII only.
 */
#include <stddef.h>
#include "zzpico_shared.h"
#include <pico/pico_types.h>
#include <pico/pico.h>
#include <pico/pico_int.h>
#include <pico/sound/ym2612.h>
#include "zzpico_font8x8.h"

static volatile u32 *shared = (volatile u32 *)ZZPICO_ARM_SHARED;

extern void mmu_init(void);
extern void mmu_set_wbwa(u32 addr, u32 len);            /* zzpico_mmu.c (3d) */
extern u32  mmu_get_desc(u32 addr);                    /* zzpico_mmu.c (diag) */
extern void dcache_inval_range(void *addr, u32 len);   /* zzpico_mmu.c */
extern void dcache_clean_range(void *addr, u32 len);   /* zzpico_mmu.c */
extern void zz_install_vectors(void);                  /* zzpico_vectors.S */

#define DIAG(v) do{ shared[ZZPICO_SH_DIAG]=(v); __asm__ volatile("dsb":::"memory"); }while(0)

/* ---- compteur de cycles PMU (Cortex-A9 CCNT) : decoupe du rendu ----
 * PMCR.E(bit0)=enable, .C(bit2)=reset cycle ; PMCNTENSET.bit31=enable CCNT.
 * CCNT compte chaque cycle (DIVIDER non arme) ; lecture via PMCCNTR. */
static inline void pmu_enable(void){
    __asm__ volatile("mcr p15,0,%0,c9,c12,0"::"r"(1u|(1u<<2)));
    __asm__ volatile("mcr p15,0,%0,c9,c12,1"::"r"(0x80000000u));
}
static inline u32 pmu_cyc(void){
    u32 v; __asm__ volatile("mrc p15,0,%0,c9,c13,0":"=r"(v)); return v;
}

/* ---- FAST_CONVERT_LUT : conversion RGB565->ARGB via LUT ----
 * LUT 65536 : RGB565 -> pixel P96 fmt5 valide. Le blob ecrit 0xFFRRGGBB ;
 * en ARM little-endian cela donne les octets DDR [B][G][R][A] = fmt5 valide.
 * Extraction 565 reelle (PicoDrive compile sans USE_BGR*) : R=15..11,
 * G=10..5 (6 bits), B=4..0. Construite une fois (build_lut565). */
static u32 g_lut565[65536];
static int g_lut_ready=0;
#ifdef ZZPICO_AUDIO_PROFILE
/* AUDIO_PROFILE_1 : buffer sortie audio (s16 stereo). 44100/50=882 samples/frame
   x2 voies = 1764 s16 ; 4096 = large marge. Samples generes puis JETES. */
static short g_sndbuf[4096];
#endif
#ifdef ZZPICO_AUDIO_REAL
/* AUDIO_REAL_1 : sortie PCM reelle vers ring DDR partage (NC), draine par le 68k/AHI.
   Ring = u32 par paire stereo : mot = (L<<16)|R ECRIT BRUT (le 68k applique le meme
   swap que rd32 -> obtient (L<<16)|R natif -> stocke big-endian = AHI S16S correct).
   pcm_write ARM-owned, pcm_read 68k-owned. Overrun si le ring se remplit. */
static short g_sndbuf[4096];
#define PCM_RING_ARM   ZZPICO_ARM_AUDIO        /* 0x05700000, NC */
#define PCM_RING_SIZE  (512u*1024u)            /* octets (=131072 paires) */
static u32 g_pcm_wpos=0;                        /* octet offset, multiple de 4 */
static u32 g_pcm_overrun=0;
/* AUDIO_TEST_PATTERN : sinus 32 pts, ~440 Hz @44100 (inc 16.16 = 20922).
   L = tone, R = 0 -> valide a l'oreille : tone propre a GAUCHE = L/R + endian OK ;
   bruit = endian faux ; tone a DROITE = L/R inverses. */
static const short g_sine32[32]={
    0,3121,6122,8886,11314,13314,14813,15760,16000,15760,14813,13314,
    11314,8886,6122,3121,0,-3121,-6122,-8886,-11314,-13314,-14813,-15760,
    -16000,-15760,-14813,-13314,-11314,-8886,-6122,-3121};
static u32 g_tone_ph=0;
static void zzpico_pcm_write(int len_bytes){
    volatile u32 *ring=(volatile u32*)PCM_RING_ARM;
    u32 wpos=g_pcm_wpos;
    u32 rpos=shared[ZZPICO_SH_PCM_READ_POS];    /* 68k-owned : lu brut = correct */
    int testpat=(int)shared[ZZPICO_SH_PCM_TESTPAT];
    int pairs=len_bytes>>2;                      /* stereo s16 -> 4 octets/paire */
    const short *s=g_sndbuf;
    int i;
    for(i=0;i<pairs;i++){
        u32 next=wpos+4u; if(next>=PCM_RING_SIZE) next=0u;
        if(next==rpos){ g_pcm_overrun++; break; }    /* ring plein : on jette le reste */
        if(testpat){
            short L=g_sine32[(g_tone_ph>>16)&31];
            g_tone_ph+=20922u;
            ring[wpos>>2]=((u32)(u16)L<<16)|(u16)0;   /* L=tone, R=silence */
        } else {
            ring[wpos>>2]=((u32)(u16)s[2*i]<<16)|(u16)s[2*i+1];   /* BRUT (L<<16)|R */
        }
        wpos=next;
    }
    g_pcm_wpos=wpos;
    shared[ZZPICO_SH_PCM_WRITE_POS]=wpos;        /* brut : 68k lit via rd32 */
    shared[ZZPICO_SH_PCM_OVERRUNS]=g_pcm_overrun;
}
#endif
static void build_lut565(void){
    u32 px;
    for(px=0;px<65536u;px++){
        u32 r=(px>>11)&0x1F, g=(px>>5)&0x3F, b=px&0x1F;
        r=(r<<3)|(r>>2); g=(g<<2)|(g>>4); b=(b<<3)|(b>>2);
        g_lut565[px]=0xFF000000u|(r<<16)|(g<<8)|b;
    }
    g_lut_ready=1;
}

static u32 zzpico_crc32(const u8 *p, u32 n)
{
    u32 c=0xFFFFFFFFu,i,k;
    for(i=0;i<n;i++){ c^=p[i];
        for(k=0;k<8;k++) c=(c>>1)^(0xEDB88320u&((c&1u)?0xFFFFFFFFu:0u)); }
    return c^0xFFFFFFFFu;
}

static u32 g_rom_addr=0, g_rom_size=0;
static int g_pico_inited=0, g_rom_ok=0;
static int g_running=0;        /* 3a: accepte les requetes de frame */
static u32 g_frames=0;
static u32 g_last_req=0;       /* derniere FRAME_REQ traitee (handshake 1 frame) */
static u32 g_frame_seq=0;      /* sequence FRAME_READY (modele autonome ZZDoom)  */
static u32 g_frame_t0=0;       /* CCNT au debut de frame (auto-cadence 50/60Hz)  */

/* ---- Phase 3a : rendu RGB555 dans un buffer ARM fixe + CRC ---- */
#define FB3A_BASE   ((u8*)ZZPICO_ARM_VIDEO)   /* 0x05600000, WB/WA */
#define FB3A_W      320u
#define FB3A_H      240u
#define FB3A_PITCH  640u                       /* octets/ligne (320*2) */
#define FB3A_SIZE   (FB3A_PITCH*FB3A_H)        /* 153600 */
/* Phase 3b : 2e buffer ARGB8888 (meme section MMU 1MB que RGB16, WB/WA) */
#define FB3B_ARGB_BASE  ((u8*)0x05640000UL)        /* ARM 0x05640000 */
#define FB3B_ARGB_SIZE  (FB3A_W*FB3A_H*4u)         /* 307200 */

/* Boucle chaude de conversion isolee, compilee -O3.
 * uint16 src RGB565 -> uint32 dst via LUT, pitch P96. Ecrit DIRECTEMENT dans
 * le backbuffer P96. Aucun diagnostic, aucun calcul par pixel : un acces LUT. */
__attribute__((optimize("O3")))
static void convert_lut_p96(const u16 *src, u32 *dst, u32 stride_px,
                            u32 loffs, u32 lines, u32 coffs, u32 cols){
    u32 yy,xx;
    const u32 black=g_lut565[0];   /* 0xFF000000 noir opaque */
    /* Rectangle actif publie par PicoDrive via emu_video_mode_change :
       [loffs..loffs+lines) en Y, [coffs..coffs+cols) en X. Tout le reste force
       en noir. MD 320 : coffs=0/cols=320 (pas de bordure laterale). SMS 256 :
       coffs=32/cols=256 -> 32px noirs a gauche/droite. SMS 192 : loffs=24/
       lines=192 -> 24px noirs haut/bas. Centrage + fond noir propres. */
    for(yy=0;yy<FB3A_H;yy++){
        u32 *drow=dst + yy*stride_px;
        if(yy<loffs || yy>=loffs+lines){
            for(xx=0;xx<FB3A_W;xx++) drow[xx]=black;
        } else {
            const u16 *srow=src + yy*(FB3A_PITCH/2);
            for(xx=0;xx<FB3A_W;xx++){
                if(xx<coffs || xx>=coffs+cols) drow[xx]=black;
                else                           drow[xx]=g_lut565[srow[xx]];
            }
        }
    }
}
#define ZZPICO_CONV_RGB555 0
#define ZZPICO_CONV_BGR555 1
#define ZZPICO_CONV_RGB565 2
#define ZZPICO_CONV_BGR565 3
#define ZZPICO_CONV_DEFAULT ZZPICO_CONV_RGB555     /* 3b : on commence en RGB555 */

static int g_vm_sl=0,g_vm_lc=0,g_vm_sc=0,g_vm_cc=0;  /* rectangle actif */
static u32 g_mode_seq=0;
static u32 g_acc_raw=0xFFFFFFFFu;              /* etat CRC accumule (pre-xor) */
static u32 g_last_pc=0xFFFFFFFFu, g_pc_changes=0;   /* DIAG : suivi PC 68k */
static u32 g_rom_swapped=0;   /* garde-fou anti double-byteswap */

/* CRC32 zlib (poly 0xEDB88320) table-driven (perf : ~150 KB/frame) */
static u32 g_ctab[256]; static int g_ctab_done=0;

/* === DIAG p3a_diag_poll : trace bus 68k (VDP/Z80/I/O) ===
   Compteurs prives WB/WA, snapshot vers shared + ring DDR a FRAME_DONE. */
static u32 bt_vdp_reads,bt_vdp_last,bt_vdp_vb,bt_vdp_hb,bt_vdp_pc;
static u32 bt_breq_reads,bt_breq_writes,bt_breq_lastw,bt_breq_lastr,bt_breq_pc;
static u32 bt_zrst_reads,bt_zrst_writes,bt_zrst_lastw,bt_zrst_lastr,bt_zrst_pc;
static u32 bt_io_reads,bt_io_writes,bt_io_lastaddr,bt_io_lastr,bt_io_lastw,bt_io_pc;
static u32 bt_ver_reads,bt_pad1_reads,bt_pad2_reads;
#define BT_RING_N 64
static u32 bt_ring[BT_RING_N*6];   /* frame,pc,cat,rw,addr,val */
static u32 bt_ring_head=0, bt_frame_now=0;

void zzpico_bus_trace(int cat, int rw, unsigned addr, unsigned val)
{
#ifdef EMU_C68K
    u32 pc=0;   /* Cyclone : PC non capture ici (bus trace = diag, hors mode perf) */
#else
    u32 pc=fm68k_get_pc(&PicoCpuFM68k);
#endif
    if(cat==1){ if(rw==0){ bt_vdp_reads++; bt_vdp_last=val; bt_vdp_pc=pc;
                 if(val & SR_VB) bt_vdp_vb++; if(val & SR_HB) bt_vdp_hb++; } }
    else if(cat==2){ bt_breq_pc=pc;
                 if(rw==0){ bt_breq_reads++; bt_breq_lastr=val; } else { bt_breq_writes++; bt_breq_lastw=val; } }
    else if(cat==3){ bt_zrst_pc=pc;
                 if(rw==0){ bt_zrst_reads++; bt_zrst_lastr=val; } else { bt_zrst_writes++; bt_zrst_lastw=val; } }
    else if(cat==4){ bt_io_pc=pc; bt_io_lastaddr=addr;
                 if(rw==0){ bt_io_reads++; bt_io_lastr=val;
                     { unsigned sub=(addr>>1)&0xf;
                       if(sub==0) bt_ver_reads++; else if(sub==1) bt_pad1_reads++; else if(sub==2) bt_pad2_reads++; } }
                 else { bt_io_writes++; bt_io_lastw=val; } }
    { u32 h=(bt_ring_head%BT_RING_N)*6;
      bt_ring[h+0]=bt_frame_now; bt_ring[h+1]=pc; bt_ring[h+2]=(u32)cat;
      bt_ring[h+3]=(u32)rw; bt_ring[h+4]=addr; bt_ring[h+5]=val; bt_ring_head++; }
}

/* === DIAG first-fault recorder : capture la PREMIERE exception 68k
   (etat AVANT le push), puis se fige. Hooke a l'entree de execute_exception. === */
static int g_fault_done=0;
void zzpico_first_fault_hook(void *ctxv, int vect, unsigned oldpc, unsigned oldsr)
{
#ifndef EMU_C68K
    M68K_CONTEXT *ctx=(M68K_CONTEXT*)ctxv; int i;
    if(g_fault_done) return;
    /* Ne capturer que les VRAIES fautes 68k (bus/addr/illegal/divzero/chk/trapv/
       priv/trace/lineA/lineF = vecteurs 2..11). Ignorer interruptions (autovecteurs
       24..31, ex. VINT=30) et traps. */
    if(vect<2 || vect>11) return;
    g_fault_done=1;
    shared[ZZPICO_SH_FAULT_FRAME]=g_frames;
    shared[ZZPICO_SH_FAULT_VECT]=(u32)vect;
    shared[ZZPICO_SH_FAULT_OLDPC]=oldpc;
    shared[ZZPICO_SH_FAULT_OLDSR]=oldsr;
    shared[ZZPICO_SH_FAULT_ASP]=ctx->asp;
    for(i=0;i<8;i++) shared[ZZPICO_SH_FAULT_D0+i]=ctx->dreg[i].D;
    for(i=0;i<8;i++) shared[ZZPICO_SH_FAULT_A0+i]=ctx->areg[i].D;
    shared[ZZPICO_SH_FAULT_CAPTURED]=1;   /* publie en dernier */
#else
    (void)ctxv;(void)vect;(void)oldpc;(void)oldsr;  /* Cyclone : famec non compile */
#endif
}
static void zzpico_snap(u32 spc,u32 ssr,u32 sa7,u32 sasp)
{
#ifdef EMU_C68K
    shared[spc]=PicoCpuCM68k.pc - (u32)PicoCpuCM68k.membase;
    shared[ssr]=CycloneGetSr(&PicoCpuCM68k);
    shared[sa7]=PicoCpuCM68k.a[7];
    shared[sasp]=PicoCpuCM68k.osp;
#else
    shared[spc]=fm68k_get_pc(&PicoCpuFM68k);
    shared[ssr]=PicoCpuFM68k.sr;
    shared[sa7]=PicoCpuFM68k.areg[7].D;
    shared[sasp]=PicoCpuFM68k.asp;
#endif
}
static void crc_tab_init(void){
    u32 i,j,c;
    for(i=0;i<256;i++){ c=i;
        for(j=0;j<8;j++) c=(c&1u)?(0xEDB88320u^(c>>1)):(c>>1);
        g_ctab[i]=c; }
    g_ctab_done=1;
}
static u32 crc_full(const u8 *p,u32 n){
    u32 c=0xFFFFFFFFu,i;
    for(i=0;i<n;i++) c=g_ctab[(c^p[i])&0xFF]^(c>>8);
    return c^0xFFFFFFFFu;
}
/* CRC d'un rectangle (stride=pitch, bpp octets/pixel), clampe a 320x240 */
static u32 crc_rect_bpp(const u8 *base,u32 pitch,int x0,int w,int y0,int h,int bpp){
    u32 c=0xFFFFFFFFu; int x,y;
    if(x0<0)x0=0; if(y0<0)y0=0;
    if(w<=0||h<=0) return 0;
    if((u32)(x0+w)>FB3A_W) w=(int)FB3A_W-x0;
    if((u32)(y0+h)>FB3A_H) h=(int)FB3A_H-y0;
    if(w<=0||h<=0) return 0;
    for(y=0;y<h;y++){ const u8 *row=base+(u32)(y0+y)*pitch+(u32)(x0*bpp);
        for(x=0;x<w*bpp;x++) c=g_ctab[(c^row[x])&0xFF]^(c>>8); }
    return c^0xFFFFFFFFu;
}
static u32 crc_rect(const u8 *base,u32 pitch,int x0,int w,int y0,int h){
    return crc_rect_bpp(base,pitch,x0,w,y0,h,2);
}

/* Callback frontend : enregistre le rectangle actif (record-only). */
void emu_video_mode_change(int start_line,int line_count,int start_col,int col_count){
    g_vm_sl=start_line; g_vm_lc=line_count; g_vm_sc=start_col; g_vm_cc=col_count;
    shared[ZZPICO_SH_VM_START_LINE]=(u32)start_line;
    shared[ZZPICO_SH_VM_LINE_COUNT]=(u32)line_count;
    shared[ZZPICO_SH_VM_START_COL]=(u32)start_col;
    shared[ZZPICO_SH_VM_COL_COUNT]=(u32)col_count;
    shared[ZZPICO_SH_VM_MODE_SEQ]=++g_mode_seq;
}

static void cmd_init(void)
{
    if(!g_pico_inited){
        PicoInit();
        /* 3a : emulation FM/PSG/Z80 ACTIVE (logique du jeu : Sonic et bien
           d'autres attendent le timer YM2612 au boot ; FM gate les ecritures/
           lectures YM dans memory.c). MAIS aucune sortie audio : sndOut=NULL et
           sndRate=0 -> chips emules, ni ring ni AHI. FM emulation != audio. */
#ifdef ZZPICO_Z80_OFF
        /* MESURE : Z80 desactive pour chiffrer son cout dans PicoFrame
           et connaitre le plafond. FM/PSG restent emules (logique jeu), muets. */
        PicoIn.opt=POPT_EN_FM|POPT_EN_PSG;
#else
        PicoIn.opt=POPT_EN_PSG|POPT_EN_Z80;   /* SMS: PSG + Z80, pas de FM YM2612 */
#endif
#ifdef ZZPICO_AUDIO_PROFILE
        /* AUDIO_PROFILE_1 : reactive la synthese audio pour MESURER son cout.
           Stereo 44100 Hz. sndOut vers buffer local -> samples generes puis JETES
           (aucun envoi vers un chemin de sortie ZZ9000). PsndRerate(0) se fait via
           PicoReset->PsndReset (Pico.m.pal connu). Compare PicoFrame son ON vs le
           baseline son OFF (6.5 ms) -> cout audio pur/frame. */
        PicoIn.opt|=POPT_EN_STEREO;
        PicoIn.sndRate=44100; PicoIn.sndOut=g_sndbuf;
#elif defined(ZZPICO_AUDIO_REAL)
        /* AUDIO_REAL (Paula MVP) : synthese stereo 22050 + sortie REELLE. PicoDrive
           appelle PicoIn.writeSound(bytes) apres chaque frame (samples dans sndOut)
           -> on les pousse dans le ring DDR (S16, byte-order prouve). Le 68k draine,
           convertit S16->S8 + desentrelace, et joue via audio.device/Paula (DMA). */
        PicoIn.opt|=POPT_EN_STEREO;
        PicoIn.sndRate=22050; PicoIn.sndOut=g_sndbuf;
        PicoIn.writeSound=zzpico_pcm_write;
        g_pcm_wpos=0; g_pcm_overrun=0;
        shared[ZZPICO_SH_PCM_BASE]=PCM_RING_ARM;   /* adresse ARM ; 68k convertit */
        shared[ZZPICO_SH_PCM_SIZE]=PCM_RING_SIZE;
        shared[ZZPICO_SH_PCM_RATE]=22050u;
        shared[ZZPICO_SH_PCM_FORMAT]=2u;           /* 2 = stereo s16 dans le ring     */
        shared[ZZPICO_SH_PCM_WRITE_POS]=0u;
        shared[ZZPICO_SH_PCM_OVERRUNS]=0u;
        shared[ZZPICO_SH_PCM_ENABLED]=1u;
#else
        PicoIn.sndRate=0; PicoIn.sndOut=NULL;
#endif
        g_pico_inited=1;
    }
    DIAG(0xA101UL);
}

static int cmd_load_rom(void)
{
    u32 crc_arm,crc_68k; const u8 *rom;
    g_rom_addr=shared[ZZPICO_SH_ROM_ADDR];
    g_rom_size=shared[ZZPICO_SH_ROM_SIZE];
    g_rom_ok=0;
    if(g_rom_addr==0){ shared[ZZPICO_SH_ERROR]=ZZPICO_ERR_ROM_NULL; return 0; }
    if(g_rom_size==0||g_rom_size>ZZPICO_ROM_MAX){ shared[ZZPICO_SH_ERROR]=ZZPICO_ERR_ROM_TOO_BIG; return 0; }
    if(g_rom_size & 1u){ shared[ZZPICO_SH_ERROR]=ZZPICO_ERR_ROM_ODD; return 0; }
    rom=(const u8*)g_rom_addr;
    shared[ZZPICO_SH_ROM_STATE]=0;   /* RAW_BE par defaut */

    /* ROM is WB/WA and was written by the 68k over the bus -> invalidate stale
       ARM D-cache lines before CRC and PicoCartInsert (XX19 lock #7). */
    dcache_inval_range((void*)rom, g_rom_size + ZZPICO_ROM_PAD);

    shared[ZZPICO_SH_ROM_FIRST4]=((u32)rom[0]<<24)|((u32)rom[1]<<16)|((u32)rom[2]<<8)|rom[3];
    shared[ZZPICO_SH_ROM_LAST4]=((u32)rom[g_rom_size-4]<<24)|((u32)rom[g_rom_size-3]<<16)|
                                ((u32)rom[g_rom_size-2]<<8)|rom[g_rom_size-1];
    crc_arm=zzpico_crc32(rom,g_rom_size);
    shared[ZZPICO_SH_ROM_CRC_ARM]=crc_arm;
    crc_68k=shared[ZZPICO_SH_ROM_CRC_68K];
    if(crc_68k!=0 && crc_68k!=crc_arm){ shared[ZZPICO_SH_ERROR]=ZZPICO_ERR_ROM_CRC; return 0; }
    /* SMS : la ROM .sms est chargee TELLE QUELLE (Z80 little-endian, adressee a
       l'octet). Pas de Byteswap (contrairement au chemin MD/FAME). PicoCartLoad
       amont ne swappe pas non plus pour is_sms. ROM_STATE reste RAW. */
    g_rom_swapped=0; shared[ZZPICO_SH_ROM_STATE]=0;   /* RAW, jamais swappe en SMS */
    /* AHW=PAHW_SMS DOIT etre pose avant PicoCartInsert : il aiguille vers
       PicoMemSetupMS + PicoPowerMS + PicoCartDetectMS (chemin Master System). */
    PicoIn.AHW = PAHW_SMS;
    if(PicoCartInsert((unsigned char*)rom,g_rom_size,NULL)!=0){ shared[ZZPICO_SH_ERROR]=ZZPICO_ERR_PICO_INIT; return 0; }
    g_rom_ok=1; DIAG(0xA102UL); return 1;
}

/* ==== SAUVEGARDES : SRAM (Pico.sv) + savestate (PicoStateFP) <-> buffer DDR ====
   Modele ZZDoom : le 68k declenche via SH_SAVE_CMD + SH_SAVE_SEQ, Core1 fait la
   (de)serialisation dans un buffer DDR, recopie SEQ dans ACK quand fini. Le 68k
   ne connait pas le format : il transporte les octets DDR<->fichier. */
/* Savestate = 0 pour l'instant : necessite state.c dans le build. SRAM d'abord.
   Passer a 1 quand state.c sera linke pour activer PicoStateFP. */
#define ZZPICO_SAVESTATE 1
#if ZZPICO_SAVESTATE
extern int PicoStateFP(void *afile, int is_save,
    size_t (*rd)(void*,size_t,size_t,void*),
    size_t (*wr)(void*,size_t,size_t,void*),
    size_t (*eof)(void*),
    int    (*seek)(void*,long,int));

static unsigned char *g_ms_buf = 0;   /* flux memoire pour PicoStateFP */
static long g_ms_pos = 0, g_ms_max = 0;
static size_t ms_read(void *p, size_t sz, size_t n, void *f){
    long total=(long)(sz*n), i; unsigned char *d=(unsigned char*)p; (void)f;
    if(g_ms_pos+total>g_ms_max) total=g_ms_max-g_ms_pos; if(total<0) total=0;
    for(i=0;i<total;i++) d[i]=g_ms_buf[g_ms_pos+i];
    g_ms_pos+=total; return sz?((size_t)total/sz):0;
}
static size_t ms_write(void *p, size_t sz, size_t n, void *f){
    long total=(long)(sz*n), i; const unsigned char *s=(const unsigned char*)p; (void)f;
    if(g_ms_pos+total>g_ms_max) total=g_ms_max-g_ms_pos; if(total<0) total=0;
    for(i=0;i<total;i++) g_ms_buf[g_ms_pos+i]=s[i];
    g_ms_pos+=total; return sz?((size_t)total/sz):0;
}
static size_t ms_eof(void *f){ (void)f; return (size_t)(g_ms_pos>=g_ms_max); }
static int ms_seek(void *f, long off, int whence){
    long np; (void)f;
    if(whence==0) np=off; else if(whence==1) np=g_ms_pos+off; else np=g_ms_max+off;
    if(np<0) np=0; if(np>g_ms_max) np=g_ms_max; g_ms_pos=np; return 0;
}
#endif

static u32 g_save_seq_seen = 0;
static void save_service(void){
    u32 seq = shared[ZZPICO_SH_SAVE_SEQ];
    u32 cmd, status=ZZPICO_SAVE_STATUS_ERR, outsize=0, err=0;
    unsigned char *buf=(unsigned char*)ZZPICO_SAVE_BUF_ARM;
    if(seq==g_save_seq_seen) return;             /* rien de nouveau -> cout nul */
    cmd = shared[ZZPICO_SH_SAVE_CMD];
    if(cmd==ZZPICO_SAVE_CMD_SRAM_SAVE){
        if(Pico.sv.data && Pico.sv.size){
            u32 n=Pico.sv.size, i; if(n>ZZPICO_SAVE_MAX) n=ZZPICO_SAVE_MAX;
            for(i=0;i<n;i++) buf[i]=Pico.sv.data[i];
            dcache_clean_range(buf,n);           /* visible au 68k */
            outsize=n; status=ZZPICO_SAVE_STATUS_OK;
        } else err=1;
    } else if(cmd==ZZPICO_SAVE_CMD_SRAM_LOAD){
        u32 n=shared[ZZPICO_SH_SAVE_SIZE], i;
        if(Pico.sv.data && n){
            if(n>Pico.sv.size) n=Pico.sv.size; if(n>ZZPICO_SAVE_MAX) n=ZZPICO_SAVE_MAX;
            dcache_inval_range(buf,n);
            for(i=0;i<n;i++) Pico.sv.data[i]=buf[i];
            Pico.sv.changed=0; status=ZZPICO_SAVE_STATUS_OK;
        } else err=2;
    } else if(cmd==ZZPICO_SAVE_CMD_STATE_SAVE){
#if ZZPICO_SAVESTATE
        g_ms_buf=buf; g_ms_pos=0; g_ms_max=(long)ZZPICO_SAVE_MAX;
        if(PicoStateFP((void*)1,1,ms_read,ms_write,ms_eof,ms_seek)==0){
            dcache_clean_range(buf,(u32)g_ms_pos);
            outsize=(u32)g_ms_pos; status=ZZPICO_SAVE_STATUS_OK;
        } else err=3;
#else
        err=99;   /* savestate pas encore active (state.c a linker) */
#endif
    } else if(cmd==ZZPICO_SAVE_CMD_STATE_LOAD){
#if ZZPICO_SAVESTATE
        u32 n=shared[ZZPICO_SH_SAVE_SIZE];
        if(n){ if(n>ZZPICO_SAVE_MAX) n=ZZPICO_SAVE_MAX;
            dcache_inval_range(buf,n);
            g_ms_buf=buf; g_ms_pos=0; g_ms_max=(long)n;
            if(PicoStateFP((void*)1,0,ms_read,ms_write,ms_eof,ms_seek)==0)
                status=ZZPICO_SAVE_STATUS_OK;
            else err=4;
        } else err=5;
#else
        err=99;
#endif
    } else err=6;
    shared[ZZPICO_SH_SAVE_SIZE]=outsize;
    shared[ZZPICO_SH_SAVE_ERR]=err;
    shared[ZZPICO_SH_SAVE_STATUS]=status;
    __asm__ volatile("dsb":::"memory");
    shared[ZZPICO_SH_SAVE_ACK]=seq;              /* handshake : 68k attend ACK==SEQ */
    g_save_seq_seen=seq;
}

/* ======================= MENU v1 (dessine par le Core1) =======================
   Modes : MODE_RUN (jeu) / MODE_MENU (PicoFrame suspendu, audio silencieux, Core1
   dessine le menu dans le pipeline triple-buffer). Navigation via SH_PAD0. Les actions
   fichier (save/load) et quit sont deleguees au 68k via SH_MENU_ACTION. */
#define MODE_RUN   0
#define MODE_MENU  1
#define MENU_H     240
#define MENU_N     5
static int g_emu_mode  = MODE_RUN;
static int g_menu_sel  = 0;
static int g_menu_slot = 1;
static u32 g_menu_padprev = 0;
static int g_menu_msg  = 0;    /* 0 none,1 saving,2 saved,3 loading,4 loaded */
static const char *g_menu_items[MENU_N] = { "RESUME","RESET","SAVE STATE","LOAD STATE","QUIT" };

static void draw_char(u32 *buf, u32 pp, int x, int y, char c, u32 col){
    int r,b; const unsigned char *g; unsigned char uc=(unsigned char)c;
    if(uc>='a'&&uc<='z') uc-=32;
    if(uc<32||uc>127) uc=' ';
    g=zzfont8x8[uc-32];
    for(r=0;r<8;r++){ unsigned char row=g[r];
        for(b=0;b<8;b++) if(row&(0x80>>b)) buf[(y+r)*(int)pp+(x+b)]=col; }
}
static void draw_text(u32 *buf, u32 pp, int x, int y, const char *s, u32 col){
    while(*s){ draw_char(buf,pp,x,y,*s++,col); x+=8; }
}
static void menu_render(void){
    u32 *buf=(u32*)shared[ZZPICO_SH_P96_BASE];
    u32 pp=shared[ZZPICO_SH_P96_PITCH]>>2;
    u32 white=0x00FFFFFFu, hi=0x00FFFF00u, dim=0x00808080u;
    int i, x=72, y=56;
    if(!buf || !pp) return;
    { u32 n=pp*(u32)MENU_H, k; for(k=0;k<n;k++) buf[k]=0; }   /* fond noir */
    draw_text(buf,pp, 104, 20, "ZZPICODRIVE", white);
    for(i=0;i<MENU_N;i++){
        u32 col=(i==g_menu_sel)?hi:white;
        if(i==g_menu_sel) draw_char(buf,pp, x-16, y+i*14, '>', hi);
        draw_text(buf,pp, x, y+i*14, g_menu_items[i], col);
    }
    { char sl[8]; sl[0]='S';sl[1]='L';sl[2]='O';sl[3]='T';sl[4]=' ';sl[5]=(char)('0'+g_menu_slot);sl[6]=0;
      draw_text(buf,pp, x+120, y+2*14, sl, dim);
      draw_text(buf,pp, x+120, y+3*14, sl, dim); }
    draw_text(buf,pp, 72, 176, Pico.m.pal?"PAL 50HZ":"NTSC 60HZ", dim);
    draw_text(buf,pp, 72, 188, "AUDIO AHI", dim);
    if(g_menu_msg==1) draw_text(buf,pp, 72, 206, "SAVING", hi);
    else if(g_menu_msg==2) draw_text(buf,pp, 72, 206, "SAVED", hi);
    else if(g_menu_msg==3) draw_text(buf,pp, 72, 206, "LOADING", hi);
    else if(g_menu_msg==4) draw_text(buf,pp, 72, 206, "LOADED", hi);
    dcache_clean_range((void*)buf, (pp<<2)*(u32)MENU_H);
}
static void menu_validate(void){
    switch(g_menu_sel){
        case 0: g_emu_mode=MODE_RUN; break;                              /* RESUME */
        case 1: PicoReset(); g_emu_mode=MODE_RUN; break;                 /* RESET (garde SRAM) */
        case 2: shared[ZZPICO_SH_SAVE_SLOT]=(u32)g_menu_slot;            /* SAVE */
                shared[ZZPICO_SH_MENU_ACTION]=ZZPICO_MENU_ACT_SAVE; g_menu_msg=1; break;
        case 3: shared[ZZPICO_SH_SAVE_SLOT]=(u32)g_menu_slot;            /* LOAD */
                shared[ZZPICO_SH_MENU_ACTION]=ZZPICO_MENU_ACT_LOAD; g_menu_msg=3; break;
        case 4: shared[ZZPICO_SH_MENU_ACTION]=ZZPICO_MENU_ACT_QUIT; break; /* QUIT (68k arrete) */
    }
}
static void menu_tick(void){
    u32 pad, pressed;
    /* action fichier en cours ? le 68k efface MENU_ACTION quand fini */
    if(shared[ZZPICO_SH_MENU_ACTION]!=ZZPICO_MENU_ACT_NONE){ menu_render(); return; }
    if(g_menu_msg==1) g_menu_msg=2;                       /* save termine */
    else if(g_menu_msg==3){ g_menu_msg=4; g_emu_mode=MODE_RUN; }  /* load termine -> RUN */
    pad=shared[ZZPICO_SH_PAD0]; pressed=pad & ~g_menu_padprev; g_menu_padprev=pad;
    if(pressed & ZP_UP)    { if(g_menu_sel>0) g_menu_sel--; }
    if(pressed & ZP_DOWN)  { if(g_menu_sel<MENU_N-1) g_menu_sel++; }
    if(pressed & ZP_LEFT)  { if(g_menu_slot>1) g_menu_slot--; }
    if(pressed & ZP_RIGHT) { if(g_menu_slot<5) g_menu_slot++; }
    if(pressed & (ZP_A|ZP_START)) menu_validate();
    if(pressed & ZP_B)     g_emu_mode=MODE_RUN;           /* B = resume */
    menu_render();
}

static void cmd_start(void)
{
    if(!g_rom_ok) return;
    if(!g_ctab_done) crc_tab_init();
    /* VISUAL_TIMING_CLEAN : region posee DANS PicoDrive (coherence console),
       pas simulee par un cap externe. Doit etre pose avant PicoReset (la
       detection region s'y fait). 0=AUTO,4=USA,1=JAPN,8=EUR,2=JAPP ; toute
       autre valeur -> AUTO. */
    { u32 rgn=shared[ZZPICO_SH_REGION_IN];
      if(rgn==1||rgn==2||rgn==4||rgn==8) PicoIn.regionOverride=(unsigned short)rgn;
      else                               PicoIn.regionOverride=0;
      shared[ZZPICO_SH_REGION_EFF]=PicoIn.regionOverride; }
    PicoPower(); PicoReset();
    /* pal effectif resolu (AUTO->NTSC/PAL ou force) : pilote le pacing 68k */
    shared[ZZPICO_SH_PAL_OUT]=Pico.m.pal?1u:0u;
    /* DIAG : snapshot juste apres PicoReset (attendu A7~0x00FFFE00, PC=vecteur1) */
    g_fault_done=0;
    /* DDR-hygiene : effacer les slots fault (sinon le launcher relit une capture rance) */
    shared[ZZPICO_SH_FAULT_CAPTURED]=0; shared[ZZPICO_SH_FAULT_VECT]=0;
    shared[ZZPICO_SH_FAULT_FRAME]=0;    shared[ZZPICO_SH_FAULT_OLDPC]=0;
    zzpico_snap(ZZPICO_SH_RESET_PC,ZZPICO_SH_RESET_SR,ZZPICO_SH_RESET_A7,ZZPICO_SH_RESET_ASP);
    /* Phase 3a : rendu REEL non affiche dans le buffer ARM fixe, verifie par
       CRC. Buffer 320x240x2 @ 0x05600000, pitch 640. PicoDrawSetOutBuf appele
       UNE seule fois (jamais rappele sur H32/H40. */
    PicoIn.skipFrame = 0;
    PicoDrawSetOutFormat(PDF_RGB555, 0);
    PicoDrawSetOutBuf((void*)FB3A_BASE, FB3A_PITCH);
    /* DIAG : sentinelle 0xA55A au lieu de 0 -> distingue
       "renderer n'ecrit pas" de "renderer ecrit du noir". */
    { u32 i; volatile u32 *b=(volatile u32*)FB3A_BASE; for(i=0;i<FB3A_SIZE/4;i++) b[i]=0xA55AA55Au; }
    dcache_clean_range((void*)FB3A_BASE, FB3A_SIZE);
    g_last_pc=0xFFFFFFFFu; g_pc_changes=0;
    /* DIAG : snapshot config (opt pose en cmd_init, avant PicoPower/Reset) */
    shared[ZZPICO_SH_DBG_OPT]=PicoIn.opt;
    shared[ZZPICO_SH_DBG_SNDRATE]=(u32)PicoIn.sndRate;
    shared[ZZPICO_SH_DBG_SNDOUT_NULL]=(PicoIn.sndOut==NULL)?1u:0u;
    shared[ZZPICO_SH_DBG_SKIPFRAME]=PicoIn.skipFrame;
    g_frames=0; g_acc_raw=0xFFFFFFFFu;
    g_vm_sl=g_vm_lc=g_vm_sc=g_vm_cc=0; g_mode_seq=0;
    g_last_req=shared[ZZPICO_SH_FRAME_REQ];   /* pas de frame fantome */
    pmu_enable();                              /* CCNT actif pour l'auto-cadence */
    g_save_seq_seen=shared[ZZPICO_SH_SAVE_SEQ];  /* pas de save fantome au demarrage */
    g_frame_seq=0; g_frame_t0=pmu_cyc();       /* auto-cadence : t0 initial */
    shared[ZZPICO_SH_FRAME_COUNTER]=0;
    shared[ZZPICO_SH_FRAME_DONE]=g_last_req;
    g_running=1;
    shared[ZZPICO_SH_STATUS]=ZZPICO_ST_RUNNING;
    DIAG(0xA103UL);
}

void core1_entry(void *env) __attribute__((section(".text.core1_entry")));
void core1_entry(void *env)
{
    u32 hb=0;
    (void)env;

    /* Zero .bss (now at private pool 0x22000000; MMU off -> physical access). */
    { extern u32 _bss_start,_bss_end; u32 *p=&_bss_start; while(p<&_bss_end)*p++=0; }

    mmu_init();   /* XX19 sections; enables MMU+caches, A=0/U=1 */

    shared[ZZPICO_SH_MAGIC]=ZZPICO_MAGIC;
    shared[ZZPICO_SH_VERSION]=ZZPICO_VERSION;
    shared[ZZPICO_SH_STATUS]=ZZPICO_ST_BOOTING;
    shared[ZZPICO_SH_ERROR]=ZZPICO_ERR_NONE;
    shared[ZZPICO_SH_HEARTBEAT]=0;
    shared[ZZPICO_SH_ACK_SEQ]=shared[ZZPICO_SH_CMD_SEQ];
    DIAG(0xA001UL);
    zz_install_vectors();   /* B: vecteurs ARM (UNDEF/PABT/DABT -> ERROR+WFE) */
    DIAG(0xA1A1UL);
    shared[ZZPICO_SH_STATUS]=ZZPICO_ST_READY;
    DIAG(0xA002UL);

    for(;;){
        u32 cmd_seq=shared[ZZPICO_SH_CMD_SEQ];
        if(cmd_seq!=shared[ZZPICO_SH_ACK_SEQ]){
            u32 cmd=shared[ZZPICO_SH_COMMAND];
            switch(cmd){
                case ZZPICO_CMD_INIT:       cmd_init();     break;
                case ZZPICO_CMD_LOAD_ROM:   cmd_load_rom(); break;
                case ZZPICO_CMD_START:      cmd_start();    break;
                case ZZPICO_CMD_STEP_FRAME: DIAG(0xA104UL); break;
                case ZZPICO_CMD_SELFTEST:
                    /* UDF permanente -> vecteur UNDEF (ERROR=8). Valide les
                       vecteurs en isolation. Ne revient jamais (WFE). */
                    DIAG(0xA1FEUL);
                    __asm__ volatile(".word 0xE7F000F0");
                    break;
                case ZZPICO_CMD_STOP:
                    g_running=0;
                    shared[ZZPICO_SH_STATUS]=ZZPICO_ST_STOPPING;
                    DIAG(0xA0FEUL);
                    shared[ZZPICO_SH_ACK_SEQ]=cmd_seq;
                    goto stopped;
                default: break;
            }
            __asm__ volatile("dsb":::"memory");
            shared[ZZPICO_SH_ACK_SEQ]=cmd_seq;
        } else if(g_running){
            {   /* AUTONOME (modele ZZDoom) : une PicoFrame par iteration, pacee par
                   le handshake FRAME_READY/FLIP_SEQ ci-dessous. Plus de FRAME_REQ 68k
                   -> le 68k n'a plus a driver chaque frame ni busy-poll FRAME_DONE. */
                /* 3a: exactement UNE PicoFrame par increment de FRAME_REQ.
                   STOP reste prioritaire (teste avant ce bloc). */
                u32 pc0,pc1,pc2=0,pc2a=0,pc3=0;   /* timestamps CCNT : decoupe rendu */
                { static int pmu_on=0; if(!pmu_on){ pmu_enable(); pmu_on=1; } }
                if(!g_lut_ready) build_lut565();
#ifdef ZZPICO_RENDER_PROFILE
                /* RENDER_AUDIT_PROFILE : frameskip natif PicoDrive. skip=1 ne
                   gate QUE le rendu (PicoVideoSync/PicoFrameFull) ; CPU Cyclone,
                   VDP timing, H-int, son tournent normalement (verifie dans
                   pico_cmn.c). Le renderer n'est jamais engage -> robuste, pas
                   de bookkeeping incoherent. Ecran noir attendu. Mesure PicoFrame
                   sans le trace pour chiffrer le cout reel de draw.c. */
                PicoIn.skipFrame = 1;
#else
                PicoIn.skipFrame = 0;   /* DIAG : forcer a chaque frame */
#endif
                bt_frame_now = g_frames;  /* DIAG : etiqueter le ring */
                /* INPUT : SH_PAD0/SH_PAD1 (bitmask ZP_*, 1=presse) -> PicoIn.pad[0]/[1]
                   (format PicoDrive MXYZ SACB RLDU : U0 D1 L2 R3 B4 C5 A6 S7 Z8 Y9 X10 M11).
                   Neutralise U+D et L+R. 1 joueur : le launcher ecrit 0 dans SH_PAD1 -> pad[1]=0. */
                { int pi; for(pi=0;pi<2;pi++){
                    u32 zp=shared[pi==0?ZZPICO_SH_PAD0:ZZPICO_SH_PAD1]; u16 pd=0;
                    if((zp&(ZP_UP|ZP_DOWN))==ZP_UP)      pd|=(1u<<0);
                    if((zp&(ZP_UP|ZP_DOWN))==ZP_DOWN)    pd|=(1u<<1);
                    if((zp&(ZP_LEFT|ZP_RIGHT))==ZP_LEFT) pd|=(1u<<2);
                    if((zp&(ZP_LEFT|ZP_RIGHT))==ZP_RIGHT)pd|=(1u<<3);
                    /* SMS : Button1=bit4, Button2=bit5, Pause(NMI)=bit7.
                       ZP_A->B1, ZP_B->B2, ZP_START->Pause. Pas de C/X/Y/Z/Mode. */
                    if(zp&ZP_A)     pd|=(1u<<4);   /* SMS Button 1 */
                    if(zp&ZP_B)     pd|=(1u<<5);   /* SMS Button 2 */
                    if(zp&ZP_START) pd|=(1u<<7);   /* SMS Pause (NMI, edge) */
                    PicoIn.pad[pi]=pd;
                  }
                }
                save_service();   /* SRAM/savestate si le 68k a declenche (sinon cout nul) */
                /* EMU_CMD : menu in-game desactive pour l'instant (mis de cote). On acquitte
                   juste la commande pour ne pas la re-declencher. RESET reste utile. */
                { u32 ec=shared[ZZPICO_SH_EMU_CMD];
                  if(ec==EMU_CMD_RESET){ PicoReset(); shared[ZZPICO_SH_EMU_CMD]=EMU_CMD_NONE; }
                  else if(ec!=EMU_CMD_NONE) shared[ZZPICO_SH_EMU_CMD]=EMU_CMD_NONE;
                }
                if(0 && g_emu_mode==MODE_MENU){   /* menu desactive : bloc conserve pour 1.1 */
                    menu_tick();
                    shared[ZZPICO_SH_FRAME_READY]=++g_frame_seq;
                    { u32 cpf=Pico.m.pal?13333333u:11111111u, s=40000000u;
                      while((pmu_cyc()-g_frame_t0)<cpf && s--){ } g_frame_t0=pmu_cyc(); }
                    { u32 s=8000000u; while(shared[ZZPICO_SH_FLIP_SEQ]!=g_frame_seq && s--){ } }
                    shared[ZZPICO_SH_HEARTBEAT]=++hb;
                    __asm__ volatile("dsb":::"memory");
                    continue;
                }
                pc0=pmu_cyc();
                PicoFrame();
                pc1=pmu_cyc();
                ++g_frames;
                shared[ZZPICO_SH_FRAME_COUNTER]=g_frames;
                if(g_frames==1)
                    zzpico_snap(ZZPICO_SH_F1_PC,ZZPICO_SH_F1_SR,ZZPICO_SH_F1_A7,ZZPICO_SH_F1_ASP);
                /* Mode P96 rapide : en rendu direct P96, couper TOUS les
                   diagnostics lourds (CRC RGB16, CRC ARGB, scan, poll) qui
                   coutent des centaines de Ko/frame de trafic memoire inutile.
                   On garde uniquement conversion + dcache_clean + FRAME_DONE. */
                int p96_fast=(shared[ZZPICO_SH_P96_ENABLE] && shared[ZZPICO_SH_P96_BASE])?1:0;
                if(!p96_fast){
                    u32 cf=crc_full(FB3A_BASE, FB3A_SIZE);
                    u32 ca=crc_rect(FB3A_BASE, FB3A_PITCH, g_vm_sc, g_vm_cc, g_vm_sl, g_vm_lc);
                    u8 b[4]; int k;
                    shared[ZZPICO_SH_FRAME_CRC_FULL]=cf;
                    shared[ZZPICO_SH_FRAME_CRC_ACTIVE]=ca;
                    /* CRC_ACC = CRC du flux des CRC_FULL successifs */
                    b[0]=(u8)(cf>>24);b[1]=(u8)(cf>>16);b[2]=(u8)(cf>>8);b[3]=(u8)cf;
                    for(k=0;k<4;k++) g_acc_raw=g_ctab[(g_acc_raw^b[k])&0xFF]^(g_acc_raw>>8);
                    shared[ZZPICO_SH_FRAME_CRC_ACC]=g_acc_raw^0xFFFFFFFFu;
                }
                /* ---- Phase 3b/3d : RGB16 -> ARGB8888 + CRC ARGB ----
                 * 3b : ecrit dans le buffer ARM fixe FB3B_ARGB_BASE.
                 * 3d : si P96_ENABLE=1 et BASE!=0, ecrit DIRECTEMENT dans le
                 *      backbuffer P96 (adresse ARM + pitch publies par le 68k),
                 *      supprimant la copie 68k. Format inchange (0xFFRRGGBB en
                 *      ARM-LE => octets [B][G][R][A], valide en 3c fmt5). */
                {
                    const u16 *src=(const u16*)FB3A_BASE;
                    u32 *adst; u32 stride_px, pitch_b;
                    u32 yy,xx,nz=0,fc=0xFFFFFFFFu;
                    int cm=565;  /* marqueur 565_FIXED publie dans CONV_MODE */
                    if(shared[ZZPICO_SH_P96_ENABLE] && shared[ZZPICO_SH_P96_BASE]){
                        adst    =(u32*)shared[ZZPICO_SH_P96_BASE];
                        pitch_b = shared[ZZPICO_SH_P96_PITCH];
                        stride_px = pitch_b>>2;
                        /* Remappe la section du buffer P96 en WB/WA (cache) la
                           1re fois qu'on la voit. Cache jusqu'a 4 bases. */
                        {
                            static u32 mapped[4]={0,0,0,0}; static int nmap=0;
                            u32 base=(u32)(uintptr_t)adst; int k,seen=0;
                            for(k=0;k<nmap;k++) if(mapped[k]==base) seen=1;
                            if(!seen && nmap<4){
                                if(nmap==0) shared[ZZPICO_SH_PERF_DESC_P96PRE]=mmu_get_desc(base);
                                mmu_set_wbwa(base, FB3A_H*pitch_b);
                                mapped[nmap++]=base;
                            }
                        }
                    } else {
                        adst    =(u32*)FB3B_ARGB_BASE;
                        pitch_b = FB3A_W*4u;
                        stride_px = FB3A_W;
                    }
                    if(p96_fast){
                        /* VISUAL_TIMING_CLEAN v2 : on reprend EXACTEMENT le loffs
                           de PicoDrive (draw.c:1741) au lieu de re-centrer nous-
                           memes. NTSC 224 -> loffs=8, lines=224 (8 haut + 8 bas,
                           symetrique). Mode 240 -> loffs=0, lines=240 (plein).
                           La conversion copie 1:1 et noircit les bordures. */
                        /* Rectangle actif = celui publie par PicoDrive (mode4/draw
                           via emu_video_mode_change -> g_vm_*). Vaut pour MD ET SMS.
                           Fallback plein cadre 8/224 si pas encore renseigne. */
                        u32 loffs=(g_vm_lc>0)?(u32)g_vm_sl:8u;
                        u32 lines=(g_vm_lc>0)?(u32)g_vm_lc:224u;
                        u32 coffs=(g_vm_cc>0)?(u32)g_vm_sc:0u;
                        u32 cols =(g_vm_cc>0)?(u32)g_vm_cc:FB3A_W;
                        shared[ZZPICO_SH_ACTIVE_H]=lines;
                        shared[ZZPICO_SH_Y_OFFSET]=loffs;
                        /* FAST_CONVERT_LUT : LUT 565->P96 directement dans le
                           backbuffer P96, bordures noircies hors rectangle. */
                        convert_lut_p96(src, adst, stride_px, loffs, lines, coffs, cols);
                        pc2a=pmu_cyc(); pc2=pc2a;
                        dcache_clean_range((void*)adst, FB3A_H*pitch_b);
                        pc3=pmu_cyc();
                        shared[ZZPICO_SH_CONV_MODE]=(u32)cm;
                    } else {
                        for(yy=0;yy<FB3A_H;yy++){
                            const u16 *srow=src + yy*(FB3A_PITCH/2);
                            u32 *arow=adst + yy*stride_px;
                            int in_row=((int)yy>=g_vm_sl && (int)yy<g_vm_sl+g_vm_lc);
                            for(xx=0;xx<FB3A_W;xx++){
                                u16 px=srow[xx]; u32 r,g,b,v;
                                r=(px>>11)&0x1F; g=(px>>5)&0x3F; b=px&0x1F;
                                r=(r<<3)|(r>>2); g=(g<<2)|(g>>4); b=(b<<3)|(b>>2);
                                v=0xFF000000u|(r<<16)|(g<<8)|b;
                                arow[xx]=v;
                                if(in_row && (int)xx>=g_vm_sc && (int)xx<g_vm_sc+g_vm_cc){
                                    if((v & 0x00FFFFFFu)!=0){ nz++; if(fc==0xFFFFFFFFu) fc=yy*stride_px+xx; }
                                }
                            }
                        }
                        pc2a=pmu_cyc(); pc2=pc2a;
                        dcache_clean_range((void*)adst, FB3A_H*pitch_b);
                        pc3=pmu_cyc();
                        shared[ZZPICO_SH_CONV_MODE]=(u32)cm;
                        shared[ZZPICO_SH_ARGB_NONZERO]=nz;
                        shared[ZZPICO_SH_ARGB_FIRST_CHG]=fc;
                        shared[ZZPICO_SH_CRC_ARGB_FULL]=crc_full((const u8*)adst, FB3A_H*pitch_b);
                        shared[ZZPICO_SH_CRC_ARGB_ACTIVE]=crc_rect_bpp((const u8*)adst,
                            pitch_b, g_vm_sc, g_vm_cc, g_vm_sl, g_vm_lc, 4);
                    }
                }
                if(p96_fast){
                    shared[ZZPICO_SH_PERF_PICOFRAME]=pc1-pc0;
                    shared[ZZPICO_SH_PERF_CONVERT]=pc2a-pc1;
                    shared[ZZPICO_SH_PERF_COPY]=pc2-pc2a;
                    shared[ZZPICO_SH_PERF_DCLEAN]=pc3-pc2;
#ifdef ZZPICO_NTSC_DIAG
                    /* NTSC_TIMING_DIAG : accumulateurs session (passif, aucune
                       influence sur emulation/rendu/audio). Seuils cycles @666667/ms :
                       12ms=8000004, 14ms=9333338, 16.67ms=11111117. */
                    {
                        static u32 d_min=0xFFFFFFFFu,d_max=0u,d_frm=0u;
                        static u32 d_sus=0u,d_g12=0u,d_g14=0u,d_g67=0u;
                        static u32 d_cmax=0u,d_lmax=0u;
                        static u32 d_cmin=0xFFFFFFFFu,d_csus=0u;
                        u32 pf=pc1-pc0, cv=pc2a-pc1, cl=pc3-pc2;
                        if(pf<d_min) d_min=pf;
                        if(pf>d_max) d_max=pf;
                        d_sus+=pf/667u;           /* ~us : 666667 cyc/ms -> 667 cyc/us */
                        d_frm++;
                        if(pf>8000004u)  d_g12++;
                        if(pf>9333338u)  d_g14++;
                        if(pf>11111117u) d_g67++;
                        if(cv>d_cmax) d_cmax=cv;
                        if(cv<d_cmin) d_cmin=cv;
                        d_csus+=cv/667u;
                        if(cl>d_lmax) d_lmax=cl;
                        shared[ZZPICO_SH_DIAG_PF_MIN]=d_min;
                        shared[ZZPICO_SH_DIAG_PF_MAX]=d_max;
                        shared[ZZPICO_SH_DIAG_PF_SUM_US]=d_sus;
                        shared[ZZPICO_SH_DIAG_FRAMES]=d_frm;
                        shared[ZZPICO_SH_DIAG_N_GT12]=d_g12;
                        shared[ZZPICO_SH_DIAG_N_GT14]=d_g14;
                        shared[ZZPICO_SH_DIAG_N_GT1667]=d_g67;
                        shared[ZZPICO_SH_DIAG_CONV_MAX]=d_cmax;
                        shared[ZZPICO_SH_DIAG_CLEAN_MAX]=d_lmax;
                        shared[ZZPICO_SH_DIAG_CONV_MIN]=d_cmin;
                        shared[ZZPICO_SH_DIAG_CONV_SUM_US]=d_csus;
                    }
#endif
                }
                /* Benchmark ponctuel (frame 1) : isoler la cause des 84 ms.
                   On chronometre 76800 ecritures u32 vers le buffer P96 puis
                   vers FB3B (section 0x056, cachee en dur). Si P96 est lent et
                   FB3B rapide => section P96 non cachee malgre le remap. On
                   publie aussi les descripteurs de section (bits TEX/C/B). */
                if(p96_fast && g_frames==1 && shared[ZZPICO_SH_P96_BASE]){
                    volatile u32 *pp=(volatile u32*)shared[ZZPICO_SH_P96_BASE];
                    volatile u32 *ff=(volatile u32*)FB3B_ARGB_BASE;
                    u32 i,a,b; const u32 PX=0xFF008000u; /* vert constant (fmt5) */
                    a=pmu_cyc(); for(i=0;i<76800u;i++) pp[i]=PX; b=pmu_cyc();
                    shared[ZZPICO_SH_PERF_WCYC_P96]=b-a;
                    a=pmu_cyc(); for(i=0;i<76800u;i++) ff[i]=PX; b=pmu_cyc();
                    shared[ZZPICO_SH_PERF_WCYC_FB3B]=b-a;
                    shared[ZZPICO_SH_PERF_DESC_P96]=mmu_get_desc((u32)shared[ZZPICO_SH_P96_BASE]);
                    shared[ZZPICO_SH_PERF_DESC_FB3B]=mmu_get_desc((u32)(uintptr_t)FB3B_ARGB_BASE);
                }
                /* DIAG complets (YM/VDP/scan/poll) : hors mode P96 rapide
                   seulement (sinon centaines de Ko/frame de lectures inutiles). */
                if(!p96_fast){
                shared[ZZPICO_SH_DBG_YM_STATUS]=ym2612.OPN.ST.status;
                shared[ZZPICO_SH_DBG_YM_MODE]=ym2612.OPN.ST.mode;
                shared[ZZPICO_SH_DBG_VDP_REG1]=Pico.video.reg[1];
                shared[ZZPICO_SH_DBG_VDP_REG12]=Pico.video.reg[12];
                { int ci,nz=0; for(ci=0;ci<0x40;ci++) if(PicoMem.cram[ci]) nz++;
                  shared[ZZPICO_SH_DBG_CRAM_NZ]=(u32)nz; }
                { u32 pc;
#ifdef EMU_C68K
                  pc=PicoCpuCM68k.pc - (u32)PicoCpuCM68k.membase;
#else
                  pc=fm68k_get_pc(&PicoCpuFM68k);
#endif
                  if(pc!=g_last_pc){ g_pc_changes++; g_last_pc=pc; }
                  shared[ZZPICO_SH_DBG_M68K_PC]=pc;
                  shared[ZZPICO_SH_DBG_PC_CHANGES]=g_pc_changes; }
                { const u16 *bb=(const u16*)FB3A_BASE; u32 i,ns=0,zz=0,nn=0,fc=0xFFFFFFFFu;
                  for(i=0;i<FB3A_SIZE/2;i++){ u16 v=bb[i];
                    if(v==0xA55A) ns++;
                    else { if(fc==0xFFFFFFFFu) fc=i; if(v==0) zz++; else nn++; } }
                  shared[ZZPICO_SH_DBG_BUF_SENT]=ns;
                  shared[ZZPICO_SH_DBG_BUF_ZERO]=zz;
                  shared[ZZPICO_SH_DBG_BUF_NONZERO]=nn;
                  shared[ZZPICO_SH_DBG_FIRST_CHG]=fc; }
                /* DIAG poll : snapshot compteurs bus -> shared, ring -> DDR debug */
                shared[ZZPICO_SH_POLL_VDP_READS]=bt_vdp_reads;
                shared[ZZPICO_SH_POLL_VDP_LAST]=bt_vdp_last;
                shared[ZZPICO_SH_POLL_VDP_VB]=bt_vdp_vb;
                shared[ZZPICO_SH_POLL_VDP_HB]=bt_vdp_hb;
                shared[ZZPICO_SH_POLL_VDP_PC]=bt_vdp_pc;
                shared[ZZPICO_SH_POLL_BREQ_READS]=bt_breq_reads;
                shared[ZZPICO_SH_POLL_BREQ_WRITES]=bt_breq_writes;
                shared[ZZPICO_SH_POLL_BREQ_LASTW]=bt_breq_lastw;
                shared[ZZPICO_SH_POLL_BREQ_LASTR]=bt_breq_lastr;
                shared[ZZPICO_SH_POLL_BREQ_PC]=bt_breq_pc;
                shared[ZZPICO_SH_POLL_ZRST_READS]=bt_zrst_reads;
                shared[ZZPICO_SH_POLL_ZRST_WRITES]=bt_zrst_writes;
                shared[ZZPICO_SH_POLL_ZRST_LASTW]=bt_zrst_lastw;
                shared[ZZPICO_SH_POLL_ZRST_LASTR]=bt_zrst_lastr;
                shared[ZZPICO_SH_POLL_ZRST_PC]=bt_zrst_pc;
                shared[ZZPICO_SH_POLL_IO_READS]=bt_io_reads;
                shared[ZZPICO_SH_POLL_IO_WRITES]=bt_io_writes;
                shared[ZZPICO_SH_POLL_IO_LASTADDR]=bt_io_lastaddr;
                shared[ZZPICO_SH_POLL_IO_LASTR]=bt_io_lastr;
                shared[ZZPICO_SH_POLL_IO_LASTW]=bt_io_lastw;
                shared[ZZPICO_SH_POLL_IO_PC]=bt_io_pc;
                shared[ZZPICO_SH_POLL_VER_READS]=bt_ver_reads;
                shared[ZZPICO_SH_POLL_PAD1_READS]=bt_pad1_reads;
                shared[ZZPICO_SH_POLL_PAD2_READS]=bt_pad2_reads;
                { volatile u32 *r=(volatile u32*)ZZPICO_ARM_DEBUG; u32 i;
                  for(i=0;i<BT_RING_N*6;i++) r[i]=bt_ring[i];
                  dcache_clean_range((void*)ZZPICO_ARM_DEBUG, BT_RING_N*6*4); }
                shared[ZZPICO_SH_POLL_RING_COUNT]=bt_ring_head;
                shared[ZZPICO_SH_POLL_RING_BASE]=(u32)ZZPICO_ARM_DEBUG;
                /* publier le buffer RGB16 en DDR (mode diag seulement ; en mode
                   P96 rapide seul l'ARGB P96 est affiche, deja clean plus haut) */
                dcache_clean_range((void*)FB3A_BASE, FB3A_SIZE);
                }  /* fin if(!p96_fast) : DIAG / scan / poll */
                __asm__ volatile("dsb":::"memory");
                /* Frame prete dans le back buffer (SH_P96_BASE, buffer LIBRE fourni par
                   le 68k). On publie la sequence. */
                shared[ZZPICO_SH_FRAME_READY] = ++g_frame_seq;
                /* AUTO-CADENCE : Core1 impose la vitesse console (PAL 50Hz / NTSC 60Hz),
                   PAS le vblank RTG (qui peut etre 60Hz). On attend que CYCLES_PER_FRAME
                   cycles ARM se soient ecoules depuis le debut de cette frame. Spin ARM. */
                { u32 cpf = Pico.m.pal ? 13333333u : 11111111u;   /* ~666MHz / (50|60) */
                  u32 s=40000000u;
                  while((pmu_cyc() - g_frame_t0) < cpf && s--){ }
                  g_frame_t0 = pmu_cyc(); }
                /* Buffer safety (triple buffer) : le 68k ecrit FLIP_SEQ=seq des qu'il a
                   PRIS la frame (= "tu peux rendre la suivante", pas "affichee") et met
                   a jour SH_P96_BASE vers un buffer LIBRE. Grace a l'auto-cadence, le 68k
                   (RTG 60Hz > 50Hz) a deja pris la frame -> attente quasi nulle. */
                { u32 s=8000000u;
                  while(shared[ZZPICO_SH_FLIP_SEQ] != g_frame_seq && s--){ } }
            }
        }
        shared[ZZPICO_SH_HEARTBEAT]=++hb;
        __asm__ volatile("dsb":::"memory");
    }
stopped:
    shared[ZZPICO_SH_STATUS]=ZZPICO_ST_STOPPED;   /* 0xFF */
    __asm__ volatile("dsb":::"memory");
    for(;;){ __asm__ volatile("wfe"); }
}
