#include "os.h"
#include "adopt.h"
Ptr Sys,Dos,P96,Exp,Timer,Int;
Z zz_last_error,zz_badslot,zz_pinned;
static Ptr timer_port,request,claim,screen;
static Ptr p96_bitmap;static Z p96_handle,p96_arm,p96_pitch,p96_rgb;
static int p96_fast_disabled, p96_fast_verified;
/* A1b-DIAG1: read-only diagnosis of the first rejected P96 lock.
   No fast-path permission checks are relaxed. */
static Z p96_reject_reason, p96_lock_attempts, p96_lock_ret;
static Z p96_bitmap_raw, p96_memory_raw, p96_pitch_raw, p96_format_raw;
static Z p96_fb_raw;
Z zz_force_safe;static Z fast_show_ticks;
static Z zz_copy_scalar,zz_ack_coop,zz_ack_delays,zz_ack_spin_blocks;
/* A8.1: ZZMiniGLWait=SLEEP - every wait for Core1 (ACK, staged packet) and the MGLEnableSync pacing sleep
   on timer.device (UNIT_MICROHZ, TR_ADDREQUEST) instead of polling: the 68060 is given back to the other
   tasks; the polls left are one every 0.5 ms.  Costs at most about 0.5 ms of latency per wait. */
static Z zz_sleep,sleep_calls,sleep_ticks;
/* A8.2: one-shot authorisation given by ZZMiniGLLoad through the private LVO -36 (an unmodified
   application such as GLQuake cannot set ZZMiniGLCold itself; a Shell variable did not reach it) */
static Z zz_armed,ctx_armed,arm_flags,ctx_flags;
Z zz_arm(Z flags){zz_armed=1;arm_flags=flags;return 1;}
int zz_ctx_armed(void){return ctx_armed!=0;}
Z zz_ctx_flags(void){return ctx_flags;}   /* A8.7: ZZ_ARM_NOTRAP 1, ZZ_ARM_NOLMBLEND 2, ZZ_ARM_NOBLACKBOX 4 */
static void sleep_us(Z us){
 Z t;
 if(!request||!Timer||!us){dos_delay(1);return;}
 t=zz_clock();
 *(unsigned short *)((zu8 *)request+28)=9u;              /* io_Command = TR_ADDREQUEST */
 *(Z *)((zu8 *)request+32)=us/1000000u;                 /* tv_secs */
 *(Z *)((zu8 *)request+36)=us%1000000u;                 /* tv_micro */
 os_doio(request);
 sleep_calls++;sleep_ticks+=zz_clock()-t;
}
/* A5: presentation pipeline (ZZMiniGLSync disables it) and the requested copy engine (ZZMiniGLCopy SCALAR|BURST|DMA) */
static Z zz_sync,zz_copy_mode=ZZ_P96_COPY_DMA,staged_wait_ticks,zz_noz;
/* A7: OFFSCREEN presentation (ZZMiniGLOffscreen=ON) and the input window of MGLGetWindowHandle */
static Z zz_offscreen_on;static Ptr win;static Z win_opened;static Z win_keys;   /* A8.28: 1 while the library's window reports characters (zz_window_keys) */
/* A8.27: MGLCreateContextFromWindow.  The application's own window (adopt.h): its screen is used and never
   closed here, its window is the input window, its pointer is left alone.  zz_adopt_size() reads the
   geometry and asks the next zz_platform_open() to adopt that window instead of opening a screen. */
static ZZAdopt adopt;static Ptr adopt_window;static Z adopt_req,adopted,screen_ours,adopt_pointer_calls;
int zz_adopt_size(Ptr window,Z *w,Z *h){
 adopt_req=0;
 if(!zz_adopt_geometry(window,&adopt))return 0;
 *w=adopt.w;*h=adopt.h;adopt_window=window;adopt_req=1;return 1;
}
/* the window cannot carry a context (no screen or RastPort, or an inner size that is not a context
   size): the request is withdrawn and the reason goes to the application's output */
void zz_adopt_refused(Z w,Z h){
 Ptr d=Dos;
 adopt_req=0;
 if(!d)Dos=os_openlib("dos.library",37);
 if(Dos){zz_log("fromwindow_refused: the inner size of the window must be 320x240, 640x480 or 800x600\n");zz_num("fromwindow_refused_width",w);zz_num("fromwindow_refused_height",h);}
 if(!d&&Dos){os_closelib(Dos);Dos=0;}
}
static zu8 *ptr_blank;static Z ptr_hidden,ptr_sets;   /* A8.20 */
static Z p96_probe_attempts,p96_fast_fallbacks;
static volatile zu8 *board,*fb,*mb;
/* A8.4: the black box of an armed context, 4 KiB below the control block (blackbox.h) */
volatile unsigned short *zz_blackbox(void){return mb?(volatile unsigned short *)(void *)(mb-4096u):0;}
void zz_bb_push(void){if(mb)os_cache((Ptr)(mb-4096u),672u,1u<<11);}   /* ZZ_BB_WORDS * 2 (blackbox.h; 640 until A8.18) */
/* A4R safe frame dump (ZZMiniGLDump): the last presented 640x480 RGB565 P96 surface, written at close */
static char dump_name[64];static zu8 *dump_buf;static Z dump_frames,dump_valid;
Z zz_profile;
static Z api_ticks,encode_ticks,vertex_ticks,bind_ticks,texture_ticks;
static int use_snapshot;
static Z coherent_reads,coherent_errors;
static Z rate,seq,door,started,width,height,bytes,write_ticks,wait_ticks,show_ticks;
static const char lockname[]="ZZMiniGL.XACP.Core1.A1";
extern void os_addport(Ptr),os_remport(Ptr);
static Z fail(Z error){if(!zz_last_error)zz_last_error=error;return 0;}
static void clean(volatile void *a,Z n){os_cache((Ptr)a,n,1u<<11);}
static Z mread(Z i){
 volatile zu8 *base=mb;
 if(use_snapshot && i>=8)base+=ZZ_SNAPSHOT-ZZ_CTRL-32u;
 volatile unsigned short *p=(volatile unsigned short *)(base+4*i);
 Z a=p[0],b=p[1];return ((a>>8)|(a<<8&0xff00u))|((b>>8)|(b<<8&0xff00u))<<16;
}
static void mwrite(Z i,Z x){
 volatile unsigned short *p=(volatile unsigned short *)(mb+4*i);
 p[1]=(unsigned short)((x>>24)|((x>>8)&0xff00));p[0]=(unsigned short)((x<<8)|((x>>8)&255));
}
static void hw16(Z off,Z x){*(volatile unsigned short *)(board+off)=(unsigned short)x;}
static void copy16(volatile zu8 *d,const zu8 *s,Z n){
 Z i;volatile unsigned short *o=(volatile unsigned short *)d;
 for(i=0;i<n;i+=2){unsigned short v=(unsigned short)((Z)s[i]<<8);if(i+1<n)v|=s[i+1];o[i/2]=v;}
 clean(d,(n+1u)&~1u);
}
/* A3: packet upload with 32-bit Zorro III writes, eight longwords per loop (the byte order in DDR
   is that of the source buffer either way: the 68k puts the most significant byte of a longword at
   the lowest address, exactly as copy16 did word by word).  The 68k source is 4-byte aligned (the
   packet union) and the destination is the packet area; a 16-bit tail is written with copy16.
   ZZMiniGLCopy=WORD16 keeps the A1 16-bit loop for comparison. */
