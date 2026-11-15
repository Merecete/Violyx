/* The small C library behind helpers/vxogg.h: memory with a real free, a sort and a few strings.
   SPDX-License-Identifier: BSD-3-Clause */
#include "violyx.h"
#include "vxogg.h"
#include <string.h>
/* Blocks of 32 bytes to 1 MB in powers of two, each class with its own free list. Larger
   blocks get their own memory and go to a list of big blocks when freed. A 16-byte head in
   front of each block keeps its size. */
#define NCLS 16
typedef struct blk {
    size_t size; /* usable bytes */
    struct blk *next;
} blk;
static blk *fl[NCLS], *big;
static char *sp, *se; /* the slab being carved */
static void *carve(size_t n) {
    if ((size_t)(se - sp) < n) {
        size_t g = n > (1 << 20) ? n : (1 << 20);
        char *p = vx_alloc(g);
        if (!p)
            return 0;
        sp = p;
        se = p + g;
    }
    void *r = sp;
    sp += n;
    return r;
}
void *vxo_malloc(size_t n) {
    if (!n)
        n = 1;
    int c = 0;
    while (c < NCLS && ((size_t)32 << c) < n)
        c++;
    blk *b;
    if (c < NCLS) {
        if ((b = fl[c]))
            fl[c] = b->next;
        else if ((b = carve(16 + ((size_t)32 << c))))
            b->size = (size_t)32 << c;
        else
            return 0;
    } else {
        blk **q = &big;
        while (*q && (*q)->size < n)
            q = &(*q)->next;
        if ((b = *q))
            *q = b->next;
        else {
            n = (n + 4095) & ~(size_t)4095;
            if (!(b = carve(16 + n)))
                return 0;
            b->size = n;
        }
    }
    return (char *)b + 16;
}
void vxo_free(void *p) {
    if (!p)
        return;
    blk *b = (blk *)((char *)p - 16);
    if (b->size > ((size_t)32 << (NCLS - 1))) {
        b->next = big;
        big = b;
        return;
    }
    int c = 0;
    while (((size_t)32 << c) < b->size)
        c++;
    b->next = fl[c];
    fl[c] = b;
}
void *vxo_calloc(size_t n, size_t s) {
    if (s && n > (size_t)-1 / s)
        return 0;
    void *p = vxo_malloc(n * s);
    if (p)
        memset(p, 0, n * s);
    return p;
}
void *vxo_realloc(void *p, size_t n) {
    if (!p)
        return vxo_malloc(n);
    size_t o = ((blk *)((char *)p - 16))->size;
    if (n <= o)
        return p;
    void *q = vxo_malloc(n);
    if (q) {
        memcpy(q, p, o);
        vxo_free(p);
    }
    return q;
}
/* heap sort: no extra memory, no deep recursion */
static void swp(char *a, char *b, size_t s) {
    while (s--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}
static void sift(char *a, size_t i, size_t n, size_t s, int (*c)(const void *, const void *)) {
    for (;;) {
        size_t l = 2 * i + 1, m = i;
        if (l < n && c(a + l * s, a + m * s) > 0)
            m = l;
        if (l + 1 < n && c(a + (l + 1) * s, a + m * s) > 0)
            m = l + 1;
        if (m == i)
            return;
        swp(a + i * s, a + m * s, s);
        i = m;
    }
}
void vxo_qsort(void *b, size_t n, size_t s, int (*c)(const void *, const void *)) {
    char *a = b;
    if (n < 2)
        return;
    for (size_t i = n / 2; i-- > 0;)
        sift(a, i, n, s, c);
    for (size_t e = n - 1; e > 0; e--) {
        swp(a, a + e * s, s);
        sift(a, 0, e, s, c);
    }
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}
char *strcat(char *d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}
void *memchr(const void *p, int c, size_t n) {
    const unsigned char *s = p;
    for (size_t i = 0; i < n; i++)
        if (s[i] == (unsigned char)c)
            return (void *)(s + i);
    return 0;
}
