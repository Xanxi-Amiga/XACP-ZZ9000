#ifndef ZZS_GAME_H
#define ZZS_GAME_H
/*
 * zzs_game.h - everything that is specific to ONE CPS-1 game. ASCII only.
 *
 * Game: Strider - MAME set "strider", "Strider (USA, B-Board 89624B-2)", the parent set.
 * (There is no "World" set of Strider: the parent is this US board.)
 *
 * Sources checked for every value below:
 *   MAME    src/mame/capcom/cps1.cpp   ROM_START( strider ), cps1_10MHz, main_map, sub_map
 *           src/mame/capcom/cps1.h     CPS_PIXEL_CLOCK, CPS_HTOTAL, CPS_VTOTAL, CPS_VBSTART
 *           src/mame/capcom/cps1_v.cpp {"strider", CPS_B_01, mapper_ST24M1}
 *   FBA     src/burn/drv/capcom/d_cps1.cpp  StriderRomDesc, StriderInit
 * To add another CPS-1 game later (Final Fight, Ghouls'n Ghosts, Willow, Magic Sword, SF2):
 * a new copy of this header plus its ROM table in zzs_rom.c; cps1/cps_config.cpp already
 * carries every CPS-B id and graphics mapper.
 */

#define ZZS_GAME_SET_NAME     "strider"

/* clocks, all "verified on pcb" in MAME */
#define ZZS_M68K_CLOCK        10000000u     /* XTAL 10 MHz (cps1_10MHz) */
#define ZZS_Z80_CLOCK         3579545u      /* XTAL 3.579545 MHz */
#define ZZS_YM_CLOCK          3579545u      /* YM2151 on the same crystal as the Z80 */
#define ZZS_OKI_CLOCK         1000000u      /* 16 MHz / 4 / 4 */
#define ZZS_OKI_PIN7_HIGH     1             /* reset value; the Z80 can change it at 0xF006 */

/* screen: 8 MHz pixel clock, 512 x 262 total -> 59.637405 Hz, 384 x 224 visible */
#define ZZS_PIXEL_CLOCK       8000000u
#define ZZS_HTOTAL            512u
#define ZZS_VTOTAL            262u
#define ZZS_FRAME_PIXELS      (ZZS_HTOTAL * ZZS_VTOTAL)                  /* 134144 */
/* cycles per frame = clock * 134144 / 8000000 */
#define ZZS_M68K_CYCLES_FRAME 167680        /* exact for 10 MHz */
#define ZZS_PACE_CYCLES_FRAME 11178667u     /* 666666687 Hz Cortex-A9 PMU clock, rounded */

/* B-board configuration */
#define ZZS_CPS_B_ID          CPS_B_01
#define ZZS_GFX_MAPPER        mapper_ST24M1
#define ZZS_HAS_STARS         1

/* decoded ROM store */
#define ZZS_PRG_SIZE          0x00100000u
#define ZZS_GFX_SIZE          0x00400000u
#define ZZS_STAR_SIZE         0x00002000u   /* 2 fields x 0x1000 bytes, appended to the gfx */
#define ZZS_Z80_SIZE          0x00010000u
#define ZZS_OKI_SIZE          0x00040000u

#define ZZS_STORE_GFX         (ZZS_ARM_ROMSTORE + 0x00000000u)   /* 0x22400000, 4 MiB + stars */
#define ZZS_STORE_PRG         (ZZS_ARM_ROMSTORE + 0x00500000u)   /* 0x22900000 */
#define ZZS_STORE_Z80         (ZZS_ARM_ROMSTORE + 0x00600000u)   /* 0x22A00000 */
#define ZZS_STORE_OKI         (ZZS_ARM_ROMSTORE + 0x00610000u)   /* 0x22A10000 */
#define ZZS_STORE_USED_END    (ZZS_ARM_ROMSTORE + 0x00650000u)

/* DIP switch defaults, RAW port values (active low), from MAME INPUT_PORTS_START( strider ):
 *   A = 0xFF  1 coin / 1 credit on both slots, upright 1 player
 *   B = 0x8D  difficulty 2 (the level recommended by the manual), unused switch off,
 *             bonus "30K & 60K only", internal difficulty on life loss "default"
 *   C = 0xFF  3 lives, no freeze, no free play, no flip, demo sounds on, continue allowed, game
 */
#define ZZS_DIP_A_DEFAULT     0xFFu
#define ZZS_DIP_B_DEFAULT     0x8Du
#define ZZS_DIP_C_DEFAULT     0xFFu

#endif /* ZZS_GAME_H */
