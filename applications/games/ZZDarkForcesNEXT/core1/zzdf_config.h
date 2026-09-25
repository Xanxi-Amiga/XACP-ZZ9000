/*
 * zzdf_config.h - ZZDarkForces NEXT Core1 memory map + shared protocol.
 * Authoritative addresses per project brief (XX19b firmware, XACP 1.7).
 * ASCII only.
 */
#ifndef ZZDF_CONFIG_H
#define ZZDF_CONFIG_H

/* ---- Memory map (ARM addresses) ---- */
#define ZZDF_ARM_FB_DELTA    0x00200000UL  /* ARM = fb_offset + this  */
#define ZZDF_BLOB_ARM        0x04900000UL
#define ZZDF_BLOB_LIMIT_ARM  0x04B00000UL  /* 2MB reserve, ld ASSERT  */
#ifndef ZZDF_SHARED_ARM               /* host tests override these   */
#define ZZDF_SHARED_ARM      0x04B00000UL  /* 4KB, NC                 */
#endif
#define ZZDF_SHARED_SIZE     0x00001000UL
#ifndef ZZDF_STAGING_ARM              /* host tests override these   */
#define ZZDF_STAGING_ARM     0x04500000UL  /* 4MB, NC                 */
#define ZZDF_STAGING_SIZE    (4UL*1024UL*1024UL)
#endif
#define ZZDF_STACK_TOP       0x06000000UL  /* firmware SP base        */
#define ZZDF_STACK_FLOOR     0x05F00000UL  /* 1MB reserve, keep free  */
 






















#ifndef ZZDF_ASSET_ARENA_BASE         /* host tests override these   */
#define ZZDF_ASSET_ARENA_BASE 0x30000000UL
#define ZZDF_ASSET_ARENA_END  0x3F800000UL /* exclusive; guard above  */
#endif
/* the super-zone IS the asset arena bound (host tests: fake arena) */
#define ZZDF_HIGH_DDR_BASE    ZZDF_ASSET_ARENA_BASE
#define ZZDF_HIGH_DDR_END     ZZDF_ASSET_ARENA_END
#define ZZDF_GUARD_BASE       0x3F800000UL
#define ZZDF_GUARD_END        0x3FC00000UL /* firmware from here      */
#define ZZDF_HEAP_END         ZZDF_HIGH_DDR_END
#define ZZDF_HEAP_MIN         (64UL*1024UL*1024UL) /* refuse below    */
#define ZZDF_HEAP_ALIGN       (1024UL*1024UL)      /* 1 MiB           */
#define ZZDF_LEGACY_HEAP_ARM  0x07000000UL /* mapped, no longer used  */
#define ZZDF_LEGACY_HEAP_SIZE (16UL*1024UL*1024UL)

#define ZZDF_BLOB_FB       (ZZDF_BLOB_ARM    - ZZDF_ARM_FB_DELTA)
#define ZZDF_SHARED_FB     (ZZDF_SHARED_ARM  - ZZDF_ARM_FB_DELTA)
#define ZZDF_STAGING_FB    (ZZDF_STAGING_ARM - ZZDF_ARM_FB_DELTA)

#define ZZDF_MAGIC         0x5A5A4446UL  /* "ZZDF" */

/* ---- Shared memory slots (u32 index into shared[]) ----
 * Core protocol slots keep the ZZQuake numbering where the launcher
 * mechanics are identical, so the launcher code ports 1:1.
 */
