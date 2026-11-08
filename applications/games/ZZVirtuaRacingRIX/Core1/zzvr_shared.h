/*
 * zzvr_shared.h - ZZVirtuaRacingRIX shared contract (Core1 ARM blob <-> 68k)
 *
 * IDENTICAL to the public ZZPicoDriveMD 1.1 zzpico_shared.h for indexes 0..175
 * (the public launcher drives this blob unchanged).  Additions only:
 * ZZVR_SH_* at indexes 180.. (A1 diagnostics), ZZVR_SH_COUNT.
 * The ARM-private addresses (BSS/heap) of 1.1 are NOT used by this blob:
 * see zzvr_mem.h (XACP v1.7 Core1 arena).
 *
 * MEMORY AUTHORITY: XX19 / XACP v1.5 canonical map (NOT ZZDoom320).
 * ZZDoom is authority ONLY for the Core1 model (STOP, handshake, MMU/cache).
 * Old ZZDoom offsets (fb+0x04000000/0x04100000/0x04200000/0x05000000/0x05800000)
 * are RESERVED under XX19 for XACP/MP3/ZZMIDI/ZZMPEG/SF2 - NOT usable.
 *
 * Conversions: ARM = fb_offset + 0x00200000 ; fb = cd_BoardAddr + 0x10000.
 * Shared NC: ARM writes direct (no bswap); 68k rd32/wr32 byteswap, off = idx*4.
 * ASCII only.
 */
#ifndef ZZVR_SHARED_H
#define ZZVR_SHARED_H

#define ZZPICO_MAGIC        0x5A504943UL   /* 'ZPIC' */
#define ZZPICO_VERSION      1UL

/* ---- DDR map (XX19 / XACP v1.5) - single source of truth ---- */
/* 68k-visible framebuffer window: */
#define ZZPICO_ARM_BLOB     0x04500000UL   /* fb+0x04300000, WB/WA, 2 MB window  */
#define ZZPICO_ARM_SHARED   0x04700000UL   /* fb+0x04500000, NC,    1 MB         */
#define ZZPICO_ARM_ROM      0x04B00000UL   /* fb+0x04900000, WB/WA, up to 7 MB   */
#define ZZPICO_ARM_ROM_END  0x05200000UL
#define ZZPICO_ARM_VIDEO    0x05600000UL   /* fb+0x05400000, WB/WA (phase 4)     */
#define ZZPICO_ARM_AUDIO    0x05700000UL   /* fb+0x05500000, NC    (phase 5)     */
#define ZZPICO_ARM_DEBUG    0x05800000UL   /* fb+0x05600000, 2 MB debug/margin   */
/* ARM private memory (ARM-only, not 68k-visible): ZZVirtuaRacing uses the
 * XACP v1.7 Core1 arena only, see zzvr_mem.h.  The 1.1 private pool constants
 * (ZZPICO_ARM_BSS 0x22000000, ZZPICO_ARM_HEAP 0x22800000) are deliberately
 * removed from this header so that nothing can reference them. */

#define ZZPICO_FB_BLOB      0x04300000UL
#define ZZPICO_FB_SHARED    0x04500000UL
#define ZZPICO_FB_ROM       0x04900000UL
#define ZZPICO_ARM_FROM_FB(x)  ((x) + 0x00200000UL)
#define ZZPICO_FB_FROM_ARM(x)  ((x) - 0x00200000UL)

#define ZZPICO_BLOB_LIMIT   (ZZPICO_ARM_SHARED - 0x00010000UL)   /* 0x046F0000 */

/* Launch: run value = ZZPICO_ARM_BLOB -> HI=0x0450, LO=0x0000 */
#define ZZPICO_REG_ARM_RUN_HI   0x0090
#define ZZPICO_REG_ARM_RUN_LO   0x0092

#define ZZPICO_ROM_MAX      (4UL*1024UL*1024UL)   /* v0 (region allows 7 MB) */
#define ZZPICO_ROM_PAD      256UL

enum zzpico_status {
    ZZPICO_ST_BOOTING=0, ZZPICO_ST_READY=1, ZZPICO_ST_RUNNING=2,
    ZZPICO_ST_STOPPING=3, ZZPICO_ST_ERROR=0xEE, ZZPICO_ST_STOPPED=0xFF
};
enum zzpico_command {
    ZZPICO_CMD_NONE=0, ZZPICO_CMD_INIT=1, ZZPICO_CMD_LOAD_ROM=2,
    ZZPICO_CMD_START=3, ZZPICO_CMD_STOP=4, ZZPICO_CMD_STEP_FRAME=5,
    ZZPICO_CMD_SELFTEST=6   /* declenche une UDF -> teste les vecteurs (ERROR=8) */
};
enum zzpico_error {
    ZZPICO_ERR_NONE=0, ZZPICO_ERR_BAD_MAGIC=1, ZZPICO_ERR_ROM_NULL=2,
    ZZPICO_ERR_ROM_TOO_BIG=3, ZZPICO_ERR_ROM_CRC=4, ZZPICO_ERR_PICO_INIT=5,
    ZZPICO_ERR_DATA_ABORT=6, ZZPICO_ERR_PREFETCH_ABORT=7, ZZPICO_ERR_UNDEF_INSTR=8,
    ZZPICO_ERR_ROM_ODD=9
};
enum zzpico_fb_format { ZZPICO_FBFMT_RGB555=1, ZZPICO_FBFMT_RGB565=2, ZZPICO_FBFMT_ARGB8888=3 };

