/* blob_data_stub.c - replacement for blob_data.c when no blob is embedded.
 * ZZPicoDrive32X then requires the Core1 file on the command line
 * (ZZPicoDrive32X rom.32x bin/zz32x_v1.2.bin usa ahi). */
const unsigned char g_blob_data[16] = {0};
const unsigned long g_blob_size = 0;
