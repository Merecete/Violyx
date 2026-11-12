/* C library of Violyx: formatted output and files. A file is read completely when it is opened
 * and written completely when it is closed, because Violyx stores files as a whole. */
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sys.h"

struct __file {
    int fd;          /* 0 1 2: standard streams, -1: a file */
    char *path;
    unsigned char *b; /* the contents */
    size_t n, cap, pos;
    int wr, rd, eof, err, dirty;
    int unget;
};
typedef struct {
    char **bp;
    size_t *sp;
} memref;
static FILE fin = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1};
static FILE fout = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1};
static FILE ferr = {2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1};
FILE *stdin = &fin, *stdout = &fout, *stderr = &ferr;

/* ---------- formatting ---------- */
typedef struct {
    void (*put)(void *, const char *, size_t);
    void *ctx;
    size_t n;
} out_t;
static void emit(out_t *o, const char *s, size_t n) {
    o->put(o->ctx, s, n);
    o->n += n;
}
static void pad(out_t *o, char c, int n) {
    char b[32];
    memset(b, c, sizeof b);
    while (n > 0) {
        int k = n > 32 ? 32 : n;
        emit(o, b, (size_t)k);
        n -= k;
    }
}
/* a double as digits: %f (mode 0), %e (1), %g (2) */
static int fmt_double(char *o, int cap, long double v, int prec, int mode, int alt, int upper) {
    int k = 0;
    if (v != v) {
        strcpy(o, upper ? "NAN" : "nan");
        return 3;
    }
    if (v == 1.0 / 0.0) {
        strcpy(o, upper ? "INF" : "inf");
        return 3;
    }
    if (prec < 0)
        prec = 6;
    if (prec > 40)
        prec = 40;
    int e10 = 0;
    long double m = v;
    if (m != 0) {
        while (m >= 10) {
            m /= 10;
            e10++;
        }
        while (m < 1) {
            m *= 10;
            e10--;
        }
    }
    if (mode == 2) {
        int P = prec ? prec : 1;
        if (e10 < -4 || e10 >= P) {
            mode = 1;
            prec = P - 1;
        } else {
            mode = 0;
            prec = P - 1 - e10;
        }
        if (!alt) /* trailing zeros go away later */
            mode |= 4;
    }
    int strip = mode & 4;
    mode &= 3;
    /* digits: integer and fraction for %f, mantissa for %e */
    long double x = mode == 1 ? m : v;
    /* rounding */
    long double r = 0.5;
    for (int i = 0; i < prec; i++)
        r /= 10;
    x += r;
    if (mode == 1 && x >= 10) {
        x /= 10;
        e10++;
    }
    /* integer part */
    char ib[400];
    int il = 0;
    long double ip = x;
    int ie = 0;
    while (ip >= 10) {
        ip /= 10;
        ie++;
    }
    for (int i = 0; i <= ie && il < 390; i++) {
        int d = (int)ip;
        if (d > 9)
            d = 9;
        ib[il++] = (char)('0' + d);
        ip = (ip - d) * 10;
    }
    long double frac = x;
    {
        /* the fraction after taking away the integer digits */
        long double t = 1;
        for (int i = 0; i < ie; i++)
            t *= 10;
        long double q = 0;
        for (int i = 0; i < il; i++) {
            q += (ib[i] - '0') * t;
            t /= 10;
        }
        frac = x - q;
        if (frac < 0)
            frac = 0;
    }
    for (int i = 0; i < il && k < cap - 1; i++)
        o[k++] = ib[i];
    if (prec || alt)
        o[k++] = '.';
    for (int i = 0; i < prec && k < cap - 8; i++) {
        frac *= 10;
        int d = (int)frac;
        if (d > 9)
            d = 9;
        if (d < 0)
            d = 0;
        o[k++] = (char)('0' + d);
        frac -= d;
    }
    if (strip && strchr(o, '.') && k) {
        o[k] = 0;
        while (k && o[k - 1] == '0')
            k--;
        if (k && o[k - 1] == '.')
            k--;
    }
    if (mode == 1) {
        o[k++] = upper ? 'E' : 'e';
        o[k++] = e10 < 0 ? '-' : '+';
        int a = e10 < 0 ? -e10 : e10;
        if (a >= 100)
            o[k++] = (char)('0' + a / 100);
        o[k++] = (char)('0' + a / 10 % 10);
        o[k++] = (char)('0' + a % 10);
    }
    o[k] = 0;
    return k;
}
static int vformat(out_t *o, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') {
            const char *s = f;
            while (f[1] && f[1] != '%')
                f++;
            emit(o, s, (size_t)(f - s + 1));
            continue;
        }
        f++;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1, len = 0;
        for (;; f++) {
            if (*f == '-')
                left = 1;
            else if (*f == '+')
                plus = 1;
            else if (*f == ' ')
                space = 1;
            else if (*f == '#')
                alt = 1;
            else if (*f == '0')
                zero = 1;
            else
                break;
        }
        if (*f == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            f++;
        } else
            while (*f >= '0' && *f <= '9')
                width = width * 10 + *f++ - '0';
        if (*f == '.') {
            f++;
            prec = 0;
            if (*f == '*') {
                prec = va_arg(ap, int);
                f++;
            } else
                while (*f >= '0' && *f <= '9')
                    prec = prec * 10 + *f++ - '0';
        }
        for (;; f++) {
            if (*f == 'h')
                len = len == -1 ? -2 : -1;
            else if (*f == 'l' || *f == 'z' || *f == 'j' || *f == 't')
                len++;
            else if (*f == 'L')
                len = 9;
            else
                break;
        }
        char buf[512], *s = buf;
        int n = 0, neg = 0;
        const char *pre = "";
        int c = *f;
        if (!c)
            break;
        switch (c) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'p':
        case 'b': {
            unsigned long long v;
            int base = c == 'x' || c == 'X' || c == 'p' ? 16 : c == 'o' ? 8 : c == 'b' ? 2 : 10;
            if (c == 'p') {
                v = (unsigned long)va_arg(ap, void *);
                alt = 1;
            } else if (c == 'd' || c == 'i') {
                long long sv = len >= 1 ? va_arg(ap, long) : va_arg(ap, int);
                if (len == -1)
                    sv = (short)sv;
                if (len == -2)
                    sv = (signed char)sv;
                neg = sv < 0;
                v = neg ? 0 - (unsigned long long)sv : (unsigned long long)sv;
            } else {
                v = len >= 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                if (len == -1)
                    v = (unsigned short)v;
                if (len == -2)
                    v = (unsigned char)v;
            }
            const char *dg = c == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char t[70];
            int k = 0;
            while (v) {
                t[k++] = dg[v % (unsigned)base];
                v /= (unsigned)base;
            }
            if (prec < 0 && !k)
                t[k++] = '0';
            while (k < prec && k < 69)
                t[k++] = '0';
            if (alt && c == 'o' && (!k || t[k - 1] != '0'))
                t[k++] = '0';
            while (k)
                buf[n++] = t[--k];
            if (neg)
                pre = "-";
            else if (plus && (c == 'd' || c == 'i'))
                pre = "+";
            else if (space && (c == 'd' || c == 'i'))
                pre = " ";
            if (alt && (c == 'x' || c == 'p') && n)
                pre = "0x";
            if (alt && c == 'X' && n)
                pre = "0X";
            if (prec >= 0)
                zero = 0;
            break;
        }
        case 'c':
            buf[n++] = (char)va_arg(ap, int);
            zero = 0;
            break;
        case 's': {
            s = va_arg(ap, char *);
            if (!s)
                s = "(null)";
            n = (int)(prec >= 0 ? strnlen(s, (size_t)prec) : strlen(s));
            zero = 0;
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            long double v = len == 9 ? va_arg(ap, long double) : va_arg(ap, double);
            neg = v < 0 || (v == 0 && 1 / v < 0);
            if (neg)
                v = -v;
            int mode = c == 'f' || c == 'F' ? 0 : c == 'e' || c == 'E' ? 1 : 2;
            n = fmt_double(buf, sizeof buf, v, prec, mode, alt, c < 'a');
            pre = neg ? "-" : plus ? "+" : space ? " " : "";
            if (v != v || v == 1.0 / 0.0)
                zero = 0;
            break;
        }
        case 'n':
            *va_arg(ap, int *) = (int)o->n;
            continue;
        case '%':
            buf[n++] = '%';
            break;
        default:
            buf[n++] = '%';
            buf[n++] = (char)c;
            break;
        }
        int pl = (int)strlen(pre), padn = width - n - pl;
        if (!left && !zero)
            pad(o, ' ', padn);
        emit(o, pre, (size_t)pl);
        if (!left && zero)
            pad(o, '0', padn);
        emit(o, s, (size_t)n);
        if (left)
            pad(o, ' ', padn);
    }
    return (int)o->n;
}