#define ZZPICO_SH_MAGIC 0
#define ZZPICO_SH_VERSION 1
#define ZZPICO_SH_STATUS 2
#define ZZPICO_SH_COMMAND 3
#define ZZPICO_SH_CMD_SEQ 4
#define ZZPICO_SH_ACK_SEQ 5
#define ZZPICO_SH_HEARTBEAT 6
#define ZZPICO_SH_FRAME_COUNTER 7
#define ZZPICO_SH_ERROR 8
#define ZZPICO_SH_DIAG 9
#define ZZPICO_SH_ROM_ADDR 10
#define ZZPICO_SH_ROM_SIZE 11
#define ZZPICO_SH_ROM_CRC_68K 12
#define ZZPICO_SH_ROM_CRC_ARM 13
#define ZZPICO_SH_ROM_FIRST4 14
#define ZZPICO_SH_ROM_LAST4 15
#define ZZPICO_SH_FB_ADDR 16
#define ZZPICO_SH_FB_WIDTH 17
#define ZZPICO_SH_FB_HEIGHT 18
#define ZZPICO_SH_FB_PITCH 19
#define ZZPICO_SH_FB_FORMAT 20
#define ZZPICO_SH_FRAME_READY 21
#define ZZPICO_SH_FLIP_SEQ 22
#define ZZPICO_SH_TIME_MS 23
#define ZZPICO_SH_FRAME_REQ 24
#define ZZPICO_SH_FRAME_DONE 25
#define ZZPICO_SH_PAD0 26
#define ZZPICO_SH_PAD1 27
#define ZZPICO_SH_PCM_BASE 30
#define ZZPICO_SH_PCM_SIZE 31
#define ZZPICO_SH_PCM_WRITE_POS 32
#define ZZPICO_SH_PCM_READ_POS 33
#define ZZPICO_SH_PCM_RATE 34
#define ZZPICO_SH_PCM_FORMAT 35
#define ZZPICO_SH_PCM_UNDERRUNS 36
#define ZZPICO_SH_PCM_ENABLED 37
#define ZZPICO_SH_PROF_FRAME_US 40
#define ZZPICO_SH_PROF_RENDER_US 41
#define ZZPICO_SH_PROF_DROPPED 42
/* Phase 3a : CRC du rendu + rectangle actif (evite 26/27/28 = dump de faute) */
#define ZZPICO_SH_FRAME_CRC_FULL   43
#define ZZPICO_SH_FRAME_CRC_ACTIVE 44
#define ZZPICO_SH_FRAME_CRC_ACC    45
#define ZZPICO_SH_VM_START_LINE    49
#define ZZPICO_SH_VM_LINE_COUNT    50
#define ZZPICO_SH_VM_START_COL     51
#define ZZPICO_SH_VM_COL_COUNT     52
#define ZZPICO_SH_VM_MODE_SEQ      53
#define ZZPICO_SH_MMU_BASE 48
#define ZZPICO_SH_DBG_OPT          54
#define ZZPICO_SH_DBG_SNDRATE      55
#define ZZPICO_SH_DBG_SNDOUT_NULL  56
#define ZZPICO_SH_DBG_SKIPFRAME    57
#define ZZPICO_SH_DBG_YM_STATUS    58
#define ZZPICO_SH_DBG_YM_MODE      59
#define ZZPICO_SH_DBG_VDP_REG1     60
#define ZZPICO_SH_DBG_CRAM_NZ      61
#define ZZPICO_SH_DBG_M68K_PC      62
#define ZZPICO_SH_DBG_PC_CHANGES   63
/* VISUAL_TIMING_CLEAN : region (68k->blob) + timing/centrage (blob->68k) */
#define ZZPICO_SH_REGION_IN        64   /* 68k->blob : 0=AUTO,4=USA,1=JAPN,8=EUR,2=JAPP */
#define ZZPICO_SH_PAL_OUT          65   /* blob->68k : Pico.m.pal effectif (0=NTSC,1=PAL) */
#define ZZPICO_SH_ACTIVE_H         66   /* blob->68k : hauteur active VDP (224/240)     */
#define ZZPICO_SH_Y_OFFSET         67   /* blob->68k : offset vertical applique (0/8)   */
#define ZZPICO_SH_REGION_EFF       68   /* blob->68k : regionOverride reellement pose    */
#define ZZPICO_SH_DBG_BUF_SENT     29
#define ZZPICO_SH_DBG_BUF_ZERO     38
#define ZZPICO_SH_DBG_BUF_NONZERO  39
#define ZZPICO_SH_DBG_FIRST_CHG    46
#define ZZPICO_SH_DBG_VDP_REG12    47
/* DIAG poll (p3a_diag_poll) : bloc 80-105, snapshot a FRAME_DONE */
#define ZZPICO_SH_POLL_VDP_READS    80
#define ZZPICO_SH_POLL_VDP_LAST     81
#define ZZPICO_SH_POLL_VDP_VB       82
#define ZZPICO_SH_POLL_VDP_HB       83
#define ZZPICO_SH_POLL_VDP_PC       84
#define ZZPICO_SH_POLL_BREQ_READS   85
#define ZZPICO_SH_POLL_BREQ_WRITES  86
#define ZZPICO_SH_POLL_BREQ_LASTW   87
#define ZZPICO_SH_POLL_BREQ_LASTR   88
#define ZZPICO_SH_POLL_BREQ_PC      89
#define ZZPICO_SH_POLL_ZRST_READS   90
#define ZZPICO_SH_POLL_ZRST_WRITES  91
#define ZZPICO_SH_POLL_ZRST_LASTW   92
#define ZZPICO_SH_POLL_ZRST_LASTR   93
#define ZZPICO_SH_POLL_ZRST_PC      94
#define ZZPICO_SH_POLL_IO_READS     95
#define ZZPICO_SH_POLL_IO_WRITES    96
#define ZZPICO_SH_POLL_IO_LASTADDR  97
#define ZZPICO_SH_POLL_IO_LASTR     98
#define ZZPICO_SH_POLL_IO_LASTW     99
#define ZZPICO_SH_POLL_IO_PC        100
#define ZZPICO_SH_POLL_VER_READS    101
#define ZZPICO_SH_POLL_PAD1_READS   102
#define ZZPICO_SH_POLL_PAD2_READS   103
#define ZZPICO_SH_POLL_RING_COUNT   104
#define ZZPICO_SH_POLL_RING_BASE    105
/* DIAG first-fault recorder (p3a_diag_fault) : 106-135 */
#define ZZPICO_SH_RESET_PC      106
#define ZZPICO_SH_RESET_SR      107
#define ZZPICO_SH_RESET_A7      108
#define ZZPICO_SH_RESET_ASP     109
#define ZZPICO_SH_F1_PC         110
#define ZZPICO_SH_F1_SR         111
#define ZZPICO_SH_F1_A7         112
#define ZZPICO_SH_F1_ASP        113
#define ZZPICO_SH_FAULT_CAPTURED 114
#define ZZPICO_SH_FAULT_FRAME   115
#define ZZPICO_SH_FAULT_VECT    116
#define ZZPICO_SH_FAULT_OLDPC   117
#define ZZPICO_SH_FAULT_OLDSR   118
#define ZZPICO_SH_FAULT_ASP     119
#define ZZPICO_SH_FAULT_D0      120
#define ZZPICO_SH_FAULT_A0      128
/* Garde-fous byteswap */
#define ZZPICO_SH_ROM_STATE     136   /* 0=RAW_BE, 1=SWAPPED_HOST */
#define ZZPICO_SH_POSTSWAP_SSP  137   /* attendu 0x00FFFE00 */
#define ZZPICO_SH_POSTSWAP_PC   138   /* attendu 0x00000206 */
/* Phase 3b : conversion ARGB8888 + CRC */
#define ZZPICO_SH_CRC_ARGB_FULL    139
#define ZZPICO_SH_CRC_ARGB_ACTIVE  140
#define ZZPICO_SH_ARGB_NONZERO     141
#define ZZPICO_SH_ARGB_FIRST_CHG   142
#define ZZPICO_SH_CONV_MODE        143
/* Phase 3d : rendu direct dans le framebuffer P96 (supprime la copie 68k).
 * Le 68k publie, chaque frame, l'adresse ARM + pitch du backbuffer courant ;
 * si P96_ENABLE=1 le blob convertit l'ARGB directement dedans (le format
 * 0xFFRRGGBB en ARM-LE donne les octets [B][G][R][A] attendus par le scanout,
 * deja valide en 3c fmt5). P96_ENABLE=0 => comportement 3b inchange. */
