/* Vio-Audio for Violyx - a sound editor for the keyboard, in the spirit of Audacity.
   Copyright (c) 2026, Merecete
   SPDX-License-Identifier: BSD-3-Clause

   Opens WAV, MP3, OGG and FLAC, records from the microphone, cuts, copies and pastes, has
   effects (normalize, volume, fade in and out, silence, reverse, echo, speed, mono) with undo,
   and saves WAV, FLAC or OGG.

   Space play/stop, R record, Left/Right move the cursor, Shift + arrows select, Up/Down zoom,
   X C V cut copy paste, Del delete, Z undo, Y redo, E effects, O open, S save, F1 all keys.

   The sound lives in blocks of 128 KB that are never changed once written. A track is a list
   of pieces (block, start, length), so cutting and pasting only rewrite that list, and each
   step of the undo history is just an older list that shares the blocks. */
#include "violyx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- memory for the decoders and encoders: the pool of helpers/vxogg.c ---------- */
void *vxo_malloc(size_t);
void *vxo_calloc(size_t, size_t);
void *vxo_realloc(void *, size_t);
void vxo_free(void *);
void vxo_qsort(void *, size_t, size_t, int (*)(const void *, const void *));
#ifndef HOSTTEST
void *malloc(size_t n) {
    return vxo_malloc(n);
}
void free(void *p) {
    vxo_free(p);
}
void *realloc(void *p, size_t n) {
    return vxo_realloc(p, n);
}
void qsort(void *b, size_t n, size_t s, int (*c)(const void *, const void *)) {
    vxo_qsort(b, n, s, c);
}
#endif

/* ---------- decoders and encoders (the same as in XZN) ---------- */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_MAX_CHANNELS 8
#define get_bits vb_get_bits
#include "stb_vorbis.c"
#undef get_bits
#undef L
#undef R
#undef C
#undef M
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
#define DRFLAC_ASSERT(x) ((void)0)
#include "dr_flac.h"
#define XF_VENDOR "Vio-Audio"
#include "xznflac.h"
#include "xznogg.h"

static int en; /* English or German */
static int exact; /* the open file is 16 bit with up to two channels: saving it again loses nothing */
#define T(de, e) (en ? (e) : (de))

/* ---------- blocks: 128 KB each, stereo 16 bit, with a summary for the waveform ---------- */
#define UNIT 131072
#define GRP 256                             /* frames per summary entry */
#define BLKF 32510                          /* frames per block */
#define NGRP ((BLKF + GRP - 1) / GRP)
typedef struct blk {
    int ref, fill;            /* pieces that use the block, frames written */
    short sum[NGRP][4];       /* min and max of left and right per 256 frames */
    short s[BLKF * 2];
} blk;
typedef char blk_fits[sizeof(blk) <= UNIT ? 1 : -1];
#ifndef MAXUNITS
#define MAXUNITS 1500 /* about 190 MB, the rest of the 256 MB stays for decoders and encoders */
#endif
static void *ufree;
static int nunits, uused;
static int drop_history(void);
static void *unit_get(void) {
    void *u;
    if (ufree) {
        u = ufree;
        ufree = *(void **)u;
    } else {
        if (nunits >= MAXUNITS)
            return 0;
        u = vx_alloc(UNIT);
        if (!u)
            return 0;
        nunits++;
    }
    uused++;
    return u;
}
/* a unit, and when there is none left, the oldest steps of the undo history make room */
static void *unit_new(void) {
    for (;;) {
        void *u = unit_get();
        if (u || !drop_history())
            return u;
    }
}
/* one unit kept back for a list of pieces: a recording that filled the memory still fits in */
static void *reserve;
static void *unit_list(void) {
    void *u = unit_new();
    if (!u && reserve) {
        u = reserve;
        reserve = 0;
    }
    return u;
}
static void unit_put(void *u) {
    *(void **)u = ufree;
    ufree = u;
    uused--;
}
static blk *zero; /* a block of silence that all silent pieces share */
static void blk_unref(blk *b) {
    if (--b->ref == 0 && b != zero)
        unit_put(b);
}
static void blk_sums(blk *b) {
    for (int g = 0; g * GRP < b->fill; g++) {
        int e = (g + 1) * GRP < b->fill ? (g + 1) * GRP : b->fill;
        short a = 32767, c = -32768, d = 32767, f = -32768;
        for (int i = g * GRP; i < e; i++) {
            short l = b->s[2 * i], r = b->s[2 * i + 1];
            if (l < a)
                a = l;
            if (l > c)
                c = l;
            if (r < d)
                d = r;
            if (r > f)
                f = r;
        }
        b->sum[g][0] = a;
        b->sum[g][1] = c;
        b->sum[g][2] = d;
        b->sum[g][3] = f;
    }
}

/* ---------- sequences: lists of pieces ---------- */
typedef struct {
    blk *b;
    int off, n;
} piece;
#ifndef MAXP
#define MAXP ((int)(UNIT / sizeof(piece)))
#endif
typedef struct {
    piece *p;
    int np;
    long len; /* frames */
} seq;
static void seq_free(seq *s) {
    for (int i = 0; i < s->np; i++)
        blk_unref(s->p[i].b);
    if (s->p)
        unit_put(s->p);
    s->p = 0;
    s->np = 0;
    s->len = 0;
}
typedef struct {
    seq s;
    int fail; /* 1: no memory, 2: too many pieces */
} sb;
static int sb_add(sb *o, blk *b, int off, long n) {
    if (n <= 0)
        return 1;
    seq *s = &o->s;
    if (s->np && s->p[s->np - 1].b == b && s->p[s->np - 1].off + s->p[s->np - 1].n == off) {
        s->p[s->np - 1].n += (int)n;
        s->len += n;
        return 1;
    }
    if (!s->p && !(s->p = unit_list())) {
        o->fail = 1;
        return 0;
    }
    if (s->np >= MAXP) {
        o->fail = 2;
        return 0;
    }
    s->p[s->np].b = b;
    s->p[s->np].off = off;
    s->p[s->np].n = (int)n;
    s->np++;
    b->ref++;
    s->len += n;
    return 1;
}
/* frames a..b of s */
static int sb_slice(sb *o, const seq *s, long a, long b) {
    long ps = 0;
    for (int i = 0; i < s->np && ps < b; i++) {
        const piece *p = &s->p[i];
        long pe = ps + p->n;
        if (pe > a) {
            long x0 = a > ps ? a : ps, x1 = b < pe ? b : pe;
            if (!sb_add(o, p->b, p->off + (int)(x0 - ps), x1 - x0))
                return 0;
        }
        ps = pe;
    }
    return 1;
}
static int sb_silence(sb *o, long n) {
    while (n > 0) {
        long k = n > BLKF ? BLKF : n;
        if (!sb_add(o, zero, 0, k))
            return 0;
        n -= k;
    }
    return 1;
}

/* reading frames in order */
typedef struct {
    const seq *s;
    int i;
    long ps, f; /* piece, its first frame, the next frame */
} rd;
static void rd_seek(rd *r, const seq *s, long f) {
    r->s = s;
    r->i = 0;
    r->ps = 0;
    while (r->i < s->np && r->ps + s->p[r->i].n <= f)
        r->ps += s->p[r->i++].n;
    r->f = f;
}
static int rd_get(rd *r, short *o, int n) {
    int got = 0;
    while (got < n && r->i < r->s->np) {
        const piece *p = &r->s->p[r->i];
        long in = r->f - r->ps, k = p->n - in;
        if (k <= 0) {
            r->ps += p->n;
            r->i++;
            continue;
        }
        if (k > n - got)
            k = n - got;
        memcpy(o + 2 * got, p->b->s + 2 * (p->off + in), (size_t)k * 4);
        got += (int)k;
        r->f += k;
    }
    return got;
}
/* random access with a buffer, for playing and resampling */
typedef struct {
    const seq *s;
    long b0;
    int bn;
    rd r;
    short buf[2 * 4096];
} src_t;
static void src_init(src_t *q, const seq *s) {
    q->s = s;
    q->b0 = 0;
    q->bn = 0;
}
static const short *src_at(src_t *q, long i) {
    static const short nil[2];
    if (i < q->b0 || i >= q->b0 + q->bn) {
        if (i < 0 || i >= q->s->len)
            return nil;
        rd_seek(&q->r, q->s, i);
        q->b0 = i;
        q->bn = rd_get(&q->r, q->buf, 4096);
        if (q->bn <= 0)
            return nil;
    }
    return q->buf + 2 * (i - q->b0);
}

/* writing new sound into fresh blocks */
typedef struct {
    sb o;
    blk *b;
} wr;
static int wr_flush(wr *w) {
    blk *b = w->b;
    w->b = 0;
    if (!b)
        return 1;
    if (!b->fill) {
        unit_put(b);
        return 1;
    }
    blk_sums(b);
    if (!sb_add(&w->o, b, 0, b->fill)) {
        if (!b->ref)
            unit_put(b);
        return 0;
    }
    return 1;
}
static int wr_put(wr *w, const short *f, long n) {
    while (n > 0) {
        if (!w->b) {
            if (!(w->b = unit_new())) {
                w->o.fail = 1;
                return 0;
            }
            w->b->ref = 0;
            w->b->fill = 0;
        }
        long k = BLKF - w->b->fill;
        if (k > n)
            k = n;
        memcpy(w->b->s + 2 * w->b->fill, f, (size_t)k * 4);
        w->b->fill += (int)k;
        f += 2 * k;
        n -= k;
        if (w->b->fill == BLKF && !wr_flush(w))
            return 0;
    }
    return 1;
}
static void wr_abort(wr *w) {
    if (w->b)
        unit_put(w->b);
    w->b = 0;
    seq_free(&w->o.s);
}
/* the finished sequence; also what was written before memory ran out (fail is then set) */
static seq wr_end(wr *w) {
    wr_flush(w);
    seq s = w->o.s;
    w->o.s.p = 0;
    w->o.s.np = 0;
    w->o.s.len = 0;
    return s;
}

/* ---------- the document and its history ---------- */
typedef struct {
    seq s;
    long cur, anc; /* cursor, and the other end of the selection */
    int sel, rate, mono;
    unsigned gen; /* the same number: the same sound */
    const char *what;
} state;
#define MAXU 40
static state D, und[MAXU], red[MAXU];
static int nund, nred;
static unsigned gens, saved_gen;
static char path[256], fname[64];
static seq clip;
static int clip_rate, clip_mono;
static long vstart, spp = 1; /* the view: first frame and frames per pixel */
static void st_free(state *s) {
    seq_free(&s->s);
}
static int drop_history(void) {
    if (nred) { /* redo steps first, the oldest of them */
        st_free(&red[0]);
        memmove(red, red + 1, sizeof(state) * (size_t)--nred);
        return 1;
    }
    if (nund) {
        st_free(&und[0]);
        memmove(und, und + 1, sizeof(state) * (size_t)--nund);
        return 1;
    }
    return 0;
}
static void history_clear(void) {
    while (nred)
        st_free(&red[--nred]);
    while (nund)
        st_free(&und[--nund]);
}
static int modified(void) {
    return D.gen != saved_gen;
}
static long sel_a(void) {
    return D.sel ? (D.anc < D.cur ? D.anc : D.cur) : D.cur;
}
static long sel_b(void) {
    return D.sel ? (D.anc < D.cur ? D.cur : D.anc) : D.cur;
}
static int has_sel(void) {
    return D.sel && D.anc != D.cur;
}
/* the new sound becomes the document, the old one goes to the history */
static void commit(seq ns, int rate, int mono, const char *what) {
    while (nred)
        st_free(&red[--nred]);
    if (nund == MAXU) {
        st_free(&und[0]);
        memmove(und, und + 1, sizeof(state) * (MAXU - 1));
        nund--;
    }
    und[nund++] = D;
    D.s = ns;
    D.rate = rate;
    D.mono = mono;
    D.gen = ++gens;
    D.what = what;
    if (D.cur > ns.len)
        D.cur = ns.len;
    if (D.anc > ns.len)
        D.anc = ns.len;
}

