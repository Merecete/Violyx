/* Host test for audio CDs: gcc -g -fsanitize=address,undefined -DHOST -Ibuild -Ikernel
 * tools/cddatest.c kernel/cdda.c -o cddatest && ./cddatest. A made-up disc with CD-Text, then
 * random tables of contents and texts that must never crash. */
#include "cdda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rd(void *ctx, uint32_t lba, uint32_t n, void *b) {
    (void)ctx;
    unsigned char *o = b;
    for (uint32_t s = 0; s < n; s++)
        for (int i = 0; i < 2352; i++)
            o[s * 2352 + i] = (unsigned char)((lba + s) * 7 + i);
    return 0;
}
static int lcb(const vfs_ent *e, void *x) {
    (void)x;
    printf("  %-40s %10lu\n", e->name, (unsigned long)e->size);
    return 0;
}
static int quiet(const vfs_ent *e, void *x) {
    (void)e;
    (void)x;
    return 0;
}
static void pack(unsigned char *p, int type, int trk, const char *t12) {
    memset(p, 0, 18);
    p[0] = (unsigned char)type;
    p[1] = (unsigned char)trk;
    memcpy(p + 4, t12, 12);
}
int main(void) {
    static cdda a;
    /* 3 audio tracks and a data track in a second session (CD Extra) */
    unsigned char toc[4 + 5 * 8] = {0, 2 + 5 * 8, 1, 4};
    unsigned st[5] = {0, 15000, 30000, 60000, 90000};
    for (int i = 0; i < 5; i++) {
        unsigned char *d = toc + 4 + i * 8;
        d[1] = i == 3 ? 0x14 : 0x10;
        d[2] = i == 4 ? 0xAA : (unsigned char)(i + 1);
        d[4] = (unsigned char)(st[i] >> 24);
        d[5] = (unsigned char)(st[i] >> 16);
        d[6] = (unsigned char)(st[i] >> 8);
        d[7] = (unsigned char)st[i];
    }
    if (cda_toc(&a, toc, sizeof toc))
        abort();
    a.rd = rd;
    printf("TOC: %s", a.toc);
    if (a.n != 3 || a.len[2] != 30000 - 11400)
        abort();
    cda_list(&a, "/", lcb, 0);
    unsigned char text[4 + 18 * 5];
    memset(text, 0, sizeof text);
    text[1] = 2 + 18 * 5;
    pack(text + 4, 0x80, 0,
         "Album \xe4"
         "ber\0S");                                /* album, start of track 1 */
    pack(text + 22, 0x80, 1, "ong One\0Two:");     /* rest of 1, start of 2 */
    pack(text + 40, 0x80, 2, " A/B\0Three?\0");    /* rest of 2, track 3 */
    pack(text + 58, 0x81, 0, "Band\0Singer\0");    /* album artist, track 1 */
    pack(text + 76, 0x81, 2, "\t\0Other\0\0\0\0"); /* track 2 as before, track 3 */
    cda_text(&a, text, sizeof text);
    cda_list(&a, "/", lcb, 0);
    if (strcmp(a.name[0], "01 Song One.wav") || strcmp(a.name[1], "02 Two- A-B.wav") ||
        strcmp(a.name[2], "03 Three.wav") || strcmp(a.artist[2], "Singer") ||
        strcmp(a.artist[3], "Other") ||
        strcmp(a.title[0], "Album \x84"
                           "ber")) {
        printf("%s|%s|%s|%s|%s\n", a.name[0], a.name[1], a.name[2], a.artist[2], a.title[0]);
        abort();
    }
    /* parts at random places must match one big read */
    vfs_ent e;
    if (cda_stat(&a, "/02 two- a-b.wav", &e))
        abort();
    unsigned char *all = malloc(e.size), *part = malloc(1 << 20);
    if (cda_read_at(&a, "02 Two- A-B.wav", all, 0, e.size) != (int64_t)e.size)
        abort();
    if (memcmp(all, "RIFF", 4) || memcmp(all + 36, "LIST", 4))
        abort();
    for (int t = 0; t < 2000; t++) {
        uint64_t o = (uint64_t)rand() % (e.size + 10), m = (uint64_t)rand() % (1 << 20);
        int64_t k = cda_read_at(&a, "02 Two- A-B.wav", part, o, m);
        uint64_t want = o >= e.size ? 0 : (e.size - o < m ? e.size - o : m);
        if (k != (int64_t)want || (k && memcmp(part, all + o, (size_t)k)))
            abort();
    }
    printf("Kopf %d Bytes, Datei %lu Bytes, Teile ok\n", cda_header(&a, 1, part),
           (unsigned long)e.size);
    free(all);
    free(part);
    /* random input */
    for (int t = 0; t < 200000; t++) {
        unsigned char b[1200];
        int l = rand() % (int)sizeof b;
        for (int i = 0; i < l; i++)
            b[i] = (unsigned char)(rand() % 4 ? rand() : rand() % 3);
        if (t % 2 && l > 4) {
            b[0] = 0;
            b[1] = (unsigned char)(l - 2);
            b[2] = 1;
            b[3] = (unsigned char)(1 + rand() % 99);
        }
        if (!cda_toc(&a, b, l)) {
            a.rd = rd;
            cda_text(&a, b, l);
            cda_list(&a, "/", quiet, 0);
        }
    }
    printf("ok\n");
    return 0;
}