#define ZZPICO_SH_P96_ENABLE       144
#define ZZPICO_SH_P96_BASE         145   /* adresse ARM du backbuffer courant */
#define ZZPICO_SH_P96_PITCH        146   /* octets par ligne du backbuffer    */
/* Decoupe du rendu (cycles PMU CCNT, mode P96 rapide seulement) */
#define ZZPICO_SH_PERF_PICOFRAME   147   /* cycles PicoFrame()                 */
#define ZZPICO_SH_PERF_CONVERT     148   /* cycles boucle conversion 565->ARGB */
#define ZZPICO_SH_PERF_DCLEAN      149   /* cycles dcache_clean(buffer P96)    */
/* Benchmark ponctuel (frame 1) : ecriture 76800 mots, P96 vs FB3B cachee */
#define ZZPICO_SH_PERF_WCYC_P96    150   /* cycles ecriture vers buffer P96    */
#define ZZPICO_SH_PERF_WCYC_FB3B   151   /* cycles ecriture vers FB3B (cachee) */
#define ZZPICO_SH_PERF_DESC_P96    152   /* descripteur section buffer P96     */
#define ZZPICO_SH_PERF_DESC_FB3B   153   /* descripteur section FB3B (ref)     */
#define ZZPICO_SH_PERF_DESC_P96PRE 154   /* descripteur section P96 AVANT remap */
#define ZZPICO_SH_PERF_COPY        155   /* cycles copie bloc FB3B->P96 (fast)  */