/* ---------- small text helpers ---------- */
typedef struct {
    char b[200];
    int n;
} tx;
static void ts(tx *t, const char *s) {
    while (*s && t->n < 199)
        t->b[t->n++] = *s++;
    t->b[t->n] = 0;
}
static void tn(tx *t, long v) {
    char d[24];
    int k = 0;
    if (v < 0) {
        ts(t, "-");
        v = -v;
    }
    do
        d[k++] = (char)('0' + v % 10);
    while ((v /= 10) && k < 23);
    while (k)
        if (t->n < 199)
            t->b[t->n++] = d[--k];
    t->b[t->n] = 0;
}
static void tn2(tx *t, long v, int w) { /* with leading zeros */
    long m = 1;
    for (int i = 1; i < w; i++)
        m *= 10;
    for (; m > 1 && v < m; m /= 10)
        ts(t, "0");
    tn(t, v);
}
/* m:ss.mmm (or h:mm:ss.mmm); dec digits after the point: 0 to 3 */
static void tt(tx *t, long fr, int rate, int dec) {
    if (rate <= 0)
        rate = 48000;
    long ms = (long)((long long)fr * 1000 / rate), s = ms / 1000;
    if (s >= 3600) {
        tn(t, s / 3600);
        ts(t, ":");
        tn2(t, s / 60 % 60, 2);
    } else
        tn(t, s / 60);
    ts(t, ":");
    tn2(t, s % 60, 2);
    if (dec > 0) {
        ts(t, ".");
        long f = ms % 1000;
        if (dec == 1)
            tn(t, f / 100);
        else if (dec == 2)
            tn2(t, f / 10, 2);
        else
            tn2(t, f, 3);
    }
}
static void scpy(char *d, const char *s, int n) {
    int i = 0;
    for (; s[i] && i < n - 1; i++)
        d[i] = s[i];
    d[i] = 0;
}
static int ends(const char *s, const char *e) {
    int a = (int)strlen(s), b = (int)strlen(e);
    if (a < b)
        return 0;
    for (int i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != e[i])
            return 0;
    }
    return 1;
}
/* a number like -6, +3.5 or 3,5; returns the characters used, 0 without a number */
static int num(const char *s, double *v) {
    const char *p = s;
    while (*p == ' ')
        p++;
    int neg = 0, any = 0;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    double r = 0, f = 0.1;
    for (; *p >= '0' && *p <= '9'; p++, any = 1)
        r = r * 10 + (*p - '0');
    if (*p == '.' || *p == ',')
        for (p++; *p >= '0' && *p <= '9'; p++, any = 1, f /= 10)
            r += (*p - '0') * f;
    if (!any)
        return 0;
    *v = neg ? -r : r;
    return (int)(p - s);
}

/* ---------- messages and progress ---------- */
static char msg[120];
static long msg_t;
static void say(const char *m) {
    scpy(msg, m, sizeof msg);
    msg_t = vx_ms();
}
static void say2(const char *a, const char *b) {
    tx t = {{0}, 0};
    ts(&t, a);
    ts(&t, b);
    say(t.b);
}
static long busy_t;
/* a bar in the bottom line while something takes long */
static void busy(const char *what, long done, long total) {
    long now = vx_ms();
    if (now - busy_t < 100)
        return;
    busy_t = now;
    int cols = vx_cols(), y = vx_rows() - 1;
    if (y < 0)
        return;
    uint8_t a = VX_ATTR(VC_BLACK, VC_HOT), b = VX_ATTR(VC_SEL, VC_GLOW), c = VX_ATTR(VC_RAISE, VC_DIM);
    for (int x = 0; x < cols; x++)
        vx_put(x, y, ' ', a);
    vx_text(1, y, what, a);
    int x0 = (int)strlen(what) + 3, w = cols - x0 - 8;
    if (w > 40)
        w = 40;
    if (w > 4 && total > 0) {
        int f = (int)((long long)done * w / total);
        for (int i = 0; i < w; i++)
            vx_put(x0 + i, y, ' ', i < f ? b : c);
        tx t = {{0}, 0};
        tn(&t, (long)((long long)done * 100 / total));
        ts(&t, " %");
        vx_text(x0 + w + 1, y, t.b, a);
    }
    vx_show();
}

/* ---------- reading files in big parts ---------- */
#define FINBUF (512 * 1024)
typedef struct {
    const char *path;
    long size, pos, b0, bn;
    uint8_t *buf;
} fin;
static int fin_fill(fin *f, long at) {
    f->b0 = at;
    f->bn = 0;
    while (f->bn < FINBUF && at + f->bn < f->size) {
        long k = vx_read_at(f->path, f->buf + f->bn, (unsigned long)(at + f->bn), (size_t)(FINBUF - f->bn));
        if (k <= 0)
            break;
        f->bn += k;
    }
    return f->bn > 0;
}
static long fin_read(fin *f, void *d, long n) {
    long got = 0;
    while (got < n && f->pos < f->size) {
        if (f->pos < f->b0 || f->pos >= f->b0 + f->bn)
            if (!fin_fill(f, f->pos))
                break;
        long k = f->b0 + f->bn - f->pos;
        if (k > n - got)
            k = n - got;
        memcpy((uint8_t *)d + got, f->buf + (f->pos - f->b0), (size_t)k);
        got += k;
        f->pos += k;
    }
    return got;
}
/* at least need bytes from pos on in the buffer (fewer at the end of the file) */
static const uint8_t *fin_peek(fin *f, long need, long *avail) {
    if (f->pos < f->b0 || f->pos + need > f->b0 + f->bn)
        if (f->pos < f->b0 || f->b0 + f->bn < f->size)
            fin_fill(f, f->pos);
    long a = f->b0 + f->bn - f->pos;
    *avail = a > 0 && f->pos >= f->b0 ? a : 0;
    return f->buf + (f->pos - f->b0);
}

