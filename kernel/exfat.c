#include "exfat.h"
#ifdef HOST
#include <string.h>
#else
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strlen(const char *);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
#endif
#include "cp437.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define EOCH      0xFFFFFFF8u /* from here on: end of a cluster chain */
#define MAXS      24          /* entries per entry set we handle (file, stream, 17 names, extras) */
#define DIRMAX    (256ull << 20) /* largest directory the specification allows */
#define R16(p, o) ((u32)(p)[o] | (u32)(p)[(o) + 1] << 8)
#define R32(p, o) (R16(p, o) | R16(p, (o) + 2) << 16)
static u64 R64(const u8 *p, int o) {
    return R32(p, o) | (u64)R32(p, o + 4) << 32;
}
static void W16(u8 *p, int o, u32 v) {
    p[o] = (u8)v;
    p[o + 1] = (u8)(v >> 8);
}
static void W32(u8 *p, int o, u32 v) {
    W16(p, o, v & 0xFFFF);
    W16(p, o + 2, v >> 16);
}
static void W64(u8 *p, int o, u64 v) {
    W32(p, o, (u32)v);
    W32(p, o + 4, (u32)(v >> 32));
}
/* Names: Violyx uses code page 437, exFAT stores UTF-16 */
static u16 cp2u(u8 c) {
    if (c < 0x80)
        return c;
    for (unsigned i = 0; i < sizeof cp437_map / sizeof cp437_map[0]; i++)
        if (cp437_map[i].c == c)
            return cp437_map[i].u;
    return '_';
}
static u8 u2cp(u32 u) {
    if (u >= 32 && u < 127)
        return (u8)u;
    for (unsigned i = 0; i < sizeof cp437_map / sizeof cp437_map[0]; i++)
        if (cp437_map[i].u == u)
            return cp437_map[i].c;
    return '_';
}

/* ---------- Sectors, FAT and allocation bitmap ---------- */
static int rs(exfvol *v, u64 s, void *b) {
    return s < v->d.sectors && !v->d.rd(v->d.start + s, b) ? 0 : VE_IO;
}
static int ws(exfvol *v, u64 s, const void *b) {
    return s < v->d.sectors && !v->d.wr(v->d.start + s, b) ? 0 : VE_IO;
}
static int cvalid(exfvol *v, u32 c) {
    return c >= 2 && c - 2 < v->clusters;
}
/* n whole sectors in one go when the device can (USB: one command instead of n) */
static int rsn(exfvol *v, u64 s, u32 n, void *b) {
    if (s + n > v->d.sectors)
        return VE_IO;
    if (v->d.rdn)
        return v->d.rdn(v->d.start + s, n, b) ? VE_IO : 0;
    for (u32 i = 0; i < n; i++)
        if (v->d.rd(v->d.start + s + i, (u8 *)b + i * 512))
            return VE_IO;
    return 0;
}
static int wsn(exfvol *v, u64 s, u32 n, const void *b) {
    if (s + n > v->d.sectors)
        return VE_IO;
    if (v->d.wrn)
        return v->d.wrn(v->d.start + s, n, b) ? VE_IO : 0;
    for (u32 i = 0; i < n; i++)
        if (v->d.wr(v->d.start + s + i, (const u8 *)b + i * 512))
            return VE_IO;
    return 0;
}
static u64 c2s(exfvol *v, u32 c) {
    return v->heap + ((u64)(c - 2) << v->shift);
}
static u32 csize(exfvol *v) {
    return 512u << v->shift;
}
/* Next cluster; 1 means the FAT could not be read */
static u32 fget(exfvol *v, u32 c) {
    if (!cvalid(v, c))
        return 1;
    u64 s = v->fatoff + c / 128;
    if (v->fs != s) {
        if (rs(v, s, v->fb)) {
            v->fs = ~0ull;
            return 1;
        }
        v->fs = s;
    }
    return R32(v->fb, (c % 128) * 4);
}
static int fput(exfvol *v, u32 c, u32 val) {
    if (!cvalid(v, c))
        return VE_IO;
    v->cc_first = 0;
    u64 s = v->fatoff + c / 128;
    if (v->fs != s) {
        if (rs(v, s, v->fb)) {
            v->fs = ~0ull;
            return VE_IO;
        }
        v->fs = s;
    }
    W32(v->fb, (c % 128) * 4, val);
    return ws(v, s, v->fb);
}
/* Sector of the allocation bitmap that holds the bit of cluster c */
static int bmsec(exfvol *v, u32 c, u64 *out) {
    u64 byte = (u64)(c - 2) / 8, si = byte / 512, ci = si >> v->shift;
    if (byte >= v->bmlen)
        return VE_IO;
    u32 k = v->bmfirst;
    u64 i = 0;
    if (v->bm_c && v->bm_ci <= ci) {
        k = v->bm_c;
        i = v->bm_ci;
    }
    while (i < ci) {
        u32 n = fget(v, k);
        if (!cvalid(v, n))
            n = k + 1; /* not chained in the FAT: the bitmap lies in one piece */
        if (!cvalid(v, n))
            return VE_IO;
        k = n;
        i++;
    }
    v->bm_c = k;
    v->bm_ci = ci;
    *out = c2s(v, k) + (si & ((1u << v->shift) - 1));
    return 0;
}
/* The bitmap sector in bb is written back when another one is needed and before entries are
 * written (bmflush), so the bitmap always reaches the disk before the directory. */