static Z zz_zorro16, zz_gpu_profile, zz_test_noswap;
static void copy32(volatile zu8 *d,const zu8 *s,Z n){
 const Z *src=(const Z *)(const void *)s;volatile Z *dst=(volatile Z *)d;Z i,words=n/4u;
 if(zz_zorro16){copy16(d,s,n);return;}
 for(i=0;i+8u<=words;i+=8u){
   dst[i]=src[i];dst[i+1]=src[i+1];dst[i+2]=src[i+2];dst[i+3]=src[i+3];
   dst[i+4]=src[i+4];dst[i+5]=src[i+5];dst[i+6]=src[i+6];dst[i+7]=src[i+7];
 }
 for(;i<words;i++)dst[i]=src[i];
 if(n&3u)copy16(d+words*4u,s+words*4u,n&3u);
 clean(d,(n+3u)&~3u);
}
Z zz_clock(void){Z t[2];if(!Timer)return 0;clockread(t);return t[1];}
Z zz_transfer_ticks(void){return write_ticks+wait_ticks+show_ticks;}
void zz_io_ticks(Z *io){io[0]=write_ticks;io[1]=staged_wait_ticks;io[2]=wait_ticks;}   /* A8.14: upload, staging wait, ACK wait */
void zz_profile_add(Z start,Z transfer,Z group){
 Z elapsed=zz_clock()-start,io=zz_transfer_ticks()-transfer;
 api_ticks+=elapsed;
 if(group){Z cpu=elapsed>io?elapsed-io:0;encode_ticks+=cpu;
 if(group==2)vertex_ticks+=cpu;else if(group==3)bind_ticks+=cpu;else if(group==4)texture_ticks+=cpu;}
}
static int timer_open(void){
 Z t[2];timer_port=os_port();if(!timer_port)return 0;
 request=os_creq(timer_port,64);if(!request)return 0;
 if(os_opendev("timer.device",0,request,0))return 0;
 Timer=*(Ptr *)((zu8 *)request+20);rate=clockread(t);return rate!=0;
}
static int attach(void){
 Ptr cd;Z size;
 Exp=os_openlib("expansion.library",37);if(!Exp)return 0;
 cd=os_board(0,0x6d6e,4);if(!cd)cd=os_board(0,0x6d6e,10);if(!cd)return 0;
 board=*(volatile zu8 **)((zu8 *)cd+32);size=*(Z *)((zu8 *)cd+36);
 if(!board||size<0x06610000u)return 0;
 fb=board+0x10000;mb=fb+ZZ_CTRL-0x00200000u;return 1;
}
static int query(void){
 volatile Z *cmd=(volatile Z *)(fb+0x04000000u);Z i,ok;
 os_forbid();clean(cmd,128);
 if(cmd[11]==1 || (*(volatile unsigned short *)(board+0x64)==1)){os_permit();return fail(ZE_BUSY);}
 cmd[11]=0;cmd[12]=0;cmd[0]=0x0600;clean(cmd,128);hw16(0x64,0x0600);os_permit();
 for(i=0;i<100;i++){clean(cmd,128);if(cmd[11]==2||cmd[11]==3)break;dos_delay(1);}
 ok=(cmd[11]==2&&!cmd[12]&&cmd[9]==0x52495831&&cmd[10]==0x00020000&&
 (cmd[5]&9)==9&&!(1&(cmd[6]|cmd[7]))&&cmd[8]==0x10000);
 zz_hex("query_magic",cmd[9]);zz_hex("query_abi",cmd[8]);return ok?1:fail(ZE_ABI);
}
static int lock_core(void){
 claim=os_port();if(!claim)return fail(ZE_OWNER);
 *(const char **)((zu8 *)claim+10)=lockname;
 os_forbid();
 if(os_findport(lockname)){os_permit();os_delport(claim);claim=0;return fail(ZE_BUSY);}
 os_addport(claim);os_permit();return 1;
}
int zz_diag(void){
 int ok=0;
 if(!attach())fail(ZE_ABI);
 else if(os_findport(lockname))fail(ZE_BUSY);
 else ok=query();
 if(Exp){os_closelib(Exp);Exp=0;}
 return ok;
}
/* Use the established XACP command protocol. Core0 invalidates the source
   before copying, then cleans the separate destination for the Zorro reader.
   Only ARM-written, cache-line-aligned data is sampled. */
static int snapshot_reply(void){
 const Z src=ZZ_CTRL-0x00200000u+32u, dst=ZZ_SNAPSHOT-0x00200000u;
 unsigned i;Z status;
 os_forbid();
 if(*(volatile unsigned short *)(board+0x64)==1u){os_permit();return 0;}
 hw16(0x64,0x10);hw16(0x68,src>>16);hw16(0x64,0x11);hw16(0x68,src);
 hw16(0x64,0x12);hw16(0x68,dst>>16);hw16(0x64,0x13);hw16(0x68,dst);
 hw16(0x64,0x14);hw16(0x68,0);hw16(0x64,0x15);hw16(0x68,ZZ_SNAPSHOT_BYTES);
 hw16(0x64,1);os_permit();
 for(i=0;i<200000u;i++){
   status=*(volatile unsigned short *)(board+0x64);
   if(status==2u){
     clean(mb+(ZZ_SNAPSHOT-ZZ_CTRL),ZZ_SNAPSHOT_BYTES);use_snapshot=1;coherent_reads++;return 1;
   }
   if(status>2u)break;
 }
 coherent_errors++;return 0;
}
static void direct_reply(void){use_snapshot=0;clean(mb+32,ZZ_SNAPSHOT_BYTES);}
static int wait_reg(Z kind,Z target,Z seconds){
 Z t=zz_clock(),last_copy=t,loops=0,now,v;
 for(;;){
   direct_reply();now=zz_clock();
   if(now-last_copy>=rate/20u || now-t>=seconds*rate){
     snapshot_reply();last_copy=now;
   }
   v=mread(MH_STATUS);
   if(v==ZZ_FATAL){fail(mread(MH_ERROR)?mread(MH_ERROR):ZE_GPU);zz_pinned=1;return 0;}
   if(mread(kind)==target)return 1;
   if(now-t>=seconds*rate){zz_pinned=1;return fail(ZE_TIMEOUT);}
   if(zz_sleep){if((++loops&3u)==0)sleep_us(500u);continue;}   /* A8.1 */
   if((++loops&255u)==0){
     /* A1c: the DIAG4 policy yielded to DOS every 256 polls, often
        quantizing a 15-25ms GPU+DDR operation up to the next DOS tick.
        On ACK only, spin for at most 25ms (with interrupts enabled),
        then restore cooperative delays. Other waits remain unchanged.
        COLD COOP restores the exact DIAG4 delay policy for comparison. */
     if(kind==MH_ACK && !zz_ack_coop && now-t<rate/10u){   /* A3: spin up to 100 ms on the frame ACK */
       zz_ack_spin_blocks++;
     }else{
       if(kind==MH_ACK)zz_ack_delays++;
       dos_delay(1);
     }
   }
 }
}
/* A6: GetVar returns -1 (an unsigned 0xFFFFFFFF here) when the variable does not exist; every
   ">0" test of A1..A5 took that as "present" and then looked at an unwritten buffer.  This returns
   0 for an absent variable and always leaves a terminated (possibly empty) string. */