/* NTSC_TIMING_DIAG : accumulateurs sur toute la session, cycles CCNT.
   PicoFrame min/max + somme 64 bits (hi/lo) + histogramme seuils 12/14/16.67 ms
   (a 666667 cyc/ms : 8000004 / 9333338 / 11111117) + max convert/clean. */
#define ZZPICO_SH_DIAG_PF_MIN      156
#define ZZPICO_SH_DIAG_PF_MAX      157
#define ZZPICO_SH_DIAG_PF_SUM_US   158   /* somme PicoFrame en us (cyc/667), u32 */
#define ZZPICO_SH_DIAG_RESV159     159   /* 32X v1.2 RC5: msh2.sr (low 16) | ssh2.sr (high 16), SR & 0x3f3 */
#define ZZPICO_SH_DIAG_FRAMES      160
#define ZZPICO_SH_DIAG_N_GT12      161
#define ZZPICO_SH_DIAG_N_GT14      162
#define ZZPICO_SH_DIAG_N_GT1667    163
#define ZZPICO_SH_DIAG_CONV_MAX    164
#define ZZPICO_SH_DIAG_CLEAN_MAX   165
#define ZZPICO_SH_DIAG_CONV_MIN    166   /* convert min (cycles)              */
#define ZZPICO_SH_DIAG_CONV_SUM_US 167   /* somme convert en us -> moy = /frames */
#define ZZPICO_SH_PCM_OVERRUNS     168   /* AUDIO_REAL : ARM ring plein (compte) */
#define ZZPICO_SH_PCM_TESTPAT      169   /* AUDIO_TEST : 1=tone L 440Hz (valide L/R+endian) */
/* ==== INPUT / CONTROLS (phase pad) : 68k ecrit, Core1 lit ==== */
/* pad joueur 1 = ZZPICO_SH_PAD0 (26) ; joueur 2 = ZZPICO_SH_PAD1 (27) [deja definis] */
#define ZZPICO_SH_INPUT_SEQ   170   /* incremente quand pad/emu change   */
#define ZZPICO_SH_EMU_CMD     171   /* commande emulateur (EMU_CMD_*)    */
#define ZZPICO_SH_EMU_ARG     172   /* argument (ex. slot savestate)     */
/* commandes emulateur (68k -> Core1, via SH_EMU_CMD, one-shot) */
#define EMU_CMD_NONE   0
#define EMU_CMD_MENU   1
#define EMU_CMD_QUIT   2
#define EMU_CMD_PAUSE  3
#define EMU_CMD_RESET  4
/* menu : Core1 -> 68k (action fichier/quit demandee par le menu) */
#define ZZPICO_SH_MENU_ACTION 78
#define ZZPICO_MENU_ACT_NONE  0
#define ZZPICO_MENU_ACT_SAVE  1
#define ZZPICO_MENU_ACT_LOAD  2
#define ZZPICO_MENU_ACT_QUIT  3
/* ==== SAUVEGARDES (modele ZZDoom) : 68k<->DDR<->Core1, 68k transporte les octets ==== */
#define ZZPICO_SAVE_BUF_ARM   0x07E00000UL   /* buffer d'echange DDR (adresse ARM) */
#define ZZPICO_SAVE_MAX       (1536UL*1024UL) /* ZZPicoDrive32X: a 32X state is ~680 KiB (SDRAM 256K +
                                                 DRAM 256K + MD state); zone 0x07E00000..0x08000000
                                                 (XACP_ARM_HIGH_SAVE_BASE .. CORE0_HEAP_BASE), 512 KiB kept */
