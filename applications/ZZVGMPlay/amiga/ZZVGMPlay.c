/*
 * ZZVGMPlay v1.0 - XACP Core1 multi-chip VGM/VGZ player for AmigaOS 3.1
 *
 * GUI layout intentionally follows ZZPlayGUI XX16:
 *   - 320 px window
 *   - 3 compact text rows
 *   - progress slider
 *   - compact bitmap-icon transport row derived from Xanxi ZZPlayGUI
 *   - REW/FF omitted until state-correct VGM seek exists
 *   - separate GadTools playlist window with Play/Add/Del/Up/Dn/Clear
 *
 * A0r4 audio scope:
 *   - .VGM and native .VGZ (inflate on Core1)
 *   - OPL2/OPL3, SN76489, YM2612, YM2151, AY8910/YM2149
 *   - Core1 synthesis/mixing; 68k remains GUI/file/AHI transport
 *
 * XACP 1.8 target. ZZMIDI/Core0 memory is never used.
 */
#define __USE_SYSBASE

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <libraries/configvars.h>
#include <devices/ahi.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/expansion.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdio.h>

#include "zzvgm_shared.h"

struct Library *GadToolsBase = NULL;
struct Library *AslBase      = NULL;

#define ZZ9000_MANUF   0x6D6Eu
#define ZZ9000_PROD_AX 0x0Au
#define ZZ9000_PROD    0x04u
#define MNT_FB_BASE    0x00010000UL
#define ZZ_WR(b,o,v) (*((volatile UWORD *)((UBYTE *)(b) + (o))) = (UWORD)(v))

#define WIN_WIDTH      320
#define PROGRESS_MAX   1000UL
#define MAX_PLAYLIST   256
#define PATH_BUF_SIZE  512
#define BUF_FRAMES     4096UL
#define BUF_BYTES      (BUF_FRAMES * 4UL)

#define GAD_PREV       1
#define GAD_REW        2
#define GAD_STOP       3
#define GAD_PLAY       4
#define GAD_PAUSE      5
#define GAD_FF         6
#define GAD_NEXT       7
#define GAD_OPEN       8
#define GAD_PLS        9
#define GAD_LOOP       10
#define GAD_PROGRESS   11
#define GAD_COUNT      12

/* Playlist-window controls, matching Xanxi ZZPlayGUI. */
#define PLG_LIST        1
#define PLG_ADD         2
#define PLG_DEL         3
#define PLG_UP          4
#define PLG_DOWN        5
#define PLG_CLEAR       6
#define PLG_PLAY        7

#define BTN_ICON_W     16
#define BTN_ICON_H      8

static const UWORD g_idata_prev[BTN_ICON_H] = {
    0x0000,0x0408,0x0C08,0x1C08,0x1C08,0x0C08,0x0408,0x0000
};
static const UWORD g_idata_rew[BTN_ICON_H] = {
    0x0000,0x0440,0x0CC0,0x1DC0,0x1DC0,0x0CC0,0x0440,0x0000
};
static const UWORD g_idata_stop[BTN_ICON_H] = {
    0x0000,0x07E0,0x07E0,0x07E0,0x07E0,0x07E0,0x07E0,0x0000
};
static const UWORD g_idata_play[BTN_ICON_H] = {
    0x1000,0x1800,0x1C00,0x1E00,0x1C00,0x1800,0x1000,0x0000
};
static const UWORD g_idata_pause[BTN_ICON_H] = {
    0x0000,0x1B00,0x1B00,0x1B00,0x1B00,0x1B00,0x1B00,0x0000
};
static const UWORD g_idata_ff[BTN_ICON_H] = {
    0x0000,0x0220,0x0330,0x03B8,0x03B8,0x0330,0x0220,0x0000
};
static const UWORD g_idata_next[BTN_ICON_H] = {
    0x0000,0x1080,0x1880,0x1C80,0x1C80,0x1880,0x1080,0x0000
};
static const UWORD g_idata_open[BTN_ICON_H] = {
    0x0000,0x1800,0x3FC0,0x2040,0x2040,0x2040,0x3FC0,0x0000
};
static const UWORD g_idata_pls[BTN_ICON_H] = {
    0x0000,0x3FE0,0x0000,0x3FE0,0x0000,0x3FE0,0x0000,0x0000
};
static const UWORD g_idata_loop[BTN_ICON_H] = {
    0x0000,0x1FC0,0x2020,0x2070,0x7020,0x1020,0x0FC0,0x0000
};
static struct Image g_btn_img[10];

/* ZZ9000/XACP */
static UBYTE *g_board = NULL;
static UBYTE *g_fb = NULL;
static UBYTE *g_upload = NULL;
static volatile ULONG *g_sh = NULL;
static volatile UBYTE *g_ring = NULL;

/* GUI */
static struct Window *g_win = NULL;
static struct Screen *g_scr = NULL;
static APTR g_vi = NULL;
static struct Gadget *g_glist = NULL;
static struct Gadget *g_gad[GAD_COUNT];
static struct Menu *g_main_menu = NULL;
static int g_menu_attached = 0;

/* GadTools menus are shown in the Workbench screen menu bar when this
 * window is active; no preference buttons occupy the transport area. */
static struct NewMenu g_main_menu_defs[] = {
    { NM_TITLE, (STRPTR)"Settings", NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Set Default Drawer...", NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Clear Default Drawer", NULL, 0, 0, NULL },
    { NM_TITLE, (STRPTR)"Debug", NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Show WRITES / UND", NULL,
      CHECKIT|MENUTOGGLE, 0, NULL },
    { NM_END, NULL, NULL, 0, 0, NULL }
};
static WORD g_tx, g_ty[3], g_th, g_iw;
static char g_row_cache[3][96];
static ULONG g_last_slider = 0xFFFFFFFFUL;

/* Playlist */
static char *g_playlist[MAX_PLAYLIST];
static int g_pcount = 0;
static int g_track = -1;
static struct Window *g_plswin = NULL;
static struct Gadget *g_plsglist = NULL;
static struct Gadget *g_plsgad = NULL;
static struct Gadget *g_plsbtn[PLG_PLAY + 1] = {NULL};
static struct List g_pls_execlist;
static struct Node g_pls_nodes[MAX_PLAYLIST];
static int g_pls_sel = -1;
#define PLS_DISP_MAX 280
static char g_pls_display[MAX_PLAYLIST][PLS_DISP_MAX];

/* Playback */
static char g_path[PATH_BUF_SIZE];
static char g_last_drawer[PATH_BUF_SIZE];  /* Changed on each successful file selection */
static char g_default_drawer[PATH_BUF_SIZE]; /* Fixed music library, chosen explicitly */
static char g_dispname[64];
static char g_status[48];
static ULONG g_vgmlen = 0;
static ULONG g_read = 0;
static ULONG g_sr = 44100;
static ULONG g_und = 0;
/* Default to a clean player window. The Debug menu can reveal live counters. */
static int g_debug_counters = 0;
static int g_repeat = 0;
static int g_playing = 0;
static int g_paused = 0;
static int g_quit = 0;
static int g_core_up = 0;
static int g_is_vgz = 0;
static int g_acur = 0;
static int g_anext = 1;
static ULONG g_refresh_tick = 0;
#define GUI_REFRESH_TICKS 6UL

/* AHI */
static struct MsgPort *g_ahiport = NULL;
static struct AHIRequest *g_req[2] = {NULL,NULL};
static UBYTE *g_buf[2] = {NULL,NULL};
static BOOL g_act[2] = {FALSE,FALSE};
static BOOL g_ahi_device_open = FALSE;

/* Diagnostic trace for orderly shutdown, intentionally disabled during playback.
 * T: is in RAM: on AmigaOS 3.x; copy the log before rebooting after a crash.
 * Every marker is explicitly flushed so the last successful step survives.
 */
static BPTR g_exit_trace_file = 0;
static void exit_trace(const char *msg)
{
    LONG n;
    if (!g_exit_trace_file || !msg) return;
    n = (LONG)strlen(msg);
    if (n > 0) (void)Write(g_exit_trace_file, (APTR)msg, n);
    (void)Write(g_exit_trace_file, (APTR)"\n", 1);
    (void)Flush(g_exit_trace_file);
}

/* GUI helpers used by playback code before their definitions. */
static void playlist_refresh(void);

