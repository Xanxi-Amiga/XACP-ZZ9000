/*
 * ZZPicoDrive32X v1.2.1 - Sega 32X (and Mega Drive / Genesis) launcher for the
 * MNT ZZ9000.
 *
 * RELEASE v1.2.1 (01.11.2026).  Build: see BUILD.md (blob built with
 * "ZZVR_DRC=1 ./build_arm.sh", embedded with tools/bin2c, launcher compiled with
 *   m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign
 *       -o ZZPicoDrive32X ZZPicoDrive32X.c blob_data.c -lamiga ).
 *
 * v1.2.1 (01.11.2026), launcher only (Core1 program of v1.2 unchanged), the
 * fixes of ZZPicoDriveVR v1.2: CD32 / USB pad left and right shoulders = X and
 * Y, yellow = Z (1.2: yellow = Mode, no X / Y / Z on the pad); checked Core1
 * start (no MAGIC / INIT timeout: report, image read back, ARM_RUN = 0, one
 * more load); GUI session log in T:ZZPicoDrive32X.log and a requester with the
 * reason of a failed start; "SH-2 PCs frozen" watchdog line only when the game
 * runs the 32X.
 * ZZPicoDrive32X v1.2 = the ZZPicoDriveMD v1.2 launcher (which already carries
 * every 32X change of ZZPicoDrive32X rc1: StackSwap, vblank PLL, Del reset with
 * handshake, keypad X/Y/Z/MODE, 1.5 MiB savestates, 32X blob diagnostics) with
 * the 32X names: program, configuration PROGDIR:ZZPicoDrive32X.cfg, embedded
 * blob zz32x_v1.2.bin, texts.  The ZZPicoDriveMD v1.2 development history
 * below (builds rc2..rc2o) is kept for reference; the rc1 launcher is in
 * docs/history/rc1/.
 * v1.2 RC5 (01.11.2026): SNAP lines show the SH-2 wait state of the RC5 blob
 * (state, poll address, SR, pending interrupt level) for the freeze report.
 * v1.2 RC4 (01.11.2026): ARM_RUN = 0 also BEFORE loading (a previous Core1
 * program that did not stop cleanly left stale L2 lines that the new blob read
 * back); a blob file given on the command line and not found is an error.
 * v1.2 RC2 (01.11.2026): the vblank PLL lock is tried up to 5 times and each
 * failure is printed with its reason (RC1 on Doom: probe OK but the 4-edge
 * measurement failed once -> whole session on WaitTOF, 56.3 FPS, 6 % dropped).
 *
 * v1.2 build rc2o (31.10.2026, blob unchanged): documentation only - the GUI
 *   hotkeys text, the Shell help and the README say that a savestate is
 *   guaranteed only with the same region and the same audio output.
 *
 * v1.2 build rc2n (31.10.2026, blob zzmd_rc2.bin unchanged): savestate region.
 *   A state carries the console's PAL/NTSC flag (PicoDrive chunk MISC =
 *   Pico.m).  Loaded into a session of the other region it switched the
 *   emulated console under a launcher, an output timing and an audio rate set
 *   for the session: wrong picture height, and in one case music at full
 *   speed.  Now each state file ends with a 9-byte region tag written after
 *   PicoDrive's end marker (chunk id 200, length 4, "ZP", pal, region
 *   override): PicoDrive stops at its end marker, so 1.1 still loads v1.2
 *   files and v1.2 loads 1.1 files (no tag: loaded, with a warning).  A tagged
 *   state of the other region is refused with a message.
 *
 * v1.2 build rc2m (31.10.2026, blob zzmd_rc2.bin unchanged): savestates, AHI.
 *   rc2j with AHI failed the same way (no looping sound, "save echoue"): the
 *   ack wait was a busy loop in the main task, which runs at priority 6 since
 *   rc2c, ABOVE the AHI task (5): the AHI task could no longer drain the ring,
 *   same deadlock.  (1.1: main task at 0, AHI at 5 -> AHI saves worked, Paula
 *   saves never did since the audio-slaved pacing of 1.1.)  save_wait_ack()
 *   now sleeps 1 ms between two polls (timer.device; Delay(1) without it),
 *   so the AHI task runs, and keeps feeding Paula; 3 s max.
 *
 * v1.2 build rc2l (31.10.2026, blob zzmd_rc2.bin unchanged): savestates.
 *   Shift+F1 froze the game for 5 s with the sound looping, then "save
 *   echoue"; F1 then found no file.  Cause: the blob serves save commands
 *   between two PicoFrames, and it starts a PicoFrame only when the 68k has
 *   drained the audio ring.  With Paula the draining is done by the main loop
 *   (pa_service) - which was busy polling the save ack: deadlock until the
 *   68k timeout (8 M polls = 5.3 s).  With AHI the separate task drains and it
 *   worked.  Same bug in 1.1 and in the 32X launchers (Doom 32X F1).
 *   - save_wait_ack(): waits on the EClock (2 s max) and keeps Paula fed
 *     every 5 ms while waiting; used by state save/load and SRAM save/load.
 *   - F1..F5 / Shift+F1..F5 ignore key auto-repeat (holding F1 fired the load
 *     30 times).
 *   - the STATE lines give the time taken and say "timeout" when it is one.
 *
 * v1.2 build rc2k (31.10.2026, blob zzmd_rc2.bin unchanged).  rc2j NTSC Paula
 * 152 s: 10 dropped = 7 at t=0.09-0.14 s + 3 at t=31.7 s.
 *   - start: the settle step was fooled - the blob was idle (audio buffers
 *     full, 0 PicoFrame per vblank counted as "steady"), then the first Paula
 *     underrun (the "main task priority" printf right after the audio start
 *     delayed the loop by 19 ms) made it catch up at 3 per vblank.  Settle now
 *     needs exactly one PicoFrame per vblank for 8 vblanks in a row (at most
 *     180), and the priority line is printed with the statistics.
 *   - the 3 drops at 31.7 s follow a 60 ms stall of the 68k (wake-up 60 ms
 *     late, Paula underrun #2, late #2): the system held the 68k for 3.5
 *     vblanks; the frames of that stall cannot be shown in time and the blob
 *     overwrites them while it catches up the audio - correct behaviour, the
 *     cause is outside the emulator.  The statistics now give the time of the
 *     worst stall.
 *
 * v1.2 (31.10.2026, build rc2j, blob zzmd_rc2.bin unchanged) = rc2i with:
 *   - the mouse no longer quits at all: the game window no longer asks for
 *     mouse button events (a 240 s Quackshot run ended with exit reason
 *     "mouse button" while the mouse was not touched: a stray event, likely
 *     from the controller polling on the mouse port).  Shift+Esc quits.
 *   - version 1.2 shown in the GUI title, the About box, the Shell banner and
 *     the $VER string.
 *
 * rc2i (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged) = rc2h plus:
 *   - a mouse button no longer quits (1.1 "click = emergency quit"): a
 *     Quackshot run of 240 s ended on its own on a stray mouse event (exit
 *     reason "mouse button"); a joystick or pad on the mouse port sends the
 *     same events.  Shift+Esc quits; flag mousequit restores the click.
 *   - the vblank pulse width is the shortest of 3 measured pulses (one
 *     measurement interrupted by the system gave 5315 us instead of ~1270).
 *   - the drop count is frozen at the quit request: the console printf of the
 *     quit snapshot (SNAP A) costs 2 frames, which are not part of the game
 *     (line "drops:" now reports publishing -> quit).
 *
 * rc2h (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged).  rc2g NTSC
 * Paula: 12 dropped in 100 s, in two bursts only: 7 in the first 0.16 s (the
 * blob runs 2-3 PicoFrames per vblank while the audio buffers fill right after
 * the audio start) and 5 at t=37.7 s (2 frames per vblank for 6 vblanks: an
 * audio catch-up, the 68k loop was regular).  rc2h:
 *   - start settle: P96_BASE (the blob cannot publish without it) is written
 *     only once the blob runs at one PicoFrame per vblank (4 vblanks in a row,
 *     at most 60): the catch-up frames of the start are never published, the
 *     screen stays black for ~0.1-0.2 s more.  Flag nosettle disables it.
 *   - the drop table records the Paula underrun/resync counters, the blob
 *     frame counter and the worst wake-up lateness at each event, to identify
 *     the cause of mid-run bursts.
 *
 * rc2g (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged): diagnostics
 * only.  rc2f measured NTSC AHI 59.0 (14 dropped / 3239) and NTSC Paula 59.8
 * (7 dropped / 3842, all 7 before the quit, none in the last 60 vblanks).  To
 * find out WHEN the few remaining frames are dropped, the loop watches the
 * blob's dropped counter every iteration and records the first 32 events
 * (iteration, time since loop start, loop state, PLL counters) in a table
 * printed at exit - no printf during the run (a console printf takes
 * milliseconds and would itself drop a frame: the 2 drops seen during the
 * quit sequence of the NTSC AHI run are the SNAP A printout).  Also prints the
 * drops of the first 2 s separately.  Nothing else changes.
 *
 * rc2f (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged).  rc2e measured
 * NTSC AHI 58.8 (4479 emulated / 4425 flips, 49 dropped, 720 edges found by the
 * ack counter, one iteration per vblank).  Residue: an iteration that found no
 * frame leaves no PAN in flight, so the next wait has no ack counter to watch
 * and falls back to the bit alone, where a hidden 1.3 ms pulse still costs a
 * vblank (and the frame that then waits 16 ms more is overwritten by the
 * blob's next pair).  rc2f measures the pulse width at init and, when no PAN
 * is in flight, treats two low samples more than 3/4 of a pulse apart with the
 * predicted edge in between as a hidden pulse: edge = prediction, uncertain
 * (no period learning).  Without a PAN in flight an early call is harmless:
 * the iteration takes the frame if there is one and its PAN is applied at the
 * edge that comes.  Counter "edges by gap" in the stats.
 *
 * rc2e (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged).  rc2d measured
 * PAL AHI 49.5, PAL Paula 49.8, NTSC Paula 59.7 (8 dropped with the 59.98 Hz
 * timing, 34 before), period estimate stable; NTSC AHI 55.8 with 239 dropped
 * and 193 edge intervals out of range: 193 vblanks skipped.  The NTSC vblank
 * pulse is 41 lines = 1.3 ms (PAL50: 145 lines = 4.6 ms); when the AHI mixer
 * (above all tasks) interrupts the poll for 1.3-4 ms across the pulse, the bit
 * is low before and low after: the edge is invisible and the poll waits for
 * the next one.  rc2e polls the PAN ack counter as well: the firmware bumps it
 * at the rising edge while a PAN is in flight (always, in steady state), and a
 * counter cannot be missed.  Ack changed between two samples -> the edge passed
 * in between (phase = middle of the gap, no period learning from it).  The
 * same counter keeps the "late wake-up" path from firing when the edge has in
 * fact not come yet (prediction early).  Counter "edges by ack" in the stats.
 *
 * rc2d (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged).  rc2c measured
 * PAL AHI 49.6 displayed/s, 0 dropped (3801 emulated / 3796 flips), AHI task
 * 1.3 ms per buffer (3.2 max), 13 % of the wake-ups late by 0.3-2 ms (AHI
 * mixer, above the tasks) absorbed without loss, half of the frames taken by
 * the re-check.  Left: the PLL period still drifted (20287 us for 20011) with
 * the edge-to-edge IIR.  rc2d measures the period on a long baseline instead:
 * (time between a reference clean edge and the current clean edge) / (number
 * of vblanks in between, rounded with the current estimate); a per-edge error
 * of a few tens of us is divided by the baseline (8 to 2048 vblanks).  The
 * relock on a real timing change (4 consecutive edge intervals out of +-10 %)
 * is kept.  PLL line: baseline length, learn/unc_rej/relock counters.
 *
 * rc2c (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged).  rc2b measured:
 * PAL Paula 49.8, NTSC Paula 59.3 (validated), AHI PAL 44.7 / NTSC 50.2: the
 * loop now ran at the right rate but ~10 % of its iterations found no frame.
 *   - PLL phase: a wake-up inside the vblank pulse took the wake-up time as the
 *     edge time, moving the phase later by the whole lateness (PAL50 pulse =
 *     145 lines = 4.6 ms: 74 % inpulse, period learnt 20376 us instead of 20008).
 *     Now edge = min(predicted, now); an edge whose detection was interrupted
 *     (more than 200 us between the last low and the first high sample) is not
 *     used to learn the period.
 *   - main task priority 6 during the loop (flag pri0 keeps the Shell priority):
 *     the AHI task (priority 5) reads 1024 Zorro III words per buffer, 43 times a
 *     second, and the timer wake-up of the main task waited for it.  With Paula
 *     the same reads run in the main task after the edge, hence 49.8.
 *   - no frame ready right after the edge -> FRAME_READY is re-checked every
 *     2 ms until 0.8 ms before the next edge and the PAN armed as soon as the
 *     blob publishes (applied at that same edge).  Flag norecheck disables it.
 *   - NTSC games: the 640x480 preset really runs at 25.0 MHz (mul/div/div2
 *     15/1/60 in the firmware table) = 59.52 Hz, so 0.4 frame/s of a 59.95 fps
 *     game had to be dropped (34 in 54 s).  Custom timing 800 x 521 lines at
 *     25.0 MHz = 59.98 Hz (same mechanism as PAL50, slot 18); flag no60 keeps
 *     the preset.
 *   - diagnostics: wake-up lateness histogram, frames taken by the re-check,
 *     AHI task service time per buffer.
 *
 * rc2b (31.10.2026, launcher only, blob zzmd_rc2.bin unchanged): a wake-up that
 * comes after the predicted vblank edge no longer waits for the next edge (see
 * vb_pll_wait).  rc2 measured on the A4000: PAL Paula 49.9 displayed/s (perfect),
 * but AHI PAL 39.5 and AHI NTSC 42.6 with 20-28 % of the frames dropped and an
 * iteration rate of 40-46/s: the AHI task and mixer held the main task for a few
 * ms at a time, the loop woke after the vblank pulse and lost a whole vblank.
 *
 * Base: the public ZZPicoDriveMD 1.1 launcher (Xanxi 2026), unchanged in its
 * screen / triple-buffer / deferred-PAN / audio backends / input / saves / GUI
 * model, plus the fixes found on ZZVirtuaRacingRIX A1 and ZZPicoDrive32X rc1
 * and the ZZPicoDriveMD PAL50 A4b work.  Every change is tagged "rc2:" (or
 * "32X:" / "A1diag:" where it came from):
 *   - 100 KiB stack installed by main() with exec StackSwap (the 1.1 "__stack"
 *     is not honoured by the bebbo/libnix build: 4 KiB Shell stack, overflow at
 *     CloseWindow -> every exit Guru / freeze);
 *   - frame pacing on the ZZ9000 vblank (register 0x4C) with a software PLL,
 *     locked AFTER the final video mode is active; the period is learnt from
 *     clean edge to clean edge only; one flip per RTG vblank;
 *   - PAN ack timeout is tolerant: cancel, re-arm the same frame, count, and
 *     leave only after 10 consecutive timeouts (1.1 left at the first one);
 *   - exit: STOP, wait STATUS=STOPPED, then ARM_RUN = 0 so that Core0 flushes
 *     the shared L2 before the next blob image is copied (relaunch without any
 *     other Core1 program in between);
 *   - INIT / LOAD / START failure: the fault published by the blob (error code,
 *     ARM pc, FAR, FSR) is printed;
 *   - AHI task hardened: io_Error / io_Actual checked, abnormally fast
 *     completions detected, no request resent after repeated failure (no busy
 *     loop at priority 5), failure reported to the main loop which then exits
 *     cleanly; buffers stay at 512 frames;
 *   - PAL games on a physical 50 Hz output: the proven ZZ_PAL50 custom timing
 *     (640x480, 25 MHz, 625 lines = 50.000 Hz) programmed through the XX19d-RIX
 *     custom video mode registers under the 320x240x32 P96 screen, scaler and
 *     colour mode inherited (scanlines kept); flag "no50" keeps 60 Hz;
 *   - Del = console reset (one-shot, acknowledged); keypad 4/5/6/0 = X/Y/Z/MODE;
 *   - diagnostics kept from A1diag (SNAP at quit, WATCHDOG, EXIT steps, STACK)
 *     plus the rc2 summary: emulated / published immediate+deferred / dropped /
 *     PAN acks / lost, PCM underruns, AHI errors, exit reason.
 *   The blob fields (slots 176-199) are read when the blob identifies itself
 *   ('ZPM1' MD rc2, or 'ZVR1' 32X); the launcher also runs the 1.1 blob.
 *
 * Usage:
 *   ZZPicoDrive32X rom.bin [blob|-] [region] [flags]
 *   region: auto | eur | usa | jap
 *   flags : kbd cd32 db9 usb port=N silence paula ahi 2p inputlog osd tof gate no50 no60 pri0 norecheck
 *   (no argument = GUI; the Core1 blob is embedded; a file zz32x_v1.2.bin next to
 *    the program, or a blob file given as 2nd argument, replaces it)
 *
 * Build (bebbo m68k-amigaos-gcc, from the directory holding the sources):
 *   m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
 *     -o ZZPicoDrive32X ZZPicoDrive32X.c blob_data.c -lamiga
 *   (blob_data.c = tools/bin2c of bin/zz32x_v1.2.bin; blob_data_stub.c = no
 *    embedded blob, the blob file is then required on the command line)
 *
 * Launcher by Xanxi, 2026.  Emulation core: PicoDrive (notaz).  ASCII-only source.
 */

#include <proto/Picasso96.h>
#include <exec/types.h>
#include <exec/tasks.h>       /* 32X: StackSwapStruct */
#include <libraries/configvars.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <devices/ahi.h>
#include <devices/inputevent.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/dos.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <proto/lowlevel.h>   /* fournit JPF_* et JP_TYPE_* corrects (ReadJoyPort) */
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <stdarg.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <stdio.h>
#include <string.h>
#include <devices/audio.h>

/* blob ARM Core1 embarque (blob_data.c) : utilise si aucun fichier blob present */
extern const unsigned char g_blob_data[];
extern const unsigned long g_blob_size;

/* IntuitionBase/GfxBase declares par les proto headers */
unsigned long __stack = 100000UL;   /* 1.1: NOT honoured by the bebbo/libnix build (measured: 4 KiB Shell stack);
                                       kept for other startups, the real guarantee is the StackSwap in main() */
#define ZZ32X_STACK_SIZE 100000UL     /* 32X: stack installed by main() */
struct Library *P96Base = NULL;
struct Library *GfxBase_ = NULL;
struct Library *LowLevelBase = NULL;   /* lowlevel.library : ReadJoyPort (mode CD32/DB9) */
struct Library *GadToolsBase = NULL;   /* GUI de lancement */
struct Library *AslBase = NULL;        /* requester fichier ROM */

/* sortie debug : active en mode CLI ; en mode GUI (pas de fenetre console) elle
   va dans T:ZZPicoDrive32X.log (v1.2 RC4: a GUI start that failed said nothing) */
static int g_quiet = 0;
static BPTR g_logfh = 0;
static void qprintf(const char *fmt, ...)
{
    va_list ap;
    if(g_quiet){
        if(g_logfh){
            static char lb[512];
            va_start(ap, fmt);
            vsnprintf(lb, sizeof(lb), fmt, ap);
            va_end(ap);
            FPuts(g_logfh, (STRPTR)lb); Flush(g_logfh);
        }
        return;
    }
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);   /* A1diag: every line reaches the console before the next step runs */
}
#define printf qprintf
struct ExpansionBase *ExpansionBase = NULL;

/* ---- mesure temps haute resolution (timer.device EClock) ----
 * proto/timer.h declare TimerBase en extern struct Device* mais ne le definit
 * pas : c'est au programme de fournir la variable (timer.device s'ouvre via
 * OpenDevice). On la definit donc ici, avec le bon type. */
struct Device *TimerBase = NULL;
static struct MsgPort *g_tport = NULL;
static struct timerequest *g_treq = NULL;
static int timer_open(void)
{
    g_tport=CreateMsgPort(); if(!g_tport) return 0;
    g_treq=(struct timerequest*)CreateIORequest(g_tport,sizeof(struct timerequest));
    if(!g_treq){ DeleteMsgPort(g_tport); g_tport=NULL; return 0; }
    if(OpenDevice("timer.device",UNIT_ECLOCK,(struct IORequest*)g_treq,0)){
        DeleteIORequest((struct IORequest*)g_treq); DeleteMsgPort(g_tport);
        g_treq=NULL; g_tport=NULL; return 0; }
    TimerBase=g_treq->tr_node.io_Device;
    return 1;
}
static void timer_close(void)
{
    if(g_treq){ CloseDevice((struct IORequest*)g_treq);
                DeleteIORequest((struct IORequest*)g_treq); g_treq=NULL; }
    if(g_tport){ DeleteMsgPort(g_tport); g_tport=NULL; }
    TimerBase=NULL;
}
/* 32X: ZZ9000 vblank PLL statistics */
static ULONG g_vb_period=0, g_vb_next=0, g_vb_edges=0, g_vb_miss=0, g_vb_late=0, g_vb_pollmax=0, g_vb_pollsum=0;
static ULONG g_ack_poll_max=0, g_ack_notseen=0;   /* ack polls after the edge: worst case, and vblanks where it never came */
/* 32X: a second timer.device unit (MICROHZ) to sleep a precise number of
   microseconds without busy-waiting (the ECLOCK unit above is read-only). */
static struct MsgPort *g_sport = NULL;
static struct timerequest *g_sreq = NULL;
static int sleep_open(void)
{
    g_sport=CreateMsgPort(); if(!g_sport) return 0;
    g_sreq=(struct timerequest*)CreateIORequest(g_sport,sizeof(struct timerequest));
    if(!g_sreq){ DeleteMsgPort(g_sport); g_sport=NULL; return 0; }
    if(OpenDevice("timer.device",UNIT_MICROHZ,(struct IORequest*)g_sreq,0)){
        DeleteIORequest((struct IORequest*)g_sreq); DeleteMsgPort(g_sport);
        g_sreq=NULL; g_sport=NULL; return 0; }
    return 1;
}
static void sleep_close(void)
{
    if(g_sreq){ CloseDevice((struct IORequest*)g_sreq);
                DeleteIORequest((struct IORequest*)g_sreq); g_sreq=NULL; }
    if(g_sport){ DeleteMsgPort(g_sport); g_sport=NULL; }
}
static void sleep_us(ULONG us)
{
    if(!g_sreq || us<100) return;
    g_sreq->tr_node.io_Command=TR_ADDREQUEST;
    g_sreq->tr_time.tv_secs=us/1000000UL;
    g_sreq->tr_time.tv_micro=us%1000000UL;
    DoIO((struct IORequest*)g_sreq);
}
static ULONG eclock_now(ULONG *freq)
{
    struct EClockVal ev; ULONG f;
    if(!TimerBase){ if(freq) *freq=0; return 0; }
    f=ReadEClock(&ev);
    if(freq) *freq=f; return ev.ev_lo;
}

#define ZZ9000_MANUF    0x6D6E
#define ZZ9000_PROD     0x04
#define MNT_FB_BASE     0x00010000UL
#define REG_ARM_RUN_HI  0x0090
#define REG_ARM_RUN_LO  0x0092
/* PAN ZZ9000 direct (firmware stock) : scan-out lit a partir de pan_offset.
   X1/Y1=0 => offset plein-cadre propre (le firmware ajoute rect_x1<<cm +
   rect_y1*pitch ; a 0 ils n'ajoutent rien). */
#define REG_ZZ_PAN_HI   0x0A
#define REG_ZZ_PAN_LO   0x0C
#define REG_ZZ_X1       0x10
#define REG_ZZ_Y1       0x12
#define REG_ZZ_X2       0x14
#define REG_ZZ_COLORMODE 0x30
#define ZZ_COLORMODE_32  2
#define REG_ZZ_USER3    0x44   /* write: 1=arme UN PAN differe, 2=clear+disarm */
#define REG_ZZ_USER4    0x46   /* read : compteur ACK PAN differe (low16)       */
/* HW_VBLANK_PACING : firmware renvoie zstate & (1<<21) au registre board+0x4C. */
#define REG_ZZ_VBLANK_STATUS   0x4C
#define ZZ_WR(b,o,v)    (*((volatile UWORD*)((UBYTE*)(b)+(o))) = (UWORD)(v))
#define ZZ_RD(b,o)      (*((volatile UWORD*)((UBYTE*)(b)+(o))))

/* rc2: physical 50 Hz output for PAL games, under the 320x240x32 P96 screen.
   The XX19d-RIX firmware exposes a custom video mode slot programmable field by
   field (REG_ZZ_CVMODE_PARAM 0x52 selects the field, REG_ZZ_CVMODE_VAL 0x54 writes
   it, REG_ZZ_CVMODE_SEL 0x56 selects the slot, REG_ZZ_CVMODE 0x58 commits); the
   commit re-initialises the video with the CURRENT scalemode and colourmode
   (video_mode_init(custom, video_state->scalemode, video_state->colormode)), so
   the P96 320x240 framebuffer, its x2 scaler and the scanlines are kept.
   Timing: the 640x480 preset really runs at 25.0 MHz (firmware table: mul/div/
   div2 15/1/60 on the 100 MHz reference, although the table says 25175000 Hz),
   HT 800: with 625 lines -> 50.00 Hz (measured 20007 us), the A4b test
   (ZZPAL50Test S1) proved it on the A4000 (49.8 PAN acks/s with Paula over
   108 s).  rc2c: the same preset with its 525 lines gives 59.52 Hz (16800 us),
   not 60: a 59.95 fps NTSC game drops 0.4 frame/s on it.  521 lines -> 59.98 Hz
   (16688 us), zero structural drop; the vertical back porch goes from 33 to 29
   lines.  Not CEA/VESA timings: some monitors refuse them -> flags no50 / no60.
   Field PHZ is not written (16-bit register, 32-bit firmware field; S1 worked
   without it). */
#define ZZ_PAL50_REG_MODE     0x02
#define ZZ_PAL50_REG_PARAM    0x52
#define ZZ_PAL50_REG_VALUE    0x54
#define ZZ_PAL50_REG_SEL      0x56
#define ZZ_PAL50_REG_COMMIT   0x58
#define ZZ_PAL50_CUSTOM_SLOT  18
#define ZZ_PAL50_FIXED_MODE   (2u | (2u << 8) | (3u << 12))   /* preset 640x480, 32-bit, scale x2 */
enum zz_pal50_vmfield { ZZP_HRES=0, ZZP_VRES=1, ZZP_HSTART=2, ZZP_HEND=3, ZZP_HMAX=4, ZZP_VSTART=5,
    ZZP_VEND=6, ZZP_VMAX=7, ZZP_POLARITY=8, ZZP_MHZ=9, ZZP_PHZ=10, ZZP_VHZ=11, ZZP_HDMI=12,
    ZZP_MUL=13, ZZP_DIV=14, ZZP_DIV2=15 };