/* ---------- decoding into blocks ---------- */
static short stb[2 * 4096];
static int emit(wr *w, const short *pcm, long n, int ch) {
    while (n > 0) {
        int k = n > 4096 ? 4096 : (int)n;
        for (int i = 0; i < k; i++) {
            stb[2 * i] = pcm[i * ch];
            stb[2 * i + 1] = pcm[i * ch + (ch > 1)];
        }
        if (!wr_put(w, stb, k))
            return 0;
        pcm += (long)k * ch;
        n -= k;
    }
    return 1;
}
static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int load_wav(fin *f, wr *w, int *rate, int *mono) {
    uint8_t h[40];
    if (fin_read(f, h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return 0;
    int tag = 0, ch = 0, bits = 0, sr = 0, align = 0;
    long data = -1, dlen = 0;
    while (f->pos + 8 <= f->size) {
        if (fin_read(f, h, 8) != 8)
            break;
        long sz = le32(h + 4), at = f->pos;
        if (!memcmp(h, "fmt ", 4) && sz >= 16) {
            fin_read(f, h, sz > 40 ? 40 : sz);
            tag = h[0] | h[1] << 8;
            ch = h[2] | h[3] << 8;
            sr = (int)le32(h + 4);
            align = h[12] | h[13] << 8;
            bits = h[14] | h[15] << 8;
            if (tag == 0xFFFE && sz >= 26)
                tag = h[24] | h[25] << 8; /* WAVE_FORMAT_EXTENSIBLE: the sub format */
        } else if (!memcmp(h, "data", 4)) {
            data = at;
            dlen = sz;
            if (dlen > f->size - at || dlen == 0xFFFFFFFFL)
                dlen = f->size - at; /* rescued recordings: the length was not written yet */
            break;
        }
        f->pos = at + sz + (sz & 1);
    }
    if (data < 0 || ch < 1 || ch > 32 || sr < 1000 || sr > 384000)
        return 0;
    if (!((tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (tag == 3 && (bits == 32 || bits == 64))))
        return -1;
    int bpf = ch * (bits / 8);
    if (align < bpf)
        align = bpf;
    *rate = sr;
    *mono = ch == 1;
    exact = tag == 1 && bits == 16 && ch <= 2;
    f->pos = data;
    long frames = dlen / align, done = 0;
    static uint8_t in[4096 * 64];
    int per = (int)(sizeof in / (size_t)align);
    if (per > 4096)
        per = 4096;
    while (done < frames) {
        int k = frames - done > per ? per : (int)(frames - done);
        long got = fin_read(f, in, (long)k * align);
        k = (int)(got / align);
        if (k <= 0)
            break;
        for (int i = 0; i < k; i++)
            for (int c = 0; c < 2; c++) {
                int cc = c < ch ? c : 0;
                const uint8_t *p = in + (long)i * align + cc * (bits / 8);
                int v;
                if (tag == 3 && bits == 32) {
                    union {
                        uint32_t u;
                        float f;
                    } q;
                    q.u = le32(p);
                    float x = q.f * 32768.0f;
                    v = x != x ? 0 : x > 32767 ? 32767 : x < -32768 ? -32768 : (int)x;
                } else if (tag == 3) {
                    union {
                        uint64_t u;
                        double d;
                    } q;
                    q.u = le32(p) | (uint64_t)le32(p + 4) << 32;
                    double x = q.d * 32768.0;
                    v = x != x ? 0 : x > 32767 ? 32767 : x < -32768 ? -32768 : (int)x;
                } else if (bits == 8)
                    v = (p[0] - 128) * 256;
                else if (bits == 16)
                    v = (short)(p[0] | p[1] << 8);
                else if (bits == 24)
                    v = (short)(p[1] | p[2] << 8);
                else
                    v = (short)(p[2] | p[3] << 8);
                stb[2 * i + c] = (short)v;
            }
        if (!wr_put(w, stb, k))
            return 2;
        done += k;
        busy(T("Lade", "Loading"), f->pos, f->size);
    }
    return 1;
}
static int load_mp3(fin *f, wr *w, int *rate, int *mono) {
    static mp3dec_t d;
    static short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint8_t h[10];
    f->pos = 0;
    if (fin_read(f, h, 10) == 10 && !memcmp(h, "ID3", 3)) /* tags, often with a cover picture */
        f->pos = 10 + ((h[6] & 127L) << 21 | (h[7] & 127L) << 14 | (h[8] & 127L) << 7 | (h[9] & 127L)) + (h[5] & 16 ? 10 : 0);
    else
        f->pos = 0;
    mp3dec_init(&d);
    int ok = 0;
    for (;;) {
        long av;
        const uint8_t *p = fin_peek(f, 16384, &av);
        if (av <= 0)
            break;
        mp3dec_frame_info_t in;
        int s = mp3dec_decode_frame(&d, p, (int)(av > 16384 ? 16384 : av), pcm, &in);
        if (!in.frame_bytes) {
            if (av < 16384)
                break;
            f->pos += av > 16384 ? 16384 - 4 : av - 4; /* no frame in there: skip it */
            continue;
        }
        f->pos += in.frame_bytes;
        if (s > 0 && in.channels > 0) {
            if (!ok) {
                *rate = in.hz;
                *mono = in.channels == 1;
            }
            ok = 1;
            if (!emit(w, pcm, s, in.channels))
                return 2;
        }
        busy(T("Lade", "Loading"), f->pos, f->size);
    }
    return ok;
}
static size_t fl_read(void *u, void *b, size_t n) {
    return (size_t)fin_read(u, b, (long)n);
}
static drflac_bool32 fl_seek(void *u, int off, drflac_seek_origin o) {
    fin *f = u;
    long p = o == DRFLAC_SEEK_SET ? off : o == DRFLAC_SEEK_CUR ? f->pos + off : f->size + off;
    if (p < 0 || p > f->size)
        return DRFLAC_FALSE;
    f->pos = p;
    return DRFLAC_TRUE;
}
static drflac_bool32 fl_tell(void *u, drflac_int64 *c) {
    *c = ((fin *)u)->pos;
    return DRFLAC_TRUE;
}
static int load_flac(fin *f, wr *w, int *rate, int *mono) {
    f->pos = 0;
    drflac *fl = drflac_open(fl_read, fl_seek, fl_tell, f, 0);
    if (!fl)
        return 0;
    int ch = fl->channels, r = 1;
    *rate = (int)fl->sampleRate;
    *mono = ch == 1;
    exact = fl->bitsPerSample == 16 && ch <= 2;
    if (ch < 1 || ch > 8 || *rate < 1000 || *rate > 384000) {
        drflac_close(fl);
        return 0;
    }
    static short pcm[1024 * 8];
    drflac_uint64 k;
    while ((k = drflac_read_pcm_frames_s16(fl, 1024, pcm)) > 0) {
        if (!emit(w, pcm, (long)k, ch)) {
            r = 2;
            break;
        }
        busy(T("Lade", "Loading"), f->pos, f->size);
    }
    drflac_close(fl);
    return r;
}
#define MAXOGG (40L << 20)
static int load_ogg(fin *f, wr *w, int *rate, int *mono) {
    if (f->size > MAXOGG)
        return -2;
    uint8_t *b = malloc((size_t)f->size);
    if (!b)
        return -2;
    f->pos = 0;
    long n = fin_read(f, b, f->size);
    int e, r = 0;
    stb_vorbis *v = stb_vorbis_open_memory(b, (int)n, &e, 0);
    if (v) {
        stb_vorbis_info in = stb_vorbis_get_info(v);
        *rate = (int)in.sample_rate;
        *mono = in.channels == 1;
        r = 1;
        int k;
        while ((k = stb_vorbis_get_samples_short_interleaved(v, 2, stb, 2 * 4096)) > 0) {
            if (!wr_put(w, stb, k)) {
                r = 2;
                break;
            }
            busy(T("Lade", "Loading"), (long)stb_vorbis_get_sample_offset(v), (long)stb_vorbis_stream_length_in_samples(v));
        }
        stb_vorbis_close(v);
    }
    free(b);
    return r;
}

/* ---------- the view ---------- */
static int wcols(void) {
    int c = vx_cols();
    return c > 240 ? 240 : c < 1 ? 1 : c;
}
static long pxw(void) { /* width of the waveform in pixels */
    return (long)wcols() * 4;
}
static long fit_spp(void) {
    long w = pxw();
    long s = (D.s.len + w - 1) / w;
    return s < 1 ? 1 : s;
}
static void clamp_view(void) {
    long w = pxw(), mx = D.s.len - (w - 8) * spp;
    if (vstart > mx)
        vstart = mx;
    if (vstart < 0)
        vstart = 0;
}
static void follow(void) { /* the cursor stays in sight */
    long w = pxw();
    if (D.cur < vstart || D.cur >= vstart + w * spp)
        vstart = D.cur - w * spp / 3;
    clamp_view();
}
static void zoom_fit(void) {
    spp = fit_spp();
    vstart = 0;
}
static void zoom(int in) {
    long x = (D.cur - vstart) / spp; /* the cursor keeps its place on the screen */
    if (x < 0 || x >= pxw())
        x = pxw() / 2;
    if (in)
        spp = spp / 2 < 1 ? 1 : spp / 2;
    else {
        long f = fit_spp();
        spp = spp * 2 > f ? f : spp * 2;
    }
    vstart = D.cur - x * spp;
    clamp_view();
}

/* ---------- playing ---------- */
static int playing, pl_done;
static long pl_a, pl_b, pl_sent, pl_p0;
static uint64_t pl_ph, pl_step;
static src_t pl_src;
static short pl_buf[2 * 2048];
static void play_stop(void) {
    if (playing)
        vx_sys(23, 0, 0, 0); /* what is still queued */
    playing = 0;
}
static long play_pos(void) {
    long played = vx_sys(23, 1, 0, 0) - pl_p0;
    if (played > pl_sent)
        played = pl_sent;
    long p = pl_a + (long)(((uint64_t)played * pl_step) >> 32);
    return p > pl_b ? pl_b : p;
}
static void play_feed(void) {
    if (!playing)
        return;
    long played = vx_sys(23, 1, 0, 0) - pl_p0;
    while (!pl_done && pl_sent - played < 9600) {
        long room = vx_sys(22, 0, 0, 0);
        if (room <= 0)
            break;
        long n = 9600 - (pl_sent - played);
        if (n > room)
            n = room;
        if (n > 2048)
            n = 2048;
        int k = 0;
        for (; k < n; k++) {
            long i = (long)(pl_ph >> 32);
            if (i >= pl_b) {
                pl_done = 1;
                break;
            }
            const short *a = src_at(&pl_src, i);
            short l = a[0], r = a[1];
            uint32_t fr = (uint32_t)pl_ph;
            if (fr && i + 1 < pl_b) {
                const short *b = src_at(&pl_src, i + 1);
                l = (short)(l + (((long long)(b[0] - l) * fr) >> 32));
                r = (short)(r + (((long long)(b[1] - r) * fr) >> 32));
            }
            pl_buf[2 * k] = l;
            pl_buf[2 * k + 1] = r;
            pl_ph += pl_step;
        }
        if (k > 0) {
            vx_sys(22, (long)pl_buf, k, 0);
            pl_sent += k;
        }
    }
    if (pl_done && played >= pl_sent)
        playing = 0;
}
static void play(long a, long b) {
    if (b <= a)
        return;
    if (vx_sys(22, 0, 0, 0) < 0) {
        say(T("Keine Tonausgabe gefunden", "No sound output found"));
        return;
    }
    vx_sys(23, 0, 0, 0);
    pl_p0 = vx_sys(23, 1, 0, 0);
    pl_sent = 0;
    pl_a = a;
    pl_b = b;
    pl_ph = (uint64_t)a << 32;
    pl_step = ((uint64_t)D.rate << 32) / 48000;
    src_init(&pl_src, &D.s);
    playing = 1;
    pl_done = 0;
    play_feed();
}

/* ---------- recording ---------- */
static int recording, rec_in, rec_diff, rec_peak;
static long rec_frames, rec_peak_t;
static wr rw;
static vx_rec_info ri;
static void edit_insert(long at, const seq *ins, int rate, int mono, int sel_it, const char *what);
static int convert(const seq *s, long a, long b, uint64_t step, int tomono, seq *out, const char *what);
/* the recording is brought to the rate and channels of the document while it comes in, so
   that a recording that fills the memory needs nothing more at the end */
static int rec_rate, rec_mono;
static uint64_t rs_pos, rs_step; /* 32.32, 0 is the last frame of the part before */
static short rs_prev[2], rs_out[2 * 8192];
static void rec_start(void) {
    play_stop();
    long r = vx_rec_start(&ri, rec_in);
    if (r < 0) {
        say(r == VX_NOTFOUND ? T("Kein Mikrofon gefunden", "No microphone found")
            : r == VX_BUSY   ? T("Ein anderes Programm nimmt gerade auf", "Another program is recording")
                             : T("Das Mikrofon startet nicht", "The microphone does not start"));
        return;
    }
    if (ri.rate < 1000)
        ri.rate = 48000;
    memset(&rw, 0, sizeof rw);
    recording = 1;
    rec_frames = 0;
    rec_diff = 0;
    rec_peak = 0;
    rec_rate = D.s.len ? D.rate : ri.rate;
    rec_mono = D.s.len ? D.mono : 0;
    rs_step = ((uint64_t)ri.rate << 32) / (uint64_t)rec_rate;
    rs_pos = 1ull << 32;
    rs_prev[0] = rs_prev[1] = 0;
}
static int rec_write(const short *in, long n) {
    if (rec_rate == ri.rate && !rec_mono)
        return wr_put(&rw, in, n);
    long k = 0;
    for (;; rs_pos += rs_step) {
        long i = (long)(rs_pos >> 32);
        uint32_t fr = (uint32_t)rs_pos;
        if (i > n || (i == n && fr))
            break; /* needs the next part */
        const short *a = i ? in + 2 * (i - 1) : rs_prev, *b = i < n ? in + 2 * i : a;
        int l = a[0] + (int)(((long long)(b[0] - a[0]) * fr) >> 32), r = a[1] + (int)(((long long)(b[1] - a[1]) * fr) >> 32);
        if (rec_mono)
            l = r = (l + r) >> 1;
        rs_out[2 * k] = (short)l;
        rs_out[2 * k + 1] = (short)r;
        if (++k == 8192) {
            if (!wr_put(&rw, rs_out, k))
                return 0;
            k = 0;
        }
    }
    rs_pos -= (uint64_t)n << 32;
    rs_prev[0] = in[2 * n - 2];
    rs_prev[1] = in[2 * n - 1];
    return !k || wr_put(&rw, rs_out, k);
}
static short rec_buf[2 * 8192];
static void rec_stop(void);
static void rec_pull(void) {
    if (!recording)
        return;
    for (;;) {
        long n = vx_rec_read(rec_buf, 8192);
        if (n < 0) {
            rec_stop();
            say(T("Das Mikrofon ist weg: Aufnahme beendet", "The microphone is gone: recording stopped"));
            return;
        }
        if (n == 0)
            break;
        for (long i = 0; i < n; i++) {
            int l = rec_buf[2 * i], r = rec_buf[2 * i + 1];
            if (l != r)
                rec_diff = 1;
            if (l < 0)
                l = -l;
            if (r < 0)
                r = -r;
            if (l > rec_peak)
                rec_peak = l;
            if (r > rec_peak)
                rec_peak = r;
        }
        if (!rec_write(rec_buf, n)) {
            rec_stop();
            say(T("Speicher voll: Aufnahme beendet", "Memory full: recording stopped"));
            return;
        }
        rec_frames += n;
        if (n < 8192)
            break;
    }
}
static void rec_stop(void) {
    if (!recording)
        return;
    recording = 0;
    long n;
    while ((n = vx_rec_read(rec_buf, 8192)) > 0 && rec_write(rec_buf, n))
        ;
    vx_rec_stop();
    seq s = wr_end(&rw);
    if (!s.len) {
        seq_free(&s);
        return;
    }
    int empty = !D.s.len, rate = rec_rate, mono = empty ? !rec_diff : rec_mono; /* both sides alike: mono */
    edit_insert(has_sel() ? sel_a() : D.cur, &s, rate, mono, 1, T("Aufnahme", "Recording"));
    seq_free(&s);
    if (empty)
        zoom_fit();
}

/* ---------- editing ---------- */
/* a copy of s packed into full blocks, when the list of pieces gets too long */
static int flatten(const seq *s, seq *out) {
    wr w;
    memset(&w, 0, sizeof w);
    rd r;
    rd_seek(&r, s, 0);
    int k;
    while ((k = rd_get(&r, stb, 4096)) > 0) {
        if (!wr_put(&w, stb, k)) {
            wr_abort(&w);
            return 0;
        }
        busy(T("Ordne", "Tidying"), r.f, s->len);
    }
    *out = wr_end(&w);
    if (w.o.fail) {
        seq_free(out);
        return 0;
    }
    return 1;
}
/* D with a..b replaced by ins; 0 without memory */
static int build(long a, long b, const seq *ins, seq *out) {
    for (int round = 0; round < 2; round++) {
        sb o;
        memset(&o, 0, sizeof o);
        if (sb_slice(&o, &D.s, 0, a) && (!ins || sb_slice(&o, ins, 0, ins->len)) && sb_slice(&o, &D.s, b, D.s.len)) {
            *out = o.s;
            return 1;
        }
        int many = o.fail == 2;
        seq_free(&o.s);
        if (!many || round)
            return 0;
        seq f; /* too many pieces: pack the document first */
        if (!flatten(&D.s, &f))
            return 0;
        seq_free(&D.s); /* the same sound, so it stays the same step */
        D.s = f;
    }
    return 0;
}
static void no_mem(void) {
    say(T("Nicht genug Speicher", "Not enough memory"));
}
static int edit_replace(long a, long b, const seq *ins, int rate, int mono, const char *what) {
    seq n;
    if (!build(a, b, ins, &n)) {
        no_mem();
        return 0;
    }
    commit(n, rate, mono, what);
    return 1;
}
static void edit_insert(long at, const seq *ins, int rate, int mono, int sel_it, const char *what) {
    long b = has_sel() && sel_it == 2 ? sel_b() : at;
    seq n;
    if (!build(at, b, ins, &n)) {
        no_mem();
        return;
    }
    commit(n, rate, mono, what);
    D.anc = at;
    D.cur = at + ins->len;
    D.sel = sel_it != 0;
    follow();
}
static int need_sel(void) {
    if (has_sel())
        return 1;
    say(T("Erst etwas auswaehlen (Shift + Pfeile)", "Select something first (Shift + arrows)"));
    return 0;
}
static void do_copy(int cut) {
    if (!need_sel())
        return;
    play_stop();
    long a = sel_a(), b = sel_b();
    sb o;
    memset(&o, 0, sizeof o);
    if (!sb_slice(&o, &D.s, a, b)) {
        seq_free(&o.s);
        no_mem();
        return;
    }
    seq_free(&clip);
    clip = o.s;
    clip_rate = D.rate;
    clip_mono = D.mono;
    if (cut && edit_replace(a, b, 0, D.rate, D.mono, T("Ausschneiden", "Cut"))) {
        D.cur = D.anc = a;
        D.sel = 0;
        follow();
    } else if (!cut)
        say(T("Kopiert", "Copied"));
}
static void do_delete(void) {
    if (!need_sel())
        return;
    play_stop();
    long a = sel_a(), b = sel_b();
    if (!edit_replace(a, b, 0, D.rate, D.mono, T("Loeschen", "Delete")))
        return;
    D.cur = D.anc = a;
    D.sel = 0;
    follow();
}
static void do_paste(void) {
    if (!clip.len) {
        say(T("Die Zwischenablage ist leer", "The clipboard is empty"));
        return;
    }
    play_stop();
    int empty = !D.s.len;
    int rate = empty ? clip_rate : D.rate, mono = empty ? clip_mono : D.mono;
    long at = has_sel() ? sel_a() : D.cur;
    if (clip_rate != rate || (mono && !clip_mono)) { /* from another file */
        seq c;
        if (!convert(&clip, 0, clip.len, ((uint64_t)clip_rate << 32) / (uint64_t)rate, mono, &c, T("Wandle um", "Converting"))) {
            no_mem();
            return;
        }
        edit_insert(at, &c, rate, mono, 2, T("Einfuegen", "Paste"));
        seq_free(&c);
    } else
        edit_insert(at, &clip, rate, mono, 2, T("Einfuegen", "Paste"));
    if (empty)
        zoom_fit();
}
static void do_trim(void) {
    if (!need_sel())
        return;
    play_stop();
    long a = sel_a(), b = sel_b();
    sb o;
    memset(&o, 0, sizeof o);
    if (!sb_slice(&o, &D.s, a, b)) {
        seq_free(&o.s);
        no_mem();
        return;
    }
    commit(o.s, D.rate, D.mono, T("Zuschneiden", "Trim"));
    D.anc = 0;
    D.cur = D.s.len;
    D.sel = 1;
    zoom_fit();
}
static void do_undo(int redo) {
    play_stop();
    if (redo ? !nred : !nund) {
        say(redo ? T("Nichts zum Wiederherstellen", "Nothing to redo") : T("Nichts rueckgaengig zu machen", "Nothing to undo"));
        return;
    }
    const char *w = redo ? red[nred - 1].what : D.what;
    if (redo) {
        und[nund++] = D;
        D = red[--nred];
    } else {
        red[nred++] = D;
        D = und[--nund];
    }
    say2(redo ? T("Wiederhergestellt: ", "Redone: ") : T("Rueckgaengig: ", "Undone: "), w ? w : "");
    if (D.cur > D.s.len)
        D.cur = D.s.len;
    if (D.anc > D.s.len)
        D.anc = D.s.len;
    clamp_view();
    follow();
}

/* ---------- effects ---------- */
enum { FX_NORM, FX_GAIN, FX_FIN, FX_FOUT, FX_SIL, FX_REV, FX_ECHO, FX_SPEED, FX_MONO, NFX };
static const char *fx_name(int i) {
    static const char *const de[NFX] = {"Normalisieren", "Lautstaerke aendern", "Einblenden", "Ausblenden", "Stille", "Rueckwaerts", "Echo", "Geschwindigkeit", "In Mono umwandeln"};
    static const char *const e[NFX] = {"Normalize", "Change volume", "Fade in", "Fade out", "Silence", "Reverse", "Echo", "Speed", "Convert to mono"};
    return en ? e[i] : de[i];
}
static const char fx_key[NFX] = {'N', 'G', 'I', 'U', 'L', 'B', 0, 0, 'M'};
typedef struct {
    int fx;
    float g, decay;
    long pos, tot, clipped;
    float *ring;
    long rn, ri;
} fxc;
static short clip16(float v, fxc *c) {
    if (v > 32767) {
        c->clipped++;
        return 32767;
    }
    if (v < -32768) {
        c->clipped++;
        return -32768;
    }
    return (short)(v < 0 ? v - 0.5f : v + 0.5f);
}
static void fx_chunk(fxc *c, short *f, int n) {
    for (int i = 0; i < n; i++) {
        float l = f[2 * i], r = f[2 * i + 1], g = 1;
        long p = c->pos + i;
        switch (c->fx) {
            case FX_GAIN:
                g = c->g;
                break;
            case FX_FIN:
                g = c->tot > 1 ? (float)p / (float)(c->tot - 1) : 1;
                break;
            case FX_FOUT:
                g = c->tot > 1 ? (float)(c->tot - 1 - p) / (float)(c->tot - 1) : 0;
                break;
            case FX_MONO:
                l = r = (l + r) * 0.5f;
                break;
            case FX_ECHO: {
                float *q = c->ring + 2 * c->ri;
                l += q[0] * c->decay;
                r += q[1] * c->decay;
                q[0] = l;
                q[1] = r;
                if (++c->ri == c->rn)
                    c->ri = 0;
                break;
            }
        }
        f[2 * i] = clip16(l * g, c);
        f[2 * i + 1] = clip16(r * g, c);
    }
}
static int fx_run(long a, long b, fxc *c, seq *out) {
    wr w;
    memset(&w, 0, sizeof w);
    rd r;
    rd_seek(&r, &D.s, a);
    c->pos = 0;
    c->tot = b - a;
    while (c->pos < c->tot) {
        long k = c->tot - c->pos > 4096 ? 4096 : c->tot - c->pos;
        k = rd_get(&r, stb, (int)k);
        if (k <= 0)
            break;
        fx_chunk(c, stb, (int)k);
        c->pos += k;
        if (!wr_put(&w, stb, k)) {
            wr_abort(&w);
            return 0;
        }
        busy(fx_name(c->fx), c->pos, c->tot);
    }
    *out = wr_end(&w);
    return 1;
}
static int reverse(long a, long b, seq *out) {
    wr w;
    memset(&w, 0, sizeof w);
    rd r;
    for (long e = b; e > a;) {
        int k = e - a > 4096 ? 4096 : (int)(e - a);
        e -= k;
        rd_seek(&r, &D.s, e);
        k = rd_get(&r, stb, k);
        for (int i = 0, j = k - 1; i < j; i++, j--) {
            short l = stb[2 * i], q = stb[2 * i + 1];
            stb[2 * i] = stb[2 * j];
            stb[2 * i + 1] = stb[2 * j + 1];
            stb[2 * j] = l;
            stb[2 * j + 1] = q;
        }
        if (!wr_put(&w, stb, k)) {
            wr_abort(&w);
            return 0;
        }
        busy(fx_name(FX_REV), b - e, b - a);
    }
    *out = wr_end(&w);
    return 1;
}
/* resampled by linear interpolation: step source frames (32.32) per new frame */
static int convert(const seq *s, long a, long b, uint64_t step, int tomono, seq *out, const char *what) {
    static src_t q;
    src_init(&q, s);
    wr w;
    memset(&w, 0, sizeof w);
    static short ob[2 * 2048];
    int on = 0;
    uint64_t ph = (uint64_t)a << 32, end = (uint64_t)b << 32;
    if (step < 1)
        step = 1;
    for (; ph < end; ph += step) {
        long i = (long)(ph >> 32);
        uint32_t fr = (uint32_t)ph;
        const short *x = src_at(&q, i);
        int l = x[0], r = x[1];
        if (fr && i + 1 < b) {
            const short *y = src_at(&q, i + 1);
            l += (int)(((long long)(y[0] - l) * fr) >> 32);
            r += (int)(((long long)(y[1] - r) * fr) >> 32);
        }
        if (tomono)
            l = r = (l + r) >> 1;
        ob[2 * on] = (short)l;
        ob[2 * on + 1] = (short)r;
        if (++on == 2048) {
            if (!wr_put(&w, ob, on)) {
                wr_abort(&w);
                return 0;
            }
            on = 0;
            busy(what, i - a, b - a);
        }
    }
    if (on && !wr_put(&w, ob, on)) {
        wr_abort(&w);
        return 0;
    }
    *out = wr_end(&w);
    return 1;
}
static int seq_peak(long a, long b) {
    rd r;
    rd_seek(&r, &D.s, a);
    int pk = 0;
    for (long p = a; p < b;) {
        int k = rd_get(&r, stb, b - p > 4096 ? 4096 : (int)(b - p));
        if (k <= 0)
            break;
        for (int i = 0; i < 2 * k; i++) {
            int v = stb[i] < 0 ? -stb[i] : stb[i];
            if (v > pk)
                pk = v;
        }
        p += k;
        busy(T("Suche den Spitzenwert", "Finding the peak"), p - a, b - a);
    }
    return pk;
}
static void fx_apply(int fx) {
    if (!D.s.len) {
        say(T("Noch kein Ton da: O oeffnet, R nimmt auf", "No sound yet: O opens, R records"));
        return;
    }
    play_stop();
    int whole = !has_sel() || fx == FX_MONO;
    long a = whole ? 0 : sel_a(), b = whole ? D.s.len : sel_b();
    fxc c;
    memset(&c, 0, sizeof c);
    c.fx = fx;
    c.g = 1;
    char in[40];
    double v = 0, v2 = 0;
    seq out = {0, 0, 0};
    int ok = 0, mono = D.mono;
    tx t = {{0}, 0};
    ts(&t, fx_name(fx));
    switch (fx) {
        case FX_NORM: {
            int pk = seq_peak(a, b);
            if (!pk) {
                say(T("Dort ist nur Stille", "There is only silence"));
                return;
            }
            c.fx = FX_GAIN;
            c.g = 0.891f * 32767 / (float)pk; /* to -1 dB */
            ok = fx_run(a, b, &c, &out);
            break;
        }
        case FX_GAIN:
            scpy(in, "-3", sizeof in);
            if (!vx_input(fx_name(fx), T("Um wie viel dB? (z. B. -6 oder +3)", "By how many dB? (for example -6 or +3)"), in, sizeof in))
                return;
            if (!num(in, &v) || v < -60 || v > 60) {
                say(T("Bitte eine Zahl von -60 bis +60", "Please a number from -60 to +60"));
                return;
            }
            c.g = (float)pow(10.0, v / 20);
            ts(&t, " ");
            ts(&t, in);
            ts(&t, " dB");
            ok = fx_run(a, b, &c, &out);
            break;
        case FX_FIN:
        case FX_FOUT:
            ok = fx_run(a, b, &c, &out);
            break;
        case FX_SIL: {
            sb o;
            memset(&o, 0, sizeof o);
            ok = sb_silence(&o, b - a);
            out = o.s;
            break;
        }
        case FX_REV:
            ok = reverse(a, b, &out);
            break;
        case FX_ECHO: {
            scpy(in, "300 40", sizeof in);
            if (!vx_input(fx_name(fx), T("Verzoegerung in ms und Lautstaerke des Echos in %:", "Delay in ms and volume of the echo in %:"), in, sizeof in))
                return;
            int k = num(in, &v);
            if (!k || !num(in + k, &v2) || v < 10 || v > 2000 || v2 < 1 || v2 > 95) {
                say(T("Bitte z. B. 300 40 (10-2000 ms, 1-95 %)", "Please for example 300 40 (10-2000 ms, 1-95 %)"));
                return;
            }
            c.rn = (long)(v * D.rate / 1000);
            if (c.rn < 1)
                c.rn = 1;
            c.decay = (float)(v2 / 100);
            c.ring = vxo_calloc((size_t)c.rn * 2, sizeof(float));
            if (!c.ring) {
                no_mem();
                return;
            }
            ok = fx_run(a, b, &c, &out);
            vxo_free(c.ring);
            break;
        }
        case FX_SPEED: {
            scpy(in, "120", sizeof in);
            if (!vx_input(fx_name(fx), T("Geschwindigkeit in % (50 halb so schnell, 200 doppelt):", "Speed in % (50 half as fast, 200 twice as fast):"), in, sizeof in))
                return;
            if (!num(in, &v) || v < 25 || v > 400) {
                say(T("Bitte eine Zahl von 25 bis 400", "Please a number from 25 to 400"));
                return;
            }
            ts(&t, " ");
            ts(&t, in);
            ts(&t, " %");
            ok = convert(&D.s, a, b, (uint64_t)(v / 100 * 4294967296.0), 0, &out, fx_name(fx));
            break;
        }
        case FX_MONO:
            if (D.mono) {
                say(T("Der Ton ist schon mono", "The sound is mono already"));
                return;
            }
            ok = fx_run(a, b, &c, &out);
            mono = 1;
            break;
    }
    if (!ok || (!out.len && b > a)) {
        seq_free(&out);
        no_mem();
        return;
    }
    static char what[NFX][40]; /* the name stays for the undo history */
    scpy(what[fx], t.b, sizeof what[fx]);
    long cur = D.cur, anc = D.anc, olen = out.len;
    int sel = D.sel, done = edit_replace(a, b, &out, D.rate, mono, what[fx]);
    seq_free(&out);
    if (!done)
        return; /* no memory: unchanged */
    if (!whole) {
        D.anc = a;
        D.cur = a + olen;
        D.sel = 1;
    } else {
        D.cur = cur > D.s.len ? D.s.len : cur;
        D.anc = anc > D.s.len ? D.s.len : anc;
        D.sel = sel;
    }
    if (c.clipped) {
        tx m = {{0}, 0};
        ts(&m, t.b);
        ts(&m, T(" - Achtung: uebersteuert an ", " - careful: clipped at "));
        tn(&m, c.clipped);
        ts(&m, T(" Stellen", " points"));
        say(m.b);
    } else
        say(t.b);
    follow();
}

/* ---------- opening and saving ---------- */
static void set_name(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/')
            b = q + 1;
    scpy(fname, b, sizeof fname);
}
static uint8_t *fbuf;
/* the user chooses a sound file; returns its kind (1 WAV, 2 FLAC, 3 OGG, 4 MP3) or 0 */
static int pick_sound(const char *title, char *p, int n, fin *f) {
    p[0] = 0;
    if (!vx_choose_file(title, p, (size_t)n))
        return 0;
    vx_stat_t st;
    if (vx_stat(p, &st) < 0 || st.dir) {
        say(T("Datei nicht gefunden", "File not found"));
        return 0;
    }
    if (!fbuf && !(fbuf = vxo_malloc(FINBUF))) {
        no_mem();
        return 0;
    }
    fin z = {p, (long)st.size, 0, 0, 0, fbuf};
    *f = z;
    uint8_t h[12] = {0};
    fin_read(f, h, 12);
    f->pos = 0;
    int kind = !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4) ? 1
               : !memcmp(h, "fLaC", 4)                            ? 2
               : !memcmp(h, "OggS", 4)                            ? 3
               : !memcmp(h, "ID3", 3) || (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) || ends(p, ".mp3") ? 4
                                                                                                       : 0;
    if (!kind)
        say(T("Unbekanntes Format: bitte WAV, MP3, OGG oder FLAC", "Unknown format: please WAV, MP3, OGG or FLAC"));
    return kind;
}
/* decodes into new blocks: 1 all, 2 only the beginning (memory full), 0 or less an error */
static int decode(fin *f, int kind, seq *out, int *rate, int *mono) {
    wr w;
    memset(&w, 0, sizeof w);
    *rate = 44100;
    *mono = 0;
    busy_t = 0;
    exact = 0;
    int r = kind == 1 ? load_wav(f, &w, rate, mono) : kind == 2 ? load_flac(f, &w, rate, mono) : kind == 3 ? load_ogg(f, &w, rate, mono) : load_mp3(f, &w, rate, mono);
    *out = wr_end(&w);
    if (r > 0 && w.o.fail)
        r = 2;
    if (r <= 0 || !out->len) {
        seq_free(out);
        say(r == -1   ? T("Diese WAV-Art kennt Vio-Audio nicht", "Vio-Audio does not know this kind of WAV")
            : r == -2 ? T("Die OGG-Datei ist zu gross (bis 40 MB)", "The OGG file is too big (up to 40 MB)")
                      : T("Datei defekt oder Format unbekannt", "File damaged or format unknown"));
        return r <= 0 ? r : 0;
    }
    return r;
}
static void open_file(void) {
    if (recording)
        return;
    play_stop();
    if (modified() && D.s.len &&
        !vx_ask(T("Oeffnen", "Open"), T("Die Aenderungen sind nicht gespeichert. Trotzdem eine andere Datei oeffnen?",
                                         "The changes are not saved. Open another file anyway?")))
        return;
    char p[256];
    fin f;
    int kind = pick_sound(T("Ton oeffnen (WAV, MP3, OGG, FLAC)", "Open sound (WAV, MP3, OGG, FLAC)"), p, sizeof p, &f);
    if (!kind)
        return;
    /* the old sound goes now, so that the new one has all the memory */
    history_clear();
    seq_free(&D.s);
    memset(&D, 0, sizeof D);
    D.rate = 48000;
    D.gen = ++gens;
    saved_gen = D.gen;
    path[0] = 0;
    scpy(fname, T("Unbenannt", "Untitled"), sizeof fname);
    seq s;
    int rate, mono, r = decode(&f, kind, &s, &rate, &mono);
    if (r <= 0)
        return;
    D.s = s;
    D.rate = rate;
    D.mono = mono;
    set_name(p);
    if (r == 2) { /* only the beginning: it must not overwrite the file */
        D.gen = ++gens;
        say(T("Speicher voll: nur der Anfang wurde geladen", "Memory full: only the beginning was loaded"));
    } else {
        scpy(path, p, sizeof path);
        say2(T("Geoeffnet: ", "Opened: "), fname);
    }
    zoom_fit();
}
/* frames a.. of the document and all of s added together, from at on */
static int mix_run(long at, const seq *s, int mono, seq *out, long *clipped) {
    static short mb[2 * 4096];
    wr w;
    memset(&w, 0, sizeof w);
    rd ra, rb;
    rd_seek(&ra, &D.s, at);
    rd_seek(&rb, s, 0);
    fxc c;
    memset(&c, 0, sizeof c);
    for (long p = 0; p < s->len;) {
        int k = s->len - p > 4096 ? 4096 : (int)(s->len - p);
        k = rd_get(&rb, stb, k);
        if (k <= 0)
            break;
        memset(mb, 0, sizeof mb);
        rd_get(&ra, mb, k); /* after the end of the document: silence */
        for (int i = 0; i < 2 * k; i += 2) {
            float l = (float)stb[i] + mb[i], r = (float)stb[i + 1] + mb[i + 1];
            if (mono)
                l = r = (l + r) * 0.5f;
            stb[i] = clip16(l, &c);
            stb[i + 1] = clip16(r, &c);
        }
        if (!wr_put(&w, stb, k)) {
            wr_abort(&w);
            return 0;
        }
        p += k;
        busy(T("Mische", "Mixing"), p, s->len);
    }
    *out = wr_end(&w);
    *clipped = c.clipped;
    return 1;
}
/* another file into the document at the cursor: inserted, or mixed in over what is there */
static void insert_file(int mix) {
    if (recording)
        return;
    play_stop();
    char p[256];
    fin f;
    int kind = pick_sound(mix ? T("Datei dazumischen (ab dem Cursor)", "Mix in a file (from the cursor)") : T("Datei einfuegen (am Cursor)", "Insert a file (at the cursor)"), p, sizeof p, &f);
    if (!kind)
        return;
    int ex = exact, rate, mono;
    seq s;
    int r = decode(&f, kind, &s, &rate, &mono);
    exact = ex; /* that belongs to the open file */
    if (r <= 0)
        return;
    if (r == 2) {
        seq_free(&s);
        say(T("Speicher voll: die Datei passt nicht mehr hinein", "Memory full: the file does not fit in any more"));
        return;
    }
    int empty = !D.s.len, drate = empty ? rate : D.rate, dmono = empty ? mono : D.mono;
    if (rate != drate || (dmono && !mono)) { /* to the rate and channels of the document */
        seq c;
        int ok = convert(&s, 0, s.len, ((uint64_t)rate << 32) / (uint64_t)drate, dmono, &c, T("Wandle um", "Converting"));
        seq_free(&s);
        if (!ok) {
            no_mem();
            return;
        }
        s = c;
    }
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/')
            b = q + 1;
    long at = has_sel() ? sel_a() : D.cur;
    if (!mix || empty) {
        edit_insert(at, &s, drate, dmono, !empty, T("Datei einfuegen", "Insert file"));
        if (empty)
            D.cur = D.anc = 0;
        say2(T("Eingefuegt: ", "Inserted: "), b);
    } else {
        seq m;
        long clipped = 0, end = at + s.len;
        if (!mix_run(at, &s, dmono, &m, &clipped)) {
            seq_free(&s);
            no_mem();
            return;
        }
        if (edit_replace(at, end < D.s.len ? end : D.s.len, &m, drate, dmono, T("Datei dazumischen", "Mix in file"))) {
            D.anc = at;
            D.cur = end;
            D.sel = 1;
            follow();
            tx t = {{0}, 0};
            ts(&t, T("Dazugemischt: ", "Mixed in: "));
            ts(&t, b);
            if (clipped) {
                ts(&t, T(" - uebersteuert an ", " - clipped at "));
                tn(&t, clipped);
                ts(&t, T(" Stellen (vorher leiser machen: G)", " points (make it quieter first: G)"));
            }
            say(t.b);
        }
        seq_free(&m);
    }
    seq_free(&s);
    if (empty)
        zoom_fit();
}
/* writing in parts: the first with vx_write, the others with vx_append */
static uint8_t *obuf;
#define OBUF (2L << 20)
static long o_n, o_tot;
static int o_first, o_err;
static void o_flush(const char *p) {
    if (!o_n || o_err)
        return;
    for (long d = 0; d < o_n && !o_err;) {
        long k = o_n - d > (1L << 20) ? 1L << 20 : o_n - d;
        long r = o_first ? vx_write(p, obuf + d, (size_t)k) : vx_append(p, obuf + d, (size_t)k);
        if (r < 0)
            o_err = (int)r;
        o_first = 0;
        d += k;
    }
    o_tot += o_n;
    o_n = 0;
}
static int save_as(const char *p) {
    int kind = ends(p, ".flac") ? 2 : ends(p, ".ogg") ? 3 : 1;
    if (!obuf && !(obuf = vxo_malloc(OBUF))) {
        no_mem();
        return 0;
    }
    o_n = o_tot = 0;
    o_first = 1;
    o_err = 0;
    busy_t = 0;
    long len = D.s.len;
    rd r;
    rd_seek(&r, &D.s, 0);
    static xo_enc oe;
    const char *what = T("Speichere", "Saving");
    char title[64];
    scpy(title, "TITLE=", sizeof title);
    scpy(title + 6, fname, sizeof title - 6);
    for (char *q = title + 6; *q; q++)
        if (*q == '.')
            *q = 0;
    const char *tags[2] = {title, "ENCODER=Vio-Audio"};
    if (kind == 1) {
        int ch = D.mono ? 1 : 2;
        uint32_t data = (uint32_t)(len * ch * 2);
        uint8_t *h = obuf;
        memcpy(h, "RIFF", 4);
        xf_le32(h + 4, 36 + data);
        memcpy(h + 8, "WAVEfmt ", 8);
        xf_le32(h + 16, 16);
        h[20] = 1;
        h[21] = 0;
        h[22] = (uint8_t)ch;
        h[23] = 0;
        xf_le32(h + 24, (uint32_t)D.rate);
        xf_le32(h + 28, (uint32_t)(D.rate * ch * 2));
        h[32] = (uint8_t)(ch * 2);
        h[33] = 0;
        h[34] = 16;
        h[35] = 0;
        memcpy(h + 36, "data", 4);
        xf_le32(h + 40, data);
        o_n = 44;
        int k;
        while ((k = rd_get(&r, stb, 4096)) > 0 && !o_err) {
            for (int i = 0; i < k * ch; i++) {
                int v = ch == 1 ? (stb[2 * i] + stb[2 * i + 1]) >> 1 : stb[i];
                obuf[o_n++] = (uint8_t)v;
                obuf[o_n++] = (uint8_t)(v >> 8);
            }
            if (o_n > OBUF - 4096 * 4 - 64)
                o_flush(p);
            busy(what, r.f, len);
        }
    } else if (kind == 2) {
        xf_rate = (uint32_t)D.rate;
        o_n = xf_head(obuf, (uint64_t)len, tags, 2);
        uint32_t fn = 0;
        int k;
        while ((k = rd_get(&r, stb, XF_BLOCK)) > 0 && !o_err) {
            o_n += xf_frame(obuf + o_n, stb, k, fn++);
            if (o_n > OBUF - XF_FRAME - 64)
                o_flush(p);
            busy(what, r.f, len);
        }
    } else {
        xo_rate = D.rate;
        long h = xo_begin(&oe, (uint32_t)vx_ms() | 1, tags, 2, obuf);
        if (h < 0) {
            say(T("Diese Abtastrate kann OGG nicht", "OGG cannot do this sample rate"));
            return 0;
        }
        o_n = h;
        int k;
        while ((k = rd_get(&r, stb, 1024)) > 0 && !o_err) {
            o_n += xo_feed(&oe, stb, k, obuf + o_n);
            if (o_n > OBUF - (256L << 10))
                o_flush(p);
            busy(what, r.f, len);
        }
        o_n += xo_feed(&oe, stb, 0, obuf + o_n);
        xo_end(&oe);
    }
    o_flush(p);
    if (o_err) {
        say(o_err == VX_FULL     ? T("Kein Platz mehr auf dem Laufwerk", "No room left on the drive")
            : o_err == VX_DENIED ? T("Dort darf Vio-Audio nicht speichern", "Vio-Audio may not save there")
                                 : T("Speichern fehlgeschlagen", "Saving failed"));
        return 0;
    }
    scpy(path, p, sizeof path);
    set_name(p);
    saved_gen = D.gen;
    exact = kind != 3;
    tx t = {{0}, 0};
    ts(&t, T("Gespeichert: ", "Saved: "));
    ts(&t, fname);
    ts(&t, " (");
    tn(&t, (o_tot + 1023) / 1024);
    ts(&t, " KB)");
    say(t.b);
    return 1;
}
static int allowed_dir(const char *p) {
    return !strncmp(p, "/home/", 6) || !strncmp(p, "/downloads/", 11) || !strncmp(p, "/drives/", 8);
}
static int save_dialog(void) {
    char p[256];
    if (path[0] && allowed_dir(path))
        scpy(p, path, sizeof p);
    else {
        scpy(p, "/home/", sizeof p);
        int n = 6;
        for (const char *q = fname; *q && n < 200; q++)
            p[n++] = *q;
        p[n] = 0;
    }
    /* the ending becomes .wav when it is no format to save */
    if (!ends(p, ".wav") && !ends(p, ".flac") && !ends(p, ".ogg")) {
        int n = (int)strlen(p), d = n;
        for (int i = n - 1; i > 0 && p[i] != '/'; i--)
            if (p[i] == '.') {
                d = i;
                break;
            }
        p[d] = 0;
        if (d + 5 < (int)sizeof p)
            memcpy(p + d, ".wav", 5);
    }
    for (;;) {
        if (!vx_input(T("Speichern unter", "Save as"), T("Name mit .wav, .flac oder .ogg am Ende:", "Name ending in .wav, .flac or .ogg:"), p, sizeof p))
            return 0;
        if (!ends(p, ".wav") && !ends(p, ".flac") && !ends(p, ".ogg")) {
            int n = (int)strlen(p);
            if (n + 5 < (int)sizeof p)
                memcpy(p + n, ".wav", 5);
        }
        if (!allowed_dir(p)) {
            vx_message(T("Speichern unter", "Save as"), T("Bitte in /home, /downloads oder auf einem USB-Laufwerk in /drives speichern.",
                                                          "Please save in /home, /downloads or on a USB drive in /drives."));
            continue;
        }
        vx_stat_t st;
        if (vx_stat(p, &st) >= 0) {
            if (st.dir) {
                vx_message(T("Speichern unter", "Save as"), T("Das ist ein Ordner.", "That is a folder."));
                continue;
            }
            if (strcmp(p, path) && !vx_ask(T("Speichern unter", "Save as"), T("Die Datei gibt es schon. Ersetzen?", "The file exists. Replace it?")))
                continue;
        }
        return save_as(p);
    }
}
static void save(int ask) {
    if (recording)
        return;
    if (!D.s.len) {
        say(T("Noch kein Ton zum Speichern", "No sound to save yet"));
        return;
    }
    play_stop();
    /* 16-bit WAV and FLAC are saved again in place, everything else gets a new name: saving
       24 bit or MP3 in place would lose something */
    if (!ask && path[0] && exact && (ends(path, ".wav") || ends(path, ".flac")))
        save_as(path);
    else
        save_dialog();
}

/* ---------- drawing ---------- */
static uint8_t *img;
static short *mm; /* per pixel: min/max left, min/max right */
static void range_mm(const blk *b, int o, int k, short *m) {
    int e = o + k;
    while (o < e) {
        if (!(o % GRP) && o + GRP <= e) {
            const short *s = b->sum[o / GRP];
            if (s[0] < m[0])
                m[0] = s[0];
            if (s[1] > m[1])
                m[1] = s[1];
            if (s[2] < m[2])
                m[2] = s[2];
            if (s[3] > m[3])
                m[3] = s[3];
            o += GRP;
        } else {
            short l = b->s[2 * o], r = b->s[2 * o + 1];
            if (l < m[0])
                m[0] = l;
            if (l > m[1])
                m[1] = l;
            if (r < m[2])
                m[2] = r;
            if (r > m[3])
                m[3] = r;
            o++;
        }
    }
}
static void wave_scan(int npix) {
    for (int x = 0; x < npix; x++) {
        mm[4 * x] = mm[4 * x + 2] = 32767;
        mm[4 * x + 1] = mm[4 * x + 3] = -32768;
    }
    long v0 = vstart, end = D.s.len < v0 + spp * npix ? D.s.len : v0 + spp * npix;
    const seq *s = &D.s;
    long ps = 0;
    int i = 0;
    while (i < s->np && ps + s->p[i].n <= v0)
        ps += s->p[i++].n;
    long f = v0;
    for (; f < end && i < s->np; ps += s->p[i++].n) {
        const piece *p = &s->p[i];
        long pe = ps + p->n, stop0 = pe < end ? pe : end;
        while (f < stop0) {
            long x = (f - v0) / spp, xe = v0 + (x + 1) * spp, stop = stop0 < xe ? stop0 : xe;
            range_mm(p->b, p->off + (int)(f - ps), (int)(stop - f), mm + 4 * x);
            f = stop;
        }
    }
}
#define PX(r, g, b) ((uint8_t)(16 + 36 * (r) + 6 * (g) + (b)))
static void draw_wave(int cols, int y0, int rows, long ph) {
    int pw = cols * 4, h = rows * 8;
    wave_scan(pw);
    long a = sel_a(), b = sel_b(), len = D.s.len;
    int sel = has_sel();
    long cx = !sel && D.cur >= vstart ? (D.cur - vstart) / spp : -1;
    long px = ph >= 0 && ph >= vstart ? (ph - vstart) / spp : -1;
    int lanes = D.mono ? 1 : 2, lh = lanes == 1 ? h : (h - 1) / 2;
    for (int x = 0; x < pw; x++) {
        long f0 = vstart + x * spp, f1 = f0 + spp;
        int in = f0 < len, s = sel && f1 > a && f0 < b;
        uint8_t bg = !in ? VC_PANEL : s ? VC_SEL : VC_BLACK, fg = s ? VC_GLOW : VC_VIOLET;
        uint8_t *col = img + x;
        for (int y = 0; y < h; y++)
            col[y * pw] = bg;
        if (lanes == 2)
            col[lh * pw] = VC_RAISE;
        for (int ln = 0; ln < lanes; ln++) {
            int top = ln ? lh + 1 : 0, mid = top + lh / 2;
            if (in)
                col[mid * pw] = s ? VC_MID : VC_RAISE;
            if (!in || mm[4 * x + 2 * ln] > mm[4 * x + 2 * ln + 1])
                continue;
            int lo = mm[4 * x + 2 * ln], hi = mm[4 * x + 2 * ln + 1];
            int y1 = mid - (int)((long)hi * (lh / 2) / 32768), y2 = mid - (int)((long)lo * (lh / 2) / 32768);
            if (y1 < top)
                y1 = top;
            if (y2 > top + lh - 1)
                y2 = top + lh - 1;
            for (int y = y1; y <= y2; y++)
                col[y * pw] = fg;
            if (hi >= 32767)
                col[top * pw] = VC_HOT; /* clipped */
            if (lo <= -32768)
                col[(top + lh - 1) * pw] = VC_HOT;
        }
        if (x == cx)
            for (int y = 0; y < h; y++)
                if ((y & 3) != 3)
                    col[y * pw] = VC_HOT;
        if (x == px)
            for (int y = 0; y < h; y++)
                col[y * pw] = VC_GLOW;
    }
    vx_image(img, pw, h);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            vx_pixcell(x, y0 + y, x, y);
}
static void ruler(int cols, int y) {
    uint8_t a = VX_ATTR(VC_PANEL, VC_DIM), b = VX_ATTR(VC_PANEL, VC_SOFT);
    for (int x = 0; x < cols; x++)
        vx_put(x, y, ' ', a);
    if (!D.s.len)
        return;
    static const long st[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000, 1800000, 3600000};
    int ns = (int)(sizeof st / sizeof st[0]), i = 0;
    double ms_px = (double)spp * 1000 / D.rate;
    while (i < ns - 1 && st[i] / ms_px < 56)
        i++;
    long step = st[i];
    long t0 = (long)((double)vstart * 1000 / D.rate);
    long t = (t0 + step - 1) / step * step;
    int last = -1;
    for (;; t += step) {
        long fr = (long)((double)t * D.rate / 1000);
        long x = (fr - vstart) / spp / 4;
        if (x >= cols || fr > D.s.len)
            break;
        if (x <= last)
            continue;
        tx s = {{0}, 0};
        ts(&s, "|");
        tt(&s, fr, D.rate, step < 10 ? 3 : step < 100 ? 2 : step < 1000 ? 1 : 0);
        if (x + s.n > cols)
            break;
        vx_text((int)x, y, s.b, b);
        last = (int)x + s.n;
    }
}
static void fill(int y, int cols, uint8_t a) {
    for (int x = 0; x < cols; x++)
        vx_put(x, y, ' ', a);
}
static void rtext(int y, int cols, const char *s, uint8_t a) {
    int n = (int)strlen(s);
    if (n < cols)
        vx_text(cols - n - 1, y, s, a);
}

/* ---------- Info, licence, keys and the effects list ---------- */
static int overlay, lic_top, fx_sel, ed_col; /* 1 Info, 2 licence, 3 keys, 4 edit and effects */
enum { E_CUT, E_COPY, E_PASTE, E_DEL, E_TRIM, E_UNDO, E_REDO, E_INS, E_MIX, NED };
static int ed_sel;
static const char *ed_name(int i) {
    static const char *const de[NED] = {"Ausschneiden", "Kopieren", "Einfuegen", "Loeschen", "Auf Auswahl kuerzen", "Rueckgaengig", "Wiederherstellen", "Datei einfuegen", "Datei dazumischen"};
    static const char *const e[NED] = {"Cut", "Copy", "Paste", "Delete", "Trim to selection", "Undo", "Redo", "Insert file", "Mix in file"};
    return en ? e[i] : de[i];
}
static const char *ed_key(int i) {
    static const char *const k[NED] = {"X", "C", "V", "Entf", "T", "Z", "Y", "D", "K"};
    return i == E_DEL && en ? "Del" : k[i];
}
static const char *const lic[] = {
    "BSD 3-Clause License",
    "",
    "Copyright (c) 2026, Merecete",
    "",
    "Redistribution and use in source and binary forms, with or without",
    "modification, are permitted provided that the following conditions",
    "are met:",
    "",
    "1. Redistributions of source code must retain the above copyright",
    "   notice, this list of conditions and the following disclaimer.",
    "",
    "2. Redistributions in binary form must reproduce the above copyright",
    "   notice, this list of conditions and the following disclaimer in",
    "   the documentation and/or other materials provided with the",
    "   distribution.",
    "",
    "3. Neither the name of the copyright holder nor the names of its",
    "   contributors may be used to endorse or promote products derived",
    "   from this software without specific prior written permission.",
    "",
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS",
    "\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT",
    "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR",
    "A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT",
    "HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,",
    "INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,",
    "BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS",
    "OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED",
    "AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT",
    "LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY",
    "WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE",
    "POSSIBILITY OF SUCH DAMAGE.",
    "",
    "Vio-Audio contains these parts of other authors:",
    "",
    "libvorbis: Copyright (c) 2002-2020 Xiph.org Foundation",
    "libogg: Copyright (c) 2002, Xiph.org Foundation",
    "  both under the BSD 3-Clause License, with the same conditions",
    "  and disclaimer as above; in condition 3 the name is the",
    "  Xiph.org Foundation",
    "stb_vorbis: Sean Barrett, public domain (or MIT)",
    "minimp3: lieff, CC0 1.0 (public domain)",
    "dr_flac: David Reid, public domain (or MIT-0)",
    0};
#define NLIC 43
static int lic_max(int rows) {
    int h = rows - 4 < NLIC ? rows - 4 : NLIC;
    return NLIC - h > 0 ? NLIC - h : 0;
}
static void box_at(int x, int y, int w, int h, const char *title) {
    uint8_t bg = VX_ATTR(VC_PANEL, VC_LAVENDER), bar = VX_ATTR(VC_VIOLET, VC_GLOW);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            vx_put(x + i, y + j, ' ', j ? bg : bar);
    vx_text(x + 2, y, title, bar);
}
static void clipped_text(int x, int y, const char *s, int max, uint8_t a) {
    char l[100];
    int k = 0;
    for (; s[k] && k < max && k < 99; k++)
        l[k] = s[k];
    l[k] = 0;
    vx_text(x, y, l, a);
}
static const char *const keys_de[] = {
    "@Bewegen und Auswaehlen", "Links/Rechts  Cursor, Strg: schnell", "Bild auf/ab   eine Seite weiter",
    "Pos1/Ende     Anfang/Ende", "Shift + diese Auswaehlen", "A             Alles auswaehlen",
    "Hoch/Runter   Zoom rein/raus", "F             Alles zeigen", "@Abspielen und Aufnehmen",
    "Leertaste     Abspielen/Stopp", "Enter         Auswahl oder alles", "R             Aufnahme an/aus",
    "Tab           Mikrofon wechseln", "@Datei", "O             Oeffnen", "S / W         Speichern / unter",
    "D             Datei am Cursor einfuegen", "K             Datei dazumischen",
    "@Bearbeiten", "X / C / V     Ausschn./Kopie/Einf.", "Entf          Loeschen", "T             Auf Auswahl kuerzen",
    "Z / Y         Rueckgaengig/Wiederh.", "@Effekte (ohne Auswahl: alles)", "E             Liste: Bearbeiten, Effekte",
    "N Normalisieren  G Lautstaerke", "I Einblenden     U Ausblenden", "L Stille         B Rueckwaerts",
    "M In Mono umwandeln", 0};
static const char *const keys_en[] = {
    "@Move and select", "Left/Right    Cursor, Ctrl: fast", "PgUp/PgDn     One page on",
    "Home/End      Start/end", "Shift + these Select", "A             Select all",
    "Up/Down       Zoom in/out", "F             Show all", "@Play and record",
    "Space         Play/stop", "Enter         Selection or all", "R             Record on/off",
    "Tab           Switch microphone", "@File", "O             Open", "S / W         Save / save as",
    "D             Insert a file at cursor", "K             Mix in a file",
    "@Edit", "X / C / V     Cut / copy / paste", "Del           Delete", "T             Trim to selection",
    "Z / Y         Undo / redo", "@Effects (no selection: all)", "E             List: edit and effects",
    "N Normalize      G Volume", "I Fade in        U Fade out", "L Silence        B Reverse",
    "M Convert to mono", 0};
static void draw_overlay(int cols, int rows) {
    uint8_t bg = VX_ATTR(VC_PANEL, VC_LAVENDER), hot = VX_ATTR(VC_PANEL, VC_HOT),
            dim = VX_ATTR(VC_PANEL, VC_SOFT), btn = VX_ATTR(VC_SEL, VC_GLOW);
    if (overlay == 2) {
        int w = cols - 4 < 74 ? cols - 4 : 74, h = rows - 2 < NLIC + 2 ? rows - 2 : NLIC + 2;
        int x = (cols - w) / 2, y = (rows - h) / 2;
        box_at(x, y, w, h,
               lic_max(rows) ? T("Lizenz (Hoch/Runter, andere Taste: zurueck)", "Licence (Up/Down scroll, other key: back)")
                             : T("Lizenz (Taste: zurueck)", "Licence (any key: back)"));
        for (int i = 0; i + 1 < h && lic[lic_top + i]; i++)
            clipped_text(x + 2, y + 1 + i, lic[lic_top + i], w - 4, lic_top + i ? bg : hot);
        return;
    }
    if (overlay == 3) {
        const char *const *k = en ? keys_en : keys_de;
        int n = 0;
        while (k[n])
            n++;
        int split = n, two = cols >= 76; /* two columns: the second starts with the third group */
        for (int i = 0, g = 0; i < n; i++)
            if (k[i][0] == '@' && ++g == 3)
                split = i;
        int lines = split > n - split ? split : n - split;
        int w = two ? 76 : (cols - 2 < 40 ? cols - 2 : 40), h = (two ? lines : n) + 3;
        if (h > rows)
            h = rows;
        int x = (cols - w) / 2, y = (rows - h) / 2;
        box_at(x, y, w, h, T("Tasten (Taste: zurueck)", "Keys (any key: back)"));
        for (int i = 0; i < n; i++) {
            int c = two && i >= split, r = c ? i - split : i;
            if (1 + r >= h - 1)
                continue;
            const char *s = k[i];
            clipped_text(x + 2 + c * 37, y + 1 + r, s[0] == '@' ? s + 1 : s, two ? 36 : w - 4, s[0] == '@' ? hot : bg);
        }
        return;
    }
    if (overlay == 4) { /* two columns: editing and effects */
        int cw = 31, w = 2 * cw + 4, h = (NED > NFX ? NED : NFX) + 5;
        if (w > cols)
            w = cols;
        int x = (cols - w) / 2, y = (rows - h) / 2;
        if (y < 0)
            y = 0;
        box_at(x, y, w, h, T("Bearbeiten und Effekte", "Edit and effects"));
        clipped_text(x + 2, y + 1, T("Bearbeiten", "Edit"), cw, hot);
        clipped_text(x + 2 + cw + 1, y + 1, has_sel() ? T("Effekte fuer die Auswahl", "Effects for the selection") : T("Effekte fuer den ganzen Ton", "Effects for the whole sound"), cw, hot);
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < (c ? NFX : NED); i++) {
                int on = c == ed_col && i == (c ? fx_sel : ed_sel), xx = x + 1 + c * (cw + 1);
                uint8_t a = on ? btn : bg;
                for (int j = 0; j < cw && xx + j < x + w - 1; j++)
                    vx_put(xx + j, y + 2 + i, ' ', a);
                clipped_text(xx + 1, y + 2 + i, c ? fx_name(i) : ed_name(i), cw - 7, a);
                char k[2] = {c ? fx_key[i] : 0, 0};
                const char *ks = c ? k : ed_key(i);
                if (ks[0])
                    vx_text(xx + cw - 1 - (int)strlen(ks), y + 2 + i, ks, on ? btn : dim);
            }
        clipped_text(x + 2, y + h - 2, has_sel() ? T("Auswahl: Shift + Pfeile, wie lang steht unten", "Selection: Shift + arrows, its length is shown below")
                                               : T("Ohne Auswahl: Einfuegen am Cursor, Effekte fuer alles", "No selection: insert at the cursor, effects for all"), w - 4, dim);
        clipped_text(x + 2, y + h - 1, T("Pfeile waehlen, Enter fuehrt aus, andere Taste: zurueck", "Arrows choose, Enter runs it, other key: back"), w - 4, dim);
        return;
    }
    int w = cols - 4 < 62 ? cols - 4 : 62, h = 15, x = (cols - w) / 2, y = (rows - h) / 2;
    if (y < 0)
        y = 0;
    box_at(x, y, w, h, "Info - Vio-Audio");
    vx_text(x + 2, y + 2, "VIO-AUDIO", hot);
    clipped_text(x + 2, y + 4, T("Ein Ton-Editor fuer die Tastatur.", "A sound editor for the keyboard."), w - 4, bg);
    clipped_text(x + 2, y + 5, T("Oeffnet WAV, MP3, OGG und FLAC, nimmt vom Mikrofon auf,", "Opens WAV, MP3, OGG and FLAC, records from the microphone,"), w - 4, bg);
    clipped_text(x + 2, y + 6, T("schneidet, hat Effekte und speichert WAV, FLAC und OGG.", "edits, has effects and saves WAV, FLAC and OGG."), w - 4, bg);
    clipped_text(x + 2, y + 8, "Version 1.0", w - 4, dim);
    clipped_text(x + 2, y + 9, "(c) 2026 Merecete", w - 4, dim);
    clipped_text(x + 2, y + 10, T("Veroeffentlicht unter der BSD 3-Clause License.", "Released under the BSD 3-Clause License."), w - 4, dim);
    clipped_text(x + 2, y + 11, T("MP3: minimp3, OGG: stb_vorbis und libvorbis, FLAC: dr_flac", "MP3: minimp3, OGG: stb_vorbis and libvorbis, FLAC: dr_flac"), w - 4, dim);
    vx_text(x + 2, y + 13, T(" L Lizenz ", " L Licence "), btn);
    const char *c = T(" Andere Taste: schliessen ", " Other key: close ");
    vx_text(x + w - 2 - (int)strlen(c), y + 13, c, btn);
}

/* while recording: the time, the level and where it goes */
static void rec_box(int cols, int y0, int rows) {
    int w = cols - 4 < 46 ? cols - 4 : 46, h = 8, x = (cols - w) / 2, y = y0 + (rows - h) / 2;
    if (y < y0)
        y = y0;
    uint8_t bg = VX_ATTR(VC_PANEL, VC_LAVENDER), hot = VX_ATTR(VC_PANEL, VC_HOT), dim = VX_ATTR(VC_PANEL, VC_SOFT);
    tx t = {{0}, 0};
    ts(&t, T("Aufnahme  ", "Recording  "));
    tt(&t, rec_frames, ri.rate, 1);
    box_at(x, y, w, h, t.b);
    clipped_text(x + 2, y + 2, ri.name[0] ? ri.name : T("Mikrofon", "Microphone"), w - 4, hot);
    int mw = w - 4, f = rec_peak * mw / 32768;
    for (int i = 0; i < mw; i++)
        vx_put(x + 2 + i, y + 3, i < f ? (char)219 : (char)250, i < f ? (i >= mw * 9 / 10 ? hot : VX_ATTR(VC_PANEL, VC_NEON)) : dim);
    tx u = {{0}, 0};
    if (D.s.len) {
        ts(&u, T("Kommt bei ", "Goes in at "));
        tt(&u, has_sel() ? sel_a() : D.cur, D.rate, 3);
        ts(&u, T(" hinein", ""));
    } else
        ts(&u, T("Wird ein neuer Ton", "Becomes a new sound"));
    clipped_text(x + 2, y + 4, u.b, w - 4, bg);
    tx v = {{0}, 0};
    long fr = (long)(MAXUNITS - uused) * BLKF / (rec_rate > 0 ? rec_rate : 48000);
    ts(&v, T("Platz fuer noch etwa ", "Room for about "));
    tn(&v, fr / 60);
    ts(&v, T(" min", " more minutes"));
    clipped_text(x + 2, y + 5, v.b, w - 4, dim);
    clipped_text(x + 2, y + 6, T("R oder Leertaste: Stopp", "R or Space: stop"), w - 4, dim);
}

static void draw(void) {
    int cols = wcols(), rows = vx_rows();
    uint8_t bg = VX_ATTR(VC_PANEL, VC_LAVENDER);
    vx_clear(bg);
    if (cols < 20 || rows < 6) {
        vx_text(0, 0, "Vio-Audio", bg);
        vx_show();
        return;
    }
    long ph = playing ? play_pos() : -1;
    /* the top line: name and format, on the right what plays or records */
    uint8_t top = VX_ATTR(VC_RAISE, VC_LAVENDER), toph = VX_ATTR(VC_RAISE, VC_HOT), topd = VX_ATTR(VC_RAISE, VC_SOFT);
    fill(0, cols, top);
    tx t = {{0}, 0};
    ts(&t, fname);
    if (modified() && D.s.len)
        ts(&t, " *");
    vx_text(1, 0, t.b, toph);
    int x = t.n + 3;
    if (D.s.len) {
        tx u = {{0}, 0};
        tn(&u, D.rate);
        ts(&u, D.mono ? T(" Hz  Mono  ", " Hz  mono  ") : T(" Hz  Stereo  ", " Hz  stereo  "));
        tt(&u, D.s.len, D.rate, 3);
        if (x + u.n < cols - 22)
            vx_text(x, 0, u.b, topd);
    }
    tx r = {{0}, 0};
    if (recording) {
        ts(&r, T("AUFNAHME ", "REC "));
        tt(&r, rec_frames, ri.rate, 1);
        rtext(0, cols, r.b, toph);
    } else if (playing) {
        ts(&r, "> ");
        tt(&r, ph, D.rate, 3);
        rtext(0, cols, r.b, toph);
    }
    /* the waveform */
    int wy = 1, wr_ = rows - 4 > 100 ? 100 : rows - 4;
    if (!D.s.len && recording) {
        for (int y = wy; y < wy + wr_; y++)
            fill(y, cols, VX_ATTR(VC_BLACK, VC_SOFT));
    } else if (!D.s.len) {
        uint8_t a = VX_ATTR(VC_BLACK, VC_SOFT), h = VX_ATTR(VC_BLACK, VC_HOT);
        for (int y = wy; y < wy + wr_; y++)
            fill(y, cols, a);
        int cy = wy + wr_ / 2 - 2;
        if (cy < wy)
            cy = wy;
        const char *l1 = T("Noch kein Ton.", "No sound yet."), *l2 = T("O  eine Datei oeffnen (WAV, MP3, OGG, FLAC)", "O  open a file (WAV, MP3, OGG, FLAC)"),
                   *l3 = T("R  eine Aufnahme starten", "R  start a recording"), *l4 = T("F1 alle Tasten", "F1 all keys");
        int lx = (cols - (int)strlen(l2)) / 2;
        if (lx < 1)
            lx = 1;
        clipped_text(lx, cy, l1, cols - lx, h);
        clipped_text(lx, cy + 2, l2, cols - lx, a);
        if (cy + 3 < wy + wr_)
            clipped_text(lx, cy + 3, l3, cols - lx, a);
        if (cy + 4 < wy + wr_)
            clipped_text(lx, cy + 4, l4, cols - lx, a);
    } else
        draw_wave(cols, wy, wr_, ph);
    if (recording)
        rec_box(cols, wy, wr_);
    ruler(cols, rows - 3);
    /* cursor, selection and what is in sight */
    uint8_t ia = VX_ATTR(VC_PANEL, VC_LAVENDER), ih = VX_ATTR(VC_PANEL, VC_HOT), idm = VX_ATTR(VC_PANEL, VC_DIM);
    fill(rows - 2, cols, ia);
    if (D.s.len) {
        tx c = {{0}, 0};
        if (has_sel()) {
            ts(&c, T("Auswahl ", "Selection "));
            tt(&c, sel_a(), D.rate, 3);
            ts(&c, " - ");
            tt(&c, sel_b(), D.rate, 3);
            ts(&c, "  (");
            tt(&c, sel_b() - sel_a(), D.rate, 3);
            ts(&c, ")");
        } else {
            ts(&c, "Cursor ");
            tt(&c, D.cur, D.rate, 3);
        }
        vx_text(1, rows - 2, c.b, ih);
        tx v = {{0}, 0};
        ts(&v, T("Sicht ", "View "));
        tt(&v, vstart, D.rate, 1);
        ts(&v, " - ");
        long ve = vstart + pxw() * spp;
        tt(&v, ve > D.s.len ? D.s.len : ve, D.rate, 1);
        if (c.n + v.n + 6 < cols)
            rtext(rows - 2, cols, v.b, idm);
    }
    /* the bottom line: a message, the recording or the keys */
    uint8_t ba = VX_ATTR(VC_BLACK, VC_LAVENDER), bh = VX_ATTR(VC_BLACK, VC_HOT);
    fill(rows - 1, cols, ba);
    if (recording) {
        clipped_text(1, rows - 1, T("Aufnahme laeuft - R oder Leertaste beendet sie", "Recording - R or Space stops it"), cols - 2, bh);
        if (vx_ms() - rec_peak_t > 120) { /* the level falls back slowly */
            rec_peak = rec_peak * 3 / 4;
            rec_peak_t = vx_ms();
        }
    } else if (msg[0] && vx_ms() - msg_t < 4000)
        clipped_text(1, rows - 1, msg, cols - 2, bh);
    else {
        msg[0] = 0;
        const char *hint = has_sel() ? T("X Ausschneiden  Entf Loeschen  C Kopieren  T Kuerzen  Leer Anhoeren",
                                         "X cut  Del delete  C copy  T trim  Space listen  E more")
                           : D.s.len ? T("Leer Spielen  Shift+Pfeile markieren  D Datei einfuegen  E Bearbeiten",
                                         "Space play  Shift+arrows select  D insert file  E edit")
                                     : T("O Oeffnen  R Aufnahme  D Datei einfuegen  F1 Tasten", "O open  R record  D insert file  F1 keys");
        clipped_text(1, rows - 1, hint, cols - 2, ba);
    }
    if (overlay)
        draw_overlay(cols, rows);
    vx_show();
}

/* ---------- keys ---------- */
static int shift(void) {
    return vx_held(0x2A) || vx_held(0x36);
}
static int ctrl(void) {
    return vx_held(0x1D);
}
static void move_to(long p, int sh) {
    if (p < 0)
        p = 0;
    if (p > D.s.len)
        p = D.s.len;
    if (sh) {
        if (!D.sel) {
            D.sel = 1;
            D.anc = D.cur;
        }
    } else
        D.sel = 0;
    D.cur = p;
    follow();
}
static int quit_ok(void) {
    if (recording)
        rec_stop();
    if (!modified() || !D.s.len)
        return 1;
    return vx_ask("Vio-Audio", T("Die Aenderungen sind nicht gespeichert. Trotzdem beenden?",
                                 "The changes are not saved. Quit anyway?"));
}
static void next_input(void) {
    char n[40];
    int k = rec_in + 1;
    if (!vx_rec_input(k, n))
        k = 0;
    if (!vx_rec_input(k, n)) {
        say(T("Kein Mikrofon gefunden", "No microphone found"));
        return;
    }
    rec_in = k;
    say2(T("Mikrofon: ", "Microphone: "), n);
    char c = (char)('0' + k);
    vx_write("input", &c, 1);
}
static void run_edit(int i) {
    switch (i) {
        case E_CUT:
            do_copy(1);
            break;
        case E_COPY:
            do_copy(0);
            break;
        case E_PASTE:
            do_paste();
            break;
        case E_DEL:
            do_delete();
            break;
        case E_TRIM:
            do_trim();
            break;
        case E_UNDO:
            do_undo(0);
            break;
        case E_REDO:
            do_undo(1);
            break;
        case E_INS:
            insert_file(0);
            break;
        case E_MIX:
            insert_file(1);
            break;
    }
}
static void key(int k) {
    if (k >= 'a' && k <= 'z')
        k -= 32;
    if (overlay == 2) {
        if (k == VK_UP && lic_top > 0)
            lic_top--;
        else if (k == VK_DOWN && lic_top < lic_max(vx_rows()))
            lic_top++;
        else if (k != VK_UP && k != VK_DOWN)
            overlay = 1;
        return;
    }
    if (overlay == 1) {
        overlay = k == 'L' ? 2 : 0;
        lic_top = 0;
        return;
    }
    if (overlay == 3) {
        overlay = 0;
        return;
    }
    if (overlay == 4) {
        int n = ed_col ? NFX : NED, *sel = ed_col ? &fx_sel : &ed_sel;
        if (k == VK_UP)
            *sel = (*sel + n - 1) % n;
        else if (k == VK_DOWN)
            *sel = (*sel + 1) % n;
        else if (k == VK_LEFT || k == VK_RIGHT || k == VK_TAB) {
            ed_col = !ed_col;
            n = ed_col ? NFX : NED;
            sel = ed_col ? &fx_sel : &ed_sel;
            if (*sel >= n)
                *sel = n - 1;
        } else {
            overlay = 0;
            if (k == VK_ENTER) {
                draw(); /* without the list behind the questions */
                if (ed_col)
                    fx_apply(fx_sel);
                else
                    run_edit(ed_sel);
            }
        }
        return;
    }
    if (recording) {
        if (k == 'R' || k == ' ' || k == VK_MENU + 6)
            rec_stop();
        else if (k == VK_MENU)
            overlay = 1;
        else
            say(T("Erst die Aufnahme beenden (R)", "Stop the recording first (R)"));
        return;
    }
    long len = D.s.len, pg = pxw() * spp, step = spp * (ctrl() ? 16 : 1);
    int sh = shift();
    switch (k) {
        case VK_MENU:
            overlay = 1;
            break;
        case VK_F1:
        case VK_MENU + 7:
        case '?':
            overlay = 3;
            break;
        case 'E':
        case VK_MENU + 4:
            overlay = 4;
            break;
        case VK_LEFT:
            move_to(D.cur - step, sh);
            break;
        case VK_RIGHT:
            move_to(D.cur + step, sh);
            break;
        case VK_PGUP:
            move_to(D.cur - pg, sh);
            break;
        case VK_PGDN:
            move_to(D.cur + pg, sh);
            break;
        case VK_HOME:
            move_to(0, sh);
            break;
        case VK_END:
            move_to(len, sh);
            break;
        case VK_UP:
            zoom(1);
            break;
        case VK_DOWN:
            zoom(0);
            break;
        case 'F':
            zoom_fit();
            break;
        case 'A':
            D.anc = 0;
            D.cur = len;
            D.sel = len > 0;
            break;
        case ' ':
            if (playing)
                play_stop();
            else if (has_sel())
                play(sel_a(), sel_b());
            else
                play(D.cur >= len ? 0 : D.cur, len);
            break;
        case VK_ENTER:
            play_stop();
            play(has_sel() ? sel_a() : 0, has_sel() ? sel_b() : len);
            break;
        case 'R':
        case VK_MENU + 6:
            rec_start();
            break;
        case VK_TAB:
            next_input();
            break;
        case 'X':
            do_copy(1);
            break;
        case 'C':
            do_copy(0);
            break;
        case 'V':
            do_paste();
            break;
        case VK_DEL:
        case VK_BS:
            do_delete();
            break;
        case 'T':
            do_trim();
            break;
        case 'Z':
            do_undo(0);
            break;
        case 'Y':
            do_undo(1);
            break;
        case 'D':
        case VK_MENU + 5:
            insert_file(0);
            break;
        case 'K':
            insert_file(1);
            break;
        case 'O':
        case VK_MENU + 1:
            open_file();
            break;
        case 'S':
        case VK_MENU + 2:
            save(0);
            break;
        case 'W':
        case VK_MENU + 3:
            save(1);
            break;
        default:
            for (int i = 0; i < NFX; i++)
                if (fx_key[i] && k == fx_key[i])
                    fx_apply(i);
    }
}

/* ---------- the main loop ---------- */
int main(void) {
    en = vx_english();
    img = vx_alloc(960 * 800);
    mm = vx_alloc(960 * 4 * sizeof(short));
    zero = unit_get();
    if (!img || !mm || !zero) {
        vx_message("Vio-Audio", T("Nicht genug Speicher.", "Not enough memory."));
        return 0;
    }
    reserve = unit_get();
    zero->ref = 1; /* never freed */
    zero->fill = BLKF;
    scpy(fname, T("Unbenannt", "Untitled"), sizeof fname);
    D.rate = 48000;
    char c = 0;
    if (vx_read("input", &c, 1) == 1 && c >= '0' && c <= '9')
        rec_in = c - '0';
    char n[40];
    if (!vx_rec_input(rec_in, n))
        rec_in = 0;
    vx_menu(en ? "Info|Open (O)|Save (S)|Save as (W)|Edit and effects (E)|Insert file (D)|Record (R)|Keys (F1)"
               : "Info|Oeffnen (O)|Speichern (S)|Speichern unter (W)|Bearbeiten (E)|Datei einfuegen (D)|Aufnahme (R)|Tasten (F1)");
    vx_sys(23, 3, 2, 0); /* Esc comes as a key: unsaved changes are asked for first */
    vx_sys(23, 8, 1, 0); /* F1 too */
    if (!vx_sys(23, 13, 0, 0))
        vx_sys(23, 13, 1, 0); /* the whole screen for the waveform */
    int lc = 0, lr = 0;
    for (;;) {
        int k, any = 0;
        while ((k = vx_key()) != 0) {
            any = 1;
            if (k == VK_ESC) {
                if (overlay)
                    overlay = 0;
                else if (quit_ok()) {
                    play_stop();
                    return 0;
                }
                continue;
            }
            key(k);
        }
        rec_pull();
        play_feed();
        if (!reserve && !recording)
            reserve = unit_get();
        int cols = vx_cols(), rows = vx_rows();
        if (any || playing || recording || msg[0] || cols != lc || rows != lr) {
            if (cols != lc && D.s.len && spp > fit_spp())
                spp = fit_spp();
            lc = cols;
            lr = rows;
            draw();
        }
        vx_wait(playing || recording ? 20 : msg[0] ? 250 : 500);
    }
}
