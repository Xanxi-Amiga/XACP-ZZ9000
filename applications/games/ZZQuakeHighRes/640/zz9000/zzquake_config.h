/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef ZZQUAKE_CONFIG_H
#define ZZQUAKE_CONFIG_H

#define ZZQ_ARM_FB_DELTA    0x00200000UL

#define ZZQ_BLOB_ARM        0x04900000UL
#define ZZQ_BLOB_LIMIT_ARM  0x04B00000UL

#define ZZQ_SHARED_ARM      0x04B00000UL
#define ZZQ_SHARED_SIZE     0x00001000UL

#define ZZQ_PCM_ARM         0x04B80000UL
#define ZZQ_PCM_SIZE        0x00080000UL

#define ZZQ_PAK_ARM         0x04500000UL
#define ZZQ_PAK_MAX_SIZE    (26UL*1024UL*1024UL)

#define ZZQ_STACK_TOP       0x06000000UL
#define ZZQ_STACK_FLOOR     0x05F00000UL

#define ZZQ_HEAP_ARM        0x07000000UL
#define ZZQ_HEAP_SIZE_A0    (16UL*1024UL*1024UL)
#define ZZQ_HUNK_SIZE       (12UL*1024UL*1024UL)

#define ZZQ_PROBE_START     0x08000000UL
#define ZZQ_PROBE_END       0x09000000UL

#define ZZQ_BLOB_FB         (ZZQ_BLOB_ARM   - ZZQ_ARM_FB_DELTA)
#define ZZQ_SHARED_FB       (ZZQ_SHARED_ARM - ZZQ_ARM_FB_DELTA)
#define ZZQ_PCM_FB          (ZZQ_PCM_ARM    - ZZQ_ARM_FB_DELTA)
#define ZZQ_PAK_FB          (ZZQ_PAK_ARM    - ZZQ_ARM_FB_DELTA)

#define ZZQ_MAGIC           0x5A5A514BUL

#define SH_MAGIC            0
#define SH_STATUS           1
#define SH_HB               2
#define SH_CMD              3
#define SH_FRAME            4
#define SH_DIAG             5
#define SH_ERROR            6
#define SH_ERR4             7
#define SH_PAK_ADDR         8
#define SH_PAK_SIZE         9
#define SH_FB_ADDR          10
#define SH_FB_PITCH         11
#define SH_FB_WIDTH         12
#define SH_FB_HEIGHT        13
#define SH_INPUT            14
#define SH_MOUSE_DX         15
#define SH_MOUSE_BTN        16

#define SH_SP_NOW           19
#define SH_FRAME_READY      20
#define SH_FLIP_SEQ         21
#define SH_ENABLE_QG        22

#define SH_FPS_X100         23
#define SH_FLOAT_DIAG       24
#define SH_PROBE_RESULT     25
#define SH_PROBE_FAIL_ADDR  26
#define SH_HUNK_SIZE        27
#define SH_HEAP_USED        28
#define SH_ENABLE_PROBE     29

#define SH_PAK_MAGIC_SEEN   30

#define SH_PAK_DIR_OFS      17
#define SH_PAK_DIR_COUNT    18
#define SH_PAK_OPEN_COUNT   31
#define SH_PAK_READ_COUNT   32
#define SH_PAK_SEEK_COUNT   33
#define SH_PAK_BYTES_READ   34
#define SH_NOTFOUND_BENIGN  35
#define SH_FILE_FATAL       36
#define SH_PALETTE_COUNT    37
#define SH_KEY_EVENTS       38
#define SH_QG_TICK_US       39
#define SH_DRAW_US          40
#define SH_CONVERT_US       41
#define SH_FRAME_TOTAL_US   42
#define SH_ENTRY_SP         43

#define SH_LAST_FILE_REQ    44

#define SH_LAST_FILE_LEN    16
#define SH_LAST_ERR_MSG     60
#define SH_LAST_ERR_LEN     16
#define SH_LAST_PRINTF      76
#define SH_LAST_PRINTF_LEN  16

#define SH_FAULT_MARK       92
#define SH_FAULT_FAR        93
#define SH_FAULT_FSR        94
#define SH_FAULT_PC         95
#define SH_FAULT_SPSR       96
#define SH_FAULT_R0         97
#define SH_TEST_UDF         98