static int bmflush(exfvol *v) {
    if (!v->bdirty)
        return 0;
    v->bdirty = 0;
    return ws(v, v->bs, v->bb);
}
static void sync_(exfvol *v) {
    bmflush(v);
    v->d.fl();
}
static int bmget(exfvol *v, u32 c) {
    u64 s;
    if (!cvalid(v, c) || bmsec(v, c, &s))
        return VE_IO;
    if (v->bs != s) {
        if (bmflush(v))
            return VE_IO;
        if (rs(v, s, v->bb)) {
            v->bs = ~0ull;
            return VE_IO;
        }
        v->bs = s;
    }
    u32 b = c - 2;
    return (v->bb[(b / 8) % 512] >> (b % 8)) & 1;
}
static int bmset(exfvol *v, u32 c, int on) {
    if (bmget(v, c) < 0)
        return VE_IO;
    u32 b = c - 2;
    u8 *p = &v->bb[(b / 8) % 512];
    *p = on ? *p | (u8)(1 << (b % 8)) : *p & (u8) ~(1 << (b % 8));
    v->bdirty = 1;
    return 0;
}
/* n free clusters in a row, marked as used (no FAT entries needed). 0 = there is no such run. */
static u32 run_alloc(exfvol *v, u64 n) {
    if (!n || n > v->clusters)
        return 0;
    u32 start = 0;
    u64 len = 0;
    for (int pass = 0; pass < 2; pass++) {
        u32 from = pass ? 2 : v->hint, to = pass ? v->hint : v->clusters + 2;
        len = 0;
        for (u32 c = from; c < to; c++) {
            int b = bmget(v, c);
            if (b < 0)
                return 0;
            if (b) {
                len = 0;
                continue;
            }
            if (!len)
                start = c;
            if (++len == n) {
                for (u64 i = 0; i < n; i++)
                    if (bmset(v, start + (u32)i, 1))
                        return 0;
                v->hint = start + (u32)n < v->clusters + 2 ? start + (u32)n : 2;
                return start;
            }
        }
    }
    return 0;
}
/* A free cluster, marked as used and as the end of a chain. 0 = volume full or error */
static u32 alloc(exfvol *v) {
    for (u32 n = 0; n < v->clusters; n++) {
        u32 c = 2 + (v->hint - 2 + n) % v->clusters;
        int b = bmget(v, c);
        if (b < 0)
            return 0;
        if (!b) {
            if (bmset(v, c, 1) || fput(v, c, 0xFFFFFFFFu))
                return 0;
            v->hint = c + 1 < v->clusters + 2 ? c + 1 : 2;
            return c;
        }
    }
    return 0;
}
/* Gives back the clusters of a file or directory */
static void cfree(exfvol *v, u32 first, u64 len, int nofat) {
    if (!cvalid(v, first))
        return;
    if (nofat) {
        u64 n = (len + csize(v) - 1) / csize(v);
        for (u64 i = 0; i < n && cvalid(v, first + (u32)i); i++)
            bmset(v, first + (u32)i, 0);
        return;
    }
    u32 c = first;
    for (u32 g = 0; cvalid(v, c) && g <= v->clusters; g++) {
        u32 n = fget(v, c);
        bmset(v, c, 0);
        fput(v, c, 0);
        if (n >= EOCH)
            break;
        c = n;
    }
}
static int czero(exfvol *v, u32 c) {
    static u8 z[512];
    memset(z, 0, 512);
    u64 s = c2s(v, c);
    for (u32 i = 0; i < (1u << v->shift); i++)
        if (ws(v, s + i, z))
            return VE_IO;
    return 0;
}

/* ---------- Streams: directories and file contents ---------- */
typedef struct {
    u32 first;
    u64 len; /* 0: unbounded (root directory, follows the FAT chain) */
    int nofat;
} xdir;
/* Sector k of a stream: 0 = found, 1 = past the end, < 0 = damaged */
static int ssec(exfvol *v, const xdir *d, u64 k, u64 *out) {
    if (d->len && k >= (d->len + 511) / 512)
        return 1;
    if (!d->len && k >= DIRMAX / 512)
        return VE_IO; /* the root directory may not be endless either */
    u64 ci = k >> v->shift;
    u32 c;
    if (d->nofat) {
        if (ci >= v->clusters || !cvalid(v, d->first) || !cvalid(v, d->first + (u32)ci))
            return VE_IO;
        c = d->first + (u32)ci;
    } else {
        u32 i = 0;
        c = d->first;
        if (v->cc_first && v->cc_first == d->first && v->cc_idx <= ci) {
            i = v->cc_idx;
            c = v->cc_clus;
        }
        if (!cvalid(v, c))
            return VE_IO;
        while (i < ci) {
            u32 n = fget(v, c);
            if (n >= EOCH)
                return d->len ? VE_IO : 1;
            if (!cvalid(v, n) || ++i > v->clusters)
                return VE_IO;
            c = n;
        }
        v->cc_first = d->first;
        v->cc_idx = i;
        v->cc_clus = c;
    }
    *out = c2s(v, c) + (k & ((1u << v->shift) - 1));
    return 0;
}

