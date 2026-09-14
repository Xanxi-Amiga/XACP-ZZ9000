/* Shared-memory protocol state. */








#ifndef ZZQUAKE_CONFIG_H
#define ZZQUAKE_CONFIG_H

/* ------------------------------------------------------------------ */
/* Memory map - ARM physical addresses.                               */
/* Amiga fb-relative offset = ARM address - 0x00200000.               */
/* ------------------------------------------------------------------ */

#define ZZQ_ARM_FB_DELTA    0x00200000UL  /* ARM = fb_offset + this  */

#define ZZQ_BLOB_ARM        0x04900000UL  /* fb+0x04700000, v134     */
#define ZZQ_BLOB_LIMIT_ARM  0x04B00000UL  /* 2MB reserve, ld ASSERT  */

/* Cache/MMU handling follows the validated Core1 memory contract. */


#define ZZQ_SHARED_ARM      0x04B00000UL  /* fb+0x04900000, 4KB, NC  */
#define ZZQ_SHARED_SIZE     0x00001000UL
/* Audio uses the shared PCM transport. */




#define ZZQ_PCM_ARM         0x04B80000UL  /* fb+0x04980000, v134     */
#define ZZQ_PCM_SIZE        0x00080000UL  /* 512KB reserve, unused v0*/

#define ZZQ_PAK_ARM         0x04500000UL  /* fb+0x04300000           */
#define ZZQ_PAK_MAX_SIZE    (26UL*1024UL*1024UL)  /* -> 0x05F00000   */
/* 26MB holds pak0 shareware (18689235) and pak0 registered. The cap
   is NOT arbitrary: see the stack window right below. A bigger pak
   (pak1, full game) must go ABOVE the heap, never here.            */

/* PAK filesystem handling. */










#define ZZQ_STACK_TOP       0x06000000UL  /* firmware SP base        */
#define ZZQ_STACK_FLOOR     0x05F00000UL  /* 1MB reserve, keep free  */

/* 0x06000000..0x07000000 : 16MB UNALLOCATED / UNVALIDATED. Not in
   collision with the stack, but nothing proves the firmware will
   never use it. Do not claim it without a hardware probe.           */

#define ZZQ_HEAP_ARM        0x07000000UL  
#define ZZQ_HEAP_SIZE_A0    (16UL*1024UL*1024UL)  /* -> 0x08000000   */
#define ZZQ_HUNK_SIZE       (12UL*1024UL*1024UL)  /* parms.memsize in
                       quakegeneric.c (patched); fits in 16MB heap
                       with surfcache + zzq overhead */
/* Extension to 32MB ONLY after PROBE zone validated on hardware:    */
#define ZZQ_PROBE_START     0x08000000UL
#define ZZQ_PROBE_END       0x09000000UL

/* fb-relative offsets for the 68k launcher */
#define ZZQ_BLOB_FB         (ZZQ_BLOB_ARM   - ZZQ_ARM_FB_DELTA)
#define ZZQ_SHARED_FB       (ZZQ_SHARED_ARM - ZZQ_ARM_FB_DELTA)
#define ZZQ_PCM_FB          (ZZQ_PCM_ARM    - ZZQ_ARM_FB_DELTA)
#define ZZQ_PAK_FB          (ZZQ_PAK_ARM    - ZZQ_ARM_FB_DELTA)

/* ------------------------------------------------------------------ */
/* Shared table - u32 slot indices (flat, ZZDoom convention).         */
/* ARM writes/reads directly (shared zone mapped NC on ARM side).     */
/* 68k uses rd32/wr32 with its usual byteswap discipline.             */
/* ------------------------------------------------------------------ */

#define ZZQ_MAGIC           0x5A5A514BUL  /* "ZZQK" */

#define SH_MAGIC            0
#define SH_STATUS           1
#define SH_HB               2   /* heartbeat, ARM increments          */
#define SH_CMD              3   /* 0=none 1=stop (68k writes)         */
#define SH_FRAME            4   /* drawframe count                    */
#define SH_DIAG             5   /* progress breadcrumbs 0xA0xx        */
#define SH_ERROR            6   
#define SH_ERR4             7   /* first 4 chars of Sys_Error text    */
#define SH_PAK_ADDR         8   /* 68k writes ARM address of pak      */
#define SH_PAK_SIZE         9   /* 68k writes pak byte size           */
#define SH_FB_ADDR          10  /* 68k updates after each flip        */
#define SH_FB_PITCH         11
#define SH_FB_WIDTH         12
#define SH_FB_HEIGHT        13
#define SH_INPUT            14  /* button bitfield, 68k writes        */
#define SH_MOUSE_DX         15  /* signed, Build B                    */
#define SH_MOUSE_BTN        16  /* Build B                            */
/* 17,18 : reallocated to SH_PAK_DIR_OFS / SH_PAK_DIR_COUNT (A1).
   Build B mouse dy will take a free slot above 92.                  */
#define SH_SP_NOW           19  /* live SP published each heartbeat  */
#define SH_FRAME_READY      20  /* ARM: frame in back buffer          */
#define SH_FLIP_SEQ         21  /* 68k: completed flip seq            */
#define SH_ENABLE_QG        22  /* 68k: 1 = blob calls QG_Create.
                                   A0 default 0 (link test only)      */
