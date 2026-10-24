/* Cache/MMU handling follows the validated Core1 memory contract. */



















#include "quakedef.h"
#include "zzquake_config.h"

extern volatile unsigned int *shared;

/* Cache/MMU handling follows the validated Core1 memory contract. */

#define ZZQ_DMA_FRAMES  4096
static short zzq_dma_buf[ZZQ_DMA_FRAMES * 2];   /* stereo entrelace */

static unsigned int zzq_copied_frames;   
static unsigned int zzq_ring_wpos;

qboolean SNDDMA_Init(void)
{
    if (!shared[SH_PCM_ENABLE]) return false;

    shm = &sn;
    memset((void *)shm, 0, sizeof(*shm));
    shm->splitbuffer = 0;
    shm->channels    = 2;
    shm->samplebits  = 16;
    shm->speed       = (int)ZZQ_PCM_RATE;
    shm->samples     = ZZQ_DMA_FRAMES * 2;      
    shm->submission_chunk = 1;
    shm->buffer      = (unsigned char *)zzq_dma_buf;
    shm->samplepos   = 0;

    zzq_copied_frames = 0;
    zzq_ring_wpos     = 0;
    return true;
}

/* Audio uses the shared PCM transport. */


int SNDDMA_GetDMAPos(void)
{
    unsigned int rd = shared[SH_PCM_READ_POS];
    unsigned int consumed_frames = rd / 4u;      
    shm->samplepos = (consumed_frames * 2u) % (unsigned int)shm->samples;
    return shm->samplepos;
}



void SNDDMA_Submit(void)
{
    volatile unsigned char *ring = (volatile unsigned char *)ZZQ_PCM_ARM;
    unsigned int painted = (unsigned int)paintedtime;
    unsigned int rd, used, room_frames, n, i;

    if (!shared[SH_PCM_ENABLE]) return;
    if (painted <= zzq_copied_frames) return;

    
    rd = shared[SH_PCM_READ_POS];
    used = (zzq_ring_wpos >= rd) ? (zzq_ring_wpos - rd)
                                 : (ZZQ_PCM_RING_SIZE - rd + zzq_ring_wpos);
    if (used >= ZZQ_PCM_TARGET_FILL) return;
    room_frames = (ZZQ_PCM_TARGET_FILL - used) / 4u;

    n = painted - zzq_copied_frames;
    if (n > room_frames) n = room_frames;

    for (i = 0; i < n; i++) {
        unsigned int f = (zzq_copied_frames + i) & (ZZQ_DMA_FRAMES - 1u);
        short l = zzq_dma_buf[f * 2];
        short r = zzq_dma_buf[f * 2 + 1];
        ring[zzq_ring_wpos + 0] = (unsigned char)(l & 0xFF);
        ring[zzq_ring_wpos + 1] = (unsigned char)((l >> 8) & 0xFF);
        ring[zzq_ring_wpos + 2] = (unsigned char)(r & 0xFF);
        ring[zzq_ring_wpos + 3] = (unsigned char)((r >> 8) & 0xFF);
        zzq_ring_wpos += 4;
        if (zzq_ring_wpos >= ZZQ_PCM_RING_SIZE) zzq_ring_wpos = 0;
    }
    zzq_copied_frames += n;

    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_PCM_WRITE_POS] = zzq_ring_wpos;
}

void SNDDMA_Shutdown(void)
{
    shared[SH_PCM_ENABLE] = 0;
}