/* ---------- Directory entry sets ---------- */
typedef struct {
    u8 e[MAXS][32];
    u64 sec[MAXS];
    u16 off[MAXS];
    int n;
    char name[VFS_NAME + 1];
} xset;
static u16 setsum(u8 (*e)[32], int n) {
    u16 c = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < 32; j++)
            if (i || (j != 2 && j != 3))
                c = (u16)(((c & 1) ? 0x8000 : 0) + (c >> 1) + e[i][j]);
    return c;
}
static u16 upc(exfvol *v, u16 c) {
    return c < EXF_UP ? v->up[c] : c;
}
static u16 namehash(exfvol *v, const u16 *n, int l) {
    u16 h = 0;
    for (int i = 0; i < l; i++) {
        u16 c = upc(v, n[i]);
        h = (u16)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (c & 0xFF));
        h = (u16)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (c >> 8));
    }
    return h;
}
/* Checks a complete set and takes its name */
static int finish(xset *s) {
    if (s->n < 3 || s->e[1][0] != 0xC0 || setsum(s->e, s->n) != R16(s->e[0], 2))
        return 0;
    int l = s->e[1][3], nn = (l + 14) / 15;
    if (!l || 2 + nn > s->n)
        return 0;
    int k = 0;
    for (int i = 0; i < nn; i++) {
        if (s->e[2 + i][0] != 0xC1)
            return 0;
        for (int j = 0; j < 15 && k < l; j++, k++)
            s->name[k] = (char)u2cp(R16(s->e[2 + i], 2 + j * 2));
    }
    if (l > VFS_NAME)
        return 0;
    s->name[l] = 0;
    return 1;
}
typedef int (*xcb)(exfvol *, xset *, void *);
/* Calls cb for every valid file and directory of a directory */
static int walk(exfvol *v, const xdir *d, xcb cb, void *arg) {
    static xset s;
    static u8 b[512];
    int want = 0;
    for (u64 k = 0;; k++) {
        u64 sec;
        int r = ssec(v, d, k, &sec);
        if (r == 1)
            return 0;
        if (r < 0)
            return r;
        if (rs(v, sec, b))
            return VE_IO;
        for (int o = 0; o < 512; o += 32) {
            u8 *e = b + o, t = e[0];
            if (!t)
                return 0;
            if (want) {
                if ((t & 0xC0) == 0xC0) {
                    memcpy(s.e[s.n], e, 32);
                    s.sec[s.n] = sec;
                    s.off[s.n] = (u16)o;
                    s.n++;
                    if (--want == 0 && finish(&s)) {
                        r = cb(v, &s, arg);
                        if (r)
                            return r;
                    }
                    continue;
                }
                want = 0; /* an incomplete set is skipped */
            }
            if (t == 0x85 && e[1] >= 2 && e[1] + 1 <= MAXS) {
                memcpy(s.e[0], e, 32);
                s.sec[0] = sec;
                s.off[0] = (u16)o;
                s.n = 1;
                want = e[1];
            }
        }
    }
}
static u32 attr(const xset *s) {
    return R16(s->e[0], 4);
}
static void sdir(const xset *s, xdir *d) {
    d->first = R32(s->e[1], 20);
    d->len = R64(s->e[1], 24);
    d->nofat = (s->e[1][1] & 2) != 0;
}
static int lc(int c) {
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c == 0x8E)
        return 0x84;
    if (c == 0x99)
        return 0x94;
    if (c == 0x9A)
        return 0x81;
    if (c == 0x8F)
        return 0x86;
    if (c == 0x90)
        return 0x82;
    if (c == 0x80)
        return 0x87;
    if (c == 0xA5)
        return 0xA4;
    if (c == 0x92)
        return 0x91;
    return c;
}
/* Is path b the same as a or inside it? */
static int inside(const char *a, const char *b) {
    size_t al = strlen(a);
    while (al > 1 && a[al - 1] == '/')
        al--;
    if (strlen(b) < al)
        return 0;
    for (size_t i = 0; i < al; i++)
        if (lc((u8)a[i]) != lc((u8)b[i]))
            return 0;
    return !b[al] || b[al] == '/';
}
static int ieq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++)
        if (!a[i] || lc((u8)a[i]) != lc((u8)b[i]))
            return 0;
    return !a[n];
}
typedef struct {
    const char *n;
    int l, hit;
    xset *r;
} fnd;
static int fcb(exfvol *v, xset *s, void *a) {
    (void)v;
    fnd *f = a;
    if (ieq(s->name, f->n, f->l)) {
        memcpy(f->r, s, sizeof *s);
        f->hit = 1;
        return 1;
    }
    return 0;
}
static int find(exfvol *v, const xdir *d, const char *n, int l, xset *o) {
    fnd f = {n, l, 0, o};
    int r = walk(v, d, fcb, &f);
    return r < 0 ? r : f.hit;
}
/* A directory of the set s, checked */
static int asdir(exfvol *v, const xset *s, xdir *d) {
    if (!(attr(s) & 0x10))
        return VE_NOTDIR;
    sdir(s, d);
    if (!cvalid(v, d->first) || !d->len || d->len > DIRMAX)
        return VE_IO;
    return 0;
}
/* Resolves a path. o: the entry, par: its directory, ps: the set of that directory (n = 0 for the
 * root directory). *isroot when the path is the root itself. */
