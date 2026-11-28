#ifndef ZZS_MSM6295_H
#define ZZS_MSM6295_H
/* zzs_msm6295.h - OKI MSM6295 4-voice ADPCM for ZZStrider. ASCII only. */
#ifdef __cplusplus
extern "C" {
#endif

void zzs_oki_init(const unsigned char *rom, unsigned int rom_size, unsigned int clock_hz,
                  int pin7_high, unsigned int out_rate);
void zzs_oki_reset(void);
void zzs_oki_set_pin7(int pin7_high);
void zzs_oki_write(unsigned char data);
unsigned char zzs_oki_read(void);
/* Render 'count' samples at the output rate. dst[i] receives the 4-voice sum in the chip's own
   scale: 12-bit signed sample x 5-bit volume (0..32), i.e. full scale one voice = +/-65536. */
void zzs_oki_render(int *dst, unsigned int count);

extern unsigned int g_oki_writes, g_oki_starts, g_oki_rejects;

#ifdef __cplusplus
}
#endif
#endif