#define SH_MAGIC           0
#define SH_STATUS          1   /* 0xAA run, 0xFF stopped              */
#define SH_HB              2   /* heartbeat, ARM increments           */
#define SH_CMD             3   /* 0=none 1=stop (68k writes)          */
#define SH_FRAME           4   /* engine frame count                  */
#define SH_DIAG            5   /* progress breadcrumbs 0xD0xx         */
#define SH_ERROR           6
#define SH_ERR4            7   /* first 4 chars of fatal text         */
#define SH_MANIFEST_ADDR   8   /* 68k: ARM address of file manifest   */
#define SH_MANIFEST_COUNT  9   /* 68k: number of manifest entries     */
#define SH_FB_ADDR         10  /* 68k updates after each flip         */
#define SH_FB_PITCH        11
#define SH_FB_WIDTH        12
#define SH_FB_HEIGHT       13
#define SH_INPUT           14  /* button bitfield, 68k writes         */
#define SH_MOUSE_DX        15  /* signed, accumulated                 */
#define SH_MOUSE_BTN       16  /* write ONCE before loop (ZZDoom)     */
#define SH_MOUSE_DY        17  /* signed, accumulated                 */
#define SH_KEYRING_HEAD    18  /* key event ring producer (68k)       */
#define SH_SP_NOW          19  /* live SP published each heartbeat    */
#define SH_FRAME_READY     20  /* ARM: frame in back buffer           */
#define SH_FLIP_SEQ        21  /* 68k: completed flip seq             */
#define SH_ENABLE_GAME     22   
#define SH_FPS_X100        23
#define SH_FLOAT_DIAG      24  /* ZZDF_FLOAT_OK after VFP self-test   */
#define SH_KEYRING_TAIL    25  /* key event ring consumer (ARM)       */
#define SH_HEAP_USED       28  /* _sbrk high-water mark               */
#define SH_ARG_LEVEL       30  /* 68k: level index 1..14, 0=default   */
#define SH_PROBE_RESULT    31   
#define SH_PROBE_DETAIL    32   
#define SH_RENDERERS_SEEN  33   
#define SH_CPP_CTORS       34   
#define SH_TIME_LO         35  /* ARM publishes gtimer us (debug)     */
#define SH_LOG_HEAD        36  /* console log ring producer (ARM)     */
#define SH_ENABLE_RESTORE  271  

/* Session cookies prevent a second Core1 launch in the same machine session. */
#define SH_SESSION_COOKIE0 240
#define SH_SESSION_COOKIE1 241
#define ZZDF_COOKIE0_VAL   0x5A44F001UL
#define ZZDF_COOKIE1_VAL   0xA5BB0FFEUL
#define SH_EXIT_MARK       253 /* 0xC0DEBAD1 written by return stub   */

 

















#define ZZDF_SH_SCALARS_OFF  0x000
#define ZZDF_SH_SCALARS_SIZE 0x400

#define ZZDF_SH_RESV1_OFF    0x400   /* holds SH_ENABLE_RESTORE      */
#define ZZDF_SH_RESV1_SIZE   0x100

#define ZZDF_KEYRING_OFF     0x500
#define ZZDF_KEYRING_LEN     64      /* entries, u32: bit8 up, code  */
#define ZZDF_KEYRING_SIZE    (ZZDF_KEYRING_LEN * 4)

/* Fault dump area, written by zzdf_abort.S. The launcher reads these
 * offsets to print a fault report. Offsets are BYTES from SHARED. */
#define ZZDF_FAULT_OFF       0x600
#define ZZDF_FAULT_SIZE      0x100
#define ZZDF_OFF_MARK        (ZZDF_FAULT_OFF + 0x00) /* BADD0DAB etc */
#define ZZDF_OFF_FAR         (ZZDF_FAULT_OFF + 0x04)
#define ZZDF_OFF_FSR         (ZZDF_FAULT_OFF + 0x08)
#define ZZDF_OFF_PC          (ZZDF_FAULT_OFF + 0x0C)
#define ZZDF_OFF_SPSR        (ZZDF_FAULT_OFF + 0x10)
#define ZZDF_OFF_LR_USR      (ZZDF_FAULT_OFF + 0x14)
#define ZZDF_OFF_SP_USR      (ZZDF_FAULT_OFF + 0x18)
#define ZZDF_OFF_R0          (ZZDF_FAULT_OFF + 0x1C)
#define ZZDF_OFF_R1          (ZZDF_FAULT_OFF + 0x20)
#define ZZDF_OFF_R2          (ZZDF_FAULT_OFF + 0x24)
#define ZZDF_OFF_R3          (ZZDF_FAULT_OFF + 0x28)
#define ZZDF_OFF_R12         (ZZDF_FAULT_OFF + 0x2C)
#define ZZDF_OFF_STK0        (ZZDF_FAULT_OFF + 0x30)
#define ZZDF_OFF_STK1        (ZZDF_FAULT_OFF + 0x34)
#define ZZDF_OFF_STK2        (ZZDF_FAULT_OFF + 0x38)
#define ZZDF_OFF_STK3        (ZZDF_FAULT_OFF + 0x3C)