#define SH_ENABLE_A1        99

#define SH_A1_RESULT        100
#define SH_A1_LUMP_POS      101
#define SH_A1_LUMP_LEN      102
#define SH_A1_FIRST8_LO     103
#define SH_A1_FIRST8_HI     104
#define SH_A1_CRC32         105

#define SH_LAST_STDIO_OP    106
#define SH_STDIO_OP_COUNT   107
#define SH_DEMO_BLOCKED     108
#define SH_ENABLE_DEMO_OFF  109
#define SH_HUNK_BASE        110
#define SH_NO_CACHEFIX      112

#define SH_HUNK_MB          111

#define SH_WAD_BASE_A3      113
#define SH_WAD_TABOFS       114
#define SH_WAD_LUMPS_A3     115
#define SH_WAD_NUMLUMPS     116
#define SH_WAD_UNAL_COUNT   117
#define SH_WAD_UNAL_IDX     118
#define SH_WAD_UNAL_POS     119
#define SH_WAD_PIC_A3       120
#define SH_WAD_PIC_W_NAT    121
#define SH_WAD_PIC_W_SAFE   122
#define SH_WAD_PIC_H_NAT    123
#define SH_WAD_PIC_H_SAFE   124
#define SH_WAD_PROBE_DONE   125
#define SH_ENABLE_FBTEST    126

#define SH_FBTEST_WRITES    127

#define SH_GT_CTRL          128
#define SH_GT_T0_LO         129
#define SH_GT_T0_HI         130
#define SH_GT_T1_LO         131
#define SH_GT_T1_HI         132
#define SH_GT_DELTA         133
#define SH_GT_PMCR          134

#define SH_FBPROBE_ADDR     135
#define SH_FBPROBE_WROTE    136
#define SH_FBPROBE_READ     137

#define SH_BIGRD_COUNT      138
#define SH_BIGRD_POS        139
#define SH_BIGRD_W0         140
#define SH_BIGRD_W1         141
#define SH_BIGRD_NUMSKINS   142
#define SH_BIGRD_NUMVERTS   143
#define SH_BIGRD_NUMTRIS    144
#define SH_BIGRD_NUMFRAMES  145
#define SH_BIGRD_DONE       146
#define SH_FBPROBE_ACK      147

#define SH_MDL_FILEPOS      150
#define SH_MDL_LEN          151
#define SH_MDL_SRC          152
#define SH_MDL_DST          153
#define SH_MDL_S_BASE       154

#define SH_MDL_D0_BASE      162
#define SH_MDL_D1_BASE      170
#define SH_MDL_CRC_S        178
#define SH_MDL_CRC_D0       179
#define SH_MDL_CRC_D1       180
#define SH_MDL_VALID        181

#define SH_SV_ACTIVE        182
#define SH_DEMOPLAYBACK     183
#define SH_DEMONUM          184

#define SH_ERR_RET0         185
#define SH_ERR_RET1         186

#define SH_BT_BASE          187
#define SH_BT_COUNT         195

#define SH_FTEST_STR        196
#define SH_FTEST_BACK       200
#define SH_CV_MAXSURFS      201
#define SH_CV_MAXEDGES      202
#define SH_CV_CNUMSURFS     203

#define SH_HUNKF_NAME       204
#define SH_HUNKF_RAW        208
#define SH_HUNKF_ADJ        209
#define SH_HUNKF_LOW        210
#define SH_HUNKF_HIGH       211
#define SH_HUNKF_SIZE       212
#define SH_ALIAS_STAGE      213

#define SH_CV1_SURFS_STR    214
#define SH_CV1_SURFS_RAW    218
#define SH_CV1_EDGES_RAW    219
#define SH_CV2_SURFS_RAW    220
#define SH_CV2_EDGES_RAW    221
#define SH_FTEST_RET        222
#define SH_ENABLE_CVARFIX   223

#define SH_SURFCACHE_KB     224

#define SH_TD_DONE          225
#define SH_TD_FRAMES        226
#define SH_TD_TIME_MS       227
#define SH_TD_FPS_X100      228
#define SH_ENABLE_TIMEDEMO  229

#define SH_EXIT_REASON      230
#define ZZQ_EXIT_QG_QUIT      1
#define ZZQ_EXIT_CMD_STOP     2
#define ZZQ_EXIT_CREATE_RET   3
#define ZZQ_EXIT_FATAL        4
#define ZZQ_EXIT_MODE_END     5
#define SH_LAST_CMD         231