static void cache_flush(void *p, ULONG n)
{
    CacheClearE(p, n, CACRF_ClearD);
}
static ULONG shget(int n)
{
    cache_flush((void *)&g_sh[n], 4);
    return g_sh[n];
}
static void shput(int n, ULONG v)
{
    g_sh[n] = v;
    cache_flush((void *)&g_sh[n], 4);
}
static void status_set(const char *s)
{
    strncpy(g_status, s ? s : "", sizeof(g_status)-1);
    g_status[sizeof(g_status)-1] = '\0';
}
static const char *base_name(const char *p)
{
    const char *n = p;
    if (!p) return "";
    while (*p) {
        if (*p == '/' || *p == ':') n = p + 1;
        p++;
    }
    return n;
}
static void set_dispname(const char *p)
{
    const char *n = base_name(p);
    int len = (int)strlen(n);
    if (len >= (int)sizeof(g_dispname)) len = sizeof(g_dispname)-1;
    memcpy(g_dispname, n, len);
    g_dispname[len] = '\0';
    if (len > 4) {
        char *e = g_dispname + len - 4;
        if (e[0] == '.' &&
            (e[1]=='v'||e[1]=='V') && (e[2]=='g'||e[2]=='G') &&
            (e[3]=='m'||e[3]=='M'||e[3]=='z'||e[3]=='Z')) *e = '\0';
    }
}

/*
 * VGM directory preference, deliberately plain AmigaDOS I/O (OS 3.1).
 * Keep both the live ENV: value and the reboot-persistent ENVARC: value.
 * A missing/unmounted volume is ignored for that ASL invocation; the
 * remembered path is not deleted, so it works again once remounted.
 */
#define LAST_DRAWER_ENV    "ENV:ZZVGMPlay_LastDrawer"
#define LAST_DRAWER_ENVARC "ENVARC:ZZVGMPlay_LastDrawer"
#define DEFAULT_DRAWER_ENV    "ENV:ZZVGMPlay_DefaultDrawer"
#define DEFAULT_DRAWER_ENVARC "ENVARC:ZZVGMPlay_DefaultDrawer"

