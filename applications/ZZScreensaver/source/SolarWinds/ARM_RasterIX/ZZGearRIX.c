/* ZZScreensaverS A1 launcher/presenter, 17/10/2026.
 * Derived from the validated ZZGear/GLExcess launcher: board detection,
 * GPU query, P96 screen, coherent mailbox polling, PAN and Core1 return.
 * New: Solar Winds identity and preset selector; no scene-data service.
 * SPDX-License-Identifier: 0BSD */
#define __USE_SYSBASE
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/nodes.h>
#include <libraries/configvars.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <devices/timer.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <proto/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "rix_shared.h"

struct ExpansionBase *ExpansionBase=NULL;
struct Library *P96Base=NULL;
struct Device *TimerBase=NULL;
extern const UBYTE zzgear_core1_blob[];
extern const ULONG zzgear_core1_blob_size;

static const char ver[] __attribute__((used))=
 "$VER: ZZScreensaverS 1.0 A1 (17.10.2026)\r\n";

#define ZZ9000_MANUF 0x6D6Eu
#define ZZ9000_PROD  0x04u
#define ZZ9000_PROD_AX 0x0Au
#define MNT_FB_BASE       0x00010000UL
#define XACP_CMD_OFFSET   0x04000000UL
#define REG_CMD           0x0064UL
#define REG_PARAM         0x0068UL
#define REG_ARM_RUN_HI    0x0090UL
#define REG_ARM_RUN_LO    0x0092UL
#define REG_ZZ_TEMPERATURE 0x00E0UL
#define OP_MEMCPY         0x0001UL
#define OP_GPU_QUERY      0x0600UL
#define CAP_QUERY         0x00000001UL
#define CAP_SUBMIT        0x00000008UL
#define SUBMIT_ABI        0x00010000UL
#define MAGIC_EXPECTED    0x52495831UL
#define VER_EXPECTED      0x00020000UL
#define HP3_32BITEN       0x00000001UL
#define STATUS_IDLE  0UL
#define STATUS_BUSY  1UL
#define STATUS_DONE  2UL
#define STATUS_ERROR 3UL
#define W 640
#define H 480

#define REG_ZZ_PAN_HI 0x0A
#define REG_ZZ_PAN_LO 0x0C
#define REG_ZZ_X1 0x10
#define REG_ZZ_Y1 0x12
#define REG_ZZ_X2 0x14
#define REG_ZZ_COLORMODE 0x30
#define REG_ZZ_USER3 0x44
#define REG_ZZ_USER4 0x46
#define ZZ_COLORMODE_16 1
#define ZZ_WR(b,o,v) (*((volatile UWORD*)((UBYTE*)(b)+(o)))=(UWORD)(v))
#define ZZ_RD(b,o)   (*((volatile UWORD*)((UBYTE*)(b)+(o))))

#ifndef CACRF_ClearD
#define CACRF_ClearD (1L << 2)
#endif

typedef struct XACPCommand {
    ULONG opcode,input_offset,input_size,output_offset,output_max_size;
    ULONG param1,param2,param3,param4,result1,result2,status,error;
} XACPCommand;

static volatile UBYTE *g_board=NULL,*g_fb=NULL,*g_shared=NULL;
static volatile XACPCommand *g_cmd=NULL;
static struct Window *g_win=NULL;
static int g_coherent=1;         /* OP_MEMCPY poll available */
static int g_quiet=1;
static int g_debug=0;
static ULONG g_presented_frames=0;
static int debug_printf(const char *fmt,...)
{
    int rc;va_list ap;
    if(!g_debug)return 0;
    va_start(ap,fmt);rc=vprintf(fmt,ap);va_end(ap);return rc;
}
#define printf debug_printf


/* ---- timer.device (A08/A12 heritage) -------------------------------- */
static struct MsgPort *g_tick_port=NULL;
static struct timerequest *g_tick_req=NULL;
static LONG g_tick_open=-1;
static int tick_open(void){
    g_tick_port=(struct MsgPort*)CreateMsgPort();
    if(!g_tick_port)return 0;
    g_tick_req=(struct timerequest*)CreateIORequest(g_tick_port,sizeof(struct timerequest));
    if(!g_tick_req){DeleteMsgPort(g_tick_port);g_tick_port=NULL;return 0;}
    g_tick_open=OpenDevice((STRPTR)"timer.device",UNIT_MICROHZ,(struct IORequest*)g_tick_req,0);
    if(g_tick_open!=0){DeleteIORequest((struct IORequest*)g_tick_req);g_tick_req=NULL;DeleteMsgPort(g_tick_port);g_tick_port=NULL;return 0;}
    TimerBase=g_tick_req->tr_node.io_Device;
    return 1;
}
static void tick_close(void){
    if(g_tick_open==0&&g_tick_req){CloseDevice((struct IORequest*)g_tick_req);g_tick_open=-1;TimerBase=NULL;}
    if(g_tick_req){DeleteIORequest((struct IORequest*)g_tick_req);g_tick_req=NULL;}
    if(g_tick_port){DeleteMsgPort(g_tick_port);g_tick_port=NULL;}
}
static unsigned long long eclock64(ULONG *freq){
    struct EClockVal e;ULONG f=ReadEClock(&e);
    if(freq)*freq=f;
    return ((unsigned long long)e.ev_hi<<32)|(unsigned long long)e.ev_lo;
}
/* blocking wait: the 68060 is free for other tasks while we sleep */
static void sleep_us(ULONG us){
    if(g_tick_open!=0||!g_tick_req)return;
    g_tick_req->tr_node.io_Command=TR_ADDREQUEST;
    g_tick_req->tr_time.tv_secs=us/1000000UL;
    g_tick_req->tr_time.tv_micro=us%1000000UL;
    DoIO((struct IORequest*)g_tick_req);
}

/* ---- small helpers -------------------------------------------------- */
static void cache_sync(APTR p,ULONG n){CacheClearE(p,n,CACRF_ClearD);}
static ULONG rd32le(volatile UBYTE *b,ULONG o){volatile UWORD*p=(volatile UWORD*)(b+o);UWORD w0=p[0],w1=p[1];return ((ULONG)((UWORD)((w1>>8)|(w1<<8)))<<16)|(ULONG)((UWORD)((w0>>8)|(w0<<8)));}
static void wr32le(volatile UBYTE*b,ULONG o,ULONG v){volatile UWORD*p=(volatile UWORD*)(b+o);ULONG q=((v&0xFFUL)<<24)|((v&0xFF00UL)<<8)|((v&0xFF0000UL)>>8)|((v&0xFF000000UL)>>24);p[0]=(UWORD)(q>>16);p[1]=(UWORD)q;}
static void copy_blob(volatile UBYTE*dst,const UBYTE*src,ULONG size){volatile UWORD*d=(volatile UWORD*)dst;ULONG i,n=(size+1)>>1;for(i=0;i<n;i++){ULONG b=i<<1;UWORD w=(UWORD)((UWORD)src[b]<<8);if(b+1<size)w|=src[b+1];d[i]=w;}}

static int poll_quit(void)
{
    struct IntuiMessage *m;
    if(SetSignal(0L,0L)&SIGBREAKF_CTRL_C)return 1;
    if(!g_win)return 0;
    while((m=(struct IntuiMessage*)GetMsg(g_win->UserPort))!=NULL){
        ULONG c=m->Class;UWORD code=m->Code;ReplyMsg((struct Message*)m);
        if(c==IDCMP_RAWKEY && !(code&0x80)){
            return 1; /* any key press; release events do not stop the saver */
        }
        if(c==IDCMP_MOUSEBUTTONS)return 1;
        if(c==IDCMP_MOUSEMOVE && g_presented_frames>8)return 1;
    }
    return 0;
}

static int wait_done(volatile XACPCommand *cmd,ULONG ticks)
{
    ULONG i,st;
    for(i=0;i<ticks;i++){
        cache_sync((APTR)cmd,128UL);
        st=cmd->status;
        if(st==STATUS_DONE||st==STATUS_ERROR)return 1;
        Delay(1);
    }
    return 0;
}

