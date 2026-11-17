/* Host test for ksnprintf: gcc -g -fsanitize=address,undefined -Ikernel -DHOST tools/strtest.c
 * kernel/str.c -o strtest && ./strtest. Every buffer size for every format, compared with the C
 * library: a number cut off at the end of the buffer once made ksnprintf loop forever. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ksnprintf(char *, size_t, const char *, ...);
int main(void) {
    char a[64], b[64];
    int bad = 0;
    for (size_t n = 0; n < 40; n++) {
#define T(F, ...)                                                                                  \
    do {                                                                                           \
        memset(a, 'x', sizeof a);                                                                  \
        memset(b, 'x', sizeof b);                                                                  \
        int ra = ksnprintf(a, n, F, __VA_ARGS__), rb = snprintf(b, n, F, __VA_ARGS__);             \
        if (ra != rb || (n && strcmp(a, b)) || (n && a[n] != 'x')) {       \
            printf("n=%zu %s: '%s' %d / '%s' %d\n", n, F, a, ra, b, rb);                           \
            bad++;                                                                                 \
        }                                                                                          \
    } while (0)
        T("%s%d/%d/%d", " ", 254, 1, 1);
        T("%d", -2147483647);
        T("%05d|%x|%lx", 42, 0xBEEFu, 0x123456789abcUL);
        T("%s%s", "", "Schnittstelle");
        T("%c|%%|%5d|%x", 'z', 7, 0u);
    }
    printf(bad ? "%d Fehler\n" : "ok\n", bad);
    return bad != 0;
}
