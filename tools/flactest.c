/* Host test for XZN's FLAC encoder: gcc -O2 -Isdk/examples tools/flactest.c -o flactest &&
 * ./flactest out.flac (writes music-like test signals; decode with ffmpeg and compare) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "xznflac.h"
int main(int ac, char **av) {
    long n = 44100 * 20 + 1234; /* not a multiple of the block size */
    int16_t *p = malloc(n * 4);
    unsigned s = 1;
    for (long i = 0; i < n; i++) {
        double t = i / 44100.0;
        double a = 9000 * sin(2 * M_PI * 220 * t) + 5000 * sin(2 * M_PI * 331 * t + t);
        s = s * 1103515245 + 12345;
        double noise = ((int)(s >> 16 & 2047) - 1024) * (i > 300000 ? 8 : 0.5);
        int l = (int)(a + noise), r = (int)(a * 0.7 - noise);
        if (i > 500000 && i < 520000) /* silence */
            l = r = 0;
        if (i > 600000 && i < 610000) { /* loud and clipping */
            l = (i & 1) ? 32767 : -32768;
            r = -l - 1;
        }
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        p[i * 2] = (int16_t)l;
        p[i * 2 + 1] = (int16_t)r;
    }
    FILE *f = fopen(av[1], "wb");
    static uint8_t b[XF_FRAME];
    const char *tags[] = {"TITLE=Gr\x81\xe1" "e \x84 Test", "ARTIST=Band", "ALBUM=", "TRACKNUMBER=3"};
    long h = xf_head(b, (uint64_t)n, tags, 4), tot = h;
    fwrite(b, 1, h, f);
    uint32_t num = 0;
    for (long o = 0; o < n; o += XF_BLOCK) {
        int k = n - o < XF_BLOCK ? (int)(n - o) : XF_BLOCK;
        long l = xf_frame(b, p + o * 2, k, num++);
        fwrite(b, 1, l, f);
        tot += l;
    }
    fclose(f);
    FILE *g = fopen("ref.raw", "wb");
    fwrite(p, 4, n, g);
    fclose(g);
    free(p);
    printf("%ld Bytes, %.1f %% von PCM\n", tot, tot * 100.0 / (n * 4));
    return 0;
}