#define SH_LAST_CMD_LEN      12
#define SH_CMD_COUNT        243
#define SH_SURFCACHE_GOT    244

#define SH_RUNNING_AT_LOOP  245

#define SH_CK_ENTRY         246
#define SH_CK_AFTER_BSS     247
#define SH_CK_AFTER_MMU     248
#define SH_CK_BEFORE_QG     249
#define SH_CK_CANARY        250

#define SH_ENTRY_LR         251

#define SH_RETURN_OK        252

#define ZZQ_RETURN_OK       0xC0DEBACCUL

#define SH_RETURN_OK2       253
#define ZZQ_RETURN_OK2      0xC0DEBAD1UL

#define SH_IMG_SIZE         254
#define SH_IMG_SUM_68K      255
#define SH_IMG_SUM_ARM      256

#define SH_SNAP_A           257
#define SH_SNAP_B           261
#define SH_SNAP_C           265
#define SH_ENABLE_PASSIVE   269

#define SH_ENABLE_COOP      270

#define SH_ENABLE_RESTORE   271

#define SH_IMG_SUM_ARM2     272

#define SH_DB_ENABLE        273
#define SH_DB_WAITS         274
#define SH_DB_WAIT_US       275
#define SH_DB_WAIT_MAX      276
#define SH_DB_TIMEOUTS      277
#define SH_DB_REQ           278
#define SH_DB_ACK           279
#define SH_DB_ACK_TMO       280
#define SH_DB_NOFLIP        281
#define SH_TIMEDEMO_NOWAIT  282

#define SH_ENGINE_MAXFPS    283

#define SH_FPS_PEAK_X100    284

#define SH_WORK_US          285

#define SH_WORK_MAX         286
#define SH_WAIT_LAST_US     287

#define SH_LAT_READY_PAN    288
#define SH_LAT_PAN_ACK      289
#define SH_LAT_ACK_REL      290
#define SH_LAT_PAN_ACK_MAX  291
#define SH_VBL_AT_PAN       292

#define SH_READY_SEEN       293

#define SH_D1_US            294
#define SH_D2_US            295
#define SH_D1_MAX           296
#define SH_D2_MAX           297
#define SH_POLL_COUNT       298

#define SH_FLIP_READBACK    299

#define SH_D1_SUM_MS        300
#define SH_D2_SUM_MS        301
#define SH_DN_COUNT         302
#define SH_POLL_SUM_K       303
#define SH_POLL_BACKOFF     304

#define SH_BUILD_ID         305

#define ZZQ_BUILD_ID        143
#define SH_CFLUSH_MODE      306

#define SH_VIS_TOKEN        307
#define SH_VIS_GO           308
#define SH_VIS_ROUND        309
#define SH_VIS_DONE         310
#define SH_ENABLE_VISTEST   311
#define SH_VIS_BULK         312

#define SH_VIS_CLEAN_US     313

#define SH_RENDER_SEQ       314
#define SH_RENDER_FB        315
#define SH_RENDER_DONE      316
#define SH_TRIPLE           317
#define SH_GATE_PASS        318
#define SH_GATE_BLOCK       319
#define SH_FB_USED          320
#define SH_FB_REJECTS       321
#define SH_FB_LAST_BAD      322

#define SH_HEAVY_FRAMES     323
#define SH_RENDERS          324

#define ZZQ_FLOAT_OK        0xF10A70CBUL

#define ZZQ_OP_NONE     0
#define ZZQ_OP_FOPEN    1
#define ZZQ_OP_FSEEK    2
#define ZZQ_OP_GETC     3
#define ZZQ_OP_FREAD    4
#define ZZQ_OP_FCLOSE   5
#define ZZQ_OP_FTELL    6
#define ZZQ_OP_SYSOPEN  7
#define ZZQ_OP_SYSREAD  8
#define ZZQ_OP_SYSSEEK  9

#define ZZQ_MARK_DATA_ABT   0xBADD0DABUL
#define ZZQ_MARK_PREFETCH   0xBADB00AFUL
#define ZZQ_MARK_UNDEF      0xBADB00DDUL