#define ZZDF_SH_RESV2_OFF    0x700
#define ZZDF_SH_RESV2_SIZE   0x100

/* Console log ring: ARM prints into a ring at shared+0x800. */
#define ZZDF_LOGRING_OFF     0x800
#define ZZDF_LOGRING_SIZE    0x7F0

/* Fault marks (ZZDF_OFF_MARK), same values as ZZQuake */
#define ZZDF_MARK_DABT       0xBADD0DABUL
#define ZZDF_MARK_PABT       0xBADB00AFUL
#define ZZDF_MARK_UNDF       0xBADB00DDUL

/* Fatal codes (SH_ERROR) */
#define ZZDF_FATAL_SYS_ERROR    0xE0000001UL
#define ZZDF_FATAL_OUT_OF_MEM   0xE0000002UL
#define ZZDF_FATAL_CPP_ABORT    0xE0000003UL
#define ZZDF_FATAL_PURECALL     0xE0000004UL

#define ZZDF_FLOAT_OK           0xF10A70Cu

 





#define ZZDF_E_GAME_ENTRY       0xE000
#define ZZDF_E_MANIFEST_HDR     0xE010
#define ZZDF_E_MANIFEST_ENT     0xE011
#define ZZDF_E_ARCHIVES_MOUNTED 0xE020
#define ZZDF_E_DF_INIT          0xE030
#define ZZDF_E_LEVELS_LOADED    0xE040
#define ZZDF_E_SECBASE_REQ      0xE050
#define ZZDF_E_SECBASE_LOADED   0xE060
#define ZZDF_E_PLAYER_INIT      0xE070
#define ZZDF_E_FIRST_UPDATE_IN  0xE080
#define ZZDF_E_FIRST_UPDATE_OUT 0xE081
#define ZZDF_E_FIRST_FB         0xE090
#define ZZDF_E_FIRST_PRESENT    0xEA00
#define ZZDF_E_STEADY_LOOP      0xEB00

 
#define ZZDF_FATAL_NO_MANIFEST  0xE0001001UL
#define ZZDF_FATAL_BAD_MANIFEST 0xE0001002UL
#define ZZDF_FATAL_NO_ARCHIVES  0xE0001003UL
#define ZZDF_FATAL_DF_INIT      0xE0001004UL
#define ZZDF_FATAL_NO_LEVEL     0xE0001005UL
#define ZZDF_FATAL_NO_FRAME     0xE0001006UL
#define ZZDF_FATAL_SETTINGS     0xE0001007UL
#define ZZDF_FATAL_PRELOAD      0xE0001008UL
#define ZZDF_FATAL_HEAP_BOUNDS  0xE0001009UL /* SH_HEAP_BASE/END bad   */

 
#define SH_FB_CRC32        40  /* CRC32 of the first indexed frame    */
#define SH_PAL_CRC32       41  /* CRC32 of the 256-entry palette      */
#define SH_FB_NONUNIFORM   42  /* distinct byte values in frame 1     */
#define SH_FB_SAMPLE0      43  /* sampled pixels, 4 per slot          */
#define SH_FB_SAMPLE1      44
#define SH_FB_SAMPLE2      45
#define SH_FB_SAMPLE3      46
#define SH_MOUNTED_COUNT   47  /* archives the engine actually opened */