static Z getvar_safe(const char *name,char *buf,Z len,Z flags){
 Z n;buf[0]=0;n=dos_getvar(name,buf,len,flags);
 if(n==(Z)-1||n>=len){buf[0]=0;return 0;}
 buf[n]=0;return n;
}
int zz_platform_open(Z w,Z h){
 char consent[8];Z best[9],tags[15],id,i;
 adopted=adopt_req;adopt_req=0;screen_ours=0;adopt_pointer_calls=0;   /* A8.27: one request, one context */
 if(zz_pinned)return fail(ZE_BUSY);
 use_snapshot=0;coherent_reads=coherent_errors=0;
 zz_last_error=zz_badslot=0;seq=door=bytes=write_ticks=wait_ticks=show_ticks=fast_show_ticks=0;
 p96_arm=p96_pitch=p96_rgb=p96_handle=0;p96_bitmap=0;
 p96_fast_disabled=p96_fast_verified=0;p96_probe_attempts=p96_fast_fallbacks=0;
 p96_reject_reason=p96_lock_attempts=p96_lock_ret=0;
 p96_bitmap_raw=p96_memory_raw=p96_pitch_raw=p96_format_raw=0;
 p96_fb_raw=0;
 Dos=os_openlib("dos.library",37);if(!Dos)return fail(ZE_BOOT);
 zz_force_safe=getvar_safe("ZZMiniGLPresent",consent,7,512)>0 && consent[0]=='S';
 zz_copy_scalar=getvar_safe("ZZMiniGLCopy",consent,7,512)==6 &&
   consent[0]=='S'&&consent[1]=='C'&&consent[2]=='A'&&
   consent[3]=='L'&&consent[4]=='A'&&consent[5]=='R';
 zz_copy_mode=zz_copy_scalar?ZZ_P96_COPY_SCALAR:
   (getvar_safe("ZZMiniGLCopy",consent,7,512)==5 && consent[0]=='B'&&consent[1]=='U'&&consent[2]=='R'&&consent[3]=='S'&&consent[4]=='T')?
   ZZ_P96_COPY_BURST32:ZZ_P96_COPY_DMA;
 zz_sync=getvar_safe("ZZMiniGLSync",consent,7,512)>0;staged_wait_ticks=0;
 zz_noz=getvar_safe("ZZMiniGLNoZ",consent,7,512)==2 && consent[0]=='O' && consent[1]=='N';   /* A6 diagnostic */   /* GetVar: -1 when absent */
 zz_offscreen_on=getvar_safe("ZZMiniGLOffscreen",consent,7,512)==2 && consent[0]=='O' && consent[1]=='N';   /* A7 */
 win=0;win_opened=0;ptr_hidden=ptr_sets=0;win_keys=0;
 dump_name[0]=0;dump_valid=0;dump_frames=0;
 {Z n=dos_getvar("ZZMiniGLDump",dump_name,63,512);if(n==(Z)-1||n==0||n>62u)dump_name[0]=0;else dump_name[n]=0;}
 zz_zorro16=getvar_safe("ZZMiniGLZorro",consent,7,512)==6 &&
   consent[0]=='W'&&consent[1]=='O'&&consent[2]=='R'&&consent[3]=='D'&&consent[4]=='1'&&consent[5]=='6';
 zz_sleep=getvar_safe("ZZMiniGLWait",consent,7,512)==5 &&
   consent[0]=='S'&&consent[1]=='L'&&consent[2]=='E'&&consent[3]=='E'&&consent[4]=='P';sleep_calls=sleep_ticks=0;
 zz_ack_coop=getvar_safe("ZZMiniGLWait",consent,7,512)==4 &&
   consent[0]=='C'&&consent[1]=='O'&&consent[2]=='O'&&consent[3]=='P';
 zz_ack_delays=zz_ack_spin_blocks=0;
 zz_test_noswap=(getvar_safe("ZZMiniGLP96",consent,7,512)==6 &&
                 consent[0]=='N' && consent[1]=='O' && consent[2]=='S' &&
                 consent[3]=='W' && consent[4]=='A' && consent[5]=='P');
 zz_gpu_profile=(getvar_safe("ZZMiniGLGPUProfile",consent,7,512)==2 &&
                 consent[0]=='O' && consent[1]=='N');
 {
   /* A8.2: the cold-start consent comes from ZZMiniGLLoad (armed, one shot) or from ZZMiniGLCold=COLD, a
      process-local variable or a global one (SetEnv); both are consumed here */
   int armed=zz_armed!=0,local,any;
   zz_armed=0;ctx_armed=(Z)armed;ctx_flags=armed?arm_flags:0u;arm_flags=0;
   if(armed)zz_hex("arm_flags",ctx_flags);
   local=getvar_safe("ZZMiniGLCold",consent,7,512)==4&&consent[0]=='C'&&consent[1]=='O'&&consent[2]=='L'&&consent[3]=='D';
   any=getvar_safe("ZZMiniGLCold",consent,7,0)==4&&consent[0]=='C'&&consent[1]=='O'&&consent[2]=='L'&&consent[3]=='D';
   if(!armed&&!local&&!any){
     zz_log("auth_refused: run ZZMiniGLLoad just before the application (or set ZZMiniGLCold COLD)\n");
     zz_num("auth_armed",0);zz_num("auth_local_var",0);zz_num("auth_any_var",0);
     return fail(ZE_AUTH);
   }
   zz_num("auth_armed",(Z)armed);zz_num("auth_local_var",(Z)local);zz_num("auth_any_var",(Z)any);
   dos_delvar("ZZMiniGLCold",512);
   if(any&&!local)dos_delvar("ZZMiniGLCold",256);
 }
 zz_profile=getvar_safe("ZZMiniGLBench",consent,7,512)>0;
 api_ticks=encode_ticks=vertex_ticks=bind_ticks=texture_ticks=0;
 if(!attach())return fail(ZE_ABI);
 if(!lock_core()||!query())return 0;
 clean(mb,128);
 if(mread(MH_MAGIC)==ZZ_MAGIC && mread(MH_COOKIE)==ZZ_COOKIE &&
    (mread(MH_STATUS)!=ZZ_STOPPED || mread(MH_EXIT)!=0xc0debad1u)){
   /* A8.6: a session that did not stop.  In this boot its owner keeps the claim port (a session pinned
      after a Core1 timeout, or an application that died) and lock_core() has already refused (ZE_BUSY).
      Here the port is free: the session belongs to a boot that ended with an Amiga reset, and the
      firmware's arm_app_run resets CPU1 before the new image runs.  Only
      a session that had reported a Core1 or GPU failure is refused: RasterIX may still be hung and
      only a power cycle clears it. */
   Z st=mread(MH_STATUS),er=mread(MH_ERROR),ft=mread(MH_FAULT_TYPE);
   zz_num("stale_status",st);zz_hex("stale_error",er);zz_hex("stale_fault_type",ft);
   /* A8.8: a refused packet (ZE_HEADER..ZE_ARG, ZE_SEQ: FAULT_TYPE 0x8...) ends the service loop, which
      returns to the firmware: relaunched.  Refused: GPU failure, timeout, CPU exception on Core1. */
   if(st==ZZ_FATAL&&(er==ZE_GPU||er==ZE_TIMEOUT||er>=0xe1f0u||(ft&&ft<0x100u))){
     zz_log("stale_refused: the previous session reported a Core1 or GPU failure: power the Amiga off and on\n");
     return fail(ZE_STALE);
   }
   zz_num("stale_previous_boot",1);
 }
 if(!timer_open())return fail(ZE_BOOT);
 P96=os_openlib("Picasso96API.library",2);if(!P96)return fail(ZE_P96);
 if(adopted){
   /* A8.27: the application's screen.  Nothing is opened; the P96 lock and the copy are as for our own
      screen, at the place of the window's inner area (zz_fast_lock), or through the window's RastPort. */
   screen=adopt.screen;
   zz_num("fromwindow",1);zz_num("fromwindow_width",adopt.w);zz_num("fromwindow_height",adopt.h);
   zz_num("fromwindow_direct",adopt.direct);zz_num("fromwindow_slow_reason",adopt.why);
   zz_num("fromwindow_x",adopt.x);zz_num("fromwindow_y",adopt.y);
   if(adopt.w!=w||adopt.h!=h)return fail(ZE_P96);
 }else{
 best[0]=0x80000063;best[1]=w;best[2]=0x80000064;best[3]=h;
 best[4]=0x80000065;best[5]=16;best[6]=0x80000061;best[7]=(1u<<4)|(1u<<10);best[8]=0;
 id=p96_best(best);if(id==0xffffffffu||!id)return fail(ZE_P96);
 tags[0]=0x80020072;tags[1]=id;tags[2]=0x80020063;tags[3]=w;tags[4]=0x80020064;tags[5]=h;
 tags[6]=0x80020065;tags[7]=16;tags[8]=0x80020074;tags[9]=0;tags[10]=0x80020076;tags[11]=1;
 tags[12]=0x80020068;tags[13]=(Z)"ZZMiniGL A1b";tags[14]=0;
 screen=p96_open(tags);if(!screen)return fail(ZE_P96);
 screen_ours=1;
 if(*(short *)((zu8 *)screen+12)!=(short)w || *(short *)((zu8 *)screen+14)!=(short)h)return fail(ZE_P96);
 }
 width=w;height=h;
 /* Blob and stack are exclusively owned following the explicit cold-start claim. */
 if(!zz_arm_size||zz_arm_size>ZZ_CTRL-ZZ_BASE)return fail(ZE_SIZE);
 copy16(fb+ZZ_BASE-0x00200000u,zz_arm_image,zz_arm_size);
 {volatile unsigned short *v=(volatile unsigned short *)(fb+ZZ_BASE-0x00200000u);
  for(i=0;i<zz_arm_size;i+=2){Z expected=(Z)zz_arm_image[i]<<8;if(i+1<zz_arm_size)expected|=zz_arm_image[i+1];if(v[i/2]!=expected)return fail(ZE_BOOT);}}
 for(i=0;i<MH_CTRL_WORDS;i++)mwrite(i,0);
 mwrite(MH_MAGIC,ZZ_MAGIC);mwrite(MH_VERSION,ZZ_PROTO);mwrite(MH_COOKIE,ZZ_COOKIE);
 mwrite(MH_WIDTH,w);mwrite(MH_HEIGHT,h);
 mwrite(MH_PROFILE_SWITCH,zz_gpu_profile);mwrite(MH_NOZBUF,zz_noz);clean(mb,MH_CTRL_WORDS*4u);
 /* Let the firmware vblank flush publish the uploaded image and mailbox before GO. */
 dos_delay(3);started=1;
 hw16(0x90,ZZ_BASE>>16);hw16(0x92,ZZ_BASE);
 return wait_reg(MH_STATUS,ZZ_READY,12);
}
int zz_acked(Z s){direct_reply();return (Z)(mread(MH_ACK)-s)<0x80000000u;}   /* A8.23: one look, no wait */
int zz_wait(void){Z t;if(!seq)return 1;t=zz_clock();if(!wait_reg(MH_ACK,seq,5))return 0;wait_ticks+=zz_clock()-t;return 1;}
/* A5: before overwriting ZZ_DATA the 68k only needs Core1 to have staged (or fully decoded) the
   previous packet, not to have finished rendering it; the ACK of a frame is awaited by the
   presentation code before the P96 bitmap is unlocked.  ZZMiniGLSync restores the A4R order. */
