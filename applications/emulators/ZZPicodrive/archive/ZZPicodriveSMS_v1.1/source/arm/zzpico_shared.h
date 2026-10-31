/*
 * zzpico_shared.h - ZZPicoDrive shared contract (Core1 ARM blob <-> 68k)
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
#ifndef ZZPICO_SHARED_H
#define ZZPICO_SHARED_H

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
/* ARM private pool (ARM-only, not 68k-visible): */
#define ZZPICO_ARM_BSS      0x22000000UL   /* WB/WA, 4 MB (..0x22400000)         */
#define ZZPICO_ARM_BSS_END  0x22400000UL
#define ZZPICO_ARM_HEAP     0x22800000UL   /* WB/WA, 8 MB (..0x23000000)         */
#define ZZPICO_HEAP_SIZE    (8UL*1024UL*1024UL)

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
#define ZZPICO_SH_COUNT 160

#endif