/* Preload service: the 68k stages 64 KiB chunks and Core1 copies them
 * into the validated high-DDR asset arena. */
#define SH_PRELOAD_STATE   48
#define SH_PRELOAD_SRC     49
#define SH_PRELOAD_DST     50
#define SH_PRELOAD_SIZE    51
#define SH_PRELOAD_SEQ     52
#define SH_PRELOAD_ACK     53
#define SH_PRELOAD_ERR     54
#define SH_PRELOAD_COUNT   55  /* chunks copied, for the report        */
#define SH_PRELOAD_BYTES   56  /* bytes copied, for the report         */
#define SH_HEAP_BASE       57  /* 68k: align_up(asset_end, 1 MiB)     */
#define SH_HEAP_END        58  /* 68k: 0x3F800000, checked by _sbrk   */
#define SH_HEAP_BREAK      59  /* ARM: current sbrk pointer (abs)     */

 





#define SH_ENGINE_FPS_X100   SH_FPS_X100
#define SH_PRESENT_FPS_X100  60  /* presents per second x100, ARM      */
#define SH_GAME_US_AVG       61  /* us per engine frame (loop+task), ARM*/
#define SH_PRESENT_US_AVG    62  /* us per 8->32 present, ARM           */
#define SH_PRESENT_DROPPED   63  /* engine frames not presented because */
                                 /* the previous PAN was not acked, ARM */
#define SH_PAN_ACK_TIMEOUTS  64  /* 250 ms slices without PAN ACK, 68k  */
#define SH_PRESENT_COUNT     65  /* total presents, ARM                 */
#define SH_FB_YOFF           66  /* launcher: rows above the 320x200    */

/* Optional return-path state and captured firmware context. */
#define SH_RETURN_STATE      67  /* ZZDF_RET_* progress of the teardown */
#define SH_FW_SP             68  /* incoming SP (core1_trampoline)      */
#define SH_FW_LR             69  /* incoming LR                         */
#define SH_FW_SCTLR          70  /* firmware SCTLR at entry             */
#define SH_FW_CPSR           71  /* firmware CPSR at entry              */
#define SH_FW_TTBR0          72
#define SH_FW_VBAR           73
#define SH_FW_FPEXC          74  /* original FPEXC (before our enable)  */
#define SH_FW_CPACR          75  /* original CPACR                      */
#define SH_FW_FRAME_SP       76  /* [0x00010000]: SP saved by core1_loop*/
/* 77..79 RETIRED: the read-only L2 probe. Three runs of the same code
 * reported ratios 72 / 244 / 153 - not an exploitable measurement. The
 * functional JuliaV2 test is the real proof. Slots left reserved so no
 * future counter silently reuses them in an old launcher's view. */
#define SH_RETIRED_L2_0      77
#define SH_RETIRED_L2_1      78
#define SH_RETIRED_L2_2      79

 








#define SH_LOGIC_STEPS       80  /* loopGame() calls                    */
#define SH_TASK_STEPS        81  /* task_run() != 0                     */
#define SH_RENDER_FRAMES     82  /* updateVirtualDisplay() calls        */
#define SH_LOGIC_FPS_X100    83  /* loopGame() per second x100          */
#define SH_RENDER_FPS_X100   84  /* renderer frames per second x100     */
#define SH_CONV_US_LAST      85  /* last 8->32 conversion, us           */
#define SH_CONV_US_MAX       86  /* worst 8->32 conversion, us          */
#define SH_READY_GAP_US_AVG  87  /* mean us between two FRAME_READY     */
#define SH_READY_GAP_US_MAX  88  /* worst us between two FRAME_READY    */
#define SH_FLIPWAIT_US_AVG   89  /* mean us swap() spent blocked by an  */
                                 /* unacknowledged PAN (dropped path)   */
#define SH_GAME_US_MAX       90  /* worst loopGame()+task_run(), us     */

 















#define SH_RENDER_FB         91
#define SH_RENDER_SEQ        92

 










