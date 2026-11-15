/* What libogg and libvorbis need from a C library, for Violyx programs without one.
   Built with -include helpers/vxogg.h (see the xzn target in the Makefile).
   Their memory comes from an own pool with a real free (helpers/vxogg.c), so an encoder
   can be opened and closed many times, separate from the program's own malloc.
   SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#pragma GCC visibility push(hidden) /* direct calls inside the library */
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#define malloc  vxo_malloc
#define calloc  vxo_calloc
#define realloc vxo_realloc
#define free    vxo_free
#define qsort   vxo_qsort
#define exit(c) __builtin_trap()
#define alloca  __builtin_alloca
#ifndef LONG_MAX
#define LONG_MAX 9223372036854775807L
#endif
void *vxo_malloc(size_t);
void *vxo_calloc(size_t, size_t);
void *vxo_realloc(void *, size_t);
void vxo_free(void *);
void vxo_qsort(void *, size_t, size_t, int (*)(const void *, const void *));
char *strcpy(char *, const char *);
char *strcat(char *, const char *);
void *memchr(const void *, int, size_t);
static inline long labs(long v) {
    return v < 0 ? -v : v;
}
static inline double rint(double x) { /* to nearest, ties to even, like the C library */
    if (x > 4e18 || x < -4e18)
        return x;
    long r;
    __asm__("cvtsd2si %1,%0" : "=r"(r) : "x"(x));
    return (double)r;
}
static inline double ceil(double x) {
    return -floor(-x);
}
static inline double atan(double x) {
    int neg = x < 0, inv = 0;
    if (neg)
        x = -x;
    if (x > 1) {
        x = 1 / x;
        inv = 1;
    }
    /* halve the angle twice, then the series converges quickly */
    x = x / (1 + sqrt(1 + x * x));
    x = x / (1 + sqrt(1 + x * x));
    double y = x * x, s = 0;
    for (int i = 23; i >= 1; i -= 2)
        s = s * y + ((i & 2) ? -1.0 : 1.0) / i;
    s = 4 * x * s;
    if (inv)
        s = M_PI / 2 - s;
    return neg ? -s : s;
}
static inline double acos(double x) {
    if (x <= -1)
        return M_PI;
    if (x >= 1)
        return 0;
    return 2 * atan(sqrt((1 - x) / (1 + x)));
}
static inline double frexp(double x, int *e) {
    union {
        double d;
        uint64_t u;
    } v = {x};
    int k = (int)((v.u >> 52) & 2047);
    if (!k || k == 2047) {
        *e = 0;
        return x;
    }
    *e = k - 1022;
    v.u = (v.u & ~(2047ull << 52)) | (1022ull << 52);
    return v.d;
}