static int gpu_query(void)
{
    ULONG magic,version,caps,hp3rd,hp3wr,abi;
    memset((void*)g_cmd,0,sizeof(*g_cmd));
    g_cmd->opcode=OP_GPU_QUERY;cache_sync((APTR)g_cmd,128UL);ZZ_WR(g_board,REG_CMD,OP_GPU_QUERY);
    if(!wait_done(g_cmd,100UL)){printf("GPU QUERY timeout\n");return 0;}
    magic=g_cmd->result1;version=g_cmd->result2;caps=g_cmd->param1;
    hp3rd=g_cmd->param2;hp3wr=g_cmd->param3;abi=g_cmd->param4;
    printf("RasterIX HW=%08lx ver=%08lx caps=%08lx ABI=%08lx HP3=%08lx/%08lx\n",
        (unsigned long)magic,(unsigned long)version,(unsigned long)caps,(unsigned long)abi,
        (unsigned long)hp3rd,(unsigned long)hp3wr);
    if(g_cmd->status!=STATUS_DONE||g_cmd->error||magic!=MAGIC_EXPECTED||version!=VER_EXPECTED||
       (caps&(CAP_QUERY|CAP_SUBMIT))!=(CAP_QUERY|CAP_SUBMIT)||abi!=SUBMIT_ABI||
       ((hp3rd|hp3wr)&HP3_32BITEN)){
        printf("GPU guard FAIL status=%lu error=%08lx\n",(unsigned long)g_cmd->status,(unsigned long)g_cmd->error);
        return 0;
    }
    return 1;
}

/* ---- XACP V1 OP_MEMCPY: Core0-cache-coherent copy inside the fb window --- */
static void xacp_param(ULONG sel,ULONG v16){ZZ_WR(g_board,REG_CMD,sel);ZZ_WR(g_board,REG_PARAM,v16);}
static int xacp_memcpy(ULONG in_off,ULONG out_off,ULONG size)
{
    ULONG st,i;
    xacp_param(0x10UL,in_off>>16);xacp_param(0x11UL,in_off&0xFFFFUL);
    xacp_param(0x12UL,out_off>>16);xacp_param(0x13UL,out_off&0xFFFFUL);
    xacp_param(0x14UL,size>>16);xacp_param(0x15UL,size&0xFFFFUL);
    ZZ_WR(g_board,REG_CMD,OP_MEMCPY);
    for(i=0;i<200000UL;i++){
        st=ZZ_RD(g_board,REG_CMD);
        if(st==STATUS_DONE)return 1;
        if(st!=STATUS_BUSY&&st!=STATUS_IDLE)return 0;
    }
    return 0;
}
/* A1: bounded asset reads, no path accepted from Core1. */
/* read mailbox line 0 (32 bytes) coherently; returns 1 on success */
static ULONG g_line0_magic=0,g_line0_stage=0,g_line0_exit=0;
static int mailbox_line0(ULONG *status,ULONG *ready,ULONG *error,ULONG *heartbeat)
{
    ULONG src=RIX_SHARED_FB,dst=RIX_SHARED_FB+SH_COPY_LINE0*4UL,base=0UL;
    if(g_coherent){
        if(!xacp_memcpy(src,dst,32UL)){return 0;}
        base=SH_COPY_LINE0*4UL;
        cache_sync((APTR)(g_shared+base),32UL);
    } else {
        cache_sync((APTR)g_shared,32UL);
    }
    *status=rd32le(g_shared,base+SH_STATUS*4UL);
    *ready=rd32le(g_shared,base+SH_READY*4UL);
    *error=rd32le(g_shared,base+SH_ERROR*4UL);
    *heartbeat=rd32le(g_shared,base+SH_HEARTBEAT*4UL);
    g_line0_magic=rd32le(g_shared,base+SH_MAGIC*4UL);
    g_line0_stage=rd32le(g_shared,base+SH_STAGE*4UL);
    g_line0_exit=rd32le(g_shared,base+SH_EXIT_MARK*4UL);
    return 1;
}
/* copy slots 16..479 coherently into the private block; returns base pointer */
static volatile UBYTE *mailbox_block(void)
{
    ULONG src=RIX_SHARED_FB+16UL*4UL,dst=RIX_SHARED_FB+SH_COPY_BLOCK*4UL,size=(SH_END-16UL)*4UL;
    if(g_coherent&&xacp_memcpy(src,dst,size)){
        cache_sync((APTR)(g_shared+SH_COPY_BLOCK*4UL),size);
        return g_shared+SH_COPY_BLOCK*4UL-16UL*4UL;   /* so that slot index * 4 works */
    }
    cache_sync((APTR)g_shared,SH_END*4UL);
    return g_shared;
}
#define SLOT(base,i) rd32le((base),(ULONG)(i)*4UL)

/* ---- P96 PAN (A04..A14 validated register sequence) ----------------- */
static void pan_request(volatile UBYTE*b,ULONG off){ZZ_WR(b,REG_ZZ_PAN_HI,(UWORD)(off>>16));ZZ_WR(b,REG_ZZ_PAN_LO,(UWORD)off);}
static ULONG pan_ack(volatile UBYTE*b){return (ULONG)(ZZ_RD(b,REG_ZZ_USER4)&0xffffu);}
/* Startup/cleanup PAN: deferred flip with acknowledgement wait (A14 code). */
static int canonical_pan16(volatile UBYTE*board,ULONG off,ULONG pitch)
{
    ULONG before,spins=0;
    ZZ_WR(board,REG_ZZ_USER3,2);ZZ_WR(board,REG_ZZ_X1,0);ZZ_WR(board,REG_ZZ_Y1,0);
    ZZ_WR(board,REG_ZZ_X2,(UWORD)(pitch>>1));ZZ_WR(board,REG_ZZ_COLORMODE,ZZ_COLORMODE_16);
    before=pan_ack(board);ZZ_WR(board,REG_ZZ_USER3,1);pan_request(board,off);
    /* ack normally arrives within one frame; bound the wait to ~0.5 s */
    while(pan_ack(board)==before && spins<2000000UL){
        spins++;
        if(spins>=4000UL){ if(g_tick_open==0)sleep_us(500UL); if(spins>=5000UL)break; }
    }
    {int ok=(pan_ack(board)!=before);
     ZZ_WR(board,REG_ZZ_USER3,2);
     return ok;}
}
/* Per-frame PAN: same registers, immediate P96 semantics (applied by Core0's
 * vblank ISR), no acknowledgement polling: Core1 observes the flip itself
 * through the VDMA start-address register. 8 register writes, ~10 us. */
static void frame_pan16(volatile UBYTE*board,ULONG off,ULONG pitch)
{
    ZZ_WR(board,REG_ZZ_USER3,2);ZZ_WR(board,REG_ZZ_X1,0);ZZ_WR(board,REG_ZZ_Y1,0);
    ZZ_WR(board,REG_ZZ_X2,(UWORD)(pitch>>1));ZZ_WR(board,REG_ZZ_COLORMODE,ZZ_COLORMODE_16);
    pan_request(board,off);
}

/* ---- idle-counter CPU meter ------------------------------------------ */
static volatile ULONG g_idle_counter=0;
static struct Task *g_idle_task=NULL;
static UBYTE *g_idle_stack=NULL;
#define IDLE_STACK 2048UL
static void idle_task_entry(void){for(;;){g_idle_counter++;}}
static int idle_task_start(void)
{
    struct Task *t=(struct Task*)AllocMem(sizeof(struct Task),MEMF_PUBLIC|MEMF_CLEAR);
    if(!t)return 0;
    g_idle_stack=(UBYTE*)AllocMem(IDLE_STACK,MEMF_PUBLIC|MEMF_CLEAR);
    if(!g_idle_stack){FreeMem(t,sizeof(struct Task));return 0;}
    t->tc_Node.ln_Type=NT_TASK;t->tc_Node.ln_Pri=-127;t->tc_Node.ln_Name=(char*)"ZZSolarWindsIdleMeter";
    t->tc_SPLower=g_idle_stack;t->tc_SPUpper=g_idle_stack+IDLE_STACK;t->tc_SPReg=g_idle_stack+IDLE_STACK;
    AddTask(t,(APTR)idle_task_entry,NULL);
    g_idle_task=t;
    return 1;
}
static void idle_task_stop(void)
{
    if(g_idle_task){RemTask(g_idle_task);FreeMem(g_idle_task,sizeof(struct Task));g_idle_task=NULL;}
    if(g_idle_stack){FreeMem(g_idle_stack,IDLE_STACK);g_idle_stack=NULL;}
}
/* counts per EClock tick scaled by 1e6 (counts per second) */
static ULONG idle_rate(unsigned long long dticks,ULONG dcount,ULONG freq)
{
    if(!dticks)return 0;
    return (ULONG)(((unsigned long long)dcount*freq)/dticks);
}

static ULONG zz_temperature10(void){return (ULONG)ZZ_RD(g_board,REG_ZZ_TEMPERATURE);}

