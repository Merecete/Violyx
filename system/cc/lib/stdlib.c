/* C library of Violyx: memory, numbers, sorting, the start and the end of a program */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "sys.h"

int errno;

/* ---------- memory: a list of free blocks in address order, neighbours are joined ---------- */
typedef struct blk {
    size_t size; /* bytes after the header */
    struct blk *next;
} blk;
#define HDR   16
#define ALIGN 16
static blk *freel;
static void *grow(size_t n) {
    size_t want = n + HDR;
    if (want < 65536)
        want = 65536;
    want = (want + 4095) & ~(size_t)4095;
    blk *b = __os_sbrk((long)want);
    if (!b)
        return 0;
    b->size = want - HDR;
    free((char *)b + HDR);
    return b;
}
void *malloc(size_t n) {
    if (n > ((size_t)1 << 40))
        return 0;
    n = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (!n)
        n = ALIGN;
    for (int tries = 0; tries < 2; tries++) {
        blk **pp = &freel;
        for (blk *b = freel; b; pp = &b->next, b = b->next)
            if (b->size >= n) {
                if (b->size >= n + HDR + 32) { /* split */
                    blk *r = (blk *)((char *)b + HDR + n);
                    r->size = b->size - n - HDR;
                    r->next = b->next;
                    *pp = r;
                    b->size = n;
                } else
                    *pp = b->next;
                b->next = 0;
                return (char *)b + HDR;
            }
        if (!grow(n)) {
            errno = 12;
            return 0;
        }
    }
    return 0;
}
void free(void *p) {
    if (!p)
        return;
    blk *b = (blk *)((char *)p - HDR), *prev = 0, *cur = freel;
    while (cur && cur < b) {
        prev = cur;
        cur = cur->next;
    }
    b->next = cur;
    if (prev)
        prev->next = b;
    else
        freel = b;
    if (cur && (char *)b + HDR + b->size == (char *)cur) { /* join the next one */
        b->size += HDR + cur->size;
        b->next = cur->next;
    }
    if (prev && (char *)prev + HDR + prev->size == (char *)b) { /* and the one before */
        prev->size += HDR + b->size;
        prev->next = b->next;
    }
}
void *calloc(size_t n, size_t s) {
    if (s && n > ((size_t)1 << 40) / s)
        return 0;
    void *p = malloc(n * s);
    if (p)
        memset(p, 0, n * s);
    return p;
}
void *realloc(void *p, size_t n) {
    if (!p)
        return malloc(n);
    if (!n) {
        free(p);
        return 0;
    }
    blk *b = (blk *)((char *)p - HDR);
    if (b->size >= n)
        return p;
    void *q = malloc(n);
    if (!q)
        return 0;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

/* ---------- start and end ---------- */
static void (*atx[32])(void);
static int natx;
int atexit(void (*f)(void)) {
    if (natx >= 32)
        return -1;
    atx[natx++] = f;
    return 0;
}
void __stdio_flush(void);
void exit(int c) {
    while (natx)
        atx[--natx]();
    __stdio_flush();
    __os_fini(c);
    __os_exit(c);
    for (;;)
        ;
}
void _Exit(int c) {
    __os_exit(c);
    for (;;)
        ;
}
void abort(void) {
    __stdio_flush();
    __os_write(2, "abort\n", 6);
    __os_fini(134);
    __os_exit(134);
    for (;;)
        ;
}
int main(int, char **);
int __os_args(char ***argv);
void __libc_start(void) {
    __os_init();
    char **argv;
    int argc = __os_args(&argv);
    exit(main(argc, argv));
}

/* ---------- numbers ---------- */
int abs(int x) {
    return x < 0 ? -x : x;
}
long labs(long x) {
    return x < 0 ? -x : x;
}
long long llabs(long long x) {
    return x < 0 ? -x : x;
}
div_t div(int a, int b) {
    div_t r;
    r.quot = a / b;
    r.rem = a % b;
    return r;
}
ldiv_t ldiv(long a, long b) {
    ldiv_t r;
    r.quot = a / b;
    r.rem = a % b;
    return r;
}
static unsigned long long strtox(const char *s, char **end, int base, int *neg, int *ok) {
    const char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    *neg = 0;
    if (*p == '+' || *p == '-')
        *neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0)
        base = *p == '0' ? 8 : 10;
    unsigned long long v = 0;
    int any = 0, over = 0;
    for (;; p++) {
        int c = (unsigned char)*p, d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        if (v > (ULLONG_MAX - (unsigned)d) / (unsigned)base)
            over = 1;
        v = v * (unsigned)base + (unsigned)d;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    *ok = !over;
    return over ? ULLONG_MAX : v;
}
long strtol(const char *s, char **e, int b) {
    int n, ok;
    unsigned long long v = strtox(s, e, b, &n, &ok);
    if (!ok || v > (unsigned long long)LONG_MAX + n) {
        errno = ERANGE;
        return n ? LONG_MIN : LONG_MAX;
    }
    return n ? (long)(0 - v) : (long)v;
}
long long strtoll(const char *s, char **e, int b) {
    return strtol(s, e, b);
}
unsigned long strtoul(const char *s, char **e, int b) {
    int n, ok;
    unsigned long long v = strtox(s, e, b, &n, &ok);
    if (!ok)
        errno = ERANGE;
    return n ? 0 - v : v;
}
unsigned long long strtoull(const char *s, char **e, int b) {
    return strtoul(s, e, b);
}
int atoi(const char *s) {
    return (int)strtol(s, 0, 10);
}
long atol(const char *s) {
    return strtol(s, 0, 10);
}
long long atoll(const char *s) {
    return strtol(s, 0, 10);
}
double pow(double, double);
double strtod(const char *s, char **end) {
    const char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    int neg = 0;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if (!strncasecmp(p, "inf", 3)) {
        if (end)
            *end = (char *)p + (strncasecmp(p, "infinity", 8) ? 3 : 8);
        return neg ? -1.0 / 0.0 : 1.0 / 0.0;
    }
    if (!strncasecmp(p, "nan", 3)) {
        if (end)
            *end = (char *)p + 3;
        return 0.0 / 0.0;
    }
    double v = 0;
    int any = 0, ex = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { /* hexadecimal: 0x1.8p3 */
        p += 2;
        for (; isxdigit((unsigned char)*p); p++, any = 1)
            v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (*p | 32) - 'a' + 10);
        if (*p == '.')
            for (p++; isxdigit((unsigned char)*p); p++, any = 1) {
                v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (*p | 32) - 'a' + 10);
                ex -= 4;
            }
        if (any && (*p == 'p' || *p == 'P')) {
            char *q;
            long e2 = strtol(p + 1, &q, 10);
            if (q != p + 1) {
                ex += (int)e2;
                p = q;
            }
        }
        if (end)
            *end = (char *)(any ? p : s);
        v = ex ? v * pow(2, ex) : v;
        return neg ? -v : v;
    }
    /* decimal: the digits as an integer as long as it is exact, the rest as a power of ten */
    unsigned long long m = 0;
    int digits = 0;
    for (; isdigit((unsigned char)*p); p++, any = 1) {
        if (digits < 19) {
            m = m * 10 + (unsigned)(*p - '0');
            if (m)
                digits++;
        } else
            ex++;
    }
    if (*p == '.')
        for (p++; isdigit((unsigned char)*p); p++, any = 1)
            if (digits < 19) {
                m = m * 10 + (unsigned)(*p - '0');
                if (m)
                    digits++;
                ex--;
            }
    if (!any) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    if (*p == 'e' || *p == 'E') {
        char *q;
        long e10 = strtol(p + 1, &q, 10);
        if (q != p + 1) {
            ex += (int)(e10 > 9999 ? 9999 : e10 < -9999 ? -9999 : e10);
            p = q;
        }
    }
    if (end)
        *end = (char *)p;
    /* scale with long double for accuracy */
    long double r = (long double)m, t = 10;
    int e = ex < 0 ? -ex : ex;
    long double f = 1;
    while (e) {
        if (e & 1)
            f *= t;
        t *= t;
        e >>= 1;
    }
    r = ex < 0 ? r / f : r * f;
    double d = (double)r;
    if (d == 1.0 / 0.0 || (d == 0 && m))
        errno = ERANGE;
    return neg ? -d : d;
}
float strtof(const char *s, char **e) {
    return (float)strtod(s, e);
}
long double strtold(const char *s, char **e) {
    return strtod(s, e);
}
double atof(const char *s) {
    return strtod(s, 0);
}