#define ZZQ_INIT_START          0x01
#define ZZQ_PAK_OK              0x02
#define ZZQ_FS_OK               0x03
#define ZZQ_QG_CREATE_START     0x04
#define ZZQ_QG_CREATE_OK        0x05
#define ZZQ_PALETTE_OK          0x06
#define ZZQ_FIRST_FRAME_OK      0x07
#define ZZQ_RUNNING             0x08
#define ZZQ_QUIT_REQUESTED      0x09
#define ZZQ_A0_IDLE             0x0A
#define ZZQ_STOPPED             0xFF

#define ZZQ_FATAL_NO_PAK        0x80
#define ZZQ_FATAL_BAD_PAK       0x81
#define ZZQ_FATAL_QG_CREATE     0x82
#define ZZQ_FATAL_OUT_OF_MEM    0x83
#define ZZQ_FATAL_SYS_ERROR     0x84
#define ZZQ_FATAL_PAK_TOO_BIG   0x85

#define ZZQ_FATAL_UNDEFINED     0x86
#define ZZQ_FATAL_PREFETCH_ABT  0x87
#define ZZQ_FATAL_DATA_ABT      0x88
#define ZZQ_FATAL_BAD_FILE      0x89

#define ZZQ_CMD_NONE            0
#define ZZQ_CMD_STOP            1

#define BTN_UP          0x00000001UL
#define BTN_DOWN        0x00000002UL
#define BTN_LEFT        0x00000004UL
#define BTN_RIGHT       0x00000008UL
#define BTN_SL          0x00000010UL
#define BTN_SR          0x00000020UL
#define BTN_FIRE        0x00000040UL
#define BTN_USE         0x00000080UL
#define BTN_RUN         0x00000100UL
#define BTN_ESC         0x00000200UL
#define BTN_ENTER       0x00000400UL
#define BTN_Y           0x00000800UL
#define BTN_STRAFE_MOD  0x00001000UL
#define BTN_MAP         0x00002000UL
#define BTN_N           0x00004000UL
#define BTN_TILDE       0x00008000UL
#define BTN_W1          0x00010000UL
#define BTN_W2          0x00020000UL
#define BTN_W3          0x00040000UL
#define BTN_W4          0x00080000UL
#define BTN_W5          0x00100000UL
#define BTN_W6          0x00200000UL
#define BTN_W7          0x00400000UL
#define BTN_W8          0x00800000UL
#define BTN_F1          0x01000000UL
#define BTN_F2          0x02000000UL
#define BTN_F3          0x04000000UL
#define BTN_F4          0x08000000UL
#define BTN_F5          0x10000000UL
#define BTN_F6          0x20000000UL
#define BTN_F10         0x40000000UL
#define BTN_PAUSE       0x80000000UL

#endif

#define ZZQ_KEYQ_SIZE       64

#define SH_MODAL_STAGE      325
#define SH_MODAL_GATEBLOCK  326
#define SH_MODAL_LOOPS      327
#define SH_MODAL_LASTKEY    328
#define SH_MODAL_FLAGS      329

#define SH_KEYQ_WR          330
#define SH_KEYQ_RD          331
#define SH_KEYQ_OVER        332
#define SH_KEYQ_BASE        334

#define ZZQ_KEV(down, key)  (((down) ? 0x10000UL : 0UL) | ((key) & 0xFFFFUL))
#define ZZQ_KEV_DOWN(v)     (((v) & 0x10000UL) ? 1 : 0)
#define ZZQ_KEV_KEY(v)      ((int)((v) & 0xFFFFUL))

#define QK_TAB        9
#define QK_ENTER      13
#define QK_ESCAPE     27
#define QK_SPACE      32
#define QK_BACKSPACE  127
#define QK_UPARROW    128
#define QK_DOWNARROW  129
#define QK_LEFTARROW  130
#define QK_RIGHTARROW 131
#define QK_ALT        132
#define QK_CTRL       133
#define QK_SHIFT      134
#define QK_F1         135
#define QK_DEL        148
#define QK_MOUSE1     200
#define QK_MOUSE2     201
#define QK_MOUSE3     202
#define QK_PAUSE      255

#define SH_MOUSE_TOT_X      398
#define SH_MOUSE_TOT_Y      399
#define SH_MOUSE_EVENTS     400
#define SH_MOUSE_RAWMAX_X   401
#define SH_MOUSE_RAWMAX_Y   402
#define SH_MOUSE_SCLMAX_X   403
#define SH_MOUSE_SCLMAX_Y   404
#define SH_MOUSE_SCALE      405