#define SH_MOUSE_SENS        93
#define ZZDF_MOUSE_SENS_DEF  40
#define ZZDF_MOUSE_SENS_MIN  5
#define ZZDF_MOUSE_SENS_MAX  300

/* Filesystem counters: unresolved opens and rejected subdirectory requests. */
#define SH_FS_MISS           94
#define SH_FS_DIRREJ         95

 












#define SH_PACER_ON          96
#define SH_PACER_WAIT_US     97   /* mean us spent waiting for a slot */

 








































#define ZZDF_PCM_ARM        0x04400000UL
#define ZZDF_PCM_SIZE       0x00010000UL   /* 64 KiB, see note above   */
#define ZZDF_PCM_MASK       (ZZDF_PCM_SIZE - 1UL)
#define ZZDF_PCM_FB         (ZZDF_PCM_ARM - ZZDF_ARM_FB_DELTA)
#define ZZDF_PCM_RATE       11025U         /* iMuse native, no resample*/
#define ZZDF_PCM_CHANNELS   2U
/* Stereo frames per mix block. 256 is not a free choice: ImUpdateWave()
 * asserts bufferSize*2 <= AUDIO_BUFFER_SIZE (512) and upstream calls it
 * with exactly AUDIO_CALLBACK_BUFFER_SIZE = 256. */
#define ZZDF_PCM_BLOCK      256U
#define ZZDF_PCM_BLOCK_BYTES (ZZDF_PCM_BLOCK * ZZDF_PCM_CHANNELS * 2U)

#define SH_AUDIO_ON         98  /* 68k: 1 = AHI is open, produce audio  */
#define SH_PCM_BASE         99  /* ARM: ring ARM address (for the log)  */
#define SH_PCM_SIZE        100  /* ARM: ring bytes                      */
#define SH_PCM_RATE        101  /* ARM: Hz                              */
#define SH_PCM_CHANNELS    102  /* ARM: 2                               */
#define SH_PCM_WRITE_POS   103  /* ARM ONLY writer, bytes, < 64 KiB     */
#define SH_PCM_READ_POS    104  /* 68k ONLY writer, bytes, < 64 KiB     */
#define SH_PCM_UNDERRUNS   105  /* 68k: AHI buffers it could not fill   */
#define SH_PCM_BLOCKS      106  /* ARM: mix blocks produced             */
#define SH_AUDIO_US_AVG    107  /* ARM: mean us per mix block           */
#define SH_AUDIO_US_MAX    108  /* ARM: worst us per mix block          */
#define SH_PCM_FULL        109  /* ARM: pumps that found the ring full  */
#define SH_IMUSE_TICKS     110  /* ARM: ImUpdate() calls (144 Hz clock) */

 





















#define SH_SCREEN_W        113  /* 68k: physical P96 screen width       */
#define SH_SCREEN_H        114  /* 68k: physical P96 screen height      */
#define SH_MODE_IDX        115  /* 68k: index into the launcher's table */
#define SH_RES_MISMATCH    116  /* ARM: engine size != published size   */

#define ZZDF_GAME_W_MIN    320u
#define ZZDF_GAME_W_MAX    800u
#define ZZDF_GAME_H_MIN    200u
#define ZZDF_GAME_H_MAX    600u

 


























#define ZZDF_MIDI_ARM      (ZZDF_PCM_ARM + ZZDF_PCM_SIZE)  /* 0x04410000 */
#define ZZDF_MIDI_LEN      1024u          /* entries, u32                */
#define ZZDF_MIDI_SIZE     (ZZDF_MIDI_LEN * 4u)
#define ZZDF_MIDI_FB       (ZZDF_MIDI_ARM - ZZDF_ARM_FB_DELTA)

