/* ZZVGMPlay A03c - full Capcom QSound HLE backend.
 * Uses superctr/ValleyBell BSD-3-Clause HLE DSP, supplied in
 * third_party/QSound-HLE. Original upstream is not called by MAME at runtime.
 * Retains A03 ROM, VGM write and 44.1kHz stereo interface unchanged.
 */
#include "qsound_pcm.h"
#include "arcade_pcm.h"
#include "qsound.h"
#include <stdint.h>
#include <stddef.h>
#define QS_ROM_MAX (96u*1024u*1024u)
#define QS_RATE_NUM 60000000ull
#define QS_RATE_DEN 2496ull
static struct qsound_chip chip;
static uint32_t rom_capacity;
static uint64_t phase_out,step_out;
static int16_t prev_l,prev_r,next_l,next_r;
static int16_t s16(int64_t v){return(v>32767)?32767:(v< -32768?-32768:(int16_t)v);}
void zz_qsound_init(uint32_t clock_hz,uint32_t output_hz){
    unsigned i;
    /* VGM clocks 4 MHz and 60 MHz both describe the 60 MHz DL-1425.
     * The DSP program output is always 60000000/(2*1248) Hz. */
    (void)clock_hz;
    if(!output_hz)output_hz=44100;
    step_out=(QS_RATE_NUM<<24)/(QS_RATE_DEN*(uint64_t)output_hz);
    if(step_out>(1ull<<24))step_out=(1ull<<24);
    phase_out=0;
    prev_l=prev_r=next_l=next_r=0;
    rom_capacity=0;
    qsound_hle_start(&chip,60000000u);
    qsound_hle_reset(&chip);
    /* Finish DSP reset *before* applying the first VGM register writes.
     * Without pre-roll, upstream state_init() clears early register writes. */
    for(i=0;i<8;i++)qsound_hle_step(&chip);
}
int zz_qsound_rom(uint32_t size,uint32_t off,const uint8_t *src,uint32_t len){
    uint32_t i;
    if(!src||!size||size>QS_ROM_MAX||off>size||len>size-off)return 0;
    if(!chip.rom_data){
        uint8_t *p=zz_arcade_alloc_rom(size,0x80u);
        if(!p)return 0;
        chip.rom_data=p;
        chip.rom_len=size;
        rom_capacity=size;
    }else if(rom_capacity!=size)return 0;
    for(i=0;i<len;i++)((uint8_t*)chip.rom_data)[off+i]=src[i];
    return 1;
}
void zz_qsound_write(uint8_t reg,uint16_t value){
    qsound_hle_write(&chip,reg,value);
}
void zz_qsound_render(int16_t *out,uint32_t frames){
    uint32_t i;
    if(!out)return;
    for(i=0;i<frames;i++){
        int64_t l,r;
        phase_out+=step_out;
        while(phase_out>=(1ull<<24)){
            phase_out-=(1ull<<24);
            prev_l=next_l;prev_r=next_r;
            qsound_hle_step(&chip);
            next_l=chip.out[0];next_r=chip.out[1];
        }
        /* A03-compatible interpolation. HLE is ~4x louder than A03 PCM.
         * Divide by 4 here to offset the already-validated x4 QSound
         * post-mixer gain in zzvgm_core1.c. That file remains untouched. */
        l=(int64_t)prev_l*((1ull<<24)-phase_out)+(int64_t)next_l*phase_out;
        r=(int64_t)prev_r*((1ull<<24)-phase_out)+(int64_t)next_r*phase_out;
        out[i*2]=s16((l>>24)/4);
        out[i*2+1]=s16((r>>24)/4);
    }
}
