#include <stdio.h>
#include <stdlib.h>
#include "lzw/lzw.h"

int main(int argc, char *argv[]) {
    if (argc != 3) { fprintf(stderr, "usage: %s infile outfile\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long inlen = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *indata = malloc(inlen);
    fread(indata, 1, inlen, f);
    fclose(f);

    long outlen = lzwGetDecompressedSize(indata, inlen);
    printf("Decompressed size: %ld bytes\n", outlen);
    unsigned char *outdata = malloc(outlen);
    lzwDecompress(indata, outdata, inlen);

    FILE *out = fopen(argv[2], "wb");
    fwrite(outdata, 1, outlen, out);
    fclose(out);
    free(indata); free(outdata);
    return 0;
}
