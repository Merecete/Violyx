/* Host test for the formatter: gcc -DHOST -Ibuild -Ikernel tools/fmttest.c kernel/format.c -o
 * fmttest. fmttest fat32|exfat IMAGE LABEL formats the image file */
#include "format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *f;
static long ns;
static int r_(uint64_t s, void *b) {
    fseek(f, (long)s * 512, 0);
    return fread(b, 1, 512, f) == 512 ? 0 : -1;
}
static int w_(uint64_t s, const void *b) {
    if ((long)s >= ns)
        return -1;
    fseek(f, (long)s * 512, 0);
    return fwrite(b, 1, 512, f) == 512 ? 0 : -1;
}
static int wn_(uint64_t s, uint32_t n, const void *b) {
    for (uint32_t i = 0; i < n; i++)
        if (w_(s + i, (const char *)b + i * 512))
            return -1;
    return 0;
}
static int f_(void) {
    return 0;
}
static void prog(uint64_t a, uint64_t b) {
    (void)a;
    (void)b;
}
int main(int ac, char **av) {
    if (ac < 4)
        return 2;
    f = fopen(av[2], "r+b");
    fseek(f, 0, 2);
    ns = ftell(f) / 512;
    vfs_dev d = {r_, w_, f_, 0, (uint64_t)ns};
    if (getenv("WRN")) /* with the multi-sector function */
        d.wrn = wn_;
    int r = !strcmp(av[1], "exfat") ? fmt_exfat(&d, av[3], 0x12345678, prog)
                                    : fmt_fat32(&d, av[3], 0x12345678, prog);
    fclose(f);
    printf("format %s: %d\n", av[1], r);
    return r ? 1 : 0;
}