static void zz_pal50_field(volatile UBYTE *board, UWORD f, UWORD v)
{
    ZZ_WR(board, ZZ_PAL50_REG_PARAM, f);
    ZZ_WR(board, ZZ_PAL50_REG_VALUE, v);
}
#define ZZ_CTM_VMAX_PAL50   625   /* 25.0 M / (800 x 625) = 50.00 Hz */
#define ZZ_CTM_VMAX_NTSC60  521   /* 25.0 M / (800 x 521) = 59.98 Hz (preset: 525 = 59.52 Hz) */
static void zz_pal50_enable(volatile UBYTE *board, UWORD vmax, UWORD vhz)
{
    ZZ_WR(board, ZZ_PAL50_REG_SEL, ZZ_PAL50_CUSTOM_SLOT);
    zz_pal50_field(board,ZZP_HRES,640);
    zz_pal50_field(board,ZZP_VRES,480);
    zz_pal50_field(board,ZZP_HSTART,656);
    zz_pal50_field(board,ZZP_HEND,752);
    zz_pal50_field(board,ZZP_HMAX,800);
    zz_pal50_field(board,ZZP_VSTART,490);
    zz_pal50_field(board,ZZP_VEND,492);
    zz_pal50_field(board,ZZP_VMAX,vmax);
    zz_pal50_field(board,ZZP_POLARITY,0);
    zz_pal50_field(board,ZZP_MHZ,25);
    zz_pal50_field(board,ZZP_VHZ,vhz);
    zz_pal50_field(board,ZZP_HDMI,0);
    zz_pal50_field(board,ZZP_MUL,15);
    zz_pal50_field(board,ZZP_DIV,1);
    zz_pal50_field(board,ZZP_DIV2,60);
    ZZ_WR(board,ZZ_PAL50_REG_COMMIT,0);
}
static void zz_pal50_restore(volatile UBYTE *board)
{
    /* back to the fixed 640x480 preset; valid only while the 320x240x32 P96 screen
       is still open (the P96 screen close then restores the Workbench mode) */
    ZZ_WR(board,ZZ_PAL50_REG_MODE, (UWORD)ZZ_PAL50_FIXED_MODE);
}

static void zz_pan(volatile UBYTE *board, ULONG off)
{
    ZZ_WR(board, REG_ZZ_X1, 0);
    ZZ_WR(board, REG_ZZ_Y1, 0);
    ZZ_WR(board, REG_ZZ_PAN_HI, (UWORD)(off>>16));
    ZZ_WR(board, REG_ZZ_PAN_LO, (UWORD)(off&0xFFFF));
}
/* PAN differe ZZDoom : le firmware applique au vblank rising edge + incremente USER4.
   Doit etre precede de ZZ_WR(USER3,1) pour armer UN flip. */
static void zz_deferred_pan_request(volatile UBYTE *board, ULONG byte_offset, ULONG pitch_bytes)
{
    ZZ_WR(board, REG_ZZ_X1, 0);
    ZZ_WR(board, REG_ZZ_Y1, 0);
    ZZ_WR(board, REG_ZZ_X2, (UWORD)(pitch_bytes >> 2));
    ZZ_WR(board, REG_ZZ_COLORMODE, ZZ_COLORMODE_32);
    ZZ_WR(board, REG_ZZ_PAN_HI, (UWORD)(byte_offset >> 16));
    ZZ_WR(board, REG_ZZ_PAN_LO, (UWORD)(byte_offset & 0xFFFF));
}
static ULONG zz_pan_ack(volatile UBYTE *board)
{
    return (ULONG)(ZZ_RD(board, REG_ZZ_USER4) & 0xFFFF);
}

/* prototypes (rd32/wr32 sont definis plus bas, le backend AHI les utilise avant) */
static ULONG rd32(volatile UBYTE *b, ULONG o);
static void  wr32(volatile UBYTE *b, ULONG o, ULONG v);

/* ===================== BACKEND AHI (16-bit stereo, double buffer) =====================
   Joue le ring PCM stereo produit par le blob via AHI. On lit
   le ring stereo s16 de PicoDrive ((L<<16)|R via rd32, byte-order prouve) et on downmixe en
   mono. Tout le reste (M16S, ahir_Link, double-buffer event-driven, DEFAULT_UNIT) est IDENTIQUE
   le ring stereo (L<<16)|R directement. */
#define SH_PCM_BASE       (30*4)
#define SH_PCM_SIZE       (31*4)
#define SH_PCM_WRITE_POS  (32*4)
#define SH_PCM_READ_POS   (33*4)
#define SH_PCM_RATE       (34*4)
#define SH_PCM_ENABLED    (37*4)
#define SH_PCM_TESTPAT    (169*4)
/* rc2: AHI task health, written by the audio task, read by the main loop */
static volatile ULONG g_ahi_errors=0, g_ahi_fast=0, g_ahi_first_err=0;
static volatile int   g_ahi_failed=0;
#define AHI_BUF_FRAMES 512    /* frames STEREO (23ms/buf, 46ms en vol -> latence mini) */
static struct MsgPort    *ahi_mp=NULL;
static struct AHIRequest *ahi_req[2]={NULL,NULL};
static BYTE   ahi_dev_open=-1;
static WORD  *ahi_buf[2]={NULL,NULL};
ULONG zz_ahi_signal=0;
static volatile UBYTE *au_shared=NULL;
static volatile UBYTE *au_ringbase=NULL;
static ULONG au_ring_size=0, au_read_pos=0, au_rate=22050;
static ULONG ahi_refill_count=0, ahi_underrun=0;
static int g_audio_on=0, g_test_pat=0;
static int   g_silence=0;      /* stress-test : silence (aucune lecture ring)   */
static int   g_sine=0;         /* stress-test : sinus stereo L/R                */
static ULONG g_au_hz=0;        /* override frequence AHI (0 = SH_PCM_RATE)       */
static ULONG g_sine_ph=0, g_sine_ph2=0;

static ULONG pcm_rd32(volatile UBYTE *p){          /* = rd32 sur adresse ring */
    volatile UWORD *w=(volatile UWORD*)p; UWORD w0=w[0],w1=w[1];
    return ((ULONG)((UWORD)((w1>>8)|(w1<<8)))<<16)|(ULONG)((UWORD)((w0>>8)|(w0<<8)));
}
/* SOURCE PCM stereo : ring (L<<16)|R via rd32 -> ecrit L puis R (S16S), pas de downmix.
   Modes stress-test : g_silence (silence pur, aucune lecture ring) ; g_sine (L=440Hz,
   R=880Hz sawtooth, pour verifier format/pitch/gauche-droite). */
static void pull_samples(WORD *buf)
{
    int n, k;
    if(g_silence){                                   /* etape 1 : silence, zero ring */
        for(k=0;k<AHI_BUF_FRAMES*2;k++) buf[k]=0;
        ahi_refill_count++; return;
    }
    if(g_sine){                                      /* etape 2 : sinus/saw L!=R */
        ULONG incL=(440UL*65536UL)/au_rate, incR=(880UL*65536UL)/au_rate;
        for(n=0;n<AHI_BUF_FRAMES;n++){
            g_sine_ph=(g_sine_ph+incL)&0xFFFF;
            g_sine_ph2=(g_sine_ph2+incR)&0xFFFF;
            buf[2*n]  =(WORD)(((LONG)g_sine_ph  - 32768) >> 2);   /* L 440Hz */
            buf[2*n+1]=(WORD)(((LONG)g_sine_ph2 - 32768) >> 2);   /* R 880Hz */
        }
        ahi_refill_count++; return;
    }
    { ULONG wr = rd32(au_shared, SH_PCM_WRITE_POS);  /* etape 3 : ring reel */
      ULONG avail;
      n=0;
      for(k=0;k<AHI_BUF_FRAMES*2;k++) buf[k]=0;
      if(wr >= au_read_pos) avail = wr - au_read_pos;
      else                  avail = au_ring_size - au_read_pos + wr;
      while(n < AHI_BUF_FRAMES && avail >= 4){        /* 4 octets = 1 paire stereo */
          ULONG v = pcm_rd32(au_ringbase + au_read_pos);   /* (L<<16)|R natif */
          buf[2*n]   = (WORD)(v >> 16);                /* L */
          buf[2*n+1] = (WORD)(v & 0xFFFF);             /* R */
          n++;
          au_read_pos += 4;
          if(au_read_pos >= au_ring_size) au_read_pos = 0;
          avail -= 4;
      }
      wr32(au_shared, SH_PCM_READ_POS, au_read_pos);
      if(n < AHI_BUF_FRAMES) ahi_underrun++;
      ahi_refill_count++;
    }
}
static void prepare_and_send(int idx, struct AHIRequest *link)
{
    pull_samples(ahi_buf[idx]);
    ahi_req[idx]->ahir_Std.io_Flags   = 0;
    ahi_req[idx]->ahir_Std.io_Command = CMD_WRITE;
    ahi_req[idx]->ahir_Std.io_Data    = ahi_buf[idx];
    ahi_req[idx]->ahir_Std.io_Length  = AHI_BUF_FRAMES * 4;
    ahi_req[idx]->ahir_Std.io_Offset  = 0;
    ahi_req[idx]->ahir_Frequency      = au_rate;
    ahi_req[idx]->ahir_Type           = AHIST_S16S;
    ahi_req[idx]->ahir_Volume         = 0x10000;
    ahi_req[idx]->ahir_Position       = 0x8000;
    ahi_req[idx]->ahir_Link           = link;
    SendIO((struct IORequest*)ahi_req[idx]);
}
static int zz_ahi_init(volatile UBYTE *shared, volatile UBYTE *ring_cpu)
{
    int i;
    au_shared=shared; au_ringbase=ring_cpu;
    au_ring_size=rd32(shared,SH_PCM_SIZE); au_rate=rd32(shared,SH_PCM_RATE);
    /* rate = production reelle du blob (auto-cadence 50Hz -> 22050/s, PAS liee au FPS RTG) */
    if(g_au_hz) au_rate=g_au_hz; if(au_rate==0) au_rate=22050;
    /* demarrer 1024 paires DERRIERE write_pos : petit coussin anti-gigue (46ms), latence mini,
       pas de backlog -> 0 underrun ET pas de retard */
    { ULONG wp=rd32(shared,SH_PCM_WRITE_POS), cush=1024UL*4UL;   /* 46ms (etait 70ms) */
      au_read_pos = (wp>=cush)? (wp-cush) : (wp + au_ring_size - cush); }
    ahi_mp=(struct MsgPort*)CreateMsgPort();
    if(!ahi_mp){ printf("AHI: no msgport\n"); return 0; }
    zz_ahi_signal=1UL<<ahi_mp->mp_SigBit;
    ahi_req[0]=(struct AHIRequest*)CreateIORequest(ahi_mp,sizeof(struct AHIRequest));
    if(!ahi_req[0]){ printf("AHI: no ioreq\n"); return 0; }
    ahi_req[0]->ahir_Version=4;
    ahi_dev_open=OpenDevice(AHINAME,0,(struct IORequest*)ahi_req[0],0);
    if(ahi_dev_open){ printf("AHI: OpenDevice failed\n"); return 0; }
    ahi_req[1]=(struct AHIRequest*)AllocMem(sizeof(struct AHIRequest),MEMF_PUBLIC|MEMF_CLEAR);
    if(!ahi_req[1]){ printf("AHI: no req[1]\n"); return 0; }
    *ahi_req[1]=*ahi_req[0];
    for(i=0;i<2;i++){
        ahi_buf[i]=(WORD*)AllocMem(AHI_BUF_FRAMES*4,MEMF_PUBLIC|MEMF_CLEAR);
        if(!ahi_buf[i]){ printf("AHI: no buf[%d]\n",i); return 0; }
    }
    printf("AUDIO: src_rate=%lu ahi_rate=%lu ring_size=%lu unit=0 buf=%d\n",
           rd32(shared,SH_PCM_RATE), au_rate, au_ring_size, AHI_BUF_FRAMES);
    return 1;
}
static void zz_ahi_start(void){
    prepare_and_send(0, NULL);
    prepare_and_send(1, ahi_req[0]);
}
/* rc2: hardened completion handler.  1.1 resent a request as soon as it came
   back, whatever its result: a request refused by the driver comes back at once
   with io_Error set, and the task then looped at priority 5 without ever
   waiting (black screen, chopped sound, Workbench half frozen, mouse alive).
   Here every completion is checked (io_Error, io_Actual, and an abnormally
   short interval = less than a quarter of the buffer duration); after 16 such
   completions in a row the request is NOT resent: the task has nothing left to
   wait for but CTRL_C, and the main loop sees g_ahi_failed and leaves cleanly. */
static ULONG ahi_last_done=0, ahi_bad_consecutive=0;
static ULONG g_ahi_svc_max=0, g_ahi_svc_sum=0, g_ahi_svc_n=0;   /* rc2c: EClock ticks spent per refill (ring reads + SendIO) */
static void zz_ahi_service(void){
    struct AHIRequest *done;
    while((done=(struct AHIRequest*)GetMsg(ahi_mp))){
        int idx=(done==ahi_req[0])?0:1, bad=0;
        ULONG now=eclock_now(NULL), f=0;
        eclock_now(&f);
        if(done->ahir_Std.io_Error){ bad=1; g_ahi_errors++; if(!g_ahi_first_err) g_ahi_first_err=(ULONG)(UBYTE)done->ahir_Std.io_Error; }
        else if(done->ahir_Std.io_Actual && done->ahir_Std.io_Actual < (ULONG)(AHI_BUF_FRAMES*4)/2UL){ bad=1; g_ahi_errors++; }
        if(f && ahi_last_done){
            ULONG dt=now-ahi_last_done, quarter=(f/au_rate)*(ULONG)AHI_BUF_FRAMES/4UL;
            if(dt < quarter){ g_ahi_fast++; bad=1; }
        }
        ahi_last_done=now;
        if(bad){
            if(++ahi_bad_consecutive>=16UL){ g_ahi_failed=1; continue; }   /* give up: no resend, no spin */
            Delay(1);                                                       /* transient: breathe, then retry */
        } else ahi_bad_consecutive=0;
        if(g_ahi_failed) continue;
        { ULONG s0=eclock_now(NULL), ds;
          if(idx==0) prepare_and_send(0, ahi_req[1]);
          else       prepare_and_send(1, ahi_req[0]);
          ds=eclock_now(NULL)-s0; g_ahi_svc_sum+=ds; g_ahi_svc_n++; if(ds>g_ahi_svc_max) g_ahi_svc_max=ds; }
    }
}
static void zz_ahi_stop(void){
    int i;
    { ULONG f=0; eclock_now(&f); f=f?f/1000UL:1UL;   /* ticks per ms */
      printf("AHI: refills=%lu underruns=%lu  task service per buffer: max=%lu us avg=%lu us (n=%lu)\n",
             ahi_refill_count, ahi_underrun, g_ahi_svc_max*1000UL/f, g_ahi_svc_n?(g_ahi_svc_sum/g_ahi_svc_n)*1000UL/f:0UL, g_ahi_svc_n); }
    for(i=0;i<2;i++) if(ahi_req[i]){ AbortIO((struct IORequest*)ahi_req[i]);
                                     WaitIO((struct IORequest*)ahi_req[i]); }
    if(ahi_dev_open==0){ CloseDevice((struct IORequest*)ahi_req[0]); ahi_dev_open=-1; }
    if(ahi_req[0]) DeleteIORequest((struct IORequest*)ahi_req[0]);
    if(ahi_req[1]) FreeMem(ahi_req[1],sizeof(struct AHIRequest));
    for(i=0;i<2;i++) if(ahi_buf[i]) FreeMem(ahi_buf[i],AHI_BUF_FRAMES*4);
    if(ahi_mp) DeleteMsgPort(ahi_mp);
}

/* ---- AUDIO_STEREO dans une TACHE SEPAREE (event-driven sur le signal AHI) ----
   Isole tout l'audio de la boucle video -> le mixage AHI ne preempte plus le timing
   du flip -> FPS video preserve. La tache DORT sur le reply port AHI et ne se reveille
   que quand un buffer est fini (event-driven). */
static volatile int g_audio_stop=0;
static struct Task    *g_main_task=NULL;
static BYTE            g_done_sig=-1;
static struct Process *g_audio_proc=NULL;
static volatile UBYTE *g_task_shared=NULL, *g_task_ring=NULL;

static void audio_task(void){
    if(!zz_ahi_init(g_task_shared, g_task_ring)){ zz_ahi_stop();
        if(g_main_task) Signal(g_main_task,1UL<<g_done_sig); return; }
    zz_ahi_start();
    { ULONG portmask=1UL<<ahi_mp->mp_SigBit;
      while(!g_audio_stop){
          ULONG sigs=Wait(portmask | SIGBREAKF_CTRL_C);   /* dort jusqu'a buffer fini */
          if(sigs & SIGBREAKF_CTRL_C) break;
          zz_ahi_service();
          /* rc2: after a failure nothing is resent; the only wake-up left is CTRL_C
             from the main loop, so this task can never spin at priority 5 */
      } }
    zz_ahi_stop();
    if(g_main_task) Signal(g_main_task,1UL<<g_done_sig);
}

/* ===================== BACKEND PAULA DMA DIRECT (integre, selectionnable via cfg->audio) =====
   Option A validee : canaux AUD0+AUD1 reserves via audio.device, Paula pilote EN DIRECT un ring
   continu Chip RAM. Curseur EClock + guard band (PAL) + rate-match NTSC. Reutilise
   au_shared/au_ringbase/au_ring_size/au_read_pos/au_rate/pcm_rd32/eclock_now/g_silence/g_au_hz
   du backend AHI (un seul backend actif a la fois). Remplissage sur WaitTOF (boucle principale). */
#define PA_DMACON  (0x096/2)
static volatile UWORD *pa_cust=(volatile UWORD*)0xDFF000;
static struct MsgPort *pa_mp=NULL;
static struct IOAudio *pa_alloc=NULL;
static int    pa_open=0;
static UBYTE  pa_gotmask=0;
static BYTE  *pa_ringL=NULL, *pa_ringR=NULL;
static ULONG pa_spf=505, pa_adv=441, pa_actual_rate=22030;
static ULONG PA_LEAD_FR=3, PA_RING_FR=24;   /* lead 3 frames ~69ms (etait 5=115ms) */
static ULONG pa_ring=0, pa_lead=0, pa_period=161;
static ULONG pa_p_wrote=0, pa_p_play_est=0;
static ULONG pa_underruns=0, pa_fills=0, pa_resyncs=0;
static long  pa_fill_min=0x7fffffffL, pa_fill_max=-0x7fffffffL;
static UWORD pa_regL=0x0A0, pa_regR=0x0B0;
static ULONG pa_last_eclk=0, pa_efreq=0;
static int   pa_have_clk=0;
static unsigned long long pa_eclk_acc=0;
#define PA_NTSC_HZ  21515u
static int   pa_ntsc=0;
static int   g_pa_active=0;      /* 1 = backend Paula en service (rempli sur WaitTOF) */

static void pa_ch_regs(UWORD base, ULONG ptr, ULONG len_words){
    volatile UWORD *c=pa_cust;
    c[base/2]      = (UWORD)(ptr>>16);
    c[base/2 + 1]  = (UWORD)ptr;
    c[(base+4)/2]  = (UWORD)len_words;
    c[(base+6)/2]  = (UWORD)pa_period;
    c[(base+8)/2]  = 0;
}

static void pa_fill(void){
    ULONG wr, r; long target, cap;
    if(!pa_ringL) return;
    cap    = (long)pa_p_play_est + (long)(pa_ring - pa_spf);
    target = (long)pa_p_play_est + (long)pa_lead;
    if(target > cap) target = cap;
    wr=rd32(au_shared,SH_PCM_WRITE_POS); r=au_read_pos;
    while((long)(target - (long)pa_p_wrote) > 0){
        ULONG avail = (wr>=r)? (wr-r) : (au_ring_size - r + wr);
        ULONG pos;
        if(avail<4) break;
        pos = pa_p_wrote % pa_ring;
        if(g_silence){ pa_ringL[pos]=0; pa_ringR[pos]=0; }
        else { ULONG v=pcm_rd32(au_ringbase+r);
               pa_ringL[pos]=(BYTE)((WORD)(v>>16)>>8);
               pa_ringR[pos]=(BYTE)((WORD)(v&0xFFFF)>>8); }
        r+=4; if(r>=au_ring_size) r=0;
        pa_p_wrote++;
    }
    au_read_pos=r; wr32(au_shared,SH_PCM_READ_POS,r);
}

static int pa_init(volatile UBYTE *shared, volatile UBYTE *ring_cpu){
    static UBYTE map01[]={0x03};
    au_shared=shared; au_ringbase=ring_cpu;
    au_ring_size=rd32(shared,SH_PCM_SIZE); au_rate=rd32(shared,SH_PCM_RATE);
    if(g_au_hz) au_rate=g_au_hz; if(au_rate==0) au_rate=22050;
    { ULONG prate = au_rate;   /* v1.1 (SMS #4b) : 22050 dans les DEUX modes ; l'audio-slaving
                                  du blob fait suivre le debit reel de Paula (ring ne deborde plus).
                                  (etait: pa_ntsc ? PA_NTSC_HZ : au_rate) */
      pa_period=(UWORD)((3546895UL + prate/2)/prate); }
    pa_actual_rate = 3546895UL/(ULONG)pa_period;
    pa_have_clk=0; pa_eclk_acc=0; pa_efreq=0;
    pa_adv = au_rate/50;
    pa_spf = au_rate/50 + 64;
    pa_ring = PA_RING_FR * pa_spf;
    pa_lead = PA_LEAD_FR * pa_spf;
    { ULONG wp=rd32(shared,SH_PCM_WRITE_POS), cush=pa_lead*4UL;
      au_read_pos=(wp>=cush)?(wp-cush):(wp+au_ring_size-cush); }
    pa_mp=(struct MsgPort*)CreateMsgPort(); if(!pa_mp){ printf("PA: no port\n"); return 0; }
    pa_alloc=(struct IOAudio*)CreateIORequest(pa_mp,sizeof(struct IOAudio));
    if(!pa_alloc){ printf("PA: no ioreq\n"); return 0; }
    pa_alloc->ioa_Request.io_Message.mn_Node.ln_Pri=0;
    pa_alloc->ioa_Data=map01; pa_alloc->ioa_Length=sizeof(map01);
    if(OpenDevice("audio.device",0,(struct IORequest*)pa_alloc,0)){ printf("PA: OpenDevice KO\n"); return 0; }
    pa_open=1;
    pa_gotmask=(UBYTE)(ULONG)pa_alloc->ioa_Request.io_Unit;
    if(pa_gotmask!=0x03){ printf("PA: alloc != AUD0+AUD1 (0x%lx) -> ABANDON\n",(ULONG)pa_gotmask); return 0; }
    pa_regL=0x0A0; pa_regR=0x0B0;
    pa_ringL=(BYTE*)AllocMem(pa_ring,MEMF_CHIP|MEMF_CLEAR);
    pa_ringR=(BYTE*)AllocMem(pa_ring,MEMF_CHIP|MEMF_CLEAR);
    if(!pa_ringL||!pa_ringR){ printf("PA: AllocMem CHIP KO\n"); return 0; }
    pa_p_wrote=0; pa_p_play_est=0; pa_underruns=0; pa_fills=0; pa_resyncs=0;
    pa_fill_min=0x7fffffffL; pa_fill_max=-0x7fffffffL;
    printf("PAULA DMA: %s AUD0/AUD1 rate=%lu per=%lu ring=%lu lead=%lu guard=%s\n",
           pa_ntsc?"NTSC(rate-match)":"PAL", pa_actual_rate,(ULONG)pa_period,pa_ring,pa_lead,
           pa_ntsc?"OFF":"ON");
    return 1;
}

static void pa_start(void){
    volatile UWORD *c=pa_cust;
    pa_fill();
    CacheClearE(pa_ringL,pa_ring,CACRF_ClearD);
    CacheClearE(pa_ringR,pa_ring,CACRF_ClearD);
    Disable();
    pa_ch_regs(pa_regL,(ULONG)pa_ringL,pa_ring/2);
    pa_ch_regs(pa_regR,(ULONG)pa_ringR,pa_ring/2);
    c[pa_regL/2 + 4] = 48;
    c[pa_regR/2 + 4] = 48;
    c[PA_DMACON] = 0x8203;
    Enable();
    printf("PAULA DMA: start (DMACON=0x8203)\n");
}

/* appele sur WaitTOF : curseur EClock + resync + fill + guard band + flush */
static void pa_service(void){
    long fill;
    if(!pa_ringL) return;
    { ULONG now, delta, adv;
      now = eclock_now(&pa_efreq);
      if(pa_have_clk){
          delta = now - pa_last_eclk;
          if(pa_efreq && delta > pa_efreq/4) delta = pa_efreq/50;
      } else { delta = pa_efreq ? pa_efreq/50 : 0; pa_have_clk=1; }
      pa_last_eclk = now;
      if(pa_efreq){
          pa_eclk_acc += (unsigned long long)delta * (unsigned long long)pa_actual_rate;
          adv = (ULONG)(pa_eclk_acc / pa_efreq);
          pa_eclk_acc %= pa_efreq;
      } else adv = pa_adv;
      pa_p_play_est += adv;
    }
    if((long)((long)pa_p_play_est - (long)pa_p_wrote) > 0){
        pa_underruns++;
        if((long)((long)pa_p_play_est - (long)pa_p_wrote) > (long)pa_lead){
            memset(pa_ringL,0,pa_ring); memset(pa_ringR,0,pa_ring);
            pa_p_wrote = pa_p_play_est; pa_resyncs++;
        }
    }
    pa_fill();
    { ULONG gpos=pa_p_wrote % pa_ring, glen=(pa_ntsc?0u:pa_adv), first;
      if(glen>pa_ring) glen=pa_ring;
      if(glen){
        if(gpos+glen<=pa_ring){ memset(pa_ringL+gpos,0,glen); memset(pa_ringR+gpos,0,glen); }
        else { first=pa_ring-gpos;
               memset(pa_ringL+gpos,0,first); memset(pa_ringR+gpos,0,first);
               memset(pa_ringL,0,glen-first); memset(pa_ringR,0,glen-first); } } }
    fill = (long)pa_p_wrote - (long)pa_p_play_est;
    if(fill < pa_fill_min) pa_fill_min=fill;
    if(fill > pa_fill_max) pa_fill_max=fill;
    pa_fills++;
    CacheClearE(pa_ringL,pa_ring,CACRF_ClearD);
    CacheClearE(pa_ringR,pa_ring,CACRF_ClearD);
}

static void pa_stop(void){
    volatile UWORD *c=pa_cust;
    printf("PAULA DMA: rate=%lu fills=%lu underruns=%lu resyncs=%lu fill_min=%ld fill_max=%ld\n",
           pa_actual_rate, pa_fills, pa_underruns, pa_resyncs, pa_fill_min, pa_fill_max);
    Disable();
    c[pa_regL/2 + 4]=0; c[pa_regR/2 + 4]=0;
    c[PA_DMACON]=0x000F;
    Enable();
    { volatile int i; for(i=0;i<20000;i++){} }
    if(pa_open){ CloseDevice((struct IORequest*)pa_alloc); pa_open=0; }
    if(pa_alloc) DeleteIORequest((struct IORequest*)pa_alloc);
    if(pa_mp) DeleteMsgPort(pa_mp);
    if(pa_ringL) FreeMem(pa_ringL,pa_ring);
    if(pa_ringR) FreeMem(pa_ringR,pa_ring);
    pa_ringL=pa_ringR=NULL;
}



