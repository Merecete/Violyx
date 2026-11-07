/* Host test for package checks: gcc -g -fsanitize=address,undefined -DHOST tools/progtest.c -o
 * progtest VALID.vpk */
#include "../kernel/prog.c"
#include <stdio.h>
#include <stdlib.h>
int main(int c, char **a) {
    FILE *f = fopen(a[1], "rb");
    fseek(f, 0, 2);
    long n = ftell(f);
    rewind(f);
    u8 *o = malloc(n), *b = malloc(n + 4096);
    fread(o, 1, n, f);
    u64 e;
    seg_t s[8];
    int ns;
    long acc = 0, rej = 0;
    if (!vpk_check(o, n) || !elf_check(o + VPK_HDR, n - VPK_HDR, &e, s, &ns)) {
        puts("Original abgelehnt");
        return 1;
    }
    for (long it = 0; it < 2000000; it++) {
        long l = n;
        memcpy(b, o, n);
        int k = 1 + rand() % 12;
        for (int i = 0; i < k; i++) {
            long p = rand() % 3 ? rand() % (VPK_HDR + 64 + 56 * 4) : rand() % n;
            b[p] = rand() % 4 ? rand() : (rand() % 2 ? 0xFF : 0);
        }
        if (rand() % 8 == 0)
            l = rand() % n;
        u8 *x = malloc(l ? l : 1);
        memcpy(x, b, l);
        int v = vpk_check(x, l);
        int g = l > VPK_HDR && elf_check(x + VPK_HDR, l - VPK_HDR, &e, s, &ns);
        if (g) {
            acc++;
            for (int i = 0; i < ns; i++) {
                if (s[i].vaddr < ULOAD || s[i].vaddr + s[i].memsz > UHEAP ||
                    (s[i].flags & 3) == 3 || s[i].filesz > s[i].memsz ||
                    s[i].off + s[i].filesz > (u64)l - VPK_HDR)
                    abort();
            }
            if (e < ULOAD || e >= UHEAP)
                abort();
        } else
            rej++;
        (void)v;
        free(x);
    }
    printf("%ld angenommen, %ld abgelehnt\n", acc, rej);
    return 0;
}