/* ---------- sorting and searching ---------- */
static void swapb(char *a, char *b, size_t n) {
    while (n--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    char *b = base;
    while (n > 1) {
        if (n < 8) { /* insertion sort */
            for (size_t i = 1; i < n; i++)
                for (size_t j = i; j && cmp(b + (j - 1) * sz, b + j * sz) > 0; j--)
                    swapb(b + (j - 1) * sz, b + j * sz, sz);
            return;
        }
        swapb(b, b + (n / 2) * sz, sz); /* the middle one as pivot */
        size_t i = 1, j = n - 1;
        for (;;) {
            while (i <= j && cmp(b + i * sz, b) < 0)
                i++;
            while (j >= i && cmp(b + j * sz, b) > 0)
                j--;
            if (i >= j)
                break;
            swapb(b + i * sz, b + j * sz, sz);
            i++;
            j--;
        }
        swapb(b, b + j * sz, sz);
        /* the smaller part by recursion, the larger one in the loop */
        if (j < n - j - 1) {
            qsort(b, j, sz, cmp);
            b += (j + 1) * sz;
            n -= j + 1;
        } else {
            qsort(b + (j + 1) * sz, n - j - 1, sz, cmp);
            n = j;
        }
    }
}
void *bsearch(const void *key, const void *base, size_t n, size_t sz,
              int (*cmp)(const void *, const void *)) {
    const char *b = base;
    while (n) {
        const char *m = b + (n / 2) * sz;
        int c = cmp(key, m);
        if (!c)
            return (void *)m;
        if (c > 0) {
            b = m + sz;
            n -= n / 2 + 1;
        } else
            n /= 2;
    }
    return 0;
}
static unsigned long long rnd = 1;
int rand(void) {
    rnd = rnd * 6364136223846793005ull + 1442695040888963407ull;
    return (int)(rnd >> 33) & RAND_MAX;
}
void srand(unsigned s) {
    rnd = s;
}
char *getenv(const char *n) {
    (void)n;
    return 0;
}
int system(const char *c) {
    (void)c;
    return -1;
}
