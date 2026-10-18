/* Native verification harness: includes the actual production command parser,
 * profile initialization, volume setup, and render_chunk. Only ARM MMU/mailbox
 * entry and unrelated devices are unavailable on this host. Not shipped as ARM. */
#define _GNU_SOURCE
#define ZZVGM_S18_HOST 1
#include "../../core1/zzvgm_core1.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <errno.h>
static void fail(const char *s){fprintf(stderr,"production test: %s at VGM 0x%x, error=%u\n",s,g_pos,g_error);exit(3);}
int main(int argc,char **argv){
 FILE *f;long length;uint64_t hash=UINT64_C(14695981039346656037);uint32_t frames=0,peak=0;
 if(argc!=2){fprintf(stderr,"usage: production_render track.vgm\n");return 2;}
 if(mmap((void*)ZZVGM_ARENA_BASE,ZZVGM_ARENA_SIZE,PROT_READ|PROT_WRITE,
   MAP_FIXED_NOREPLACE|MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED){perror("mmap arena");return 2;}
 if(mmap((void*)ZZVGM_SHARED_ARM_ADDR,0x100000,PROT_READ|PROT_WRITE,
   MAP_FIXED_NOREPLACE|MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED){perror("mmap shared");return 2;}
 f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
 fseek(f,0,SEEK_END);length=ftell(f);rewind(f);
 if(length<128||length>0x01000000){fclose(f);return 2;}
 if(fread((void*)g_arena,1,(size_t)length,f)!=(size_t)length){fclose(f);return 2;}fclose(f);
 g_upload_dst=g_arena;g_upload_len=g_uploaded=(uint32_t)length;g_file_flags=0;
 sh_write(VSH_SAMPLE_RATE,44100);sh_write(VSH_PCM_READ,0);
 if(!vgm_init())fail("init");
 if(!g_system18)fail("fixture profile not System 18");
 while(!g_done&&!g_error){uint32_t wait=parse_until_wait();
   if(wait==UINT32_MAX)break;
   while(wait){uint32_t n=wait>256u?256u:wait;unsigned i;
     render_chunk(n,0);
     for(i=0;i<n;i++){
       uint32_t pair=ARM32(g_ring[i]);int16_t l=(int16_t)(pair>>16),r=(int16_t)pair;
       int16_t v[2]={l,r};unsigned j;
       for(j=0;j<2;j++){
         uint16_t u=(uint16_t)v[j];uint32_t a=v[j]<0?-(int32_t)v[j]:v[j];
         if(a>peak)peak=a;
         hash^=(u&255u);hash*=UINT64_C(1099511628211);
         hash^=(u>>8);hash*=UINT64_C(1099511628211);
       }
     }
     frames+=n;wait-=n;
   }
 }
 if(g_error)fail("parse");
 if(frames!=le32(g_vgm+0x18))fail("duration mismatch");
 printf("{\"frames\":%u,\"fnv64\":\"%016" PRIx64 "\",\"peak\":%u,\"pool_used\":%zu,\"chip_mask\":%u,"
        "\"original_fnv64\":\"%016" PRIx64 "\",\"pre_gain_clipped_channels\":%" PRIu64 ","
        "\"clipped_channels\":%" PRIu64 ",\"preclip_peak\":%u}\n",
    frames,hash,peak,zz_s18_pool_used(),g_chip_mask,g_s18_host_original_hash,
    g_s18_host_pre_gain_clipped_channels,g_s18_host_clipped_channels,g_s18_host_preclip_peak);
 return 0;
}
