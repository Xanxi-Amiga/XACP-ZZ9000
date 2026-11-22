#include "wire.h"
#include "rixgpu.h"
extern void mmu_init(void),mmu_prepare_return(void);
int zrender_init(volatile zu32 *);
int zrender_visit(zu32,const zu8 *,void *);
volatile zu32 exc_info[8];
static volatile zu32 *const m=(volatile zu32 *)ZZ_CTRL;
static int quit;
/* A3: the packet is written by the 68k into non-cacheable DDR (one bus transaction per access).
   Packets up to ZZ_STAGE_BYTES are first copied into this cached buffer with 32-byte bursts and
   decoded from there (validation pass + execution pass); larger ones (texture images, resident
   array loads) are decoded in place with 32-bit accesses. */
#define ZZ_STAGE_BYTES 65536u
/* A8.22: a queue of staged packets.  The 68k publishes a packet only once the one before has left ZZ_DATA
   (MH_STAGED): until A8.21 that happened when Core1 had finished drawing the packet before that one, so
   the 68k was never more than one packet ahead and waited whenever Core1 was in a long one.  Core1 now
   looks at the door between two records of the packet it is drawing and copies a published packet into
   this queue at once, and does the same while it waits for the rasteriser (rix_idle_hook); the packets are
   still drawn and acknowledged one by one, in order.  Nothing changes on the 68k side of the exchange.  A packet larger than ZZ_STAGE_BYTES is not queued: it is decoded in
   place once the queue is empty, as before. */
#define ZZ_SQ_BYTES (192u*1024u)
#define ZZ_SQ_MAX 64u
static zu32 sq_buf[ZZ_SQ_BYTES/4u] __attribute__((aligned(32)));
static struct {zu32 seq,bytes,off;} sq[ZZ_SQ_MAX];
static zu32 sq_head,sq_n,sq_wr,sq_rd;   /* oldest entry, entries, next free byte, first byte in use */
static zu32 sq_door,sq_last;            /* door value and sequence of the last packet taken */
static int sq_bad;                      /* a published packet was out of sequence: reported by the service loop */
static void stage_copy(zu32 *d,const volatile zu32 *s,zu32 bytes){
 zu32 n=(bytes+31u)/32u;
#if defined(__arm__) && !defined(RIX_HOST_SIM)
 __asm__ volatile(
  "1:\n\t"
  "ldmia %[s]!, {r3,r4,r5,r6,r8,r9,r10,r12}\n\t"
  "stmia %[d]!, {r3,r4,r5,r6,r8,r9,r10,r12}\n\t"
  "subs %[n],%[n],#1\n\t" "bne 1b\n\t"
  : [s]"+r"(s),[d]"+r"(d),[n]"+r"(n) : : "r3","r4","r5","r6","r8","r9","r10","r12","cc","memory");
#else
 zu32 i;for(i=0;i<n*8u;i++)d[i]=s[i];
#endif
}
/* a place of n bytes (a multiple of 32) in the queue's buffer, kept contiguous: after the last packet,
   or at the start when the end is too short and the start is free; ~0 when there is none */