#define SH_PCM_ENABLE       406
#define SH_PCM_BASE_SLOT    407
#define SH_PCM_SIZE_SLOT    408
#define SH_PCM_WRITE_POS    409
#define SH_PCM_READ_POS     410
#define SH_PCM_RATE         411
#define SH_PCM_UNDERRUNS    412
#define SH_PCM_FILL_MIN     413
#define SH_PCM_FILL_MAX     414

#define SH_HIST_BASE        415
#define ZZQ_HIST_BINS       12

#define ZZQ_PH_COUNT        8
#define SH_PROF_LIGHT       427
#define SH_PROF_HEAVY       435
#define SH_PROF_MAX         443
#define SH_PROF_NLIGHT      451
#define SH_PROF_NHEAVY      452

#define ZZQ_S2_COUNT        7
#define SH_S2_LIGHT         453
#define SH_S2_HEAVY         460

#define SH_N_NORMAL         467
#define SH_N_SKY            468
#define SH_N_TURB           469
#define SH_N_BACK           470
#define SH_N_SUBMODEL       471
#define SH_N_SPANS          472
#define SH_N_SPANPIX_K      473

#define ZZQ_FILEBUF_ARM     0x06E00000UL
#define ZZQ_FILEBUF_SIZE    0x00080000UL
#define ZZQ_FILEBUF_SLOTS   4
#define ZZQ_FILEBUF_TOTAL   (ZZQ_FILEBUF_SIZE * ZZQ_FILEBUF_SLOTS)
#define ZZQ_FILEBUF_AT(i)   (ZZQ_FILEBUF_ARM + (u32)(i) * ZZQ_FILEBUF_SIZE)
#define ZZQ_FNAME_MAX       64
#define ZZQ_FILEBUF_FB      (ZZQ_FILEBUF_ARM - ZZQ_ARM_FB_DELTA)

#define SH_FS_CMD           474
#define SH_FS_SEQ           475
#define SH_FS_ACK           476
#define SH_FS_SIZE          477
#define SH_FS_ERR           478
#define SH_FS_OFFSET        479
#define SH_FS_CHUNK         480
#define SH_FS_EXISTS        481
#define SH_FS_NAME          482
#define SH_FS_SLOT          506
#define SH_FS_OPENFAIL      507
#define SH_FS_CFG_READ      508
#define SH_FS_CFG_BYTES     509
#define SH_FS_OPENS         510
#define SH_FS_CLOSES        511
#define SH_FS_SLOTMASK      512
#define SH_FS_SLOTPEAK      513

#define FS_CMD_WRITE        1
#define FS_CMD_READ         2
#define FS_CMD_STAT         3

#define ZZFS_OK             0
#define ZZFS_ERR_OPEN       1
#define ZZFS_ERR_TOO_LARGE  2
#define ZZFS_ERR_TIMEOUT    3
#define ZZFS_ERR_WRITE      4
#define ZZFS_ERR_READ       5

#define SH_LG_ENTNUM        498
#define SH_LG_NUMEDICTS     499
#define SH_LG_MAXEDICTS     500
#define SH_LG_EDICTSPTR     501
#define SH_LG_EDICTSIZE     502
#define SH_LG_BADPTR        503
#define SH_LG_BADB          504
#define SH_LG_CALLER        505

#define SH_WATER_TURB       514
#define SH_WATER_WARP       515
#define SH_WATER_TURBIDX    516
#define SH_WATER_CLTIME     517
#define SH_WATER_VRECT      518
#define SH_WATER_STAGE      519

#define SH_CK_A_TEXT        520
#define SH_CK_A_DATA        521
#define SH_CK_B_TEXT        522
#define SH_CK_B_DATA        523
#define SH_CK_C_TEXT        524
#define SH_CK_C_DATA        525
#define SH_CK_D_TEXT        526
#define SH_CK_D_DATA        527