/* ---- histogram percentiles ------------------------------------------ */
static ULONG hist_percentile(volatile UBYTE *base,ULONG slot0,ULONG bins,ULONG total,ULONG pct)
{
    ULONG acc=0,i,target=(total*pct+99UL)/100UL;
    if(!total)return 0;
    for(i=0;i<bins;i++){acc+=SLOT(base,slot0+i);if(acc>=target)return (i+1UL)*100UL;}
    return bins*100UL;
}
static ULONG hist_total(volatile UBYTE *base,ULONG slot0,ULONG bins){ULONG i,t=0;for(i=0;i<bins;i++)t+=SLOT(base,slot0+i);return t;}

static void print_ms(const char *label,ULONG us_sum,ULONG count)
{
    ULONG cs;
    if(!count){printf("  %-28s n/a\n",label);return;}
    cs=(ULONG)(((unsigned long long)us_sum*100ULL)/((unsigned long long)count*1000ULL));
    printf("  %-28s %lu.%02lu ms\n",label,(unsigned long)(cs/100),(unsigned long)(cs%100));
}
static void print_us(const char *label,ULONG us){printf("  %-28s %lu us\n",label,(unsigned long)us);}
static void print_fps(const char *label,ULONG frames,ULONG elapsed_us)
{
    ULONG f100;
    if(!elapsed_us){printf("  %-28s n/a\n",label);return;}
    f100=(ULONG)(((unsigned long long)frames*100000000ULL)/elapsed_us);
    printf("  %-28s %lu.%02lu /s (%lu in %lu.%03lu s)\n",label,(unsigned long)(f100/100),(unsigned long)(f100%100),
        (unsigned long)frames,(unsigned long)(elapsed_us/1000000UL),(unsigned long)((elapsed_us/1000UL)%1000UL));
}

static const char *dl_mode_name(ULONG m){return m==1?"DDR list (FSC MEM->ST1, 2 FIFO words/frame)":m==2?"FIFO stream (ST0->ST1, CPU MMIO 1.1 us/word)":m==3?"FIFO fed by the PL330 (ST0->ST1, DMA channel 6, packets of 248 words)":"?";}
static const char *swap_mode_name(ULONG m){return m==1?"PL330 DMA endian swap":m==2?"NEON (Core1)":m==3?"none (pre-swapped nearest texture)":"none (NOSWAP: wrong colors)";}
static const char *idle_mode_name(ULONG m){return m==1?"WFI + private timer":"spin";}

