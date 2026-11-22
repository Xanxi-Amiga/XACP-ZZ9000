/* loader.c - ZZMiniGLLoad A8: puts the minigl.library of its own directory in memory, so that an unmodified
 * MiniGL application (GLQuake) that opens "minigl.library" by name gets this one, with nothing copied into
 * LIBS: and no Assign.  Exec searches the libraries already in memory first.
 *   ZZMiniGLLoad          load PROGDIR:minigl.library (identity checked) and leave it in memory, unused
 * It refuses when a different minigl.library is already in memory: then "Avail FLUSH" (or a reboot) first.
 * The library stays in memory, closed, until "Avail FLUSH" or a reboot removes it.
 * A8.2: it also authorises the next MGLCreateContext once (private LVO -36 of this build), so the
 * application needs no ZZMiniGLCold variable: run ZZMiniGLLoad just before every GLQuake start.
 * A8.4: first, the black box left in ZZ9000 memory by the previous armed run (blackbox.h), if any, is
 * decoded into PROGDIR:zzminigl_blackbox.txt (and the Shell), then cleared.  "ZZMiniGLLoad BLACKBOX" does
 * only that, without loading anything: run it first after a crash, before any other ZZ9000 program. */
#include "abi.h"
#include "os.h"
#include "blackbox.h"
extern const char *const zz_slot_names[205];