int zz_pipelined(void){return !zz_sync;}
int zz_offscreen(void){return zz_offscreen_on!=0;}
Z zz_eclock_hz(void){return Timer?rate:0;}
/* A7 MGLEnableSync: spin on the EClock up to a slot (never more than 100 ms) */
void zz_spin_until(Z target){
 Z t0=zz_clock(),now,d;
 if(zz_sleep){
   /* A8.1: sleep up to 0.4 ms before the slot, then poll the last part */
   now=t0;d=target-now;
   if((int)d>0 && d<rate/10u && d>rate/2500u)sleep_us((Z)(((unsigned long long)(d-rate/2500u)*1000000ull)/rate));
 }
 while((int)(target-zz_clock())>0 && zz_clock()-t0<rate/10u){}
}
/* A7: a borderless backdrop window covering the context's own screen, opened with a classic
   NewWindow (OpenWindow, intuition.library V39+) on the first MGLGetWindowHandle.  IDCMP_MOUSEBUTTONS and
   IDCMP_RAWKEY; RMBTRAP so that the right button never draws a menu bar into the bitmap; simple
   refresh without refresh events: Intuition never redraws it.  Closed before the screen. */
Ptr zz_window(void){
 zu8 nw[48];Z i;
 if(adopted)return (screen&&!zz_pinned&&started)?adopt_window:0;   /* A8.27: the application's window */
 if(win)return win;
 if(!screen||zz_pinned||!started)return 0;
 if(!Int)Int=os_openlib("intuition.library",39);
 if(!Int)return 0;
 for(i=0;i<48u;i++)nw[i]=0;
 *(unsigned short *)(nw+4)=(unsigned short)width;*(unsigned short *)(nw+6)=(unsigned short)height;
 nw[8]=0;nw[9]=1;                                   /* DetailPen, BlockPen */
 *(Z *)(nw+10)=0x00000008u|0x00000400u;             /* IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY */
 *(Z *)(nw+14)=0x00000100u|0x00000800u|0x00001000u|0x00010000u|0x00000040u|0x00020000u;
                                                    /* BACKDROP BORDERLESS ACTIVATE RMBTRAP SIMPLE_REFRESH NOCAREREFRESH */
 *(Ptr *)(nw+30)=screen;                            /* NewWindow.Screen */
 *(unsigned short *)(nw+46)=0x000Fu;                /* CUSTOMSCREEN */
 win=int_openwindow(nw);
 if(win)win_opened++;
 return win;
}
/* A8.20: MGLSetPointer / MGLClearPointer.  MiniGL gives the context's window an empty pointer and the
   applications count on it (GLQuake calls mglSetPointer once its screen is open): the input window gets a
   one-line pointer with no pixel set (chip RAM), and its own pointer back on MGLClearPointer. */
