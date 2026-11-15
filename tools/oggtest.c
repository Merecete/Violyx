/* Host test for XZN's OGG rips, with the same encoder objects as on Violyx:
 *   (cd sdk && make apps) && gcc -O2 -static -Isdk/examples -Ithird_party/ogg/include
 *   -Ithird_party/vorbis/include -I<dir with ogg/config_types.h> tools/oggtest.c
 *   sdk/build/lib-xzn/[a-z]*.o -lm -o oggtest && ./oggtest t.ogg 60. A made-up piece of music,
 *   encoded six times; ffmpeg then checks the file. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
static size_t total; /* memory the encoder took from the system */
void *vx_alloc(size_t n) {
    total += n;
    return calloc(1, n);
}
#include "xznflac.h"
#include "xznogg.h"
int main(int argc, char **argv) {
    int secs = argc > 2 ? atoi(argv[2]) : 60;
    long n = 44100L * secs;
    int16_t *pcm = malloc((size_t)n * 4);
    unsigned r = 1;
    for (long i = 0; i < n; i++) { /* chords, a beat and some noise */
        double t = i / 44100.0, b = fmod(t, 0.5);
        double v = 0.2 * sin(2 * M_PI * 220 * t) + 0.15 * sin(2 * M_PI * 277.2 * t) +
                   0.12 * sin(2 * M_PI * 329.6 * t * (1 + 0.001 * sin(t))) +
                   0.3 * exp(-b * 30) * sin(2 * M_PI * 60 * b);
        r = r * 1103515245 + 12345;
        double nz = ((int)(r >> 16 & 0x7fff) - 16384) / 16384.0 * 0.03;
        pcm[2 * i] = (int16_t)((v + nz) * 32767 * 0.8);
        pcm[2 * i + 1] = (int16_t)((v * 0.9 - nz) * 32767 * 0.8);
    }
    static xo_enc e;
    size_t last = 0;
    uint8_t *out = malloc((size_t)n * 4 + (1 << 20));
    const char *tags[] = {"TITLE=Gr\x81n \x84rger", "ARTIST=Testband", "ALBUM=Probe",
                          "TRACKNUMBER=3", "ALBUMARTIST="};
    for (int round = 0; round < 6; round++) { /* open and close again: memory is reused */
        clock_t c0 = clock();
        long k = xo_begin(&e, 1234 + round, tags, 5, out);
        if (k < 0)
            abort();
        for (long o = 0; o < n; o += 16384) {
            int m = n - o < 16384 ? (int)(n - o) : 16384;
            k += xo_feed(&e, pcm + 2 * o, m, out + k);
        }
        k += xo_feed(&e, 0, 0, out + k);
        xo_end(&e);
        double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
        printf("Runde %d: %ld Bytes, %.0f kbit/s, %.2f s fuer %d s Musik (%.0fx Echtzeit), "
               "Speicher %zu KB\n",
               round, k, k * 8.0 / secs / 1000, s, secs, secs / s, total >> 10);
        if (round > 1 && total != last)
            abort(); /* the second track must not need more memory */
        last = total;
        if (round == 0) {
            FILE *f = fopen(argv[1], "wb");
            fwrite(out, 1, (size_t)k, f);
            fclose(f);
            f = fopen(argc > 3 ? argv[3] : "oggtest.raw", "wb");
            fwrite(pcm, 4, (size_t)n, f);
            fclose(f);
        }
    }
    return 0;
}