static int lookup(exfvol *v, const char *p, xset *o, xdir *par, xset *ps, int *isroot) {
    xdir cur = {v->root, 0, 0};
    const char *s = p;
    while (*s == '/')
        s++;
    *par = cur;
    ps->n = 0;
    *isroot = !*s;
    if (!*s)
        return 0;
    static xset t;
    while (*s) {
        const char *e = s;
        while (*e && *e != '/')
            e++;
        int r = find(v, &cur, s, (int)(e - s), o);
        if (r < 0)
            return r;
        if (!r)
            return VE_NOENT;
        *par = cur;
        s = e;
        while (*s == '/')
            s++;
        if (*s) {
            r = asdir(v, o, &cur);
            if (r)
                return r;
            memcpy(&t, o, sizeof t);
            memcpy(ps, &t, sizeof t);
        }
    }
    return 0;
}

/* ---------- Reading ---------- */
static void fallback_up(exfvol *v) {
    for (u32 i = 0; i < EXF_UP; i++) {
        u32 u = i;
        if ((i >= 'a' && i <= 'z') || (i >= 0xE0 && i <= 0xFE && i != 0xF7) ||
            (i >= 0x3B1 && i <= 0x3CB && i != 0x3C2) || (i >= 0x430 && i <= 0x44F))
            u = i - 0x20;
        else if (i == 0xFF)
            u = 0x178;
        else if (i == 0x3C2)
            u = 0x3A3;
        else if (i >= 0x450 && i <= 0x45F)
            u = i - 0x50;
        v->up[i] = (u16)u;
    }
}
/* The up-case table is stored compressed: 0xFFFF n skips n unchanged characters */
static int load_up(exfvol *v, u32 first, u64 len) {
    xdir d = {first, len, 0};
    static u8 b[512];
    u32 idx = 0;
    int skip = 0;
    for (u64 k = 0; k * 512 < len && idx < EXF_UP; k++) {
        u64 s;
        if (ssec(v, &d, k, &s) || rs(v, s, b))
            return VE_IO;
        for (int o = 0; o < 512 && k * 512 + o + 1 < len && idx < EXF_UP; o += 2) {
            u16 c = (u16)R16(b, o);
            if (skip) {
                idx += c;
                skip = 0;
            } else if (c == 0xFFFF)
                skip = 1;
            else
                v->up[idx++] = c;
        }
    }
    return 0;
}
int exf_mount(exfvol *v, const vfs_dev *d) {
    memset(v, 0, sizeof *v);
    v->d = *d;
    v->fs = v->bs = ~0ull;
    static u8 b[512];
    if (d->rd(d->start, b))
        return VE_IO;
    if (memcmp(b + 3, "EXFAT   ", 8) || b[510] != 0x55 || b[511] != 0xAA)
        return VE_FORMAT;
    for (int i = 11; i < 64; i++)
        if (b[i])
            return VE_FORMAT;
    u64 vl = R64(b, 72);
    u32 fo = R32(b, 80), fl = R32(b, 84), ho = R32(b, 88), cc = R32(b, 92), rc = R32(b, 96);
    int bps = b[108], sh = b[109], nf = b[110];
    if (bps != 9 || sh > 16 || nf < 1 || nf > 2)
        return VE_FORMAT; /* only 512-byte sectors, clusters up to 32 MB */
    if (vl > d->sectors)
        vl = d->sectors;
    if (fo < 24 || !fl || (u64)ho < (u64)fo + (u64)fl * nf || ho >= vl || !cc)
        return VE_FORMAT;
    if ((u64)cc > ((vl - ho) >> sh))
        cc = (u32)((vl - ho) >> sh);
    if ((u64)cc + 2 > (u64)fl * 128)
        cc = fl * 128 - 2;
    if (cc > 0xFFFFFFF5u - 2 || cc < 1)
        return VE_FORMAT;
    v->fatoff = fo;
    v->fatlen = fl;
    v->heap = ho;
    v->clusters = cc;
    v->root = rc;
    v->shift = (u32)sh;
    v->spc = 1u << sh;
    v->hint = 2;
    v->ro = nf != 1; /* two FATs (TexFAT) are only read */
    v->wasdirty = (R16(b, 106) & 2) != 0;
    if (!cvalid(v, rc))
        return VE_FORMAT;
    fallback_up(v);
    /* The root directory holds the allocation bitmap and the up-case table */
    xdir root = {rc, 0, 0};
    int bm = 0, up = 0;
    for (u64 k = 0; k < ((u64)v->clusters << v->shift); k++) {
        u64 s;
        int r = ssec(v, &root, k, &s);
        if (r == 1)
            break;
        if (r < 0 || rs(v, s, b))
            return VE_IO;
        int end = 0;
        for (int o = 0; o < 512 && !end; o += 32) {
            u8 *e = b + o;
            if (!e[0])
                end = 1;
            else if (e[0] == 0x81 && !bm && !(e[1] & 1)) {
                v->bmfirst = R32(e, 20);
                v->bmlen = R64(e, 24);
                bm = 1;
            } else if (e[0] == 0x82 && !up) {
                u32 f = R32(e, 20);
                u64 l = R64(e, 24);
                up = 1;
                if (cvalid(v, f) && l >= 256 && l <= 0x20000)
                    up = load_up(v, f, l) ? 1 : 2;
            }
        }
        if (end)
            break;
    }
    if (!bm || !cvalid(v, v->bmfirst) || v->bmlen * 8 < cc)
        v->ro = 1; /* without a valid bitmap nothing can be allocated safely */
    if (up != 2)
        v->ro = 1;
    v->ok = 1;
    return 0;
}
int exf_stat(exfvol *v, const char *p, vfs_ent *e) {
    static xset s, ps;
    xdir par;
    int root;
    int r = lookup(v, p, &s, &par, &ps, &root);
    if (r)
        return r;
    if (e) {
        memset(e, 0, sizeof *e);
        if (root)
            e->dir = 1;
        else {
            e->dir = !!(attr(&s) & 0x10);
            e->size = e->dir ? 0 : R64(s.e[1], 24);
            memcpy(e->name, s.name, VFS_NAME + 1);
        }
    }
    return 0;
}
int64_t exf_read(exfvol *v, const char *p, void *b, u64 max) {
    return exf_read_at(v, p, b, 0, max);
}
/* Reads up to max bytes from offset off on; behind the valid data length it reads zeros */
int64_t exf_read_at(exfvol *v, const char *p, void *b, u64 off, u64 max) {
    static xset s, ps;
    xdir par, f;
    int root;
    int r = lookup(v, p, &s, &par, &ps, &root);
    if (r)
        return r;
    if (root || (attr(&s) & 0x10))
        return VE_ISDIR;
    sdir(&s, &f);
    u64 size = f.len, valid = R64(s.e[1], 8);
    if (valid > size)
        valid = size;
    if (off >= size)
        return 0;
    u64 sz = size - off < max ? size - off : max;
    if (sz && !cvalid(v, f.first))
        return VE_IO;
    static u8 t[512];
    for (u64 o = 0; o < sz;) {
        u64 at = off + o, k = at / 512, in = at % 512;
        u64 n = 512 - in < sz - o ? 512 - in : sz - o;
        if (!in && sz - o >= 1024 && k * 512 + 1024 <= valid) { /* whole sectors of a cluster */
            u64 run = v->spc - (k & (v->spc - 1)), m = (sz - o) / 512, vl = (valid - k * 512) / 512,
                sec;
            if (run > m)
                run = m;
            if (run > vl)
                run = vl;
            if (run > 1) {
                if (ssec(v, &f, k, &sec) || rsn(v, sec, (u32)run, (u8 *)b + o))
                    return VE_IO;
                o += run * 512;
                continue;
            }
        }
        if (k * 512 < valid) {
            u64 sec;
            if (ssec(v, &f, k, &sec) || rs(v, sec, t))
                return VE_IO;
            if (k * 512 + 512 > valid)
                memset(t + (valid - k * 512), 0, 512 - (valid - k * 512));
        } else
            memset(t, 0, 512);
        memcpy((u8 *)b + o, t + in, n);
        o += n;
    }
    return (int64_t)sz;
}
typedef struct {
    int (*cb)(const vfs_ent *, void *);
    void *a;
} lst;
static int lcb(exfvol *v, xset *s, void *x) {
    (void)v;
    lst *l = x;
    static vfs_ent e;
    memset(&e, 0, sizeof e);
    memcpy(e.name, s->name, VFS_NAME + 1);
    e.dir = !!(attr(s) & 0x10);
    e.size = e.dir ? 0 : R64(s->e[1], 24);
    return l->cb(&e, l->a) ? 1 : 0;
}
int exf_list(exfvol *v, const char *p, int (*cb)(const vfs_ent *, void *), void *a) {
    static xset s, ps;
    xdir par, d = {v->root, 0, 0};
    int root;
    int r = lookup(v, p, &s, &par, &ps, &root);
    if (r)
        return r;
    if (!root) {
        r = asdir(v, &s, &d);
        if (r)
            return r;
    }
    lst l = {cb, a};
    r = walk(v, &d, lcb, &l);
    return r < 0 ? r : 0;
}