#define SH_MS_C_SBRKBASE    528
#define SH_MS_C_AV0         529
#define SH_MS_C_AV1         530
#define SH_MS_C_AV2         531
#define SH_MS_C_TRIM        532
#define SH_MS_C_IMPURE      533
#define SH_MS_C_HEAPPTR     534
#define SH_MS_D_SBRKBASE    535
#define SH_MS_D_AV0         536
#define SH_MS_D_AV1         537
#define SH_MS_D_AV2         538
#define SH_MS_D_TRIM        539
#define SH_MS_D_IMPURE      540
#define SH_MS_D_HEAPPTR     541
#define SH_SBRK_FIRST       542
#define SH_SBRK_INCR        543

#define SH_BSSPROBE_PRE     544
#define SH_BSSPROBE_POST    545
#define ZZQ_BSSPROBE_MAGIC  0x5A11EDEDUL

#define SH_TEXT_LEN         546
#define SH_DATA_LEN         547

#define SH_DDS_AFTER_SETUP  548
#define SH_DDS_BEFORE_CALL  549
#define SH_DDS_EXPECTED     550
#define SH_DDS_MISMATCH     551

#define SH_W_ENTER          552
#define SH_W_EXIT           553
#define SH_W_SP_ENTER       554
#define SH_W_LR_ENTER       555
#define SH_W_SP_EXIT        556

#define SH_CRC_A_FULL       557
#define SH_CRC_B_FULL       558

#define SH_FLT_LR_USR       559
#define SH_FLT_SP_USR       560
#define SH_FLT_R0           561
#define SH_FLT_R1           562
#define SH_FLT_R2           563
#define SH_FLT_R3           564
#define SH_FLT_R12          565

#define SH_FLT_STK0         566
#define SH_FLT_STK1         567
#define SH_FLT_STK2         568
#define SH_FLT_STK3         569

#define SH_WARP_VID         570
#define SH_WARP_MAX         571
#define SH_WARP_CONST       572
#define SH_WARP_SBLINES     573
#define SH_WARP_ACTIVE      574
#define SH_WARP_VRECT       575
#define SH_WARP_SCRW        576
#define SH_WARP_ISBUF       577

#define SH_FSQ_TOTAL        578
#define SH_FSQ_TIMEOUT      579
#define SH_FSQ_ERR68K       580
#define SH_FSQ_WAIT_MAX     581
#define SH_FSQ_WAIT_SUM_MS  582

#define SH_FSQ_H0           583
#define SH_FSQ_H1           584
#define SH_FSQ_H2           585
#define SH_FSQ_H3           586
#define SH_FSQ_H4           587

#define SH_FSQ_LASTFAIL     588

#define SH_FSS_POLLS        604
#define SH_FSS_HANDLED      605
#define SH_FSS_GAP_MAX      606

#define SH_CLEAN_MODE       607
#define SH_CLEAN_ENTERED    608
#define SH_CLEAN_MALLOC     609
#define SH_CLEAN_B_SBRK     610
#define SH_CLEAN_B_AV1      611
#define SH_CLEAN_B_AV2      612
#define SH_CLEAN_B_HEAP     613
#define SH_CLEAN_A_SBRK     614
#define SH_CLEAN_A_AV1      615
#define SH_CLEAN_A_AV2      616
#define SH_CLEAN_DONE       617

#define SH_CLN_ENTERED      618
#define SH_CLN_B_SBRK       619
#define SH_CLN_B_AV1        620
#define SH_CLN_B_AV2        621
#define SH_CLN_B_HEAP       622
#define SH_CLN_MALLOC       623
#define SH_CLN_A_SBRK       624
#define SH_CLN_A_AV1        625
#define SH_CLN_A_AV2        626
#define SH_CLN_DONE         627
#define ZZQ_CLN_MAGIC_IN    0xC1EA4E11UL
#define ZZQ_CLN_MAGIC_OUT   0xC1EA4D02UL

#define ZZQ_PASSIVE_POSTCLEAN 2
#define SH_PC_ENTERED       628
#define SH_PC_B_SBRK        629
#define SH_PC_B_AV1         630
#define SH_PC_B_AV2         631
#define SH_PC_B_HEAP        632
#define SH_PC_MALLOC        633
#define SH_PC_A_SBRK        634
#define SH_PC_A_AV1         635
#define SH_PC_A_AV2         636
#define SH_PC_DONE          637
#define ZZQ_PC_MAGIC_IN     0xC1EAB001UL
#define ZZQ_PC_MAGIC_OUT    0xC1EAB002UL