/* HW vblank : firmware renvoie zstate & (1<<21) au registre board+0x4C.
   Lecture LONG (assemble les 2 mots) -> 0 ou 0x00200000. */
#define ZZ_VBL(b)  ((*(volatile ULONG*)((UBYTE*)(b)+REG_ZZ_VBLANK_STATUS)) & 0x00200000UL)

/* sonde : le bit vblank bat-il a 50-60 Hz ? (compte les transitions ~60 ms) */
static int probe_vblank(volatile UBYTE *board)
{
    ULONG f=0, t0, prev, edges=0, dur;
    t0=eclock_now(&f); if(f==0) return 0;
    dur=f/16;                        /* ~62 ms */
    prev=ZZ_VBL(board);
    while((eclock_now(NULL)-t0) < dur){
        ULONG cur=ZZ_VBL(board);
        if(cur!=prev){ edges++; prev=cur; }
    }
    return (edges>=2)?1:0;            /* le bit bouge -> vblank exploitable */
}

/* attend le front montant (entree en vblank), avec timeout anti-blocage. */
static void wait_vblank_edge(volatile UBYTE *board, ULONG spins)
{
    ULONG prev=ZZ_VBL(board);
    while(spins--){
        ULONG cur=ZZ_VBL(board);
        if(!prev && cur) return;     /* 0 -> non-zero = debut de vblank */
        prev=cur;
    }
}

/* ---- 32X: pacing on the ZZ9000 vblank, software PLL ----
   The firmware applies a deferred PAN at the rising edge of its vblank and
   increments the ack counter there.  1.1 woke on the Amiga WaitTOF, out of
   phase with that edge, and saw the ack one vblank late half of the time (one
   flip out of two).  Here the loop predicts the next ZZ9000 edge from the
   measured period, sleeps on timer.device until ~0.8 ms before it, then polls
   register 0x4C for the edge (a few hundred reads at most), and re-locks the
   prediction on every observed edge.  Fallback: WaitTOF (flag tof, or no edge). */
#define VB_MARGIN_US   800UL
static ULONG g_vb_inpulse=0;   /* woke up inside the vblank pulse: the edge had just passed */
static ULONG g_vb_last_clean=0; /* rc2: EClock of the last clean edge (0 = none since an inpulse/miss) */
static ULONG g_vb_edge_unc=0;   /* rc2c: ticks between the last low sample and the high one (edge uncertainty) */
static ULONG g_vb_lat_hist[6];  /* rc2c: wake-up lateness after the predicted edge: <1 <2 <4 <8 <16 >=16 ms */
static ULONG g_vb_lat_max_us=0, g_vb_lat_max_at=0;   /* rc2k: EClock of the worst wake-up */
static LONG  g_loop_pri=0; static int g_loop_pri_raised=0; static ULONG g_t_loop=0;
static ULONG g_vb_ref_e=0;      /* rc2d: EClock of the reference clean edge (0 = none) */
static ULONG g_vb_ref_n=0;      /* rc2d: vblanks between the reference and the last clean edge */
static ULONG g_vb_learn=0, g_vb_unc_rej=0, g_vb_relocks=0;   /* rc2d: counters */
static ULONG g_vb_win_iter=0, g_vb_win_clean=0, g_vb_win_bad=0, g_vb_reinit=0;   /* rc2d: per 64 iterations */
/* rc2d: a lock lost for good (the output timing changed under us) shows as
   edge-to-edge intervals outside +-10 % of the period (8 or more in 64
   iterations; a locked run has none) or as almost no clean edge (fewer than 16
   in 64; locked runs show 70-96 %): the PLL is then re-measured from scratch. */
static int vb_pll_init(volatile UBYTE *board, ULONG efreq);
#define VB_LOCK_CHECK(is_clean) do{ if(is_clean) g_vb_win_clean++; \
        if(++g_vb_win_iter>=64UL){ int lost=(g_vb_win_clean<16UL || g_vb_win_bad>=8UL); g_vb_win_iter=0; g_vb_win_clean=0; g_vb_win_bad=0; \
            if(lost){ g_vb_reinit++; vb_pll_init(board, efreq); } } }while(0)
#define VB_NOACK 0xFFFFFFFFUL
static ULONG g_vb_by_ack=0;     /* rc2e: edges detected by the PAN ack counter (pulse hidden by an interruption) */
static ULONG g_vb_pulse=0;      /* rc2f: vblank pulse width in EClock ticks (measured at init; NTSC60 ~1.3 ms, PAL50 ~4.6 ms) */
static ULONG g_vb_by_gap=0;     /* rc2f: edges assumed from a sampling gap covering the predicted edge (no PAN in flight) */
static int   g_vb_path=0;       /* rc2g: how the last vb_pll_wait returned: 1 clean, 2 inpulse, 3 late, 4 by ack, 5 by gap, 6 miss */
/* pred/use_pred: the predicted edge time, used only when no PAN is in flight */
static ULONG vb_poll_edge(volatile UBYTE *board, ULONG max_ticks, int accept_high, ULONG ack_before, ULONG pred, int use_pred)
{
    ULONG t0=eclock_now(NULL), tl=t0, prev=ZZ_VBL(board), n=1;
    g_vb_edge_unc=0;
    if(prev && accept_high){ g_vb_inpulse++; g_vb_path=2; return t0?t0:1UL; }   /* already in vblank: edge just passed */
    for(;;){
        ULONG cur=ZZ_VBL(board); n++;
        if(!prev && cur){ ULONG t=eclock_now(NULL); g_vb_edge_unc=t-tl; g_vb_pollsum+=n; if(n>g_vb_pollmax) g_vb_pollmax=n; g_vb_path=1; return t?t:1UL; }
        /* rc2f: no PAN in flight -> no counter to watch.  Two low samples more than
           3/4 of a pulse apart, with the predicted edge in between: the pulse was
           hidden by the interruption.  Edge = prediction, uncertainty = the gap. */
        if(use_pred && ack_before==VB_NOACK && g_vb_pulse && !cur){
            ULONG t=eclock_now(NULL), gap=t-tl;
            if(gap > g_vb_pulse - g_vb_pulse/4 && (LONG)(pred - tl) >= 0 && (LONG)(t - pred) > 0){
                g_vb_edge_unc=gap; g_vb_by_gap++; g_vb_pollsum+=n; if(n>g_vb_pollmax) g_vb_pollmax=n; g_vb_path=5;
                return pred?pred:1UL;
            }
        }
        /* rc2e: the pulse may have come and gone while we were interrupted (NTSC:
           41 lines = 1.3 ms).  The PAN ack counter is bumped by the firmware at
           that same rising edge: changed -> the edge passed between the last
           sample and now.  Middle of the gap, uncertainty = the gap. */
        if(ack_before!=VB_NOACK && zz_pan_ack(board)!=ack_before){
            ULONG t=eclock_now(NULL), gap=t-tl;
            if(n==2 && gap < max_ticks/8) gap=max_ticks/8;   /* changed before our first sample: the edge is somewhere in the pulse before (unknown, ~2 periods/8) */
            g_vb_edge_unc=gap; g_vb_by_ack++; g_vb_pollsum+=n; if(n>g_vb_pollmax) g_vb_pollmax=n; g_vb_path=4;
            t-=gap/2; return t?t:1UL;
        }
        prev=cur;
        tl=eclock_now(NULL);           /* rc2c: time of the last sample seen low */
        if((tl-t0) > max_ticks) return 0;
    }
}
/* measure the period over 4 edges; returns 1 when the PLL is usable */
static int g_vb_init_fail=0;   /* 32X v1.2: why the last vb_pll_init() failed (1-4 edge, 5 period) */
static ULONG g_vb_init_per=0;  /* 32X v1.2: period measured by the last attempt (EClock ticks) */
static int vb_pll_init(volatile UBYTE *board, ULONG efreq)
{
    ULONG e0, e1, per, lo, hi;
    g_vb_init_fail=0; g_vb_init_per=0;
    if(!efreq || !g_sreq){ g_vb_init_fail=9; return 0; }
    lo=efreq/120; hi=efreq/40;                     /* 8.3 .. 25 ms */
    e0=vb_poll_edge(board, efreq/20, 0, VB_NOACK, 0, 0); if(!e0){ g_vb_init_fail=1; return 0; }
#define VB_MEASURE_PULSE(e) do{ /* rc2f: pulse width = rising edge -> falling edge (bounded poll); rc2i: keep the shortest */ \
      ULONG tf_=(e), lim_=efreq/50, w_; \
      while(ZZ_VBL(board)){ tf_=eclock_now(NULL); if((tf_-(e)) > lim_) break; } \
      w_=tf_-(e); if(w_ >= efreq/5000 && w_ <= lim_ && (pw==0 || w_<pw)) pw=w_; }while(0)
    { ULONG pw=0;
      VB_MEASURE_PULSE(e0);
      e1=vb_poll_edge(board, efreq/20, 0, VB_NOACK, 0, 0); if(!e1){ g_vb_init_fail=2; return 0; }
      VB_MEASURE_PULSE(e1);
      e1=vb_poll_edge(board, efreq/20, 0, VB_NOACK, 0, 0); if(!e1){ g_vb_init_fail=3; return 0; }
      VB_MEASURE_PULSE(e1);
      e1=vb_poll_edge(board, efreq/20, 0, VB_NOACK, 0, 0); if(!e1){ g_vb_init_fail=4; return 0; }
      g_vb_pulse = pw ? pw : efreq/700;   /* none plausible: assume ~1.4 ms */
    }
    per=(e1-e0)/3; g_vb_init_per=per;
    if(per<lo || per>hi){ g_vb_init_fail=5; return 0; }
    g_vb_period=per; g_vb_next=e1+per; g_vb_edges+=4; g_vb_last_clean=e1;
    g_vb_ref_e=e0; g_vb_ref_n=3; g_vb_win_iter=0; g_vb_win_clean=0; g_vb_win_bad=0;
    return 1;
}
/* sleep until the predicted edge, catch it, re-lock.  Always returns after at
   most ~2 periods. */
static void vb_pll_wait(volatile UBYTE *board, ULONG efreq, ULONG ack_before)
{
    ULONG now=eclock_now(NULL), e;
    LONG ahead=(LONG)(g_vb_next - now);                       /* EClock ticks until the predicted edge */
    LONG margin=(LONG)((efreq/1000UL)*VB_MARGIN_US/1000UL);  /* 0.8 ms in ticks */
    if(ahead > margin){
        ULONG us=(ULONG)(ahead - margin) * 1000UL / (efreq/1000UL);   /* ticks -> us (efreq ~709 kHz) */
        sleep_us(us);
        now=eclock_now(NULL); ahead=(LONG)(g_vb_next - now);
    }
    /* rc2c: lateness histogram (how late after the predicted edge did we wake up) */
    if(ahead < 0){
        ULONG lat_us=(ULONG)(-ahead) * 1000UL / (efreq/1000UL);
        if(lat_us>g_vb_lat_max_us){ g_vb_lat_max_us=lat_us; g_vb_lat_max_at=now; }
        g_vb_lat_hist[lat_us<1000UL?0:lat_us<2000UL?1:lat_us<4000UL?2:lat_us<8000UL?3:lat_us<16000UL?4:5]++;
    }
    /* rc2b: woke up AFTER the predicted edge (the task was held by something else:
       the AHI task and the AHI mixer in particular, several ms at a time).  If the
       vblank bit is still high the edge just passed and the normal path below takes
       it at once (inpulse).  If the bit is already low, the pulse is over: do NOT
       wait for the next edge - that cost a whole vblank per late wake-up (AHI runs
       of rc2: 20-25 % of the frames dropped, iteration rate 40/s).  Run the
       iteration now: the PAN of the previous frame was applied at the edge we
       missed (its ack is there), the next frame is taken and its PAN is applied at
       the next edge.  One frame of extra latency once, no frame lost. */
    /* rc2e: with a PAN in flight the ack counter tells whether the edge really
       passed; unchanged -> the prediction was early, poll for the edge instead */
    if(ahead < 0 && !ZZ_VBL(board) && (ack_before==VB_NOACK || zz_pan_ack(board)!=ack_before)){
        g_vb_late++; g_vb_path=3;
        while((LONG)(g_vb_next - now) < 0) g_vb_next+=g_vb_period;   /* keep the phase, skip the missed edges */
        g_vb_last_clean=0;
        VB_LOCK_CHECK(0);
        return;
    }
    { ULONG inpulse_before=g_vb_inpulse;
      e=vb_poll_edge(board, g_vb_period*2UL, 1, ack_before, g_vb_next, 1);
      if(e){
        if(g_vb_inpulse!=inpulse_before){
            /* rc2: woke inside the pulse: the edge time is not known precisely,
               do not learn from it.
               rc2c: the edge passed somewhere between the prediction and now.
               rc2/rc2b took now: a wake-up late by d moved the phase later by d,
               the next wake-up fell inside the pulse again, and so on (PAL50 pulse
               = 145 lines = 4.6 ms: 74 % inpulse with AHI).  Estimate = the
               prediction when we woke after it, the wake-up time (an upper bound
               of the edge) when we woke before it: the phase never drifts later,
               and converges earlier until clean edges are seen again. */
            if((LONG)(e - g_vb_next) > 0) e=g_vb_next;
            g_vb_last_clean=0;
            VB_LOCK_CHECK(0);
        } else {
            VB_LOCK_CHECK(1);
            /* rc2: a clean edge.  Learn the period only from the previous clean
               edge, so that late wake-ups never bias the estimate (rc1 drifted
               from 16675 to 16800 us and lost ~1 % of the vblanks).
               rc2c: an edge whose detection was interrupted (more than ~200 us
               between the last low sample and the high one: the task was
               preempted while polling) is late by an unknown amount; it is not
               used to learn and the phase takes the middle of the interval.
               Learning only the +d intervals (the -d one that follows is usually
               an inpulse, not learnt) is what pushed the period to 20376 us. */
            ULONG unc=g_vb_edge_unc, unc_max=efreq/5000UL;   /* 200 us */
            if(unc > unc_max){ g_vb_last_clean=0; e-=unc/2; g_vb_unc_rej++; }
            else {
                /* rc2d: an interval between consecutive clean edges outside +-10 % of
                   the period counts toward the lost-lock test (VB_LOCK_CHECK) */
                if(g_vb_last_clean){
                    ULONG d=e-g_vb_last_clean;
                    if(!(d > g_vb_period-g_vb_period/10 && d < g_vb_period+g_vb_period/10)){ g_vb_win_bad++; g_vb_relocks++; }
                }
                g_vb_last_clean=e;
                /* rc2d: long-baseline period.  The edge-to-edge IIR of rc2/rc2c drifted
                   (PAL AHI: 20287 us learnt for 20011): whatever biases one edge by a
                   few tens of us, dividing by the number of vblanks since the reference
                   edge makes it negligible.  n is rounded with the current estimate;
                   it is safe as long as n * (relative error) < 0.5, which holds
                   because the estimate is refined from n = 8 on. */
                if(!g_vb_ref_e){ g_vb_ref_e=e; g_vb_ref_n=0; }
                else {
                    ULONG dt=e-g_vb_ref_e, n=(dt + g_vb_period/2UL)/g_vb_period;
                    if(n>=1 && n<4096UL){
                        ULONG per=dt/n;
                        if(per > g_vb_period-g_vb_period/10 && per < g_vb_period+g_vb_period/10){
                            g_vb_ref_n=n;
                            if(n>=8){ g_vb_period=per; g_vb_learn++; }
                            if(n>=2048UL){ g_vb_ref_e=e; g_vb_ref_n=0; }   /* re-base (EClock wrap safety) */
                        }
                    } else { g_vb_ref_e=e; g_vb_ref_n=0; }
                }
            }
        }
        g_vb_next=e+g_vb_period; g_vb_edges++;
      } else { g_vb_next+=g_vb_period; g_vb_miss++; g_vb_last_clean=0; g_vb_path=6; VB_LOCK_CHECK(0); }
    }
}

#define FB_BLOB         0x04300000UL
#define FB_SHARED       0x04500000UL
#define FB_ROM          0x04900000UL
#define ARM_BLOB        0x04500000UL
#define ARM_ROM         0x04B00000UL

#define ZZPICO_MAGIC    0x5A504943UL
#define ROM_PAD         256

/* offsets slots (octet = index*4) */
#define SH_MAGIC        (0*4)
#define SH_STATUS       (2*4)
#define SH_COMMAND      (3*4)
#define SH_CMD_SEQ      (4*4)
#define SH_ACK_SEQ      (5*4)
#define SH_ERROR        (8*4)
#define SH_ROM_ADDR     (10*4)
#define SH_ROM_SIZE     (11*4)
#define SH_ROM_CRC_68K  (12*4)
#define SH_ROM_CRC_ARM  (13*4)
#define SH_FRAME_REQ    (24*4)
#define SH_FRAME_DONE   (25*4)
#define SH_FRAME_READY  (21*4)   /* ARM: frame rendue  */
#define SH_FLIP_SEQ     (22*4)   /* 68k: flip confirme par ACK firmware */
#define SH_P96_ENABLE   (144*4)
#define SH_P96_BASE     (145*4)
/* ==== INPUT / CONTROLS ==== */
#define SH_PAD0         (26*4)     /* bitmask pad joueur 1 (ZP_*) */
#define SH_PAD1_P2      (27*4)     /* pad joueur 2                */
#define SH_INPUT_SEQ    (170*4)
#define SH_EMU_CMD      (171*4)
#define SH_EMU_ARG      (172*4)
/* ==== SAUVEGARDES  ==== */
#define SH_SAVE_CMD     (70*4)
#define SH_SAVE_SEQ     (71*4)
#define SH_SAVE_ACK     (72*4)
#define SH_SAVE_STATUS  (73*4)
#define SH_SAVE_SLOT    (74*4)
#define SH_SAVE_SIZE    (75*4)
#define SH_SAVE_ERR     (77*4)
#define SAVE_BUF_ARM    0x07E00000UL       /* buffer DDR ; 68k = fb + (ARM - 0x00200000) */
#define SAVE_CMD_SRAM_SAVE   1
#define SAVE_CMD_SRAM_LOAD   2
#define SAVE_CMD_STATE_SAVE  3
#define SAVE_CMD_STATE_LOAD  4
#define SAVE_STATUS_OK  2
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
#define EMU_CMD_NONE  0
#define EMU_CMD_MENU  1
#define EMU_CMD_QUIT  2
#define EMU_CMD_PAUSE 3
#define EMU_CMD_RESET 4
/* rawcodes clavier Amiga (a VERIFIER via inputlog - defauts standards) */
#define RK_UP     0x4C
#define RK_DOWN   0x4D
#define RK_LEFT   0x4F
#define RK_RIGHT  0x4E
#define RK_NUM1   0x1D
#define RK_NUM2   0x1E
#define RK_NUM3   0x1F
#define RK_NUM4   0x2D   /* A1diag: 6-button X/Y/Z/MODE */
#define RK_NUM5   0x2E
#define RK_NUM6   0x2F
#define RK_NUM0   0x0F
#define RK_RET    0x44
#define RK_ENT    0x43
#define RK_ESC    0x45
#define RK_DEL    0x46   /* 32X: console reset */
#define RK_P      0x19
#define RK_F1     0x50
#define RK_F2     0x51
#define RK_F3     0x52
#define RK_F4     0x53
#define RK_F5     0x54
static UWORD *g_blankspr=NULL;   /* sprite pointeur invisible */
static UBYTE key_down[128];
/* registres hardware joystick (lecture directe : marche pour les pads que lowlevel ignore) */
#define HW_JOY0DAT (*(volatile UWORD*)0xDFF00A)
#define HW_JOY1DAT (*(volatile UWORD*)0xDFF00C)
#define HW_CIAAPRA (*(volatile UBYTE*)0xBFE001)   /* feu : bit6=port0 bit7=port1 (actif bas) */
#define HW_POTGO   (*(volatile UWORD*)0xDFF034)    /* config lignes POT (2e/3e bouton) */
#define HW_POTINP  (*(volatile UWORD*)0xDFF016)    /* lecture : bit10=P0 btn2, bit14=P1 btn2 (actif bas) */
static ULONG g_padmask=0, g_padmask_prev=0xFFFFFFFF, g_input_seq=0;
static ULONG g_emu_cmd=EMU_CMD_NONE;
static int   g_input_log=0, g_input_mode=0;   /* 0=kbd, 1=cd32/db9 */
static ULONG g_joyport=1;                      /* port ReadJoyPort (1=port jeu par defaut) */
static int   g_players=1, g_input_mode2=1;     /* joueur 2 */
static ULONG g_joyport2=0;                     /* joueur 2 : autre port par defaut */
static ULONG g_padmask2=0, g_padmask2_prev=0xFFFFFFFF;
#define SH_P96_PITCH    (146*4)
#define SH_PERF_PICOFRAME (147*4)
#define SH_PERF_CONVERT   (148*4)
#define SH_PERF_DCLEAN    (149*4)
#define SH_PERF_WCYC_P96  (150*4)
#define SH_PERF_WCYC_FB3B (151*4)
#define SH_PERF_DESC_P96  (152*4)
#define SH_PERF_DESC_FB3B (153*4)
#define SH_PERF_DESC_P96PRE (154*4)
#define SH_PERF_COPY      (155*4)
/* NTSC_TIMING_DIAG : slots accumulateurs cote blob */
#define SH_DIAG_PF_MIN    (156*4)
#define SH_DIAG_PF_MAX    (157*4)
#define SH_DIAG_PF_SUM_US (158*4)
#define SH_DIAG_FRAMES    (160*4)
#define SH_DIAG_N_GT12    (161*4)
#define SH_DIAG_N_GT14    (162*4)
#define SH_DIAG_N_GT1667  (163*4)
#define SH_DIAG_CONV_MAX  (164*4)
#define SH_DIAG_CLEAN_MAX (165*4)
#define SH_DIAG_CONV_MIN  (166*4)
#define SH_DIAG_CONV_SUM_US (167*4)
/* VISUAL_TIMING_CLEAN */
#define SH_REGION_IN      (64*4)   /* 68k->blob : 0=AUTO,4=USA,1=JAPN,8=EUR,2=JAPP */
#define SH_PAL_OUT        (65*4)   /* blob->68k : 0=NTSC,1=PAL */
#define SH_ACTIVE_H       (66*4)   /* blob->68k : 224/240 */
#define SH_Y_OFFSET       (67*4)   /* blob->68k : 0/8 */
#define SH_REGION_EFF     (68*4)   /* blob->68k : regionOverride pose */

/* region choisie (regionOverride PicoDrive) */
#define RGN_AUTO   0UL
#define RGN_JAPN   1UL   /* Japan NTSC */
#define RGN_JAPP   2UL   /* Japan PAL (debug) */
#define RGN_USA    4UL   /* USA NTSC   */
#define RGN_EUR    8UL   /* Europe PAL */

#define ZP_CMD_INIT     1
#define ZP_CMD_LOAD_ROM 2
#define ZP_CMD_START    3
#define ZP_CMD_STOP     4

/* ==== A1diag: 1.1 slots this launcher did not read before ==== */
#define SH_HEARTBEAT      (6*4)    /* blob: ++ at every main-loop iteration        */
#define SH_FRAME_COUNTER  (7*4)    /* blob: ++ at every PicoFrame (drawn or not)   */
#define ST_RUNNING        0x02
#define ST_STOPPING       0x03
#define ST_ERROR          0xEE
#define ST_STOPPED        0xFF
/* ==== A1diag: ZZVirtuaRacingRIX A1 fields (zzvr_shared.h, slots 180-199) ==== */
#define SH_ZVR_MAGIC      (180*4)  /* 'ZVR1' = 0x5A565231 (32X blobs), 'ZPM1' = 0x5A504D31 (MD rc2) */
#define ZVR_MAGIC_VAL     0x5A565231UL
#define ZPM_MAGIC_VAL     0x5A504D31UL
#define SH_PUB_IMMEDIATE  (177*4)  /* rc2 blobs: frames published at the end of PicoFrame      */
#define SH_PUB_DEFERRED   (178*4)  /* rc2 blobs: frames published from the pacing wait         */
#define SH_PUB_DROPPED    (179*4)  /* rc2 blobs: frames overwritten before publication         */
#define SH_ZVR_BUILD      (181*4)  /* 0x0A1A interpreter, 0x0A1B DRC                 */
#define SH_ZVR_AHW        (182*4)  /* PicoIn.AHW (bit 2 = 32X)                       */
#define SH_ZVR_X32_REG0   (183*4)  /* 32X sysreg 0: ADEN bit0, nRES bit1, FM bit15   */
#define SH_ZVR_MSH2_PC    (184*4)
#define SH_ZVR_SSH2_PC    (185*4)
#define SH_ZVR_MMAP_USED  (186*4)
#define SH_ZVR_SBRK_USED  (187*4)
#define SH_ZVR_PF_W_AVG   (188*4)  /* PicoFrame average over the last 600 frames, us */
#define SH_ZVR_PF_W_MAX   (189*4)
#define SH_ZVR_PF_LAST    (190*4)
#define SH_ZVR_VFP        (191*4)
#define SH_ZVR_DRC        (192*4)
#define SH_ZVR_MMAP_FAIL  (193*4)
#define SH_ZVR_SH2_STATE  (173*4)  /* RC5 blob (sub>=6): msh2.state | ssh2.state<<8 | m lvl<<16 | s lvl<<24 */
#define SH_ZVR_MSH2_POLL  (174*4)  /* RC5 blob: msh2.poll_addr */
#define SH_ZVR_SSH2_POLL  (175*4)  /* RC5 blob: ssh2.poll_addr */
#define SH_ZVR_SH2_SR     (159*4)  /* RC5 blob: msh2.sr | ssh2.sr<<16 (SR & 0x3f3) */
#define SH_ZVR_X32_VDP0   (194*4)
#define SH_ZVR_OSD        (195*4)  /* 68k -> blob: 1 = OSD off                       */
#define SH_ZVR_W_FRAMES   (196*4)
#define SH_ZVR_W_DRAWN    (197*4)
#define SH_ZVR_ENTRY_SP   (198*4)
#define SH_ZVR_BUILD2     (199*4)
static int g_osd=0;                /* rc2: flag osd (blob text line; off by default) */
static int g_no50=0;               /* rc2: flag no50 (no 50 Hz output for PAL games) */
static int g_session_pal=-1;       /* rc2n: emulated region of this session (0 NTSC, 1 PAL; -1 unknown) */
static int g_session_reg=0;        /* rc2n: regionOverride of this session */
#define ST_TAG_LEN   9             /* rc2n: chunk id 200, len 4 (LE), 'Z','P', pal, region */
#define ST_TAG_CHUNK 200
static int g_no60=0;               /* rc2c: flag no60 (keep the 59.52 Hz preset for NTSC games) */
static int g_pal50_active=0;       /* rc2: custom timing programmed (cleared when restored) */
static int g_pal50_used=0;         /* rc2b: it was used in this session (for the summary) */
static const char *g_ctm_label="60 Hz preset";   /* rc2c: output timing label for the summary */
static int g_pri0=0;               /* rc2c: flag pri0 (do not raise the main task priority) */
static int g_norecheck=0;          /* rc2c: flag norecheck (no FRAME_READY re-check inside the period) */
static ULONG g_take_recheck=0;     /* rc2c: frames taken by the re-check (would have been a lost slot) */
/* rc2g: drop-event table (first 32 increments of the blob's dropped counter) */
#define DROP_EV_MAX 32
struct drop_ev { ULONG iter, ms, dropped, ready, taken, late, inpulse, byack, unc_rej, recheck, pa_ur, pa_rs, emu, latmax; int inflight, path; };
static int   g_nosettle=0;         /* rc2h: flag nosettle (publish from the first iteration, as rc2g) */
static ULONG g_settle_iters=0;     /* rc2h: iterations spent before P96_BASE was written */
static ULONG g_drop_at_quit=0xFFFFFFFFUL, g_emu_at_quit=0;   /* rc2i: counters frozen at the quit request */
#define FREEZE_AT_QUIT() do{ if(g_drop_at_quit==0xFFFFFFFFUL){ g_drop_at_quit=diag_blob(shared)?rd32(shared,SH_PUB_DROPPED):0UL; \
                             g_emu_at_quit=rd32(shared,SH_FRAME_COUNTER); } }while(0)
