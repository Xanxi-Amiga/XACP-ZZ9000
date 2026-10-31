/*
 * ZZPicoDrive Master System Edition - Sega Master System emulator for the MNT ZZ9000.
 *
 * The 68k host runs this launcher; the emulation itself runs on the ZZ9000's
 * ARM coprocessor and communicates over Zorro through XACP-compatible firmware.
 * The 68k opens a 32-bit RTG (Picasso96) screen with triple buffering, hands each
 * free buffer to the ARM and flips via deferred PAN. The ARM renders straight into
 * the RTG buffer, so the host CPU stays almost free.
 *
 * Features: launch GUI (ROM / region / audio / per-player controllers), two audio
 * backends (AHI and direct Paula DMA), keyboard / CD32-lowlevel / DB9 controllers,
 * two players, SRAM and savestates, in-game hotkeys.
 *
 * Usage:
 *   ZZPicoDrive3e_INPUT                          (no argument: opens the GUI)
 *   ZZPicoDrive3e_INPUT rom.bin [blob.bin] [region] [flags]
 *   region: auto | eur | usa | jap
 *   The ARM blob is embedded in the executable; a blob file, if present, overrides it.
 *
 * Build:
 *   m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign \
 *     -o ZZPicoDrive3e_INPUT ZZPicoDrive3e_INPUT.c blob_data.c -lamiga
 *
 * Port by Xanxi, 2026.  Emulation core: PicoDrive (notaz).  ASCII-only source.
 */

#include <proto/Picasso96.h>
#include <exec/types.h>
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
unsigned long __stack = 100000UL;   /* pile garantie 100Ko (shell ET Workbench) : evite le reset par debordement */
struct Library *P96Base = NULL;
struct Library *GfxBase_ = NULL;
struct Library *LowLevelBase = NULL;   /* lowlevel.library : ReadJoyPort (mode CD32/DB9) */
struct Library *GadToolsBase = NULL;   /* GUI de lancement */
struct Library *AslBase = NULL;        /* requester fichier ROM */

/* sortie debug : active en mode CLI, muette en mode GUI (pas de fenetre console) */
static int g_quiet = 0;
static void qprintf(const char *fmt, ...)
{
    va_list ap;
    if(g_quiet) return;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
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
static void zz_ahi_service(void){
    struct AHIRequest *done;
    while((done=(struct AHIRequest*)GetMsg(ahi_mp))){
        if(done==ahi_req[0]) prepare_and_send(0, ahi_req[1]);
        else                 prepare_and_send(1, ahi_req[0]);
    }
}
static void zz_ahi_stop(void){
    int i;
    printf("AHI: refills=%lu underruns=%lu\n", ahi_refill_count, ahi_underrun);
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
    { ULONG prate = pa_ntsc ? PA_NTSC_HZ : au_rate;
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
#define RK_RET    0x44
#define RK_ENT    0x43
#define RK_ESC    0x45
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
                if(jp&CP_YELLOW)pm|=ZP_MODE;
                if(jp&CP_START) pm|=ZP_START;
            }
        }
        if(!ll_ok){   /* hardware direct (pads que lowlevel ignore) */
            UWORD jd=(port==0)?HW_JOY0DAT:HW_JOY1DAT;
            UBYTE pra=HW_CIAAPRA;
            int right=(jd&0x0002)?1:0, left=(jd&0x0200)?1:0;
            int down=((jd^(jd>>1))&0x0001)?1:0, up=((jd^(jd>>1))&0x0100)?1:0;
            int fire=(port==0)?((pra&0x40)?0:1):((pra&0x80)?0:1);
            /* 2e bouton via les broches POT : POTGO en sortie haute (0xFF00) pour
               que le bouton puisse tirer la broche a la masse, puis lecture POTINP.
               bit10 (0x0400)=P0 btn2, bit14 (0x4000)=P1 btn2, actifs bas. */
            { static int potcfg=0; UWORD pin;
              if(!potcfg){ HW_POTGO=0xFF00; potcfg=1; }
              pin=HW_POTINP;
              { int fire2=(port==0)?((pin&0x0400)?0:1):((pin&0x4000)?0:1);
                if(fire2) pm|=ZP_B; } }
            if(up)pm|=ZP_UP; if(down)pm|=ZP_DOWN; if(left)pm|=ZP_LEFT; if(right)pm|=ZP_RIGHT;
            if(fire)pm|=ZP_A;
        }
        /* Start clavier de secours (Entree pour P1, Tab pour P2) */
        if(keyset==0){ if(key_down[RK_RET]||key_down[RK_ENT]) pm|=ZP_START; }
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
    }
    /* joueur 2 en mode clavier n'existe pas : il doit avoir une manette (Tab = Start secours) */
    return pm;
}