void zz_pointer(int hide){
 if(adopted){adopt_pointer_calls++;return;}   /* A8.27: the pointer of the application's window is the application's */
 if(hide){
   Ptr w=zz_window();
   if(!w)return;
   if(!ptr_blank)ptr_blank=(zu8 *)os_alloc(16u,0x00010002u);   /* MEMF_CHIP | MEMF_CLEAR */
   if(!ptr_blank)return;
   int_setpointer(w,ptr_blank,1u,16u,0u,0u);ptr_hidden=1;ptr_sets++;
 }else if(win&&ptr_hidden){int_clearpointer(win);ptr_hidden=0;}
}
/* A8.28: MGLMainLoop hands the keys to the application's key function as characters.  The library's own
   input window reports IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY (GLQuake reads raw keys): IDCMP_VANILLAKEY is
   added for the time of the loop, so that a key with a character arrives as that character and the
   others still as raw keys.  A window of the application keeps the events the application chose. */
void zz_window_keys(int on){
 if(adopted||!win||!Int)return;
 if(on&&!win_keys){int_modifyidcmp(win,0x00000008u|0x00000400u|0x00200000u);win_keys=1;}
 else if(!on&&win_keys){int_modifyidcmp(win,0x00000008u|0x00000400u);win_keys=0;}
}
static void window_close(void){
 if(win&&ptr_hidden){int_clearpointer(win);ptr_hidden=0;}
 win_keys=0;
 if(win){
   Ptr port=*(Ptr *)((zu8 *)win+86),m;               /* Window.UserPort */
   if(port)while((m=os_getmsg(port))!=0)os_replymsg(m);
   int_closewindow(win);win=0;
 }
 if(ptr_blank){os_free(ptr_blank,16u);ptr_blank=0;}   /* after the window that showed it */
 if(Int){os_closelib(Int);Int=0;}
}
static int wait_staged(void){Z t;if(!seq)return 1;if(mread(MH_STAGED)==seq)return 1;   /* A8.18: already staged, no clock read */
 t=zz_clock();if(!wait_reg(MH_STAGED,seq,5))return 0;staged_wait_ticks+=zz_clock()-t;return 1;}
int zz_send(const zu8 *s,Z n,int sync){
 Z t=0;int timed=zz_profile||(ctx_flags&ZZ_ARM_PROFILE);   /* A8.19: the two EClock reads only when the upload time is reported */
 if(!started||zz_pinned||n>ZZ_LIMIT||!(zz_sync?zz_wait():wait_staged()))return 0;
 if(timed)t=zz_clock();
 seq=znext(seq);if(zget(s+8)!=seq)return fail(ZE_SEQ);
 copy32(fb+ZZ_DATA-0x00200000u,s,n);mwrite(MH_BYTES,n);mwrite(MH_PUBLISH,seq);clean(mb,32);
 door^=1u;mwrite(MH_RESERVED,door);clean(mb,32);
 if(timed)write_ticks+=zz_clock()-t;
 bytes+=n;return sync?zz_wait():1;
}
/* p96LockBitMap pins only this dedicated screen's bitmap for the duration
   of a synchronous Core1 presentation. Its RenderInfo reports an actual
   ZZ9000 board-memory address, otherwise the safe A1a path is used. */
int zz_fast_lock(Z *target,Z *pitch){
 struct {Ptr pixels;unsigned short row,pad;Z format;} ri;
 Z cpu,base,offset,bytes,arm,hold;
 Ptr bitmap;
 if(zz_force_safe){if(!p96_reject_reason)p96_reject_reason=1;return 0;}
 if(p96_fast_disabled)return 0;
 if(!P96 || !screen || !fb){if(!p96_reject_reason)p96_reject_reason=2;return 0;}
 if(p96_handle){if(!p96_reject_reason)p96_reject_reason=3;return 0;}
 if(adopted&&!adopt.direct){p96_reject_reason=20u+adopt.why;p96_fast_disabled=1;return 0;}   /* A8.27: 21..24, adopt.h */
 /* Screen.RastPort begins at +84 (already used by p96_write).
    RastPort starts with Layer* at +0, then BitMap* at +4.
    The A1b/DIAG2 bug dereferenced Layer* (+84), which is NULL for
    this full-screen P96 screen. Use Screen.RastPort.BitMap at +88. */
 bitmap=*(Ptr *)((zu8 *)screen+88);
 p96_bitmap_raw=(Z)(unsigned long)bitmap;
 p96_fb_raw=(Z)(unsigned long)fb;
 if(!bitmap){p96_reject_reason=10;p96_fast_disabled=1;return 0;}
 ri.pixels=0;ri.row=0;ri.pad=0;ri.format=0;
 p96_lock_attempts++;
 hold=p96_lock(bitmap,&ri,sizeof ri);
 p96_lock_ret=hold;
 p96_memory_raw=(Z)(unsigned long)ri.pixels;
 p96_pitch_raw=(Z)ri.row;
 p96_format_raw=ri.format;
 if(!hold){p96_reject_reason=11;p96_fast_disabled=1;return 0;}
 p96_bitmap=bitmap;p96_handle=hold;
 p96_rgb=ri.format;
 cpu=(Z)(unsigned long)ri.pixels;base=(Z)(unsigned long)fb;
 if(adopted){
   /* A8.27: the inner area of the window inside the screen's bitmap */
   if(!zz_adopt_target(&adopt,width,(Z)ri.row,&cpu)){p96_reject_reason=25;p96_fast_disabled=1;zz_fast_unlock();return 0;}
 }
 if(cpu<base){p96_reject_reason=12;p96_fast_disabled=1;zz_fast_unlock();return 0;}
 /* P96 on this actual ZZ9000 selects RGBFB_R5G6B5 (10, BE).
    Core1 converts each RGB565 word when copying to this mode. */
 if(ri.format!=ZZ_P96_FMT_RGB565_PC && ri.format!=ZZ_P96_FMT_RGB565_BE){
   p96_reject_reason=13;p96_fast_disabled=1;zz_fast_unlock();return 0;
 }
 if((cpu&3u)||(ri.row&3u)||ri.row<width*2u||ri.row>4096u){
   p96_reject_reason=14;p96_fast_disabled=1;zz_fast_unlock();return 0;
 }
 offset=cpu-base;arm=0x00200000u+offset;
 bytes=(height-1u)*(Z)ri.row+width*2u;
 if(offset>0x03e00000u || arm<ZZ_P96_LOW_ARM ||
    bytes>ZZ_P96_HIGH_ARM-ZZ_P96_LOW_ARM ||
    arm>ZZ_P96_HIGH_ARM-bytes){
   p96_reject_reason=15;p96_fast_disabled=1;zz_fast_unlock();return 0;
 }
 if(arm!=p96_arm || (Z)ri.row!=p96_pitch)p96_fast_verified=0;
 p96_arm=arm;p96_pitch=ri.row;
 *target=arm;*pitch=ri.row;
 return 1;
}
void zz_fast_unlock(void){
 if(p96_handle){p96_unlock(p96_bitmap,p96_handle);p96_handle=0;p96_bitmap=0;}
}
/* Endian-independent probe words are derived from the bytes that the 68k
   actually reads through the P96 aperture, not from its big-endian UWORDs.
   The two marker words are overwritten by the first rendered frame. */