Ptr Sys,Dos,P96,Exp,Timer,Int;
static const char want[]="ZZMiniGL XACP A8.29 ";   /* the private LVO -36 exists from A8.2 on: an older build in memory is "different" */
#if defined(__mc68000__) || defined(__m68k__)
static Z arm_lib(Ptr lib,Z flags){
 register Ptr a6 __asm__("a6")=lib;register Z d0 __asm__("d0");register Z d1 __asm__("d1")=flags;
 __asm__ volatile("jsr -36(%%a6)":"=r"(d0),"+r"(d1):"r"(a6):"a0","a1","cc","memory");
 return d0;
}
#else
static Z arm_lib(Ptr lib,Z flags){(void)lib;(void)flags;return 1;}
#endif
static int ours(Ptr base){
 const char *s,*w=want;
 if(!base)return 0;
 s=*(const char **)((zu8 *)base+24);if(!s)return 0;
 while(*w)if(*s++!=*w++)return 0;
 return 1;
}
/* ---- A8.4 black box of the previous run ---- */
static Z bfh;static char lb[200];static Z ll;
static void lput(const char *t){while(*t&&ll<196u)lb[ll++]=*t++;}
static void lnum(Z v,int hex){char t[12];static const char h[]="0123456789abcdef";unsigned i=11;Z b=hex?16u:10u;t[i]=0;do{t[--i]=h[v%b];v/=b;}while(v);lput(t+i);}
static void lkv(const char *k,Z v,int hex){lput(k);lput("=");lnum(v,hex);}
static void lend(void){lb[ll++]='\n';lb[ll]=0;if(bfh)dos_write(bfh,lb,ll);zz_log(lb);ll=0;}
static const char *slot_name(Z w){Z s=w&255u;return s<205u?zz_slot_names[s]:"-";}
static const char *phase_text(Z p){
 switch(p){
  case 0:return "outside MGLSwitchDisplay, no packet in flight";
  case 1:return "MGLSwitchDisplay entered";
  case 10:return "heartbeat file being written";
  case 20:case 21:return "waiting for the ACK of the previous frame (Core1)";
  case 22:case 23:return "previous frame acknowledged, P96 unlock";
  case 30:return "60 Hz pacing";
  case 40:return "P96 lock of the screen bitmap";
  case 41:return "P96 lock returned";
  case 50:return "frame published to Core1";
  case 60:return "slow presentation path";
  case 70:return "packet being written to the board";
  case 71:return "packet written, waiting for Core1";
  case 72:return "packet accepted by Core1";
  case 80:return "LATESHOW: P96 lock of the screen bitmap";
  case 81:return "LATESHOW: show sent, waiting for Core1's copy";
  case 82:case 83:return "LATESHOW: picture shown, P96 unlock";
  case 84:return "LATESHOW: lock refused, slow presentation path";
  case 85:return "LATESHOW: frame closed without lock";
 }
 return "unknown";
}
static Z w32(volatile unsigned short *bb,unsigned i){return (Z)bb[i]<<16|bb[i+1];}
static void where(volatile unsigned short *bb,Z a){
 /* A8.5: an address as segment + offset (GLQuake's first three segments, then minigl.library's first) */
 unsigned k;
 for(k=0;k<3u;k++){Z st=w32(bb,48+4*k),sz=w32(bb,50+4*k);if(st&&a>=st&&a<st+sz){lput(" (application segment ");lnum(k,0);lput(" + 0x");lnum(a-st,1);lput(")");return;}}
 {Z st=w32(bb,60),sz=w32(bb,62);if(st&&a>=st&&a<st+sz){lput(" (minigl.library + 0x");lnum(a-st,1);lput(")");return;}}
}
static const char *trap_text(Z n){
 switch(n){case 2:return "bus / access error";case 3:return "address error";case 4:return "illegal instruction";
  case 5:return "division by zero";case 6:return "CHK";case 7:return "TRAPV";case 8:return "privilege violation";
  case 9:return "trace";case 10:return "line A";case 11:return "line F";}
 if(n>=32u&&n<48u)return "TRAP #n";
 return "other";
}
static Z ctl(volatile zu8 *mb,Z i){volatile unsigned short *q=(volatile unsigned short *)(mb+4*i);Z a=q[0],b=q[1];return ((a>>8)|(a<<8&0xff00u))|((b>>8)|(b<<8&0xff00u))<<16;}
static void blackbox(void){
 Ptr cd;volatile zu8 *board,*mb;volatile unsigned short *bb;
 Z i,r,p,q,ret,lap,inside;
 Exp=os_openlib("expansion.library",37);if(!Exp){zz_log("blackbox: no expansion.library\n");return;}
 cd=os_board(0,0x6d6e,4);if(!cd)cd=os_board(0,0x6d6e,10);
 if(!cd){zz_log("blackbox: no ZZ9000\n");os_closelib(Exp);return;}
 board=*(volatile zu8 **)((zu8 *)cd+32);
 if(!board||*(Z *)((zu8 *)cd+36)<0x06610000u){zz_log("blackbox: ZZ9000 window too small\n");os_closelib(Exp);return;}
 mb=board+0x10000+ZZ_CTRL-0x00200000u;bb=(volatile unsigned short *)(void *)(mb-ZZ_BB_OFFSET);
 os_cache((Ptr)bb,ZZ_BB_WORDS*2u,1u<<11);os_cache((Ptr)mb,256u,1u<<11);
 if(bb[0]!=ZZ_BB_MAGIC0||bb[1]!=ZZ_BB_MAGIC1||(bb[2]&0x7FFFu)!=ZZ_BB_LAYOUT){zz_log("blackbox_found=0\n");os_closelib(Exp);return;}
 bfh=dos_open("PROGDIR:zzminigl_blackbox.txt",1006u);
 lput("ZZMiniGL A8.29 black box of the previous armed run");lend();
 lkv("blackbox_found",1,0);lend();
 lkv("bb_clean_end",(bb[2]&ZZ_BB_CLOSED)?1u:0u,0);lend();
 lkv("bb_frames_68k",(Z)bb[4]<<16|bb[5],0);lput(" ");lkv("bb_t_ms",(Z)bb[6]<<16|bb[7],0);lend();
 lkv("bb_core1_frames",(Z)bb[10]<<16|bb[11],0);lput(" ");lkv("bb_gl_errors",(Z)bb[12]<<16|bb[13],0);lput(" ");
 lkv("bb_tex_refused",(Z)bb[14]<<16|bb[15],0);lput(" ");lkv("bb_lib_error",(Z)bb[16]<<16|bb[17],1);lend();
 lkv("bb_phase",bb[3],0);lput(" (");lput(phase_text(bb[3]));lput(")");lend();
 if(w32(bb,18)){lkv("bb_cap_unknown_first",w32(bb,18),1);lend();}
 {unsigned k;for(k=0;k<3u;k++)if(w32(bb,48+4*k)){lput("bb_app_segment");lnum(k,0);lput(" start=0x");lnum(w32(bb,48+4*k),1);lput(" size=");lnum(w32(bb,50+4*k),0);lend();}
  if(w32(bb,60)){lput("bb_lib_segment0 start=0x");lnum(w32(bb,60),1);lput(" size=");lnum(w32(bb,62),0);lend();}}
 if(bb[20]==ZZ_BB_TRAP){
   Z fmt=bb[25]>>12,k;
   lkv("bb_trap",bb[21],0);lput(" (");lput(trap_text(bb[21]));lput(")");lend();
   lput("bb_trap_pc=0x");lnum(w32(bb,23),1);where(bb,w32(bb,23));lend();
   lkv("bb_trap_sr",bb[22],1);lput(" ");lkv("bb_trap_format",fmt,0);lend();
   if(fmt==4u){lput("bb_trap_address=0x");lnum(w32(bb,26),1);where(bb,w32(bb,26));lput(" ");lkv("fslw",w32(bb,28),1);lend();}
   lput("bb_trap_usp=0x");lnum(w32(bb,30),1);lend();
   for(k=0;k<8u;k++){lput("  stack+");lnum(4*k,0);lput("=0x");lnum(w32(bb,32+2*k),1);where(bb,w32(bb,32+2*k));lend();}
 }else{lput("bb_trap=none (no CPU exception of the application task was seen)");lend();}
 ret=bb[8];
 if(ret==0xFFFFu){lput("bb_no_call_returned");lend();}
 else{
   r=ret>>8;q=(r+1u)&255u;lap=(Z)bb[ZZ_BB_RING+r]>>8;
   inside=bb[ZZ_BB_RING+q]!=0xFFFFu && ((Z)bb[ZZ_BB_RING+q]>>8)==(q?lap:((lap+1u)&255u));
   p=inside?q:r;
   if(!inside && (bb[ZZ_BB_RING+r]&255u)!=(ret&255u))inside=1;   /* defensive: the entry at r is not the call that returned */
   lput("bb_last_entered=");lput(slot_name(bb[ZZ_BB_RING+p]));lend();
   lput("bb_last_returned=");lput(slot_name(ret));lend();
   if(inside){lput("bb_verdict=inside_library ");lput(slot_name(bb[ZZ_BB_RING+p]));}
   else{lput("bb_verdict=outside_library (in the application or the system, after ");lput(slot_name(ret));lput(")");}
   lend();
   lput("bb_last_calls (oldest first):");lend();
   for(i=0;i<64u;i++){
     Z k=(p+193u+i)&255u,w=bb[ZZ_BB_RING+k];      /* p-63 .. p */
     if(w==0xFFFFu)continue;
     lput("  ");lput(slot_name(w));if(k==p)lput(inside?"   <- entered, not returned":"   <- last");lend();
   }
 }

 if(bb[ZZ_BB_AUD]==ZZ_BB_AUD_ON){
   /* A8.19: the application's audio interrupt handler under the guard (PAULAGUARD) */
   Z in=bb[ZZ_BB_AUD+1],out=bb[ZZ_BB_AUD+2],at=w32(bb,ZZ_BB_AUD+3),fr=w32(bb,4);
   lkv("bb_audguard_installs",bb[ZZ_BB_AUD+5],0);lput(" ");lkv("bb_aud_interrupts_entered",in,0);lput(" ");lkv("bb_aud_interrupts_returned",out,0);lend();
   if(in!=out){lput("bb_aud_verdict=inside_audio_interrupt (the handler was entered and did not return)");lend();}
   else if(in&&fr>=at){lkv("bb_aud_last_at_frame",at,0);lput(" ");lkv("bb_frames_since_last_audio_interrupt",fr-at,0);lend();}
   else{lput("bb_aud_no_interrupt_seen");lend();}
 }else{lput("bb_audguard=off");lend();}
 lput("core1_now");lput(" ");lkv("status",ctl(mb,MH_STATUS),0);lput(" ");lkv("frames",ctl(mb,MH_FRAMES),0);lput(" ");lkv("ack",ctl(mb,MH_ACK),0);
 lput(" ");lkv("phase",ctl(mb,MH_PHASE),0);lput(" ");lkv("error",ctl(mb,MH_ERROR),1);lend();
 lput("core1_now");lput(" ");lkv("fault_pc",ctl(mb,MH_FAULT_PC),1);lput(" ");lkv("fault_far",ctl(mb,MH_FAULT_FAR),1);lput(" ");lkv("fault_type",ctl(mb,MH_FAULT_TYPE),0);
 lend();
 /* A8.6: MH_TIMER is the Core1 timer frequency, not a counter: A8.4/A8.5 printed timer_moved_in_200ms=0 for
    a meaningless comparison; removed.  What the next start does with this state: */
 if(ctl(mb,MH_FAULT_TYPE)&0x80000000u){   /* A8.8: the packet record Core1 refused */
   Z ft=ctl(mb,MH_FAULT_TYPE);
   lput("core1_refused_record op=");lnum(ft&0xFFFFu,0);lput(" pass=");lnum((ft>>16)&15u,0);lput(" bytes=");lnum((ft>>20)&0x7FFu,0);
   lput(" offset=");lnum(ctl(mb,MH_FAULT_PC),0);lput(" first_word=0x");lnum(ctl(mb,MH_FAULT_FAR),1);lend();
 }
 {Z st=ctl(mb,MH_STATUS),er=ctl(mb,MH_ERROR),ft=ctl(mb,MH_FAULT_TYPE);
  if(st==ZZ_FATAL&&(er==ZE_GPU||er==ZE_TIMEOUT||er>=0xe1f0u||(ft&&ft<0x100u)))lput("core1_next_start=refused (GPU failure, timeout or Core1 exception): power the Amiga off and on");
 else lput("core1_next_start=relaunch (the firmware resets CPU1)");}
 lend();
 if(bfh){dos_close(bfh);bfh=0;zz_log("black box written to zzminigl_blackbox.txt\n");}
 bb[0]=0;bb[1]=0;os_cache((Ptr)bb,4u,1u<<11);          /* read once */
 os_closelib(Exp);
}
static int is_blackbox_only(const char *a,Z n){
 static const char k[]="BLACKBOX";Z i=0,j;
 while(i<n&&(a[i]==' '||a[i]=='\t'))i++;
 for(j=0;k[j];j++,i++){if(i>=n)return 0;if((a[i]&0xDF)!=k[j])return 0;}
 return 1;
}
/* A8.27: NOBLACKBOX and NOTRAP are the defaults, RECORD turns them off; WAITSHOW names the default (cli below).
   A8.7: diagnostic switches, any order: NOTRAP (no exception capture), NOLMBLEND (ARGB4444 lightmaps
   under GLQuake's own blend, as A8.4), NOBLACKBOX (no black box, no exception capture) */