#define SH_FPS_X100         23
#define SH_FLOAT_DIAG       24  /* ZZQ_FLOAT_OK after VFP self-test   */
#define SH_PROBE_RESULT     25  /* 0=not run 1=ok 2=fail              */
#define SH_PROBE_FAIL_ADDR  26
#define SH_HUNK_SIZE        27  /* parms.memsize actually used        */
#define SH_HEAP_USED        28  /* _sbrk high-water mark              */
#define SH_ENABLE_PROBE     29  /* 68k: 1 = run the 0x08000000 DDR
                                   probe. DEFAULT 0: A0 validates the
                                   vital signs first, no side
                                   experiment (guardrail #1)          */
#define SH_PAK_MAGIC_SEEN   30  /* first 4 bytes of the pak header,
                                   DIAGNOSTIC ONLY - the real test is
                                   a byte compare against "PACK"     */
#define SH_PAK_DIR_OFS      17  /* pak directory offset (A1)         */
#define SH_PAK_DIR_COUNT    18  /* number of dpackfile_t entries (A1)*/
#define SH_PAK_OPEN_COUNT   31
#define SH_PAK_READ_COUNT   32
#define SH_PAK_SEEK_COUNT   33
#define SH_PAK_BYTES_READ   34
#define SH_NOTFOUND_BENIGN  35  /* opens that failed, non fatal       */
#define SH_FILE_FATAL       36  /* 1 if Sys_Error follows a file miss */
#define SH_PALETTE_COUNT    37
#define SH_KEY_EVENTS       38
#define SH_QG_TICK_US       39
#define SH_DRAW_US          40
#define SH_CONVERT_US       41
#define SH_FRAME_TOTAL_US   42
#define SH_ENTRY_SP         43  /* firmware-provided SP at entry:
                                   decides if Quake needs its own
                                   stack before A2 (headroom check)  */
#define SH_LAST_FILE_REQ    44  /* 44..59 : 64 bytes ASCII, last path
                                   passed to any open, zero padded    */
#define SH_LAST_FILE_LEN    16  /* in u32 slots                       */
#define SH_LAST_ERR_MSG     60  /* 60..75 : 64 bytes ASCII Sys_Error  */
#define SH_LAST_ERR_LEN     16
#define SH_LAST_PRINTF      76  /* 76..91 : 64 bytes last Sys_Printf  */
#define SH_LAST_PRINTF_LEN  16
/* --- Exception/fault window (A0.2), written by abort_handler.S ---
   Kept OUT of slots 5..9 (which the ZZDoom handler used) because
   those are DIAG/ERROR/PAK_ADDR/PAK_SIZE in ZZQuake. A literal port
   would smash the pak address. The handler writes here instead. */
#define SH_FAULT_MARK       92  /* 0xBADD0DAB / 0xBADB00AF / ...DD    */
#define SH_FAULT_FAR        93  /* DFAR (data) or IFAR (prefetch)     */
#define SH_FAULT_FSR        94  /* DFSR (data) or IFSR (prefetch)     */
#define SH_FAULT_PC         95  /* faulting PC (LR - 8 / -4 / -4)     */
#define SH_FAULT_SPSR       96  /* SPSR of the aborted mode           */
#define SH_FAULT_R0         97  /* scratch snapshot (see handler)     */
#define SH_TEST_UDF         98  /* 68k sets 1 to fire a deliberate UDF
                                   and validate the abort path (A0.2).
                                   Default 0 = normal run.            */
#define SH_ENABLE_A1        99  /* 68k sets 1 to run the A1 filesystem
                                   self-test (QG stays disabled).     */
#define SH_A1_RESULT        100 /* 0=ok, 0xE1..0xE4 = which step fail */
#define SH_A1_LUMP_POS      101 /* chosen lump offset in pak          */
#define SH_A1_LUMP_LEN      102 /* chosen lump length                 */
#define SH_A1_FIRST8_LO     103 /* first 8 bytes read (diag)          */
#define SH_A1_FIRST8_HI     104
#define SH_A1_CRC32         105 /* CRC32 of the whole pak (ARM side),
                                   68k compares to its own            */
/* Engine stdio is redirected to the in-memory filesystem. */

#define SH_LAST_STDIO_OP    106 /* ZZQ_OP_* below                     */
#define SH_STDIO_OP_COUNT   107 /* total stdio calls                  */
#define SH_DEMO_BLOCKED     108 /* count of .dem opens refused (A2b)  */
#define SH_ENABLE_DEMO_OFF  109 /* 68k: 1 = refuse every .dem open    */
#define SH_HUNK_BASE        110 /* malloc'd hunk base (0 = FAILED)    */
#define SH_NO_CACHEFIX      112 /* 68k: 1 = SKIP the pak cache
                                   invalidation (to prove whether it
                                   is the fix or not)                 */
#define SH_HUNK_MB          111 /* 68k: hunk size in MB. 0 = default 8.
                                   Lets us bisect 8 vs 12 MB WITHOUT
                                   rebuilding the blob (one variable
                                   per test cycle).                   */
/* WAD loading diagnostics. */
#define SH_WAD_BASE_A3      113 /* wad_base & 3                       */
#define SH_WAD_TABOFS       114 /* infotableofs (derived)             */
#define SH_WAD_LUMPS_A3     115 /* wad_lumps & 3                      */
#define SH_WAD_NUMLUMPS     116
#define SH_WAD_UNAL_COUNT   117 /* lumps whose filepos is not 4-aligned*/
#define SH_WAD_UNAL_IDX     118 /* index of the first such lump        */
#define SH_WAD_UNAL_POS     119 /* its filepos                         */
#define SH_WAD_PIC_A3       120 /* first qpic ptr & 3                  */
#define SH_WAD_PIC_W_NAT    121 /* pic->width read natively            */
#define SH_WAD_PIC_W_SAFE   122 /* same, read byte by byte             */
#define SH_WAD_PIC_H_NAT    123
#define SH_WAD_PIC_H_SAFE   124
#define SH_WAD_PROBE_DONE   125 /* 1 = probe ran                       */
#define SH_ENABLE_FBTEST    126 /* 68k: 1 = draw a test pattern into
                                   the framebuffer and idle. NO Quake
                                   at all. Proves the display path on
                                   its own, exactly like A1 proved the
                                   filesystem on its own.             */
#define SH_FBTEST_WRITES    127 /* pixels written by the pattern      */
/* Shared-memory protocol state. */



#define SH_GT_CTRL          128 /* SCU+0x208 raw, bit0 = Timer Enable */
#define SH_GT_T0_LO         129
#define SH_GT_T0_HI         130
#define SH_GT_T1_LO         131
#define SH_GT_T1_HI         132
#define SH_GT_DELTA         133 /* T1-T0 low word after a busy loop   */
#define SH_GT_PMCR          134 /* PMU PMCR, bit0 = counters enabled  */
/* --- Framebuffer write-back probe: does the ARM's store actually
   land where we think? The blob writes a magic value at fb+0 and
   fb+4, reads it straight back, and publishes both. The 68k reads
   the same VRAM. Three outcomes:
     blob reads back its magic AND 68k sees it -> writes land, the
        problem is elsewhere (repaint, wrong screen)
     blob reads back its magic but 68k does NOT -> ARM writes to the
        wrong address (or a cache never reaching DDR)
     blob does NOT read back its own magic -> MMU/mapping problem  */
#define SH_FBPROBE_ADDR     135 /* address the blob wrote to          */
#define SH_FBPROBE_WROTE    136 /* value written                      */
#define SH_FBPROBE_READ     137 /* value read straight back           */
/* --- Big-read capture: what the fake FS actually delivers to Quake.
   The .mdl header is: ident(0) version(4) scale(8) scale_origin(20)
   boundingradius(32) eyeposition(36) numskins(48) skinwidth(52)
   skinheight(56) numverts(60) numtris(64) numframes(68).
   version passes its check but numframes comes back absurd, so we
   capture the bytes as delivered and compare with the real file. --- */
#define SH_BIGRD_COUNT      138 /* bytes requested on the big read    */
#define SH_BIGRD_POS        139 /* file position it was read from     */
#define SH_BIGRD_W0         140 /* bytes 0..3   (should be 'IDPO')    */
#define SH_BIGRD_W1         141 /* bytes 4..7   (version, expect 6)   */
#define SH_BIGRD_NUMSKINS   142 /* offset 48                          */
#define SH_BIGRD_NUMVERTS   143 /* offset 60                          */
#define SH_BIGRD_NUMTRIS    144 /* offset 64                          */
#define SH_BIGRD_NUMFRAMES  145 /* offset 68  <- the guilty field     */
#define SH_BIGRD_DONE       146
#define SH_FBPROBE_ACK      147 /* 68k sets 1 once it has read back.
                                   Without this handshake the blob
                                   draws over the probe value before
                                   the 68k looks, and the comparison
                                   reports a false mismatch.          */
/* PAK filesystem handling. */





#define SH_MDL_FILEPOS      150
#define SH_MDL_LEN          151
#define SH_MDL_SRC          152
#define SH_MDL_DST          153
#define SH_MDL_S_BASE       154  /* 154..161 : ident version numskins
                                    skinw skinh numverts numtris
                                    numframes                        */
#define SH_MDL_D0_BASE      162  
#define SH_MDL_D1_BASE      170  /* 170..177 : idem au Sys_Error      */
#define SH_MDL_CRC_S        178
#define SH_MDL_CRC_D0       179
#define SH_MDL_CRC_D1       180
#define SH_MDL_VALID        181  /* 1 = S/D0 captures, 2 = D1 aussi   */

#define SH_SV_ACTIVE        182
#define SH_DEMOPLAYBACK     183
#define SH_DEMONUM          184  


#define SH_ERR_RET0         185  /* appelant direct                   */
#define SH_ERR_RET1         186  /* appelant de l'appelant            */




#define SH_BT_BASE          187  
#define SH_BT_COUNT         195
/* Cache/MMU handling follows the validated Core1 memory contract. */







#define SH_FTEST_STR        196  
#define SH_FTEST_BACK       200  
#define SH_CV_MAXSURFS      201  
#define SH_CV_MAXEDGES      202  
#define SH_CV_CNUMSURFS     203  /* r_cnumsurfs                       */
/* Hunk allocation diagnostics. */





#define SH_HUNKF_NAME       204  
#define SH_HUNKF_RAW        208  
#define SH_HUNKF_ADJ        209  
#define SH_HUNKF_LOW        210  /* hunk_low_used                    */
#define SH_HUNKF_HIGH       211  /* hunk_high_used                   */
#define SH_HUNKF_SIZE       212  /* hunk_size                        */
#define SH_ALIAS_STAGE      213  /* 1 header 2 skindesc 3 skin
                                    4 skingroup 5 skinintervals
                                    6 frame 7 framegroup 8 intervals */

#define SH_CV1_SURFS_STR    214  
#define SH_CV1_SURFS_RAW    218  /* .value en IEEE32 brut            */
#define SH_CV1_EDGES_RAW    219
#define SH_CV2_SURFS_RAW    220  
#define SH_CV2_EDGES_RAW    221
#define SH_FTEST_RET        222  
#define SH_ENABLE_CVARFIX   223  

#define SH_SURFCACHE_KB     224  /* Cache/MMU handling follows the validated Core1 memory contract. */




/* Timedemo results are exported to the launcher. */




#define SH_TD_DONE          225  /* Timedemo results are exported to the launcher. */
#define SH_TD_FRAMES        226
#define SH_TD_TIME_MS       227  
#define SH_TD_FPS_X100      228  
#define SH_ENABLE_TIMEDEMO  229  /* 68k: 1 = +timedemo demo1         */
/* Shared-memory protocol state. */


#define SH_EXIT_REASON      230
#define ZZQ_EXIT_QG_QUIT      1  
#define ZZQ_EXIT_CMD_STOP     2  /* le 68k a demande STOP            */
#define ZZQ_EXIT_CREATE_RET   3  
#define ZZQ_EXIT_FATAL        4  /* Sys_Error                        */
#define ZZQ_EXIT_MODE_END     5  
#define SH_LAST_CMD         231  



#define SH_LAST_CMD_LEN      12
#define SH_CMD_COUNT        243  /* nombre de commandes executees    */
#define SH_SURFCACHE_GOT    244  

#define SH_RUNNING_AT_LOOP  245  


/* Cache/MMU handling follows the validated Core1 memory contract. */




#define SH_CK_ENTRY         246  
#define SH_CK_AFTER_BSS     247
#define SH_CK_AFTER_MMU     248
#define SH_CK_BEFORE_QG     249
#define SH_CK_CANARY        250  

#define SH_ENTRY_LR         251  /* Core1 execution path. */

#define SH_RETURN_OK        252  





#define ZZQ_RETURN_OK       0xC0DEBACCUL

#define SH_RETURN_OK2       253
#define ZZQ_RETURN_OK2      0xC0DEBAD1UL
/* Cache/MMU handling follows the validated Core1 memory contract. */




#define SH_IMG_SIZE         254  
#define SH_IMG_SUM_68K      255  
#define SH_IMG_SUM_ARM      256  



#define SH_SNAP_A           257  
#define SH_SNAP_B           261  
#define SH_SNAP_C           265  /* Cache/MMU handling follows the validated Core1 memory contract. */
#define SH_ENABLE_PASSIVE   269  /* 68k : 1 = ne PAS lancer Quake,
                                    juste entrer et ressortir        */
#define SH_ENABLE_COOP      270  



#define SH_ENABLE_RESTORE   271  



#define SH_IMG_SUM_ARM2     272  /* Cache/MMU handling follows the validated Core1 memory contract. */



/* Core1 execution path. */












#define SH_DB_ENABLE        273  
#define SH_DB_WAITS         274  /* blob : attentes de FLIP_SEQ       */
#define SH_DB_WAIT_US       275  
#define SH_DB_WAIT_MAX      276  
#define SH_DB_TIMEOUTS      277  /* blob : spins epuises              */
#define SH_DB_REQ           278  
#define SH_DB_ACK           279  /* 68k : acquittements firmware      */
#define SH_DB_ACK_TMO       280  /* 68k : timeouts d'ACK              */
#define SH_DB_NOFLIP        281  
#define SH_TIMEDEMO_NOWAIT  282  /* Timedemo results are exported to the launcher. */


/* Timedemo results are exported to the launcher. */







#define SH_ENGINE_MAXFPS    283  


#define SH_FPS_PEAK_X100    284  






#define SH_WORK_US          285  


#define SH_WORK_MAX         286
#define SH_WAIT_LAST_US     287  
/* Framebuffer ownership is synchronized with the 68k launcher. */







#define SH_LAT_READY_PAN    288  /* R1-R0, en microsecondes          */
#define SH_LAT_PAN_ACK      289  /* R2-R1                            */
#define SH_LAT_ACK_REL      290  /* R3-R2                            */
#define SH_LAT_PAN_ACK_MAX  291
#define SH_VBL_AT_PAN       292  
/* Core1 execution path. */







#define SH_READY_SEEN       293  

#define SH_D1_US            294  /* Core1 -> 68k                     */
#define SH_D2_US            295  /* 68k -> Core1                     */
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
#define SH_CFLUSH_MODE      306  /* Cache/MMU handling follows the validated Core1 memory contract. */




/* Cache/MMU handling follows the validated Core1 memory contract. */








#define SH_VIS_TOKEN        307  /* Core1 execution path. */
#define SH_VIS_GO           308  
#define SH_VIS_ROUND        309  /* numero d'essai                   */
#define SH_VIS_DONE         310  
#define SH_ENABLE_VISTEST   311  /* 68k : 1 = test de visibilite */
#define SH_VIS_BULK         312  


#define SH_VIS_CLEAN_US     313  
/* Framebuffer ownership is synchronized with the 68k launcher. */













#define SH_RENDER_SEQ       314  /* 68k -> Core1 : tu peux rendre    */
#define SH_RENDER_FB        315  
#define SH_RENDER_DONE      316  /* Core1 execution path. */
#define SH_TRIPLE           317  
#define SH_GATE_PASS        318  
#define SH_GATE_BLOCK       319  
#define SH_FB_USED          320  
#define SH_FB_REJECTS       321  
#define SH_FB_LAST_BAD      322  




#define SH_HEAVY_FRAMES     323  /* travail > 16810 us               */
#define SH_RENDERS          324  
/* next free slot: 325 */
/* 105..119 : lump name echo (publish_name uses SH_LAST_FILE_REQ) */
/* WAD loading diagnostics. */
/* Shared-memory protocol state. */



/* --- Framebuffer write-back probe: does the ARM's store actually
   land where we think? The blob writes a magic value at fb+0 and
   fb+4, reads it straight back, and publishes both. The 68k reads
   the same VRAM. Three outcomes:
     blob reads back its magic AND 68k sees it -> writes land, the
        problem is elsewhere (repaint, wrong screen)
     blob reads back its magic but 68k does NOT -> ARM writes to the
        wrong address (or a cache never reaching DDR)
     blob does NOT read back its own magic -> MMU/mapping problem  */
/* --- Big-read capture: what the fake FS actually delivers to Quake.
   The .mdl header is: ident(0) version(4) scale(8) scale_origin(20)
   boundingradius(32) eyeposition(36) numskins(48) skinwidth(52)
   skinheight(56) numverts(60) numtris(64) numframes(68).
   version passes its check but numframes comes back absurd, so we
   capture the bytes as delivered and compare with the real file. --- */
/* PAK filesystem handling. */












/* Cache/MMU handling follows the validated Core1 memory contract. */







/* Hunk allocation diagnostics. */






/* Timedemo results are exported to the launcher. */




/* Shared-memory protocol state. */


/* Cache/MMU handling follows the validated Core1 memory contract. */





/* Cache/MMU handling follows the validated Core1 memory contract. */







/* Core1 execution path. */












/* Timedemo results are exported to the launcher. */










/* Framebuffer ownership is synchronized with the 68k launcher. */







/* Core1 execution path. */











/* Cache/MMU handling follows the validated Core1 memory contract. */








/* Framebuffer ownership is synchronized with the 68k launcher. */

















/* next free slot: 325 */

#define ZZQ_FLOAT_OK        0xF10A70CBUL

/* stdio operation codes for SH_LAST_STDIO_OP */
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

/* Fault markers written to SH_FAULT_MARK by abort_handler.S. Kept
   identical to the ZZDoom values so existing 68k-side tooling that
   recognizes them keeps working. */
#define ZZQ_MARK_DATA_ABT   0xBADD0DABUL
#define ZZQ_MARK_PREFETCH   0xBADB00AFUL
#define ZZQ_MARK_UNDEF      0xBADB00DDUL

/* ------------------------------------------------------------------ */
/* Status codes                                                       */
/* ------------------------------------------------------------------ */

#define ZZQ_INIT_START          0x01
#define ZZQ_PAK_OK              0x02
#define ZZQ_FS_OK               0x03
#define ZZQ_QG_CREATE_START     0x04
#define ZZQ_QG_CREATE_OK        0x05
#define ZZQ_PALETTE_OK          0x06
#define ZZQ_FIRST_FRAME_OK      0x07
#define ZZQ_RUNNING             0x08
#define ZZQ_QUIT_REQUESTED      0x09
#define ZZQ_A0_IDLE             0x0A  /* A0: init done, waiting stop  */
#define ZZQ_STOPPED             0xFF  /* MANDATORY final status + WFE */

/* Fatal codes published in SH_ERROR (status still ends 0xFF + WFE
   so Core1 can always be relaunched without power cycle) */
#define ZZQ_FATAL_NO_PAK        0x80
#define ZZQ_FATAL_BAD_PAK       0x81
#define ZZQ_FATAL_QG_CREATE     0x82
#define ZZQ_FATAL_OUT_OF_MEM    0x83
#define ZZQ_FATAL_SYS_ERROR     0x84
#define ZZQ_FATAL_PAK_TOO_BIG   0x85  /* pak would reach the Core1
                                         stack window - refuse before
                                         reading a single byte      */
#define ZZQ_FATAL_UNDEFINED     0x86  /* undefined instruction abort */
#define ZZQ_FATAL_PREFETCH_ABT  0x87  /* prefetch (instruction) abort*/
#define ZZQ_FATAL_DATA_ABT      0x88  /* data abort                  */
#define ZZQ_FATAL_BAD_FILE      0x89  /* A1 filesystem self-test fail*/

/* Commands (SH_CMD, 68k -> ARM) */
#define ZZQ_CMD_NONE            0
#define ZZQ_CMD_STOP            1

/* ------------------------------------------------------------------ */
/* Input button bits (SH_INPUT) - values identical to ZZDoom so the   */

/* ------------------------------------------------------------------ */

#define BTN_UP          0x00000001UL
#define BTN_DOWN        0x00000002UL
#define BTN_LEFT        0x00000004UL
#define BTN_RIGHT       0x00000008UL
#define BTN_SL          0x00000010UL   /* strafe left  (',')          */
#define BTN_SR          0x00000020UL   /* strafe right ('.')          */
#define BTN_FIRE        0x00000040UL   /* K_CTRL                      */
#define BTN_USE         0x00000080UL   /* K_SPACE jump/swim           */
#define BTN_RUN         0x00000100UL   /* K_SHIFT                     */
#define BTN_ESC         0x00000200UL
#define BTN_ENTER       0x00000400UL
#define BTN_Y           0x00000800UL   /* confirm quit                */
#define BTN_STRAFE_MOD  0x00001000UL   /* K_ALT strafe modifier       */
#define BTN_MAP         0x00002000UL   /* K_TAB scores                */
#define BTN_N           0x00004000UL   /* deny prompt                 */
#define BTN_TILDE       0x00008000UL   /* '~' console                 */
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

#endif /* ZZQUAKE_CONFIG_H */

/* Framebuffer ownership is synchronized with the 68k launcher. */









#define ZZQ_KEYQ_SIZE       64        /* puissance de 2 obligatoire  */
/* Pump queued input while Quake is inside modal loops. */
















#define SH_MODAL_STAGE      325
#define SH_MODAL_GATEBLOCK  326
#define SH_MODAL_LOOPS      327
#define SH_MODAL_LASTKEY    328
#define SH_MODAL_FLAGS      329

#define SH_KEYQ_WR          330       
#define SH_KEYQ_RD          331       /* Core1 execution path. */
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
/* next free slot: 398 */

/* Shared-memory protocol state. */


/* Core1 execution path. */






#define SH_MOUSE_TOT_X      398   /* total cumule, signe             */
#define SH_MOUSE_TOT_Y      399
#define SH_MOUSE_EVENTS     400   /* diagnostic                      */
#define SH_MOUSE_RAWMAX_X   401   
#define SH_MOUSE_RAWMAX_Y   402
#define SH_MOUSE_SCLMAX_X   403   
#define SH_MOUSE_SCLMAX_Y   404
#define SH_MOUSE_SCALE      405   /* diviseur applique               */
/* Audio uses the shared PCM transport. */







#define SH_PCM_ENABLE       406
#define SH_PCM_BASE_SLOT    407   
#define SH_PCM_SIZE_SLOT    408   
#define SH_PCM_WRITE_POS    409   /* Core1 avance                    */
#define SH_PCM_READ_POS     410   /* 68k avance                      */
#define SH_PCM_RATE         411
#define SH_PCM_UNDERRUNS    412
#define SH_PCM_FILL_MIN     413   /* diagnostic de remplissage       */
#define SH_PCM_FILL_MAX     414






#define SH_HIST_BASE        415   /* 415..426 : 12 tranches         */
#define ZZQ_HIST_BINS       12
/* Framebuffer ownership is synchronized with the 68k launcher. */













#define ZZQ_PH_COUNT        8     /* setup world brush scan surf ent vm part */
#define SH_PROF_LIGHT       427   
#define SH_PROF_HEAVY       435   
#define SH_PROF_MAX         443   /* 443..450 : maximum vu               */
#define SH_PROF_NLIGHT      451
#define SH_PROF_NHEAVY      452
/* Optional renderer profiling hooks. */




#define ZZQ_S2_COUNT        7  /* cache grad tex z special submodel other */
#define SH_S2_LIGHT         453   /* 453..459 */
#define SH_S2_HEAVY         460   /* 460..466 */
/* Cache/MMU handling follows the validated Core1 memory contract. */




#define SH_N_NORMAL         467
#define SH_N_SKY            468
#define SH_N_TURB           469
#define SH_N_BACK           470
#define SH_N_SUBMODEL       471
#define SH_N_SPANS          472
#define SH_N_SPANPIX_K      473   
/* Hunk allocation diagnostics. */















/* Hunk allocation diagnostics. */







#define ZZQ_FILEBUF_ARM     0x06E00000UL   /* Shared-memory protocol state. */
#define ZZQ_FILEBUF_SIZE    0x00080000UL   /* 512 Kio par slot       */
#define ZZQ_FILEBUF_SLOTS   4
#define ZZQ_FILEBUF_TOTAL   (ZZQ_FILEBUF_SIZE * ZZQ_FILEBUF_SLOTS)
#define ZZQ_FILEBUF_AT(i)   (ZZQ_FILEBUF_ARM + (u32)(i) * ZZQ_FILEBUF_SIZE)
#define ZZQ_FNAME_MAX       64             /* MAX_QPATH de Quake     */
#define ZZQ_FILEBUF_FB      (ZZQ_FILEBUF_ARM - ZZQ_ARM_FB_DELTA)

#define SH_FS_CMD           474
#define SH_FS_SEQ           475   /* Core1 execution path. */
#define SH_FS_ACK           476   /* 68k repond                     */
#define SH_FS_SIZE          477
#define SH_FS_ERR           478
#define SH_FS_OFFSET        479   /* prevu, 0 en phase E            */
#define SH_FS_CHUNK         480   /* prevu, = SIZE en phase E       */
#define SH_FS_EXISTS        481   /* reponse a FS_CMD_STAT          */
#define SH_FS_NAME          482   
#define SH_FS_SLOT          506   /* Shared-memory protocol state. */
#define SH_FS_OPENFAIL      507   /* Shared-memory protocol state. */
#define SH_FS_CFG_READ      508   /* config.cfg relu au demarrage ? */
#define SH_FS_CFG_BYTES     509   
#define SH_FS_OPENS         510   
#define SH_FS_CLOSES        511   /* fermetures                      */
#define SH_FS_SLOTMASK      512   /* slots occupes, bit par slot     */
#define SH_FS_SLOTPEAK      513   /* occupation maximale atteinte    */

#define FS_CMD_WRITE        1
#define FS_CMD_READ         2
#define FS_CMD_STAT         3     




#define ZZFS_OK             0
#define ZZFS_ERR_OPEN       1
#define ZZFS_ERR_TOO_LARGE  2
#define ZZFS_ERR_TIMEOUT    3
#define ZZFS_ERR_WRITE      4
#define ZZFS_ERR_READ       5



#define SH_LG_ENTNUM        498   /* entite en cours de parsing      */
#define SH_LG_NUMEDICTS     499   /* sv.num_edicts a cet instant     */
#define SH_LG_MAXEDICTS     500
#define SH_LG_EDICTSPTR     501   /* sv.edicts                       */
#define SH_LG_EDICTSIZE     502   /* pr_edict_size                   */
#define SH_LG_BADPTR        503   /* le pointeur refuse              */
#define SH_LG_BADB          504   
#define SH_LG_CALLER        505   
/* Warp limits must match the actual warp buffer dimensions. */




#define SH_WATER_TURB       514   
#define SH_WATER_WARP       515   /* Warp limits must match the actual warp buffer dimensions. */
#define SH_WATER_TURBIDX    516   
#define SH_WATER_CLTIME     517   
#define SH_WATER_VRECT      518   /* largeur | hauteur << 16        */
#define SH_WATER_STAGE      519   
/* Cache/MMU handling follows the validated Core1 memory contract. */









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
#define SH_SBRK_INCR        543   /* 1er incr demande                */
/* Cache/MMU handling follows the validated Core1 memory contract. */


#define SH_BSSPROBE_PRE     544   
#define SH_BSSPROBE_POST    545   /* Cache/MMU handling follows the validated Core1 memory contract. */
#define ZZQ_BSSPROBE_MAGIC  0x5A11EDEDUL




#define SH_TEXT_LEN         546
#define SH_DATA_LEN         547







#define SH_DDS_AFTER_SETUP  548   
#define SH_DDS_BEFORE_CALL  549   
#define SH_DDS_EXPECTED     550   
#define SH_DDS_MISMATCH     551   /* compteur de divergences         */
/* Warp limits must match the actual warp buffer dimensions. */


#define SH_W_ENTER          552
#define SH_W_EXIT           553
#define SH_W_SP_ENTER       554
#define SH_W_LR_ENTER       555
#define SH_W_SP_EXIT        556


#define SH_CRC_A_FULL       557   
#define SH_CRC_B_FULL       558   







#define SH_FLT_LR_USR       559   /* LR du mode interrompu = appelant */
#define SH_FLT_SP_USR       560   /* SP du mode interrompu            */
#define SH_FLT_R0           561
#define SH_FLT_R1           562
#define SH_FLT_R2           563
#define SH_FLT_R3           564
#define SH_FLT_R12          565

#define SH_FLT_STK0         566
#define SH_FLT_STK1         567
#define SH_FLT_STK2         568
#define SH_FLT_STK3         569
/* ---- diagnostic du waterwarp ---- */
#define SH_WARP_VID         570   /* vid.width | height << 16        */
#define SH_WARP_MAX         571   /* maxwarpwidth | maxwarpheight    */
#define SH_WARP_CONST       572   /* WARP_WIDTH | WARP_HEIGHT        */
#define SH_WARP_SBLINES     573
#define SH_WARP_ACTIVE      574   /* nombre d activations            */
#define SH_WARP_VRECT       575   
#define SH_WARP_SCRW        576   /* screenwidth                     */
#define SH_WARP_ISBUF       577   /* d_viewbuffer == r_warpbuffer    */
/* Shared-memory protocol state. */







#define SH_FSQ_TOTAL        578   
#define SH_FSQ_TIMEOUT      579   
#define SH_FSQ_ERR68K       580   
#define SH_FSQ_WAIT_MAX     581   /* attente maximale, en us         */
#define SH_FSQ_WAIT_SUM_MS  582   



#define SH_FSQ_H0           583
#define SH_FSQ_H1           584
#define SH_FSQ_H2           585
#define SH_FSQ_H3           586
#define SH_FSQ_H4           587

#define SH_FSQ_LASTFAIL     588   /* 588..603 */



#define SH_FSS_POLLS        604   
#define SH_FSS_HANDLED      605   
#define SH_FSS_GAP_MAX      606   
/* Cache/MMU handling follows the validated Core1 memory contract. */








#define SH_CLEAN_MODE       607   /* 1 = mode CLEAN                  */
#define SH_CLEAN_ENTERED    608   /* Core1 execution path. */
#define SH_CLEAN_MALLOC     609   /* resultat du malloc du hunk      */
#define SH_CLEAN_B_SBRK     610   
#define SH_CLEAN_B_AV1      611
#define SH_CLEAN_B_AV2      612
#define SH_CLEAN_B_HEAP     613
#define SH_CLEAN_A_SBRK     614   
#define SH_CLEAN_A_AV1      615
#define SH_CLEAN_A_AV2      616
#define SH_CLEAN_DONE       617   
/* Shared-memory protocol state. */




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
/* Shared-memory protocol state. */








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
#define ZZQ_PACK_ARENA_END  0x3F800000UL   /* EXCLU */

/* PAK filesystem handling. */


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
#define ZZQ_PRELOAD_COPY    0x50434F50UL   /* "PCOP" : le 68k arme    */
#define ZZQ_PRELOAD_READY   0x50524459UL   /* "PRDY" : Core1 attend   */
#define ZZQ_PRELOAD_START   0x50535452UL   /* "PSTR" : fini, continue */
#define ZZQ_PRELOAD_ERROR   0x50455252UL   /* "PERR" */
/* PAK filesystem handling. */





#define SH_PACK2_ADDR       650
#define SH_PACK2_SIZE       651
#define SH_GAME_MODE        652
#define ZZQ_GAME_ID1        0
#define ZZQ_GAME_HIPNOTIC   1
#define ZZQ_GAME_ROGUE      2

#define SH_MP_ARGC          653   
#define SH_MP_MODE_SEEN     654   /* SH_GAME_MODE vu PAR LE BLOB       */
#define SH_MP_OPENS         655   
#define SH_MP_PROBES        656   /* chemins contenant hipnotic/rogue   */
/* PAK filesystem handling. */






#define ZZQ_MODPACK_MAX     4
#define SH_MOD_COUNT        657   /* nombre de paks du mod          */
#define SH_MOD_ADDR         658   /* 658..661 */
#define SH_MOD_SIZE         662   /* 662..665 */
#define SH_MOD_NAME         666   
#define ZZQ_GAME_MOD        3     
/* Audio uses the shared PCM transport. */






#define SH_CD_INIT          682
#define SH_CD_PLAY          683   /* Audio uses the shared PCM transport. */
#define SH_CD_TRACK         684   
#define SH_CD_LOOP          685
#define SH_CD_STOP          686
#define SH_CD_PAUSE         687
#define SH_CD_RESUME        688
#define SH_CD_UPDATE        689
#define SH_CD_SHUTDOWN      690
#define SH_CD_TRACKMASK     691   
/* Core1 execution path. */




#define ZZQ_MUSQ_SIZE       8            /* puissance de 2 */
#define SH_MUSQ_WR          692          /* Core1 execution path. */
#define SH_MUSQ_RD          693          
#define SH_MUSQ_BASE        694          /* 8 entrees de 2 mots      */
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
#define SH_CD_VOLMUTE       714   /* bgmvolume == 0 : coupe */
/* Core1 execution path. */


#define SH_MUSIC_ENABLE     715
/* Optional renderer profiling hooks. */


#define SH_PMU_PMCR         716
#define SH_PMU_CNTEN        717
#define SH_PMU_C0           718
#define SH_PMU_C1           719
#define SH_PMU_GT0          720
#define SH_PMU_GT1          721
/* Shared-memory protocol state. */











#define SH_STAGE            722
#define ZZQ_STG_NONE        0
#define ZZQ_STG_SHUTSRV_IN  1   
#define ZZQ_STG_SHUTSRV_OUT 2   
#define ZZQ_STG_CLEARMEM    3   /* Host_ClearMemory                */
#define ZZQ_STG_SPAWN_IN    4   
#define ZZQ_STG_MODLOAD     5   /* Mod_ForName du worldmodel       */
#define ZZQ_STG_NEWMAP      6   /* R_NewMap                        */
#define ZZQ_STG_SPAWN_OUT   7   
#define ZZQ_STG_LOOP        8   
/* Pump queued input while Quake is inside modal loops. */







#define SH_MODAL_WAITS      723
#define SH_MODAL_TIMEOUTS   724
#define SH_FB_BPP           725
#define ZZQ_SLOT_COUNT      726





#define ZZQ_PCM_RING_SIZE   0x10000UL
#define ZZQ_PCM_RATE        22050UL
#define ZZQ_PCM_TARGET_FILL 16384UL     