static int drawer_is_available(const char *drawer)
{
    BPTR lock;
    if (!drawer || !drawer[0] || strlen(drawer) >= PATH_BUF_SIZE) return 0;
    lock = Lock((STRPTR)drawer, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

static int read_drawer_pref(const char *name, char *dst)
{
    char buf[PATH_BUF_SIZE];
    BPTR f = Open((STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!f) return 0;
    n = Read(f, buf, PATH_BUF_SIZE);
    Close(f);
    if (n <= 0 || n >= PATH_BUF_SIZE) return 0;
    /* Refuse binary/partial preferences; tolerate a hand-edited newline. */
    if (memchr(buf, '\0', (size_t)n)) return 0;
    while (n > 0 && (buf[n-1] == '\r' || buf[n-1] == '\n')) --n;
    if (!n) return 0;
    buf[n] = '\0';
    if (!drawer_is_available(buf)) return 0;
    strcpy(dst, buf);
    return 1;
}

static void load_last_drawer(void)
{
    g_last_drawer[0] = '\0';
    if (read_drawer_pref(LAST_DRAWER_ENV, g_last_drawer)) return;
    (void)read_drawer_pref(LAST_DRAWER_ENVARC, g_last_drawer);
}

static void write_drawer_pref(const char *name, const char *drawer)
{
    BPTR f = Open((STRPTR)name, MODE_NEWFILE);
    if (f) {
        (void)Write(f, (APTR)drawer, (LONG)strlen(drawer));
        Close(f);
    }
}

static void remember_last_drawer(const char *drawer)
{
    size_t n;
    if (!drawer || !drawer[0]) return;
    n = strlen(drawer);
    if (n >= PATH_BUF_SIZE || !drawer_is_available(drawer)) return;
    if (!strcmp(g_last_drawer, drawer)) return;
    memcpy(g_last_drawer, drawer, n + 1);
    write_drawer_pref(LAST_DRAWER_ENVARC, g_last_drawer);
    write_drawer_pref(LAST_DRAWER_ENV, g_last_drawer);
}

/* An explicit DefaultDrawer always wins. LastDrawer is the fallback.
 * Neither setting is erased when a volume is temporarily unavailable. */
static void load_default_drawer(void)
{
    g_default_drawer[0] = '\0';
    /* The persistent copy is authoritative, even if ENV: is stale. */
    if (read_drawer_pref(DEFAULT_DRAWER_ENVARC, g_default_drawer)) return;
    (void)read_drawer_pref(DEFAULT_DRAWER_ENV, g_default_drawer);
}

/* Keep a strict durable-write check for the user-selected default drawer. */
static int write_default_drawer(const char *path, const char *drawer)
{
    LONG n = (LONG)strlen(drawer);
    BPTR f = Open((STRPTR)path, MODE_NEWFILE);
    LONG written;
    if (!f) return 0;
    written = Write(f, (APTR)drawer, n);
    Close(f);
    return written == n;
}

static void save_default_drawer(const char *drawer)
{
    size_t n;
    if (!drawer || !drawer[0]) return;
    n = strlen(drawer);
    if (n >= PATH_BUF_SIZE || !drawer_is_available(drawer)) {
        status_set("Invalid default drawer");
        return;
    }
    if (!write_default_drawer(DEFAULT_DRAWER_ENVARC, drawer)) {
        status_set("Cannot save default in ENVARC:");
        return;
    }
    /* Persistent copy takes precedence on restart if ENV: is unwritable. */
    (void)write_default_drawer(DEFAULT_DRAWER_ENV, drawer);
    memcpy(g_default_drawer, drawer, n + 1);
    status_set("Default drawer saved");
}

static void clear_default_drawer(void)
{
    BPTR lock;
    /* DeleteFile also fails when a preference does not exist. */
    (void)DeleteFile((STRPTR)DEFAULT_DRAWER_ENV);
    (void)DeleteFile((STRPTR)DEFAULT_DRAWER_ENVARC);
    lock = Lock((STRPTR)DEFAULT_DRAWER_ENVARC, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        status_set("Cannot remove ENVARC: default");
        return;
    }
    lock = Lock((STRPTR)DEFAULT_DRAWER_ENV, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        status_set("Cannot remove ENV: default");
        return;
    }
    g_default_drawer[0] = '\0';
    status_set("Default cleared - using Last");
}

/* TAG_IGNORE prevents ASL from dereferencing an unmounted volume. */
static void set_initial_music_drawer_tag(struct TagItem *tag)
{
    const char *drawer = NULL;
    if (drawer_is_available(g_default_drawer))
        drawer = g_default_drawer;
    else if (drawer_is_available(g_last_drawer))
        drawer = g_last_drawer;
    if (drawer) {
        tag->ti_Tag = ASLFR_InitialDrawer;
        tag->ti_Data = (ULONG)drawer;
    } else {
        tag->ti_Tag = TAG_IGNORE;
        tag->ti_Data = 0;
    }
}

/* ASLFR_DrawersOnly returns the selected directory in fr_Drawer (OS 3.1). */
static void choose_default_drawer(void)
{
    struct FileRequester *fr;
    struct TagItem tags[4];
    tags[0].ti_Tag = ASLFR_TitleText;
    tags[0].ti_Data = (ULONG)"Choose default VGM/VGZ drawer";
    tags[1].ti_Tag = ASLFR_DrawersOnly;
    tags[1].ti_Data = TRUE;
    set_initial_music_drawer_tag(&tags[2]);
    tags[3].ti_Tag = TAG_END;
    tags[3].ti_Data = 0;
    fr = AllocAslRequest(ASL_FileRequest, tags);
    if (!fr) { status_set("ASL drawer selector failed"); return; }
    if (AslRequest(fr, NULL)) save_default_drawer(fr->fr_Drawer);
    FreeAslRequest(fr);
}

static void format_time(char *dst, ULONG samples)
{
    ULONG sec = samples / 44100UL;
    ULONG min = sec / 60UL;
    sec %= 60UL;
    sprintf(dst, "%lu:%02lu", min, sec);
}

static void draw_row(int row, const char *src)
{
    struct RastPort *rp;
    char b[96];
    int len, orig;
    WORD x, y, maxw;

    if (!g_win || row < 0 || row > 2) return;
    rp = g_win->RPort;
    strncpy(b, src ? src : "", sizeof(b)-1);
    b[sizeof(b)-1] = '\0';
    orig = (int)strlen(b);
    len = orig;
    maxw = g_iw - 6;
    while (len > 0 && TextLength(rp, (STRPTR)b, len) > maxw) len--;
    if (len < orig && len >= 4) {
        b[len-3] = '.'; b[len-2] = '.'; b[len-1] = '.'; b[len] = '\0';
    } else b[len] = '\0';

    if (!strcmp(g_row_cache[row], b)) return;
    strcpy(g_row_cache[row], b);

    x = g_tx;
    y = g_ty[row];
    SetAPen(rp, 0);
    RectFill(rp, x, y, x + g_iw - 1, y + g_th - 1);
    if (b[0]) {
        SetAPen(rp, 1);
        SetDrMd(rp, JAM1);
        Move(rp, x + 2, y + rp->Font->tf_Baseline);
        Text(rp, (STRPTR)b, (LONG)strlen(b));
    }
}

static void chipmask_text(char *dst, ULONG mask)
{
    int first=1;
    dst[0]='\0';
#define ADDCH(bit,name) do { if(mask&(bit)){ if(!first) strcat(dst,"+"); strcat(dst,(name)); first=0; } } while(0)
    if(mask&ZZVGM_CMASK_YM2612_DUAL){ADDCH(ZZVGM_CMASK_YM2612_DUAL,"2x2612");}
    else {ADDCH(ZZVGM_CMASK_YM2612,"2612");}
    ADDCH(ZZVGM_CMASK_RF5C68,"RF5C68");
    ADDCH(ZZVGM_CMASK_YM2610,"2610");
    ADDCH(ZZVGM_CMASK_QSOUND,"QSound");
    ADDCH(ZZVGM_CMASK_ES5503,"ES5503");
    ADDCH(ZZVGM_CMASK_K053260,"K053260");
    ADDCH(ZZVGM_CMASK_K054539,"K054539");
    ADDCH(ZZVGM_CMASK_K051649,"SCC/SCC+");
    ADDCH(ZZVGM_CMASK_YM2151,"2151");
    ADDCH(ZZVGM_CMASK_SEGAPCM,"SEGA");
    ADDCH(ZZVGM_CMASK_OKI6295,"OKI");
    ADDCH(ZZVGM_CMASK_YM2413,"2413");
    ADDCH(ZZVGM_CMASK_NES_APU,"NES");
    ADDCH(ZZVGM_CMASK_GB_DMG,"GB");
    ADDCH(ZZVGM_CMASK_HUC6280,"PCE");
    ADDCH(ZZVGM_CMASK_OPL3,"OPL3");
    ADDCH(ZZVGM_CMASK_OPL2,"OPL2");
    ADDCH(ZZVGM_CMASK_SN76489,"SN");
    ADDCH(ZZVGM_CMASK_AY8910,"AY");
#undef ADDCH
    if(first) strcpy(dst,"---");
}

static void update_display(void)
{
    char b[96], t1[16], t2[16];
    ULONG st=0, mask=0, played=0, total=0, pos=0, writes=0, loops=0, up=0;
    ULONG level=0;

    if (!g_win) return;
    if (g_sh) {
        st = shget(VSH_STATUS);
        mask = shget(VSH_CHIP_MASK);
        played = shget(VSH_PLAY_SAMPLES);
        total = shget(VSH_TOTAL_SAMPLES);
        if (g_debug_counters) {
            writes = shget(VSH_OPL_WRITES);
            loops = shget(VSH_LOOP_COUNT);
        }
        up = shget(VSH_UPLOAD_DONE);
    }
    /* Repeat now applies to the playlist, not the VGM internal loop. */
    pos = played;
    if (total && pos > total) pos = total;

    draw_row(0, g_dispname[0] ? g_dispname : "ZZVGMPlay / Core1");

    format_time(t1, pos);
    format_time(t2, total);
    {
        char chips[96];
        chipmask_text(chips,mask);
        sprintf(b, "%s / %s  %s  %s",
                t1, total ? t2 : "--:--", chips, g_is_vgz ? "VGZ" : "VGM");
    }
    draw_row(1, b);

    if (st == ZZVGM_ST_UPLOADING)
        sprintf(b, "Loading %luK/%luK", up/1024UL, g_vgmlen/1024UL);
    else if (g_paused) {
        if (g_debug_counters)
            sprintf(b, "Paused  writes %lu  und %lu  loop %lu", writes, g_und, loops);
        else
            strcpy(b, "Paused");
    } else if (g_playing) {
        if (g_debug_counters)
            sprintf(b, "Playing  writes %lu  und %lu", writes, g_und);
        else
            strcpy(b, "Playing");
    }
    else
        sprintf(b, "%s", g_status[0] ? g_status : "Ready");
    draw_row(2, b);

    if (g_gad[GAD_PROGRESS]) {
        if (total) {
            unsigned long long q = (unsigned long long)pos * PROGRESS_MAX;
            level = (ULONG)(q / total);
            if (level > PROGRESS_MAX) level = PROGRESS_MAX;
        }
        if (g_last_slider == 0xFFFFFFFFUL || level != g_last_slider) {
            GT_SetGadgetAttrs(g_gad[GAD_PROGRESS], g_win, NULL,
                              GTSL_Level, level, TAG_END);
            g_last_slider = level;
        }
    }
}

static void init_btn_images(void)
{
    static const UWORD * const src[10] = {
        g_idata_prev,g_idata_rew,g_idata_stop,g_idata_play,g_idata_pause,
        g_idata_ff,g_idata_next,g_idata_open,g_idata_pls,g_idata_loop
    };
    int i;
    for (i=0;i<10;i++) {
        g_btn_img[i].LeftEdge=0; g_btn_img[i].TopEdge=0;
        g_btn_img[i].Width=BTN_ICON_W; g_btn_img[i].Height=BTN_ICON_H;
        g_btn_img[i].Depth=1; g_btn_img[i].ImageData=(UWORD *)src[i];
        g_btn_img[i].PlanePick=1; g_btn_img[i].PlaneOnOff=0;
        g_btn_img[i].NextImage=NULL;
    }
}
static void draw_icons(void)
{
    int i;
    if (!g_win) return;
    for (i=1;i<=10;i++) if (g_gad[i]) {
        WORD x = g_gad[i]->LeftEdge + (g_gad[i]->Width-BTN_ICON_W)/2;
        WORD y = g_gad[i]->TopEdge + (g_gad[i]->Height-BTN_ICON_H)/2;
        DrawImage(g_win->RPort, &g_btn_img[i-1], x, y);
    }
    if (g_gad[GAD_LOOP]) {
        WORD x = g_gad[GAD_LOOP]->LeftEdge + g_gad[GAD_LOOP]->Width - 5;
        WORD y = g_gad[GAD_LOOP]->TopEdge + g_gad[GAD_LOOP]->Height - 5;
        SetAPen(g_win->RPort, g_repeat ? 1 : 0);
        RectFill(g_win->RPort, x, y, x+2, y+2);
    }
}

static void ahi_close(void);
static int ahi_open(void)
{
    int i;
    g_ahiport = CreateMsgPort();
    if (!g_ahiport) return 0;
    for (i=0;i<2;i++) {
        g_req[i]=(struct AHIRequest *)CreateIORequest(g_ahiport,sizeof(struct AHIRequest));
        if (!g_req[i]) goto fail;
    }
    g_req[0]->ahir_Version=4;
    if (OpenDevice(AHINAME,AHI_DEFAULT_UNIT,(struct IORequest *)g_req[0],0)) goto fail;
    g_ahi_device_open=TRUE;
    /* Documented AHI double-buffering pattern; copy only after OpenDevice. */
    CopyMem(g_req[0],g_req[1],sizeof(struct AHIRequest));
    for (i=0;i<2;i++) {
        g_buf[i]=(UBYTE *)AllocMem(BUF_BYTES,MEMF_PUBLIC|MEMF_CLEAR);
        if (!g_buf[i]) goto fail;
    }
    return 1;
fail:
    ahi_close();
    return 0;
}
static void audio_abort(void)
{
    int i;
    for (i=0;i<2;i++) if (g_req[i] && g_act[i]) {
        if (!CheckIO((struct IORequest *)g_req[i])) AbortIO((struct IORequest *)g_req[i]);
        WaitIO((struct IORequest *)g_req[i]);
        g_act[i]=FALSE;
    }
}
static void ahi_close(void)
{
    int i;
    exit_trace("ahi: abort outstanding requests");
    audio_abort();
    exit_trace("ahi: requests drained");
    if (g_ahi_device_open && g_req[0]) {
        CloseDevice((struct IORequest *)g_req[0]);
        g_ahi_device_open=FALSE;
    }
    exit_trace("ahi: device closed");
    for (i=0;i<2;i++) {
        if (g_buf[i]) { FreeMem(g_buf[i],BUF_BYTES); g_buf[i]=NULL; }
        if (g_req[i]) { DeleteIORequest((struct IORequest *)g_req[i]); g_req[i]=NULL; }
        g_act[i]=FALSE;
    }
    exit_trace("ahi: requests and buffers freed");
    if (g_ahiport) { DeleteMsgPort(g_ahiport); g_ahiport=NULL; }
    exit_trace("ahi: reply port deleted");
}
static void sendbuf(int s)
{
    struct AHIRequest *r=g_req[s];
    r->ahir_Std.io_Command=CMD_WRITE;
    r->ahir_Std.io_Data=g_buf[s];
    r->ahir_Std.io_Length=BUF_BYTES;
    r->ahir_Std.io_Flags=0;
    r->ahir_Std.io_Offset=0;
    r->ahir_Frequency=g_sr;
    r->ahir_Type=AHIST_S16S;
    r->ahir_Volume=0x10000L;
    r->ahir_Position=0x8000L;
    r->ahir_Link=g_act[s^1] ? g_req[s^1] : NULL;
    SendIO((struct IORequest *)r);
    g_act[s]=TRUE;
}
static int fillbuf(UBYTE *dst)
{
    ULONG wr, avail, n1, off, rest;
    wr=shget(VSH_PCM_WRITE);
    avail=wr-g_read;
    if (avail<BUF_FRAMES) return 0;

    off=(g_read&ZZVGM_RING_MASK)*4UL;
    n1=ZZVGM_RING_BYTES-off;
    if (n1>BUF_BYTES) n1=BUF_BYTES;

    /* Same low-CPU path as the latest ZZSIDPlay/ZZMIDIPlay: the PCM ring
     * already contains AHI-ready stereo S16, so copy one contiguous block
     * from the ZZ9000 DDR window (two only on ring wrap). */
    cache_flush((void *)(g_ring+off),n1);
    CopyMem((APTR)(g_ring+off),dst,n1);
    if (n1<BUF_BYTES) {
        rest=BUF_BYTES-n1;
        cache_flush((void *)g_ring,rest);
        CopyMem((APTR)g_ring,dst+n1,rest);
    }

    g_read+=BUF_FRAMES;
    shput(VSH_PCM_READ,g_read);
    return 1;
}
static int audio_prime(int reset_read)
{
    int timeout=0, ok=1;
    if (reset_read) {
        g_read=0;
        shput(VSH_PCM_READ,0);
    }

    /* Latest ZZSIDPlay/ZZMIDIPlay model: start one AHI request only.
     * audio_tick() always prepares/queues the NEXT request before waiting
     * for the current one, so AHI itself paces the 68k without polling. */
    g_acur=0;
    g_anext=1;
    g_refresh_tick=GUI_REFRESH_TICKS;
    while (!fillbuf(g_buf[g_acur])) {
        ULONG st=shget(VSH_STATUS);
        if (st==ZZVGM_ST_ERROR || st==ZZVGM_ST_DONE) { ok=0; goto out; }
        Delay(1);
        if (++timeout>300) { ok=0; goto out; }
    }
    sendbuf(g_acur);
out:
    return ok;
}

static int load_blob(void)
{
    BPTR f;
    LONG sz,rd;
    f=Open("PROGDIR:zzvgm_core1.bin",MODE_OLDFILE);
    if (!f) f=Open("zzvgm_core1.bin",MODE_OLDFILE);
    if (!f) return 0;
    Seek(f,0,OFFSET_END); sz=Seek(f,0,OFFSET_CURRENT);
    Seek(f,0,OFFSET_BEGINNING);
    if (sz<=0 || sz>0x100000) { Close(f); return 0; }
    rd=Read(f,g_fb+ZZVGM_BLOB_FB_OFFSET,sz);
    Close(f);
    cache_flush(g_fb+ZZVGM_BLOB_FB_OFFSET,sz);
    return rd==sz;
}
static int wait_cmd_ack(ULONG loops)
{
    ULONG i;
    for (i=0;i<loops;i++) {
        if (shget(VSH_CMD)==ZZVGM_CMD_NONE) return 1;
        if (shget(VSH_STATUS)==ZZVGM_ST_ERROR) return 0;
    }
    return 0;
}
static int vgm_file_size(const char *p, ULONG *outsz)
{
    BPTR f=Open((STRPTR)p,MODE_OLDFILE);
    LONG sz;
    if (!f) return 0;
    Seek(f,0,OFFSET_END); sz=Seek(f,0,OFFSET_CURRENT); Close(f);
    if (sz<18 || (ULONG)sz>ZZVGM_VGM_MAX_BYTES) return 0;
    *outsz=(ULONG)sz;
    return 1;
}
static int detect_vgz(const char *p)
{
    BPTR f=Open((STRPTR)p,MODE_OLDFILE);
    UBYTE h[2]; LONG n;
    if(!f) return 0;
    n=Read(f,h,2); Close(f);
    return n==2 && h[0]==0x1f && h[1]==0x8b;
}
static int upload_vgm(const char *p)
{
    BPTR f;
    UBYTE *tmp;
    LONG rd;
    ULONG off=0,i,words,k,b0,b1,b2,b3,n;
    volatile ULONG *dst=(volatile ULONG *)g_upload;

    if (!vgm_file_size(p,&g_vgmlen)) return 0;
    f=Open((STRPTR)p,MODE_OLDFILE); if (!f) return 0;
    tmp=(UBYTE *)AllocMem(ZZVGM_UPLOAD_BYTES,MEMF_PUBLIC);
    if (!tmp) { Close(f); return 0; }

    shput(VSH_VGM_LEN,g_vgmlen);
    shput(VSH_UPLOAD_OFFSET,0); shput(VSH_UPLOAD_SIZE,0);
    shput(VSH_CMD,ZZVGM_CMD_UPLOAD_BEGIN);
    if (!wait_cmd_ack(2000000UL)) { FreeMem(tmp,ZZVGM_UPLOAD_BYTES); Close(f); return 0; }

    while (off<g_vgmlen) {
        n=g_vgmlen-off; if (n>ZZVGM_UPLOAD_BYTES) n=ZZVGM_UPLOAD_BYTES;
        rd=Read(f,tmp,n);
        if (rd!=(LONG)n) { FreeMem(tmp,ZZVGM_UPLOAD_BYTES); Close(f); return 0; }
        words=(n+3UL)>>2;
        for (i=0;i<words;i++) {
            k=i<<2;
            b0=(k+0<n)?tmp[k+0]:0; b1=(k+1<n)?tmp[k+1]:0;
            b2=(k+2<n)?tmp[k+2]:0; b3=(k+3<n)?tmp[k+3]:0;
            dst[i]=(b0<<24)|(b1<<16)|(b2<<8)|b3;
        }
        cache_flush((void *)g_upload,words*4UL);
        shput(VSH_UPLOAD_OFFSET,off); shput(VSH_UPLOAD_SIZE,n);
        shput(VSH_CMD,ZZVGM_CMD_UPLOAD_CHUNK);
        if (!wait_cmd_ack(3000000UL)) { FreeMem(tmp,ZZVGM_UPLOAD_BYTES); Close(f); return 0; }
        off+=n;
        update_display();
    }
    FreeMem(tmp,ZZVGM_UPLOAD_BYTES); Close(f);
    return shget(VSH_UPLOAD_DONE)==g_vgmlen;
}
static int launch_core(void)
{
    int i;
    if (!load_blob()) return 0;
    for (i=0;i<ZZVGM_SHARED_WORDS;i++) g_sh[i]=0;
    shput(VSH_SAMPLE_RATE,g_sr);
    /* Core1 must reach VGM end (0x66) so GUI can advance the playlist. */
    shput(VSH_FLAGS,(g_is_vgz?ZZVGM_FLAG_GZIP:0));
    shput(VSH_CANARY_68K,ZZVGM_CANARY_68K);
    ZZ_WR(g_board,ZZVGM_REG_ARM_RUN_HI,ZZVGM_RUN_HI_VALUE);
    ZZ_WR(g_board,ZZVGM_REG_ARM_RUN_LO,ZZVGM_RUN_LO_VALUE);
    for (i=0;i<3000000;i++) {
        if (shget(VSH_STATUS)==ZZVGM_ST_READY) {
            if (shget(VSH_MAGIC)!=ZZVGM_MAGIC ||
                shget(VSH_ABI_VERSION)!=ZZVGM_ABI_VERSION) return 0;
            g_core_up=1;
            return 1;
        }
    }
    return 0;
}
static void release_core(void)
{
    int i;
    if (!g_core_up) return;
    shput(VSH_CMD,ZZVGM_CMD_STOP);
    for (i=0;i<1000000;i++) if (shget(VSH_STATUS)==ZZVGM_ST_STOPPED) break;
    if (i==1000000) exit_trace("core1: STOP timeout (not acknowledged)");
    else exit_trace("core1: STOP acknowledged");
    g_core_up=0;
}
static void stopplay(void)
{
    audio_abort();
    g_playing=0; g_paused=0;
    release_core();
    status_set("Stopped");
    g_last_slider=0xFFFFFFFFUL;
}

static int player_start(int track)
{
    int i;
    if (track<0 || track>=g_pcount) return 0;
    stopplay();
    g_track=track;
    g_pls_sel=track;
    strncpy(g_path,g_playlist[track],sizeof(g_path)-1);
    g_path[sizeof(g_path)-1]='\0';
    set_dispname(g_path);
    g_und=0;

    if (!vgm_file_size(g_path,&g_vgmlen)) { status_set("Bad/too large VGM/VGZ"); update_display(); return 0; }
    g_is_vgz=detect_vgz(g_path);
    if (!launch_core()) { status_set("Core1 start/ABI error"); update_display(); return 0; }

    status_set("Loading"); update_display();
    if (!upload_vgm(g_path)) {
        status_set("VGM upload failed"); release_core(); update_display(); return 0;
    }

    /* Core1 must reach VGM end (0x66) so GUI can advance the playlist. */
    shput(VSH_FLAGS,(g_is_vgz?ZZVGM_FLAG_GZIP:0));
    shput(VSH_PCM_READ,0);
    shput(VSH_CMD,ZZVGM_CMD_START);
    for (i=0;i<1500000;i++) {
        ULONG s=shget(VSH_STATUS);
        if (s==ZZVGM_ST_PLAYING) break;
        if (s==ZZVGM_ST_ERROR) {
            char eb[48];
            sprintf(eb,"Core1 err %lu cmd %lu",shget(VSH_ERROR),shget(VSH_BAD_CMD));
            status_set(eb); release_core(); update_display(); return 0;
        }
    }
    if (shget(VSH_STATUS)!=ZZVGM_ST_PLAYING) {
        status_set("Core1 start timeout"); release_core(); update_display(); return 0;
    }

    if (!audio_prime(1)) {
        status_set("No PCM from Core1"); release_core(); update_display(); return 0;
    }
    g_playing=1; g_paused=0;
    status_set("Playing");
    playlist_refresh();
    update_display();
    return 1;
}

static void player_pause(void)
{
    if (!g_playing || g_paused) return;
    audio_abort();
    g_paused=1;
    status_set("Paused");
    update_display();
}
static void player_resume(void)
{
    if (!g_playing || !g_paused) return;
    if (audio_prime(0)) {
        g_paused=0;
        status_set("Playing");
    } else {
        status_set("Resume failed");
        g_playing=0;
    }
    update_display();
}

static void player_finished(void)
{
    int next=g_track+1;
    audio_abort();
    g_playing=0; g_paused=0;
    release_core();
    status_set("Done");
    update_display();
    /* Repeat applies to the full playlist, not the current VGM's loop point.
     * A one-item playlist naturally repeats that one item. */
    if (next>=g_pcount && g_repeat && g_pcount>0) next=0;
    if (next>=0 && next<g_pcount) player_start(next);
}

static void audio_tick(void)
{
    ULONG st, wr, avail;
    UBYTE *buf;
    int queued=0;

    if (!g_playing || g_paused) return;

    /* Same AHI pacing model as current ZZSIDPlay/ZZMIDIPlay:
     * prepare and queue NEXT first, then WaitIO() on CURRENT.
     * The blocking WaitIO is the scheduler; there is no reply-port polling
     * loop while audio is playing. */
    buf=g_buf[g_anext];
    if (fillbuf(buf)) {
        sendbuf(g_anext);
        queued=1;
    } else {
        /* Only consult STATUS on the uncommon short-ring path. In the normal
         * case a tick now needs one PCM_WRITE read before WaitIO, instead of
         * repeatedly flushing STATUS around CheckIO. */
        st=shget(VSH_STATUS);
        if (st==ZZVGM_ST_ERROR) {
            char b[48];
            sprintf(b,"Core1 err %lu cmd %lu",shget(VSH_ERROR),shget(VSH_BAD_CMD));
            audio_abort(); g_playing=0; release_core(); status_set(b); update_display();
            return;
        }
        if (st==ZZVGM_ST_PLAYING) {
            memset(buf,0,BUF_BYTES);
            g_und++;
            sendbuf(g_anext);
            queued=1;
        }
        /* ZZVGM_ST_DONE: do not append a silence buffer. Let the current
         * request finish and terminate once the final full block is drained. */
    }

    if (g_act[g_acur]) {
        WaitIO((struct IORequest *)g_req[g_acur]);
        g_act[g_acur]=FALSE;
    }

    if (queued) {
        int t=g_acur; g_acur=g_anext; g_anext=t;
    }

    st=shget(VSH_STATUS);
    if (st==ZZVGM_ST_ERROR) {
        char b[48];
        sprintf(b,"Core1 err %lu cmd %lu",shget(VSH_ERROR),shget(VSH_BAD_CMD));
        audio_abort(); g_playing=0; release_core(); status_set(b); update_display();
        return;
    }
    if (st==ZZVGM_ST_DONE) {
        wr=shget(VSH_PCM_WRITE);
        avail=wr-g_read;
        if (avail<BUF_FRAMES && !g_act[0] && !g_act[1]) {
            player_finished();
            return;
        }
    }

    /* Like ZZMIDIPlay/ZZSIDPlay, GUI status does not need to redraw for every
     * 4096-frame AHI write. This also avoids several cache-flushed mailbox
     * reads per audio buffer. */
    if (++g_refresh_tick>=GUI_REFRESH_TICKS) {
        g_refresh_tick=0;
        update_display();
    }
}

static void playlist_close(void)
{
    if (g_plswin) { CloseWindow(g_plswin); g_plswin=NULL; }
    if (g_plsglist) { FreeGadgets(g_plsglist); g_plsglist=NULL; g_plsgad=NULL; }
    memset(g_plsbtn,0,sizeof(g_plsbtn));
}

static void playlist_rebuild_exec_list(void)
{
    int i;
    g_pls_execlist.lh_Head=(struct Node *)&g_pls_execlist.lh_Tail;
    g_pls_execlist.lh_Tail=NULL;
    g_pls_execlist.lh_TailPred=(struct Node *)&g_pls_execlist;
    for (i=0;i<g_pcount;i++) {
        const char *mark = (i==g_track && i==g_pls_sel) ? "*> " :
                           (i==g_track) ? "*  " :
                           (i==g_pls_sel) ? ">  " : "   ";
        strncpy(g_pls_display[i],mark,PLS_DISP_MAX-1);
        g_pls_display[i][PLS_DISP_MAX-1]='\0';
        strncat(g_pls_display[i],base_name(g_playlist[i]),
                PLS_DISP_MAX-1-(int)strlen(g_pls_display[i]));
        g_pls_nodes[i].ln_Name=g_pls_display[i];
        g_pls_nodes[i].ln_Type=NT_USER;
        g_pls_nodes[i].ln_Pri=0;
        AddTail(&g_pls_execlist,&g_pls_nodes[i]);
    }
}

static void playlist_refresh(void)
{
    if (g_plsgad && g_plswin)
        GT_SetGadgetAttrs(g_plsgad,g_plswin,NULL,GTLV_Labels,(ULONG)~0UL,TAG_END);
    playlist_rebuild_exec_list();
    if (g_plsgad && g_plswin) {
        GT_SetGadgetAttrs(g_plsgad,g_plswin,NULL,
                          GTLV_Labels,(ULONG)&g_pls_execlist,
                          GTLV_Selected,(g_pls_sel>=0)?(ULONG)g_pls_sel:0,
                          TAG_END);
        GT_RefreshWindow(g_plswin,NULL);
    }
}

static void playlist_open(void)
{
    struct NewGadget ng;
    struct Gadget *prev;
    struct TagItem lv_tags[4];
    struct TagItem wt[11];
    UWORD bx,by,ih,listw,gap,bw,bh,btop;
    int k;
    static const char *labels[6] = {"Play","Add","Del","Up","Dn","Clear"};
    static const UWORD ids[6] = {PLG_PLAY,PLG_ADD,PLG_DEL,PLG_UP,PLG_DOWN,PLG_CLEAR};

    if (g_plswin || !g_scr || !g_vi) return;
    if (g_pls_sel<0 && g_track>=0) g_pls_sel=g_track;
    playlist_rebuild_exec_list();

    bx=g_scr->WBorLeft;
    by=g_scr->WBorTop+g_scr->Font->ta_YSize+1;
    ih=100;
    prev=CreateContext(&g_plsglist);
    if (!prev) return;
    memset(&ng,0,sizeof(ng));
    ng.ng_LeftEdge=bx; ng.ng_TopEdge=by;
    ng.ng_Width=300-bx-g_scr->WBorRight; ng.ng_Height=ih;
    ng.ng_TextAttr=g_scr->Font; ng.ng_GadgetID=PLG_LIST; ng.ng_VisualInfo=g_vi;
    lv_tags[0].ti_Tag=GTLV_Labels; lv_tags[0].ti_Data=(ULONG)&g_pls_execlist;
    lv_tags[1].ti_Tag=GTLV_ScrollWidth; lv_tags[1].ti_Data=16;
    lv_tags[2].ti_Tag=GTLV_Selected; lv_tags[2].ti_Data=(g_pls_sel>=0)?(ULONG)g_pls_sel:0;
    lv_tags[3].ti_Tag=TAG_END; lv_tags[3].ti_Data=0;
    prev=g_plsgad=CreateGadgetA(LISTVIEW_KIND,prev,&ng,lv_tags);
    if (!prev) { playlist_close(); return; }

    listw=(UWORD)(300-bx-g_scr->WBorRight);
    gap=4; bw=(UWORD)((listw-5*gap)/6); bh=(UWORD)(g_scr->Font->ta_YSize+6);
    btop=(UWORD)(by+ih+4);
    for (k=0;k<6;k++) {
        ng.ng_LeftEdge=(WORD)(bx+k*(bw+gap)); ng.ng_TopEdge=(WORD)btop;
        ng.ng_Width=bw; ng.ng_Height=bh; ng.ng_GadgetText=(UBYTE *)labels[k];
        ng.ng_GadgetID=ids[k]; ng.ng_Flags=0; ng.ng_VisualInfo=g_vi;
        prev=CreateGadgetA(BUTTON_KIND,prev,&ng,NULL);
        g_plsbtn[ids[k]]=prev;
        if (!prev) { playlist_close(); return; }
    }

    wt[0].ti_Tag=WA_Left; wt[0].ti_Data=50;
    wt[1].ti_Tag=WA_Top; wt[1].ti_Data=160;
    wt[2].ti_Tag=WA_Width; wt[2].ti_Data=300;
    wt[3].ti_Tag=WA_Height; wt[3].ti_Data=btop+bh+g_scr->WBorBottom+2;
    wt[4].ti_Tag=WA_Title; wt[4].ti_Data=(ULONG)"ZZVGMPlay Playlist";
    wt[5].ti_Tag=WA_Gadgets; wt[5].ti_Data=(ULONG)g_plsglist;
    wt[6].ti_Tag=WA_Flags; wt[6].ti_Data=WFLG_DRAGBAR|WFLG_CLOSEGADGET|WFLG_DEPTHGADGET|WFLG_ACTIVATE|WFLG_SMART_REFRESH;
    wt[7].ti_Tag=WA_IDCMP; wt[7].ti_Data=IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|LISTVIEWIDCMP;
    wt[8].ti_Tag=WA_PubScreen; wt[8].ti_Data=(ULONG)g_scr;
    wt[9].ti_Tag=TAG_END; wt[9].ti_Data=0;
    g_plswin=OpenWindowTagList(NULL,wt);
    if (!g_plswin) { playlist_close(); return; }
    GT_RefreshWindow(g_plswin,NULL);
}

static void playlist_add_via_asl(void);

static void playlist_delete_selected(void)
{
    int i,idx=g_pls_sel;
    if (idx<0 || idx>=g_pcount) return;
    if (idx==g_track) { stopplay(); g_track=-1; }
    else if (g_track>idx) g_track--;
    if (g_playlist[idx]) { FreeMem(g_playlist[idx],PATH_BUF_SIZE); g_playlist[idx]=NULL; }
    for (i=idx;i<g_pcount-1;i++) g_playlist[i]=g_playlist[i+1];
    g_playlist[g_pcount-1]=NULL; g_pcount--;
    if (g_pcount<=0) g_pls_sel=-1;
    else if (idx>=g_pcount) g_pls_sel=g_pcount-1;
    else g_pls_sel=idx;
    playlist_refresh();
}

static void playlist_move_selected(int dir)
{
    int idx=g_pls_sel,dst;
    char *tmp;
    if (idx<0 || idx>=g_pcount) return;
    dst=idx+dir; if (dst<0 || dst>=g_pcount) return;
    tmp=g_playlist[idx]; g_playlist[idx]=g_playlist[dst]; g_playlist[dst]=tmp;
    if (g_track==idx) g_track=dst; else if (g_track==dst) g_track=idx;
    g_pls_sel=dst; playlist_refresh();
}

static void playlist_clear_all(void)
{
    int i;
    stopplay();
    for (i=0;i<g_pcount;i++) if (g_playlist[i]) { FreeMem(g_playlist[i],PATH_BUF_SIZE); g_playlist[i]=NULL; }
    g_pcount=0; g_track=-1; g_pls_sel=-1;
    playlist_refresh();
}

static void playlist_play_selected(void)
{
    if (g_pls_sel<0 || g_pls_sel>=g_pcount) return;
    player_start(g_pls_sel);
    playlist_refresh();
}

static void playlist_handle(void)
{
    struct IntuiMessage *m;
    if (!g_plswin) return;
    while ((m=(struct IntuiMessage *)GT_GetIMsg(g_plswin->UserPort))) {
        ULONG cl=m->Class;
        UWORD code=m->Code;
        struct Gadget *gd=(struct Gadget *)m->IAddress;
        UWORD gid=gd?gd->GadgetID:0xffff;
        GT_ReplyIMsg(m);
        if (cl==IDCMP_CLOSEWINDOW) { playlist_close(); return; }
        if (cl==IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(g_plswin); GT_EndRefresh(g_plswin,TRUE); continue;
        }
        if (cl==IDCMP_GADGETUP) {
            if (gd==g_plsgad || gid==PLG_LIST) {
                if ((int)code>=0 && (int)code<g_pcount) { g_pls_sel=(int)code; playlist_refresh(); }
                continue;
            }
            switch (gid) {
            case PLG_PLAY:  playlist_play_selected(); break;
            case PLG_ADD:   playlist_add_via_asl(); break;
            case PLG_DEL:   playlist_delete_selected(); break;
            case PLG_UP:    playlist_move_selected(-1); break;
            case PLG_DOWN:  playlist_move_selected(+1); break;
            case PLG_CLEAR: playlist_clear_all(); break;
            default: break;
            }
        }
    }
}

static int playlist_add_path(const char *p)
{
    char *s;
    int n;
    if (!p || !p[0] || g_pcount>=MAX_PLAYLIST) return 0;
    n=(int)strlen(p); if (n>=PATH_BUF_SIZE) n=PATH_BUF_SIZE-1;
    s=(char *)AllocMem(PATH_BUF_SIZE,MEMF_ANY|MEMF_CLEAR);
    if (!s) return 0;
    memcpy(s,p,n); s[n]='\0';
    g_playlist[g_pcount++]=s;
    return 1;
}

static void playlist_add_via_asl(void)
{
    struct FileRequester *fr;
    struct TagItem tags[6];
    int fi,old_count=g_pcount;
    tags[0].ti_Tag=ASLFR_TitleText; tags[0].ti_Data=(ULONG)"Add VGM / VGZ to playlist";
    tags[1].ti_Tag=ASLFR_DoMultiSelect; tags[1].ti_Data=TRUE;
    tags[2].ti_Tag=ASLFR_DoPatterns; tags[2].ti_Data=TRUE;
    tags[3].ti_Tag=ASLFR_InitialPattern; tags[3].ti_Data=(ULONG)"#?.vg?";
    set_initial_music_drawer_tag(&tags[4]);
    tags[5].ti_Tag=TAG_END; tags[5].ti_Data=0;
    fr=AllocAslRequest(ASL_FileRequest,tags);
    if (fr && AslRequest(fr,NULL)) {
        char full[PATH_BUF_SIZE];
        remember_last_drawer(fr->fr_Drawer);
        if (fr->fr_NumArgs>0 && fr->fr_ArgList) {
            for (fi=0;fi<(int)fr->fr_NumArgs && g_pcount<MAX_PLAYLIST;fi++) {
                int dl=(int)strlen(fr->fr_Drawer);
                if (dl>PATH_BUF_SIZE-2) dl=PATH_BUF_SIZE-2;
                memcpy(full,fr->fr_Drawer,dl); full[dl]='\0';
                if (dl>0 && full[dl-1]!='/' && full[dl-1]!=':') full[dl++]='/';
                strncpy(full+dl,fr->fr_ArgList[fi].wa_Name,PATH_BUF_SIZE-1-dl);
                full[PATH_BUF_SIZE-1]='\0'; playlist_add_path(full);
            }
        } else if (fr->fr_File && fr->fr_File[0] && g_pcount<MAX_PLAYLIST) {
            int dl=(int)strlen(fr->fr_Drawer);
            if (dl>PATH_BUF_SIZE-2) dl=PATH_BUF_SIZE-2;
            memcpy(full,fr->fr_Drawer,dl); full[dl]='\0';
            if (dl>0 && full[dl-1]!='/' && full[dl-1]!=':') full[dl++]='/';
            strncpy(full+dl,fr->fr_File,PATH_BUF_SIZE-1-dl);
            full[PATH_BUF_SIZE-1]='\0'; playlist_add_path(full);
        }
    }
    if (fr) FreeAslRequest(fr);
    if (g_pls_sel<0 && g_pcount>old_count) g_pls_sel=old_count;
    playlist_refresh();
}
static void open_files(void)
{
    struct FileRequester *fr;
    struct TagItem tags[6];
    int fi,added=0,first;

    tags[0].ti_Tag=ASLFR_TitleText; tags[0].ti_Data=(ULONG)"Select VGM / VGZ";
    tags[1].ti_Tag=ASLFR_DoMultiSelect; tags[1].ti_Data=TRUE;
    tags[2].ti_Tag=ASLFR_DoPatterns; tags[2].ti_Data=TRUE;
    tags[3].ti_Tag=ASLFR_InitialPattern; tags[3].ti_Data=(ULONG)"#?.vg?";
    set_initial_music_drawer_tag(&tags[4]);
    tags[5].ti_Tag=TAG_END; tags[5].ti_Data=0;
    fr=AllocAslRequest(ASL_FileRequest,tags);
    if (!fr) return;
    if (AslRequest(fr,NULL)) {
        remember_last_drawer(fr->fr_Drawer);
        first=g_pcount;
        if (fr->fr_NumArgs>0 && fr->fr_ArgList) {
            for (fi=0;fi<(int)fr->fr_NumArgs && g_pcount<MAX_PLAYLIST;fi++) {
                char full[PATH_BUF_SIZE];
                int dl=(int)strlen(fr->fr_Drawer);
                if (dl>PATH_BUF_SIZE-2) dl=PATH_BUF_SIZE-2;
                memcpy(full,fr->fr_Drawer,dl); full[dl]='\0';
                if (dl>0 && full[dl-1]!='/' && full[dl-1]!=':') full[dl++]='/';
                strncpy(full+dl,fr->fr_ArgList[fi].wa_Name,PATH_BUF_SIZE-1-dl);
                full[PATH_BUF_SIZE-1]='\0';
                added+=playlist_add_path(full);
            }
        }
        if (!added && fr->fr_File && fr->fr_File[0]) {
            char full[PATH_BUF_SIZE];
            int dl=(int)strlen(fr->fr_Drawer);
            if (dl>PATH_BUF_SIZE-2) dl=PATH_BUF_SIZE-2;
            memcpy(full,fr->fr_Drawer,dl); full[dl]='\0';
            if (dl>0 && full[dl-1]!='/' && full[dl-1]!=':') full[dl++]='/';
            strncpy(full+dl,fr->fr_File,PATH_BUF_SIZE-1-dl);
            full[PATH_BUF_SIZE-1]='\0';
            added+=playlist_add_path(full);
        }
        if (added) {
            playlist_refresh();
            if (!g_playing && !g_paused) player_start(first);
        }
    }
    FreeAslRequest(fr);
}

static int gui_open(void)
{
    struct NewGadget ng;
    struct Gadget *prev;
    struct TagItem wt[12];
    UWORD bx,by,iw,fh,btn_w,btn_h,prog_y,btn_y;
    int i,win_h;

    memset(g_gad,0,sizeof(g_gad));
    g_scr=LockPubScreen(NULL); if (!g_scr) return 0;
    g_vi=GetVisualInfoA(g_scr,NULL);
    if (!g_vi) { UnlockPubScreen(NULL,g_scr); g_scr=NULL; return 0; }

    fh=g_scr->Font->ta_YSize;
    bx=g_scr->WBorLeft;
    by=g_scr->WBorTop+fh+1;
    iw=WIN_WIDTH-bx-g_scr->WBorRight;
    g_th=fh+2;
    g_tx=bx+2;
    g_ty[0]=by+2; g_ty[1]=g_ty[0]+g_th; g_ty[2]=g_ty[1]+g_th;
    prog_y=g_ty[2]+g_th+2;
    btn_y=prog_y+14;
    btn_w=(iw-7*2)/8;
    btn_h=12;
    win_h=btn_y+btn_h+g_scr->WBorBottom+4;

    prev=CreateContext(&g_glist); if (!prev) goto fail;
    memset(&ng,0,sizeof(ng));
    ng.ng_LeftEdge=bx+2; ng.ng_TopEdge=prog_y;
    ng.ng_Width=iw-4; ng.ng_Height=12;
    ng.ng_TextAttr=g_scr->Font; ng.ng_GadgetID=GAD_PROGRESS;
    ng.ng_VisualInfo=g_vi;
    {
        struct TagItem st[]={{GTSL_Min,0},{GTSL_Max,PROGRESS_MAX},{GTSL_Level,0},{GTSL_MaxLevelLen,5},{TAG_END,0}};
        prev=g_gad[GAD_PROGRESS]=CreateGadgetA(SLIDER_KIND,prev,&ng,st);
    }
    if (!prev) goto fail;

    {
        static const UWORD ids[8]={GAD_PREV,GAD_STOP,GAD_PLAY,GAD_PAUSE,GAD_NEXT,GAD_OPEN,GAD_PLS,GAD_LOOP};
        for (i=0;i<8;i++) {
            UWORD id=ids[i];
            ng.ng_LeftEdge=bx+2+i*(btn_w+2);
            ng.ng_TopEdge=btn_y; ng.ng_Width=btn_w; ng.ng_Height=btn_h;
            ng.ng_GadgetText=NULL; ng.ng_TextAttr=g_scr->Font;
            ng.ng_GadgetID=id; ng.ng_Flags=0; ng.ng_VisualInfo=g_vi;
            prev=g_gad[id]=CreateGadgetA(BUTTON_KIND,prev,&ng,NULL);
            if (!prev) goto fail;
        }
    }

    wt[0].ti_Tag=WA_Left; wt[0].ti_Data=40;
    wt[1].ti_Tag=WA_Top; wt[1].ti_Data=40;
    wt[2].ti_Tag=WA_Width; wt[2].ti_Data=WIN_WIDTH;
    wt[3].ti_Tag=WA_Height; wt[3].ti_Data=win_h;
    wt[4].ti_Tag=WA_Title; wt[4].ti_Data=(ULONG)"ZZVGMPlay v1.0 - (c) Xanxi 2026";
    wt[5].ti_Tag=WA_Gadgets; wt[5].ti_Data=(ULONG)g_glist;
    wt[6].ti_Tag=WA_Flags; wt[6].ti_Data=WFLG_DRAGBAR|WFLG_CLOSEGADGET|WFLG_DEPTHGADGET|WFLG_ACTIVATE|WFLG_SMART_REFRESH;
    wt[7].ti_Tag=WA_IDCMP; wt[7].ti_Data=IDCMP_CLOSEWINDOW|IDCMP_GADGETUP|IDCMP_RAWKEY|IDCMP_REFRESHWINDOW|IDCMP_MENUPICK;
    wt[8].ti_Tag=WA_PubScreen; wt[8].ti_Data=(ULONG)g_scr;
    wt[9].ti_Tag=TAG_END; wt[9].ti_Data=0;
    g_win=OpenWindowTagList(NULL,wt);
    if (!g_win) goto fail;
    g_main_menu=CreateMenusA(g_main_menu_defs,NULL);
    if (!g_main_menu) goto fail;
    if (!LayoutMenusA(g_main_menu,g_vi,NULL)) goto fail;
    if (!SetMenuStrip(g_win,g_main_menu)) goto fail;
    g_menu_attached=1;
    g_iw=g_win->Width-g_win->BorderLeft-g_win->BorderRight;
    GT_RefreshWindow(g_win,NULL);
    draw_icons();
    /* Keep the public screen locked while its font/visual info is in use. */
    return 1;
fail:
    if (g_win) {
        if (g_menu_attached) { ClearMenuStrip(g_win); g_menu_attached=0; }
        CloseWindow(g_win); g_win=NULL;
    }
    if (g_main_menu) { FreeMenus(g_main_menu); g_main_menu=NULL; }
    if (g_glist) { FreeGadgets(g_glist); g_glist=NULL; }
    if (g_vi) { FreeVisualInfo(g_vi); g_vi=NULL; }
    UnlockPubScreen(NULL,g_scr); g_scr=NULL;
    return 0;
}
static void gui_close(void)
{
    exit_trace("gui: closing playlist window");
    playlist_close();
    exit_trace("gui: playlist window disposed");
    if (g_win) {
        if (g_menu_attached) { ClearMenuStrip(g_win); g_menu_attached=0; }
        exit_trace("gui: menu detached");
        CloseWindow(g_win); g_win=NULL;
    }
    exit_trace("gui: main window closed");
    if (g_main_menu) { FreeMenus(g_main_menu); g_main_menu=NULL; }
    exit_trace("gui: menus freed");
    if (g_glist) { FreeGadgets(g_glist); g_glist=NULL; }
    exit_trace("gui: gadgets freed");
    if (g_vi) { FreeVisualInfo(g_vi); g_vi=NULL; }
    exit_trace("gui: visual info freed");
    if (g_scr) { UnlockPubScreen(NULL,g_scr); g_scr=NULL; }
    exit_trace("gui: screen unlocked");
}

static void handle_main_idcmp(void)
{
    struct IntuiMessage *m;
    while (g_win && (m=(struct IntuiMessage *)GT_GetIMsg(g_win->UserPort))) {
        ULONG cl=m->Class;
        UWORD code=m->Code;
        struct Gadget *gd=(struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);

        if (cl==IDCMP_CLOSEWINDOW) { g_quit=1; break; }
        if (cl==IDCMP_MENUPICK) {
            /* Process even multiple selections from a single menu drag. */
            while (code != MENUNULL) {
                struct MenuItem *item = ItemAddress(g_main_menu, code);
                UWORD next = item ? item->NextSelect : MENUNULL;
                if (item && MENUNUM(code)==0) {
                    switch (ITEMNUM(code)) {
                    case 0: choose_default_drawer(); break;
                    case 1: clear_default_drawer(); break;
                    }
                } else if (item && MENUNUM(code)==1 && ITEMNUM(code)==0) {
                    /* Intuition toggles the checkmark; mirror its state. */
                    g_debug_counters = (item->Flags & CHECKED) != 0;
                }
                code = next;
            }
            update_display();
            continue;
        }
        if (cl==IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(g_win); GT_EndRefresh(g_win,TRUE);
            memset(g_row_cache,0,sizeof(g_row_cache));
            g_last_slider=0xFFFFFFFFUL;
            draw_icons(); update_display();
            continue;
        }
        if (cl==IDCMP_GADGETUP && gd) {
            switch (gd->GadgetID) {
            case GAD_PREV:
                if (g_track>0) player_start(g_track-1);
                else if (g_track>=0) player_start(g_track);
                break;
            case GAD_STOP:
                stopplay(); break;
            case GAD_PLAY:
                if (g_paused) player_resume();
                else if (!g_playing && g_pcount>0) player_start((g_track>=0)?g_track:0);
                break;
            case GAD_PAUSE:
                if (g_paused) player_resume(); else player_pause();
                break;
            case GAD_NEXT:
                if (g_track+1<g_pcount) player_start(g_track+1);
                break;
            case GAD_OPEN:
                open_files(); break;
            case GAD_PLS:
                if (g_plswin) playlist_close(); else playlist_open();
                break;
            case GAD_LOOP:
                g_repeat=!g_repeat;
                /* Repeat is a GUI playlist mode; never enable Core1 VGM looping. */
                if (g_core_up) shput(VSH_FLAGS,(g_is_vgz?ZZVGM_FLAG_GZIP:0));
                status_set(g_repeat?"Repeat playlist on":"Repeat playlist off");
                break;
            }
            if (gd->GadgetID>=GAD_PREV && gd->GadgetID<=GAD_LOOP) draw_icons();
            update_display();
        }
        if (cl==IDCMP_RAWKEY && !(code&0x80)) {
            switch (code) {
            case 0x40: if (g_paused) player_resume(); else if (g_playing) player_pause(); else if (g_pcount) player_start((g_track>=0)?g_track:0); break;
            case 0x45: stopplay(); break;
            case 0x4F: if (g_track>0) player_start(g_track-1); break;
            case 0x4E: if (g_track+1<g_pcount) player_start(g_track+1); break;
            }
        }
    }
}

static void fatal_msg(const char *msg)
{
    if (IntuitionBase) {
        struct EasyStruct es;
        es.es_StructSize=sizeof(struct EasyStruct); es.es_Flags=0;
        es.es_Title=(UBYTE *)"ZZVGMPlay"; es.es_TextFormat=(UBYTE *)msg;
        es.es_GadgetFormat=(UBYTE *)"OK";
        EasyRequestArgs(NULL,&es,NULL,NULL);
    }
    printf("ZZVGMPlay: %s\n",msg);
}

int main(int argc, char *argv[])
{
    struct Library *ExpLib=NULL;
    struct ConfigDev *cd;
    ULONG sig,sigmask;
    int i;

    IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",37);
    GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",37);
    GadToolsBase=OpenLibrary("gadtools.library",37);
    AslBase=OpenLibrary("asl.library",37);
    if (!IntuitionBase||!GfxBase||!GadToolsBase||!AslBase) { fatal_msg("Cannot open OS3 GUI libraries"); return 20; }
    ExpLib=OpenLibrary("expansion.library",37);
    if (!ExpLib) { fatal_msg("Cannot open expansion.library"); return 20; }

    cd=FindConfigDev(NULL,ZZ9000_MANUF,ZZ9000_PROD_AX);
    if (!cd) cd=FindConfigDev(NULL,ZZ9000_MANUF,ZZ9000_PROD);
    if (!cd) { fatal_msg("ZZ9000 not found"); CloseLibrary(ExpLib); return 20; }
    g_board=(UBYTE *)cd->cd_BoardAddr;
    g_fb=g_board+MNT_FB_BASE;
    g_sh=(volatile ULONG *)(g_fb+ZZVGM_SHARED_FB_OFFSET);
    g_ring=(volatile UBYTE *)(g_fb+ZZVGM_PCM_FB_OFFSET);
    g_upload=g_fb+ZZVGM_UPLOAD_FB_OFFSET;

    init_btn_images();
    if (!ahi_open()) { fatal_msg("Cannot open ahi.device"); CloseLibrary(ExpLib); return 10; }
    load_last_drawer();
    load_default_drawer();
    if (!gui_open()) { fatal_msg("Cannot open GadTools window"); ahi_close(); CloseLibrary(ExpLib); return 20; }

    strcpy(g_dispname,"ZZVGMPlay / Core1");
    status_set("Ready - VGM/VGZ");

    /* CLI filenames are accepted exactly like ZZPlayGUI. */
    if (argc>1) {
        for (i=1;i<argc && g_pcount<MAX_PLAYLIST;i++) playlist_add_path(argv[i]);
    }
    update_display();
    if (g_pcount>0) player_start(0);

    while (!g_quit) {
        /* PLAYING: WaitIO inside audio_tick() paces this loop exactly like
         * the latest ZZSIDPlay and ZZMIDIPlay. Idle/paused: sleep on GUI
         * signals only; the AHI reply-port is not polled as a scheduler. */
        if (g_playing && !g_paused) audio_tick();

        handle_main_idcmp();
        if (g_quit) break;
        if (g_plswin) playlist_handle();

        if ((!g_playing || g_paused) && !g_quit) {
            sigmask=(1UL<<g_win->UserPort->mp_SigBit)|SIGBREAKF_CTRL_C;
            if (g_plswin) sigmask|=(1UL<<g_plswin->UserPort->mp_SigBit);
            sig=Wait(sigmask);
            if (sig&SIGBREAKF_CTRL_C) g_quit=1;
        }
    }

    /* Diagnostic trace covers the complete 68k shutdown, not just Core1. */
    g_exit_trace_file=Open((STRPTR)"T:ZZVGMPlay_exit.log",MODE_NEWFILE);
    if (!g_exit_trace_file)
        g_exit_trace_file=Open((STRPTR)"RAM:ZZVGMPlay_exit.log",MODE_NEWFILE);
    exit_trace("00: shutdown entered");
    stopplay();
    exit_trace("01: audio stopped and Core1 stop requested");
    gui_close();
    exit_trace("02: GUI windows, menus and gadgets disposed");
    for (i=0;i<g_pcount;i++) {
        if (g_playlist[i]) { FreeMem(g_playlist[i],PATH_BUF_SIZE); g_playlist[i]=NULL; }
    }
    g_pcount=0;
    exit_trace("03: playlist buffers freed");
    ahi_close();
    exit_trace("04: AHI fully closed");
    if (ExpLib) { CloseLibrary(ExpLib); ExpLib=NULL; }
    exit_trace("05: expansion.library closed");
    if (AslBase) { CloseLibrary(AslBase); AslBase=NULL; }
    exit_trace("06: asl.library closed");
    if (GadToolsBase) { CloseLibrary(GadToolsBase); GadToolsBase=NULL; }
    exit_trace("07: gadtools.library closed");
    if (GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase=NULL; }
    exit_trace("08: graphics.library closed");
    if (IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase=NULL; }
    exit_trace("09: intuition.library closed");
    exit_trace("10: returning from main");
    if (g_exit_trace_file) { Close(g_exit_trace_file); g_exit_trace_file=0; }
    return 0;
}
