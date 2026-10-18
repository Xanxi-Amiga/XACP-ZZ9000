/* XX19a1 / XACP v1.7: SMUSH Codec1/37/47 prototypes removed together
 * with their implementation (codec37.c, codec47.c, compression.c out
 * of the build). ACC_CMPTYPE_* values in gfx.h are intentionally NOT
 * renumbered: old requests are accepted and ignored (unsupported). */

void init_imc_tables();
uint32_t decompress_adpcm(uint8_t *compInput, uint8_t *compOutput, int channels);