#define SH_MIDI_ON         117  /* 68k: 1 = camd link is up             */
#define SH_MIDI_HEAD       118  /* ARM ONLY writer: producer index      */
#define SH_MIDI_TAIL       119  /* 68k ONLY writer: consumer index      */
#define SH_MIDI_DROPPED    120  /* ARM: entries lost to a full ring     */
#define SH_MIDI_SENT       121  /* 68k: messages handed to camd         */
#define SH_MIDI_QUEUED     122  /* ARM: messages put into the ring      */
#define SH_MIDI_RESV123    123  /* free - see the note below            */

 

































 





 









#define SH_SCHED_EXEC      124  /* ARM: iMuse callbacks executed       */
#define SH_SCHED_LATE_MAX  125  /* ARM: worst lateness of a tick, us   */
#define SH_SCHED_LATE_AVG  126  /* ARM: mean lateness of a tick, us    */
#define SH_SCHED_MULTI     127  /* ARM: passes with >1 callback. MUST  */
                                /*      stay 0 at MIDISTEP=1           */
#define SH_SCHED_IVL_MAX   128  /* ARM: longest gap between 2 ticks us */
#define SH_SCHED_IVL_MIN   129  /* ARM: shortest gap between 2 ticks us*/
#define SH_SCHED_HIST0     160  /* ARM: gaps  < 5 ms                   */
#define SH_SCHED_HIST1     161  /* ARM: gaps  5 ..  8 ms (on time)     */
#define SH_SCHED_HIST2     162  /* ARM: gaps  8 .. 15 ms               */
#define SH_SCHED_HIST3     163  /* ARM: gaps 15 .. 30 ms               */
#define SH_SCHED_HIST4     164  /* ARM: gaps      > 30 ms              */
#define SH_SCHED_DROP_MAX  165  /* ARM: most deadlines lost in 1 pass  */

 













#define SH_RING_NON        166  /* ARM: Note Ons (vel>0) put in ring    */
#define SH_RING_NOFF       167  /* ARM: Note Offs (incl. vel 0) put     */
#define SH_IMOFF_SUPP      168  /* ARM: logical Off, current phys chan  */
                                /*      does not hold the note          */
#define SH_IMOFF_ELSE      169  /* ARM: ... and another phys chan does  */
#define SH_IMOFF_NODATA    170  /* ARM: logical Off, part had no chan   */
#define SH_IMOFF_LOGN      171  /* ARM: entries in the log below        */
#define SH_IMOFF_LOG0      172  /* 172..187, 16 entries, one word each: */
                                /* note 0..6, cur 7..10, other 11..14,  */
                                /* logical 15..18, sus 19, secs 20..31  */
#define ZZDF_IMOFF_LOG      16
#define SH_RING_CC         188  /* ARM: control changes put in ring     */
#define SH_RING_PROG       189  /* ARM: program changes put in ring     */

 




#define SH_MIDI_DEVICE     190  /* 68k: 0 camd, 1 SoundFont            */
#define SH_SF2_STATUS      191  /* ARM: 0 not tried, 1 loaded, 2 failed */
#define SH_SF2_VOICES_NOW  193  /* ARM: active TSF voices, last block   */
#define SH_SF2_VOICES_PEAK 194  /* ARM: most TSF voices in one block    */
#define SH_SF2_BLOCKS_BUSY 195  /* ARM: blocks rendered with voices > 0 */

 

























#define SH_WB_SEQ          130  /* ARM: bumped once per file offered   */
#define SH_WB_ACK          131  /* 68k: echoes SEQ once written        */
#define SH_WB_SIZE         132  /* ARM: payload bytes in staging       */
#define SH_WB_COUNT        133  /* ARM: files it intends to offer      */
#define SH_WB_DONE         134  /* ARM: 1 = no more files coming       */
#define SH_WB_SKIPPED      135  /* ARM: too large for the staging buf  */
#define SH_WB_WRITTEN      136  /* 68k: files really written to DATA   */
#define SH_WB_FAILED       137  /* 68k: files it could not write       */
#define ZZDF_WB_NAME       24   /* zero-padded name at staging + 0     */
 


#define ZZDF_WB_MAX        (2048UL*1024UL)

 