typedef struct {
    char *b;
    size_t cap, n;
} sbuf;
static void sput(void *c, const char *s, size_t n) {
    sbuf *b = c;
    for (size_t i = 0; i < n; i++, b->n++)
        if (b->n + 1 < b->cap)
            b->b[b->n] = s[i];
}
int vsnprintf(char *b, size_t cap, const char *f, va_list ap) {
    sbuf s = {b, cap, 0};
    out_t o = {sput, &s, 0};
    vformat(&o, f, ap);
    if (cap)
        b[s.n < cap ? s.n : cap - 1] = 0;
    return (int)s.n;
}
int snprintf(char *b, size_t cap, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, cap, f, ap);
    va_end(ap);
    return r;
}
int vsprintf(char *b, const char *f, va_list ap) {
    return vsnprintf(b, (size_t)1 << 30, f, ap);
}
int sprintf(char *b, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vsprintf(b, f, ap);
    va_end(ap);
    return r;
}

/* ---------- streams ---------- */
static int grow(FILE *f, size_t need) {
    if (need <= f->cap)
        return 0;
    size_t c = f->cap ? f->cap * 2 : 1024;
    while (c < need)
        c *= 2;
    unsigned char *n = realloc(f->b, c);
    if (!n)
        return -1;
    f->b = n;
    f->cap = c;
    return 0;
}
/* standard output keeps a line, then gives it to the system */
static char obuf[2][1024];
static size_t on[2];
static void oflush(int fd) {
    if (on[fd - 1]) {
        __os_write(fd, obuf[fd - 1], (long)on[fd - 1]);
        on[fd - 1] = 0;
    }
}
size_t fwrite(const void *p, size_t sz, size_t cnt, FILE *f) {
    size_t n = sz * cnt;
    const char *s = p;
    if (!f || !f->wr) {
        if (f)
            f->err = 1;
        return 0;
    }
    if (f->fd == 1 || f->fd == 2) {
        int k = f->fd - 1;
        for (size_t i = 0; i < n; i++) {
            obuf[k][on[k]++] = s[i];
            if (s[i] == '\n' || on[k] == sizeof obuf[k] || f->fd == 2)
                oflush(f->fd);
        }
        return cnt;
    }
    if (grow(f, f->pos + n)) {
        f->err = 1;
        return 0;
    }
    memcpy(f->b + f->pos, s, n);
    f->pos += n;
    if (f->pos > f->n)
        f->n = f->pos;
    f->dirty = 1;
    return cnt;
}
static void fput(void *c, const char *s, size_t n) {
    fwrite(s, 1, n, c);
}
int vfprintf(FILE *f, const char *fmt, va_list ap) {
    out_t o = {fput, f, 0};
    return vformat(&o, fmt, ap);
}
int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}
int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
int fputc(int c, FILE *f) {
    unsigned char b = (unsigned char)c;
    return fwrite(&b, 1, 1, f) ? b : EOF;
}
int putc(int c, FILE *f) {
    return fputc(c, f);
}
int putchar(int c) {
    return fputc(c, stdout);
}
int fputs(const char *s, FILE *f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) || !n ? 0 : EOF;
}
int puts(const char *s) {
    fputs(s, stdout);
    return fputc('\n', stdout) == EOF ? EOF : 0;
}
int fflush(FILE *f) {
    if (!f) {
        oflush(1);
        oflush(2);
        return 0;
    }
    if (f->fd == 1 || f->fd == 2) {
        oflush(f->fd);
        return 0;
    }
    if (f->fd == -2) { /* memory: the caller sees the text so far, with a 0 behind it */
        memref *m = (memref *)f->path;
        if (grow(f, f->n + 1))
            return EOF;
        f->b[f->n] = 0;
        *m->bp = (char *)f->b;
        *m->sp = f->n;
        return 0;
    }
    if (f->fd < 0 && f->wr && f->dirty && f->path) {
        if (__os_fwrite(f->path, f->b, (long)f->n) < 0) {
            f->err = 1;
            return EOF;
        }
        f->dirty = 0;
    }
    return 0;
}
void __stdio_flush(void) {
    fflush(0);
}
FILE *fopen(const char *path, const char *mode) {
    FILE *f = calloc(1, sizeof *f);
    if (!f)
        return 0;
    f->fd = -1;
    f->unget = -1;
    f->path = strdup(path);
    int plus = strchr(mode, '+') != 0;
    f->rd = mode[0] == 'r' || plus;
    f->wr = mode[0] == 'w' || mode[0] == 'a' || plus;
    if (mode[0] == 'r' || mode[0] == 'a') {
        long sz = __os_fsize(path);
        if (sz < 0 && mode[0] == 'r') {
            errno = 2;
            free(f->path);
            free(f);
            return 0;
        }
        if (sz > 0) {
            if (grow(f, (size_t)sz) || __os_fread(path, f->b, sz) != sz) {
                errno = 13;
                free(f->b);
                free(f->path);
                free(f);
                return 0;
            }
            f->n = (size_t)sz;
        }
        if (mode[0] == 'a')
            f->pos = f->n;
    }
    if (mode[0] == 'w')
        f->dirty = 1; /* also an empty file is written */
    return f;
}
int fclose(FILE *f) {
    if (!f)
        return EOF;
    int r = fflush(f);
    if (f->fd == -2) { /* the memory now belongs to the caller */
        memref *m = (memref *)f->path;
        m->bp = 0;
        free(f);
        return r;
    }
    if (f->fd < 0) {
        free(f->b);
        free(f->path);
        free(f);
    }
    return r;
}
size_t fread(void *p, size_t sz, size_t cnt, FILE *f) {
    size_t n = sz * cnt, k = 0;
    unsigned char *d = p;
    if (!f || !sz)
        return 0;
    if (f->fd == 0) {
        while (k < n) {
            int c = getchar();
            if (c < 0)
                break;
            d[k++] = (unsigned char)c;
        }
        return k / sz;
    }
    if (f->unget >= 0 && n) {
        d[k++] = (unsigned char)f->unget;
        f->unget = -1;
    }
    size_t av = f->pos < f->n ? f->n - f->pos : 0;
    size_t m = n - k < av ? n - k : av;
    memcpy(d + k, f->b + f->pos, m);
    f->pos += m;
    k += m;
    if (k < n)
        f->eof = 1;
    return k / sz;
}
static int ungot = -1;
int getchar(void) {
    if (ungot >= 0) {
        int c = ungot;
        ungot = -1;
        return c;
    }
    fflush(stdout);
    int c = __os_getchar();
    if (c < 0)
        fin.eof = 1;
    return c;
}
int fgetc(FILE *f) {
    if (f->fd == 0)
        return getchar();
    if (f->unget >= 0) {
        int c = f->unget;
        f->unget = -1;
        return c;
    }
    if (f->pos >= f->n) {
        f->eof = 1;
        return EOF;
    }
    return f->b[f->pos++];
}
int getc(FILE *f) {
    return fgetc(f);
}
int ungetc(int c, FILE *f) {
    if (c == EOF)
        return EOF;
    if (f->fd == 0)
        ungot = c;
    else
        f->unget = c;
    f->eof = 0;
    return c;
}
char *fgets(char *s, int n, FILE *f) {
    int k = 0;
    while (k < n - 1) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        s[k++] = (char)c;
        if (c == '\n')
            break;
    }
    if (!k)
        return 0;
    s[k] = 0;
    return s;
}
int feof(FILE *f) {
    return f->eof;
}
FILE *open_memstream(char **, size_t *);
int ferror(FILE *f) {
    return f->err;
}
void clearerr(FILE *f) {
    f->eof = f->err = 0;
}
int fseek(FILE *f, long off, int wh) {
    if (f->fd >= 0)
        return -1;
    long b = wh == SEEK_SET ? 0 : wh == SEEK_CUR ? (long)f->pos : (long)f->n;
    if (b + off < 0)
        return -1;
    f->pos = (size_t)(b + off);
    f->eof = 0;
    f->unget = -1;
    return 0;
}
long ftell(FILE *f) {
    return f->fd >= 0 ? -1 : (long)f->pos;
}
void rewind(FILE *f) {
    fseek(f, 0, SEEK_SET);
}
int remove(const char *p) {
    return __os_remove(p);
}
int rename(const char *a, const char *b) {
    return __os_rename(a, b);
}
void perror(const char *s) {
    if (s && *s)
        fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", strerror(errno));
}