static int has_word(const char *a,Z n,const char *w){
 Z i,j;
 for(i=0;i<n;i++){
   for(j=0;w[j]&&i+j<n&&(a[i+j]&0xDF)==w[j];j++);
   if(!w[j])return 1;
 }
 return 0;
}
int cli(const char *args,Z n){
 Ptr old,lib;int rc=20,record=0,trap=0,late=0;Z flags=0;
 Sys=os_sys();Dos=os_openlib("dos.library",37);
 if(!Dos)return 20;
 zz_log("ZZMiniGLLoad A8.29\n");
 blackbox();
 if(args&&is_blackbox_only(args,n)){os_closelib(Dos);return 0;}
 os_forbid();old=os_find((zu8 *)Sys+378,"minigl.library");
 if(old){
   int same=ours(old);
   os_permit();
   if(!same){zz_log("A different minigl.library is in memory: run Avail FLUSH (or reboot) first\n");goto done;}
   zz_log("minigl.library A8.29 already in memory\n");
 }else os_permit();
 lib=os_openlib("PROGDIR:minigl.library",29);
 if(!lib){zz_log("Cannot open PROGDIR:minigl.library version 29\n");goto done;}
 if(!ours(lib)){
   os_closelib(lib);
   zz_log("PROGDIR:minigl.library is not the A8.29 build: run Avail FLUSH, copy the A8.29 files together\n");
   goto done;
 }
 if(!get_table(lib)){os_closelib(lib);zz_log("minigl.library returned no dispatch table\n");goto done;}
 if(args){if(has_word(args,n,"NOTRAP"))flags|=ZZ_ARM_NOTRAP;if(has_word(args,n,"NOLMBLEND"))flags|=ZZ_ARM_NOLMBLEND;if(has_word(args,n,"NOBLACKBOX"))flags|=ZZ_ARM_NOBLACKBOX|ZZ_ARM_NOTRAP;if(has_word(args,n,"HOLDLOCK"))flags|=ZZ_ARM_HOLDLOCK;if(has_word(args,n,"ONEPACKET"))flags|=ZZ_ARM_ONEPACKET;if(has_word(args,n,"NODRAW"))flags|=ZZ_ARM_NODRAW;if(has_word(args,n,"NOLMPASS"))flags|=ZZ_ARM_NOLMPASS;if(has_word(args,n,"LMNEAREST"))flags|=ZZ_ARM_LMNEAREST;if(has_word(args,n,"TEXNEAREST"))flags|=ZZ_ARM_TEXNEAREST;if(has_word(args,n,"PROFILE"))flags|=ZZ_ARM_PROFILE;if(has_word(args,n,"NORAW"))flags|=ZZ_ARM_NORAW;if(has_word(args,n,"NOVCACHE"))flags|=ZZ_ARM_NOVCACHE;if(has_word(args,n,"BIGCHUNK"))flags|=ZZ_ARM_CHUNK32;if(has_word(args,n,"PAULAGUARD"))flags|=ZZ_ARM_PAULAGUARD;if(has_word(args,n,"NORGL"))flags|=ZZ_ARM_NORGL;if(has_word(args,n,"LATESHOW"))flags|=ZZ_ARM_LATESHOW;if(has_word(args,n,"AUTOEXIT"))flags|=ZZ_ARM_AUTOEXIT;if(has_word(args,n,"NOPOINTS"))flags|=ZZ_ARM_NOPOINTS;
   /* A8.27: by default no black box and no exception capture (1.3 ms per frame on GLQuake R200): RECORD
      brings both back (PAULAGUARD needs them); NOBLACKBOX and NOTRAP are still accepted.  The picture is
      shown inside MGLSwitchDisplay unless LATESHOW is given: with LATESHOW the last frame an application
      draws before it stops calling the library is not shown until its next call, which is right for a
      game that draws without pause and wrong as a general rule.  WAITSHOW names the default (and wins
      over LATESHOW, as HOLDLOCK does). */
   if(has_word(args,n,"RECORD")||has_word(args,n,"PAULAGUARD")){record=1;if(!has_word(args,n,"NOTRAP"))trap=1;}
   if(has_word(args,n,"NOBLACKBOX"))record=trap=0;
   if(has_word(args,n,"LATESHOW"))late=1;
   if(has_word(args,n,"WAITSHOW")||has_word(args,n,"HOLDLOCK"))late=0;
 }
 flags&=~(ZZ_ARM_NOBLACKBOX|ZZ_ARM_NOTRAP|ZZ_ARM_LATESHOW);
 if(!record)flags|=ZZ_ARM_NOBLACKBOX;
 if(!trap)flags|=ZZ_ARM_NOTRAP;
 if(late)flags|=ZZ_ARM_LATESHOW;
 {lput("switches in force:");if(flags&ZZ_ARM_NOTRAP)lput(" NOTRAP");if(flags&ZZ_ARM_NOLMBLEND)lput(" NOLMBLEND");if(flags&ZZ_ARM_NOBLACKBOX)lput(" NOBLACKBOX");if(flags&ZZ_ARM_HOLDLOCK)lput(" HOLDLOCK");if(flags&ZZ_ARM_ONEPACKET)lput(" ONEPACKET");if(flags&ZZ_ARM_NODRAW)lput(" NODRAW");if(flags&ZZ_ARM_NOLMPASS)lput(" NOLMPASS");if(flags&ZZ_ARM_LMNEAREST)lput(" LMNEAREST");if(flags&ZZ_ARM_TEXNEAREST)lput(" TEXNEAREST");if(flags&ZZ_ARM_PROFILE)lput(" PROFILE");if(flags&ZZ_ARM_NORAW)lput(" NORAW");if(flags&ZZ_ARM_NOVCACHE)lput(" NOVCACHE");if(flags&ZZ_ARM_CHUNK32)lput(" BIGCHUNK");if(flags&ZZ_ARM_PAULAGUARD)lput(" PAULAGUARD");if(flags&ZZ_ARM_NORGL)lput(" NORGL");if(flags&ZZ_ARM_LATESHOW)lput(" LATESHOW");if(flags&ZZ_ARM_NOPOINTS)lput(" NOPOINTS");if(flags&ZZ_ARM_AUTOEXIT)lput(" AUTOEXIT");if(record)lput(" RECORD");if(!late)lput(" WAITSHOW");lend();}
 if(arm_lib(lib,flags)!=1){os_closelib(lib);zz_log("minigl.library refused the authorisation call\n");goto done;}
 os_closelib(lib);
 zz_log("minigl.library A8.29 in memory and armed for one context: start the application now\n");
 rc=0;
done:
 os_closelib(Dos);return rc;
}