#define ZZQ_PACK_ARENA_BASE 0x30000000UL
#define ZZQ_PACK_ARENA_END  0x3F800000UL

#define ZZQ_STAGING_ARM     0x04500000UL
#define ZZQ_STAGING_SIZE    (4UL*1024UL*1024UL)
#define ZZQ_STAGING_FB      (ZZQ_STAGING_ARM - ZZQ_ARM_FB_DELTA)

#define SH_PRELOAD_STATE    638
#define SH_PRELOAD_SRC      639
#define SH_PRELOAD_DST      640
#define SH_PRELOAD_SIZE     641
#define SH_PRELOAD_SEQ      642
#define SH_PRELOAD_ACK      643
#define SH_PRELOAD_ERR      644
#define SH_PACK_COUNT       645
#define SH_PACK0_ADDR       646
#define SH_PACK0_SIZE       647
#define SH_PACK1_ADDR       648
#define SH_PACK1_SIZE       649
#define ZZQ_PRELOAD_COPY    0x50434F50UL
#define ZZQ_PRELOAD_READY   0x50524459UL
#define ZZQ_PRELOAD_START   0x50535452UL
#define ZZQ_PRELOAD_ERROR   0x50455252UL

#define SH_PACK2_ADDR       650
#define SH_PACK2_SIZE       651
#define SH_GAME_MODE        652
#define ZZQ_GAME_ID1        0
#define ZZQ_GAME_HIPNOTIC   1
#define ZZQ_GAME_ROGUE      2

#define SH_MP_ARGC          653
#define SH_MP_MODE_SEEN     654
#define SH_MP_OPENS         655
#define SH_MP_PROBES        656

#define ZZQ_MODPACK_MAX     4
#define SH_MOD_COUNT        657
#define SH_MOD_ADDR         658
#define SH_MOD_SIZE         662
#define SH_MOD_NAME         666
#define ZZQ_GAME_MOD        3

#define SH_CD_INIT          682
#define SH_CD_PLAY          683
#define SH_CD_TRACK         684
#define SH_CD_LOOP          685
#define SH_CD_STOP          686
#define SH_CD_PAUSE         687
#define SH_CD_RESUME        688
#define SH_CD_UPDATE        689
#define SH_CD_SHUTDOWN      690
#define SH_CD_TRACKMASK     691

#define ZZQ_MUSQ_SIZE       8
#define SH_MUSQ_WR          692
#define SH_MUSQ_RD          693
#define SH_MUSQ_BASE        694
#define SH_MUSQ_END         (SH_MUSQ_BASE + ZZQ_MUSQ_SIZE * 2)

#define ZZQ_MUS_NONE        0
#define ZZQ_MUS_PLAY        1
#define ZZQ_MUS_STOP        2
#define ZZQ_MUS_PAUSE       3
#define ZZQ_MUS_RESUME      4
#define ZZQ_MUS_VOLUME      5
#define ZZQ_MUS_SHUTDOWN    6

#define SH_CD_HOST_OK       710
#define SH_CD_HOST_ERR      711
#define SH_CD_HOST_TRACK    712
#define SH_CD_HOST_LOOPS    713
#define SH_CD_VOLMUTE       714

#define SH_MUSIC_ENABLE     715

#define SH_PMU_PMCR         716
#define SH_PMU_CNTEN        717
#define SH_PMU_C0           718
#define SH_PMU_C1           719
#define SH_PMU_GT0          720
#define SH_PMU_GT1          721

#define SH_STAGE            722
#define ZZQ_STG_NONE        0
#define ZZQ_STG_SHUTSRV_IN  1
#define ZZQ_STG_SHUTSRV_OUT 2
#define ZZQ_STG_CLEARMEM    3
#define ZZQ_STG_SPAWN_IN    4
#define ZZQ_STG_MODLOAD     5
#define ZZQ_STG_NEWMAP      6
#define ZZQ_STG_SPAWN_OUT   7
#define ZZQ_STG_LOOP        8

#define SH_MODAL_WAITS      723
#define SH_MODAL_TIMEOUTS   724
#define SH_FB_BPP           725
#define ZZQ_SLOT_COUNT      726

#define ZZQ_PCM_RING_SIZE   0x10000UL
#define ZZQ_PCM_RATE        22050UL
#define ZZQ_PCM_TARGET_FILL 16384UL