/* ---------- reading numbers and words: sscanf ---------- */
#include <ctype.h>
int vsscanf(const char *s, const char *f, va_list ap) {
    int got = 0;
    const char *p = s;
    for (; *f; f++) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*p))
                p++;
            continue;
        }
        if (*f != '%') {
            if (*p != *f)
                return got;
            p++;
            continue;
        }
        f++;
        int skip = 0, width = 0, len = 0;
        if (*f == '*') {
            skip = 1;
            f++;
        }
        while (*f >= '0' && *f <= '9')
            width = width * 10 + *f++ - '0';
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'L') {
            len += *f == 'l' || *f == 'z' || *f == 'L';
            if (*f == 'h')
                len = -1;
            f++;
        }
        if (*f == '%') {
            if (*p++ != '%')
                return got;
            continue;
        }
        if (*f != 'c' && *f != '[' && *f != 'n')
            while (isspace((unsigned char)*p))
                p++;
        if (!*p && *f != 'n')
            return got ? got : EOF;
        char tmp[128];
        if (width > 0 && width < 127 && *f != 'c' && *f != 's') {
            memcpy(tmp, p, (size_t)width);
            tmp[width] = 0;
        }
        const char *src = width > 0 && width < 127 && *f != 'c' && *f != 's' ? tmp : p;
        char *e;
        switch (*f) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'o': {
            int base = *f == 'd' || *f == 'u' ? 10 : *f == 'x' ? 16 : *f == 'o' ? 8 : 0;
            long v = (long)strtoul(src, &e, base);
            if (*f == 'd' || *f == 'i')
                v = strtol(src, &e, base);
            if (e == src)
                return got;
            p += e - src;
            if (!skip) {
                if (len >= 1)
                    *va_arg(ap, long *) = v;
                else if (len < 0)
                    *va_arg(ap, short *) = (short)v;
                else
                    *va_arg(ap, int *) = (int)v;
                got++;
            }
            break;
        }
        case 'f':
        case 'e':
        case 'g': {
            double v = strtod(src, &e);
            if (e == src)
                return got;
            p += e - src;
            if (!skip) {
                if (len >= 1)
                    *va_arg(ap, double *) = v;
                else
                    *va_arg(ap, float *) = (float)v;
                got++;
            }
            break;
        }
        case 's': {
            char *d = skip ? 0 : va_arg(ap, char *);
            int k = 0;
            while (*p && !isspace((unsigned char)*p) && (!width || k < width)) {
                if (d)
                    d[k] = *p;
                k++;
                p++;
            }
            if (d) {
                d[k] = 0;
                got++;
            }
            break;
        }
        case 'c': {
            char *d = skip ? 0 : va_arg(ap, char *);
            int k = 0, w = width ? width : 1;
            while (*p && k < w) {
                if (d)
                    d[k] = *p;
                k++;
                p++;
            }
            if (d)
                got++;
            break;
        }
        case 'n':
            if (!skip)
                *va_arg(ap, int *) = (int)(p - s);
            break;
        default:
            return got;
        }
    }
    return got;
}
int sscanf(const char *s, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vsscanf(s, f, ap);
    va_end(ap);
    return r;
}
/* scanf reads one line of the standard input */
int scanf(const char *f, ...) {
    char l[512];
    if (!fgets(l, sizeof l, stdin))
        return EOF;
    va_list ap;
    va_start(ap, f);
    int r = vsscanf(l, f, ap);
    va_end(ap);
    return r;
}

/* ---------- a stream into memory (the compiler writes its assembly with it) ---------- */
static memref mrefs[16];
FILE *open_memstream(char **bp, size_t *sp) {
    FILE *f = calloc(1, sizeof *f);
    if (!f)
        return 0;
    f->fd = -2; /* memory */
    f->unget = -1;
    f->wr = 1;
    for (int i = 0; i < 16; i++)
        if (!mrefs[i].bp) {
            mrefs[i].bp = bp;
            mrefs[i].sp = sp;
            f->path = (char *)&mrefs[i]; /* where the result goes */
            *bp = 0;
            *sp = 0;
            return f;
        }
    free(f);
    return 0;
}
