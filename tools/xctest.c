/* Host test of the XZN decoders in sdk/examples/xzncodec.h:
 *   xctest FILE OUT.raw [seek-frame]   writes stereo 16-bit PCM */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define XC_HOST 1
#include "../sdk/examples/xzncodec.h"
int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n + 16);
    if (fread(b, 1, n, f) != (size_t)n)
        return 2;
    fclose(f);
    xdec d;
    memset(&d, 0, sizeof d);
    d.f = b;
    d.n = n;
    int r = xd_open(&d, argv[1]);
    if (r == 2) { /* remuxed: write the stream for ffmpeg to check */
        FILE *o = fopen(argv[2], "wb");
        fwrite(d.f, 1, d.n, o);
        fclose(o);
        printf("remuxed %ld bytes\n", d.n);
        return 0;
    }
    if (!r) {
        printf("cannot open\n");
        return 1;
    }
    printf("%s: %d Hz, %d ch, %d bit, %ld frames, %d kbit/s\n", d.name, d.srate, d.chans, d.bits,
           d.total, d.kbps);
    if (argc > 3)
        d.seek(&d, atol(argv[3]));
    FILE *o = fopen(argv[2], "wb");
    short buf[4096 * 2];
    long tot = 0;
    for (;;) {
        int k = d.read(&d, buf, 4096);
        if (k <= 0)
            break;
        fwrite(buf, 4, k, o);
        tot += k;
    }
    fclose(o);
    printf("decoded %ld frames\n", tot);
    return 0;
}