static void print_selftest(volatile UBYTE *blk)
{
#define SDV(i) SLOT(blk,SH_SELFTEST_DATA+(i))
    printf("SELFTEST (Core1, G4B RasterIX):\n");
    printf("  ID magic/version            %08lx / %08lx\n",(unsigned long)SDV(SD_ID_MAGIC),(unsigned long)SDV(SD_ID_VERSION));
    printf("  FIFO vacancy after reset    %lu words (reset %lu us)\n",(unsigned long)SDV(SD_FIFO_VACANCY),(unsigned long)SDV(SD_FIFO_RESET_US));
    printf("  FSC resync (4608 NOP words) %lu us\n",(unsigned long)SDV(SD_RESYNC_US));
    {
        static const char *lad_name[SD_LADDER_MAX]={
            "L0 alive at launch (2 NOP via parser)","L1 resync 9x508 FSC NOP","L2 Test2 recipe 64x32 memset->SCRATCH",
            "L3 A07b recipe 640x480 memset->FB0","L4 640x481 memset (line 480)","L5 scissor regs set, scissor off, memset",
            "L6 FENCE: scissor on, 64 px -> line 480","L7 OP_NOP words in stream + memset","L8 app frame head+clear+fence (FIFO)",
            "L9 same list fetched from DDR (FSC MEM)","L10 alive at end",
            "L11 TMU0 registers + fence (FIFO)","L12 texture page table 64 pages (FIFO)",
            "L13 1 textured triangle + fence (FIFO)","L14 same textured frame from DDR",
            "L15 page table inside a DDR list (TEXDDR)"};
        ULONG k,stages=SDV(SD_LADDER_STAGES),failed=SDV(SD_LADDER_FAILED);
        printf("  init ladder                 %lu stages run, %s\n",(unsigned long)stages,failed?"FAILED (see below)":"all passed");
        for(k=0;k<SD_LADDER_MAX;k++){
            ULONG b=SD_LADDER+k*SD_LADDER_SLOTS,r=SDV(b),tc=SDV(b+1),ck=SDV(b+2),map=SDV(b+3);
            if(r==0xEEEEEEEEUL){printf("    %-42s not run\n",lad_name[k]);continue;}
            if(r==0)printf("    %-42s ok   TC %lu us checks %lx\n",lad_name[k],(unsigned long)tc,(unsigned long)ck);
            else if((r&0xFFFFUL)==0xE114UL)printf("    %-42s HUNG: GPU never took the packet (TC timeout %lu us)%s\n",lad_name[k],(unsigned long)tc,(ck&0x80000000UL)?" after consuming it":"");
            else{
                printf("    %-42s FAIL rc=%04lx checks %lx",lad_name[k],(unsigned long)(r&0xFFFFUL),(unsigned long)(ck&0x7FFFFFFFUL));
                if(ck&0x80000000UL)printf(" then GPU HUNG");
                if(map!=0xFFFFFFFFUL){if((map>>16)==0xFFFFUL)printf(", color found nowhere in FB0");else printf(", color found in FB0 lines %lu..%lu",(unsigned long)(map>>16),(unsigned long)(map&0xFFFFUL));}
                printf("\n");
            }
        }
        if(failed){
            ULONG b=SD_LADDER+(failed-1UL)*SD_LADDER_SLOTS;
            if((SDV(b)&0xFFFFUL)==0xE114UL||(SDV(b+2)&0x80000000UL))
                printf("  => the GPU stopped consuming commands at this stage. RasterIX has no software reset on G4B\n"
                       "     (shared proc_sys_reset_0 with the Zorro bridge): a warm relaunch will not recover it,\n"
                       "     POWER CYCLE the Amiga before the next run. The stage above is the culprit to report.\n");
        }
    }
    printf("  fence via FIFO list         %s %lu us (value %04lx, %lu words)\n",SDV(SD_FENCE_FIFO_US)==0xFFFFFFFFUL?"TIMEOUT":"ok",(unsigned long)SDV(SD_FENCE_FIFO_US),(unsigned long)SDV(SD_FENCE_VALUE_FIFO),(unsigned long)SDV(SD_FENCE_FIFO_WORDS));
    printf("  fence via DDR list          %s %lu us (value %04lx)\n",SDV(SD_FENCE_MEM_US)==0xFFFFFFFFUL?"TIMEOUT -> FIFO fallback":SDV(SD_FENCE_MEM_US)==0xEEEEEEEEUL?"not run (GPU hung earlier)":SDV(SD_FENCE_MEM_US)==0?"skipped (DLFIFO)":"ok",(unsigned long)SDV(SD_FENCE_MEM_US),(unsigned long)SDV(SD_FENCE_VALUE_MEM));
    printf("  register baseline           %lu words sent before the probes (REG 3 color mask on)\n",(unsigned long)SDV(SD_BASELINE_WORDS));
    if(SDV(SD_INIT_FIRST_ERROR))printf("  init first error            %04lx (%s)\n",(unsigned long)SDV(SD_INIT_FIRST_ERROR),(SDV(SD_INIT_FIRST_ERROR)>=0xE1B2UL&&SDV(SD_INIT_FIRST_ERROR)<=0xE1B7UL)?"diagnostic stage, not fatal: init went on":"init continued to collect the other tests");
    if(SDV(SD_INIT_FIRST_ERROR)==0xE1A0UL)printf("  => E1A0: the GPU did not consume a single packet at launch: hung by an earlier run. POWER CYCLE required.\n");
    printf("  GPU clear 640x481           min %lu avg %lu max %lu us\n",(unsigned long)SDV(SD_CLEAR_US_MIN),(unsigned long)SDV(SD_CLEAR_US_AVG),(unsigned long)SDV(SD_CLEAR_US_MAX));
    printf("  GPU full clear+gear         min %lu avg %lu max %lu us\n",(unsigned long)SDV(SD_FRAME_US_MIN),(unsigned long)SDV(SD_FRAME_US_AVG),(unsigned long)SDV(SD_FRAME_US_MAX));
    printf("  GPU gear, no clear          min %lu avg %lu max %lu us\n",(unsigned long)SDV(SD_TRIS_US_MIN),(unsigned long)SDV(SD_TRIS_US_AVG),(unsigned long)SDV(SD_TRIS_US_MAX));
    printf("  GPU dirty clear+gear NOTEX  min %lu avg %lu us\n",(unsigned long)SDV(SD_CULL_US_MIN),(unsigned long)SDV(SD_CULL_US_AVG));
    printf("  GPU dirty clear+gear        min %lu avg %lu us (default mode)\n",(unsigned long)SDV(SD_DIRTY_US_MIN),(unsigned long)SDV(SD_DIRTY_US_AVG));
    printf("  PL330 swap self-test        %s; 614400 B in-place: min %lu avg %lu max %lu us (DSR %08lx base %08lx)\n",
        SDV(SD_DMA_TEST)==1?"PASS":SDV(SD_DMA_TEST)==2?"MISMATCH":SDV(SD_DMA_TEST)==3?"FAULT":SDV(SD_DMA_TEST)==4?"TIMEOUT":SDV(SD_DMA_TEST)==5?"DMAC unclocked/in reset (SLCR)":"skipped",
        (unsigned long)SDV(SD_DMA_US_MIN),(unsigned long)SDV(SD_DMA_US_AVG),(unsigned long)SDV(SD_DMA_US_MAX),(unsigned long)SDV(SD_DMAC_DSR),(unsigned long)SDV(SD_DMAC_BASE));
    printf("  NEON swap 614400 B          min %lu avg %lu max %lu us\n",(unsigned long)SDV(SD_NEON_US_MIN),(unsigned long)SDV(SD_NEON_US_AVG),(unsigned long)SDV(SD_NEON_US_MAX));
    printf("  WFI wake-up                 stage %lu/3, 1000 us sleep measured %lu us (ISPENDR %08lx)\n",(unsigned long)SDV(SD_WFI_TEST),(unsigned long)SDV(SD_WFI_SLEEP_US),(unsigned long)SDV(SD_ICDISPR));
    printf("  VDMA start address at init  %08lx (P96 screen expected)\n",(unsigned long)SDV(SD_VDMA_SA));
    printf("  global timer                %lu Hz assumed, ctrl %08lx, ticks in Core1 second %lu\n",(unsigned long)SDV(SD_TIMER_HZ),(unsigned long)SDV(SD_GT_CTRL),(unsigned long)SDV(SD_SECOND_TICKS));
    if(SDV(SD_PROBE_RUN)){
        static const char *pname[6]={"depth test (Z-buffer in FB2)","blending ONE,ONE","alpha test GREATER 0.5","texture switch 64x64","texture switch 256x256","state only (baseline)"};
        ULONG run=SDV(SD_PROBE_RUN),failed=SDV(SD_PROBE_FAILED),k;
        printf("CAPABILITY PROBES (G4B, Core1 direct FIFO, not the FAST1 sandbox):\n");
        if(run&1UL)printf("  depth test                  %s (pixels near-then-far %04lx, far-then-near %04lx; red F800 = near wins; no-clear rerun %04lx, 0000 = Z kept)\n",
            SDV(SD_PROBE_DEPTH)==1?"WORKS: the nearer triangle wins in both orders -> G4B has a Z-buffer":SDV(SD_PROBE_DEPTH)==2?"NO: last drawn wins (depth test ineffective: no Z-buffer in this bitstream, or depth path inactive)":"INCONSISTENT",
            (unsigned long)(SDV(SD_PROBE_DEPTH_PIX)>>16),(unsigned long)(SDV(SD_PROBE_DEPTH_PIX)&0xFFFFUL),(unsigned long)SDV(SD_PROBE_DEPTH_KEEP));
        if(run&2UL)printf("  blending ONE,ONE            %s (pixel %04lx; FFE0 = red+green)\n",SDV(SD_PROBE_BLEND)==1?"WORKS":SDV(SD_PROBE_BLEND)==2?"NO: green only":"OTHER",(unsigned long)SDV(SD_PROBE_BLEND_PIX));
        if(run&4UL)printf("  alpha test GREATER 0.5      %s (alpha 0.25 -> %04lx, alpha 0.75 -> %04lx; 0000 then FFFF expected)\n",SDV(SD_PROBE_ALPHA)==1?"WORKS":SDV(SD_PROBE_ALPHA)==2?"NO: both drawn":"OTHER",(unsigned long)(SDV(SD_PROBE_ALPHA_PIX)>>16),(unsigned long)(SDV(SD_PROBE_ALPHA_PIX)&0xFFFFUL));
        if(run&32UL)printf("  state only baseline         %lu us per (TMU config + 1 small triangle), %lu runs (64x64 scissored clear included)\n",(unsigned long)SDV(SD_PROBE_STATE_US),(unsigned long)SDV(SD_PROBE_TEX_N));
        if(run&8UL)printf("  texture switch 64x64        %lu us GPU per switch (4 pages, 8 KiB; baseline subtracted), Core1 MMIO %lu us per switch (5 table words)\n",(unsigned long)SDV(SD_PROBE_TEX64_US),(unsigned long)SDV(SD_PROBE_TEX64_PUSH));
        if(run&16UL)printf("  texture switch 256x256      %lu us GPU per switch (64 pages, 128 KiB; baseline subtracted), Core1 MMIO %lu us per switch (65 table words); whole run %lu us\n",(unsigned long)SDV(SD_PROBE_TEX256_US),(unsigned long)SDV(SD_PROBE_TEX256_PUSH),(unsigned long)SDV(SD_PROBE_TEX_RAW));
        if(run&64UL)printf("  placement (arena)           %s: texture at %08lx + Z-buffer at %08lx, both OUTSIDE the 2 MiB window (texel %04lx, written this run %04lx; depth-order pixel %04lx)\n",
            SDV(SD_PROBE_PLACE)==1?"WORKS -> RasterIX reads/writes the Core1 arena over HP3 (fresh texels, L1 clean + outer-NC mapping)":SDV(SD_PROBE_PLACE)==2?"NO/STALE TEXTURE (texel != the one written this run: not read from DDR, or cache not cleaned)":"DEPTH WRONG (Z-buffer in the arena not honoured)",
            (unsigned long)SDV(SD_PROBE_PLACE_TEX),(unsigned long)SDV(SD_PROBE_PLACE_Z),(unsigned long)(SDV(SD_PROBE_PLACE_PIX)>>16),(unsigned long)SDV(SD_PROBE_PLACE_EXPECT),(unsigned long)(SDV(SD_PROBE_PLACE_PIX)&0xFFFFUL));
        if(run&128UL)printf("  TMU env MODULATE            %s (white texel x 0.5 vertex colour -> %04lx; 7BEF/8410 = half, FFFF = REPLACE behaviour)\n",SDV(SD_PROBE_MOD)==1?"WORKS -> vertex-lit textured models":SDV(SD_PROBE_MOD)==2?"NO: vertex colour ignored":"OTHER",(unsigned long)SDV(SD_PROBE_MOD_PIX));
        if(run&256UL)printf("  blend ZERO,SRC_COLOR        %s (white then 0.5 grey -> %04lx; the GL lightmap pass)\n",SDV(SD_PROBE_LMBLEND)==1?"WORKS -> two-pass lightmaps (strategy A) possible":SDV(SD_PROBE_LMBLEND)==2?"NO: unchanged":"OTHER",(unsigned long)SDV(SD_PROBE_LMBLEND_PIX));
        if(failed)printf("  => probe %lu (%s) HUNG the GPU or lost its fence: nothing was sent after it. POWER CYCLE before the next run.\n",(unsigned long)failed,failed<=6UL?pname[(failed-1UL)]:failed==7UL?"placement (arena)":failed==8UL?"TMU env MODULATE":failed==9UL?"blend ZERO,SRC_COLOR":failed==10UL?"DDR list: depth, 2 triangles":failed==11UL?"DDR list: blend, 2 triangles":failed==12UL?"DDR list: depth, 12 triangles":failed==13UL?"DDR list: depth, 40 triangles":failed==14UL?"DDR list: depth + texture, 40 triangles (or its FIFO bind)":"?");
        for(k=0;k<6;k++)if(!(run&(1UL<<k)))printf("  %-28s not run\n",pname[k]);
        if(!(run&64UL))printf("  %-28s not run\n","placement (arena)");
        if(!(run&128UL))printf("  %-28s not run\n","TMU env MODULATE");
        if(!(run&256UL))printf("  %-28s not run\n","blend ZERO,SRC_COLOR");
    }
    if(SDV(SD_PROBE_DDR_RUN)||SDV(SD_PROBE_DDR_RES)){
        /* D2c: lists fetched by the FSC whose fragments READ the framebuffer (the ROOM hang of D2b) */
        static const char *dname[5]={"depth test, 2 triangles","blend ZERO,SRC_COLOR, 2 triangles","depth test, 12 triangles (~1 FIFO)","depth test, 40 triangles (ROOM chunk)","depth + texture, 40 triangles (ROOM mix)"};
        ULONG drun=SDV(SD_PROBE_DDR_RUN),dres=SDV(SD_PROBE_DDR_RES),k,pix[5];
        pix[0]=SDV(SD_PROBE_DDR_PIX0)>>16;pix[1]=SDV(SD_PROBE_DDR_PIX0)&0xFFFFUL;pix[2]=SDV(SD_PROBE_DDR_PIX1)>>16;pix[3]=SDV(SD_PROBE_DDR_PIX1)&0xFFFFUL;pix[4]=SDV(SD_PROBE_DDR_PIX2)>>16;
        printf("DDR-LIST READ PROBES (lists fetched by the FSC; every DDR list before D2b carried writes only):\n");
        for(k=0;k<5;k++){
            ULONG r=(dres>>(4UL*k))&15UL;
            printf("  probe %2lu  %-42s %s%s%04lx%s\n",(unsigned long)(9UL+k),dname[k],
                r==1UL?"WORKS":r==2UL?"WRONG PIXEL":r==3UL?"HUNG THE GPU":r==4UL?"skipped (no DDR lists this run)":(drun&(1UL<<k))?"sent, no result":"not run",
                (r==1UL||r==2UL)?" (pixel ":"",(unsigned long)((r==1UL||r==2UL)?pix[k]:0UL),(r==1UL||r==2UL)?")":"");
        }
        printf("  largest list %lu words. Reading: 9 hung = a DDR list cannot carry a single depth-tested triangle (lists are write-only on G4B);\n  9 ok, 11/12 hung = a size threshold exists (chunk size workaround); all WORK = the ROOM hang is something else.\n",(unsigned long)(SDV(SD_PROBE_DDR_PIX2)&0xFFFFUL));
    }
    if(SDV(SD_DMAFIFO_STATUS)){
        /* D2d: the DMAC writes the command words into the FIFO instead of Core1's MMIO stores */
        ULONG st=SDV(SD_DMAFIFO_STATUS),cnt=SDV(SD_DMAFIFO_COUNT);
        printf("FIFO BY PL330 (DLDMA: list words pushed into TDFD by DMA channel 6 instead of 1.1 us/word MMIO):\n");
        printf("  status                      %s\n",st==2UL?"USABLE with 16-beat FIXED bursts":st==1UL?"USABLE with single-beat stores (bursts not accepted by the interconnect)":st==3UL?"UNUSABLE: count test failed or DMA fault (CPU FIFO used instead)":"INTEGRITY FAILED: the fence carried by the DMA was not seen (CPU FIFO used instead)");
        printf("  count test                  16 words by bursts -> vacancy %lu, by single beats -> %lu (492 expected: 508 - 16)\n",(unsigned long)(cnt>>16),(unsigned long)(cnt&0xFFFFUL));
        printf("  integrity                   fence pixel %04lx (D0D1 expected: every word arrived, in order)\n",(unsigned long)SDV(SD_DMAFIFO_FENCE_PIX));
        if(SDV(SD_DMAFIFO_NS_B16))printf("  throughput, 16-beat bursts  %lu ns per word (%lu us per 40-word triangle; CPU MMIO: 1100 ns, 44 us)\n",(unsigned long)SDV(SD_DMAFIFO_NS_B16),(unsigned long)(SDV(SD_DMAFIFO_NS_B16)*40UL/1000UL));
        if(SDV(SD_DMAFIFO_NS_B1))printf("  throughput, single beats    %lu ns per word (%lu us per 40-word triangle)\n",(unsigned long)SDV(SD_DMAFIFO_NS_B1),(unsigned long)(SDV(SD_DMAFIFO_NS_B1)*40UL/1000UL));
        printf("  words moved by the test     %lu\n",(unsigned long)SDV(SD_DMAFIFO_WORDS));
    }
    if(SDV(SD_EXC_IRQS))printf("  interrupts taken by Core1's vectors: %lu IRQ, %lu FIQ (expected 0)\n",(unsigned long)(SDV(SD_EXC_IRQS)&0xFFFFUL),(unsigned long)(SDV(SD_EXC_IRQS)>>16));
#undef SDV
}