static struct drop_ev g_drop_ev[DROP_EV_MAX];
static ULONG g_drop_ev_n=0, g_drop_first2s=0, g_drop_at_start=0, g_emu_at_start=0;

static const char *g_exit_reason="unknown";   /* rc2: printed at the end */

static int g_blob_explicit=0;      /* v1.2 RC4: blob file given on the command line */
static int g_force_tof=0;          /* 32X: flag tof  -> 1.1 WaitTOF pacing (A/B) */
static int g_gate=0;               /* 32X: flag gate -> blob slot 176 = 1, 1.1 drawing gate (A/B) */
#define SH_ZVR_DRAWMODE   (176*4)  /* 68k -> blob: 0 = draw every frame, 1 = 1.1 gate */
/* 32X: one-shot emulator command (reset) with a consume handshake: written when
   the slot reads NONE, considered done when the blob has cleared it again */
static ULONG g_emu_pending=0; static int g_emu_sent=0;

/* A1diag: main-loop bookkeeping printed by the snapshots / watchdog */
static ULONG g_wd_iter=0;          /* loop iterations (one per WaitTOF)              */
static ULONG g_ack_max_ms=0;       /* longest arm -> PAN ack wait seen (ms)          */
static ULONG g_ack_timeouts=0;     /* PAN ack timeouts (>100 ms)                     */
static ULONG g_flips=0;            /* frames taken (FLIP_SEQ written)                */
static int   g_lp_inflight=0, g_lp_front=0, g_lp_pending=-1, g_lp_render=1;
static ULONG g_lp_last_taken=0;

#define IMG_W           320
#define IMG_H           240

/* ARM = 0x00200000 + (cpu_addr - fb)  (adresse ARM physique du buffer) */
#define CPU_TO_ARM(cpu,fb) (0x00200000UL + ((ULONG)(cpu) - (ULONG)(fb)))

/* ---- helpers bus (byteswap ARM<->68k pour les champs partages) ---- */
static ULONG rd32(volatile UBYTE *b, ULONG o)
{
    volatile UWORD *p=(volatile UWORD*)(b+o);
    UWORD w0=p[0],w1=p[1];
    return ((ULONG)((UWORD)((w1>>8)|(w1<<8)))<<16)|
            (ULONG)((UWORD)((w0>>8)|(w0<<8)));
}
static void wr32(volatile UBYTE *b, ULONG o, ULONG v)
{
    volatile UWORD *p=(volatile UWORD*)(b+o);
    ULONG s=((v&0xFFUL)<<24)|((v&0xFF00UL)<<8)|((v&0xFF0000UL)>>8)|((v&0xFF000000UL)>>24);
    p[0]=(UWORD)(s>>16); p[1]=(UWORD)(s&0xFFFF);
}
/* ---- lecture d'une manette (joueur 1 ou 2) : lowlevel/hardware + clavier ---- */
/* bits Competition Pro (layout non standard via ReadJoyPort) */
#define CP_UP     0x00000008UL
#define CP_DOWN   0x00000004UL
#define CP_LEFT   0x00000002UL
#define CP_RIGHT  0x00000001UL
#define CP_BLUE   0x00800000UL
#define CP_RED    0x00400000UL
#define CP_GREEN  0x00100000UL
#define CP_YELLOW 0x00200000UL
#define CP_START  0x00020000UL
#define CP_REVERSE 0x00040000UL   /* left shoulder  (JPB_BUTTON_REVERSE, 18) */
#define CP_FORWARD 0x00080000UL   /* right shoulder (JPB_BUTTON_FORWARD, 19) */
/* touches clavier joueur 2 (WASD + FGH + Tab) */
#define RK_W 0x11
#define RK_A 0x20
#define RK_S 0x21
#define RK_D 0x22
#define RK_F 0x23
#define RK_G 0x24
#define RK_H 0x25
#define RK_TAB 0x42
static ULONG read_pad(int mode, ULONG port, int keyset)
{
    ULONG pm=0;
    if(mode==1 || mode==3){
        int ll_ok=0;
        if(mode==1 && LowLevelBase){
            ULONG jp=ReadJoyPort(port);
            if((jp&JP_TYPE_MASK)!=JP_TYPE_NOTAVAIL){
                ll_ok=1;
                if(jp&CP_UP)    pm|=ZP_UP;
                if(jp&CP_DOWN)  pm|=ZP_DOWN;
                if(jp&CP_LEFT)  pm|=ZP_LEFT;
                if(jp&CP_RIGHT) pm|=ZP_RIGHT;
                if(jp&CP_BLUE)  pm|=ZP_A;
                if(jp&CP_RED)   pm|=ZP_B;
                if(jp&CP_GREEN) pm|=ZP_C;
                /* v1.2.1: the 6-button functions on a CD32 / USB (Poseidon)
                   pad: shoulders = X / Y, yellow = Z (Virtua Racing Deluxe: the
                   views; 1.2 had yellow = Mode and no X / Y / Z on the pad).
                   Mode stays on keypad 0. */
                if(jp&CP_REVERSE)pm|=ZP_X;
                if(jp&CP_FORWARD)pm|=ZP_Y;
                if(jp&CP_YELLOW)pm|=ZP_Z;
                if(jp&CP_START) pm|=ZP_START;
            }
        }
        if(!ll_ok){   /* hardware direct (pads que lowlevel ignore) */
            UWORD jd=(port==0)?HW_JOY0DAT:HW_JOY1DAT;
            UBYTE pra=HW_CIAAPRA;
            int right=(jd&0x0002)?1:0, left=(jd&0x0200)?1:0;
            int down=((jd^(jd>>1))&0x0001)?1:0, up=((jd^(jd>>1))&0x0100)?1:0;
            int fire=(port==0)?((pra&0x40)?0:1):((pra&0x80)?0:1);
            if(up)pm|=ZP_UP; if(down)pm|=ZP_DOWN; if(left)pm|=ZP_LEFT; if(right)pm|=ZP_RIGHT;
            if(fire)pm|=ZP_A;
        }
        /* Start clavier de secours (Entree pour P1, Tab pour P2) */
        if(keyset==0){ if(key_down[RK_RET]||key_down[RK_ENT]) pm|=ZP_START;
                       /* A1diag: 6-button extras from the keyboard even with a pad */
                       if(key_down[RK_NUM4]) pm|=ZP_X;
                       if(key_down[RK_NUM5]) pm|=ZP_Y;
                       if(key_down[RK_NUM6]) pm|=ZP_Z;
                       if(key_down[RK_NUM0]) pm|=ZP_MODE; }
        else         { if(key_down[RK_TAB]) pm|=ZP_START; }
    } else if(keyset==0){   /* mode 0 : clavier complet, joueur 1 uniquement */
        if(key_down[RK_UP])    pm|=ZP_UP;
        if(key_down[RK_DOWN])  pm|=ZP_DOWN;
        if(key_down[RK_LEFT])  pm|=ZP_LEFT;
        if(key_down[RK_RIGHT]) pm|=ZP_RIGHT;
        if(key_down[RK_NUM1])  pm|=ZP_A;
        if(key_down[RK_NUM2])  pm|=ZP_B;
        if(key_down[RK_NUM3])  pm|=ZP_C;
        if(key_down[RK_RET]||key_down[RK_ENT]) pm|=ZP_START;
        /* A1diag: 6-button pad (32X games use X/Y/Z/MODE): keypad 4/5/6/0 */
        if(key_down[RK_NUM4])  pm|=ZP_X;
        if(key_down[RK_NUM5])  pm|=ZP_Y;
        if(key_down[RK_NUM6])  pm|=ZP_Z;
        if(key_down[RK_NUM0])  pm|=ZP_MODE;
    }
    /* joueur 2 en mode clavier n'existe pas : il doit avoir une manette (Tab = Start secours) */
    return pm;
}

/* ---- SAUVEGARDES SRAM (68k) : fichier ROMNAME.srm <-> buffer DDR <-> Core1 ---- */
#define SAVE_MAX_68K  (1536UL*1024UL)   /* 32X: a 32X savestate is ~680 KiB; zone 0x07E00000..0x08000000 */
static void make_srm_name(const char *rom, char *out)
{
    int i, dot=-1, len=0;
    for(i=0; rom[i] && i<250; i++){ out[i]=rom[i]; if(rom[i]=='.') dot=i; len=i+1; }
    if(dot>=0) len=dot;
    out[len]='.'; out[len+1]='s'; out[len+2]='r'; out[len+3]='m'; out[len+4]=0;
}
/* rc2l: wait for the blob's save ack.  The blob serves the command between two
   PicoFrames and starts a PicoFrame only once the audio ring has been drained:
   with Paula the drain is pa_service() in this task, so it must go on while we
   wait (1.1 polled 8 M times without servicing Paula: 5.3 s freeze, looping
   sound, failed save).  Returns the time taken in ms, or -1 on timeout. */
static LONG save_wait_ack(volatile UBYTE *shared, ULONG seq, ULONG ms_max)
{
    ULONG f=0, t0=eclock_now(&f), now, n=0;
    for(;;){
        if(rd32(shared,SH_SAVE_ACK)==seq) return f ? (LONG)((eclock_now(NULL)-t0)/(f/1000UL)) : 0L;
        if(g_pa_active) pa_service();            /* Paula: drain the ring (this task) */
        /* rc2m: SLEEP, do not spin: this task runs at priority 6 during the game,
           above the AHI task (5) which must run to drain the ring */
        if(g_sreq) sleep_us(1000UL); else Delay(1);
        if(f){ now=eclock_now(NULL); if((now-t0) > (f/1000UL)*ms_max) return -1L; }
        else if(++n > 3000UL) return -1L;
    }
}
static void sram_load(volatile UBYTE *shared, volatile UBYTE *fb, const char *romname)
{
    char srm[256]; BPTR fh; LONG n; ULONG seq, to;
    volatile UBYTE *ddr=(volatile UBYTE*)((ULONG)fb + (SAVE_BUF_ARM - 0x00200000UL));
    make_srm_name(romname, srm);
    fh=Open((STRPTR)srm, MODE_OLDFILE);
    if(!fh) return;                                  /* pas de sauvegarde : normal */
    n=Read(fh, (APTR)ddr, (LONG)SAVE_MAX_68K);
    Close(fh);
    if(n<=0) return;
    CacheClearU();                                   /* committer l'ecriture DDR */
    wr32(shared, SH_SAVE_SIZE, (ULONG)n);
    wr32(shared, SH_SAVE_CMD, SAVE_CMD_SRAM_LOAD);
    seq=rd32(shared, SH_SAVE_SEQ)+1; wr32(shared, SH_SAVE_SEQ, seq);
    to=(ULONG)save_wait_ack(shared, seq, 3000UL);
    printf("SRAM: charge %ld octets (%s)%s\n", n, srm, ((LONG)to<0)?" - TIMEOUT":"");
}
static void sram_save(volatile UBYTE *shared, volatile UBYTE *fb, const char *romname)
{
    char srm[256]; BPTR fh; ULONG seq, to, sz, st;
    volatile UBYTE *ddr=(volatile UBYTE*)((ULONG)fb + (SAVE_BUF_ARM - 0x00200000UL));
    wr32(shared, SH_SAVE_CMD, SAVE_CMD_SRAM_SAVE);
    seq=rd32(shared, SH_SAVE_SEQ)+1; wr32(shared, SH_SAVE_SEQ, seq);
    to=(ULONG)save_wait_ack(shared, seq, 3000UL);
    if((LONG)to<0){ printf("SRAM: save TIMEOUT\n"); return; }
    st=rd32(shared, SH_SAVE_STATUS); sz=rd32(shared, SH_SAVE_SIZE);
    if(st!=SAVE_STATUS_OK || sz==0 || sz>SAVE_MAX_68K) return;   /* jeu sans SRAM */
    CacheClearU();                                   /* voir l'ecriture ARM du DDR */
    make_srm_name(romname, srm);
    fh=Open((STRPTR)srm, MODE_NEWFILE);
    if(!fh){ printf("SRAM: ecriture %s impossible\n", srm); return; }
    Write(fh, (APTR)ddr, (LONG)sz);
    Close(fh);
    printf("SRAM: sauve %lu octets -> %s\n", sz, srm);
}
/* nom ROMNAME.stN (slot 0..9) */
static void make_st_name(const char *rom, int slot, char *out)
{
    int i, dot=-1, len=0;
    for(i=0; rom[i] && i<248; i++){ out[i]=rom[i]; if(rom[i]=='.') dot=i; len=i+1; }
    if(dot>=0) len=dot;
    out[len]='.'; out[len+1]='s'; out[len+2]='t'; out[len+3]=(char)('0'+(slot%10)); out[len+4]=0;
}
/* SAVESTATE : Core1 serialise -> DDR -> ecrit ROMNAME.stN */
static void state_save(volatile UBYTE *shared, volatile UBYTE *fb, const char *romname, int slot)
{
    char st[256]; BPTR fh; ULONG seq, to, sz, status;
    volatile UBYTE *ddr=(volatile UBYTE*)((ULONG)fb + (SAVE_BUF_ARM - 0x00200000UL));
    wr32(shared, SH_SAVE_SLOT, (ULONG)slot);
    wr32(shared, SH_SAVE_CMD, SAVE_CMD_STATE_SAVE);
    seq=rd32(shared, SH_SAVE_SEQ)+1; wr32(shared, SH_SAVE_SEQ, seq);
    { LONG ms=save_wait_ack(shared, seq, 3000UL);
      if(ms<0){ printf("STATE: save slot %d TIMEOUT (no answer from the blob in 3 s)\n", slot); return; }
      to=(ULONG)ms; }
    status=rd32(shared, SH_SAVE_STATUS); sz=rd32(shared, SH_SAVE_SIZE);
    if(status!=SAVE_STATUS_OK || sz==0 || sz>SAVE_MAX_68K){
        printf("STATE: save slot %d echoue (status=%lu err=%lu size=%lu)\n", slot, status, rd32(shared,SH_SAVE_ERR), sz); return; }
    CacheClearU();
    make_st_name(romname, slot, st);
    fh=Open((STRPTR)st, MODE_NEWFILE);
    if(!fh){ printf("STATE: ecriture %s impossible\n", st); return; }
    Write(fh, (APTR)ddr, (LONG)sz);
    { UBYTE tag[ST_TAG_LEN];   /* rc2n: region tag after PicoDrive's end marker */
      tag[0]=ST_TAG_CHUNK; tag[1]=4; tag[2]=0; tag[3]=0; tag[4]=0;
      tag[5]='Z'; tag[6]='P'; tag[7]=(UBYTE)(g_session_pal==1?1:0); tag[8]=(UBYTE)g_session_reg;
      Write(fh, (APTR)tag, (LONG)ST_TAG_LEN); }
    Close(fh);
    printf("STATE: sauve slot %d (%lu octets, %s, blob %lu ms) -> %s\n", slot, sz, g_session_pal==1?"PAL":"NTSC", to, st);
}
/* SAVESTATE : lit ROMNAME.stN -> DDR -> Core1 deserialise */
static void state_load(volatile UBYTE *shared, volatile UBYTE *fb, const char *romname, int slot)
{
    char st[256]; BPTR fh; LONG n; ULONG seq, to, status;
    volatile UBYTE *ddr=(volatile UBYTE*)((ULONG)fb + (SAVE_BUF_ARM - 0x00200000UL));
    make_st_name(romname, slot, st);
    fh=Open((STRPTR)st, MODE_OLDFILE);
    if(!fh){ printf("STATE: pas de save slot %d (%s)\n", slot, st); return; }
    n=Read(fh, (APTR)ddr, (LONG)SAVE_MAX_68K);
    Close(fh);
    if(n<=0) return;
    CacheClearU();
    /* rc2n: region tag (absent in 1.1 files) */
    if(n > ST_TAG_LEN){
        volatile UBYTE *t=ddr+(n-ST_TAG_LEN);
        if(t[0]==ST_TAG_CHUNK && t[1]==4 && t[2]==0 && t[3]==0 && t[4]==0 && t[5]=='Z' && t[6]=='P'){
            int spal=t[7]?1:0;
            if(g_session_pal>=0 && spal!=g_session_pal){
                printf("STATE: slot %d was saved in %s, this session is %s: NOT loaded (start the game with %s)\n",
                       slot, spal?"PAL":"NTSC", g_session_pal?"PAL":"NTSC", spal?"eur":"usa/jap");
                return;
            }
            n-=ST_TAG_LEN;
        } else printf("STATE: slot %d has no region tag (older file): loaded as is - use the same region it was saved in\n", slot);
    }
    wr32(shared, SH_SAVE_SIZE, (ULONG)n);
    wr32(shared, SH_SAVE_SLOT, (ULONG)slot);
    wr32(shared, SH_SAVE_CMD, SAVE_CMD_STATE_LOAD);
    seq=rd32(shared, SH_SAVE_SEQ)+1; wr32(shared, SH_SAVE_SEQ, seq);
    { LONG ms=save_wait_ack(shared, seq, 3000UL);
      if(ms<0){ printf("STATE: load slot %d TIMEOUT (no answer from the blob in 3 s)\n", slot); return; }
      to=(ULONG)ms; }
    status=rd32(shared, SH_SAVE_STATUS);
    printf("STATE: charge slot %d (%ld octets) status=%lu err=%lu (blob %lu ms)\n", slot, n, status, rd32(shared,SH_SAVE_ERR), to);
}
static void copy_uword(volatile UBYTE *d, const UBYTE *s, LONG sz)
{
    volatile UWORD *dw=(volatile UWORD*)d; const UWORD *sw=(const UWORD*)s;
    LONG i; for(i=0;i<(sz+1)/2;i++) dw[i]=sw[i];
}
static ULONG g_crctab[256]; static int g_crctab_done=0;
static void crc32_init(void){ ULONG i,j,c;
    for(i=0;i<256;i++){ c=i; for(j=0;j<8;j++) c=(c&1)?(0xEDB88320UL^(c>>1)):(c>>1); g_crctab[i]=c; }
    g_crctab_done=1; }
static ULONG crc32_buf(const UBYTE *p, ULONG n){ ULONG c=0xFFFFFFFFUL,i;
    if(!g_crctab_done) crc32_init();
    for(i=0;i<n;i++) c=g_crctab[(c^p[i])&0xFF]^(c>>8); return c^0xFFFFFFFFUL; }

static int send_cmd(volatile UBYTE *shared, ULONG cmd, LONG to)
{
    ULONG seq=rd32(shared,SH_CMD_SEQ)+1;
    wr32(shared,SH_COMMAND,cmd); wr32(shared,SH_CMD_SEQ,seq);
    while(to-->0){ if(rd32(shared,SH_ACK_SEQ)==seq) return 1; Delay(1); }
    return 0;
}
/* pilote une frame ; le blob rend dans le backbuffer P96 publie au prealable.
   Busy-poll de FRAME_DONE sur le bus Zorro (PAS de Delay(1)=20ms qui plafonnait
   la cadence) : le 68k detecte la fin de frame en microsecondes. ERROR verifie
   periodiquement pour bailler vite sur un fault. */
static int drive_one_frame(volatile UBYTE *shared)
{
    ULONG req=rd32(shared,SH_FRAME_REQ)+1;
    ULONG spins=4000000UL;
    wr32(shared,SH_FRAME_REQ,req);
    while(spins--){
        if(rd32(shared,SH_FRAME_DONE)==req) return 1;
        if((spins & 0xFFFF)==0 && rd32(shared,SH_ERROR)!=0) return 0;
    }
    return (rd32(shared,SH_FRAME_DONE)==req)?1:0;
}

/* argv[3] optionnel -> regionOverride PicoDrive. Defaut AUTO. */
static ULONG parse_region(const char *s, const char **label)
{
    if(!s){ *label="AUTO"; return RGN_AUTO; }
    if(!strcmp(s,"auto")||!strcmp(s,"AUTO")){ *label="AUTO"; return RGN_AUTO; }
    if(!strcmp(s,"usa") ||!strcmp(s,"USA") ||!strcmp(s,"ntsc")||!strcmp(s,"NTSC")){ *label="USA_NTSC"; return RGN_USA; }
    if(!strcmp(s,"jap") ||!strcmp(s,"JAP") ||!strcmp(s,"japan")||!strcmp(s,"JAPAN")){ *label="JAPAN_NTSC"; return RGN_JAPN; }
    if(!strcmp(s,"eur") ||!strcmp(s,"EUR") ||!strcmp(s,"pal") ||!strcmp(s,"PAL")||!strcmp(s,"europe")){ *label="EUROPE_PAL"; return RGN_EUR; }
    if(!strcmp(s,"jappal")||!strcmp(s,"JAPPAL")){ *label="JAPAN_PAL(dbg)"; return RGN_JAPP; }
    *label="AUTO(unknown-arg)"; return RGN_AUTO;
}

/* ================= CONFIG UNIQUE (CLI et GUI remplissent la meme struct) ================= */
typedef enum { ZP_REGION_AUTO=0, ZP_REGION_EUR_PAL, ZP_REGION_USA_NTSC, ZP_REGION_JAPAN_NTSC } ZP_Region;
typedef enum { ZP_AUDIO_AHI=0, ZP_AUDIO_PAULA, ZP_AUDIO_NONE } ZP_Audio;
typedef enum { ZP_INPUT_KBD=0, ZP_INPUT_CD32_LOWLEVEL, ZP_INPUT_HW_DB9, ZP_INPUT_USB_HID } ZP_Input;
typedef struct {
    char rom_path[256];
    char blob_path[256];
    int  region;     /* ZP_Region */
    int  audio;      /* ZP_Audio  */
    int  input;      /* ZP_Input joueur 1 */
    int  port;       /* port joueur 1 (0 ou 1) */
    int  players;    /* 1 ou 2 */
    int  input2;     /* ZP_Input joueur 2 */
    int  port2;      /* port joueur 2 (0 ou 1) */
    int  show_fps;
    char rom_dir[256];   /* v1.2 update: default ROM drawer, chosen explicitly ("" = none) */
    char last_dir[256];  /* v1.2 update: drawer of the last ROM chosen in the requester    */
} ZP_Config;

/* mappe ZP_Region -> regionOverride PicoDrive (memes valeurs que parse_region) */
static ULONG region_from_cfg(int r, const char **label)
{
    switch(r){
        case ZP_REGION_EUR_PAL:    return parse_region("eur", label);
        case ZP_REGION_USA_NTSC:   return parse_region("usa", label);
        case ZP_REGION_JAPAN_NTSC: return parse_region("jap", label);
        default:                   return parse_region(NULL,  label);   /* AUTO */
    }
}

/* ===================== A1diag helpers ===================== */
/* 68k stack of this task: SP now, bounds from the Task structure (valid after
   the libnix __stack swap). */
static void stack_report(const char *tag)
{
    struct Task *t=FindTask(NULL); ULONG sp;
    __asm__ volatile("move.l %%sp,%0" : "=r"(sp));
    printf("STACK [%s]: sp=0x%08lx lower=0x%08lx upper=0x%08lx used=%lu free=%lu\n",
           tag, sp, (ULONG)t->tc_SPLower, (ULONG)t->tc_SPUpper,
           (ULONG)t->tc_SPUpper - sp, sp - (ULONG)t->tc_SPLower);
}
static void exit_step(const char *what)
{
    printf("EXIT %s\n", what);
}
static int zvr_blob(volatile UBYTE *shared)   /* 32X blob (ZZVirtuaRacingRIX / ZZPicoDrive32X) */
{
    return (shared && rd32(shared,SH_ZVR_MAGIC)==ZVR_MAGIC_VAL) ? 1 : 0;
}
static int zpm_blob(volatile UBYTE *shared)   /* rc2: MD rc2 blob */
{
    return (shared && rd32(shared,SH_ZVR_MAGIC)==ZPM_MAGIC_VAL) ? 1 : 0;
}
static int diag_blob(volatile UBYTE *shared)  /* either: slots 177-179, 188-190, 195-199 are valid */
{
    return zvr_blob(shared) || zpm_blob(shared);
}
/* One snapshot of the blob + launcher state.  Reading:
   - frames/hb advancing between A and B : the blob runs (PicoFrame + main loop);
   - ready == flip : the blob has nothing pending, it draws when it wants;
   - ready == flip+1 for a long time : a frame was published and never taken (68k side);
   - msh2/ssh2 identical in A and B while frames advance : SH-2s stuck (poll/sleep or
     recompiler), the MD side (music) keeps running - the "frozen image, sound on" case. */