#define SH_DEMO_PLAY       138  /* 68k: 1 = play the demo, do not wait */
#define SH_DEMO_STATE      139  /* ARM: see ZZDF_DEMO_* below          */
#define ZZDF_DEMO_OFF        0  /* not asked for                       */
#define ZZDF_DEMO_NO_FILE    1  /* base_test.demo is not in the MemFS  */
#define ZZDF_DEMO_LOADED     2  /* loadReplayFromPath accepted it      */
#define ZZDF_DEMO_NO_LEVEL   3  /* loaded, but no -l override came back*/
#define ZZDF_DEMO_RUNNING    4  /* the engine reports playback live    */
#define ZZDF_DEMO_ENDED      5  /* playback finished on its own        */

 












#define SH_SAVE_LOADS      140  /* ARM: load requests consumed         */
#define SH_SAVE_LOAD_FAIL  141  /* ARM: loadGame() returned false      */
#define SH_SAVE_BYTES      142  /* ARM: bytes in the last .tfe written */
#define SH_SAVE_TRUNC      143  /* ARM: writes refused, RAMFS full     */

 
























#define SH_SUS_JUMPS       144  /* ARM: ImJumpSustain() calls          */
#define SH_SUS_CAPTURED    145  /* ARM: entries really allocated       */
#define SH_SUS_RELEASED    146  /* ARM: entries freed by countdown     */
#define SH_SUS_ACTIVE      147  /* ARM: entries active right now       */
#define SH_SUS_ACTIVE_PEAK 148  /* ARM: high-water mark (24 = full)    */
#define SH_SUS_ALLOC_FAIL  149  /* ARM: pool empty, note dropped       */
#define SH_SUS_PURGED      150  /* ARM: freed by ImRemoveInstrumentSound */
#define SH_SUS_NET_SAFETY  151  /* ARM: notes killed by the 1887 net   */
 







#define SH_INSTR_OFF_ORPHAN 152 /* ARM: Note Off for an already-clear bit */
#define SH_INSTR_ON_DOUBLE  153 /* ARM: Note On for an already-set bit    */
#define SH_SUS_LOGN         154 /* ARM: entries written to the SUS log    */
#define SH_INSTR_LOGN       155 /* ARM: entries written to the ON2 log    */

 












#define SH_SUS_LOG0        192  /* 192..223, 32 entries              */
#define ZZDF_SUS_LOG        32
/* SH_INSTR_LOG0: 16 entries, one word each.
 *     bits  0..6   note      bits 7..10 channel      bits 11..21 seconds
 */
#define SH_INSTR_LOG0      224  /* 224..239, 16 entries              */
#define ZZDF_INSTR_LOG      16

 






















#define ZZDF_NOTEON_SRC_IMHANDLE  0  /* ImHandleNoteOn()             */
#define ZZDF_NOTEON_SRC_DRUMOUT   1  /* ImMidi_DrumOut_NoteOn()      */
#define ZZDF_NOTEON_SRC_OTHER     2  /* ImNoteOn() par un 3e chemin  */

#define SH_NOTEON_TOTAL     156   
#define SH_NOTEON_DRUM      157  /* ARM: dont source = DRUMOUT        */
#define SH_NOTEON_MELODIC   158  /* ARM: dont source = IMHANDLE       */
#define SH_NOTEON_OTHER     159  /* ARM: dont source inconnue         */
 
 



#define SH_DBL_DRUM         242  /* ARM: doubles sur une percussion   */
#define SH_DBL_MELODIC      243  /* ARM: doubles sur une melodie      */
#define SH_DBL_OTHER        244  /* ARM: doubles de source inconnue   */
#define SH_NOTEON_RING_N    245   
#define SH_NOTEON_RING_W    246  /* ARM: index d'ecriture courant     */
#define SH_NOTEON_FROZEN    247   
#define SH_ASSIGN_GEN       248   

 