/* ---- SAUVEGARDES SRAM (68k) : fichier ROMNAME.srm <-> buffer DDR <-> Core1 ---- */
#define SAVE_MAX_68K  (512UL*1024UL)
static void make_srm_name(const char *rom, char *out)
{
    int i, dot=-1, len=0;
    for(i=0; rom[i] && i<250; i++){ out[i]=rom[i]; if(rom[i]=='.') dot=i; len=i+1; }
    if(dot>=0) len=dot;
    out[len]='.'; out[len+1]='s'; out[len+2]='r'; out[len+3]='m'; out[len+4]=0;
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
    for(to=0; to<2000000UL; to++){ if(rd32(shared,SH_SAVE_ACK)==seq) break; }
    printf("SRAM: charge %ld octets (%s)\n", n, srm);
}
static void sram_save(volatile UBYTE *shared, volatile UBYTE *fb, const char *romname)
{
    char srm[256]; BPTR fh; ULONG seq, to, sz, st;
    volatile UBYTE *ddr=(volatile UBYTE*)((ULONG)fb + (SAVE_BUF_ARM - 0x00200000UL));
    wr32(shared, SH_SAVE_CMD, SAVE_CMD_SRAM_SAVE);
    seq=rd32(shared, SH_SAVE_SEQ)+1; wr32(shared, SH_SAVE_SEQ, seq);
    for(to=0; to<2000000UL; to++){ if(rd32(shared,SH_SAVE_ACK)==seq) break; }
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
    for(to=0; to<8000000UL; to++){ if(rd32(shared,SH_SAVE_ACK)==seq) break; }
    status=rd32(shared, SH_SAVE_STATUS); sz=rd32(shared, SH_SAVE_SIZE);
    if(status!=SAVE_STATUS_OK || sz==0 || sz>SAVE_MAX_68K){
        printf("STATE: save slot %d echoue (err=%lu)\n", slot, rd32(shared,SH_SAVE_ERR)); return; }
    CacheClearU();
    make_st_name(romname, slot, st);
    fh=Open((STRPTR)st, MODE_NEWFILE);
    if(!fh){ printf("STATE: ecriture %s impossible\n", st); return; }
    Write(fh, (APTR)ddr, (LONG)sz);
    Close(fh);
    printf("STATE: sauve slot %d (%lu octets) -> %s\n", slot, sz, st);
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
    wr32(shared, SH_SAVE_SIZE, (ULONG)n);
    wr32(shared, SH_SAVE_SLOT, (ULONG)slot);
    wr32(shared, SH_SAVE_CMD, SAVE_CMD_STATE_LOAD);
    seq=rd32(shared, SH_SAVE_SEQ)+1; wr32(shared, SH_SAVE_SEQ, seq);
    for(to=0; to<8000000UL; to++){ if(rd32(shared,SH_SAVE_ACK)==seq) break; }
    status=rd32(shared, SH_SAVE_STATUS);
    printf("STATE: charge slot %d (%ld octets) status=%lu\n", slot, n, status);
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

static int controllers_conflict(const ZP_Config *cfg);   /* defini plus bas, utilise ici */
static int run_picodrive(const ZP_Config *cfg)
{
    struct ConfigDev *cd;
    volatile UBYTE *board=NULL,*fb=NULL,*shared=NULL;
    BPTR fh=0; UBYTE *rombuf=NULL,*blobbuf=NULL; int blob_alloced=0;
    LONG rom_size=0,blob_size=0; ULONG crc_68k=0; int rc=0,i;

    struct Screen *my_scr=NULL; struct Window *win=NULL;
    struct ScreenBuffer *sb[3]={NULL,NULL,NULL};
    struct BitMap *man_bm[3]={NULL,NULL,NULL};   /* backbuffers manuels p96AllocBitMap (compat P96 recent) */
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

    printf("ZZPicoDrive VTC+P96compat - OpenScreen strict/C/D + VRAM backbuffer fallback\n");
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
    if(!cd){ printf("ZZ9000 not found\n"); return 10; }
    board=(volatile UBYTE*)cd->cd_BoardAddr; fb=board+MNT_FB_BASE; shared=fb+FB_SHARED;
    printf("board=0x%08lX fb=0x%08lX shared=0x%08lX\n",(ULONG)board,(ULONG)fb,(ULONG)shared);

    /* ROM */
    fh=Open((STRPTR)cfg->rom_path,MODE_OLDFILE);
    if(!fh){ printf("Cannot open ROM\n"); rc=10; goto done; }
    Seek(fh,0,OFFSET_END); rom_size=(LONG)Seek(fh,0,OFFSET_END); Seek(fh,0,OFFSET_BEGINNING);
    if(rom_size<=0){ Close(fh); rc=10; goto done; }
    rombuf=(UBYTE*)AllocMem((ULONG)rom_size+ROM_PAD,MEMF_PUBLIC|MEMF_CLEAR);
    if(!rombuf||Read(fh,rombuf,rom_size)!=rom_size){ printf("ROM read fail\n"); Close(fh); rc=10; goto done; }
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
    } else {
        blobbuf=(UBYTE*)g_blob_data; blob_size=(LONG)g_blob_size; blob_alloced=0;
        printf("Blob: %ld bytes (embarque dans l'executable)\n",blob_size);
    }

    crc_68k=crc32_buf(rombuf,(ULONG)rom_size);
    copy_uword(fb+FB_ROM,rombuf,rom_size);
    { volatile UWORD *pad=(volatile UWORD*)(fb+FB_ROM+(ULONG)rom_size);
      for(i=0;i<(int)(ROM_PAD/2);i++) pad[i]=0; }
    copy_uword(fb+FB_BLOB,blobbuf,blob_size);

    { volatile UWORD *p=(volatile UWORD*)shared; for(i=0;i<320;i++) p[i]=0; }
    ZZ_WR(board,REG_ARM_RUN_HI,(UWORD)(ARM_BLOB>>16));
    ZZ_WR(board,REG_ARM_RUN_LO,(UWORD)(ARM_BLOB&0xFFFF));

    { LONG t=250; while(t-->0 && rd32(shared,SH_MAGIC)!=ZZPICO_MAGIC) Delay(1); }
    if(rd32(shared,SH_MAGIC)!=ZZPICO_MAGIC){ printf("No MAGIC\n"); rc=10; goto done; }
    printf("Core1 READY.\n");

    if(!send_cmd(shared,ZP_CMD_INIT,250)){ printf("INIT timeout\n"); rc=12; goto stop; }
    wr32(shared,SH_ROM_ADDR,ARM_ROM); wr32(shared,SH_ROM_SIZE,(ULONG)rom_size);
    wr32(shared,SH_ROM_CRC_68K,crc_68k);
    if(!send_cmd(shared,ZP_CMD_LOAD_ROM,250)){ printf("LOAD_ROM timeout\n"); rc=13; goto stop; }
    if(rd32(shared,SH_ERROR)!=0){ printf("LOAD_ROM err=%lu\n",rd32(shared,SH_ERROR)); rc=14; goto stop; }
    printf("ROM loaded (CRC_ARM=0x%08lX)\n",rd32(shared,SH_ROM_CRC_ARM));
    /* VTC : region posee AVANT cmd_start (le blob l'applique avant PicoReset) */
    wr32(shared,SH_REGION_IN,region_in);
    if(!send_cmd(shared,ZP_CMD_START,250)){ printf("START timeout\n"); rc=15; goto stop; }
    sram_load(shared, fb, cfg->rom_path);   /* charge ROMNAME.srm si present */
    if(rd32(shared,SH_STATUS)!=0x02){ printf("Not RUNNING\n"); rc=16; goto stop; }
    /* pal effectif resolu par PicoDrive -> pacing 50/60 */
    pal_out=rd32(shared,SH_PAL_OUT); target_hz=pal_out?50UL:60UL;
    printf("region=%s->%s  target=%luHz  regionOverride=%lu\n",
           region_label, pal_out?"PAL":"NTSC", target_hz, rd32(shared,SH_REGION_EFF));
    printf("RUNNING. Opening 32-bit screen + double buffer...\n");

    have_timer=timer_open();
    if(!have_timer) printf("(timer.device indisponible : pas de mesure FPS)\n");
    /* HW_VBLANK_PACING : sonde le registre vblank. Si le bit bat -> VBLANK_HW
       (tear-free), sinon repli TIMER. Jamais bloquant. uncapped -> ni l'un ni
       l'autre (benchmark libre). */
    vbl_ok = (!uncapped && have_timer) ? probe_vblank(board) : 0;
    pacing_mode = uncapped ? "UNCAPPED" : (vbl_ok ? "VBLANK_HW" : "TIMER");
    printf("pacing_mode=%s (vblank probe=%s)\n",
           pacing_mode, vbl_ok?"OK":(uncapped?"skip":"no-signal->TIMER"));

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
    if(!IntuitionBase||!GfxBase_||!P96Base){ printf("libs missing (P96?)\n"); rc=17; goto stop; }

    modeid=p96BestModeIDTags(P96BIDTAG_NominalWidth,(ULONG)IMG_W,
        P96BIDTAG_NominalHeight,(ULONG)IMG_H,P96BIDTAG_Depth,32,TAG_DONE);
    if(modeid==(ULONG)INVALID_ID){ printf("No 32-bit RTG mode\n"); rc=18; goto stop; }
    /* P96_OPENSCREEN_COMPAT : strict d'abord (comportement historique, aucune
       regression sur les configs qui marchaient), sinon methode C permissive,
       sinon methode D, sinon abandon propre. Certains P96/iComp recents (OS 3.2)
       refusent la combinaison de tags stricte alors que le mode est valide.
       Ne JAMAIS juger le mode sur les champs legacy du screen (Width/Height/Depth) :
       la validation se fait plus bas sur les bitmaps P96 (lecture non-lockee). */
    my_scr=OpenScreenTags(NULL,SA_DisplayID,modeid,SA_Width,(ULONG)IMG_W,
        SA_Height,(ULONG)IMG_H,SA_Depth,32,SA_Quiet,TRUE,SA_ShowTitle,FALSE,
        SA_Type,CUSTOMSCREEN,TAG_DONE);
    if(!my_scr){
        printf("OpenScreen strict failed -> trying permissive (method C)\n");
        my_scr=OpenScreenTags(NULL,SA_DisplayID,modeid,SA_Type,CUSTOMSCREEN,
            SA_Quiet,TRUE,TAG_DONE);
    }
    if(!my_scr){
        printf("method C failed -> trying method D\n");
        my_scr=OpenScreenTags(NULL,SA_DisplayID,modeid,SA_Type,CUSTOMSCREEN,TAG_DONE);
    }
    if(!my_scr){ printf("OpenScreen failed (strict/C/D). Please run ZZP96Test and report.\n"); rc=18; goto stop; }
    win=(struct Window*)OpenWindowTags(NULL,WA_Left,0,WA_Top,0,WA_Width,(ULONG)IMG_W,
        WA_Height,(ULONG)IMG_H,WA_Flags,WFLG_BACKDROP|WFLG_BORDERLESS|WFLG_RMBTRAP|WFLG_ACTIVATE,
        WA_IDCMP,IDCMP_RAWKEY|IDCMP_MOUSEBUTTONS,WA_CustomScreen,(ULONG)my_scr,TAG_DONE);
    if(!win){ printf("OpenWindow failed\n"); rc=18; goto closescr; }

    /* cacher le pointeur souris sur l'ecran PicoDrive (sprite blank en Chip RAM) */
    g_blankspr=(UWORD*)AllocMem(12L, MEMF_CHIP|MEMF_CLEAR);   /* 6 words a 0 = invisible */
    if(g_blankspr) SetPointer(win, g_blankspr, 1, 16, 0, 0);

    /* TRIPLE BUFFER : sb[0]=bitmap ecran, sb[1] et sb[2]=buffers de rendu.
       3 buffers -> Core1 rend en avance dans un buffer libre pendant qu'un autre
       attend son flip (pipeline) -> 50 fps possible malgre le PAN differe. */
    dbuf_port=(struct MsgPort*)CreateMsgPort();
    sb[0]=AllocScreenBuffer(my_scr,NULL,SB_SCREEN_BITMAP);
    if(!sb[0]){ printf("AllocScreenBuffer(screen bitmap) failed\n"); rc=19; goto freebuf; }
    sb[1]=AllocScreenBuffer(my_scr,NULL,0);
    sb[2]=AllocScreenBuffer(my_scr,NULL,0);

    /* P96_BACKBUFFER_COMPAT : buf[0] = bitmap ecran (toujours VRAM). buf[1]/buf[2] :
       ScreenBuffers si leurs bitmaps sont de vrais buffers VRAM (mem!=0 ET mem>=fb),
       SINON allocation manuelle p96AllocBitMap avec le bitmap ECRAN 32 bits comme
       friend (PAS le Workbench, qui peut etre en 16 bits). Sur certains P96/iComp
       recents, AllocScreenBuffer secondaire revient sans VRAM (mem=0) : c'est ce
       cas que couvre l'allocation manuelle. Le deferred-PAN ne depend que de
       l'offset VRAM, il fonctionne a l'identique avec des bitmaps manuels.
       Lecture des attributs NON-LOCKEE : p96LockBitMap est interdit (gele ces
       configs, meme sur bitmap off-screen). Aucun fallback Planes[0] : si mem==0
       le buffer est invalide (Planes[0] n'est pas un framebuffer chunky VRAM). */
    { int k; int need_manual=0;
      struct BitMap *rbm[3];
      ULONG bprv[3];
      rbm[0]=sb[0]->sb_BitMap;
      rbm[1]=sb[1]?sb[1]->sb_BitMap:NULL;
      rbm[2]=sb[2]?sb[2]->sb_BitMap:NULL;
      for(k=1;k<3;k++){
        ULONG mem = rbm[k] ? p96GetBitMapAttr(rbm[k],P96BMA_MEMORY) : 0;
        if(mem==0 || mem<(ULONG)fb){ need_manual=1; break; }
      }
      if(need_manual){
        struct BitMap *friend_bm=my_scr->RastPort.BitMap;   /* ecran 32 bits */
        ULONG rgbf=p96GetBitMapAttr(friend_bm,P96BMA_RGBFORMAT);
        printf("Secondary ScreenBuffers not in VRAM -> manual p96AllocBitMap (P96 compat)\n");
        if(sb[1]){ FreeScreenBuffer(my_scr,sb[1]); sb[1]=NULL; }
        if(sb[2]){ FreeScreenBuffer(my_scr,sb[2]); sb[2]=NULL; }
        man_bm[1]=p96AllocBitMap((ULONG)IMG_W,(ULONG)IMG_H,32,
                                 BMF_DISPLAYABLE|BMF_CLEAR,friend_bm,(RGBFTYPE)rgbf);
        man_bm[2]=p96AllocBitMap((ULONG)IMG_W,(ULONG)IMG_H,32,
                                 BMF_DISPLAYABLE|BMF_CLEAR,friend_bm,(RGBFTYPE)rgbf);
        if(!man_bm[1]||!man_bm[2]){
            printf("p96AllocBitMap failed. Please run ZZP96Test and report.\n"); rc=19; goto freebuf; }
        rbm[1]=man_bm[1]; rbm[2]=man_bm[2];
      } else {
        if(!sb[1]||!sb[2]){ printf("AllocScreenBuffer failed\n"); rc=19; goto freebuf; }
      }
      /* validation NON-LOCKEE des 3 bitmaps de rendu : mem!=0, mem>=fb (VRAM
         ZZ9000), bpr suffisant, adresses distinctes, pitch homogene. Sinon
         abandon PROPRE (jamais de mono-buffer : tearing + casse l'archi PAN). */
      for(k=0;k<3;k++){
        ULONG bpr=p96GetBitMapAttr(rbm[k],P96BMA_BYTESPERROW);
        ULONG mem=p96GetBitMapAttr(rbm[k],P96BMA_MEMORY);
        if(mem==0 || mem<(ULONG)fb){
            printf("ERROR: buf[%d] not in VRAM (mem=0x%08lX). Please run ZZP96Test.\n",k,mem);
            rc=19; goto freebuf; }
        if(bpr<(ULONG)IMG_W*4UL){
            printf("ERROR: buf[%d] bpr %lu < %lu. Please run ZZP96Test.\n",
                   k,bpr,(ULONG)IMG_W*4UL); rc=19; goto freebuf; }
        bprv[k]=bpr; buf_mem[k]=mem; buf_arm[k]=CPU_TO_ARM(mem,fb);
        buf_off[k]=(ULONG)mem-(ULONG)fb;
        printf("buf[%d] mem=0x%08lX arm=0x%08lX bpr=%lu%s\n",k,mem,buf_arm[k],bpr,
               (k>0 && man_bm[k])?" (manual)":"");
      }
      if(bprv[0]!=bprv[1] || bprv[0]!=bprv[2]){
        printf("ERROR: pitch mismatch (%lu/%lu/%lu). Please run ZZP96Test.\n",
               bprv[0],bprv[1],bprv[2]); rc=19; goto freebuf; }
      pitch=bprv[0];
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
            pa_ntsc = (region_in==RGN_USA || region_in==RGN_JAPN) ? 1 : 0;
            if(pa_init(g_task_shared, g_task_ring)){ pa_start(); g_pa_active=1; g_audio_on=1;
                printf("AUDIO: backend Paula DMA direct\n"); }
            else { pa_stop(); printf("AUDIO: init Paula KO -> pas de son\n"); }
        } else {
            /* backend AHI (defaut) ou None (silence) : tache separee */
            g_done_sig=AllocSignal(-1L);
            if(g_done_sig>=0){
                g_audio_proc=(struct Process*)CreateNewProcTags(
                    NP_Entry,(ULONG)audio_task, NP_Name,(ULONG)"ZZPicoAudio",
                    /* priorite HAUTE : la tache audio double-buffer doit preempter le
                       thread principal pour realimenter AHI a temps (sinon underrun
                       quand le 68k est charge, typiquement NTSC 60fps). Elle dort
                       (Wait) la plupart du temps -> priorite haute sans danger. */
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
      t_start=eclock_now(&efreq);
      wr32(shared, SH_FLIP_SEQ, 0);
      wr32(shared, SH_P96_BASE, buf_arm[render_idx]);   /* Core1 rend dans render_idx */
      while(running){
          WaitTOF();   /* dort jusqu'au vblank RTG : ZERO busy-wait, CPU rendu au systeme */
          if(g_pa_active) pa_service();   /* backend Paula : remplit le ring Chip sur WaitTOF */
          /* AUDIO : rien ici, l'AudioTask separee gere AHI -> pas de preemption du flip */

          /* (1) confirmer le flip en vol */
          if(flip_inflight){
              if(zz_pan_ack(board) != flip_ack_before){
                  front_idx = pending_idx;          /* buffer flippe -> affiche */
                  pending_idx = -1; flip_inflight = 0; frames++;
              } else {
                  ULONG now=eclock_now(NULL);
                  if(efreq && (now - t_flip) > efreq/10UL){
                      printf("PAN ACK timeout (ready=%lu)\n", flip_ready);
                      ZZ_WR(board, REG_ZZ_USER3, 2); running=0; rc=21;
                  }
              }
          }
          /* (2) prendre la derniere frame prete + armer son PAN */
          if(!flip_inflight){
              ULONG ready = rd32(shared, SH_FRAME_READY);
              if(ready != last_taken){
                  flip_ack_before = zz_pan_ack(board);
                  ZZ_WR(board, REG_ZZ_USER3, 1);                    /* arme UN PAN */
                  zz_deferred_pan_request(board, buf_off[render_idx], pitch);
                  pending_idx = render_idx;                         /* ce buffer sera affiche */
                  render_idx  = 3 - front_idx - pending_idx;        /* le 3e = libre -> Core1 */
                  wr32(shared, SH_P96_BASE, buf_arm[render_idx]);
                  wr32(shared, SH_FLIP_SEQ, ready);                 /* "pris, rends la suivante" */
                  flip_ready = ready; last_taken = ready; t_flip=eclock_now(NULL); flip_inflight=1;
              }
          }
          /* ---- INPUT clavier (mode 0) : non bloquant, cout quasi nul ---- */
          { struct IntuiMessage *m;
            while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
                ULONG cl=m->Class, code=m->Code, qual=m->Qualifier;
                ReplyMsg((struct Message*)m);
                if(cl==IDCMP_MOUSEBUTTONS){ running=0; continue; }   /* clic = quit (secours) */
                if(cl!=IDCMP_RAWKEY) continue;
                { int up=(code&0x80)?1:0, raw=(int)(code&0x7F);
                  if(g_input_log)
                      printf("RAWKEY code=0x%02lx base=0x%02lx %s qual=0x%04lx\n",
                             (ULONG)code,(ULONG)raw,up?"UP":"DOWN",(ULONG)qual);
                  if(raw<128) key_down[raw]=up?0:1;
                  if(!up){   /* commandes emulateur sur DOWN */
                      if(raw==RK_ESC){
                          if(qual&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)){ g_emu_cmd=EMU_CMD_QUIT; running=0; }
                          else g_emu_cmd=EMU_CMD_MENU;   /* menu emulateur (pas encore d'UI) */
                      } else if(raw==RK_P) g_emu_cmd=EMU_CMD_PAUSE;
                      else if(raw>=RK_F1 && raw<=RK_F5){
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
          wr32(shared, SH_EMU_CMD, g_emu_cmd);
          wr32(shared, SH_INPUT_SEQ, g_input_seq);
      }
      { ULONG total=eclock_now(NULL)-t_start;
        g_frames=frames; g_efreq=efreq;
        if(efreq>1000 && frames>0){
            ULONG efk=efreq/1000, el_ms=total/efk;
            g_fps10 = el_ms? (frames*10000UL)/el_ms : 0; g_el_ms=el_ms;
        } }
      g_rend10=0; g_flip10=0; g_rmin=0; g_rmax=0; g_r12=0; g_r14=0; g_r67=0;
      g_fmin=0; g_fmax=0; g_missed=0; g_vbmax=0; g_vbsum=0;
    }
    /* arret propre audio : Paula DMA direct (pa_stop) ou tache AHI selon le backend actif */
    if(g_pa_active){ pa_stop(); g_pa_active=0; g_audio_on=0; }
    else if(g_audio_on){
        g_audio_stop=1;
        if(g_audio_proc) Signal(&g_audio_proc->pr_Task, SIGBREAKF_CTRL_C);
        if(g_done_sig>=0){ Wait(1UL<<g_done_sig); FreeSignal(g_done_sig); g_done_sig=-1; }
        g_audio_on=0;
    }
    /* desactive le rendu direct avant de liberer les buffers */
    wr32(shared,SH_P96_ENABLE,0);
    /* IMPORTANT : restaurer le PAN d'origine (bitmap ecran sb[0]) AVANT de
       liberer. Intuition n'a jamais ete bascule (on a pane le firmware, pas
       ChangeScreenBuffer) : sa vue reste sb[0], on remet le scan-out dessus. */
    if(board) zz_pan(board, pan_orig);

    /* FIX STABILITE : arreter le BLOB (sort du game loop -> cesse rendu/emulation)
       AVANT de liberer les framebuffers ci-dessous. Sinon il ecrit encore dans la
       VRAM qu'on rend a l'OS -> corruption -> gel/reset intermittent a la sortie.
       send_cmd attend l'ack du blob avant de continuer. */
    if(shared){ wr32(shared,SH_P96_ENABLE,0); send_cmd(shared,ZP_CMD_STOP,250); }

freebuf:
    /* ordre fiche : re-PAN sur buf_off[0] fait en amont (sortie normale) ou PAN
       jamais modifie (abandon precoce), puis FreeScreenBuffer, puis p96FreeBitMap. */
    if(sb[0]) FreeScreenBuffer(my_scr,sb[0]);
    if(sb[1]) FreeScreenBuffer(my_scr,sb[1]);
    if(sb[2]) FreeScreenBuffer(my_scr,sb[2]);
    if(man_bm[1]){ p96FreeBitMap(man_bm[1]); man_bm[1]=NULL; }
    if(man_bm[2]){ p96FreeBitMap(man_bm[2]); man_bm[2]=NULL; }
    if(dbuf_port){ DeleteMsgPort(dbuf_port); dbuf_port=NULL; }
closewin:
    if(win && g_blankspr) ClearPointer(win);
    if(g_blankspr){ FreeMem(g_blankspr,12L); g_blankspr=NULL; }
    if(win){ CloseWindow(win); win=NULL; }
closescr:
    if(my_scr){ CloseScreen(my_scr); my_scr=NULL; }
stop:
    if(shared){ sram_save(shared, fb, cfg->rom_path);   /* sauve ROMNAME.srm si le jeu a une SRAM */
                wr32(shared,SH_P96_ENABLE,0); send_cmd(shared,ZP_CMD_STOP,250);
                printf("Core1 stopped.\n"); }
done:
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
    if(have_timer) timer_close();
    if(fh) Close(fh);
    if(P96Base) CloseLibrary(P96Base);
    if(LowLevelBase) CloseLibrary(LowLevelBase);
    if(rombuf) FreeMem(rombuf,(ULONG)rom_size+ROM_PAD);
    if(blob_alloced && blobbuf) FreeMem(blobbuf,(ULONG)blob_size);   /* pas de free si blob embarque */
    printf("Done (rc=%d).\n",rc);
    return rc;
}

/* ===================== parse CLI -> ZP_Config (backward compatible) ===================== */
static int parse_cli(int argc, char **argv, ZP_Config *cfg)
{
    int ai;
    if(argc<3){
        printf("Usage: %s <rom> <blob> [region] [flags]\n", argv[0]);
        printf("  region : auto|usa|jap|eur|jappal   (defaut auto)\n");
        printf("  flags  : kbd cd32 usb port=N inputlog silence\n");
        printf("  (sans argument = interface graphique)\n");
        return 0;
    }
    strncpy(cfg->rom_path,  argv[1], 255); cfg->rom_path[255]=0;
    strncpy(cfg->blob_path, argv[2], 255); cfg->blob_path[255]=0;
    if(argc>=4){
        const char *r=argv[3];
        if(!strcmp(r,"eur")||!strcmp(r,"jappal")) cfg->region=ZP_REGION_EUR_PAL;
        else if(!strcmp(r,"usa")) cfg->region=ZP_REGION_USA_NTSC;
        else if(!strcmp(r,"jap")) cfg->region=ZP_REGION_JAPAN_NTSC;
        else cfg->region=ZP_REGION_AUTO;
    }
    for(ai=4; ai<argc; ai++){
        if(!strcmp(argv[ai],"inputlog")) g_input_log=1;
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
    }
    return 1;
}

/* ===================== config par defaut + PROGDIR:ZZPicoDrive.cfg ===================== */
static void zp_config_defaults(ZP_Config *cfg)
{
    cfg->rom_path[0]=0;
    strcpy(cfg->blob_path, "zzpicodrive_p3w_2p.bin");
    cfg->region=ZP_REGION_AUTO;
    cfg->audio =ZP_AUDIO_AHI;
    cfg->input =ZP_INPUT_CD32_LOWLEVEL;
    cfg->port  =1;
    cfg->players=1;
    cfg->input2=ZP_INPUT_CD32_LOWLEVEL;
    cfg->port2 =0;                       /* joueur 2 sur l'autre port par defaut */
    cfg->show_fps=0;
}
static int cfg_app(char *b, int p, const char *s){ while(*s) b[p++]=*s++; return p; }
static void zp_config_save(const ZP_Config *cfg, const char *path)
{
    BPTR fh; char b[700]; int p=0;
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
    Write(fh, b, (LONG)p); Close(fh);
}
static void zp_config_load(ZP_Config *cfg, const char *path)
{
    BPTR fh; char buf[1200]; LONG n, i, ls;
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
static const char *TXT_ABOUT =
    "ZZPicoDrive Master System Edition brings Sega Master\n"
    "System emulation to classic Amigas equipped with an MNT\n"
    "ZZ9000. The emulation runs on the ZZ9000's ARM coprocessor\n"
    "and talks to the 68k over Zorro through XACP-compatible\n"
    "firmware, keeping the host CPU almost free.\n\n"
    "Port by Xanxi, 2026.   Emulation core: PicoDrive (notaz).";
static const char *TXT_CONTROLS =
    "CONTROLS\n\n"
    "Player 1: keyboard OR a controller on a chosen port.\n"
    "Player 2: a controller only, on a different port.\n\n"
    "Keyboard (Player 1):\n"
    "  Arrows = D-pad    Numpad 1/2 = Button 1/2    Enter = Pause\n\n"
    "Controller types:\n"
    "  DB9 hardware = joystick/pad on the real port (2 buttons)\n"
    "  CD32 / USB   = CD32 pad, or USB pad via Poseidon\n"
    "  CD32 pad: Blue = Button 1, Red = Button 2, Start = Pause\n"
    "  DB9 pad has no Start button - use Enter for Pause\n\n"
    "Pause is the console button on a real Master System:\n"
    "  Enter always works, even with a controller.";
static const char *TXT_OPTIONS =
    "OPTIONS\n\n"
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
    "Enter              Pause (console button)";
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
    struct Gadget *g_input2=NULL;
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
      /* Region */
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+46; ng.ng_Width=210; ng.ng_Height=14;
      ng.ng_GadgetText=(STRPTR)"Region:"; ng.ng_GadgetID=3;
      g_region=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_region, GTCY_Active,cfg->region, TAG_END); gc=g_region;
      /* Audio */
      ng.ng_TopEdge=top+64; ng.ng_GadgetText=(STRPTR)"Audio:"; ng.ng_GadgetID=4;
      g_audio=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_audio, GTCY_Active,cfg->audio, TAG_END); gc=g_audio;
      /* Player 1 (mode + port combines) */
      ng.ng_TopEdge=top+82; ng.ng_GadgetText=(STRPTR)"Player 1:"; ng.ng_GadgetID=5;
      g_input=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_p1,
                           GTCY_Active,p1_to_sel(cfg->input,cfg->port), TAG_END); gc=g_input;
      /* Player 2 (None = 1 joueur) */
      ng.ng_TopEdge=top+100; ng.ng_GadgetText=(STRPTR)"Player 2:"; ng.ng_GadgetID=6;
      g_input2=CreateGadget(CYCLE_KIND, gc, &ng, GTCY_Labels,(ULONG)gui_p2,
                            GTCY_Active,p2_to_sel(cfg->players,cfg->input2,cfg->port2), TAG_END); gc=g_input2;
      /* Start / Quit */
      ng.ng_LeftEdge=L; ng.ng_TopEdge=top+126; ng.ng_Width=100; ng.ng_Height=16;
      ng.ng_GadgetText=(STRPTR)"Start"; ng.ng_GadgetID=7;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      ng.ng_LeftEdge=L+120; ng.ng_GadgetText=(STRPTR)"Quit"; ng.ng_GadgetID=8;
      gc=CreateGadget(BUTTON_KIND, gc, &ng, TAG_END);
      if(!gc){ result=-1; goto guiclean; }

      win=(struct Window*)OpenWindowTags(NULL,
        WA_Title,(ULONG)"ZZPicoDrive Master System Edition 1.1",
        WA_Left,60, WA_Top,40, WA_Width,460, WA_Height,top+154,
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
                    case 2: {   /* Browse ROM via ASL */
                        struct FileRequester *fr=(struct FileRequester*)AllocAslRequest(ASL_FileRequest, NULL);
                        if(fr){
                            if(AslRequestTags(fr, ASLFR_TitleText,(ULONG)"Choisir une ROM Master System",
                                              ASLFR_Window,(ULONG)win, TAG_END)){
                                strncpy(cfg->rom_path, fr->fr_Drawer, 255); cfg->rom_path[255]=0;
                                AddPart((STRPTR)cfg->rom_path,(STRPTR)fr->fr_File, 256);
                                GT_SetGadgetAttrs(g_rom, win, NULL, GTTX_Text,(ULONG)cfg->rom_path, TAG_END);
                            }
                            FreeAslRequest(fr);
                        }
                    } break;
                    case 7:   /* Start */
                        if(!cfg->rom_path[0]){ DisplayBeep(pub); break; }
                        if(controllers_conflict(cfg)){
                            struct EasyStruct es;
                            es.es_StructSize=sizeof(struct EasyStruct); es.es_Flags=0;
                            es.es_Title=(STRPTR)"ZZPicoDrive";
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
                      if(ud==1L)       gui_req(win,"About ZZPicoDrive Master System Edition",TXT_ABOUT);
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
int main(int argc, char **argv)
{
    ZP_Config cfg;
    int rc;
    zp_config_defaults(&cfg);
    zp_config_load(&cfg, "PROGDIR:ZZPicoDrive.cfg");
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
    } else {
        int g;
        g_quiet=1;                       /* GUI : aucune sortie console */
        g=gui_get_config(&cfg);
        if(g<=0){ rc=0; goto mainclean; }   /* Quit, fermeture, ou GUI indisponible */
    }
    zp_config_save(&cfg, "PROGDIR:ZZPicoDrive.cfg");
    rc=run_picodrive(&cfg);
mainclean:
    if(GfxBase_){ CloseLibrary(GfxBase_); GfxBase_=NULL; }
    if(IntuitionBase){ CloseLibrary((struct Library*)IntuitionBase); IntuitionBase=NULL; }
    return rc;
}