static void diag_snapshot(volatile UBYTE *shared, volatile UBYTE *board, const char *tag)
{
    if(!shared) return;
    printf("SNAP %s: status=0x%02lx err=%lu cmd/ack=%lu/%lu hb=%lu frames=%lu ready=%lu flip=%lu pcm w/r=%lu/%lu\n",
           tag, rd32(shared,SH_STATUS), rd32(shared,SH_ERROR),
           rd32(shared,SH_CMD_SEQ), rd32(shared,SH_ACK_SEQ),
           rd32(shared,SH_HEARTBEAT), rd32(shared,SH_FRAME_COUNTER),
           rd32(shared,SH_FRAME_READY), rd32(shared,SH_FLIP_SEQ),
           rd32(shared,SH_PCM_WRITE_POS), rd32(shared,SH_PCM_READ_POS));
    printf("     68k: iter=%lu flips=%lu inflight=%d front/pending/render=%d/%d/%d last_taken=%lu panack=%lu ackmax=%lums acktimeouts=%lu vbl=%lu\n",
           g_wd_iter, g_flips, g_lp_inflight, g_lp_front, g_lp_pending, g_lp_render,
           g_lp_last_taken, board?zz_pan_ack(board):0UL, g_ack_max_ms, g_ack_timeouts,
           board?(ZZ_VBL(board)?1UL:0UL):0UL);
    if(diag_blob(shared))
        printf("     blob: pf last/avg/max=%lu/%lu/%lu us win=%lu/%lu  published imm/def/dropped=%lu/%lu/%lu\n",
               rd32(shared,SH_ZVR_PF_LAST), rd32(shared,SH_ZVR_PF_W_AVG), rd32(shared,SH_ZVR_PF_W_MAX),
               rd32(shared,SH_ZVR_W_FRAMES), rd32(shared,SH_ZVR_W_DRAWN),
               rd32(shared,SH_PUB_IMMEDIATE), rd32(shared,SH_PUB_DEFERRED), rd32(shared,SH_PUB_DROPPED));
    if(zvr_blob(shared))
        printf("     32X: msh2=0x%08lx ssh2=0x%08lx reg0=0x%04lx vdp0=0x%04lx mmapfail=%lu\n",
               rd32(shared,SH_ZVR_MSH2_PC), rd32(shared,SH_ZVR_SSH2_PC),
               rd32(shared,SH_ZVR_X32_REG0)&0xFFFF, rd32(shared,SH_ZVR_X32_VDP0)&0xFFFF,
               rd32(shared,SH_ZVR_MMAP_FAIL));
    if(zvr_blob(shared) && rd32(shared,SH_ZVR_BUILD2)>=6UL){
        /* RC5: what each SH-2 waits for (state bits: 1 run, 2 sleep, 4 comm poll,
           8 VDP poll, 0x10 SDRAM poll, 0x80 in recompiled code; SR bits 4-7 =
           interrupt mask; lvl = pending interrupt level) */
        ULONG st=rd32(shared,SH_ZVR_SH2_STATE), sr=rd32(shared,SH_ZVR_SH2_SR);
        printf("     SH-2: master state=0x%02lx poll=0x%08lx sr=0x%03lx lvl=%lu   slave state=0x%02lx poll=0x%08lx sr=0x%03lx lvl=%lu\n",
               st&0xFF, rd32(shared,SH_ZVR_MSH2_POLL), sr&0xFFFF, (st>>16)&0xFF,
               (st>>8)&0xFF, rd32(shared,SH_ZVR_SSH2_POLL), sr>>16, (st>>24)&0xFF);
    }
}
static void diag_a1_report(volatile UBYTE *shared)
{
    if(!diag_blob(shared)){ printf("(older blob: no diagnostic fields)\n"); return; }
    printf("=== BLOB DIAG === %s build=0x%04lx sub=%lu entry_sp=0x%08lx\n",
           zvr_blob(shared)?"32X":"MD v1.2", rd32(shared,SH_ZVR_BUILD), rd32(shared,SH_ZVR_BUILD2),
           rd32(shared,SH_ZVR_ENTRY_SP));
    if(zvr_blob(shared)){
        printf("  32X: drc=%lu vfp=%lu ahw=0x%lx reg0=0x%04lx pools: mmap=%lu KiB (fail=%lu) sbrk=%lu KiB\n",
               rd32(shared,SH_ZVR_DRC), rd32(shared,SH_ZVR_VFP), rd32(shared,SH_ZVR_AHW),
               rd32(shared,SH_ZVR_X32_REG0)&0xFFFF,
               rd32(shared,SH_ZVR_MMAP_USED)>>10, rd32(shared,SH_ZVR_MMAP_FAIL), rd32(shared,SH_ZVR_SBRK_USED)>>10);
        printf("  last msh2=0x%08lx ssh2=0x%08lx\n", rd32(shared,SH_ZVR_MSH2_PC), rd32(shared,SH_ZVR_SSH2_PC));
    }
    printf("  PicoFrame window (last %lu frames, %lu published): avg=%lu us max=%lu us last=%lu us\n",
           rd32(shared,SH_ZVR_W_FRAMES), rd32(shared,SH_ZVR_W_DRAWN),
           rd32(shared,SH_ZVR_PF_W_AVG), rd32(shared,SH_ZVR_PF_W_MAX), rd32(shared,SH_ZVR_PF_LAST));
    printf("  published: immediate=%lu deferred=%lu dropped=%lu\n",
           rd32(shared,SH_PUB_IMMEDIATE), rd32(shared,SH_PUB_DEFERRED), rd32(shared,SH_PUB_DROPPED));
}
/* rc2: on an INIT / LOAD / START failure, what the blob published about it:
   error code (slot 8), and when an ARM exception was taken (STATUS 0xEE) the
   faulting pc / FAR / FSR written by zzpico_vectors.S (slots 26/27/28). */
static void diag_fault_report(volatile UBYTE *shared)
{
    ULONG st=rd32(shared,SH_STATUS);
    printf("  blob status=0x%02lx error=%lu", st, rd32(shared,SH_ERROR));
    if(st==ST_ERROR)
        printf("  ARM exception: pc=0x%08lx far=0x%08lx fsr=0x%08lx (6=data abort 7=prefetch abort 8=undefined)",
               rd32(shared,26*4), rd32(shared,27*4), rd32(shared,28*4));
    printf("\n");
}
/* STOP with ack, then wait for the terminal state (STATUS=0xFF, blob in wfe). */
static void stop_blob(volatile UBYTE *shared, volatile UBYTE *board)
{
    ULONG t0=eclock_now(NULL), f=0, t1; int ok; LONG w;
    eclock_now(&f);
    ok=send_cmd(shared, ZP_CMD_STOP, 250);
    t1=eclock_now(NULL);
    printf("  STOP ack=%s after %lu ms (status=0x%02lx)\n", ok?"yes":"TIMEOUT",
           f?((t1-t0)/(f/1000UL)):0UL, rd32(shared,SH_STATUS));
    for(w=0; w<150; w++){ if(rd32(shared,SH_STATUS)==ST_STOPPED) break; Delay(1); }
    printf("  STATUS=0x%02lx after %ld more ticks (%s)\n", rd32(shared,SH_STATUS), w,
           rd32(shared,SH_STATUS)==ST_STOPPED?"STOPPED, wfe":"NOT STOPPED: blob still running");
    /* rc2: ARM_RUN = 0 = the firmware "zero-address stop" (core2.c arm_app_run):
       CPU1 held in reset, trampoline cleared, Xil_DCacheFlush() = L1 + shared L2
       clean & invalidate, CPU1 released into its idle loop.  Without it the dirty
       L2 lines of this blob's .data were written back over the NEXT blob image
       after the launcher had copied it (INIT exception on an immediate relaunch;
       running another Core1 program in between hid it).  Whatever the blob state
       (STOPPED or ERROR), this is the clean end of a Core1 session. */
    if(board){
        ZZ_WR(board,REG_ARM_RUN_HI,0);
        ZZ_WR(board,REG_ARM_RUN_LO,0);
        Delay(2);
        printf("  ARM_RUN=0 written (Core0 L2 flush, CPU1 parked by the firmware)\n");
    }
}