#define ZZPICO_SH_SAVE_CMD    70   /* 68k->Core1 : commande (ZZPICO_SAVE_CMD_*) */
#define ZZPICO_SH_SAVE_SEQ    71   /* 68k incremente pour declencher            */
#define ZZPICO_SH_SAVE_ACK    72   /* Core1 recopie SEQ quand fini              */
#define ZZPICO_SH_SAVE_STATUS 73   /* Core1 : OK/ERR                            */
#define ZZPICO_SH_SAVE_SLOT   74   /* slot savestate (0..N)                     */
#define ZZPICO_SH_SAVE_SIZE   75   /* SAVE: taille produite ; LOAD: taille fournie */
#define ZZPICO_SH_SAVE_ERR    77   /* Core1 : detail erreur                     */
#define ZZPICO_SAVE_CMD_SRAM_SAVE   1
#define ZZPICO_SAVE_CMD_SRAM_LOAD   2
#define ZZPICO_SAVE_CMD_STATE_SAVE  3
#define ZZPICO_SAVE_CMD_STATE_LOAD  4
#define ZZPICO_SAVE_STATUS_OK   2
#define ZZPICO_SAVE_STATUS_ERR  3
/* bitmask interne pad (1 = presse) */
#define ZP_UP     (1u<<0)
#define ZP_DOWN   (1u<<1)
#define ZP_LEFT   (1u<<2)
#define ZP_RIGHT  (1u<<3)
#define ZP_A      (1u<<4)
#define ZP_B      (1u<<5)
#define ZP_C      (1u<<6)
#define ZP_START  (1u<<7)
#define ZP_X      (1u<<8)
#define ZP_Y      (1u<<9)
#define ZP_Z      (1u<<10)
#define ZP_MODE   (1u<<11)
#define ZP_SELECT (1u<<12)
#define ZP_MENU   (1u<<13)
#define ZZVR_SH_SH2_STATE      173   /* 32X v1.2 RC5: msh2.state | ssh2.state<<8 | msh2.pending_level<<16 | ssh2.pending_level<<24 */
#define ZZVR_SH_MSH2_POLL      174   /* 32X v1.2 RC5: msh2.poll_addr */
#define ZZVR_SH_SSH2_POLL      175   /* 32X v1.2 RC5: ssh2.poll_addr */
#define ZZPICO_SH_COUNT 176

/* ==== ZZVirtuaRacingRIX A1 additions (Core1 -> 68k, diagnostics; read-only for the 68k) ==== */
#define ZZVR_SH_DRAWMODE       176   /* 68k -> Core1 (optional): 0 = draw every frame, publish when the
                                        P96 buffer is free (ZZPicoDrive32X rc1); 1 = 1.1 gating (draw
                                        only when the previous frame was taken) for A/B tests        */
#define ZZVR_SH_PUB_IMMEDIATE  177   /* v1.2: frames published at the end of PicoFrame             */
#define ZZVR_SH_PUB_DEFERRED   178   /* v1.2: frames published from the pacing wait                */
#define ZZVR_SH_PUB_DROPPED    179   /* v1.2: frames overwritten in FB3A before publication        */
#define ZZVR_SH_MAGIC          180   /* 'ZVR1' = 0x5A565231 : identifies a ZZVirtuaRacing blob */
#define ZZVR_SH_BUILD          181   /* 0x0A1A = A1a (SH-2 interpreter), 0x0A1B = A1b (DRC)      */
#define ZZVR_SH_AHW            182   /* PicoIn.AHW (bit PAHW_32X = 0x04 when the ROM is 32X)      */
#define ZZVR_SH_X32_REG0       183   /* Pico32x.regs[0] (ADEN bit0, nRES bit1, FM bit15)          */
#define ZZVR_SH_MSH2_PC        184   /* master SH-2 pc at frame end                               */
#define ZZVR_SH_SSH2_PC        185   /* slave SH-2 pc at frame end                                */
#define ZZVR_SH_MMAP_USED      186   /* plat_mmap bytes handed out (Pico32xMem...)                */
#define ZZVR_SH_SBRK_USED      187   /* _sbrk bytes handed out                                    */
#define ZZVR_SH_PF_W_AVG_US    188   /* PicoFrame average over the last 600 frames (~10 s), us   */
#define ZZVR_SH_PF_W_MAX_US    189   /* PicoFrame maximum over the same window, us                */
#define ZZVR_SH_PF_LAST_US     190   /* PicoFrame of the last frame, us                           */
#define ZZVR_SH_VFP            191   /* 1 = VFP enabled in core1_entry (FPEXC.EN read back)       */
#define ZZVR_SH_DRC            192   /* A1b: 1 = recompiler compiled in and enabled, 0 = interpreter */
#define ZZVR_SH_MMAP_FAIL      193   /* plat_mmap refusals (must stay 0)                          */
#define ZZVR_SH_X32_VDP0       194   /* Pico32x.vdp_regs[0] (mode bits 0-1: 0 blank,1 packed,2 direct,3 RLE) */
#define ZZVR_SH_OSD            195   /* 68k -> Core1 (optional): 0 = OSD on (default), 1 = OSD off */
#define ZZVR_SH_W_FRAMES       196   /* frames in the PicoFrame window (<= 600)                   */
#define ZZVR_SH_W_DRAWN        197   /* of which drawn (the others ran with skipFrame = 1)        */
#define ZZVR_SH_ENTRY_SP       198   /* SP at core1_entry (expected 0x05FFFFxx: firmware stack)    */
#define ZZVR_SH_BUILD2         199   /* sub-build: 1 = A1a/A1b, 2 = A1a2/A1b2 (stack cached), 3 = A1x3, 4 = ZZPicoDrive32X rc1, 5 = v1.2 RC1-RC4, 6 = v1.2 RC5, 7 = ZZVirtuaRacingRIX A2a */
#define ZZVR_SH_COUNT          200

