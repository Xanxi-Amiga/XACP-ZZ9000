#ifndef ZZ_QSOUND_HLE_H
#define ZZ_QSOUND_HLE_H
/* Capcom DL-1425 QSound HLE, based on the BSD-3-Clause
 * QSound emulator by superctr (Ian Karlsson), with thanks to Valley Bell,
 * 2018. Modified for ZZVGMPlay XACP Cortex-A9 bare metal 2026.
 */
#include <stdint.h>
struct qsound_voice {
    uint16_t bank;
    int16_t addr;
    uint16_t phase,rate;
    int16_t loop_len,end_addr,volume,echo;
};
struct qsound_adpcm {
    uint16_t start_addr,end_addr,bank;
    int16_t volume;
    uint16_t flag;
    int16_t cur_vol,step_size;
    uint16_t cur_addr;
};
struct qsound_fir {
    int tap_count,delay_pos;
    int16_t table_pos,taps[95],delay_line[95];
};
struct qsound_delay {
    int16_t delay,volume,write_pos,read_pos,delay_line[51];
};
struct qsound_echo {
    uint16_t end_pos;
    int16_t feedback,length,last_sample,delay_line[1024],delay_pos;
};
struct qsound_chip {
    uint32_t rom_len; /* modified: real allocation size, not ROM mask */
    const uint8_t *rom_data;
    uint16_t data_latch;
    int16_t out[2];
    int16_t pan_tables[2][2][98];
    struct qsound_voice voice[16];
    struct qsound_adpcm adpcm[3];
    uint16_t voice_pan[19];
    int16_t voice_output[19];
    struct qsound_echo echo;
    struct qsound_fir filter[2],alt_filter[2];
    struct qsound_delay wet[2],dry[2];
    uint16_t state,next_state,delay_update;
    int state_counter,ready_flag;
    uint16_t *register_map[256];
};
uint32_t qsound_hle_start(struct qsound_chip *chip,uint32_t clock);
void qsound_hle_reset(struct qsound_chip *chip);
void qsound_hle_write(struct qsound_chip *chip,uint8_t addr,uint16_t val);
void qsound_hle_step(struct qsound_chip *chip);
#endif