static int controllers_conflict(const ZP_Config *cfg);   /* defini plus bas, utilise ici */
static int run_picodrive(const ZP_Config *cfg)
{
    struct ConfigDev *cd;
    volatile UBYTE *board=NULL,*fb=NULL,*shared=NULL;
    int blob_stopped=0;   /* v1.1 : le blob a-t-il deja ete arrete (evite double STOP) */
    BPTR fh=0; UBYTE *rombuf=NULL,*blobbuf=NULL; int blob_alloced=0;
    LONG rom_size=0,blob_size=0; ULONG crc_68k=0; int rc=0,i;

    struct Screen *my_scr=NULL; struct Window *win=NULL;
    struct ScreenBuffer *sb[3]={NULL,NULL,NULL};
    struct BitMap *man_bm[3]={NULL,NULL,NULL};   /* backbuffers VRAM manuels (fallback) */
    struct MsgPort *dbuf_port=NULL;
    ULONG buf_arm[3]={0,0,0}, buf_mem[3]={0,0,0}, buf_off[3]={0,0,0}, pitch=0, dbuf_sig=0, modeid, pan_orig=0;
    ULONG g_fps10=0, g_rend10=0, g_flip10=0, g_frames=0, g_el_ms=0;
    /* NTSC_TIMING_DIAG : stats launcher (ticks EClock) */
    ULONG g_rmin=0, g_rmax=0, g_r12=0, g_r14=0, g_r67=0;   /* render r1-r0 */
    ULONG g_fmin=0, g_fmax=0, g_missed=0;                   /* PAN-to-PAN */
    ULONG g_vbmax=0, g_vbsum=0, g_efreq=0;                  /* vblanks/PAN */
    int have_timer=0;
    /* VISUAL_TIMING_CLEAN */
    ULONG region_in; const char *region_label="AUTO";
    ULONG pal_out=0, target_hz=60, active_h=224, y_off=8;
    int uncapped=0; const char *pacing_mode="TIMER"; int vbl_ok=0;
    int g_pace_zz=0; ULONG efreq_main=0;   /* 32X: 1 = PLL on the ZZ9000 vblank, 0 = WaitTOF */

    printf("ZZPicoDrive32X v1.2.1 - Sega 32X on the ZZ9000 Core1\n");
    stack_report("start");
    region_in=region_from_cfg(cfg->region,&region_label);
    switch(cfg->input){
        case ZP_INPUT_KBD:     g_input_mode=0; break;
        case ZP_INPUT_USB_HID: g_input_mode=2; break;
        case ZP_INPUT_HW_DB9:  g_input_mode=3; break;   /* hardware pur (ignore lowlevel) */
        default:               g_input_mode=1; break;   /* CD32_LOWLEVEL / HW_DB9 : lowlevel + repli hardware */
    }
    g_joyport=(ULONG)cfg->port;
    /* joueur 2 */
    g_players = (cfg->players==2)?2:1;
    switch(cfg->input2){
        case ZP_INPUT_KBD:     g_input_mode2=0; break;
        case ZP_INPUT_USB_HID: g_input_mode2=2; break;
        case ZP_INPUT_HW_DB9:  g_input_mode2=3; break;
        default:               g_input_mode2=1; break;
    }
    if(g_players==2 && g_input_mode2==0) g_input_mode2=1;   /* P2 clavier interdit -> manette */
    g_joyport2=(ULONG)cfg->port2;
    if(controllers_conflict(cfg)){
        printf("ATTENTION: J1 et J2 sur le meme port -> J2 desactive. Utilise des ports differents.\n");
        g_players=1;
    }
    if(cfg->audio==ZP_AUDIO_NONE) g_silence=1;

    ExpansionBase=(struct ExpansionBase*)OpenLibrary("expansion.library",0L);
    if(!ExpansionBase){ printf("expansion.library?\n"); return 20; }
    cd=FindConfigDev(NULL,ZZ9000_MANUF,ZZ9000_PROD);
    if(!cd) cd=FindConfigDev(NULL,ZZ9000_MANUF,0x0A);
    CloseLibrary((struct Library*)ExpansionBase); ExpansionBase=NULL;
    if(!cd){ printf("ZZ9000 not found\n"); g_exit_reason="ZZ9000 not found"; return 10; }
    board=(volatile UBYTE*)cd->cd_BoardAddr; fb=board+MNT_FB_BASE; shared=fb+FB_SHARED;
    printf("board=0x%08lX fb=0x%08lX shared=0x%08lX\n",(ULONG)board,(ULONG)fb,(ULONG)shared);

    /* ROM */
    fh=Open((STRPTR)cfg->rom_path,MODE_OLDFILE);
    if(!fh){ printf("Cannot open ROM\n"); g_exit_reason="cannot open the ROM"; rc=10; goto done; }
    Seek(fh,0,OFFSET_END); rom_size=(LONG)Seek(fh,0,OFFSET_END); Seek(fh,0,OFFSET_BEGINNING);
    if(rom_size<=0){ Close(fh); rc=10; goto done; }
    rombuf=(UBYTE*)AllocMem((ULONG)rom_size+ROM_PAD,MEMF_PUBLIC|MEMF_CLEAR);
    if(!rombuf||Read(fh,rombuf,rom_size)!=rom_size){ printf("ROM read fail\n"); g_exit_reason="ROM read failed"; Close(fh); rc=10; goto done; }
    Close(fh); fh=0; printf("ROM: %ld bytes\n",rom_size);
    /* blob : fichier si present (override dev), sinon blob embarque dans l'executable (release) */
    fh=Open((STRPTR)cfg->blob_path,MODE_OLDFILE);
    if(fh){
        Seek(fh,0,OFFSET_END); blob_size=(LONG)Seek(fh,0,OFFSET_END); Seek(fh,0,OFFSET_BEGINNING);
        if(blob_size<=0){ Close(fh); rc=10; goto done; }
        blobbuf=(UBYTE*)AllocMem((ULONG)blob_size,MEMF_PUBLIC);
        if(!blobbuf||Read(fh,blobbuf,blob_size)!=blob_size){ printf("blob read fail\n"); Close(fh); rc=10; goto done; }
        Close(fh); fh=0; blob_alloced=1;
        printf("Blob: %ld bytes (fichier %s)\n",blob_size,cfg->blob_path);
    } else if(g_blob_explicit){
        /* v1.2 RC4: RC3 test - "zz32x_rc1.bin" not found, the embedded blob ran
           instead without a word: the comparison was not the one asked for */
        printf("Blob file %s not found (copy it next to the program or give its path)\n", cfg->blob_path);
        rc=10; goto done;
    } else {
        blobbuf=(UBYTE*)g_blob_data; blob_size=(LONG)g_blob_size; blob_alloced=0;
        printf("Blob: %ld bytes (embarque dans l'executable)\n",blob_size);
        /* A1diag: built with blob_data_stub.c there is no embedded blob */
        if(blob_size<4096){ printf("No usable embedded blob: give the blob file (%s not found)\n", cfg->blob_path); rc=10; goto done; }
    }

    crc_68k=crc32_buf(rombuf,(ULONG)rom_size);
    /* v1.2 RC4: ARM_RUN = 0 BEFORE the ROM and blob images are copied.  This
       launcher writes it at exit, but a previous Core1 program that did not (any
       1.1 / rc1 launcher, a crash) leaves CPU1 running or its dirty lines in the
       shared L2.  Measured (RC3 run after the rc1 launcher): the new blob zeroes
       its BSS with the MMU off (straight to DDR) and then read stale L2 lines of
       the old blob through the cache - wrong statistics (">12ms=73643370"), a
       wrong page table descriptor read back (0 instead of 0x00b10c02), i.e. any
       variable could start with a value of the previous program.  The firmware
       stop holds CPU1 in reset and cleans + invalidates L1 and the shared L2. */
    /* v1.2.1 (from ZZPicoDriveVR v1.2): a GUI start failed with no message and CPU1 was left
       running a program that never answered ("No MAGIC" went straight to the
       exit): the next Core1 program (Julia) then crashed.  Now a start that does
       not answer is reported (blob image read back, Core1 status), CPU1 is
       parked with ARM_RUN = 0, and the load is done once more from scratch. */
    {   int attempt;
        for(attempt=0;;attempt++){
            int ok=0;
            ZZ_WR(board,REG_ARM_RUN_HI,0);
            ZZ_WR(board,REG_ARM_RUN_LO,0);
            Delay(attempt?10:5);
            printf("ARM_RUN=0 before loading (previous Core1 program stopped, caches flushed)%s\n", attempt?" - second attempt":"");
            copy_uword(fb+FB_ROM,rombuf,rom_size);
            { volatile UWORD *pad=(volatile UWORD*)(fb+FB_ROM+(ULONG)rom_size);
              for(i=0;i<(int)(ROM_PAD/2);i++) pad[i]=0; }
            copy_uword(fb+FB_BLOB,blobbuf,blob_size);

            /* A1diag: also clear slots 160..199 (A1 fields incl. the 'ZVR1' magic) so a
               previous run cannot leave a stale identification (1.1 cleared 0..159) */
            { volatile UWORD *p=(volatile UWORD*)shared; for(i=0;i<400;i++) p[i]=0; }
            ZZ_WR(board,REG_ARM_RUN_HI,(UWORD)(ARM_BLOB>>16));
            ZZ_WR(board,REG_ARM_RUN_LO,(UWORD)(ARM_BLOB&0xFFFF));

            { LONG t=250; while(t-->0 && rd32(shared,SH_MAGIC)!=ZZPICO_MAGIC) Delay(1); }
            if(rd32(shared,SH_MAGIC)!=ZZPICO_MAGIC){
                printf("No MAGIC (the Core1 program did not answer)\n");
                g_exit_reason="the Core1 program did not start";
            } else {
                printf("Core1 READY.\n");
                if(send_cmd(shared,ZP_CMD_INIT,250)) ok=1;
                else { printf("INIT timeout\n"); g_exit_reason="INIT timeout"; }
            }
            if(ok) break;
            diag_fault_report(shared);
            {   /* is the program image still the one copied? */
                volatile UWORD *m=(volatile UWORD*)(fb+FB_BLOB); const UWORD *src=(const UWORD*)blobbuf;
                ULONG n=(ULONG)blob_size/2, k, bad=0, first=0;
                for(k=0;k<n;k++) if(m[k]!=src[k]){ if(!bad) first=k*2; bad++; }
                if(bad) printf("  program image in ZZ9000 memory: %lu words differ, first at +0x%lx\n", bad, first);
                else    printf("  program image in ZZ9000 memory: identical to the copy\n");
            }
            ZZ_WR(board,REG_ARM_RUN_HI,0);
            ZZ_WR(board,REG_ARM_RUN_LO,0);
            Delay(5);
            printf("  ARM_RUN=0 written (CPU1 parked)\n");
            if(attempt>=1){ blob_stopped=1; rc=12; goto done; }
            printf("Core1 start failed: loading again\n");
        }
    }
    wr32(shared,SH_ROM_ADDR,ARM_ROM); wr32(shared,SH_ROM_SIZE,(ULONG)rom_size);
    wr32(shared,SH_ROM_CRC_68K,crc_68k);
    if(!send_cmd(shared,ZP_CMD_LOAD_ROM,250)){ printf("LOAD_ROM timeout\n"); diag_fault_report(shared); g_exit_reason="LOAD_ROM timeout"; rc=13; goto stop; }
    if(rd32(shared,SH_ERROR)!=0){ printf("LOAD_ROM err=%lu\n",rd32(shared,SH_ERROR)); g_exit_reason="ROM refused by the Core1 program"; rc=14; goto stop; }
    printf("ROM loaded (CRC_ARM=0x%08lX)\n",rd32(shared,SH_ROM_CRC_ARM));
    /* A1diag: identify the blob, OSD flag */
    if(diag_blob(shared)){
        printf("Blob is %s build=0x%04lx sub=%lu entry_sp=0x%08lx\n",
               zvr_blob(shared)?"a 32X Core1 program":"a Mega Drive Core1 program",
               rd32(shared,SH_ZVR_BUILD), rd32(shared,SH_ZVR_BUILD2), rd32(shared,SH_ZVR_ENTRY_SP));
        wr32(shared,SH_ZVR_OSD, g_osd?0UL:1UL);          /* rc2: OSD off unless flag osd */
        wr32(shared,SH_ZVR_DRAWMODE, g_gate?1UL:0UL);    /* 0 = draw every frame, deferred publication; 1 = 1.1 gate */
        printf("Draw mode: %s\n", g_gate?"1.1 gate (draw when the previous frame was taken)":"every frame, deferred publication");
    } else printf("Blob is an older blob (no diagnostic fields)\n");
    /* VTC : region posee AVANT cmd_start (le blob l'applique avant PicoReset) */
    wr32(shared,SH_REGION_IN,region_in);
    if(!send_cmd(shared,ZP_CMD_START,250)){ printf("START timeout\n"); diag_fault_report(shared); g_exit_reason="START timeout"; rc=15; goto stop; }
    sram_load(shared, fb, cfg->rom_path);   /* charge ROMNAME.srm si present */
    if(rd32(shared,SH_STATUS)!=0x02){ printf("Not RUNNING\n"); diag_fault_report(shared); g_exit_reason="not RUNNING after START"; rc=16; goto stop; }
    /* pal effectif resolu par PicoDrive -> pacing 50/60 */
    pal_out=rd32(shared,SH_PAL_OUT); target_hz=pal_out?50UL:60UL;
    g_session_pal=pal_out?1:0; g_session_reg=(int)rd32(shared,SH_REGION_EFF);   /* rc2n: savestate region tag */
    printf("region=%s->%s  target=%luHz  regionOverride=%lu\n",
           region_label, pal_out?"PAL":"NTSC", target_hz, rd32(shared,SH_REGION_EFF));
    printf("RUNNING. Opening 32-bit screen + double buffer...\n");

    have_timer=timer_open();
    if(!have_timer) printf("(timer.device indisponible : pas de mesure FPS)\n");
    /* HW_VBLANK_PACING : sonde le registre vblank. Si le bit bat -> VBLANK_HW
       (tear-free), sinon repli TIMER. Jamais bloquant. uncapped -> ni l'un ni
       l'autre (benchmark libre). */
    /* rc2: the vblank probe and the PLL lock are done AFTER the P96 screen and the
       50 Hz timing are up (below): the period measured here on the Workbench mode
       would be wrong for the game screen. */
    eclock_now(&efreq_main);

    P96Base      =OpenLibrary("Picasso96API.library",2L);   /* intuition/gfx : ouverts par main */
    if(g_input_mode==2){
        /* INPUT_USB_HID : squelette. Lecture HID USB (Poseidon/input.device) pas encore
           implementee -> repli clavier. Si le pad USB est expose en game controller par
           Poseidon, le mode 'cd32' (lowlevel) peut deja le lire. */
        printf("INPUT USB HID : squelette non implemente -> repli clavier.\n");
        printf("  (si ton pad USB passe par Poseidon en game controller, essaie 'cd32')\n");
        g_input_mode=0;
    }
    /* lowlevel : ouvert si le joueur 1 OU le joueur 2 utilise le mode manette detectee */
    if(g_input_mode==1 || (g_players==2 && g_input_mode2==1)){
        LowLevelBase=OpenLibrary("lowlevel.library",39L);
        if(LowLevelBase){
            if(g_input_mode==1)  SetJoyPortAttrs(g_joyport,  SJA_Type,SJA_TYPE_AUTOSENSE,TAG_END);
            if(g_players==2 && g_input_mode2==1) SetJoyPortAttrs(g_joyport2, SJA_Type,SJA_TYPE_AUTOSENSE,TAG_END);
            printf("INPUT lowlevel actif\n");
        } else printf("INPUT lowlevel absente -> hardware direct\n");
    }
    if(g_input_mode==3) printf("INPUT J1 DB9 hardware pur, port %lu\n", g_joyport);
    if(g_players==2) printf("INPUT 2 joueurs : J1 port %lu, J2 port %lu\n", g_joyport, g_joyport2);
    if(!IntuitionBase||!GfxBase_||!P96Base){ printf("libs missing (P96?)\n"); g_exit_reason="Picasso96 missing"; rc=17; goto stop; }

    modeid=p96BestModeIDTags(P96BIDTAG_NominalWidth,(ULONG)IMG_W,
        P96BIDTAG_NominalHeight,(ULONG)IMG_H,P96BIDTAG_Depth,32,TAG_DONE);
    if(modeid==(ULONG)INVALID_ID){ printf("No 32-bit RTG mode\n"); g_exit_reason="no 320x240 32-bit screen mode"; rc=18; goto stop; }
    /* v1.01 : ouverture ecran robuste. On essaie D'ABORD l'ouverture stricte
       (comportement V1.0 valide, qui marche sur la plupart des configs). Seulement
       si elle ECHOUE (certaines AmigaOS 3.2 + Picasso96/iComp recentes la refusent
       alors que le mode est valide), on bascule sur une ouverture permissive
       (methode C, puis D). La validation du framebuffer reste faite plus bas sur
       les bitmaps des ScreenBuffers (chemin eprouve V1.0), pas ici. */
    printf("P96 modeid=0x%08lX\n", modeid);
    my_scr=OpenScreenTags(NULL,SA_DisplayID,modeid,SA_Width,(ULONG)IMG_W,
        SA_Height,(ULONG)IMG_H,SA_Depth,32,SA_Quiet,TRUE,SA_ShowTitle,FALSE,
        SA_Type,CUSTOMSCREEN,TAG_DONE);
    if(my_scr) printf("OpenScreen strict: OK\n");
    else {
        printf("OpenScreen strict: FAIL -> methode C permissive\n");
        my_scr=OpenScreenTags(NULL,SA_DisplayID,modeid,
            SA_Type,CUSTOMSCREEN,SA_Quiet,TRUE,TAG_DONE);
        if(my_scr) printf("OpenScreen method C: OK\n");
        else {
            my_scr=OpenScreenTags(NULL,SA_DisplayID,modeid,
                SA_Type,CUSTOMSCREEN,TAG_DONE);
            if(my_scr) printf("OpenScreen method D: OK\n");
        }
    }
    if(!my_scr){
        printf("OpenScreen failed for P96 modeid=0x%08lX\n", modeid);
        printf("Run ZZP96Check and post the output.\n");
        rc=18; goto stop;
    }
    win=(struct Window*)OpenWindowTags(NULL,WA_Left,0,WA_Top,0,WA_Width,(ULONG)IMG_W,
        WA_Height,(ULONG)IMG_H,WA_Flags,WFLG_BACKDROP|WFLG_BORDERLESS|WFLG_RMBTRAP|WFLG_ACTIVATE,
        WA_IDCMP,IDCMP_RAWKEY,WA_CustomScreen,(ULONG)my_scr,TAG_DONE);   /* v1.2: no mouse button events (the mouse never quits) */
    if(!win){ printf("OpenWindow failed\n"); g_exit_reason="window could not be opened"; rc=18; goto closescr; }

    /* cacher le pointeur souris sur l'ecran PicoDrive (sprite blank en Chip RAM) */
    g_blankspr=(UWORD*)AllocMem(12L, MEMF_CHIP|MEMF_CLEAR);   /* 6 words a 0 = invisible */
    if(g_blankspr) SetPointer(win, g_blankspr, 1, 16, 0, 0);

    /* TRIPLE BUFFER. buf[0]=bitmap ecran (toujours en VRAM). buf[1]/buf[2]=backbuffers.
       On tente d'abord AllocScreenBuffer (chemin V1.0, marche sur configs normales).
       Sur AmigaOS 3.2 + P96/iComp recent, l'ecran s'ouvre (methode C/D) mais
       AllocScreenBuffer ne fournit PAS de backbuffers VRAM secondaires (mem=0). Dans ce
       cas on alloue les backbuffers MANUELLEMENT en VRAM via p96AllocBitMap avec le
       bitmap ecran 32 bits comme friend -> memes offsets VRAM -> deferred-PAN inchange.
       Lecture des attributs NON-LOCKEE (p96LockBitMap gele certaines P96 recentes). */
    dbuf_port=(struct MsgPort*)CreateMsgPort();
    { struct BitMap *friend=my_scr->RastPort.BitMap;
      ULONG rgbf=p96GetBitMapAttr(friend,P96BMA_RGBFORMAT);
      struct BitMap *rbm[3]={NULL,NULL,NULL};
      int k, bad=0, sb_ok;

      sb[0]=AllocScreenBuffer(my_scr,NULL,SB_SCREEN_BITMAP);
      if(!sb[0]){ printf("AllocScreenBuffer(screen) failed\n"); rc=19; goto freebuf; }
      rbm[0]=sb[0]->sb_BitMap;

      sb[1]=AllocScreenBuffer(my_scr,NULL,0);
      sb[2]=AllocScreenBuffer(my_scr,NULL,0);
      sb_ok=(sb[1]&&sb[2]);
      if(sb_ok){
          ULONG m1=p96GetBitMapAttr(sb[1]->sb_BitMap,P96BMA_MEMORY);
          ULONG m2=p96GetBitMapAttr(sb[2]->sb_BitMap,P96BMA_MEMORY);
          if(m1==0UL||m2==0UL||m1<(ULONG)fb||m2<(ULONG)fb) sb_ok=0;
      }
      if(sb_ok){
          rbm[1]=sb[1]->sb_BitMap; rbm[2]=sb[2]->sb_BitMap;
          printf("backbuffers: AllocScreenBuffer VRAM OK\n");
      } else {
          /* fallback : liberer les ScreenBuffers secondaires, allouer en VRAM manuel */
          printf("backbuffers: AllocScreenBuffer not VRAM -> p96AllocBitMap (friend=screen)\n");
          if(sb[1]){ FreeScreenBuffer(my_scr,sb[1]); sb[1]=NULL; }
          if(sb[2]){ FreeScreenBuffer(my_scr,sb[2]); sb[2]=NULL; }
          man_bm[1]=p96AllocBitMap((ULONG)IMG_W,(ULONG)IMG_H,32UL,BMF_DISPLAYABLE|BMF_CLEAR,friend,rgbf);
          man_bm[2]=p96AllocBitMap((ULONG)IMG_W,(ULONG)IMG_H,32UL,BMF_DISPLAYABLE|BMF_CLEAR,friend,rgbf);
          rbm[1]=man_bm[1]; rbm[2]=man_bm[2];
          if(!rbm[1]||!rbm[2]){
              printf("ERROR: manual p96AllocBitMap backbuffers failed\n"); rc=19; goto freebuf; }
      }

      /* validation non-lockee des 3 bitmaps de rendu */
      for(k=0;k<3;k++){
          ULONG bpr=p96GetBitMapAttr(rbm[k],P96BMA_BYTESPERROW);
          ULONG mem=p96GetBitMapAttr(rbm[k],P96BMA_MEMORY);
          printf("buf[%d] mem=0x%08lX bpr=%lu\n",k,mem,bpr);
          if(mem==0UL||bpr==0UL){ printf("ERROR: buf[%d] not VRAM (mem/bpr=0)\n",k); bad=1; }
          else if(mem<(ULONG)fb){ printf("ERROR: buf[%d] outside ZZ9000 VRAM (mem<fb)\n",k); bad=1; }
          else if(bpr<(ULONG)IMG_W*4UL){ printf("ERROR: buf[%d] pitch %lu < %lu\n",k,bpr,(ULONG)IMG_W*4UL); bad=1; }
          else { pitch=bpr; buf_mem[k]=mem; buf_arm[k]=CPU_TO_ARM(mem,fb); buf_off[k]=(ULONG)mem-(ULONG)fb; }
      }
      if(bad){
          printf("ERROR: no usable 32-bit VRAM backbuffers on this setup.\n");
          printf("Run ZZP96Test and post the output.\n");
          rc=19; goto freebuf; }
    }
    if(buf_mem[0]==buf_mem[1]||buf_mem[0]==buf_mem[2]||buf_mem[1]==buf_mem[2]){
        printf("ERROR: identical buffer addresses\n"); rc=19; goto freebuf; }
    pan_orig = buf_off[0];

    if(dbuf_port){
        dbuf_sig=1UL<<dbuf_port->mp_SigBit;
        if(sb[0] && sb[0]->sb_DBufInfo) sb[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort=dbuf_port;
        if(sb[1] && sb[1]->sb_DBufInfo) sb[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort=dbuf_port;
        if(sb[2] && sb[2]->sb_DBufInfo) sb[2]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort=dbuf_port;
    }

    /* active le rendu direct P96 cote blob */
    wr32(shared,SH_P96_PITCH,pitch);
    wr32(shared,SH_P96_ENABLE,1);

    /* clear noir des 3 buffers AVANT tout PAN (pas de garbage au demarrage) */
    {
        ULONG nw=(((ULONG)IMG_H*pitch)>>2), i; int k;
        for(k=0;k<3;k++){ volatile ULONG *b=(volatile ULONG*)buf_mem[k];
                          for(i=0;i<nw;i++) b[i]=0UL; }
    }
    zz_pan(board, pan_orig);   /* afficher buffer 0 (noir) */
    /* rc2: PAL game -> physical 50 Hz output (custom timing under the P96 screen) */
    if(pal_out && !g_no50 && my_scr && my_scr->Width==IMG_W && my_scr->Height==IMG_H){
        zz_pal50_enable(board, ZZ_CTM_VMAX_PAL50, 50); g_pal50_active=1; g_pal50_used=1; g_ctm_label="50 Hz custom (625 lines)";
        printf("PAL50: physical 640x480 at 50 Hz programmed (custom slot %d, 625 lines); flag no50 keeps the preset\n", ZZ_PAL50_CUSTOM_SLOT);
        Delay(5);   /* let the output settle before measuring the vblank period */
    } else if(!pal_out && !g_no60 && my_scr && my_scr->Width==IMG_W && my_scr->Height==IMG_H){
        /* rc2c: the 640x480 preset is 59.52 Hz (25.0 MHz / 800 / 525); 521 lines -> 59.98 Hz */
        zz_pal50_enable(board, ZZ_CTM_VMAX_NTSC60, 60); g_pal50_active=1; g_pal50_used=1; g_ctm_label="59.98 Hz custom (521 lines)";
        printf("NTSC60: physical 640x480 at 59.98 Hz programmed (custom slot %d, 521 lines); flag no60 keeps the 59.52 Hz preset\n", ZZ_PAL50_CUSTOM_SLOT);
        Delay(5);
    } else printf("Output timing: %s\n", pal_out?"preset kept (flag no50 or non-320x240 screen)":"NTSC game, 59.52 Hz preset kept (flag no60 or non-320x240 screen)");
    /* rc2: vblank probe + PLL lock on the final output */
    vbl_ok = (!uncapped && have_timer) ? probe_vblank(board) : 0;
    /* 32X v1.2: up to 5 attempts (first hardware run of the 32X v1.2 RC1: the
       probe saw the vblank but the 4-edge measurement failed once, and the whole
       session ran on WaitTOF at the Amiga's 60 Hz: 56 FPS, 6 % dropped) */
    if(vbl_ok && !g_force_tof && sleep_open()){
        int att;
        for(att=1; att<=5; att++){
            if(vb_pll_init(board, efreq_main)){ g_pace_zz=1; break; }
            printf("PLL lock attempt %d failed (reason %d: %s, period=%lu us)\n", att, g_vb_init_fail,
                   g_vb_init_fail==5?"period out of range":(g_vb_init_fail==9?"no timer":"edge not seen within 50 ms"),
                   efreq_main? (g_vb_init_per*1000UL)/(efreq_main/1000UL) : 0UL);
            Delay(2);
        }
    }
    pacing_mode = uncapped ? "UNCAPPED" : (g_pace_zz ? "ZZ9000_VBLANK_PLL" : "WAITTOF");
    printf("pacing_mode=%s (vblank probe=%s, period=%lu us, pulse=%lu us)\n",
           pacing_mode, vbl_ok?"OK":(uncapped?"skip":"no-signal"),
           (g_pace_zz && efreq_main)? (g_vb_period*1000UL)/(efreq_main/1000UL) : 0UL,
           (g_pace_zz && efreq_main)? (g_vb_pulse*1000UL)/(efreq_main/1000UL) : 0UL);

    /* ---- AUDIO_STEREO_22050_AHI dans une TACHE SEPAREE : isole l'audio du timing video
       -> le mixage AHI ne preempte plus le flip -> FPS preserve + underruns ~0. ---- */
    if(rd32(shared,SH_PCM_ENABLED)==1){
        ULONG pcm_arm=rd32(shared,SH_PCM_BASE);
        g_task_shared=shared;
        g_task_ring=(volatile UBYTE*)((ULONG)fb + (pcm_arm - 0x00200000UL));
        wr32(shared,SH_PCM_READ_POS,0);
        wr32(shared,SH_PCM_TESTPAT,0);   /* 0 = Sonic (pas le tone de test) */
        g_main_task=FindTask(NULL);
        if(cfg->audio==ZP_AUDIO_PAULA){
            /* backend Paula DMA direct : init/start dans le thread principal, fill sur WaitTOF */
            pa_ntsc = pal_out ? 0 : 1;   /* fix 31.10: resolved region (AUTO picked PAL before) */
            if(pa_init(g_task_shared, g_task_ring)){ pa_start(); g_pa_active=1; g_audio_on=1;
                printf("AUDIO: backend Paula DMA direct\n"); }
            else { pa_stop(); printf("AUDIO: init Paula KO -> pas de son\n"); }
        } else {
            /* backend AHI (defaut) ou None (silence) : tache separee */
            g_done_sig=AllocSignal(-1L);
            if(g_done_sig>=0){
                g_audio_proc=(struct Process*)CreateNewProcTags(
                    NP_Entry,(ULONG)audio_task, NP_Name,(ULONG)"ZZPicoAudio",
                    NP_Priority,5L, NP_StackSize,16384UL, TAG_DONE);
            }
            if(g_audio_proc){ g_audio_on=1; printf("AUDIO: backend AHI (tache separee)\n"); }
            else { printf("AUDIO: CreateNewProc echoue -> pas de son\n");
                   if(g_done_sig>=0){ FreeSignal(g_done_sig); g_done_sig=-1; } }
        }
    }
    printf("Direct P96 render ON. screen=%dx%d modeid=0x%08lX pacing=%s target=%luHz\n",
           IMG_W, IMG_H, modeid, pacing_mode, target_hz);
    printf("Press any key to STOP.\n");

    /* boucle : rendre dans back -> flip -> swap, jusqu'a touche.
       front=0 affiche au depart ; on rend dans back=1 puis on flippe. */
    /* ---- Boucle principale : Core1 rend en autonome et publie FRAME_READY.
       Le 68k DORT sur WaitTOF() (vblank Amiga ~50Hz) puis flippe via PAN differe
       firmware (applique au vblank ZZ9000, ACK sur USER4). Plus de drive_one_frame,
       plus de busy-poll FRAME_DONE, plus de busy-wait vblank -> fin du 99% CPU. ---- */
    /* ---- Boucle triple-buffer ----
       Core1 s'auto-cadence a 50/60Hz et rend EN AVANCE dans un buffer LIBRE.
       Le 68k dort sur WaitTOF() (vblank RTG) puis, dans la MEME iteration :
         (1) confirme le flip precedent (ACK firmware) ;
         (2) si une nouvelle frame est prete -> arme son PAN differe + donne a Core1
             un buffer libre + ecrit FLIP_SEQ (="pris, rends la suivante").
       3 buffers : front_idx (affiche), pending_idx (PAN arme, ACK attendu), render_idx
       (Core1 y rend). La cadence des flips = celle de Core1 (50Hz PAL), pas le RTG. */
    { int front_idx=0, render_idx=1, pending_idx=-1;   /* buffer 2 = libre au depart */
      int running=1, flip_inflight=0;
      ULONG last_taken=0, flip_ready=0, flip_ack_before=0;
      ULONG frames=0, efreq=0, t_start, t_flip=0;
      /* A1diag: watchdog state (all reads are 3-5 rd32 per vblank, negligible) */
      int   quit_count=0;                      /* >0: quit requested, B snapshot pending */
      ULONG ack_consecutive=0;                 /* rc2: consecutive PAN ack timeouts */
      ULONG wd_fc=0, wd_fc_iter=0, wd_rd=0, wd_rd_iter=0, wd_pc=0, wd_pc_iter=0;
      ULONG wd_next_stall=0, wd_next_noframe=0, wd_next_sh2=0;
      int   is_zvr=zvr_blob(shared);
      BYTE old_pri=0; int pri_raised=0;
      t_start=eclock_now(&efreq); g_t_loop=t_start;
      int settled=0; ULONG settle_ok=0, settle_fc=0;
      ULONG settle_d[8]={0,0,0,0,0,0,0,0}, settle_n=0;   /* v1.2 update: last 8 per-vblank increments */
      wr32(shared, SH_FLIP_SEQ, 0);
      /* rc2h: P96_BASE (Core1 renders into render_idx) is written by the settle
         step below once the blob runs at one PicoFrame per vblank */
      settle_fc=rd32(shared,SH_FRAME_COUNTER);
      if(g_nosettle || !g_pace_zz){ wr32(shared, SH_P96_BASE, buf_arm[render_idx]); settled=1; }
      /* rc2g: counters at loop start (the blob cannot publish before P96_BASE is set) */
      g_drop_at_start = diag_blob(shared) ? rd32(shared,SH_PUB_DROPPED) : 0UL;
      g_emu_at_start  = rd32(shared,SH_FRAME_COUNTER);
      /* rc2c: the main task does a few register accesses per vblank and sleeps
         the rest of the time; the AHI task (priority 5) reads 1024 Zorro III
         words per buffer 43 times a second and the timer wake-up of this task
         waited for it (late wake-ups, frames found not ready).  Above it, this
         task preempts those reads for a few microseconds; the AHI task keeps
         46 ms of audio in flight.  Flag pri0 keeps the Shell priority. */
      if(g_pace_zz && !g_pri0){ old_pri=SetTaskPri(FindTask(NULL), 6); pri_raised=1; }
      g_loop_pri=(LONG)FindTask(NULL)->tc_Node.ln_Pri; g_loop_pri_raised=pri_raised;   /* rc2k: printed with the statistics (a printf here delayed the first vblanks) */
#define TAKE_FRAME_IF_READY(counter) do{ \
              ULONG ready_ = rd32(shared, SH_FRAME_READY); \
              if(ready_ != last_taken){ \
                  flip_ack_before = zz_pan_ack(board); \
                  ZZ_WR(board, REG_ZZ_USER3, 1);                    /* arme UN PAN */ \
                  zz_deferred_pan_request(board, buf_off[render_idx], pitch); \
                  pending_idx = render_idx;                         /* ce buffer sera affiche */ \
                  render_idx  = 3 - front_idx - pending_idx;        /* le 3e = libre -> Core1 */ \
                  wr32(shared, SH_P96_BASE, buf_arm[render_idx]); \
                  wr32(shared, SH_FLIP_SEQ, ready_);                /* "pris, rends la suivante" */ \
                  flip_ready = ready_; last_taken = ready_; t_flip=eclock_now(NULL); flip_inflight=1; \
                  g_flips++; counter; \
              } }while(0)
      while(running){
          if(g_pace_zz) vb_pll_wait(board, efreq, flip_inflight?flip_ack_before:VB_NOACK);   /* 32X: ZZ9000 vblank edge (PLL) */
          else WaitTOF();                            /* 1.1: Amiga vblank */
          if(g_pa_active) pa_service();   /* backend Paula : remplit le ring Chip (per iteration, EClock based) */
          /* AUDIO : rien ici, l'AudioTask separee gere AHI -> pas de preemption du flip */
          g_wd_iter++;
          /* rc2h: start settle.  Right after the audio start the blob fills the
             audio buffers at 2-3 PicoFrames per vblank; frames published then can
             only be overwritten (7 of the 12 drops of a 100 s rc2g run).  Publish
             from the first vblank with a steady 1 PicoFrame per vblank (4 in a
             row), at most 60 vblanks after the start. */
          if(!settled){
              ULONG fc=rd32(shared,SH_FRAME_COUNTER), d=fc-settle_fc;
              settle_fc=fc;
              /* v1.2 update: steady = 7 to 9 PicoFrames over the last 8 vblanks,
                 never more than 2 in one (with AHI's 512-pair buffers the blob
                 produces 0, 1 or 2 per vblank: "exactly 1, 8 in a row" could run
                 to the 180-vblank limit).  Initial fill and idle blob excluded. */
              settle_d[settle_n&7UL]=d; settle_n++;
              { ULONG k, sum=0, mx=0;
                for(k=0;k<8UL;k++){ sum+=settle_d[k]; if(settle_d[k]>mx) mx=settle_d[k]; }
                settle_ok = (settle_n>=8UL && sum>=7UL && sum<=9UL && mx<=2UL) ? 8UL : 0UL; }
              if(settle_ok>=8UL || g_wd_iter>=180UL){
                  wr32(shared, SH_P96_BASE, buf_arm[render_idx]);
                  settled=1; g_settle_iters=g_wd_iter;
                  g_drop_at_start = diag_blob(shared) ? rd32(shared,SH_PUB_DROPPED) : 0UL;
                  g_emu_at_start  = fc;
              }
          }
          /* rc2g: record every increment of the blob's dropped counter (no printf here) */
          if(diag_blob(shared)){
              static ULONG drop_seen=0xFFFFFFFFUL;
              ULONG d=rd32(shared,SH_PUB_DROPPED);
              if(drop_seen==0xFFFFFFFFUL) drop_seen=d;
              if(d!=drop_seen){
                  ULONG ms=efreq?(eclock_now(NULL)-t_start)/(efreq/1000UL):0UL;
                  if(ms<2000UL) g_drop_first2s+=d-drop_seen;
                  if(g_drop_ev_n<DROP_EV_MAX){
                      struct drop_ev *ev=&g_drop_ev[g_drop_ev_n++];
                      ev->iter=g_wd_iter; ev->ms=ms; ev->dropped=d; ev->ready=rd32(shared,SH_FRAME_READY); ev->taken=last_taken;
                      ev->late=g_vb_late; ev->inpulse=g_vb_inpulse; ev->byack=g_vb_by_ack; ev->unc_rej=g_vb_unc_rej; ev->recheck=g_take_recheck;
                      ev->inflight=flip_inflight; ev->path=g_vb_path;
                      ev->pa_ur=pa_underruns; ev->pa_rs=pa_resyncs; ev->emu=rd32(shared,SH_FRAME_COUNTER); ev->latmax=g_vb_lat_max_us;
                  }
                  drop_seen=d;
              }
          }

          /* (1) confirmer le flip en vol.  32X: right after the edge the firmware may
             still be inside its loop iteration: give the ack up to ~400 register reads
             (well under a millisecond) before concluding it is not there yet. */
          if(flip_inflight){
              ULONG ack=zz_pan_ack(board), ap=0;
              if(g_pace_zz){ while(ack==flip_ack_before && ap<400UL){ ap++; ack=zz_pan_ack(board); }
                             if(ap>g_ack_poll_max) g_ack_poll_max=ap; if(ack==flip_ack_before) g_ack_notseen++; }
              if(ack != flip_ack_before){
                  ULONG now=eclock_now(NULL);
                  if(efreq){ ULONG ms=(now - t_flip)/(efreq/1000UL); if(ms>g_ack_max_ms) g_ack_max_ms=ms; }
                  front_idx = pending_idx;          /* buffer flippe -> affiche */
                  pending_idx = -1; flip_inflight = 0; frames++; ack_consecutive=0;
              } else {
                  ULONG now=eclock_now(NULL);
                  if(efreq && (now - t_flip) > efreq/10UL){
                      /* rc2: tolerant.  100 ms without ack: cancel the deferred PAN in
                         flight, re-arm the same buffer and go on; leave only after ten
                         consecutive timeouts (1.1 left at the first one: "spontaneous
                         return to the Workbench"). */
                      g_ack_timeouts++; ack_consecutive++;
                      printf("PAN ACK timeout #%lu (ready=%lu iter=%lu ack=%lu before=%lu)\n",
                             ack_consecutive, flip_ready, g_wd_iter, zz_pan_ack(board), flip_ack_before);
                      ZZ_WR(board, REG_ZZ_USER3, 2);
                      if(ack_consecutive>=10UL){ g_exit_reason="PAN ACK timeout x10"; running=0; rc=21; }
                      else {
                          flip_ack_before = zz_pan_ack(board);
                          ZZ_WR(board, REG_ZZ_USER3, 1);
                          zz_deferred_pan_request(board, buf_off[pending_idx], pitch);
                          t_flip=eclock_now(NULL);
                      }
                  }
              }
          }
          /* (2) prendre la derniere frame prete + armer son PAN */
          if(!flip_inflight && settled) TAKE_FRAME_IF_READY((void)0);
          /* A1diag: publish the loop state for the snapshots */
          g_lp_inflight=flip_inflight; g_lp_front=front_idx; g_lp_pending=pending_idx;
          g_lp_render=render_idx; g_lp_last_taken=last_taken;
          /* ---- INPUT clavier (mode 0) : non bloquant, cout quasi nul ---- */
          { struct IntuiMessage *m;
            while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
                ULONG cl=m->Class, code=m->Code, qual=m->Qualifier;
                ReplyMsg((struct Message*)m);
                if(cl!=IDCMP_RAWKEY) continue;
                { int up=(code&0x80)?1:0, raw=(int)(code&0x7F);
                  if(g_input_log)
                      printf("RAWKEY code=0x%02lx base=0x%02lx %s qual=0x%04lx\n",
                             (ULONG)code,(ULONG)raw,up?"UP":"DOWN",(ULONG)qual);
                  if(raw<128) key_down[raw]=up?0:1;
                  if(!up){   /* commandes emulateur sur DOWN */
                      if(raw==RK_ESC){
                          if(qual&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)){
                              /* A1diag: snapshot A now, keep servicing audio/flips for 30
                                 vblanks, snapshot B, then leave (1.1 left at once) */
                              if(!quit_count){ FREEZE_AT_QUIT(); printf("QUIT (Shift+Esc) at iter=%lu\n", g_wd_iter); g_exit_reason="Shift+Esc";
                                               diag_snapshot(shared, board, "A (quit)"); quit_count=30; }
                          }
                          else g_emu_cmd=EMU_CMD_MENU;   /* menu emulateur (pas encore d'UI) */
                      } else if(raw==RK_DEL){ if(!g_emu_pending){ g_emu_pending=EMU_CMD_RESET; g_emu_sent=0; printf("RESET requested\n"); } }
                      else if(raw==RK_P) g_emu_cmd=EMU_CMD_PAUSE;
                      else if(raw>=RK_F1 && raw<=RK_F5 && !(qual&IEQUALIFIER_REPEAT)){   /* rc2l: no auto-repeat */
                          int slot=(raw-RK_F1)+1;   /* slots 1..5 */
                          if(qual&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))
                              state_save(shared, fb, cfg->rom_path, slot);
                          else
                              state_load(shared, fb, cfg->rom_path, slot);
                      }
                  }
                }
            } }
          /* recompose les bitmasks pad via read_pad (P1 toujours, P2 si active) */
          g_padmask  = read_pad(g_input_mode,  g_joyport,  0);
          g_padmask2 = (g_players==2) ? read_pad(g_input_mode2, g_joyport2, 1) : 0;
          /* ecrit les pads dans la shared (Core1 les lit chaque frame) ; seq++ si change */
          if(g_padmask!=g_padmask_prev || g_padmask2!=g_padmask2_prev){
              g_input_seq++; g_padmask_prev=g_padmask; g_padmask2_prev=g_padmask2; }
          wr32(shared, SH_PAD0, g_padmask);
          wr32(shared, SH_PAD1_P2, g_padmask2);
          /* 32X: one-shot emulator command (reset).  1.1 rewrote g_emu_cmd every
             vblank, which would have re-fired a RESET at every frame. */
          if(g_emu_pending){
              ULONG cur=rd32(shared, SH_EMU_CMD);
              if(cur==EMU_CMD_NONE){
                  if(!g_emu_sent){ wr32(shared, SH_EMU_CMD, g_emu_pending); g_emu_sent=1; }
                  else { printf("RESET done (iter=%lu)\n", g_wd_iter); g_emu_pending=0; g_emu_sent=0; }
              }
          }
          wr32(shared, SH_INPUT_SEQ, g_input_seq);

          /* ---- A1diag WATCHDOG : one line when something stops moving for ~2 s ----
             (120 vblanks = 2.0 s NTSC / 2.4 s PAL; repeated every 600 vblanks while it lasts) */
          { ULONG fc=rd32(shared,SH_FRAME_COUNTER), rd=rd32(shared,SH_FRAME_READY);
            if(fc!=wd_fc){ wd_fc=fc; wd_fc_iter=g_wd_iter; }
            if(rd!=wd_rd){ wd_rd=rd; wd_rd_iter=g_wd_iter; }
            if(is_zvr){ ULONG pc=rd32(shared,SH_ZVR_MSH2_PC) ^ (rd32(shared,SH_ZVR_SSH2_PC)<<1);
                        if(pc!=wd_pc){ wd_pc=pc; wd_pc_iter=g_wd_iter; } }
            if(g_wd_iter - wd_fc_iter >= 120 && g_wd_iter >= wd_next_stall){
                wd_next_stall = g_wd_iter + 600;
                printf("WATCHDOG iter=%lu: BLOB STALL, no PicoFrame for %lu vblanks (frames=%lu)\n",
                       g_wd_iter, g_wd_iter - wd_fc_iter, fc);
                diag_snapshot(shared, board, "stall");
            } else if(g_wd_iter - wd_rd_iter >= 120 && g_wd_iter - wd_fc_iter < 120 && g_wd_iter >= wd_next_noframe){
                wd_next_noframe = g_wd_iter + 600;
                printf("WATCHDOG iter=%lu: NO NEW FRAME for %lu vblanks while the blob runs (ready=%lu flip=%lu inflight=%d)\n",
                       g_wd_iter, g_wd_iter - wd_rd_iter, rd, rd32(shared,SH_FLIP_SEQ), flip_inflight);
                diag_snapshot(shared, board, "noframe");
            } else if(is_zvr && (rd32(shared,SH_ZVR_AHW)&2UL) && g_wd_iter - wd_pc_iter >= 120 && g_wd_iter - wd_fc_iter < 120 && g_wd_iter >= wd_next_sh2){
                wd_next_sh2 = g_wd_iter + 600;
                printf("WATCHDOG iter=%lu: SH-2 PCS FROZEN for %lu vblanks while PicoFrame runs (msh2=0x%08lx ssh2=0x%08lx)\n",
                       g_wd_iter, g_wd_iter - wd_pc_iter, rd32(shared,SH_ZVR_MSH2_PC), rd32(shared,SH_ZVR_SSH2_PC));
                diag_snapshot(shared, board, "sh2");
            }
          }
          /* rc2: AHI task gave up (driver errors / instant completions): leave cleanly */
          if(g_ahi_failed && !quit_count){
              printf("AHI FAILURE: errors=%lu fast=%lu first io_Error=%lu -> leaving\n", g_ahi_errors, g_ahi_fast, g_ahi_first_err);
              g_exit_reason="AHI failure"; diag_snapshot(shared, board, "A (ahi failure)"); quit_count=5;
          }
          /* A1diag: deferred quit -> snapshot B with audio/flips still serviced */
          if(quit_count){ if(--quit_count==0){ diag_snapshot(shared, board, "B (+30 vblanks)"); running=0; } }
          /* rc2c: no frame was ready right after the edge (the blob publishes the
             next one 1 ms after FLIP_SEQ when it is in its audio wait, up to
             ~10 ms when FLIP_SEQ caught it inside a PicoFrame, and a late
             FLIP_SEQ pushes that past the next edge).  Instead of sleeping to the
             next edge and losing the slot, re-check FRAME_READY every 2 ms until
             0.8 ms before the predicted edge; a PAN armed here is applied at that
             edge like one armed right after the previous edge.  The normal path
             (frame ready at once, one sleep) is unchanged. */
          if(g_pace_zz && !g_norecheck && !flip_inflight && running && efreq && settled){
              LONG margin=(LONG)((efreq/1000UL)*VB_MARGIN_US/1000UL), chunk=(LONG)(efreq/500UL);   /* 0.8 ms, 2 ms */
              while(!flip_inflight){
                  LONG ahead=(LONG)(g_vb_next - eclock_now(NULL));
                  if(ahead <= margin + chunk) break;
                  sleep_us(2000UL);
                  TAKE_FRAME_IF_READY(g_take_recheck++);
              }
          }
      }
      if(pri_raised) SetTaskPri(FindTask(NULL), old_pri);
      { ULONG total=eclock_now(NULL)-t_start;
        g_frames=frames; g_efreq=efreq;
        if(efreq>1000 && frames>0){
            ULONG efk=efreq/1000, el_ms=total/efk;
            g_fps10 = el_ms? (frames*10000UL)/el_ms : 0; g_el_ms=el_ms;
        } }
      g_rend10=0; g_flip10=0; g_rmin=0; g_rmax=0; g_r12=0; g_r14=0; g_r67=0;
      g_fmin=0; g_fmax=0; g_missed=0; g_vbmax=0; g_vbsum=0;
    }
    /* ================= A1diag : instrumented exit path =================
       Same resources, same order as 1.1 except: (3) the deferred PAN in flight is
       cancelled before anything is freed, (5) the SRAM save runs while the blob
       still services save commands (after STOP the blob sits in wfe and the 1.1
       sram_save could only time out), (6) STOP waits for STATUS=STOPPED. */
    stack_report("exit begin");
    /* fix 31.10: the SRAM save is done while the audio still runs.  The blob
       serves the save between two PicoFrames and starts a PicoFrame only when
       the audio ring is drained: with the audio already stopped (old step 5)
       the save waited for the blob's pacing guard and timed out. */
    if(shared && rd32(shared,SH_STATUS)==0x02){
        exit_step("0 SRAM save (audio still running)");
        sram_save(shared, fb, cfg->rom_path);
    }
    exit_step("1 audio stop");
    /* arret propre audio : Paula DMA direct (pa_stop) ou tache AHI selon le backend actif */
    if(g_pa_active){ pa_stop(); g_pa_active=0; g_audio_on=0; }
    else if(g_audio_on){
        g_audio_stop=1;
        if(g_audio_proc) Signal(&g_audio_proc->pr_Task, SIGBREAKF_CTRL_C);
        if(g_done_sig>=0){ Wait(1UL<<g_done_sig); FreeSignal(g_done_sig); g_done_sig=-1; }
        g_audio_on=0;
    }
    exit_step("2 P96_ENABLE=0 (blob stops writing the P96 buffers)");
    /* desactive le rendu direct avant de liberer les buffers */
    wr32(shared,SH_P96_ENABLE,0);
    exit_step("3 cancel deferred PAN in flight (USER3=2), 2 vblanks");
    if(board){ ULONG a0=zz_pan_ack(board);
               ZZ_WR(board, REG_ZZ_USER3, 2); WaitTOF(); WaitTOF();
               printf("  panack before=%lu after=%lu inflight_was=%d\n", a0, zz_pan_ack(board), g_lp_inflight); }
    exit_step("4 PAN back to the screen bitmap (immediate), custom timing restored");
    if(board && g_pal50_active){ zz_pal50_restore(board); g_pal50_active=0; WaitTOF(); WaitTOF(); }
    /* IMPORTANT : restaurer le PAN d'origine (bitmap ecran sb[0]) AVANT de
       liberer. Intuition n'a jamais ete bascule (on a pane le firmware, pas
       ChangeScreenBuffer) : sa vue reste sb[0], on remet le scan-out dessus. */
    if(board){ zz_pan(board, pan_orig); WaitTOF(); }

    if(shared){
        exit_step("6 STOP command + wait STOPPED   (5: SRAM saved at step 0)");
        /* FIX SORTIE (comme SMS) : ARRETER LE BLOB (avec ack) AVANT de liberer la moindre
           ressource qu'il touche (framebuffers, ring, shared). */
        stop_blob(shared, board);
        blob_stopped=1;
        printf("Core1 stopped.\n");
        diag_a1_report(shared);
        diag_snapshot(shared, board, "C (after STOP)");
    }

