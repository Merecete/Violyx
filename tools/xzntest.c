/* Host test of the decoders in XZN, see tools/xzntest.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
long xt_run(const char *, long *);
void *xm(size_t n) {
    return malloc(n ? n : 1);
}
void xf(void *p) {
    (void)p;
}
void *xr(void *p, size_t n) {
    return realloc(p, n ? n : 1);
}
void xq(void *b, size_t n, size_t s, int (*c)(const void *, const void *)) {
    qsort(b, n, s, c);
}
void *vx_alloc(size_t n) {
    return calloc(1, n);
}
long vx_sys(long n, long a, long b, long c) {
    if (n == 13) {
        struct stat s;
        if (stat((char *)a, &s))
            return -1;
        ((long *)b)[0] = s.st_size;
        ((long *)b)[1] = 0;
        return 0;
    }
    if (n == 11) {
        FILE *f = fopen((char *)a, "rb");
        if (!f)
            return -1;
        long r = fread((void *)b, 1, c, f);
        fclose(f);
        return r;
    }
    if (n == 22)
        return a ? b : 100000;
    return 0;
}
int vx_cols(void) {
    return 80;
}
int vx_rows(void) {
    return 25;
}
void vx_clear(unsigned char a) {
}
void vx_paint(int x, int y, char c, unsigned char f, unsigned char b) {
}
void vx_show(void) {
}
int vx_random(int n) {
    return 0;
}
unsigned char vx_rgb(int r, int g, int b) {
    return 16;
}
void vx_pixcell(int x, int y, int a, int b) {
}
int main(int ac, char **av) {
    for (int i = 1; i < ac; i++) {
        long s = 0, n = xt_run(av[i], &s);
        printf("%s %ld %lx\n", av[i], n, s);
    }
    return 0;
}