static zu32 sq_place(zu32 n){
 if(!sq_n){sq_wr=sq_rd=0;return n<=ZZ_SQ_BYTES?0u:~0u;}
 if(sq_wr>=sq_rd){
   if(n<=ZZ_SQ_BYTES-sq_wr)return sq_wr;
   return n<sq_rd?0u:~0u;
 }
 return n<sq_rd-sq_wr?sq_wr:~0u;
}
/* the door: a packet published and not taken yet goes into the queue if it fits */
static void stage_poll(void){
 zu32 d,seq,bytes,need,off,t;
 if(sq_bad)return;
 d=m[MH_RESERVED];if(d==sq_door)return;
 dmb();seq=m[MH_PUBLISH];bytes=m[MH_BYTES];
 if(d>1u||seq!=znext(sq_last)){sq_bad=1;return;}
 if(bytes>ZZ_STAGE_BYTES||sq_n>=ZZ_SQ_MAX)return;   /* in place, or later */
 need=(bytes+31u)&~31u;off=sq_place(need);
 if(off==~0u)return;
 t=rix_now();
 stage_copy(sq_buf+off/4u,(const volatile zu32 *)ZZ_DATA,bytes);
 {zu32 i=(sq_head+sq_n)%ZZ_SQ_MAX;sq[i].seq=seq;sq[i].bytes=bytes;sq[i].off=off;}
 sq_n++;sq_wr=off+need;sq_door=d;sq_last=seq;
 /* ZZ_DATA is free from here on: the 68k may upload the next packet */
 dmb();m[MH_STAGED]=seq;dsb();
 m[MH_DECODE_US]+=rix_ticks_to_us(rix_now()-t);
}
static int consume(zu32 op,const zu8 *p,void *ctx){
 stage_poll();   /* A8.22 */
 if(op==Z_STOP)quit=1;
 return zrender_visit(op,p,ctx);
}
static void fatal(zu32 err){m[MH_ERROR]=err;dmb();m[MH_STATUS]=ZZ_FATAL;dsb();}
void zzmini_exception(void){
 m[MH_FAULT_PC]=exc_info[0];m[MH_FAULT_FAR]=exc_info[1];m[MH_FAULT_TYPE]=exc_info[4];
 fatal(0xe1f0u+exc_info[4]);mmu_prepare_return();
}
void zzmini_service(void){
 zu32 door=0,seq,t,w0,elapsed,wait;int rc;
 sq_head=sq_n=sq_wr=sq_rd=sq_door=sq_last=0;sq_bad=0;quit=0;
 /* A4R: mmu_init() is called by start.S before the stack switch */
 dmb();
 *(volatile zu32 *)ZZ_TRACE=0x5a410005u;dsb();
 if(m[MH_MAGIC]!=ZZ_MAGIC||m[MH_VERSION]!=ZZ_PROTO||m[MH_COOKIE]!=ZZ_COOKIE){fatal(ZE_HEADER);goto out;}
 m[MH_PHASE]=1;m[MH_STATUS]=ZZ_STARTING;dsb();
 rc=zrender_init(m);if(rc){fatal(rc);goto out;}
 *(volatile zu32 *)ZZ_TRACE=0x5a410006u;dsb();
 m[MH_STATUS]=ZZ_READY;dsb();
 rix_idle_hook=stage_poll;
 while(!quit){
   const zu8 *src;zu32 bytes;int queued;
   stage_poll();
   if(sq_bad){fatal(ZE_SEQ);break;}
   queued=sq_n!=0u;
   if(queued){
     seq=sq[sq_head].seq;bytes=sq[sq_head].bytes;src=(const zu8 *)sq_buf+sq[sq_head].off;
   }else{
     if(m[MH_RESERVED]==sq_door){cpu_yield();continue;}
     /* published and not queued: too large to stage, decoded in place (the queue is empty: the order holds) */
     dmb();door=m[MH_RESERVED];seq=m[MH_PUBLISH];bytes=m[MH_BYTES];
     if(door>1u||seq!=znext(sq_last)){fatal(ZE_SEQ);break;}
     if(bytes<=ZZ_STAGE_BYTES)continue;   /* published between the two looks: the next one queues it */
     sq_door=door;sq_last=seq;src=(const zu8 *)ZZ_DATA;
   }
   t=rix_now();w0=m[MH_FENCE_US];
   rc=zpacket(src,bytes,seq,consume,0);
   if(queued){
     /* drawn: its place is free; the next entry, if any, is the oldest */
     sq_head=(sq_head+1u)%ZZ_SQ_MAX;sq_n--;
     if(sq_n)sq_rd=sq[sq_head].off;else sq_wr=sq_rd=0;
   }else{dmb();m[MH_STAGED]=seq;dsb();}   /* decoded in place: ZZ_DATA is free only now */
   elapsed=rix_ticks_to_us(rix_now()-t);wait=m[MH_FENCE_US]-w0;
   m[MH_CPU_US]+=elapsed>wait?elapsed-wait:0;
   if(rc){
     /* A8.8: which record: FAULT_TYPE = 0x80000000 | length << 20 | pass << 16 | op (pass 0 = validation,
        1 = execution), FAULT_FAR = its first argument word, FAULT_PC = its offset in the packet; no CPU
        exception is involved (an exception sets FAULT_TYPE below 0x100) */
     m[MH_FAULT_PC]=zz_wire_fail_at;m[MH_FAULT_FAR]=zz_wire_fail_word;m[MH_FAULT_TYPE]=0x80000000u|zz_wire_fail_op;
     fatal(rc);break;
   }
   m[MH_RUNS]++;dmb();m[MH_ACK]=seq;dsb();
 }
 rix_idle_hook=0;
 if(quit && m[MH_STATUS]!=ZZ_FATAL){m[MH_STATUS]=ZZ_STOPPED;dsb();}
out:mmu_prepare_return();
}