/* ZZVirtuaRacingRIX A2: the HLE of VRDE following the game (Core1/zzvr_hle.c).
   Times in us (PMU cycles / 666), sums since START. */
#define ZZVR_SH_HLE_FIRST          232
#define ZZVR_SH_HLE_MAGIC          232   /* 'HLEC' HLE + check, 'HLE-' HLE without check, 'OFF!' HLE off  */
#define ZZVR_SH_HLE_CTRL           233   /* 68k -> Core1 before START: bit0 = HLE off, bit1 = check off,
                                            bit2 = RasterIX off (A3), bit3 = RasterIX only (A3 debug: the 3D
                                            pixels the GPU leaves empty shown black), bit4 = no A5 patches,
                                            bit5 = no A6 patches (the SH-2s draw the race as before),
                                            bit6 = no A7 (one GPU image per game tick, laid over by Core1)  */
#define ZZVR_SH_HLE_SCENES         234   /* race scenes the 68000 sent (hle_scene_count)                  */
#define ZZVR_SH_HLE_BUILDS         235   /* polygon lists built (timed)                                    */
#define ZZVR_SH_HLE_BUILD_US_SUM   236   /* time in hle_on_dreq_write for those, sum                       */
#define ZZVR_SH_HLE_BUILD_US_MAX   237   /*   longest                                                      */
#define ZZVR_SH_HLE_PICTURES       238   /* race pictures put on screen by the 32X with their polygon list */
#define ZZVR_SH_HLE_POLY_SUM       239   /* polygons in those lists, sum                                   */
#define ZZVR_SH_HLE_POLY_MAX       240   /*   most in one picture                                          */
#define ZZVR_SH_HLE_CMD_LAST       241   /* command of the last picture (2 race, 5 demo)                    */
#define ZZVR_SH_HLE_CHK_PICTURES   242   /* pictures compared (something drawn)                            */
#define ZZVR_SH_HLE_CHK_IDENTICAL  243   /*   identical to the SH-2s' frame buffer, every pixel            */
#define ZZVR_SH_HLE_CHK_BELOW90    244   /*   below 90 % of pixels equal                                   */
#define ZZVR_SH_HLE_CHK_LOWEST     245   /* lowest share of equal pixels, 1/100 % (10000 = all)            */
#define ZZVR_SH_HLE_CHK_LOWEST_FRAME 246 /*   at that frame (counted from START)                           */
#define ZZVR_SH_HLE_CHK_PIX        247   /* pixels compared, sum                                           */
#define ZZVR_SH_HLE_CHK_SAME       248   /*   of which equal                                               */
#define ZZVR_SH_HLE_CHK_US_SUM     249   /* fill + compare time, sum                                       */
#define ZZVR_SH_HLE_CHK_US_MAX     250   /*   longest                                                      */
#define ZZVR_SH_HLE_NOTES          251   /* hle.c messages (unexpected model size / depth mode)            */
#define ZZVR_SH_HLE_NOTE_LAST      252   /*   the last one: kind << 24 | value                             */
#define ZZVR_SH_HLE_LAST           252

/* ZZVirtuaRacingRIX A3: the HLE's 3D drawn by RasterIX (Core1/zzvr_rix.c) and laid
   over the 32X picture.  Times in us, sums since START. */