freebuf:
    exit_step("7 FreeScreenBuffer x3 / p96FreeBitMap / DeleteMsgPort");
    if(sb[0]) FreeScreenBuffer(my_scr,sb[0]);
    if(sb[1]) FreeScreenBuffer(my_scr,sb[1]);
    if(sb[2]) FreeScreenBuffer(my_scr,sb[2]);
    if(man_bm[1]) p96FreeBitMap(man_bm[1]);
    if(man_bm[2]) p96FreeBitMap(man_bm[2]);
    if(dbuf_port){ DeleteMsgPort(dbuf_port); dbuf_port=NULL; }
closewin:
    exit_step("8 ClearPointer / CloseWindow");
    if(win && g_blankspr) ClearPointer(win);
    if(g_blankspr){ FreeMem(g_blankspr,12L); g_blankspr=NULL; }
    if(win){ CloseWindow(win); win=NULL; }
closescr:
    exit_step("9 CloseScreen");
    if(my_scr){ CloseScreen(my_scr); my_scr=NULL; }
stop:
    /* chemins d'erreur (goto stop avant la game loop) : le blob n'a pas encore ete
       arrete ci-dessus -> on le fait ici. En sortie normale, blob_stopped=1 -> on saute
       (pas de double STOP qui timeouterait). */
    if(shared && !blob_stopped){ exit_step("6' STOP (error path)");
                sram_save(shared, fb, cfg->rom_path);
                wr32(shared,SH_P96_ENABLE,0); stop_blob(shared, board);
                printf("Core1 stopped.\n"); }
done:
    exit_step("10 statistics");
    if(g_frames>0){
        printf("\n=== TIMING VTC ===\n");
        if(shared){ active_h=rd32(shared,SH_ACTIVE_H); y_off=rd32(shared,SH_Y_OFFSET); }
        printf("region=%s->%s target=%luHz pacing=%s  active_h=%lu y_offset=%lu screen=%dx%d\n",
               region_label, pal_out?"PAL":"NTSC", target_hz, pacing_mode,
               active_h, y_off, IMG_W, IMG_H);
        printf("frames=%lu  duree=%lu.%lus  FPS=%lu.%lu\n",
               g_frames, g_el_ms/1000, (g_el_ms/100)%10, g_fps10/10, g_fps10%10);
        printf("rendu ARM moyen=%lu.%lu ms/frame   flip moyen=%lu.%lu ms/frame\n",
               g_rend10/10, g_rend10%10, g_flip10/10, g_flip10%10);
        /* 32X: pacing report */
        if(g_pace_zz)
        {   printf("main task priority during the loop: %ld%s\n", g_loop_pri, g_loop_pri_raised?" (raised; flag pri0 keeps the Shell priority)":(g_pri0?" (flag pri0)":""));
            if(g_vb_lat_max_us && efreq_main){ ULONG ms=(g_vb_lat_max_at-g_t_loop)/(efreq_main/1000UL);
                printf("worst 68k stall: wake-up %lu us late at t=%lu.%03lus (system: something held the 68k)\n", g_vb_lat_max_us, ms/1000UL, ms%1000UL); }
            printf("PLL ZZ9000: period=%lu us edges=%lu missed=%lu late(no wait)=%lu inpulse=%lu polls/edge max=%lu avg=%lu  ack polls max=%lu notseen=%lu\n",
                   efreq_main?(g_vb_period*1000UL)/(efreq_main/1000UL):0UL, g_vb_edges, g_vb_miss, g_vb_late, g_vb_inpulse,
                   g_vb_pollmax, g_vb_edges?g_vb_pollsum/g_vb_edges:0UL, g_ack_poll_max, g_ack_notseen);
            printf("  wake-up lateness: max=%lu us  <1ms=%lu <2=%lu <4=%lu <8=%lu <16=%lu >=16=%lu   frames taken by re-check=%lu%s\n",
                   g_vb_lat_max_us, g_vb_lat_hist[0], g_vb_lat_hist[1], g_vb_lat_hist[2], g_vb_lat_hist[3], g_vb_lat_hist[4], g_vb_lat_hist[5],
                   g_take_recheck, g_norecheck?" (norecheck)":"");
            printf("  period: long baseline=%lu vblanks  updates=%lu  edges rejected (uncertain)=%lu  intervals out of range=%lu  re-inits=%lu  edges by ack=%lu by gap=%lu\n",
                   g_vb_ref_n, g_vb_learn, g_vb_unc_rej, g_vb_relocks, g_vb_reinit, g_vb_by_ack, g_vb_by_gap);
            /* rc2g: when were the frames dropped */
            if(shared && diag_blob(shared)){
                ULONG i, dnow=rd32(shared,SH_PUB_DROPPED);
                static const char *pathname[7]={"?","clean","inpulse","late","byack","bygap","miss"};
                ULONG dq=(g_drop_at_quit!=0xFFFFFFFFUL)?g_drop_at_quit:dnow, eq=(g_drop_at_quit!=0xFFFFFFFFUL)?g_emu_at_quit:rd32(shared,SH_FRAME_COUNTER);
                printf("  drops: before publishing=%lu  IN GAME (publishing -> quit)=%lu for %lu frames  during quit=%lu  (first 2 s of loop: %lu)  events=%lu  settle=%lu vblanks%s\n",
                       g_drop_at_start, dq-g_drop_at_start, eq-g_emu_at_start, dnow-dq, g_drop_first2s, g_drop_ev_n,
                       g_settle_iters, g_nosettle?" (nosettle)":"");
                for(i=0;i<g_drop_ev_n;i++){
                    struct drop_ev *ev=&g_drop_ev[i];
                    printf("   DROP iter=%lu t=%lu.%03lus total=%lu ready=%lu taken=%lu emu=%lu inflight=%d edge=%s late=%lu inpulse=%lu byack=%lu recheck=%lu paula ur=%lu rs=%lu latmax=%luus\n",
                           ev->iter, ev->ms/1000UL, ev->ms%1000UL, ev->dropped, ev->ready, ev->taken, ev->emu, ev->inflight, pathname[ev->path>6?0:ev->path],
                           ev->late, ev->inpulse, ev->byack, ev->recheck, ev->pa_ur, ev->pa_rs, ev->latmax);
                }
            }
        }
        if(shared){
            ULONG emu=rd32(shared,SH_FRAME_COUNTER);
            printf("flips=%lu  PicoFrames=%lu  (every frame displayed = both equal)\n", g_flips, emu);
            /* rc2: one-block summary of what matters */
            printf("=== SUMMARY === exit=%s  output=%s\n", g_exit_reason, g_pal50_used?g_ctm_label:(pal_out?"60 Hz preset (PAL game, no50)":"59.52 Hz preset"));
            printf("  frames: emulated=%lu published imm=%lu def=%lu dropped=%lu  PAN acks=%lu  lost=%ld\n",
                   emu, diag_blob(shared)?rd32(shared,SH_PUB_IMMEDIATE):0UL, diag_blob(shared)?rd32(shared,SH_PUB_DEFERRED):0UL,
                   diag_blob(shared)?rd32(shared,SH_PUB_DROPPED):0UL, g_flips, (LONG)emu-(LONG)g_flips);
            printf("  audio: backend=%s paula underruns=%lu resyncs=%lu  ahi refills=%lu underruns=%lu errors=%lu fast=%lu first_err=%lu failed=%d\n",
                   cfg->audio==ZP_AUDIO_PAULA?"paula":(cfg->audio==ZP_AUDIO_NONE?"silence":"ahi"),
                   pa_underruns, pa_resyncs, ahi_refill_count, ahi_underrun, g_ahi_errors, g_ahi_fast, g_ahi_first_err, g_ahi_failed);
            printf("  pacing: %s  ack timeouts=%lu ackmax=%lu ms\n", pacing_mode, g_ack_timeouts, g_ack_max_ms);
        }
        if(shared){
            ULONG pf=rd32(shared,SH_PERF_PICOFRAME);
            ULONG cv=rd32(shared,SH_PERF_CONVERT);
            ULONG dc=rd32(shared,SH_PERF_DCLEAN);
            ULONG tot=pf+cv+dc; if(!tot) tot=1;
            printf("--- decoupe rendu (dernier frame, cycles CCNT @~666MHz) ---\n");
            printf("PicoFrame = %lu cyc (%lu.%lu ms, %lu%%)\n",
                   pf, pf/666667, (pf/66667)%10, (pf*100)/tot);
            printf("conversion= %lu cyc (%lu.%lu ms, %lu%%)\n",
                   cv, cv/666667, (cv/66667)%10, (cv*100)/tot);
            {
                ULONG cp=rd32(shared,SH_PERF_COPY);
                printf("copie P96 = %lu cyc (%lu.%lu ms)\n",
                       cp, cp/666667, (cp/66667)%10);
            }
            printf("dcache_cln= %lu cyc (%lu.%lu ms, %lu%%)\n",
                   dc, dc/666667, (dc/66667)%10, (dc*100)/tot);
            {
                ULONG wp=rd32(shared,SH_PERF_WCYC_P96);
                ULONG wf=rd32(shared,SH_PERF_WCYC_FB3B);
                ULONG dp=rd32(shared,SH_PERF_DESC_P96);
                ULONG df=rd32(shared,SH_PERF_DESC_FB3B);
                ULONG dpre=rd32(shared,SH_PERF_DESC_P96PRE);
                printf("--- benchmark ecriture 76800 mots (frame 1) ---\n");
                printf("vers P96  = %lu cyc (%lu.%lu ms)  desc=0x%08lx\n",
                       wp, wp/666667, (wp/66667)%10, dp);
                printf("vers FB3B = %lu cyc (%lu.%lu ms)  desc=0x%08lx (ref cachee)\n",
                       wf, wf/666667, (wf/66667)%10, df);
                printf("desc P96 AVANT remap=0x%08lx  APRES=0x%08lx\n", dpre, dp);
                printf("(desc: TEX[14:12] C[3] B[2] ; WB/WA=TEX001 C1 B1)\n");
            }
        }
        printf("\n=== NTSC_TIMING_DIAG (session) ===\n");
        if(shared){
            ULONG pmin=rd32(shared,SH_DIAG_PF_MIN), pmax=rd32(shared,SH_DIAG_PF_MAX);
            ULONG sus=rd32(shared,SH_DIAG_PF_SUM_US), dfrm=rd32(shared,SH_DIAG_FRAMES);
            ULONG g12=rd32(shared,SH_DIAG_N_GT12), g14=rd32(shared,SH_DIAG_N_GT14);
            ULONG g67=rd32(shared,SH_DIAG_N_GT1667);
            ULONG cvmax=rd32(shared,SH_DIAG_CONV_MAX), clmax=rd32(shared,SH_DIAG_CLEAN_MAX);
            ULONG cvmin=rd32(shared,SH_DIAG_CONV_MIN), cvsus=rd32(shared,SH_DIAG_CONV_SUM_US);
            ULONG avgus=dfrm?sus/dfrm:0;
            ULONG cvavg=dfrm?cvsus/dfrm:0;
            printf("PicoFrame blob (%lu frames)  min=%lu.%lu  moy=%lu.%lu  max=%lu.%lu ms\n",
                   dfrm, pmin/666667,(pmin/66667)%10, avgus/1000,(avgus/100)%10,
                   pmax/666667,(pmax/66667)%10);
            printf("  frames  >12ms=%lu   >14ms=%lu   >16.67ms=%lu\n", g12, g14, g67);
            printf("  convert min=%lu.%lu  moy=%lu.%lu  max=%lu.%lu ms   dclean max=%lu.%lu ms\n",
                   cvmin/666667,(cvmin/66667)%10, cvavg/1000,(cvavg/100)%10,
                   cvmax/666667,(cvmax/66667)%10, clmax/666667,(clmax/66667)%10);
        }
        if(g_efreq>1000){
            ULONG efk=g_efreq/1000;
            printf("render 68k (r1-r0)  min=%lu.%lu  max=%lu.%lu ms   [>12=%lu >14=%lu >16.67=%lu]\n",
                   g_rmin/efk,((g_rmin*10UL)/efk)%10, g_rmax/efk,((g_rmax*10UL)/efk)%10,
                   g_r12, g_r14, g_r67);
            printf("frame PAN-to-PAN    min=%lu.%lu  max=%lu.%lu ms   missed_vblank=%lu\n",
                   g_fmin/efk,((g_fmin*10UL)/efk)%10, g_fmax/efk,((g_fmax*10UL)/efk)%10,
                   g_missed);
            { ULONG vb10=g_frames?(g_vbsum*10UL)/g_frames:0;
              printf("vblanks attendus/PAN  moy=%lu.%lu  max=%lu\n", vb10/10, vb10%10, g_vbmax); }
            printf("--- lecture : missed_vblank~0 => NTSC verrouille. Comparer convert moy/max\n");
            printf("    avec/sans asm_memory pour voir le gain sur les pics.\n");
        }
    }
    exit_step("11 timer / libraries / ROM and blob buffers");
    if(have_timer) timer_close();
    sleep_close();   /* 32X */
    if(fh) Close(fh);
    if(P96Base) CloseLibrary(P96Base);
    if(LowLevelBase) CloseLibrary(LowLevelBase);
    if(rombuf) FreeMem(rombuf,(ULONG)rom_size+ROM_PAD);
    if(blob_alloced && blobbuf) FreeMem(blobbuf,(ULONG)blob_size);   /* pas de free si blob embarque */
    stack_report("exit end");
    printf("Done (rc=%d) exit=%s.\n",rc,g_exit_reason);
    return rc;
}

/* ===================== parse CLI -> ZP_Config (backward compatible) ===================== */
/* fix 31.10: case-insensitive keyword compare (AmigaDOS habits) */
static int kw(const char *a, const char *b)
{
    while(*a && *b){
        char x=*a, y=*b;
        if(x>='A'&&x<='Z') x=(char)(x+32);
        if(y>='A'&&y<='Z') y=(char)(y+32);
        if(x!=y) return 0;
        a++; b++;
    }
    return *a==0 && *b==0;
}

static int parse_cli(int argc, char **argv, ZP_Config *cfg)
{
    int ai, blob_set=0;
    if(argc<2){
        printf("Usage: %s <rom> [region] [flags]   (no argument = graphical interface)\n", argv[0]);
        printf("  region and flags can be given in any order, e.g. %s Doom.32x usa ahi\n", argv[0]);
        printf("  blob   : optional external Core1 file (any word that is not a keyword);\n");
        printf("           '-' or nothing = the blob embedded in the program (normal use)\n");
        printf("  region : auto|usa|jap|eur|jappal   (default auto)\n");
        printf("  flags  : ahi paula silence  kbd cd32 db9 usb port=N 2p  no50 no60  osd\n");
        printf("  kbd    : arrows, keypad 1/2/3 = A/B/C, keypad 4/5/6 = X/Y/Z, keypad 0 = MODE, Enter = Start\n");
        printf("  Del    : console reset    Shift+Esc : quit    F1-F5 : load state, Shift+F1-F5 : save\n");
        printf("           (a savestate is guaranteed only with the same region and the same audio output)\n");
        printf("  no50   : PAL games on the 60 Hz screen mode   no60 : NTSC games on the 59.5 Hz preset\n");
        printf("  osd    : blob text line on   diagnostics: pri0 norecheck nosettle tof gate inputlog\n");
        return 0;
    }
    strncpy(cfg->rom_path,  argv[1], 255); cfg->rom_path[255]=0;
    strcpy(cfg->blob_path, "zz32x_v1.2.bin");   /* v1.2: missing -> embedded blob */
    /* fix 31.10: region and flags are keywords recognised at any position.
       Before, argv[2] was always the blob and argv[3] the region, so
       "Doom.32x usa ahi" took "usa" as the blob name and ran in AUTO. */
    for(ai=2; ai<argc; ai++){
        const char *r=argv[ai];
        if(kw(r,"eur")||kw(r,"jappal")||kw(r,"pal")) cfg->region=ZP_REGION_EUR_PAL;
        else if(kw(r,"usa")||kw(r,"ntsc")) cfg->region=ZP_REGION_USA_NTSC;
        else if(kw(r,"jap")) cfg->region=ZP_REGION_JAPAN_NTSC;
        else if(kw(r,"auto")) cfg->region=ZP_REGION_AUTO;
        else if(kw(r,"-")) { /* explicit: embedded blob */ }
        else if(!strcmp(argv[ai],"inputlog")) g_input_log=1;
        else if(!strcmp(argv[ai],"kbd")) cfg->input=ZP_INPUT_KBD;
        else if(!strcmp(argv[ai],"cd32")||!strcmp(argv[ai],"joy")) cfg->input=ZP_INPUT_CD32_LOWLEVEL;
        else if(!strcmp(argv[ai],"db9")) cfg->input=ZP_INPUT_HW_DB9;
        else if(!strcmp(argv[ai],"usb")) cfg->input=ZP_INPUT_USB_HID;
        else if(!strncmp(argv[ai],"port=",5)) cfg->port=(argv[ai][5]=='0')?0:1;
        else if(!strcmp(argv[ai],"silence")) cfg->audio=ZP_AUDIO_NONE;
        else if(!strcmp(argv[ai],"paula")) cfg->audio=ZP_AUDIO_PAULA;
        else if(!strcmp(argv[ai],"ahi")) cfg->audio=ZP_AUDIO_AHI;
        else if(!strcmp(argv[ai],"2p")||!strcmp(argv[ai],"players2")) cfg->players=2;
        else if(!strcmp(argv[ai],"cd32-2")||!strcmp(argv[ai],"joy2")) cfg->input2=ZP_INPUT_CD32_LOWLEVEL;
        else if(!strcmp(argv[ai],"db9-2")) cfg->input2=ZP_INPUT_HW_DB9;
        else if(!strncmp(argv[ai],"port2=",6)) cfg->port2=(argv[ai][6]=='1')?1:0;
        else if(!strcmp(argv[ai],"sine")||!strcmp(argv[ai],"sinus")) g_sine=1;
        else if(!strcmp(argv[ai],"hq44")) g_au_hz=44100;
        else if(!strcmp(argv[ai],"hz22")) g_au_hz=22050;
        else if(!strcmp(argv[ai],"osd")) g_osd=1;              /* rc2: blob OSD line on (off by default) */
        else if(!strcmp(argv[ai],"no50")) g_no50=1;            /* rc2: keep the 60 Hz output for PAL games */
        else if(!strcmp(argv[ai],"no60")) g_no60=1;            /* rc2c: keep the 59.52 Hz preset for NTSC games */
        else if(!strcmp(argv[ai],"pri0")) g_pri0=1;            /* rc2c: main task stays at the Shell priority */
        else if(!strcmp(argv[ai],"norecheck")) g_norecheck=1;  /* rc2c: no FRAME_READY re-check inside the period */
        else if(!strcmp(argv[ai],"nosettle")) g_nosettle=1;    /* rc2h: publish from the first iteration */
        else if(!strcmp(argv[ai],"tof")) g_force_tof=1;         /* 32X: 1.1 WaitTOF pacing */
        else if(!strcmp(argv[ai],"gate")) g_gate=1;             /* 32X: 1.1 drawing gate in the blob */
        else if(!blob_set){
            /* first word that is not a keyword: external blob file (as before) */
            strncpy(cfg->blob_path, argv[ai], 255); cfg->blob_path[255]=0; blob_set=1;
            g_blob_explicit=1;   /* v1.2 RC4: a missing file is an error, not the embedded blob */
            printf("(argument '%s' taken as Core1 blob file)\n", argv[ai]);
        }
        else printf("(unknown flag '%s' ignored)\n", argv[ai]);
    }
    return 1;
}

/* ===================== config par defaut + PROGDIR:ZZPicoDrive.cfg ===================== */
static void zp_config_defaults(ZP_Config *cfg)
{
    cfg->rom_path[0]=0;
    strcpy(cfg->blob_path, "zz32x_v1.2.bin");   /* v1.2: used only if this file exists, else the embedded blob */
    cfg->region=ZP_REGION_AUTO;
    cfg->audio =ZP_AUDIO_AHI;
    cfg->input =ZP_INPUT_CD32_LOWLEVEL;
    cfg->port  =1;
    cfg->players=1;
    cfg->input2=ZP_INPUT_CD32_LOWLEVEL;
    cfg->port2 =0;                       /* joueur 2 sur l'autre port par defaut */
    cfg->show_fps=0;
    cfg->rom_dir[0]=0;                   /* v1.2 update */
    cfg->last_dir[0]=0;
}
/* v1.2 update: does this drawer / file exist right now?  The "insert volume"
   requester is suppressed (pr_WindowPtr = -1): an unmounted volume simply
   answers no, and the saved setting is kept for the next time. */
static int path_available(const char *d)
{
    struct Process *me; APTR old; BPTR l;
    if(!d || !d[0]) return 0;
    me=(struct Process*)FindTask(NULL); old=me->pr_WindowPtr; me->pr_WindowPtr=(APTR)-1L;
    l=Lock((STRPTR)d, ACCESS_READ);
    me->pr_WindowPtr=old;
    if(!l) return 0;
    UnLock(l); return 1;
}
/* v1.2 update: drawer the ROM requester opens in: default drawer, else the
   last one, else the drawer of the last ROM, else none (ASL default). */
