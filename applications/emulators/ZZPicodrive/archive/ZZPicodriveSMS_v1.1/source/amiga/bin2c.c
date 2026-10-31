/*
 * bin2c.c - embarque un blob binaire dans un tableau C pour le launcher 68k.
 * Produit g_blob_size + g_blob_data[N], format compatible ZZPicoDrive.
 *
 * Build (hote, MSYS2/MINGW ou Linux) :  gcc -O2 -o bin2c bin2c.c
 * Usage :  bin2c  entree.bin  sortie.c
 *   ex.:   bin2c  zzpicodrive_sms_core1.bin  blob_data.c
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *fi, *fo;
    unsigned char *buf;
    long n, i;

    if (argc != 3) {
        fprintf(stderr, "usage: %s input.bin output.c\n", argv[0]);
        return 2;
    }
    fi = fopen(argv[1], "rb");
    if (!fi) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(fi, 0, SEEK_END);
    n = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    if (n <= 0) { fprintf(stderr, "empty/invalid input\n"); fclose(fi); return 1; }
    buf = (unsigned char*)malloc((size_t)n);
    if (!buf) { fprintf(stderr, "oom\n"); fclose(fi); return 1; }
    if (fread(buf, 1, (size_t)n, fi) != (size_t)n) {
        fprintf(stderr, "read fail\n"); free(buf); fclose(fi); return 1;
    }
    fclose(fi);

    fo = fopen(argv[2], "wb");   /* wb : forcer des fins de ligne LF stables */
    if (!fo) { fprintf(stderr, "cannot write %s\n", argv[2]); free(buf); return 1; }

    fprintf(fo,
        "/* blob_data.c - blob ARM Core1 (%s) embarque dans l'executable.\n"
        " * Genere automatiquement par bin2c. Ajouter a la compilation :\n"
        " *   m68k-amigaos-gcc ... <launcher>.c blob_data.c -lamiga\n"
        " */\n", argv[1]);
    fprintf(fo, "const unsigned long g_blob_size = %ldUL;\n", n);
    fprintf(fo, "const unsigned char g_blob_data[%ld] = {\n", n);
    for (i = 0; i < n; i++) {
        fprintf(fo, "0x%02x,", buf[i]);
        if ((i % 24) == 23) fputc('\n', fo);
    }
    if ((n % 24) != 0) fputc('\n', fo);
    fprintf(fo, "};\n");

    fclose(fo);
    free(buf);
    fprintf(stderr, "bin2c: wrote %s (%ld bytes -> g_blob_data)\n", argv[2], n);
    return 0;
}