int zz_fast_probe(Z *a,Z *b){
 volatile unsigned short *p;const volatile zu8 *c;
 if(!p96_handle || !p96_bitmap)return 0;
 p=(volatile unsigned short *)((zu8 *)fb+p96_arm-0x00200000u);
 p[0]=0x1357;p[1]=0x2468;p[2]=0xAC39;p[3]=0x5B21;
 clean(p,8);c=(const volatile zu8 *)p;
 *a=(Z)c[0]|((Z)c[1]<<8)|((Z)c[2]<<16)|((Z)c[3]<<24);
 *b=(Z)c[4]|((Z)c[5]<<8)|((Z)c[6]<<16)|((Z)c[7]<<24);
 p96_probe_attempts++;
 return 1;
}
int zz_fast_probe_ok(void){
 direct_reply();snapshot_reply();
 if(mread(MH_P96_PROBE)==1u){p96_fast_verified=1;return 1;}
 p96_reject_reason=16;p96_fast_disabled=1;p96_fast_fallbacks++;return 0;
}
/* A4R: optional capture of the displayed frame.  With the environment variable ZZMiniGLDump set to
   a file name, every completed direct presentation copies the P96 bitmap the viewer sees (640x480
   RGB565 as stored on the board, pitch 1280, byte order of the screen format) through the Zorro
   aperture into a 68k buffer; the LAST frame is written to the file when the context closes.  Pure
   68k reads of the library's own locked bitmap after the ACK: nothing on Core1 changes, nothing is
   written to the board.  Dump runs are slow (about 614 KB per frame over Zorro) and are not
   benchmarks: use them with COLD 1 HOLD. */
