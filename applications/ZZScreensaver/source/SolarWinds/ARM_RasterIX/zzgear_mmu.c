/* Core1 MMU for ZZCube RasterIX B1 (from ZZGear A0 / A14D4).
 * Identity map.  D2: the arena sections the GPU reads (texture pool, probe
 * copies) are Inner write-back / OUTER NON-CACHEABLE: dcache_clean_range()
 * below is a CP15 L1 clean by MVA, which never reaches the shared L2C-310;
 * with an outer-cacheable mapping the texels would sit in the L2 until
 * Core0's vblank flush (D2 review).  Outer-NC keeps L1 caching for Core1 and
 * makes the L1 clean sufficient for HP3 readers.
 * Changes versus A14D4:
 *   - 0x06600000..0x067FFFFF (RasterIX G4B window: framebuffers, texture,
 *     display lists) is Normal Non-cacheable instead of Strongly-Ordered.
 *     SO forced every 16-bit framebuffer access into a serialized DDR
 *     round trip (the 26 ms "swap" of A08..A14D4); NC lets NEON/word
 *     accesses stream and keeps the region out of L1/L2, which is required
 *     because RasterIX (HP3), the PL330 and the VDMA access it directly.
 *   - everything else unchanged: blob WBWA, mailbox NC, PS/PL registers
 *     Device or Strongly-Ordered.
 */
#include <stdint.h>
#include <stddef.h>
static uint32_t __attribute__((aligned(0x4000))) page_table[4096];
#define SECT_TYPE (1U<<1)
#define SECT_B (1U<<2)
#define SECT_C (1U<<3)
#define SECT_DOMAIN(d) ((uint32_t)(d)<<5)
#define SECT_AP_RW (0x3U<<10)
#define SECT_TEX(t) ((uint32_t)(t)<<12)
#define SECT_S (1U<<16)
#define ATTR_SO 0U
#define ATTR_DEVICE SECT_B
#define ATTR_NC SECT_TEX(1U)
#define ATTR_WBWA (SECT_TEX(1U)|SECT_C|SECT_B)
#define ATTR_IWB_ONC (SECT_TEX(4U)|SECT_B)   /* inner write-back write-allocate, outer non-cacheable (TEX remap off) */
#define BASE_FLAGS (SECT_TYPE|SECT_DOMAIN(0)|SECT_AP_RW|SECT_S)
static inline uint32_t sect(uint32_t mb,uint32_t a){return (mb<<20)|BASE_FLAGS|a;}
static inline void dsb(void){__asm__ volatile("dsb":::"memory");}
static inline void isb(void){__asm__ volatile("isb":::"memory");}
static inline uint32_t rsctlr(void){uint32_t v;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(v));return v;}
static inline void wsctlr(uint32_t v){__asm__ volatile("mcr p15,0,%0,c1,c0,0"::"r"(v):"memory");}
static inline uint32_t ractlr(void){uint32_t v;__asm__ volatile("mrc p15,0,%0,c1,c0,1":"=r"(v));return v;}
static inline void wactlr(uint32_t v){__asm__ volatile("mcr p15,0,%0,c1,c0,1"::"r"(v):"memory");}
static inline void tlb_all(void){uint32_t z=0;__asm__ volatile("mcr p15,0,%0,c8,c7,0"::"r"(z):"memory");}
static inline void ic_all(void){uint32_t z=0;__asm__ volatile("mcr p15,0,%0,c7,c5,0"::"r"(z):"memory");}
static inline void bp_all(void){uint32_t z=0;__asm__ volatile("mcr p15,0,%0,c7,c5,6"::"r"(z):"memory");}
static void dc_clean_all(void){uint32_t w,s;for(w=0;w<4;w++)for(s=0;s<256;s++){uint32_t sw=(w<<30)|(s<<5);__asm__ volatile("mcr p15,0,%0,c7,c10,2"::"r"(sw):"memory");}dsb();}
static void dc_inval_all(void){uint32_t w,s;for(w=0;w<4;w++)for(s=0;s<256;s++){uint32_t sw=(w<<30)|(s<<5);__asm__ volatile("mcr p15,0,%0,c7,c6,2"::"r"(sw):"memory");}dsb();}
/* L1 clean by MVA only (DCCMVAC): sufficient for outer-non-cacheable or NC regions, NOT for outer write-back ones */
void dcache_clean_range(void *addr,uint32_t len){uint32_t p=(uint32_t)(uintptr_t)addr&~31U,e=((uint32_t)(uintptr_t)addr+len+31U)&~31U;while(p<e){__asm__ volatile("mcr p15,0,%0,c7,c10,1"::"r"(p):"memory");p+=32;}dsb();}
void mmu_set_wbwa(uint32_t addr,uint32_t len){uint32_t s=addr>>20,e=(addr+len-1)>>20,i;for(i=s;i<=e;i++)page_table[i]=sect(i,ATTR_WBWA);dcache_clean_range(page_table+s,(e-s+1)*4);dsb();tlb_all();isb();}
void mmu_init(void)
{
    uint32_t i,v=rsctlr();
    if(v&(1U<<2))dc_clean_all();
    v&=~((1U<<0)|(1U<<2));wsctlr(v);dsb();isb();dc_inval_all();ic_all();bp_all();tlb_all();
    v=ractlr();v|=(1U<<6);wactlr(v);dsb();isb();
    for(i=0;i<4096;i++)page_table[i]=sect(i,ATTR_SO);
    page_table[0x045]=sect(0x045,ATTR_WBWA); page_table[0x046]=sect(0x046,ATTR_WBWA);
    page_table[0x047]=sect(0x047,ATTR_NC);
    page_table[0x066]=sect(0x066,ATTR_NC); page_table[0x067]=sect(0x067,ATTR_NC);
    for(i=0x300;i<=0x30F;i++)page_table[i]=sect(i,ATTR_IWB_ONC);   /* arena: GPU-shared (textures, Z, ROOM pool) */
    for(i=0xF80;i<0x1000;i++)page_table[i]=sect(i,ATTR_DEVICE);
    dcache_clean_range(page_table,sizeof(page_table));
    __asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(0):"memory");
    __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(1):"memory");
    v=(uint32_t)(uintptr_t)page_table;__asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(v):"memory");
    tlb_all();ic_all();bp_all();dsb();isb();
    v=rsctlr();v|=(1U<<0)|(1U<<2)|(1U<<12)|(1U<<22);v&=~(1U<<1);wsctlr(v);dsb();isb();
}
void mmu_prepare_return(void){dc_clean_all();dsb();}