static void print_frame_diag(volatile UBYTE *blk,ULONG err);
/* D2c: Core1 took a CPU exception (its own vectors): where, and what the frame being built looked like */
static void print_exception(volatile UBYTE *blk,ULONG err)
{
#define SDV(i) SLOT(blk,SH_SELFTEST_DATA+(i))
    ULONG t=SDV(SD_EXC_TYPE),fsr=SDV(SD_EXC_FSR);
    printf("CORE1 EXCEPTION (error %04lx): %s at PC %08lx, %s %08lx, %s %08lx (status %lx%s), SPSR %08lx (mode %02lx), stage %04lx, IRQ/FIQ taken %lu/%lu\n",
        (unsigned long)err,t==1UL?"UNDEFINED INSTRUCTION":t==2UL?"SVC (unexpected)":t==3UL?"PREFETCH ABORT":t==4UL?"DATA ABORT":t==5UL?"reserved vector":"reset vector",
        (unsigned long)SDV(SD_EXC_PC),t==3UL?"IFAR":"DFAR",(unsigned long)SDV(SD_EXC_FAR),t==3UL?"IFSR":"DFSR",(unsigned long)fsr,
        (unsigned long)(((fsr>>10)&1UL)<<4|(fsr&15UL)),
        ((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x01UL)?" = alignment":((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x05UL)?" = translation (section): unmapped address":((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x07UL)?" = translation (page)":((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x08UL)?" = synchronous external abort (bus error)":((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x16UL)?" = asynchronous external abort (a write to a bad address, earlier)":((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x0DUL)?" = permission (section)":((((fsr>>10)&1UL)<<4|(fsr&15UL))==0x0FUL)?" = permission (page)":"",
        (unsigned long)SDV(SD_EXC_SPSR),(unsigned long)(SDV(SD_EXC_SPSR)&0x1FUL),(unsigned long)SDV(SD_EXC_STAGE),(unsigned long)(SDV(SD_EXC_IRQS)&0xFFFFUL),(unsigned long)(SDV(SD_EXC_IRQS)>>16));
    printf("  (PC -> function: arm-none-eabi-addr2line -f -e zzsolarwinds_core1.elf <PC>, or ARM_RasterIX/zzsolarwinds_core1.sym of the same build)\n");
    if(SDV(SD_FD_FRAME))print_frame_diag(blk,0xE1F0UL);
#undef SDV
}

static const char *phase_name(ULONG p)
{
    if(p==0x100UL)return "frame begin (head, depth clear)";
    if(p>=0x200UL&&p<0x300UL)return "texture bind";
    if(p==0x300UL)return "world pass 1 (textured faces)";
    if(p==0x310UL)return "world pass 2 (lightmap atlas, blend)";
    if(p==0x320UL)return "world (precomposed faces)";
    if(p==0x400UL)return "models (MODULATE, vertex-lit)";
    if(p==0x500UL)return "effects (blended, no depth write)";
    if(p==0x600UL)return "frame end";
    if(p==0x700UL)return "frame fence wait";
    return "?";
}
/* a chunk or bind fence timed out (E129/E12A) or a frame fence (E120) : what the GPU did and did not do */
static void print_frame_diag(volatile UBYTE *blk,ULONG err)
{
#define SDV(i) SLOT(blk,SH_SELFTEST_DATA+(i))
    ULONG ph=SDV(SD_FD_PHASE),alive=SDV(SD_FD_ALIVE),cseq=SDV(SD_FD_LAST_CSEQ),cpix=SDV(SD_FD_CSEQ_PIX),bseq=SDV(SD_FD_BIND_SEQ),bpix=SDV(SD_FD_BIND_PIX);
    if(SDV(SD_FD_FRAME)==0UL){   /* D2f: never print an empty block as if it were measured (D2e did on E120) */
        printf("FRAME DIAG: not captured (phase %04lx = %s)\n",(unsigned long)ph,phase_name(ph));
        return;
    }
    printf("FRAME DIAG (%s): frame %lu, phase %04lx = %s%s, chunks submitted %lu (last: %lu words, first word %08lx), triangles %lu\n",
        err==0xE129UL?"E129 bind fence never passed":err==0xE12AUL?"E12A chunk fence never passed (SYNC)":err==0xE120UL?"E120 frame fence timeout":err==0xE1F0UL?"state of the frame being built at the exception":"E124 ring slot never freed",
        (unsigned long)SDV(SD_FD_FRAME),(unsigned long)ph,phase_name(ph),(ph>=0x200UL&&ph<0x300UL)?" (bind index in the frame = phase-0x200)":"",
        (unsigned long)SDV(SD_FD_CHUNKS),(unsigned long)SDV(SD_FD_WORDS),(unsigned long)SDV(SD_FD_LAST_OPS),(unsigned long)SDV(SD_FD_TRIS));
    printf("  last chunk fence: seq %04lx, pixel at x=64 reads %04lx -> %s\n",cseq,cpix,cpix==cseq?"EXECUTED (the GPU finished that chunk)":"NOT executed");
    printf("  bind fence:       seq %04lx, pixel at x=128 reads %04lx -> %s\n",bseq,bpix,bseq==0UL?"no bind pending":(bpix==bseq?"EXECUTED":"NOT executed (the GPU stopped at or before the OP_TEXTURE)"));
    printf("  frame fence pixel 0: %04lx; FIFO vacancy %lu words (508 = everything left the FIFO), ISR %08lx\n",(unsigned long)SDV(SD_FD_FRAME_PIX),(unsigned long)SDV(SD_FD_FIFO_VAC),(unsigned long)SDV(SD_FD_FIFO_ISR));
    printf("  liveness after the timeout: %s\n",alive==1UL?"2 NOPs TAKEN by the parser -> the GPU is NOT hung; the fence was lost/misdirected":alive==2UL?"2 NOPs NOT taken -> GPU HUNG at this point. POWER CYCLE before the next run":"not run");
#undef SDV
}

static void print_stats(volatile UBYTE *blk,ULONG mode,ULONG scene,ULONG shown_by_68k,ULONG pans_by_68k,ULONG elapsed_68k_us)
{
#define RS(i) SLOT(blk,SH_STATS+(i))
    ULONG fenced=RS(RS_FRAMES_FENCED),el=RS(RS_T_ELAPSED_HI)?0xFFFFFFFFUL:RS(RS_T_ELAPSED_LO);
    ULONG pub=RS(RS_FRAMES_PUBLISHED),built=RS(RS_FRAMES_BUILT),sw=RS(RS_FRAMES_SWAPPED);
    (void)mode;(void)scene;(void)shown_by_68k;
    printf("CONFIG: %s; swap: %s; idle: %s; caps=%03lx\n",
        dl_mode_name(RS(RS_DL_MODE)),swap_mode_name(RS(RS_SWAP_MODE)),idle_mode_name(RS(RS_IDLE_MODE)),SLOT(blk,SH_CAPS));
    printf("COUNTS: built=%lu FENCED=%lu swapped=%lu published=%lu publication-address-seen=%lu dropped=%lu fence-timeouts=%lu dma-faults=%lu\n",
        built,fenced,sw,pub,RS(RS_FRAMES_FLIPPED),RS(RS_FRAMES_DROPPED),RS(RS_FENCE_TIMEOUTS),RS(RS_DMA_FAULTS));
    print_fps("completed frames (Core1 clock)",fenced,el);
    print_fps("PAN requests (68k clock)",pans_by_68k,elapsed_68k_us);
    printf("  SA1 observations are diagnostics, not a completed-scanout counter.\n");
    printf("PER-FRAME TIMES (overlapping stages are NOT additive):\n");
    print_ms("history copy (Core1 NEON)",RS(RS_T_SUBMIT),built);
    print_ms("scene + GL commands",RS(RS_T_XFORM),built);
    print_ms("build + submit total",RS(RS_T_BUILD),built);
    print_ms("  of which ring-slot waits",RS(RS_T_RINGWAIT),built);
    print_ms("render start -> fence",RS(RS_T_GPU),fenced);
    print_ms("first chunk -> fence",RS(RS_GPU_BUSY_US),fenced);
    print_ms("RGB565 swap",RS(RS_T_SWAP),sw);
    print_ms("wait for free buffer",RS(RS_T_WAITBUF),pub);
    print_ms("60 Hz pacing wait",RS(RS_T_IDLE),pub);
    print_ms("loop total",RS(RS_T_LOOP),pub);
    print_us("render start -> fence max",RS(RS_T_GPU_MAX));
    print_us("free-buffer wait max",RS(RS_T_WAITBUF_MAX));
    print_us("loop max",RS(RS_T_LOOP_MAX));
    printf("  last-frame triangles=%lu; chunks=%lu; words/frame=%lu\n",
        RS(RS_TRIS_PER_FRAME),RS(RS_CHUNKS),built?RS(RS_DL_WORDS)/built:0UL);
    printf("Solar Winds: Terence M. Welsh; Amiga port Xanxi 2026; GPL v2 or later.\n");
#undef RS
}

static void usage(void)
{
 g_debug=1;
 printf("ZZScreensaverS [frames] [PRESET 1..6] [DEBUG] [QUIET] [IDLEMETER]\n");
 printf("640x480 RGB565. Any key, mouse button or mouse movement: exit. Silent by default; DEBUG prints diagnostics.\n");
 printf("1 Regular; 2 Cosmic Strings; 3 Cold Pricklies; 4 Space Fur; 5 Jiggly; 6 Undertow.\n");
}

int main(int argc,char **argv)
{
    struct ConfigDev *cd;struct Library *IntuitionBase=NULL,*GfxBase_=NULL;
    struct Screen *screen=NULL;
    ULONG mode_id,screen_mem=0,screen_off=0,screen_pitch=0,bpp=0,rf=0;
    ULONG limit=0,flags=RIXF_BINDLIST,mode=RIX_MODE_MONITOR,tick_us=0UL,scene=1;
    ULONG last_ready=0,pans=0,polls=0,loops=0;
    ULONG eclock_freq=0;
    unsigned long long t_start=0,t_last_print=0,t_last_stats=0;
    ULONG idle_cal_rate=0,idle_run_rate=0,temp0=0,temp1=0;
    int rc=20,core_started=0,quit=0,timer_ok=0,need_screen=1,use_idle_meter=0,fatal=0;
    ULONG final_status=0,final_error=0;
    UWORD *blank_pointer=NULL;
    int pointer_hidden=0;

    for(int k=1;k<argc;k++) {
        char *end=NULL;
        if(!strcmp(argv[k],"PRESET") && k+1<argc) {
            unsigned long v=strtoul(argv[++k],&end,10);
            if(!*argv[k] || *end || v<1 || v>6){usage();return 10;}
            scene=v;
        } else if(!strcmp(argv[k],"DEBUG")){g_debug=1;g_quiet=0;}
        else if(!strcmp(argv[k],"QUIET")){g_debug=0;g_quiet=1;}
        else if(!strcmp(argv[k],"IDLEMETER"))use_idle_meter=1;
        else if(!strcmp(argv[k],"?")||!strcmp(argv[k],"HELP")){usage();return 0;}
        else if(argv[k][0]>='0'&&argv[k][0]<='9') {
            unsigned long v=strtoul(argv[k],&end,10);
            if(*end){usage();return 10;}limit=v;
        } else {usage();return 10;}
    }
    printf("ZZScreensaverS 1.0 A1 - (c) Xanxi 2026\n");
    printf("Solar Winds by Terence M. Welsh; GPL-2.0-or-later, no warranty.\n");
    printf("640x480 RGB565; preset=%lu; frame limit=%lu (0=unlimited).\n",scene,limit);
    if(!zzgear_core1_blob_size||zzgear_core1_blob_size>RIX_BLOB_MAX){printf("Invalid Core1 blob\n");return 10;}

    ExpansionBase=(struct ExpansionBase*)OpenLibrary("expansion.library",37);
    if(!ExpansionBase){printf("expansion.library missing\n");return 20;}
    cd=FindConfigDev(NULL,ZZ9000_MANUF,ZZ9000_PROD);
    if(!cd)cd=FindConfigDev(NULL,ZZ9000_MANUF,ZZ9000_PROD_AX);
    if(!cd){printf("ZZ9000 not found\n");goto cleanup;}
    g_board=(volatile UBYTE*)cd->cd_BoardAddr;g_fb=g_board+MNT_FB_BASE;
    g_cmd=(volatile XACPCommand*)(g_fb+XACP_CMD_OFFSET);
    g_shared=g_fb+RIX_SHARED_FB;
    if(!gpu_query())goto cleanup;
    temp0=zz_temperature10();
    printf("Zynq temperature %lu.%lu C\n",(unsigned long)(temp0/10),(unsigned long)(temp0%10));

    if(!tick_open()){printf("timer.device unavailable\n");goto cleanup;}
    timer_ok=1;
    eclock64(&eclock_freq);
    if(!eclock_freq){printf("ReadEClock unavailable\n");goto cleanup;}

    /* OP_MEMCPY availability check on the mailbox itself */
    if(g_coherent){
        wr32le(g_shared,SH_COPY_LINE0*4UL,0x11223344UL);
        cache_sync((APTR)(g_shared+SH_COPY_LINE0*4UL),32UL);
        Delay(3);   /* the write sits in Core0's cache until its vblank flush; MEMCPY invalidates its source */
        if(!xacp_memcpy(RIX_SHARED_FB+SH_COPY_LINE0*4UL,RIX_SHARED_FB+SH_COPY_LINE0*4UL+64UL,32UL)){
            printf("OP_MEMCPY not available: falling back to plain (vblank-granular) mailbox reads\n");g_coherent=0;
        } else {
            cache_sync((APTR)(g_shared+SH_COPY_LINE0*4UL+64UL),32UL);
            if(rd32le(g_shared,SH_COPY_LINE0*4UL+64UL)!=0x11223344UL){printf("OP_MEMCPY verification failed: plain reads\n");g_coherent=0;}
        }
    }

    if(need_screen){
        IntuitionBase=OpenLibrary("intuition.library",39L);
        GfxBase_=OpenLibrary("graphics.library",39L);
        P96Base=OpenLibrary("Picasso96API.library",2L);
        if(!IntuitionBase||!GfxBase_||!P96Base){printf("required RTG libraries missing\n");goto cleanup;}
        mode_id=p96BestModeIDTags(P96BIDTAG_NominalWidth,W,
            P96BIDTAG_NominalHeight,H,P96BIDTAG_Depth,16UL,TAG_DONE);
        if(mode_id==(ULONG)INVALID_ID){printf("no 640x480x16 P96 mode\n");goto cleanup;}
        screen=OpenScreenTags(NULL,SA_DisplayID,mode_id,SA_Width,W,SA_Height,H,
            SA_Depth,16UL,SA_Quiet,TRUE,SA_ShowTitle,FALSE,
            SA_Type,CUSTOMSCREEN,TAG_END);
        if(!screen){printf("OpenScreen failed\n");goto cleanup;}
        bpp=(ULONG)p96GetBitMapAttr(screen->RastPort.BitMap,P96BMA_BYTESPERPIXEL);
        rf=(ULONG)p96GetBitMapAttr(screen->RastPort.BitMap,P96BMA_RGBFORMAT);
        screen_pitch=(ULONG)p96GetBitMapAttr(screen->RastPort.BitMap,P96BMA_BYTESPERROW);
        screen_mem=(ULONG)p96GetBitMapAttr(screen->RastPort.BitMap,P96BMA_MEMORY);
        if(!screen_pitch)screen_pitch=W*2UL;
        if(bpp!=2UL||!screen_mem||screen_mem<(ULONG)g_fb){
            printf("P96 mismatch bpp=%lu memory=%08lx format=%lu\n",(unsigned long)bpp,(unsigned long)screen_mem,(unsigned long)rf);
            goto cleanup;
        }
        screen_off=screen_mem-(ULONG)g_fb;
        g_win=OpenWindowTags(NULL,WA_Left,0L,WA_Top,0L,WA_Width,W,WA_Height,H,
            WA_Backdrop,TRUE,WA_Borderless,TRUE,WA_NoCareRefresh,TRUE,
            WA_CustomScreen,(ULONG)screen,WA_Activate,TRUE,WA_RMBTrap,TRUE,
            WA_ReportMouse,TRUE,
            WA_IDCMP,IDCMP_RAWKEY|IDCMP_MOUSEBUTTONS|IDCMP_MOUSEMOVE,TAG_END);
        if(!g_win){printf("OpenWindow failed\n");goto cleanup;}
        /* Classic SetPointer sprite data must live in Chip RAM. Reserve two
           control words, one two-plane line and two trailing words (6 words). */
        blank_pointer=(UWORD*)AllocMem(6UL*sizeof(UWORD),MEMF_CHIP|MEMF_CLEAR);
        if(!blank_pointer){printf("Blank pointer allocation failed\n");goto cleanup;}
        SetPointer(g_win,blank_pointer,1,16,0,0);pointer_hidden=1;
        if(!canonical_pan16(g_board,screen_off,screen_pitch)){printf("initial P96 PAN ACK failed\n");goto cleanup;}
    }

    /* CPU meter calibration: 1 s with the screen open and nothing running */
    if(use_idle_meter&&idle_task_start()){
        unsigned long long a=eclock64(NULL);ULONG c0=g_idle_counter;
        sleep_us(1000000UL);
        idle_cal_rate=idle_rate(eclock64(NULL)-a,g_idle_counter-c0,eclock_freq);
    } else use_idle_meter=0;

    /* mailbox: 68k owns line 1 (slots 8..15); clear it and the Core1 lines */
    {ULONG i;for(i=0;i<SH_END;i++)wr32le(g_shared,i*4UL,0UL);}
    wr32le(g_shared,SH_STOP*4UL,0UL);
    wr32le(g_shared,SH_FLAGS*4UL,flags);
    wr32le(g_shared,SH_MODE*4UL,mode);
    wr32le(g_shared,SH_PARAM1*4UL,limit);
    wr32le(g_shared,SH_PARAM2*4UL,scene);
    wr32le(g_shared,SH_PARAM3*4UL,tick_us);
    wr32le(g_shared,SH_PARAM4*4UL,need_screen?(screen_off+RIX_FB_TO_68K):0UL);
    cache_sync((APTR)g_shared,SH_END*4UL);
    Delay(3);   /* let Core0's vblank flush push the mailbox to DDR before Core1 reads it */

    copy_blob(g_fb+RIX_BLOB_FB,zzgear_core1_blob,zzgear_core1_blob_size);
    cache_sync((APTR)(g_fb+RIX_BLOB_FB),zzgear_core1_blob_size);
    Delay(3);
    ZZ_WR(g_board,REG_ARM_RUN_HI,(UWORD)(RIX_BLOB_ARM>>16));
    ZZ_WR(g_board,REG_ARM_RUN_LO,(UWORD)RIX_BLOB_ARM);
    {LONG t=500;ULONG st,rd,er,hb;while(t-->0){mailbox_line0(&st,&rd,&er,&hb);if(g_line0_magic==RIX_CORE1_MAGIC)break;Delay(1);}}
    if(g_line0_magic!=RIX_CORE1_MAGIC){
        printf("Core1 boot failed (no magic). status=%08lx\n",(unsigned long)rd32le(g_shared,SH_STATUS*4UL));goto cleanup;
    }
    core_started=1;
    /* wait for init (probes, texture) : up to 10 s */
    {LONG t=500;ULONG st=0,rd,er,hb;
     while(t-->0){mailbox_line0(&st,&rd,&er,&hb);if(st==ST_RUNNING||st==ST_FATAL||st==ST_STOPPED)break;Delay(1);}
     if(st==ST_FATAL){
        volatile UBYTE *blk=mailbox_block();
        printf("Core1 init FAILED error=%08lx stage=%08lx init_rc=%ld caps=%03lx\n",(unsigned long)er,
            (unsigned long)g_line0_stage,(long)SLOT(blk,SH_INIT_ERROR),(unsigned long)SLOT(blk,SH_CAPS));
        if((er&0xFFF0UL)==0xE1F0UL)print_exception(blk,er&0xFFFFUL);
        print_selftest(blk);rc=10;fatal=1;goto stop;
     }
     if(st!=ST_RUNNING&&st!=ST_STOPPED){printf("Core1 init timeout status=%08lx stage=%08lx\n",(unsigned long)st,(unsigned long)g_line0_stage);rc=10;fatal=1;goto stop;}
    }
    {
        volatile UBYTE *blk=mailbox_block();
        printf("Core1 running: %s; swap: %s; idle: %s; caps=%03lx; timer %lu Hz\n",
            dl_mode_name(SLOT(blk,SH_STATS+RS_DL_MODE)),swap_mode_name(SLOT(blk,SH_STATS+RS_SWAP_MODE)),idle_mode_name(SLOT(blk,SH_STATS+RS_IDLE_MODE)),
            (unsigned long)SLOT(blk,SH_CAPS),(unsigned long)SLOT(blk,SH_TIMER_HZ));
        printf("TMU binds: %s\n",(flags&RIXF_BINDLIST)?"short DDR list per bind + bind fence (BINDLIST, rule 11 kept; ladder L15 passed)":"FIFO packet + bind fence (rule 11, the D2d path)");
        if(need_screen)printf("P96 screen at ARM %08lx, VDMA start address read by Core1 at init %08lx\n",(unsigned long)(screen_off+RIX_FB_TO_68K),(unsigned long)SLOT(blk,SH_SELFTEST_DATA+SD_VDMA_SA));
    }
    rc=0;
    t_start=eclock64(NULL);t_last_print=t_start;t_last_stats=t_start;
    {ULONG c0=g_idle_counter;unsigned long long a=t_start;
    /* ---- presenter loop ---- */
    while(!quit){
        ULONG st,ready,err,hb;
        if(poll_quit()){quit=1;break;}
        sleep_us(2000UL);
        loops++;
        if(!mailbox_line0(&st,&ready,&err,&hb)){printf("mailbox read failed\n");rc=10;break;}
        polls++;
        if(st==ST_FATAL){
            printf("Core1 FATAL error=%08lx stage=%08lx\n",(unsigned long)err,(unsigned long)g_line0_stage);
            if((err&0xFFFFUL)==0xE129UL||(err&0xFFFFUL)==0xE12AUL||(err&0xFFFFUL)==0xE120UL||(err&0xFFFFUL)==0xE124UL){
                volatile UBYTE *blk=mailbox_block();
                print_frame_diag(blk,err&0xFFFFUL);
            }
            if((err&0xFFF0UL)==0xE1F0UL){volatile UBYTE *blk=mailbox_block();print_exception(blk,err&0xFFFFUL);}
            rc=10;fatal=1;break;}
        if(st==ST_STOPPED){break;}
        if(need_screen&&ready!=last_ready&&ready!=0UL&&(flags&RIXF_FWPAN)){last_ready=ready;}   /* Core0 patch presents */
        if(need_screen&&ready!=last_ready&&ready!=0UL){
            ULONG idx=ready&3UL,off;
            volatile UBYTE *tbl=g_shared;
            cache_sync((APTR)(g_shared+SH_BUF0_OFF*4UL),16UL);
            off=rd32le(tbl,(SH_BUF0_OFF+idx)*4UL);
            if(idx>2UL||off<0x06400000UL||off>0x065F0000UL){printf("GPU published invalid buffer %lu off %08lx\n",(unsigned long)idx,(unsigned long)off);rc=10;break;}
            frame_pan16(g_board,off,W*2UL);
            wr32le(g_shared,SH_PAN_ISSUED*4UL,ready);
            last_ready=ready;pans++;g_presented_frames=pans;
        }
        if(!g_quiet&&eclock64(NULL)-t_last_print>(unsigned long long)eclock_freq*5ULL){
            unsigned long long now=eclock64(NULL);
            volatile UBYTE *blk=mailbox_block();
            ULONG el=SLOT(blk,SH_STATS+RS_T_ELAPSED_LO),fe=SLOT(blk,SH_STATS+RS_FRAMES_FENCED),fl=SLOT(blk,SH_STATS+RS_FRAMES_FLIPPED);
            ULONG t=zz_temperature10();
            ULONG fps100=el?(ULONG)(((unsigned long long)fe*100000000ULL)/el):0,fl100=el?(ULONG)(((unsigned long long)fl*100000000ULL)/el):0;
            printf("t=%lus fenced=%lu (%lu.%02lu/s) publication-address-seen=%lu (%lu.%02lu/s) gpu_last=%lu us swap_last=%lu us pans=%lu temp=%lu.%lu C\n",
                (unsigned long)((now-t_start)/eclock_freq),(unsigned long)fe,(unsigned long)(fps100/100),(unsigned long)(fps100%100),
                (unsigned long)fl,(unsigned long)(fl100/100),(unsigned long)(fl100%100),
                (unsigned long)SLOT(blk,SH_STATS+RS_T_GPU_LAST),(unsigned long)SLOT(blk,SH_STATS+RS_T_SWAP_LAST),(unsigned long)pans,
                (unsigned long)(t/10),(unsigned long)(t%10));
            t_last_print=now;
        }
        
        (void)t_last_stats;(void)hb;
    }
    if(use_idle_meter)idle_run_rate=idle_rate(eclock64(NULL)-a,g_idle_counter-c0,eclock_freq);
    }
stop:
    temp1=zz_temperature10();
    if(core_started){
        ULONG st=0,rd,er,hb;LONG t=500;
        wr32le(g_shared,SH_STOP*4UL,1UL);cache_sync((APTR)(g_shared+SH_STOP*4UL),32UL);
        while(t-->0){mailbox_line0(&st,&rd,&er,&hb);if(st==ST_STOPPED||st==ST_FATAL)break;Delay(1);}
        t=100;
        while(t-->0){mailbox_line0(&st,&rd,&er,&hb);if(g_line0_exit==RIX_EXIT_MARK)break;Delay(1);}
        final_status=st;final_error=er;
        printf("Core1 stopped status=%08lx error=%08lx exit=%08lx\n",(unsigned long)st,(unsigned long)er,(unsigned long)g_line0_exit);
    }
    if(core_started&&fatal&&mode==RIX_MODE_SELFTEST&&g_line0_exit==RIX_EXIT_MARK){
        /* D2c run 23: a FATAL during SELFTEST (a probe hung the GPU) lost the results obtained before it: print them */
        volatile UBYTE *blk=mailbox_block();
        printf("SELFTEST results obtained before the FATAL:\n");
        print_selftest(blk);
    }
    if(core_started&&!fatal){
        volatile UBYTE *blk=mailbox_block();
        unsigned long long el68=eclock64(NULL)-t_start;
        ULONG el68_us=(ULONG)((el68*1000000ULL)/eclock_freq);
        if(mode==RIX_MODE_SELFTEST){print_selftest(blk);}
        else {
            print_stats(blk,mode,scene,last_ready>>2,pans,el68_us);
            printf("68k: %lu presenter wake-ups (2 ms), %lu coherent polls, %lu PANs in %lu.%03lu s\n",(unsigned long)loops,(unsigned long)polls,(unsigned long)pans,
                (unsigned long)(el68_us/1000000UL),(unsigned long)((el68_us/1000UL)%1000UL));
        }
        if(use_idle_meter){
            if(idle_cal_rate&&idle_run_rate<=idle_cal_rate){
                ULONG pct100=(ULONG)(((unsigned long long)(idle_cal_rate-idle_run_rate)*10000ULL)/idle_cal_rate);
                printf("68060 load estimate (idle-counter method): %lu.%02lu %% incremental (idle rate %lu -> %lu /s). Confirm with Sensei.\n",
                    (unsigned long)(pct100/100),(unsigned long)(pct100%100),(unsigned long)idle_cal_rate,(unsigned long)idle_run_rate);
            } else printf("68060 load estimate unavailable (idle rate %lu -> %lu /s)\n",(unsigned long)idle_cal_rate,(unsigned long)idle_run_rate);
        }
        printf("Zynq temperature start %lu.%lu C, end %lu.%lu C\n",(unsigned long)(temp0/10),(unsigned long)(temp0%10),(unsigned long)(temp1/10),(unsigned long)(temp1%10));
        if(final_status==ST_FATAL)rc=10;
    }
cleanup:
    idle_task_stop();
    if(g_board&&screen&&screen_off)canonical_pan16(g_board,screen_off,screen_pitch);
    if(timer_ok)tick_close();
    if(pointer_hidden && g_win)ClearPointer(g_win);
    if(g_win){CloseWindow(g_win);g_win=NULL;}
    if(blank_pointer)FreeMem(blank_pointer,6UL*sizeof(UWORD));
    if(screen)CloseScreen(screen);
    if(P96Base){CloseLibrary(P96Base);P96Base=NULL;}
    if(GfxBase_)CloseLibrary(GfxBase_);
    if(IntuitionBase)CloseLibrary(IntuitionBase);
    if(ExpansionBase)CloseLibrary((struct Library*)ExpansionBase);
    (void)final_error;
    return rc;
}