#define ZZVR_SH_A5_STATE           253   /* A5b: 0x4135xxxx on (flag noa5: 0), bit0 slave idle patch, bit1 fill delay patch */
#define ZZVR_SH_A5_APPLIED         254   /* A5b: patches written (re-applied if the game reloads its code)  */
#define ZZVR_SH_A6_STATE           255   /* A6a: 0x4136xxxx on (flag noa6: 0), bit0 race command patched, bit1 demo command patched */
#define ZZVR_SH_A6_APPLIED         225   /* A6a: patch writes (re-applied if the game reloads its code)      */
#define ZZVR_SH_A6_CLEARS          226   /* A6a: frame buffer 3D lines cleared (one per race scene)         */
#define ZZVR_SH_A6_TICK_SUM        227   /* A6a: frames between consecutive race scenes, sum (the game's tick) */
#define ZZVR_SH_A6_TICK_N          228   /* A6a:   ticks counted                                           */
#define ZZVR_SH_A6_TICK_3          229   /* A6a:   of which 3 frames long (the original cadence)            */
#define ZZVR_SH_A6_TICK_MAX        230   /* A6a:   longest                                                 */
#define ZZVR_SH_A6_SIG             231   /* A6b: words read at 06000D06 (high) and 06000D1C (low), every frame   */
#define ZZVR_SH_A6_SLAVE_CMDS      201   /* A6b: master -> slave commands (COMM4) while the 68000's command is 2/5 */
#define ZZVR_SH_A7_STATE           202   /* A7a: 0x4137xxxx direct (60 images a second), bit0 on, bit1 PL330 failed (CPU copy since) */
#define ZZVR_SH_A7_PICTURES        203   /* A7a: images drawn into a prefilled buffer (one per frame in a race) */
#define ZZVR_SH_A7_PREFILL_US_SUM  204   /* A7a: Core1 prefill of the GPU buffer (FB3A doubled) + L1 clean, sum */
#define ZZVR_SH_A7_PREFILL_US_MAX  205
#define ZZVR_SH_A7_DMA_US_SUM      206   /* A7a: publication waiting for the PL330 copy of the 3D lines, sum */
#define ZZVR_SH_A7_DMA_US_MAX      207
#define ZZVR_SH_A7_INTERP_US_SUM   208   /* A7a: hle_interpolated() per frame, sum                        */
#define ZZVR_SH_A7_INTERP_US_MAX   209
#define ZZVR_SH_A7_HUD_SUM         210   /* A7a: HUD pixels written over the GPU's picture, sum          */
#define ZZVR_SH_A7_DMA_ERR         211   /* A7a: last PL330 error (RIXE_*), 0 = none                     */
#define ZZVR_SH_A7_FPI2            212   /* A7a: 68k -> Core1 before START: frames per image x 2 (2 = 60/s, 3 = 40/s, 4 = 30/s; 0 = 2) */
#define ZZVR_SH_A7_SUBMIT_US_SUM   213   /* A7a: descriptors + submission per image, sum                 */
#define ZZVR_SH_A7_SUBMIT_US_MAX   214
#define ZZVR_SH_A7_HELD            215   /* A7b: frames that kept the image on screen (same interpolated list) */
#define ZZVR_SH_A8_FAR             216   /* A8a: 68k -> Core1 before START: far view 0 = default (on, haze), 1 off, 2 on with haze, 3 on without haze */
#define ZZVR_SH_A8_TILES           217   /* A8a: 68k -> Core1: far view tiles (0 = default 8; 4..16)               */
#define ZZVR_SH_A8_DETAIL          218   /* A8a: 68k -> Core1: detailed opponents 0 = default (on), 1 off, 2 on       */
#define ZZVR_SH_A8_POLY_SUM        219   /* A8a: polygons of the interpolated lists drawn, sum (per A7 image)      */
#define ZZVR_SH_A8_POLY_MAX        220
#define ZZVR_SH_A8_HAZED_SUM       221   /* A8a: of which faded by the haze, sum                                   */
#define ZZVR_SH_A8_STATE           222   /* A8a: 0x4138 << 16 | far (0 off, 1 haze, 2 no haze) | detail << 4 | tiles << 8 */
#define ZZVR_SH_A8_HAZE            223   /* A8a: haze start | end << 16, world units                               */
#define ZZVR_SH_A8_GOV             224   /* A8a: frames that kept the image because Core1 was behind (PCM ring low) */
#define ZZVR_SH_A8_SKY_FRAMES      121   /* A8b: images drawn over the sky moved with the camera (vrde/sky.c)  */
#define ZZVR_SH_A8_SKY_OFF         122   /* A8b: images over the emulator's sky (plane B not what the console shows) */
#define ZZVR_SH_A8_SKY_REBUILDS    123   /* A8b: sky cache rebuilds (plane B's map, tiles or colours changed)   */
#define ZZVR_SH_A8_SKY_BLENDROWS   124   /* A8b: sky rows blended (8 a frame after each rebuild)               */
#define ZZVR_SH_A8_SKY_PLANE       125   /* A8b: plane B's size, width | height << 16                           */
#define ZZVR_SH_A8_SKY_MODE        126   /* A8b: 68k -> Core1: 0 = sky moved and blended, 1 off (flag nosky), 2 not blended (flag noblend) */
#define ZZVR_SH_A8_WHEELS          127   /* A8b: 68k -> Core1: 0 = wheels strobe at racing speed, 1 off (flag nowheels) */
#define ZZVR_SH_RIX_FIRST          256
#define ZZVR_SH_RIX_MAGIC          256   /* 'RIX3' on, 'NORX' off (flag norix / HLE off), 'FAIL' init failed  */
#define ZZVR_SH_RIX_INIT_RC        257   /* rix_init() result (0 = ok, RIXE_* else); 0xE1xx                   */
#define ZZVR_SH_RIX_CAPS           258   /* rix_caps() after init (RIXCAP_*)                                  */
#define ZZVR_SH_RIX_CAL            259   /* colour calibration: chosen mapping 1..3 (0 = none exact) | worst<<8  */
#define ZZVR_SH_RIX_CAL_ERR0       260   /* calibration: wrong pixels of mapping 1, 2, 3 (192 pixels each)    */
#define ZZVR_SH_RIX_CAL_ERR1       261
#define ZZVR_SH_RIX_CAL_ERR2       262
#define ZZVR_SH_RIX_PICTURES       263   /* race pictures drawn by RasterIX                                  */
#define ZZVR_SH_RIX_TRIS_SUM       264   /* triangles sent, sum                                              */
#define ZZVR_SH_RIX_TRIS_MAX       265   /*   most in one picture                                            */
#define ZZVR_SH_RIX_BUILD_US_SUM   266   /* Core1: mask + descriptors + submission, per picture, sum         */
#define ZZVR_SH_RIX_BUILD_US_MAX   267
#define ZZVR_SH_RIX_WAIT_US_SUM    268   /* Core1 waiting for the picture's fence (next frame), sum          */
#define ZZVR_SH_RIX_WAIT_US_MAX    269
#define ZZVR_SH_RIX_READ_US_SUM    270   /* reading the GPU picture back into the overlay, sum               */
#define ZZVR_SH_RIX_READ_US_MAX    271
#define ZZVR_SH_RIX_COMP_FRAMES    272   /* frames the overlay was laid over the 32X picture                 */
#define ZZVR_SH_RIX_COMP_US_SUM    273
#define ZZVR_SH_RIX_COMP_US_MAX    274
#define ZZVR_SH_RIX_PIX            275   /* first frame of each picture: pixels replaced by the GPU's, sum   */
#define ZZVR_SH_RIX_PIX_SAME       276   /*   of which already equal to the SH-2s' pixel                     */
#define ZZVR_SH_RIX_LOWEST         277   /* lowest share equal in one picture, 1/100 % (10000 = all)         */
#define ZZVR_SH_RIX_LOWEST_FRAME   278   /*   at that frame                                                  */
#define ZZVR_SH_RIX_ERR            279   /* last GPU error (RIXE_* of frame begin/tri/end/wait), 0 = none    */
#define ZZVR_SH_RIX_ERR_COUNT      280   /* GPU errors (RasterIX switched off after the first)               */
#define ZZVR_SH_RIX_L2_US          281   /* init: L2 clean + invalidate of the GPU window                    */
#define ZZVR_SH_RIX_TIMER_HZ       282   /* global timer frequency seen by librixgpu                         */
#define ZZVR_SH_RIX_SKIPPED        283   /* pictures not drawn (GPU off after an error)                      */
#define ZZVR_SH_RIX_NOPIX          284   /* first frame of each picture: 3D pixels the GPU left empty, sum   */
#define ZZVR_SH_RIX_RETRY_RC       285   /* A3a2: second rix_init() after a failed first one: 0x10000 | rc   */
#define ZZVR_SH_RIX_RETRY_CAPS     286   /*   its caps                                                       */
#define ZZVR_SH_RIX_MAPS_US_SUM    287   /* A3a4: of BUILD, the HUD and index maps, sum                   */
#define ZZVR_SH_RIX_LAST           287
#define ZZVR_SH_RIX_MAPS_US_MAX    313   /* A3a4:   longest (after the CHKLOG entries 289..312)            */
#define ZZVR_SH_OUT_SCALE          314   /* A3b: 68k -> Core1 before START: 2 = 640 x 480 P96 screen (FB3A
                                            doubled, the RasterIX 3D at 640 x 448), else 320 x 240 */
