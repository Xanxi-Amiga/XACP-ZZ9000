/* XACP v1.7: legacy SMUSH codec prototypes were removed.
 * ACC_CMPTYPE values remain reserved for ABI compatibility. */
void init_imc_tables();
uint32_t decompress_adpcm(uint8_t *compInput, uint8_t *compOutput, int channels);