static void dump_capture(void){
 const volatile Z *src;Z *dst,y,x;
 if(!dump_name[0]||!p96_arm||p96_pitch<1280u)return;
 if(!dump_buf){dump_buf=os_alloc(614400u,0);if(!dump_buf){dump_name[0]=0;return;}}
 for(y=0;y<480u;y++){
   src=(const volatile Z *)((zu8 *)fb+p96_arm-0x00200000u+y*p96_pitch);dst=(Z *)(dump_buf+y*1280u);
   for(x=0;x<320u;x++)dst[x]=src[x];
 }
 dump_frames++;dump_valid=1;
}
static void dump_write(void){
 Z fh;
 if(!dump_valid||!dump_buf)return;
 fh=dos_open(dump_name,1006u);
 if(fh){dos_write(fh,dump_buf,614400u);dos_close(fh);zz_log("frame_dump_written=1\n");}else zz_log("frame_dump_written=0\n");
 zz_num("frame_dump_frames",dump_frames);zz_num("frame_dump_format",p96_rgb);zz_num("frame_dump_pitch",1280u);
}
void zz_fast_timing(Z ticks){fast_show_ticks+=ticks;dump_capture();}
int zz_fast_verified(void){return p96_fast_verified;}
Z zz_fast_format(void){return zz_test_noswap?ZZ_P96_FMT_RGB565_PC:p96_rgb;}
Z zz_fast_copy_mode(void){return zz_copy_mode;}
int zz_present(void){
 struct {Ptr pixels;short pitch,pad;Z format;} ri;
 /* A8.26: Core1's buffers are 640 x 481 (pitch 1280), or 800 x 601 (pitch 1600) for an 800x600 context */
 Z bw=width==800u?800u:640u,bh=width==800u?600u:480u,bp=bw*2u;
 Z base=mread(MH_BUF),skip=(bh-height)*bp,t;
 if(base!=0x06600000u+skip && base!=0x06600000u+bw*(bh+1u)*2u+skip)return fail(ZE_ARG);
 ri.pixels=(Ptr)(fb+base-0x00200000u);ri.pitch=(short)bp;ri.pad=0;ri.format=4;
 t=zz_clock();clean(ri.pixels,(height-1u)*bp+width*2u);
 if(adopted)p96_write(&ri,0,0,adopt.rastport,adopt.rx,adopt.ry,width,height);   /* A8.27: clipped by the window's layer */
 else p96_write(&ri,0,0,(zu8 *)screen+84,0,0,width,height);
 show_ticks+=zz_clock()-t;return 1;
}
Z zz_metric(unsigned k){
 if(!mb)return 0;
 clean(mb+32,96);
 if(k>=32u&&k<MH_CTRL_WORDS){volatile zu8 *b=mb;if(use_snapshot)b+=ZZ_SNAPSHOT-ZZ_CTRL-32u;clean(b+((4u*k)&~31u),32);}   /* A8.3: words 32..63 (heartbeat) */
 return mread(k);
}
void zz_summary(void){
 if(!Dos)return;
 zz_hex("error",zz_last_error);zz_num("missing_dispatch_slot",zz_badslot);
 if(mb&&started){zz_hex("core1_exit_marker",mread(MH_EXIT));zz_num("stop_status",mread(MH_STATUS));}
 if(mb&&started){direct_reply();snapshot_reply();
 zz_hex("arm_boot_trace",mread(32));zz_hex("entry_cpsr",mread(33));zz_hex("entry_mpidr",mread(34));
 zz_hex("entry_sp_after_push",mread(35));zz_num("reply_via_core0",use_snapshot);zz_num("coherent_reads",coherent_reads);zz_num("coherent_errors",coherent_errors);
 zz_hex("status",mread(MH_STATUS));zz_num("phase",mread(MH_PHASE));zz_hex("arm_error",mread(MH_ERROR));
 zz_num("ladder_failed_plus_one",mread(MH_LADDER));zz_hex("gpu_id",mread(MH_GPU_ID));zz_hex("caps",mread(MH_CAPS));
 zz_num("frames",mread(MH_FRAMES));zz_num("gpu_triangles",mread(MH_TRIS));zz_hex("pixel_hash",mread(MH_CHECK));zz_num("samples_different_from_corner",mread(MH_VARY));
 { Z cs=mread(MH_NONZERO);
   zz_num("gpu_chromatic_samples",cs&65535u);
   zz_num("p96_copy_mismatch_samples",cs>>16);
 }
 zz_hex("fault_pc",mread(MH_FAULT_PC));zz_hex("fault_far",mread(MH_FAULT_FAR));zz_num("fault_type",mread(MH_FAULT_TYPE));
 zz_num("first_gpu_submit_to_frame_fence_us",mread(MH_GPU_SPAN_US));zz_num("core1_us_excluding_final_fences",mread(MH_CPU_US));zz_num("final_gpu_fence_wait_us",mread(MH_FENCE_US));}
 if(zz_profile){zz_num("dispatch_ticks",api_ticks);zz_num("encode_api_ticks",encode_ticks);zz_num("vertex_api_ticks",vertex_ticks);zz_num("bind_api_ticks",bind_ticks);zz_num("image_api_ticks",texture_ticks);}
 zz_hex("p96_a2_marker",0xA2A2u);
 zz_hex("p96_a3_marker",0xA3A3u);
 zz_hex("p96_a3b_marker",0xA3B1u);
 zz_hex("p96_a3d_marker",0xA3D1u);
 zz_hex("p96_a4r_marker",0xA4F1u);
 zz_hex("p96_a5_marker",0xA5F1u);
 zz_hex("p96_a6_marker",0xA6F1u);
 zz_hex("p96_a7_marker",0xA7F1u);
 zz_hex("p96_a8_marker",0xA8F1u);
 zz_hex("p96_a81_marker",0xA81Fu);   /* A8.1: sleeping waits */
 zz_hex("p96_a82_marker",0xA82Fu);   /* A8.2: ZZMiniGLLoad authorisation */
 zz_hex("p96_a83_marker",0xA83Fu);   /* A8.3: heartbeat file for an armed context */
 zz_hex("p96_a84_marker",0xA84Fu);   /* A8.4: black box for an armed context */
 zz_hex("p96_a85_marker",0xA85Fu);   /* A8.5: ARGB4444 lightmap blend, unknown capabilities, trap record */
 zz_hex("p96_a86_marker",0xA86Fu);   /* A8.6: session left by an Amiga reset relaunched */
 zz_hex("p96_a87_marker",0xA87Fu);   /* A8.7: diagnostic switches from ZZMiniGLLoad */
 zz_hex("p96_a88_marker",0xA88Fu);   /* A8.8: refused packet record published by Core1 */
 zz_hex("p96_a89_marker",0xA89Fu);   /* A8.9: armed context: P96 lock released before MGLSwitchDisplay returns */
 zz_hex("p96_a810_marker",0xA810u);   /* A8.10: 2560 texture names, ZZGL state in the Core1 arena */
 zz_hex("p96_a811_marker",0xA811u);   /* A8.11: the context stays open when the application clears currentContext */
 zz_hex("p96_a812_marker",0xA812u);   /* A8.12: second texture unit emulated (deferred lightmap pass), MiniGL array types */
 zz_hex("p96_a813_marker",0xA813u);   /* A8.13: frame in 32 KiB chunks, unit-1 pass repeats the unit-0 records */
 zz_hex("p96_a814_marker",0xA814u);   /* A8.14: PROFILE split of the 68060 time */
 zz_hex("p96_a815_marker",0xA815u);   /* A8.15: NODRAW keeps the 2D */
 zz_hex("p96_a816_marker",0xA816u);   /* A8.16: additive triangles snapped off the sample grid edges */
 zz_hex("p96_a817_marker",0xA817u);   /* A8.17: raw records, unit-1 pass and vertex cache on Core1, lightmap rows that changed */
 zz_hex("p96_a818_marker",0xA818u);   /* A8.18: array packing without a call per vertex, cache compared before packing, 4 KiB packets */
 zz_hex("p96_a819_marker",0xA819u);   /* A8.19: packing loops for GLQuake's layouts, index cache, current colour for arrays, PAULAGUARD */
 zz_hex("p96_a820_marker",0xA820u);   /* A8.20: MGLSetPointer hides the pointer */
 zz_hex("p96_a829_marker",0xA829u);   /* A8.29: Core1 writes no depth while GL_DEPTH_TEST is disabled */
 zz_hex("p96_a828_marker",0xA828u);   /* A8.28: MGLMainLoop, glPixelStorei, gluPerspective, the locked arrays kept over a second-texture pass */
 zz_hex("p96_a827_marker",0xA827u);   /* A8.27: MGLCreateContextFromWindow, textures up to 2048 reduced to 256, lines and points of array draws */
 zz_num("fromwindow_adopted",adopted);zz_num("fromwindow_pointer_calls",adopt_pointer_calls);
 zz_hex("p96_a826_marker",0xA826u);   /* A8.26: 800x600 */
 zz_hex("p96_a825_marker",0xA825u);   /* A8.25: Z_SHOW copies with the CPU while a frame is open */
 zz_hex("p96_a824_marker",0xA824u);   /* A8.24: point size rounded down */
 zz_hex("p96_a823_marker",0xA823u);   /* A8.23: LATESHOW (Z_SHOW), GL_POINTS drawn */
 zz_hex("p96_a822_marker",0xA822u);   /* A8.22: Core1 queues the staged packets (the 68k side of the exchange is unchanged) */
 zz_hex("p96_a821_marker",0xA821u);   /* A8.21: GL_RGL array hints, static store on Core1, 8-bit lightmap rows that changed */
 zz_num("p96_noswap_requested",zz_test_noswap);
 if(mb&&started)zz_hex("core1_texture_ddr_fnv",mread(MH_TEX_FNV));
 if(mb&&started){zz_num("core1_decode_us",mread(MH_DECODE_US));zz_num("core1_xform_us",mread(MH_XFORM_US));
 zz_num("core1_setup_us",mread(MH_SETUP_US));zz_num("core1_ringwait_us",mread(MH_RINGWAIT_US));
 zz_num("core1_verts",mread(MH_VERTS));zz_num("core1_resident_verts",mread(MH_RESIDENT));
 zz_num("core1_indexed_tris",mread(MH_IDX_TRIS));zz_num("core1_chunks",mread(MH_CHUNKS));
 zz_num("core1_gl_errors",mread(MH_GL_ERRORS));zz_hex("core1_last_gl_error",mread(MH_LAST_GL));
 zz_num("core1_fast_screen_tris",mread(MH_FAST_CLIP_TRIS));
 zz_num("core1_screen_clipper_tris",mread(MH_SCREEN_CLIP_TRIS));
 zz_num("core1_descriptor_us",mread(MH_DESC_US));
 zz_num("core1_emit_us",mread(MH_EMIT_US));
 zz_num("core1_gpu_profile",mread(MH_PROFILE_ACTIVE));}
 zz_num("zorro_write32",!zz_zorro16);
 zz_num("p96_copy_requested_burst",!zz_copy_scalar);
 zz_num("ack_early_spin",!zz_ack_coop);
 zz_num("wait_sleep",zz_sleep);zz_num("wait_sleep_calls",sleep_calls);zz_num("wait_sleep_us",zz_ticks_to_us(sleep_ticks));   /* A8.1 */
 zz_num("ack_delay_calls",zz_ack_delays);
 zz_num("ack_spin_blocks_256",zz_ack_spin_blocks);
 zz_num("p96_fast_probe_attempts",p96_probe_attempts);
 zz_num("p96_fast_fallbacks",p96_fast_fallbacks);
 zz_num("p96_fast_disabled",p96_fast_disabled);
 zz_num("p96_reject_reason",p96_reject_reason);
 zz_num("p96_lock_attempts",p96_lock_attempts);
 zz_hex("p96_lock_ret",p96_lock_ret);
 zz_hex("p96_bitmap_raw",p96_bitmap_raw);
 zz_hex("p96_memory_raw",p96_memory_raw);
 zz_hex("p96_fb_raw",p96_fb_raw);
 zz_num("p96_pitch_raw",p96_pitch_raw);
 zz_num("p96_format_raw",p96_format_raw);
 zz_hex("p96_direct_arm",p96_arm);zz_num("p96_direct_pitch",p96_pitch);
 zz_num("p96_direct_format",p96_rgb);
 zz_num("p96_copy_format_effective",zz_test_noswap?ZZ_P96_FMT_RGB565_PC:p96_rgb);
 if(mb&&started){zz_num("p96_probe_result",mread(MH_P96_PROBE));
 zz_num("p96_fast_frames",mread(MH_P96_FAST_FRAMES));
 zz_num("p96_copy_core1_us",mread(MH_P96_COPY_US));
 zz_num("p96_rejected_core1",mread(MH_P96_REJECTS));
 zz_num("p96_dma_frames",mread(MH_P96_DMA_FRAMES));
 zz_num("p96_dma_faults",mread(MH_P96_DMA_FAULTS));
 zz_hex("p96_dma_info",mread(MH_P96_DMA_INFO));}
 zz_num("p96_copy_mode_requested",zz_copy_mode);   /* 0 scalar, 1 burst32, 2 PL330 */
 zz_num("present_pipelined",!zz_sync);
 zz_num("zbuf_detached",zz_noz);
 zz_num("offscreen",zz_offscreen_on);zz_num("input_window_opened",win_opened);zz_num("pointer_hidden_calls",ptr_sets);   /* A7 */
 if(mb&&started){Z ts=mread(MH_TEX_STATS);zz_num("core1_tex_refused",ts&0xFFFFu);zz_num("core1_tex_stored_reduced",ts>>16);}   /* A8 */
 zz_num("p96_fast_dispatch_ticks",fast_show_ticks);
 zz_num("bytes_zorro",bytes);zz_num("eclock_hz",rate);zz_num("upload_ticks",write_ticks);zz_num("ack_wait_ticks",wait_ticks);zz_num("staged_wait_ticks",staged_wait_ticks);zz_num("p96_ticks",show_ticks);
 /* A5: the same counters per presented frame, in microseconds (integer), readable without any tool.
    Nested scopes as before: setup contains the ring wait; nothing here is to be added up. */
 if(mb&&started&&mread(MH_FRAMES)){
   Z f=mread(MH_FRAMES);
   zz_num("frames_presented",f);
   zz_num("us_per_frame_core1_setup",mread(MH_SETUP_US)/f);
   zz_num("us_per_frame_core1_ringwait",mread(MH_RINGWAIT_US)/f);
   {Z su=mread(MH_SETUP_US),rw=mread(MH_RINGWAIT_US);zz_num("us_per_frame_core1_setup_without_ringwait",(su>rw?su-rw:0u)/f);}   /* A8.29: the wait may exceed the setup time it was once part of */
   zz_num("us_per_frame_core1_xform",mread(MH_XFORM_US)/f);
   zz_num("us_per_frame_gpu_span",mread(MH_GPU_SPAN_US)/f);
   zz_num("us_per_frame_final_fence_wait",mread(MH_FENCE_US)/f);
   zz_num("us_per_frame_p96_copy",mread(MH_P96_COPY_US)/f);
   zz_num("chunks_per_frame_x10",mread(MH_CHUNKS)*10u/f);          /* historical diagnostic reference */
   zz_num("list_words_per_frame",mread(MH_LIST_WORDS)/f);         /* historical diagnostic reference */
   zz_num("gpu_triangles_per_frame",mread(MH_TRIS)/f);
   zz_num("upload_timed",(zz_profile||(ctx_flags&ZZ_ARM_PROFILE))?1u:0u);   /* A8.19: 0 = upload_ticks and us_per_frame_68k_upload not measured */
   zz_num("us_per_frame_68k_upload",zz_ticks_to_us(write_ticks)/f);
   zz_num("us_per_frame_68k_ack_wait",zz_ticks_to_us(wait_ticks)/f);
   zz_num("us_per_frame_68k_staged_wait",zz_ticks_to_us(staged_wait_ticks)/f);
 }
}
/* eclock ticks -> microseconds without overflow (ticks up to 2^32, rate about 709 kHz) */
Z zz_ticks_to_us(Z ticks){unsigned long long t=(unsigned long long)ticks*1000000ull;return rate?(Z)(t/(unsigned long long)rate):0;}
void zz_platform_close(void){
 if(started&&!zz_pinned){
   if(wait_reg(MH_EXIT,0xc0debad1u,5)&&mread(MH_STATUS)==ZZ_STOPPED){
     /* The exit marker precedes the firmware epilogue. Let it return before parking. */
     dos_delay(2);hw16(0x90,0);hw16(0x92,0);
   }else zz_pinned=1;
 }
 zz_summary();
 dump_write();if(dump_buf){os_free(dump_buf,614400u);dump_buf=0;}
 /* On an uncertain Core1 timeout, do not free a bitmap while ARM might
    still be copying into it. The live P96 resources intentionally remain
    pinned until the whole system is power-cycled. */
 if(!zz_pinned){zz_fast_unlock();window_close();if(screen){if(screen_ours)p96_close(screen);screen=0;}   /* A8.27: an adopted screen is the application's */
    if(P96){os_closelib(P96);P96=0;}}
 if(Timer){os_closedev(request);Timer=0;}if(request){os_dreq(request);request=0;}
 if(timer_port){os_delport(timer_port);timer_port=0;}
 if(claim&&!zz_pinned){os_remport(claim);os_delport(claim);claim=0;}
 if(Exp){os_closelib(Exp);Exp=0;}if(Dos){os_closelib(Dos);Dos=0;}
 started=0;mb=0;fb=0;board=0;
}