#define ZZVR_SH_SH2_MS_US          316   /* A5a: time running the master SH-2 inside PicoFrame, us, sum      */
#define ZZVR_SH_SH2_SS_US          317   /* A5a:   the slave SH-2 (the 3D fill), us, sum                     */
#define ZZVR_SH_SH2_FRAMES         318   /* A5a:   frames summed                                           */
#define ZZVR_SH_SH2_SS_MAX         319   /* A5a:   slave, longest frame, us                                 */
#define ZZVR_SH_OUT_FMT            315   /* A3b2: 68k -> Core1 before P96_ENABLE: pixel format of the P96
                                            buffers at 640 x 480: 0 ARGB8888 (A3b1), 1 RGB565 little-endian
                                            (R5G6B5PC), 2 RGB565 big-endian (R5G6B5, bytes swapped) */
/* A3a2: librixgpu's diagnostic block (rix_diag.h SD_*, 192 words) after the first
   rix_init(): ID, init ladder L0..L15 (4 words per stage at SD_LADDER = 48), fences */
#define ZZVR_SH_RIXSD_FIRST        320
#define ZZVR_SH_RIXSD_COUNT        192
/* A3: the first pictures of the check (flag check) that are not identical: frame,
   command | view_top << 8 | view_bottom << 16, equal share in 1/100 % | polygons << 16 */
#define ZZVR_SH_CHKLOG_N           288   /* entries written (at most 8)                                      */
#define ZZVR_SH_CHKLOG_FIRST       289   /* 3 words per entry, 289..312                                      */
#define ZZVR_SH_CHKLOG_MAX         8

#define ZZVR_MAGIC             0x5A565231UL
#define ZZVR_BUILD_A1A         0x0A1AUL   /* A1a2: 0x0A1A2 is not representable in 16 bits: see ZZVR_SH_BUILD2 */
#define ZZVR_BUILD_A1B         0x0A1BUL

#endif /* ZZVR_SHARED_H */
