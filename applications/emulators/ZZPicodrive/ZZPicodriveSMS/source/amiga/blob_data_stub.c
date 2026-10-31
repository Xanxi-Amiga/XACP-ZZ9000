/* blob_data_stub.c - replacement for blob_data.c when no blob is embedded
 * (development build): the launcher then requires the Core1 blob file on the
 * command line (it refuses to start when the embedded size is below 4096). */
const unsigned char g_blob_data[16] = {0};
const unsigned long g_blob_size = 0;