static const char *rom_start_drawer(const ZP_Config *cfg, char *tmp)
{
    if(path_available(cfg->rom_dir))  return cfg->rom_dir;
    if(path_available(cfg->last_dir)) return cfg->last_dir;
    if(cfg->rom_path[0]){
        int n=(int)((UBYTE*)PathPart((STRPTR)cfg->rom_path)-(UBYTE*)cfg->rom_path);
        if(n>0 && n<256){ memcpy(tmp,cfg->rom_path,(size_t)n); tmp[n]=0;
                          if(path_available(tmp)) return tmp; }
    }
    return NULL;
}
static int cfg_app(char *b, int p, const char *s){ while(*s) b[p++]=*s++; return p; }
static void zp_config_save(const ZP_Config *cfg, const char *path)
{
    BPTR fh; char b[1400]; int p=0;
    fh=Open((STRPTR)path, MODE_NEWFILE); if(!fh) return;
    p=cfg_app(b,p,"rom=");  p=cfg_app(b,p,cfg->rom_path);  b[p++]='\n';
    p=cfg_app(b,p,"region=");b[p++]=(char)('0'+cfg->region);  b[p++]='\n';
    p=cfg_app(b,p,"audio="); b[p++]=(char)('0'+cfg->audio);   b[p++]='\n';
    p=cfg_app(b,p,"input="); b[p++]=(char)('0'+cfg->input);   b[p++]='\n';
    p=cfg_app(b,p,"port=");  b[p++]=(char)('0'+cfg->port);    b[p++]='\n';
    p=cfg_app(b,p,"players=");b[p++]=(char)('0'+cfg->players); b[p++]='\n';
    p=cfg_app(b,p,"input2="); b[p++]=(char)('0'+cfg->input2);  b[p++]='\n';
    p=cfg_app(b,p,"port2=");  b[p++]=(char)('0'+cfg->port2);   b[p++]='\n';
    p=cfg_app(b,p,"fps=");   b[p++]=(char)('0'+cfg->show_fps);b[p++]='\n';
    p=cfg_app(b,p,"romdir="); p=cfg_app(b,p,cfg->rom_dir);  b[p++]='\n';   /* v1.2 update */
    p=cfg_app(b,p,"lastdir=");p=cfg_app(b,p,cfg->last_dir); b[p++]='\n';
    Write(fh, b, (LONG)p); Close(fh);
}
static void zp_config_load(ZP_Config *cfg, const char *path)
{
    BPTR fh; char buf[2000]; LONG n, i, ls;
    fh=Open((STRPTR)path, MODE_OLDFILE); if(!fh) return;
    n=Read(fh, buf, sizeof(buf)-1); Close(fh);
    if(n<=0) return; buf[n]=0; ls=0;
    for(i=0;i<=n;i++){
        if(buf[i]=='\n' || buf[i]==0){
            char *line=buf+ls, *eq=0, *k, *v; int j;
            buf[i]=0;
            for(j=0;line[j];j++) if(line[j]=='='){ eq=line+j; break; }
            if(eq){ *eq=0; k=line; v=eq+1;
                if(!strcmp(k,"rom")){ strncpy(cfg->rom_path,v,255); cfg->rom_path[255]=0; }
                /* blob : NON charge depuis config (toujours le defaut, evite un chemin obsolete) */
                else if(!strcmp(k,"region")){ int x=v[0]-'0'; cfg->region=(x>=0&&x<=3)?x:0; }
                else if(!strcmp(k,"audio")) { int x=v[0]-'0'; cfg->audio =(x>=0&&x<=2)?x:0; }
                else if(!strcmp(k,"input")) { int x=v[0]-'0'; cfg->input =(x>=0&&x<=3)?x:1; }
                else if(!strcmp(k,"port"))    cfg->port  =(v[0]=='0')?0:1;
                else if(!strcmp(k,"players")) cfg->players=(v[0]=='2')?2:1;
                else if(!strcmp(k,"input2")){ int x=v[0]-'0'; cfg->input2=(x>=0&&x<=3)?x:1; }
                else if(!strcmp(k,"port2"))   cfg->port2 =(v[0]=='1')?1:0;
                else if(!strcmp(k,"fps"))    cfg->show_fps=(v[0]=='1')?1:0;
                else if(!strcmp(k,"romdir")){ strncpy(cfg->rom_dir,v,255);  cfg->rom_dir[255]=0; }   /* v1.2 update */
                else if(!strcmp(k,"lastdir")){strncpy(cfg->last_dir,v,255); cfg->last_dir[255]=0; }
            }
            ls=i+1;
        }
    }
}

/* ===================== GUI de lancement (Intuition + GadTools + ASL) ===================== */
static const char *gui_region[] = {"Auto","Europe PAL 50Hz","USA NTSC 60Hz","Japan NTSC 60Hz",NULL};
static const char *gui_audio[]  = {"AHI stereo","Paula DMA","None",NULL};
static const char *gui_input[]  = {"Keyboard","CD32 / Lowlevel","DB9 hardware",NULL};
static const char *gui_port[]   = {"Port 0 (mouse)","Port 1 (joystick)",NULL};
static const char *gui_players[]= {"1 Player","2 Players",NULL};
/* choix combine par joueur : mode + port en une seule liste */
static const char *gui_p1[] = {"Keyboard","Joystick DB9 port 0","Joystick DB9 port 1",
                               "Pad CD32/USB port 0","Pad CD32/USB port 1",NULL};
static const char *gui_p2[] = {"None (1 player)","Joystick DB9 port 0","Joystick DB9 port 1",
                               "Pad CD32/USB port 0","Pad CD32/USB port 1",NULL};
/* choix P1 (0..4) -> (input,port) et retour */
static int p1_to_sel(int input, int port){
    if(input==ZP_INPUT_KBD) return 0;
    if(input==ZP_INPUT_HW_DB9) return (port==0)?1:2;
    return (port==0)?3:4;   /* CD32/lowlevel */
}
static void sel_to_p1(int sel, int *input, int *port){
    switch(sel){
        case 0: *input=ZP_INPUT_KBD; break;
        case 1: *input=ZP_INPUT_HW_DB9;        *port=0; break;
        case 2: *input=ZP_INPUT_HW_DB9;        *port=1; break;
        case 3: *input=ZP_INPUT_CD32_LOWLEVEL; *port=0; break;
        case 4: *input=ZP_INPUT_CD32_LOWLEVEL; *port=1; break;
    }
}
/* choix P2 (0=None..5) -> (players,input2,port2) et retour */
static int p2_to_sel(int players, int input2, int port2){
    if(players!=2) return 0;                 /* None */
    if(input2==ZP_INPUT_HW_DB9) return (port2==0)?1:2;
    return (port2==0)?3:4;                    /* CD32/lowlevel (clavier force en manette) */
}
static void sel_to_p2(int sel, int *players, int *input2, int *port2){
    if(sel==0){ *players=1; return; }
    *players=2;
    switch(sel){
        case 1: *input2=ZP_INPUT_HW_DB9;        *port2=0; break;
        case 2: *input2=ZP_INPUT_HW_DB9;        *port2=1; break;
        case 3: *input2=ZP_INPUT_CD32_LOWLEVEL; *port2=0; break;
        case 4: *input2=ZP_INPUT_CD32_LOWLEVEL; *port2=1; break;
    }
}
/* conflit : 2 manettes (non-clavier) sur le meme port physique -> interdit.
   2 claviers = OK (touches differentes). Clavier + manette = OK. */
static int controllers_conflict(const ZP_Config *cfg){
    if(cfg->players!=2) return 0;
    if(cfg->input==ZP_INPUT_KBD || cfg->input2==ZP_INPUT_KBD) return 0;
    return (cfg->port==cfg->port2);
}

/* ---- barre de menu GUI (clic droit) : About + Help ---- */
static void gui_req(struct Window *w, const char *title, const char *text){
    struct EasyStruct es;
    es.es_StructSize=sizeof(struct EasyStruct); es.es_Flags=0;
    es.es_Title=(STRPTR)title;
    es.es_TextFormat=(STRPTR)text;
    es.es_GadgetFormat=(STRPTR)"OK";
    EasyRequest(w,&es,NULL,0);
}
static const char zz32x_version[] __attribute__((used)) = "$VER: ZZPicoDrive32X 1.2.1 (01.11.2026)";
static const char *TXT_ABOUT =
    "ZZPicoDrive32X brings Sega 32X (and Mega Drive) emulation to\n"
    "classic Amigas equipped with an MNT ZZ9000. The emulation\n"
    "runs on the ZZ9000's ARM coprocessor and talks to the 68k\n"
    "over Zorro through XACP-compatible firmware, keeping the\n"
    "host CPU almost free.\n\n"
    "v1.2: every frame displayed, real 50 Hz output for PAL\n"
    "games and 59.98 Hz for NTSC games, Paula and AHI.\n"
    "Port by Xanxi, 2026.  Emulation core: PicoDrive (notaz,\n"
    "irixxxx) with its SH-2 recompiler.";
static const char *TXT_CONTROLS =
    "CONTROLS\n\n"
    "Player 1: keyboard OR a controller on a chosen port.\n"
    "Player 2: a controller only, on a different port.\n\n"
    "Keyboard (Player 1):\n"
    "  Arrows = D-pad    Numpad 1/2/3 = A/B/C    Enter = Start\n"
    "  Numpad 4/5/6 = X/Y/Z   Numpad 0 = Mode   Del = Reset\n\n"
    "Controller types:\n"
    "  DB9 hardware = joystick/pad on the real port (2 buttons)\n"
    "  CD32 / USB   = CD32 pad, or USB pad via Poseidon:\n"
    "    blue/red/green = A/B/C, left/right shoulder = X/Y,\n"
    "    yellow = Z, play = Start\n\n"
    "Start fallback (for a pad with no Start button):\n"
    "  Player 1 = Enter     Player 2 = Tab\n"
    "These keys always work, even with a controller.";
static const char *TXT_OPTIONS =
    "OPTIONS\n\n"
    "ROM dir: 'Set ROM drawer...' chooses the drawer the ROM\n"
    "requester always opens in; 'Clear' goes back to the last\n"
    "drawer used.  Saved at once.  From the Shell, a ROM name\n"
    "alone is also looked for in this drawer.\n\n"
    "Region:  Auto / Europe PAL / USA NTSC / Japan NTSC\n\n"
    "Audio:\n"
    "  AHI = works on any audio hardware (recommended).\n"
    "  Paula DMA = plays directly on Paula, zero CPU mixing.\n"
    "    Great on games that keep full speed; if a heavy game\n"
    "    crackles, switch back to AHI.\n"
    "  None = silent.\n\n"
    "Players: set Player 2 to a controller for 2-player games\n"
    "(each player on a different port).";
static const char *TXT_HOTKEYS =
    "IN-GAME HOTKEYS\n\n"
    "Shift + Esc        quit\n"
    "F1 - F5            load savestate (slots 1-5)\n"
    "Shift + F1 - F5    save savestate (slots 1-5)\n"
    "Enter              Start (Player 1)\n"
    "Tab                Start (Player 2)\n\n"
    "A savestate is guaranteed only when it is loaded with\n"
    "the same region (USA/Japan/Europe) and the same audio\n"
    "output (AHI/Paula) it was saved with.  A state saved\n"
    "in the other region (PAL/NTSC) is refused.";
static struct NewMenu gui_menu[] = {
    { NM_TITLE, (STRPTR)"Project",     NULL, 0, 0L, NULL },
    { NM_ITEM,  (STRPTR)"About...",    NULL, 0, 0L, (APTR)1L },
    { NM_ITEM,  (STRPTR)NM_BARLABEL,   NULL, 0, 0L, NULL },
    { NM_ITEM,  (STRPTR)"Quit",        NULL, 0, 0L, (APTR)2L },
    { NM_TITLE, (STRPTR)"Help",        NULL, 0, 0L, NULL },
    { NM_ITEM,  (STRPTR)"Controls...", NULL, 0, 0L, (APTR)3L },
    { NM_ITEM,  (STRPTR)"Options...",  NULL, 0, 0L, (APTR)4L },
    { NM_ITEM,  (STRPTR)"Hotkeys...",  NULL, 0, 0L, (APTR)5L },
    { NM_END,   NULL,                  NULL, 0, 0L, NULL },
};

static int gui_get_config(ZP_Config *cfg)
{
    struct Screen *pub=NULL; struct Window *win=NULL; struct Gadget *glist=NULL, *gc;
    struct Menu *menu=NULL;
    APTR vi=NULL; struct NewGadget ng; struct TextAttr ta;
    struct Gadget *g_rom=NULL,*g_region=NULL,*g_audio=NULL,*g_input=NULL;
    struct Gadget *g_input2=NULL, *g_romdir=NULL;
    int done=0, result=0;
    ta.ta_Name=(STRPTR)"topaz.font"; ta.ta_YSize=8; ta.ta_Style=0; ta.ta_Flags=0;

    GadToolsBase =OpenLibrary("gadtools.library",39L);
    AslBase      =OpenLibrary("asl.library",38L);
    if(!GadToolsBase||!AslBase){
        if(AslBase)CloseLibrary(AslBase); if(GadToolsBase)CloseLibrary(GadToolsBase);
        GadToolsBase=NULL;AslBase=NULL;
        printf("GUI indisponible -> utilise la CLI\n"); return -1;
    }
    pub=LockPubScreen(NULL);
    if(!pub){ result=-1; goto guiclean; }
    vi=GetVisualInfo(pub, TAG_END);
    if(!vi){ result=-1; goto guiclean; }
    gc=CreateContext(&glist);
    if(!gc){ result=-1; goto guiclean; }

    { int top = (int)pub->WBorTop + (pub->Font ? (int)pub->Font->ta_YSize : 8) + 3;  /* sous la barre de titre */
      int L=96;   /* colonne des gadgets (labels dessines a gauche) */
      ng.ng_TextAttr=&ta; ng.ng_VisualInfo=vi; ng.ng_Flags=0;
      /* ROM (texte affiche) */
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+6;  ng.ng_Width=300; ng.ng_Height=14;
      ng.ng_GadgetText=(STRPTR)"ROM:"; ng.ng_GadgetID=1;
      g_rom=CreateGadget(TEXT_KIND, gc, &ng, GTTX_Text,(ULONG)(cfg->rom_path[0]?cfg->rom_path:"(aucune)"),
                         GTTX_Border,TRUE, TAG_END); gc=g_rom;
      /* Browse */
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+24; ng.ng_Width=100; ng.ng_Height=14;
      ng.ng_GadgetText=(STRPTR)"Browse..."; ng.ng_GadgetID=2;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      /* v1.2 update: default ROM drawer: choose / clear, and its current value */
      ng.ng_LeftEdge=L+110; ng.ng_Width=150;
      ng.ng_GadgetText=(STRPTR)"Set ROM drawer..."; ng.ng_GadgetID=10;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      ng.ng_LeftEdge=L+270; ng.ng_Width=56;
      ng.ng_GadgetText=(STRPTR)"Clear"; ng.ng_GadgetID=11;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+42; ng.ng_Width=300; ng.ng_Height=14;
      ng.ng_GadgetText=(STRPTR)"ROM dir:"; ng.ng_GadgetID=12;
      g_romdir=CreateGadget(TEXT_KIND, gc, &ng, GTTX_Text,(ULONG)(cfg->rom_dir[0]?cfg->rom_dir:"(none: last drawer used)"),
                            GTTX_Border,TRUE, TAG_END); gc=g_romdir;
      /* Region */
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+64; ng.ng_Width=210; ng.ng_Height=14;
      ng.ng_GadgetText=(STRPTR)"Region:"; ng.ng_GadgetID=3;
      g_region=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_region, GTCY_Active,cfg->region, TAG_END); gc=g_region;
      /* Audio */
      ng.ng_TopEdge=top+82; ng.ng_GadgetText=(STRPTR)"Audio:"; ng.ng_GadgetID=4;
      g_audio=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_audio, GTCY_Active,cfg->audio, TAG_END); gc=g_audio;
      /* Player 1 (mode + port combines) */
      ng.ng_TopEdge=top+100; ng.ng_GadgetText=(STRPTR)"Player 1:"; ng.ng_GadgetID=5;
      g_input=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_p1,
                           GTCY_Active,p1_to_sel(cfg->input,cfg->port), TAG_END); gc=g_input;
      /* Player 2 (None = 1 joueur) */
      ng.ng_TopEdge=top+118; ng.ng_GadgetText=(STRPTR)"Player 2:"; ng.ng_GadgetID=6;
      g_input2=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_p2,
                            GTCY_Active,p2_to_sel(cfg->players,cfg->input2,cfg->port2), TAG_END); gc=g_input2;
      /* Start / Quit */
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+144; ng.ng_Width=100; ng.ng_Height=16;
      ng.ng_GadgetText=(STRPTR)"Start"; ng.ng_GadgetID=7;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      ng.ng_LeftEdge=L+120; ng.ng_GadgetText=(STRPTR)"Quit"; ng.ng_GadgetID=8;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      if(!gc){ result=-1; goto guiclean; }

      win=(struct Window*)OpenWindowTags(NULL,
        WA_Title,(ULONG)"ZZPicoDrive32X v1.2.1 - Sega 32X edition",
        WA_Left,60, WA_Top,40, WA_Width,460, WA_Height,top+172,
        WA_Gadgets,(ULONG)glist, WA_DragBar,TRUE, WA_DepthGadget,TRUE,
        WA_CloseGadget,TRUE, WA_Activate,TRUE, WA_SimpleRefresh,TRUE,
        WA_IDCMP,IDCMP_CLOSEWINDOW|BUTTONIDCMP|CYCLEIDCMP|IDCMP_REFRESHWINDOW|IDCMP_MENUPICK,
        WA_PubScreen,(ULONG)pub, TAG_END);
    }
    if(!win){ result=-1; goto guiclean; }
    { struct Menu *m=CreateMenus(gui_menu, TAG_END);   /* barre de menu About/Help */
      if(m){ if(LayoutMenus(m, vi, TAG_END)){ SetMenuStrip(win, m); menu=m; } else FreeMenus(m); } }
    GT_RefreshWindow(win, NULL);

    while(!done){
        struct IntuiMessage *msg;
        WaitPort(win->UserPort);
        while((msg=GT_GetIMsg(win->UserPort))){
            ULONG cl=msg->Class, code=msg->Code; APTR ia=msg->IAddress;
            GT_ReplyIMsg(msg);
            if(cl==IDCMP_CLOSEWINDOW){ done=1; result=0; }
            else if(cl==IDCMP_REFRESHWINDOW){ GT_BeginRefresh(win); GT_EndRefresh(win,TRUE); }
            else if(cl==IDCMP_GADGETUP){
                struct Gadget *g=(struct Gadget*)ia;
                switch(g->GadgetID){
                    case 3: cfg->region=(int)code; break;
                    case 4: cfg->audio =(int)code; break;
                    case 5: sel_to_p1((int)code, &cfg->input, &cfg->port); break;
                    case 6: sel_to_p2((int)code, &cfg->players, &cfg->input2, &cfg->port2); break;
                    case 2: {   /* Browse ROM via ASL (v1.2 update: opens in the default / last drawer) */
                        struct FileRequester *fr=(struct FileRequester*)AllocAslRequest(ASL_FileRequest, NULL);
                        if(fr){
                            static char tmpd[256];
                            const char *sd=rom_start_drawer(cfg, tmpd);
                            if(AslRequestTags(fr, ASLFR_TitleText,(ULONG)"Choose a 32X or Mega Drive ROM",
                                              ASLFR_Window,(ULONG)win,
                                              sd?ASLFR_InitialDrawer:TAG_IGNORE,(ULONG)sd,
                                              TAG_END)){
                                strncpy(cfg->rom_path, fr->fr_Drawer, 255); cfg->rom_path[255]=0;
                                AddPart((STRPTR)cfg->rom_path,(STRPTR)fr->fr_File, 256);
                                strncpy(cfg->last_dir, fr->fr_Drawer, 255); cfg->last_dir[255]=0;
                                GT_SetGadgetAttrs(g_rom, win, NULL, GTTX_Text,(ULONG)cfg->rom_path, TAG_END);
                            }
                            FreeAslRequest(fr);
                        }
                    } break;
                    case 10: {  /* v1.2 update: choose the default ROM drawer (saved at once) */
                        struct FileRequester *fr=(struct FileRequester*)AllocAslRequest(ASL_FileRequest, NULL);
                        if(fr){
                            static char tmpd2[256];
                            const char *sd=rom_start_drawer(cfg, tmpd2);
                            if(AslRequestTags(fr, ASLFR_TitleText,(ULONG)"Choose the default ROM drawer",
                                              ASLFR_Window,(ULONG)win, ASLFR_DrawersOnly,TRUE,
                                              sd?ASLFR_InitialDrawer:TAG_IGNORE,(ULONG)sd,
                                              TAG_END) && fr->fr_Drawer && fr->fr_Drawer[0]){
                                strncpy(cfg->rom_dir, fr->fr_Drawer, 255); cfg->rom_dir[255]=0;
                                GT_SetGadgetAttrs(g_romdir, win, NULL, GTTX_Text,(ULONG)cfg->rom_dir, TAG_END);
                                zp_config_save(cfg, "PROGDIR:ZZPicoDrive32X.cfg");
                            }
                            FreeAslRequest(fr);
                        }
                    } break;
                    case 11:    /* v1.2 update: clear the default ROM drawer (saved at once) */
                        cfg->rom_dir[0]=0;
                        GT_SetGadgetAttrs(g_romdir, win, NULL, GTTX_Text,(ULONG)"(none: last drawer used)", TAG_END);
                        zp_config_save(cfg, "PROGDIR:ZZPicoDrive32X.cfg");
                        break;
                    case 7:   /* Start */
                        if(!cfg->rom_path[0]){ DisplayBeep(pub); break; }
                        if(controllers_conflict(cfg)){
                            struct EasyStruct es;
                            es.es_StructSize=sizeof(struct EasyStruct); es.es_Flags=0;
                            es.es_Title=(STRPTR)"ZZPicoDrive32X";
                            es.es_TextFormat=(STRPTR)"Players 1 and 2 use the same port.\nUse different ports for two controllers\n(or set one player to Keyboard).";
                            es.es_GadgetFormat=(STRPTR)"OK";
                            EasyRequest(win,&es,NULL,0); DisplayBeep(pub); break;
                        }
                        done=1; result=1;
                        break;
                    case 8: done=1; result=0; break;   /* Quit */
                }
            }
            else if(cl==IDCMP_MENUPICK){
                UWORD mn=(UWORD)code;
                while(menu && mn!=MENUNULL){
                    struct MenuItem *it=ItemAddress(menu, mn);
                    if(!it) break;
                    { ULONG ud=(ULONG)GTMENUITEM_USERDATA(it);
                      if(ud==1L)       gui_req(win,"About ZZPicoDrive32X",TXT_ABOUT);
                      else if(ud==2L){ done=1; result=0; }
                      else if(ud==3L)  gui_req(win,"Controls",TXT_CONTROLS);
                      else if(ud==4L)  gui_req(win,"Options",TXT_OPTIONS);
                      else if(ud==5L)  gui_req(win,"Hotkeys",TXT_HOTKEYS); }
                    mn=it->NextSelect;
                }
            }
        }
    }

guiclean:
    if(win && menu) ClearMenuStrip(win);
    if(menu) FreeMenus(menu);
    if(win) CloseWindow(win);
    if(glist) FreeGadgets(glist);
    if(vi) FreeVisualInfo(vi);
    if(pub) UnlockPubScreen(NULL, pub);
    if(AslBase){ CloseLibrary(AslBase); AslBase=NULL; }
    if(GadToolsBase){ CloseLibrary(GadToolsBase); GadToolsBase=NULL; }
    return result;
}

/* ===================== main : CLI ou GUI -> run_picodrive ===================== */
/* 32X: run the program on its own 100 KiB stack.  exec StackSwap() (V37+) moves
   the task to the new stack and updates tc_SPLower/Upper; main_body() runs there
   and the original stack is restored before returning to the startup code. */
static int main_body(int argc, char **argv);
static int    g_argc; static char **g_argv; static int g_main_rc;
static struct StackSwapStruct g_sss;
int main(int argc, char **argv)
{
    APTR stk;
    g_argc=argc; g_argv=argv; g_main_rc=20;
    stk=AllocMem(ZZ32X_STACK_SIZE, MEMF_PUBLIC);
    if(!stk) return main_body(argc, argv);        /* no memory: run on the Shell stack as 1.1 did */
    g_sss.stk_Lower=stk;
    g_sss.stk_Upper=(ULONG)stk+ZZ32X_STACK_SIZE;
    g_sss.stk_Pointer=(APTR)g_sss.stk_Upper;
    StackSwap(&g_sss);
    g_main_rc=main_body(g_argc, g_argv);
    StackSwap(&g_sss);
    FreeMem(stk, ZZ32X_STACK_SIZE);
    return g_main_rc;
}
static int main_body(int argc, char **argv)
{
    ZP_Config cfg;
    int rc;
    zp_config_defaults(&cfg);
    zp_config_load(&cfg, "PROGDIR:ZZPicoDrive32X.cfg");
    /* intuition/graphics ouverts UNE seule fois ici (ni la GUI ni run_picodrive ne les ferment) */
    IntuitionBase=(struct IntuitionBase*)OpenLibrary("intuition.library",39L);
    GfxBase_     =OpenLibrary("graphics.library",39L);
    if(!IntuitionBase||!GfxBase_){
        if(GfxBase_) CloseLibrary(GfxBase_);
        if(IntuitionBase) CloseLibrary((struct Library*)IntuitionBase);
        return 20;
    }
    if(argc>1){
        g_quiet=0;                       /* CLI : sortie debug active */
        if(!parse_cli(argc, argv, &cfg)){ rc=20; goto mainclean; }
        /* v1.2 update: a bare ROM name that is not in the current directory is
           looked for in the default ROM drawer ("ZZPicoDrive32X Doom.32x usa") */
        if(!path_available(cfg.rom_path) && cfg.rom_dir[0] && !strchr(cfg.rom_path,':') && !strchr(cfg.rom_path,'/')){
            char t[256];
            strncpy(t, cfg.rom_dir, 255); t[255]=0;
            if(AddPart((STRPTR)t,(STRPTR)cfg.rom_path,256) && path_available(t)){
                strcpy(cfg.rom_path, t);
                printf("ROM found in the default ROM drawer: %s\n", t);
            }
        }
    } else {
        int g;
        g_quiet=1;                       /* GUI : aucune sortie console */
        g=gui_get_config(&cfg);
        if(g<=0){ rc=0; goto mainclean; }   /* Quit, fermeture, ou GUI indisponible */
        /* v1.2 RC4: the GUI session log (the Shell prints it) */
        g_logfh=Open((STRPTR)"T:ZZPicoDrive32X.log", MODE_NEWFILE);
        printf("ZZPicoDrive32X 1.2.1 GUI session - ROM %s\n", cfg.rom_path);
    }
    zp_config_save(&cfg, "PROGDIR:ZZPicoDrive32X.cfg");
    rc=run_picodrive(&cfg);
    if(g_quiet && rc!=0){   /* v1.2 RC4: a GUI start that failed says why */
        struct EasyStruct es;
        es.es_StructSize=sizeof(struct EasyStruct); es.es_Flags=0;
        es.es_Title=(STRPTR)"ZZPicoDrive32X";
        es.es_TextFormat=(STRPTR)"The game stopped: %s (code %ld).\nThe details are in T:ZZPicoDrive32X.log.";
        es.es_GadgetFormat=(STRPTR)"OK";
        EasyRequest(NULL,&es,NULL,(ULONG)g_exit_reason,(LONG)rc);
    }
mainclean:
    if(g_logfh){ Close(g_logfh); g_logfh=0; }
    printf("MAIN: closing graphics/intuition\n");
    if(GfxBase_){ CloseLibrary(GfxBase_); GfxBase_=NULL; }
    if(IntuitionBase){ CloseLibrary((struct Library*)IntuitionBase); IntuitionBase=NULL; }
    printf("MAIN: return %d (libnix exit follows)\n", rc);
    return rc;
}