#define ZZDF_NOTEON_RING_OFF   (2UL*1024UL*1024UL)
#define ZZDF_NOTEON_RING_ARM   (ZZDF_STAGING_ARM + ZZDF_NOTEON_RING_OFF)
#define ZZDF_NOTEON_RING_FB    (ZZDF_STAGING_FB  + ZZDF_NOTEON_RING_OFF)
#define ZZDF_NOTEON_RING       128    

 
































#define SH_MIDI_MAXSTEP    249   
#define SH_MIDI_STEP_DROP  250   
#define SH_MIDI_STEP_BURST 251  /* ARM: passes ayant atteint le plafond */
#define ZZDF_MIDI_MAXSTEP_DEF 32  
 

#define ZZDF_MIDI_DROPDEBT_BIT 0x100u
#define ZZDF_NOTEON_TAIL        32    
 























#define ZZDF_RET_SAVED       0xC0DE0001UL /* entry capture verified   */
#define ZZDF_RET_CHECKED     0xC0DE0002UL /* C-side gates passed      */
#define ZZDF_RET_ASM         0xC0DE0010UL /* asm teardown entered     */
#define ZZDF_RET_L1_DONE     0xC0DE0020UL /* L1 D clean+inv, I inv    */
#define ZZDF_RET_SWITCHING   0xC0DE0030UL /* last SHARED write before */
                                          /* the MMU switch (with     */
                                          /* SH_EXIT_MARK)            */
#define ZZDF_RET_NO_MAGIC    0xC0DEBAD0UL /* capture incomplete       */
#define ZZDF_RET_REFUSED_ARM 0xC0DE00E1UL /* SH_ENABLE_RESTORE != 1   */
#define ZZDF_RET_REFUSED_MAG 0xC0DE00E2UL /* zzdf_saved magic missing */
#define ZZDF_RET_REFUSED_MOD 0xC0DE00E3UL /* not in SVC mode          */
#define ZZDF_RET_REFUSED_CTX 0xC0DE00E4UL /* saved SP/LR implausible  */
#define ZZDF_SAVED_MAGIC     0x5A5AFEEDUL
#define ZZDF_EXIT_MARK_VAL   0xC0DEBAD1UL

#define ZZDF_PRELOAD_COPY  0x50434F50UL   /* "PCOP": 68k requests service */
#define ZZDF_PRELOAD_READY 0x50524459UL   /* "PRDY": Core1 waits          */
#define ZZDF_PRELOAD_START 0x50535452UL   /* "PSTR": done, start the game */
#define ZZDF_PRELOAD_ERROR 0x50455252UL   /* "PERR": refused, parked      */
#define ZZDF_PRELOAD_CHUNK (64UL*1024UL)

/* Manifest entry, written by the 68k launcher into staging or DDR:
 * repeated SH_MANIFEST_COUNT times at SH_MANIFEST_ADDR.
 * All u32 fields are written by the 68k with byteswap so the ARM
 * reads them natively little-endian (ZZQuake pak convention).
 */
#ifndef ZZDF_ASM
 



#define ZZDF_MANIFEST_MAGIC 0x5A4D4654UL   /* "ZMFT", was ZMFS */
 







#define ZZDF_MANIFEST_MAX   128

typedef struct {
    char name[24];        /* zero-padded, e.g. "DARK.GOB"             */
    unsigned int base;    /* ARM address of blob                      */
    unsigned int size;    /* bytes                                    */
     






    unsigned int mtime;   /* seconds since 1978, 0 if unknown         */
    unsigned int resv;    /* keeps the entry 8-byte aligned           */
} zzdf_manifest_entry;    /* 40 bytes                                 */

typedef struct {
    unsigned int magic;   /* ZZDF_MANIFEST_MAGIC                      */
    unsigned int count;   /* number of entries that follow            */
    unsigned int total;   /* total asset bytes staged                 */
    unsigned int resv;
    /* zzdf_manifest_entry entry[count] follows immediately */
} zzdf_manifest_hdr;      /* 16 bytes                                 */
#endif

#endif /* ZZDF_CONFIG_H */