/* ---------- Writing ---------- */
/* While Violyx writes, the volume is marked dirty: if the stick is pulled in between, Windows
 * and Linux check it on the next start. */
static int wbegin(exfvol *v) {
    if (v->ro)
        return VE_DENIED;
    if (v->depth++)
        return 0;
    static u8 b[512];
    if (rs(v, 0, b)) {
        v->depth = 0;
        return VE_IO;
    }
    b[106] |= 2;
    b[112] = 0xFF; /* percent in use: unknown */
    if (ws(v, 0, b)) {
        v->depth = 0;
        return VE_IO;
    }
    sync_(v);
    return 0;
}
static int wend(exfvol *v, int r) {
    if (--v->depth > 0)
        return r;
    v->depth = 0;
    sync_(v);
    if (!v->wasdirty) {
        static u8 b[512];
        if (!rs(v, 0, b)) {
            b[106] &= (u8)~2;
            if (!ws(v, 0, b))
                sync_(v);
        }
    }
    return r;
}
static int put_ent(exfvol *v, u64 s, int o, const u8 *e) {
    static u8 b[512];
    if (bmflush(v) || rs(v, s, b))
        return VE_IO;
    memcpy(b + o, e, 32);
    return ws(v, s, b);
}
/* Writes a set: the secondary entries first, the file entry last */
static int put_set(exfvol *v, u8 (*e)[32], int n, const u64 *ss, const u16 *so) {
    for (int i = n - 1; i >= 0; i--)
        if (put_ent(v, ss[i], so[i], e[i]))
            return VE_IO;
    return 0;
}
static int del_set(exfvol *v, const xset *s) {
    static u8 b[512];
    if (bmflush(v))
        return VE_IO;
    for (int i = 0; i < s->n; i++) {
        if (rs(v, s->sec[i], b))
            return VE_IO;
        b[s->off[i]] &= 0x7F;
        if (ws(v, s->sec[i], b))
            return VE_IO;
    }
    return 0;
}
static u32 ts(u32 t) {
    u32 d = t / 86400, r = t % 86400, z = d + 719468, era = z / 146097, doe = z - era * 146097,
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365,
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153,
        dd = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9,
        y = yoe + era * 400 + (m <= 2);
    u32 yy = y >= 1980 ? y - 1980 : 0;
    if (yy > 127)
        yy = 127;
    return (yy << 25) | (m << 21) | (dd << 16) | ((r / 3600) << 11) | (((r / 60) % 60) << 5) |
           ((r % 60) / 2);
}
static int legal(const char *n, int l) {
    if (l <= 0 || l > 255)
        return 0;
    if (n[l - 1] == '.' || n[l - 1] == ' ')
        return 0;
    for (int i = 0; i < l; i++) {
        u8 c = n[i];
        if (c < 32 || c == '"' || c == '*' || c == '/' || c == ':' || c == '<' || c == '>' ||
            c == '?' || c == '\\' || c == '|')
            return 0;
    }
    return 1;
}
/* Builds an entry set. t0/t1: file and stream entry to keep (rename), otherwise new ones. */
static int mkset(exfvol *v, u8 (*e)[32], const char *nm, int l, const u8 *t0, const u8 *t1, u32 at,
                 u32 first, u64 len, int nofat, u32 now) {
    int nn = (l + 14) / 15, n = 2 + nn;
    memset(e, 0, (size_t)n * 32);
    if (t0)
        memcpy(e[0], t0, 32);
    else {
        u32 x = ts(now);
        W16(e[0], 4, at);
        W32(e[0], 8, x);
        W32(e[0], 12, x);
        W32(e[0], 16, x);
    }
    e[0][0] = 0x85;
    e[0][1] = (u8)(n - 1);
    if (t1)
        memcpy(e[1], t1, 32);
    else {
        e[1][1] = (u8)(1 | (nofat ? 2 : 0)); /* allocation possible; 2: in one piece, no FAT */
        W64(e[1], 8, len);
        W32(e[1], 20, first);
        W64(e[1], 24, len);
    }
    e[1][0] = 0xC0;
    e[1][3] = (u8)l;
    u16 un[255];
    for (int i = 0; i < l; i++)
        un[i] = cp2u((u8)nm[i]);
    W16(e[1], 4, namehash(v, un, l));
    for (int i = 0; i < nn; i++) {
        e[2 + i][0] = 0xC1;
        for (int j = 0; j < 15 && i * 15 + j < l; j++)
            W16(e[2 + i], 2 + j * 2, un[i * 15 + j]);
    }
    W16(e[0], 2, setsum(e, n));
    return n;
}
/* n free entries in a row: 0 = found, 1 = directory full */
static int slots(exfvol *v, const xdir *d, int n, u64 *ss, u16 *so) {
    static u8 b[512];
    int run = 0;
    for (u64 k = 0;; k++) {
        u64 sec;
        int r = ssec(v, d, k, &sec);
        if (r)
            return r;
        if (rs(v, sec, b))
            return VE_IO;
        for (int o = 0; o < 512; o += 32) {
            if (!(b[o] & 0x80)) {
                ss[run] = sec;
                so[run] = (u16)o;
                if (++run >= n)
                    return 0;
            } else
                run = 0;
        }
    }
}
/* Makes a directory one cluster bigger. self: its own entry set (n = 0 for the root). */
static int grow(exfvol *v, xdir *d, xset *self) {
    u32 cs = csize(v), nc;
    if (d->nofat) {
        u64 cnt = d->len / cs;
        if (!cnt || cnt > v->clusters)
            return VE_IO;
        u32 nx = d->first + (u32)cnt;
        int b = cvalid(v, nx) ? bmget(v, nx) : 1;
        if (b < 0)
            return VE_IO;
        if (!b) { /* the next cluster is free: the directory stays in one piece */
            if (czero(v, nx) || bmset(v, nx, 1))
                return VE_IO;
            nc = nx;
        } else {
            nc = alloc(v);
            if (!nc)
                return VE_NOSPC;
            if (czero(v, nc))
                return VE_IO;
            for (u64 i = 0; i + 1 < cnt; i++)
                if (fput(v, d->first + (u32)i, d->first + (u32)i + 1))
                    return VE_IO;
            if (fput(v, d->first + (u32)cnt - 1, nc))
                return VE_IO;
            d->nofat = 0;
        }
    } else {
        u32 c = d->first;
        for (u32 g = 0;; g++) {
            u32 n = fget(v, c);
            if (n >= EOCH)
                break;
            if (!cvalid(v, n) || g > v->clusters)
                return VE_IO;
            c = n;
        }
        nc = alloc(v);
        if (!nc)
            return VE_NOSPC;
        if (czero(v, nc) || fput(v, c, nc))
            return VE_IO;
    }
    if (self && self->n) {
        d->len += cs;
        u8 *x = self->e[1];
        x[1] = (u8)((x[1] & ~2) | (d->nofat ? 2 : 0));
        W64(x, 8, d->len);
        W64(x, 24, d->len);
        W16(self->e[0], 2, setsum(self->e, self->n));
        if (put_ent(v, self->sec[1], self->off[1], self->e[1]) ||
            put_ent(v, self->sec[0], self->off[0], self->e[0]))
            return VE_IO;
    }
    sync_(v);
    return 0;
}
static int add(exfvol *v, xdir *d, xset *self, u8 (*e)[32], int n) {
    u64 ss[MAXS];
    u16 so[MAXS];
    for (int tries = 0; tries < 4; tries++) {
        int r = slots(v, d, n, ss, so);
        if (r < 0)
            return r;
        if (!r) {
            r = put_set(v, e, n, ss, so);
            sync_(v);
            return r;
        }
        r = grow(v, d, self);
        if (r)
            return r;
    }
    return VE_NOSPC;
}
/* Splits a path into its directory and the last name */
static int spl(exfvol *v, const char *p, xdir *dir, xset *self, const char **n, int *l) {
    size_t L = strlen(p);
    while (L > 1 && p[L - 1] == '/')
        L--;
    const char *e = p + L, *s = e;
    while (s > p && s[-1] != '/')
        s--;
    int nl = (int)(e - s);
    if (nl <= 0 || !legal(s, nl) || (nl == 1 && s[0] == '.') ||
        (nl == 2 && s[0] == '.' && s[1] == '.'))
        return VE_INVAL;
    *n = s;
    *l = nl;
    static char par[VFS_PATH];
    size_t pl = (size_t)(s - p);
    if (pl >= VFS_PATH)
        return VE_INVAL;
    memcpy(par, p, pl);
    par[pl] = 0;
    static xset ps;
    xdir pp;
    int root;
    int r = lookup(v, par, self, &pp, &ps, &root);
    if (r)
        return r;
    if (root) {
        dir->first = v->root;
        dir->len = 0;
        dir->nofat = 0;
        self->n = 0;
        return 0;
    }
    return asdir(v, self, dir);
}
int exf_write(exfvol *v, const char *p, const void *b, u64 len, u32 now) {
    static xset self, old;
    static u8 e[MAXS][32];
    xdir dir;
    const char *n;
    int l;
    int r = spl(v, p, &dir, &self, &n, &l);
    if (r)
        return r;
    int hit = find(v, &dir, n, l, &old);
    if (hit < 0)
        return hit;
    if (hit && (attr(&old) & 0x10))
        return VE_ISDIR;
    if (hit && (attr(&old) & 1))
        return VE_DENIED;
    r = wbegin(v);
    if (r)
        return r;
    u32 first = 0, prev = 0, cs = csize(v);
    static u8 s[512];
    /* Like Windows: a file in one piece needs no FAT chain */
    int nofat = 0;
    if (len) {
        first = run_alloc(v, (len + cs - 1) / cs);
        nofat = first != 0;
    }
    for (u64 o = 0; nofat && o < len;) {
        u64 s0 = c2s(v, first + (u32)(o / cs));
        for (u32 i = 0; i < v->spc && o < len; i++) {
            if (len - o >= 1024) { /* whole sectors straight from the buffer */
                u64 run = v->spc - i, m = (len - o) / 512;
                if (run > m)
                    run = m;
                if (run > 1) {
                    if (wsn(v, s0 + i, (u32)run, (const u8 *)b + o)) {
                        cfree(v, first, len, 1);
                        return wend(v, VE_IO);
                    }
                    o += run * 512;
                    i += (u32)run - 1;
                    continue;
                }
            }
            u64 k = len - o > 512 ? 512 : len - o;
            memset(s, 0, 512);
            memcpy(s, (const u8 *)b + o, k);
            if (ws(v, s0 + i, s)) {
                cfree(v, first, len, 1);
                return wend(v, VE_IO);
            }
            o += k;
        }
    }
    for (u64 o = 0; !nofat && o < len;) {
        u32 c = alloc(v);
        if (!c || (prev && fput(v, prev, c))) {
            if (c && c != first)
                cfree(v, c, 0, 0);
            cfree(v, first, 0, 0);
            return wend(v, c ? VE_IO : VE_NOSPC);
        }
        if (!prev)
            first = c;
        prev = c;
        u64 s0 = c2s(v, c);
        for (u32 i = 0; i < v->spc && o < len; i++) {
            if (len - o >= 1024) { /* whole sectors straight from the buffer */
                u64 run = v->spc - i, m = (len - o) / 512;
                if (run > m)
                    run = m;
                if (run > 1) {
                    if (wsn(v, s0 + i, (u32)run, (const u8 *)b + o)) {
                        cfree(v, first, 0, 0);
                        return wend(v, VE_IO);
                    }
                    o += run * 512;
                    i += (u32)run - 1;
                    continue;
                }
            }
            u64 k = len - o > 512 ? 512 : len - o;
            memset(s, 0, 512);
            memcpy(s, (const u8 *)b + o, k);
            if (ws(v, s0 + i, s)) {
                cfree(v, first, 0, 0);
                return wend(v, VE_IO);
            }
            o += k;
        }
    }
    sync_(v);
    if (hit) {
        xdir was;
        sdir(&old, &was);
        u8 *x = old.e[1];
        x[1] = (u8)(1 | (nofat ? 2 : 0));
        W64(x, 8, len);
        W32(x, 20, first);
        W64(x, 24, len);
        u32 t = ts(now);
        W32(old.e[0], 12, t);
        W32(old.e[0], 16, t);
        old.e[0][22] = old.e[0][23] = 0;
        W16(old.e[0], 4, attr(&old) | 0x20);
        W16(old.e[0], 2, setsum(old.e, old.n));
        if (put_ent(v, old.sec[1], old.off[1], old.e[1]) ||
            put_ent(v, old.sec[0], old.off[0], old.e[0])) {
            cfree(v, first, len, nofat);
            return wend(v, VE_IO);
        }
        sync_(v);
        cfree(v, was.first, was.len, was.nofat);
        return wend(v, 0);
    }
    int k = mkset(v, e, n, l, 0, 0, 0x20, first, len, nofat, now);
    r = add(v, &dir, &self, e, k);
    if (r)
        cfree(v, first, len, nofat);
    return wend(v, r);
}
int exf_mkdir(exfvol *v, const char *p, u32 now) {
    static xset self, old;
    static u8 e[MAXS][32];
    xdir dir;
    const char *n;
    int l;
    int r = spl(v, p, &dir, &self, &n, &l);
    if (r)
        return r;
    int hit = find(v, &dir, n, l, &old);
    if (hit < 0)
        return hit;
    if (hit)
        return VE_EXIST;
    r = wbegin(v);
    if (r)
        return r;
    u32 c = run_alloc(v, 1);
    if (!c)
        return wend(v, VE_NOSPC);
    if (czero(v, c)) {
        cfree(v, c, csize(v), 1);
        return wend(v, VE_IO);
    }
    int k = mkset(v, e, n, l, 0, 0, 0x10, c, csize(v), 1, now);
    r = add(v, &dir, &self, e, k);
    if (r)
        cfree(v, c, csize(v), 1);
    return wend(v, r);
}
static int anycb(exfvol *v, xset *s, void *a) {
    (void)v;
    (void)s;
    *(int *)a = 1;
    return 1;
}
int exf_remove(exfvol *v, const char *p) {
    static xset s, ps;
    xdir par;
    int root;
    int r = lookup(v, p, &s, &par, &ps, &root);
    if (r)
        return r;
    if (root)
        return VE_INVAL;
    if (attr(&s) & 1)
        return VE_DENIED;
    xdir d;
    sdir(&s, &d);
    if (attr(&s) & 0x10) {
        r = asdir(v, &s, &d);
        if (r)
            return r;
        int any = 0;
        r = walk(v, &d, anycb, &any);
        if (r < 0)
            return r;
        if (any)
            return VE_NOTEMPTY;
    }
    r = wbegin(v);
    if (r)
        return r;
    r = del_set(v, &s);
    if (r)
        return wend(v, r);
    sync_(v);
    cfree(v, d.first, d.len, d.nofat);
    return wend(v, 0);
}
int exf_rename(exfvol *v, const char *a, const char *b) {
    static xset s, ps, self, other;
    static u8 e[MAXS][32];
    xdir par;
    int root;
    int r = lookup(v, a, &s, &par, &ps, &root);
    if (r)
        return r;
    if (root)
        return VE_INVAL;
    if (inside(a, b) && strlen(b) > strlen(a)) /* not into itself */
        return VE_INVAL;
    xdir dir;
    const char *n;
    int l;
    r = spl(v, b, &dir, &self, &n, &l);
    if (r)
        return r;
    int hit = find(v, &dir, n, l, &other);
    if (hit < 0)
        return hit;
    if (hit && !(other.sec[0] == s.sec[0] && other.off[0] == s.off[0]))
        return VE_EXIST;
    r = wbegin(v);
    if (r)
        return r;
    int k = mkset(v, e, n, l, s.e[0], s.e[1], 0, 0, 0, 0, 0);
    r = add(v, &dir, &self, e, k);
    if (!r)
        r = del_set(v, &s);
    return wend(v, r);
}
