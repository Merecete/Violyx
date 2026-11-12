/* More formats for XZN, all written for Violyx (MIT license, like the rest):
 *   AIFF/AIFC, CAF (PCM and Apple Lossless), True Audio, WavPack, DSD (.dsf/.dff),
 *   Matroska audio (.mka) and AC3.
 * Every decoder delivers stereo frames as 16-bit samples, like the others in XZN.
 * The file lies in memory (f, n), only DSD is read piece by piece because those files are huge. */
typedef unsigned char xu8;
typedef unsigned int xu32;
typedef unsigned long long xu64;

typedef struct xdec {
    int srate, chans, bits, kbps;
    long total; /* frames, 0 = unknown */
    const char *name;
    int (*read)(struct xdec *, short *, int);
    void (*seek)(struct xdec *, long);
    const xu8 *f;
    long n;
    void *s;
    /* without f: parts of the file come from fetch (DSD) */
    long (*fetch)(struct xdec *, void *b, long off, long len);
    /* framed decoders: decode frame idx into buf, return the frames */
    int (*frame)(struct xdec *, long idx, short *out);
    long fidx, fcount, flen; /* next frame, frames in the file, nominal frame length */
    int have, at, skip;
    short *buf;
    char title[70], artist[70]; /* from the tags of the container, if there are any */
} xdec;

static xu32 xc_be16(const xu8 *p) {
    return (xu32)p[0] << 8 | p[1];
}
static xu32 xc_be32(const xu8 *p) {
    return (xu32)p[0] << 24 | (xu32)p[1] << 16 | (xu32)p[2] << 8 | p[3];
}
static xu64 xc_be64(const xu8 *p) {
    return (xu64)xc_be32(p) << 32 | xc_be32(p + 4);
}
static xu32 xc_le16(const xu8 *p) {
    return p[0] | (xu32)p[1] << 8;
}
static xu32 xc_le32(const xu8 *p) {
    return p[0] | (xu32)p[1] << 8 | (xu32)p[2] << 16 | (xu32)p[3] << 24;
}
static xu64 xc_le64(const xu8 *p) {
    return xc_le32(p) | (xu64)xc_le32(p + 4) << 32;
}
static void xc_cpy(char *o, const char *s, int n) {
    int i = 0;
    for (; s[i] && i < n - 1; i++)
        o[i] = s[i];
    o[i] = 0;
}
static short xc_clip(long v) {
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (short)v;
}
/* a sample of `bits` bits as a 16-bit one */
static short xc_to16(long v, int bits) {
    if (bits > 16)
        return xc_clip(v >> (bits - 16));
    if (bits < 16)
        return xc_clip(v << (16 - bits));
    return xc_clip(v);
}
static void xc_rate(xdec *d) {
    long secs = d->srate && d->total ? d->total / d->srate : 0;
    d->kbps = secs ? (int)((long long)d->n * 8 / (secs * 1000)) : 0;
}

/* ---------- bit readers: MSB first (ALAC, AC3) and LSB first (TTA, WavPack) ---------- */
typedef struct {
    const xu8 *p;
    long n;   /* bytes */
    long pos; /* bits */
} xbr;
static xu32 xb_word(const xbr *b, long byte, int le) {
    xu32 v = 0;
    for (int i = 0; i < 4; i++) {
        xu32 c = byte + i < b->n && byte + i >= 0 ? b->p[byte + i] : 0;
        v = le ? v | c << (8 * i) : v << 8 | c;
    }
    return v;
}
static int xb_left(const xbr *b) {
    long l = b->n * 8 - b->pos;
    return l > 0x7fffffff ? 0x7fffffff : (int)l;
}
/* MSB first, k = 0..32 */
static xu32 xbm(xbr *b, int k) {
    xu32 v = 0;
    while (k > 0) {
        int t = k > 24 ? 24 : k;
        xu32 w = xb_word(b, b->pos >> 3, 0) << (b->pos & 7);
        v = v << t | w >> (32 - t);
        b->pos += t;
        k -= t;
    }
    return v;
}
static int xbms(xbr *b, int k) { /* signed */
    xu32 v = xbm(b, k);
    return k && k < 32 ? (int)(v << (32 - k)) >> (32 - k) : (int)v;
}
/* LSB first */
static xu32 xbl(xbr *b, int k) {
    xu32 v = 0;
    int sh = 0;
    while (k > 0) {
        int t = k > 24 ? 24 : k;
        xu32 w = xb_word(b, b->pos >> 3, 1) >> (b->pos & 7);
        v |= (w & ((1u << t) - 1)) << sh;
        b->pos += t;
        sh += t;
        k -= t;
    }
    return v;
}
/* LSB first: ones before the first zero, at most max (the zero is read too if it comes) */
static int xbl_unary(xbr *b, int max) {
    int c = 0;
    while (c < max) {
        if (b->pos >= b->n * 8)
            return c;
        int bit = b->p[b->pos >> 3] >> (b->pos & 7) & 1;
        b->pos++;
        if (!bit)
            return c;
        c++;
    }
    return c;
}

/* ---------- framed decoders: read and seek for all that work frame by frame ---------- */
static int xf_read(xdec *d, short *o, int n) {
    int g = 0;
    while (g < n) {
        if (d->at >= d->have) {
            if (d->fidx >= d->fcount)
                break;
            d->have = d->frame(d, d->fidx++, d->buf);
            if (d->have <= 0) {
                d->have = 0;
                break;
            }
            d->at = d->skip < d->have ? d->skip : d->have;
            d->skip = 0;
            continue;
        }
        int k = d->have - d->at;
        if (k > n - g)
            k = n - g;
        memcpy(o + g * 2, d->buf + d->at * 2, (size_t)k * 4);
        d->at += k;
        g += k;
    }
    return g;
}
static void xf_seek(xdec *d, long fr) {
    d->fidx = d->flen ? fr / d->flen : 0;
    d->skip = d->flen ? (int)(fr % d->flen) : 0;
    d->have = d->at = 0;
}
static int xf_init(xdec *d, long fmax) {
    d->buf = malloc((size_t)fmax * 4 + 64);
    d->read = xf_read;
    d->seek = xf_seek;
    d->fidx = 0;
    d->have = d->at = d->skip = 0;
    return d->buf != 0;
}

/* ---------- PCM (AIFF, CAF, Matroska) ---------- */
typedef struct {
    long start, off, end;
    int bpf, bytes, be, flt, uns;
} xpcm;
static long xpcm_get(const xu8 *p, int bytes, int be, int flt, int uns) {
    if (flt) {
        union {
            xu32 i;
            float f;
        } u32;
        union {
            xu64 i;
            double f;
        } u64;
        double v;
        if (bytes == 4) {
            u32.i = be ? xc_be32(p) : xc_le32(p);
            v = u32.f;
        } else {
            u64.i = be ? xc_be64(p) : xc_le64(p);
            v = u64.f;
        }
        v *= 32768.0;
        return v > 32767 ? 32767 : v < -32768 ? -32768 : (long)v;
    }
    long v = 0;
    if (be)
        for (int i = 0; i < bytes; i++)
            v = v << 8 | p[i];
    else
        for (int i = bytes - 1; i >= 0; i--)
            v = v << 8 | p[i];
    if (uns)
        v -= 1L << (bytes * 8 - 1);
    else if (bytes < 8)
        v = (long)((xu64)v << (64 - bytes * 8)) >> (64 - bytes * 8);
    return xc_to16(v, bytes * 8);
}
static int xpcm_read(xdec *d, short *o, int n) {
    xpcm *s = d->s;
    int g = 0;
    for (; g < n && s->off + s->bpf <= s->end; g++, s->off += s->bpf) {
        const xu8 *p = d->f + s->off;
        o[g * 2] = (short)xpcm_get(p, s->bytes, s->be, s->flt, s->uns);
        o[g * 2 + 1] =
            d->chans > 1 ? (short)xpcm_get(p + s->bytes, s->bytes, s->be, s->flt, s->uns) : o[g * 2];
    }
    return g;
}
static void xpcm_seek(xdec *d, long fr) {
    xpcm *s = d->s;
    s->off = s->start + fr * s->bpf;
    if (s->off > s->end)
        s->off = s->end;
}
static int xpcm_setup(xdec *d, long start, long len, int bits, int be, int flt, int uns) {
    xpcm *s = malloc(sizeof *s);
    if (!s || d->chans < 1 || d->chans > 32 || bits < 8 || bits > 64 || (bits & 7))
        return 0;
    if (flt && bits != 32 && bits != 64)
        return 0;
    s->bytes = bits / 8;
    s->bpf = s->bytes * d->chans;
    s->start = s->off = start;
    if (start < 0 || start > d->n)
        return 0;
    s->end = len < 0 || start + len > d->n ? d->n : start + len;
    s->be = be;
    s->flt = flt;
    s->uns = uns;
    d->s = s;
    d->bits = bits;
    d->total = (s->end - s->start) / s->bpf;
    d->read = xpcm_read;
    d->seek = xpcm_seek;
    xc_rate(d);
    return 1;
}

/* ---------- AIFF and AIFC ---------- */
static double xc_ext80(const xu8 *p) { /* the 80-bit number of the sample rate */
    int e = (p[0] & 0x7f) << 8 | p[1];
    xu64 m = xc_be64(p + 2);
    if (!e && !m)
        return 0;
    double v = (double)m;
    e -= 16383 + 63;
    while (e > 0) {
        v *= 2;
        e--;
    }
    while (e < 0) {
        v /= 2;
        e++;
    }
    return v;
}
static int aiff_open(xdec *d) {
    const xu8 *f = d->f;
    long n = d->n;
    if (n < 12 || memcmp(f, "FORM", 4) || (memcmp(f + 8, "AIFF", 4) && memcmp(f + 8, "AIFC", 4)))
        return 0;
    int aifc = !memcmp(f + 8, "AIFC", 4), bits = 0, be = 1, flt = 0, ok = 0;
    long o = 12;
    while (o + 8 <= n) {
        long l = xc_be32(f + o + 4);
        if (l < 0)
            break;
        const xu8 *c = f + o + 8;
        if (!memcmp(f + o, "COMM", 4) && l >= 18 && o + 8 + 18 <= n) {
            d->chans = (int)xc_be16(c);
            bits = (int)xc_be16(c + 6);
            d->srate = (int)(xc_ext80(c + 8) + 0.5);
            if (aifc && l >= 22 && o + 8 + 22 <= n) {
                if (!memcmp(c + 18, "sowt", 4))
                    be = 0;
                else if (!memcmp(c + 18, "fl32", 4) || !memcmp(c + 18, "FL32", 4))
                    flt = 1, bits = 32;
                else if (!memcmp(c + 18, "fl64", 4) || !memcmp(c + 18, "FL64", 4))
                    flt = 1, bits = 64;
                else if (memcmp(c + 18, "NONE", 4) && memcmp(c + 18, "twos", 4) &&
                         memcmp(c + 18, "in24", 4) && memcmp(c + 18, "in32", 4))
                    return 0; /* compressed AIFC (ulaw, ima4 ...) */
            }
            ok = 1;
        }
        if (!memcmp(f + o, "SSND", 4) && ok && l >= 8 && o + 16 <= n) {
            long off = xc_be32(c);
            bits = (bits + 7) & ~7;
            d->name = "AIFF";
            return xpcm_setup(d, o + 16 + off, l - 8 - off, bits, be, flt, 0);
        }
        o += 8 + l + (l & 1);
    }
    return 0;
}

/* ---------- Apple Lossless (in CAF and Matroska) ---------- */
typedef struct {
    xu32 flen;
    int depth, pb, mb, kb, chans;
    int *buf[2], *err, *ex[2];
} xalac;
static int alac_config(xalac *a, const xu8 *c, long l) {
    /* the cookie may still be wrapped in 'frma' and 'alac' atoms */
    for (int k = 0; k < 2; k++) {
        if (l >= 12 && !memcmp(c + 4, "frma", 4)) {
            c += 12;
            l -= 12;
        }
        if (l >= 12 && !memcmp(c + 4, "alac", 4)) {
            c += 12;
            l -= 12;
        }
    }
    if (l < 24)
        return 0;
    a->flen = xc_be32(c);
    a->depth = c[5];
    a->pb = c[6];
    a->mb = c[7];
    a->kb = c[8];
    a->chans = c[9];
    if (a->flen < 1 || a->flen > 65536 || (a->depth != 16 && a->depth != 20 && a->depth != 24 &&
                                           a->depth != 32))
        return 0;
    for (int i = 0; i < 2; i++) {
        a->buf[i] = malloc(a->flen * 4);
        a->ex[i] = malloc(a->flen * 4);
        if (!a->buf[i] || !a->ex[i])
            return 0;
    }
    a->err = malloc(a->flen * 4);
    return a->err != 0;
}
static xu32 alac_scalar(xbr *b, int k, int bps) {
    xu32 x = 0;
    while (x < 9 && xbm(b, 1))
        x++;
    if (x > 8)
        return xbm(b, bps);
    if (k != 1) {
        xu32 save = (xu32)b->pos, extra = xbm(b, k);
        x = (x << k) - x;
        if (extra > 1)
            x += extra - 1;
        else
            b->pos = save + (xu32)k - 1;
    }
    return x;
}
static int alac_log2(xu32 v) {
    int r = 0;
    while (v >>= 1)
        r++;
    return r;
}
static void alac_rice(xalac *a, xbr *b, int *out, int ns, int bps, int mult) {
    xu32 hist = (xu32)a->mb;
    int signmod = 0;
    for (int i = 0; i < ns;) {
        int k = alac_log2((hist >> 9) + 3);
        if (k > a->kb)
            k = a->kb;
        xu32 x = alac_scalar(b, k, bps) + (xu32)signmod;
        signmod = 0;
        out[i] = (int)(x >> 1) ^ -(int)(x & 1);
        if (x > 0xffff)
            hist = 0xffff;
        else
            hist += x * (xu32)mult - ((hist * (xu32)mult) >> 9);
        if (hist < 128 && i + 1 < ns) {
            k = 7 - alac_log2(hist) + (int)((hist + 16) >> 6);
            if (k > a->kb)
                k = a->kb;
            int bs = (int)alac_scalar(b, k, 16);
            if (bs > 0) {
                if (bs >= ns - i)
                    bs = ns - i - 1;
                memset(out + i + 1, 0, (size_t)bs * 4);
                i += bs;
            }
            if (bs <= 0xffff)
                signmod = 1;
            hist = 0;
        }
        i++;
    }
}
static int alac_sext(xu32 v, int bits) {
    return (int)(v << (32 - bits)) >> (32 - bits);
}
static int alac_sgn(int v) {
    return (v > 0) - (v < 0);
}
static void alac_lpc(int *err, int *out, int ns, int bps, short *coef, int order, int quant) {
    out[0] = err[0];
    if (ns <= 1)
        return;
    if (!order) {
        memcpy(out + 1, err + 1, (size_t)(ns - 1) * 4);
        return;
    }
    if (order == 31) {
        for (int i = 1; i < ns; i++)
            out[i] = alac_sext((xu32)out[i - 1] + (xu32)err[i], bps);
        return;
    }
    int i = 1;
    for (; i <= order && i < ns; i++)
        out[i] = alac_sext((xu32)out[i - 1] + (xu32)err[i], bps);
    for (; i < ns; i++) {
        int *pr = out + i - order - 1, dd = pr[0];
        pr++;
        long long val = 0;
        for (int j = 0; j < order; j++)
            val += (long long)(pr[j] - dd) * coef[j];
        val = (val + (1LL << (quant - 1))) >> quant;
        int e = err[i];
        out[i] = alac_sext((xu32)(val + dd + e), bps);
        int es = alac_sgn(e);
        if (es)
            for (int j = 0; j < order && e * es > 0; j++) {
                int v = dd - pr[j], sg = alac_sgn(v) * es;
                coef[j] = (short)(coef[j] - sg);
                v *= sg;
                e -= (v >> quant) * (j + 1);
            }
    }
}
/* one packet: the first channel pair (or the single channel) into o, returns the frames */
static int alac_packet(xalac *a, const xu8 *p, long len, short *o) {
    xbr b = {p, len, 0};
    int got = 0, outch = 0, ns = 0;
    while (xb_left(&b) >= 3) {
        int tag = (int)xbm(&b, 3);
        if (tag == 7) /* end */
            break;
        if (tag == 4) { /* data stream element */
            xbm(&b, 4);
            int al = (int)xbm(&b, 1), cnt = (int)xbm(&b, 8);
            if (cnt == 255)
                cnt += (int)xbm(&b, 8);
            if (al)
                b.pos = (b.pos + 7) & ~7L;
            b.pos += cnt * 8L;
            continue;
        }
        if (tag == 6) { /* fill */
            int cnt = (int)xbm(&b, 4);
            if (cnt == 15)
                cnt += (int)xbm(&b, 8) - 1;
            b.pos += cnt * 8L;
            continue;
        }
        if (tag != 0 && tag != 1 && tag != 3)
            return got ? ns : -1;
        int ch = tag == 1 ? 2 : 1;
        xbm(&b, 4);
        xbm(&b, 12);
        int hassize = (int)xbm(&b, 1), shifted = (int)xbm(&b, 2), esc = (int)xbm(&b, 1);
        int n = (int)a->flen;
        if (hassize)
            n = (int)xbm(&b, 32);
        if (n < 1 || n > (int)a->flen || shifted > 2)
            return -1;
        int exbits = shifted * 8, sz = a->depth;
        int *bf[2] = {a->buf[0], a->buf[1]};
        int dshift = 0, dweight = 0;
        if (!esc) {
            int ptype[2], quant[2], mult[2], order[2];
            short coef[2][32];
            dshift = (int)xbm(&b, 8);
            dweight = xbms(&b, 8);
            for (int c = 0; c < ch; c++) {
                ptype[c] = (int)xbm(&b, 4);
                quant[c] = (int)xbm(&b, 4);
                mult[c] = (int)xbm(&b, 3);
                order[c] = (int)xbm(&b, 5);
                if (!quant[c])
                    quant[c] = 1;
                for (int i = order[c] - 1; i >= 0; i--)
                    coef[c][i] = (short)xbms(&b, 16);
            }
            if (exbits)
                for (int i = 0; i < n; i++)
                    for (int c = 0; c < ch; c++)
                        a->ex[c][i] = (int)xbm(&b, exbits);
            int bps = sz - exbits + ch - 1;
            if (bps < 1 || bps > 32)
                return -1;
            for (int c = 0; c < ch; c++) {
                alac_rice(a, &b, a->err, n, bps, mult[c] * a->pb / 4);
                if (ptype[c] == 15)
                    alac_lpc(a->err, a->err, n, bps, 0, 31, 0);
                alac_lpc(a->err, bf[c], n, bps, coef[c], order[c], quant[c]);
            }
        } else {
            exbits = 0;
            for (int i = 0; i < n; i++)
                for (int c = 0; c < ch; c++)
                    bf[c][i] = xbms(&b, sz);
        }
        if (xb_left(&b) < 0)
            return -1;
        if (ch == 2 && dweight)
            for (int i = 0; i < n; i++) {
                int x = bf[0][i], y = bf[1][i];
                x -= (int)(((long long)y * dweight) >> dshift);
                y += x;
                bf[0][i] = y;
                bf[1][i] = x;
            }
        if (exbits)
            for (int c = 0; c < ch; c++)
                for (int i = 0; i < n; i++)
                    bf[c][i] = (int)((xu32)bf[c][i] << exbits | (xu32)a->ex[c][i]);
        /* the first pair wins, otherwise the first single channel */
        if (!got || (ch == 2 && outch < 2)) {
            for (int i = 0; i < n; i++) {
                o[i * 2] = xc_to16(bf[0][i], sz);
                o[i * 2 + 1] = xc_to16(bf[ch - 1][i], sz);
            }
            outch = ch;
            ns = n;
            got = 1;
        }
    }
    return got ? ns : -1;
}

/* ---------- CAF: PCM or Apple Lossless ---------- */
typedef struct {
    xalac a;
    long *pk; /* packet offsets, one more at the end */
    long np, prime;
} xcaf;
static int caf_frame(xdec *d, long idx, short *o) {
    xcaf *c = d->s;
    if (idx >= c->np)
        return 0;
    int k = alac_packet(&c->a, d->f + c->pk[idx], c->pk[idx + 1] - c->pk[idx], o);
    if (k < 0)
        return 0;
    if (idx == 0 && c->prime > 0 && c->prime < k) { /* the encoder's warm-up */
        memmove(o, o + c->prime * 2, (size_t)(k - c->prime) * 4);
        k -= (int)c->prime;
    }
    return k;
}
static void caf_seek(xdec *d, long fr) {
    xcaf *c = d->s;
    xf_seek(d, fr + c->prime);
    if (d->fidx == 0)
        d->skip = (int)fr; /* frame 0 already drops the warm-up */
}
static int caf_open(xdec *d) {
    const xu8 *f = d->f;
    long n = d->n;
    if (n < 8 || memcmp(f, "caff", 4))
        return 0;
    long o = 8, dstart = -1, dlen = -1, pakt = -1, paktl = 0, kuki = -1, kukil = 0;
    xu32 fmtid = 0, flags = 0, fpp = 0, bpp = 0;
    double rate = 0;
    int got = 0;
    while (o + 12 <= n) {
        long long l = (long long)xc_be64(f + o + 4);
        const xu8 *c = f + o + 12;
        if (!memcmp(f + o, "data", 4)) {
            dstart = o + 12 + 4;
            dlen = l < 0 || o + 12 + l > n ? n - dstart : l - 4;
            if (l < 0)
                break;
        } else if (l < 0 || l > n)
            break;
        if (!memcmp(f + o, "desc", 4) && l >= 32 && o + 12 + 32 <= n) {
            union {
                xu64 i;
                double v;
            } u;
            u.i = xc_be64(c);
            rate = u.v;
            fmtid = xc_be32(c + 8);
            flags = xc_be32(c + 12);
            bpp = xc_be32(c + 16);
            fpp = xc_be32(c + 20);
            d->chans = (int)xc_be32(c + 24);
            got = 1;
        }
        if (!memcmp(f + o, "pakt", 4)) {
            pakt = o + 12;
            paktl = (long)l;
        }
        if (!memcmp(f + o, "kuki", 4)) {
            kuki = o + 12;
            kukil = (long)l;
        }
        o += 12 + (long)l;
    }
    if (!got || dstart < 0 || rate < 1000 || rate > 400000)
        return 0;
    d->srate = (int)(rate + 0.5);
    if (fmtid == 0x6c70636d) { /* 'lpcm' */
        if (!bpp || bpp % (xu32)(d->chans > 0 ? d->chans : 1))
            return 0;
        d->name = "CAF";
        return xpcm_setup(d, dstart, dlen, (int)(bpp / (xu32)d->chans) * 8, !(flags & 2), flags & 1,
                          0);
    }
    if (fmtid != 0x616c6163 || kuki < 0 || pakt < 0 || paktl < 24) /* 'alac' */
        return 0;
    xcaf *c = malloc(sizeof *c);
    if (!c || !alac_config(&c->a, f + kuki, kukil))
        return 0;
    long np = (long)xc_be64(f + pakt);
    long long valid = (long long)xc_be64(f + pakt + 8);
    c->prime = (long)xc_be32(f + pakt + 16);
    if (np < 1 || np > 50000000 || c->prime < 0)
        return 0;
    c->pk = malloc((size_t)(np + 1) * sizeof(long));
    if (!c->pk)
        return 0;
    long q = pakt + 24, at = dstart;
    for (long i = 0; i < np; i++) {
        long v = 0;
        int k = 0;
        do {
            if (q >= pakt + paktl || q >= n || ++k > 5)
                return 0;
            v = v << 7 | (f[q] & 127);
        } while (f[q++] & 128);
        c->pk[i] = at;
        at += v;
    }
    c->pk[np] = at;
    if (at > n)
        return 0;
    c->np = np;
    d->s = c;
    d->bits = c->a.depth;
    d->name = "ALAC";
    d->frame = caf_frame;
    d->fcount = np;
    d->flen = fpp ? fpp : c->a.flen;
    d->total = valid > 0 ? (long)valid : np * d->flen;
    if (!xf_init(d, c->a.flen))
        return 0;
    d->seek = caf_seek;
    xc_rate(d);
    return 1;
}

/* ---------- True Audio ---------- */
typedef struct {
    int shift, round, err, qm[8], dx[8], dl[8];
} xttaf;
typedef struct {
    xu32 k0, k1, s0, s1;
} xttar;
typedef struct {
    int bps, nch;
    long data, *fo; /* frame offsets */
    long last;      /* frames in the last frame */
    int *tmp;
} xtta;
static void tta_filter(xttaf *c, int *in) {
    int *qm = c->qm, *dx = c->dx, *dl = c->dl;
    long long sum = c->round;
    if (c->err < 0)
        for (int i = 0; i < 8; i++)
            qm[i] -= dx[i];
    else if (c->err > 0)
        for (int i = 0; i < 8; i++)
            qm[i] += dx[i];
    for (int i = 0; i < 8; i++)
        sum += (long long)dl[i] * qm[i];
    int s = (int)sum;
    dx[0] = dx[1];
    dx[1] = dx[2];
    dx[2] = dx[3];
    dx[3] = dx[4];
    dl[0] = dl[1];
    dl[1] = dl[2];
    dl[2] = dl[3];
    dl[3] = dl[4];
    dx[4] = ((dl[4] >> 30) | 1);
    dx[5] = ((dl[5] >> 30) | 2) & ~1;
    dx[6] = ((dl[6] >> 30) | 2) & ~1;
    dx[7] = ((dl[7] >> 30) | 4) & ~3;
    c->err = *in;
    *in += s >> c->shift;
    dl[4] = -dl[5];
    dl[5] = -dl[6];
    dl[6] = *in - dl[7];
    dl[7] = *in;
    dl[5] += dl[6];
    dl[4] += dl[5];
}
static int tta_frame(xdec *d, long idx, short *o) {
    xtta *t = d->s;
    int len = (int)(idx == d->fcount - 1 && t->last ? t->last : d->flen), nch = t->nch;
    long st = t->fo[idx], en = t->fo[idx + 1] - 4;
    if (en <= st || en > d->n)
        return 0;
    xbr b = {d->f + st, en - st, 0};
    static const int shifts[4] = {10, 9, 10, 12};
    xttaf fl[8];
    xttar rc[8];
    int pred[8];
    for (int c = 0; c < nch; c++) {
        memset(&fl[c], 0, sizeof fl[c]);
        fl[c].shift = shifts[t->bps - 1];
        fl[c].round = 1 << (fl[c].shift - 1);
        rc[c].k0 = rc[c].k1 = 10;
        rc[c].s0 = rc[c].s1 = 1u << 14;
        pred[c] = 0;
    }
    int *p = t->tmp;
    for (int i = 0; i < len; i++) {
        for (int c = 0; c < nch; c++, p++) {
            xttar *r = &rc[c];
            int un = xbl_unary(&b, 1 << 20), depth;
            xu32 k;
            if (!un) {
                depth = 0;
                k = r->k0;
            } else {
                depth = 1;
                k = r->k1;
                un--;
            }
            if (b.pos > b.n * 8 || k > 24)
                return i;
            xu32 v = k ? ((xu32)un << k) + xbl(&b, (int)k) : (xu32)un;
            if (depth) {
                r->s1 += v - (r->s1 >> 4);
                if (r->k1 > 0 && r->s1 < (1u << (r->k1 + 4)))
                    r->k1--;
                else if (r->s1 > (1u << (r->k1 + 5)) && r->k1 < 26)
                    r->k1++;
                v += 1u << r->k0;
            }
            r->s0 += v - (r->s0 >> 4);
            if (r->k0 > 0 && r->s0 < (1u << (r->k0 + 4)))
                r->k0--;
            else if (r->s0 > (1u << (r->k0 + 5)) && r->k0 < 26)
                r->k0++;
            *p = 1 + (int)((v >> 1) ^ ((v & 1) - 1));
            tta_filter(&fl[c], p);
            if (t->bps == 1)
                *p += (int)(((long long)pred[c] * 16 - pred[c]) >> 4);
            else if (t->bps == 4)
                *p += pred[c];
            else
                *p += (int)(((long long)pred[c] * 32 - pred[c]) >> 5);
            pred[c] = *p;
        }
        if (nch > 1) {
            int *q = p - 1, *last = p - 1;
            *last += *(last - 1) / 2;
            for (q = last - 1; q > p - 1 - nch; q--)
                *q = *(q + 1) - *q;
        }
    }
    int bits = t->bps * 8;
    for (int i = 0; i < len; i++) {
        int *s = t->tmp + i * nch;
        o[i * 2] = xc_to16(s[0], bits);
        o[i * 2 + 1] = xc_to16(s[nch > 1], bits);
    }
    return len;
}
static int tta_open(xdec *d) {
    const xu8 *f = d->f;
    long n = d->n, o = 0;
    if (n > 10 && !memcmp(f, "ID3", 3))
        o = 10 + ((f[6] & 127) << 21 | (f[7] & 127) << 14 | (f[8] & 127) << 7 | (f[9] & 127));
    if (o + 22 > n || memcmp(f + o, "TTA1", 4))
        return 0;
    xtta *t = malloc(sizeof *t);
    if (!t)
        return 0;
    int fmt = (int)xc_le16(f + o + 4);
    t->nch = (int)xc_le16(f + o + 6);
    int bits = (int)xc_le16(f + o + 8);
    d->srate = (int)xc_le32(f + o + 10);
    xu32 len = xc_le32(f + o + 14);
    if (fmt != 1 || t->nch < 1 || t->nch > 8 || bits < 8 || bits > 32 || d->srate < 1000 ||
        d->srate > 400000)
        return 0;
    t->bps = (bits + 7) / 8;
    d->flen = 256L * d->srate / 245;
    long nf = (long)(len / d->flen) + (len % d->flen ? 1 : 0);
    t->last = (long)(len % d->flen);
    long tab = o + 22, data = tab + nf * 4 + 4;
    if (nf < 1 || data > n)
        return 0;
    t->fo = malloc((size_t)(nf + 1) * sizeof(long));
    t->tmp = malloc((size_t)d->flen * t->nch * sizeof(int) + 64);
    if (!t->fo || !t->tmp)
        return 0;
    long at = data;
    for (long i = 0; i < nf; i++) {
        t->fo[i] = at;
        at += xc_le32(f + tab + i * 4);
        if (at > n)
            at = n;
    }
    t->fo[nf] = at;
    d->s = t;
    d->chans = t->nch;
    d->bits = bits;
    d->total = len;
    d->name = "True Audio";
    d->frame = tta_frame;
    d->fcount = nf;
    if (!xf_init(d, d->flen))
        return 0;
    xc_rate(d);
    return 1;
}

/* ---------- WavPack (lossless integer audio) ---------- */
typedef struct {
    int value, delta, wa, wb, sa[8], sb[8];
} xwvterm;
typedef struct {
    int median[3];
} xwvch;
typedef struct {
    long *bo; /* offsets of the first block of each frame */
    long nb;
    int bits;
    xwvterm t[16];
    int nterms;
    xwvch ch[2];
    int zero, one, zeroes;
    xbr b;
    int shift, and_, or_, sent; /* int32 info */
} xwv;
static const int wv_rates[15] = {6000,  8000,  9600,  11025, 12000, 16000, 22050, 24000,
                                 32000, 44100, 48000, 64000, 88200, 96000, 192000};
static int wv_exp2(int v) {
    int neg = v < 0;
    if (neg)
        v = -v;
    int t = v & 255;
    int r = (int)(256 * pow(2.0, t / 256.0) + 0.5) - 256; /* the table of WavPack, computed */
    r = (r & 255) | 256;
    v >>= 8;
    if (v > 31)
        return neg ? -0x7fffffff : 0x7fffffff;
    r = v > 9 ? r << (v - 9) : r >> (9 - v);
    return neg ? -r : r;
}
#define WV_GET_MED(n) ((c->median[n] >> 4) + 1)
#define WV_DEC_MED(n) c->median[n] -= ((c->median[n] + (128 >> (n)) - 2) / (128 >> (n))) * 2
#define WV_INC_MED(n) c->median[n] += ((c->median[n] + (128 >> (n))) / (128 >> (n))) * 5
static xu32 wv_tail(xbr *b, xu32 k) {
    if (k < 1)
        return 0;
    int p = 0;
    while ((k >> (p + 1)))
        p++;
    xu32 e = (xu32)((1LL << (p + 1)) - k - 1);
    xu32 r = xbl(b, p);
    if (r >= e)
        r = r * 2 - e + xbl(b, 1);
    return r;
}
static int wv_value(xwv *s, int chn, int *last) {
    xwvch *c = &s->ch[chn];
    xbr *b = &s->b;
    *last = 0;
    if (s->ch[0].median[0] < 2 && s->ch[1].median[0] < 2 && !s->zero && !s->one) {
        if (s->zeroes) {
            if (--s->zeroes)
                return 0;
        } else {
            int t = xbl_unary(b, 33);
            if (t >= 2) {
                if (t >= 32)
                    goto bad;
                t = (int)(xbl(b, t - 1) | (1u << (t - 1)));
            }
            s->zeroes = t;
            if (t) {
                memset(s->ch, 0, sizeof s->ch);
                return 0;
            }
        }
    }
    int t;
    if (s->zero) {
        t = 0;
        s->zero = 0;
    } else {
        t = xbl_unary(b, 33);
        if (t == 16) {
            int t2 = xbl_unary(b, 33);
            if (t2 < 2)
                t += t2;
            else {
                if (t2 >= 32)
                    goto bad;
                t += (int)(xbl(b, t2 - 1) | (1u << (t2 - 1)));
            }
        }
        if (s->one) {
            s->one = t & 1;
            t = (t >> 1) + 1;
        } else {
            s->one = t & 1;
            t >>= 1;
        }
        s->zero = !s->one;
    }
    xu32 base, add;
    if (!t) {
        base = 0;
        add = (xu32)WV_GET_MED(0) - 1;
        WV_DEC_MED(0);
    } else if (t == 1) {
        base = (xu32)WV_GET_MED(0);
        add = (xu32)WV_GET_MED(1) - 1;
        WV_INC_MED(0);
        WV_DEC_MED(1);
    } else if (t == 2) {
        base = (xu32)(WV_GET_MED(0) + WV_GET_MED(1));
        add = (xu32)WV_GET_MED(2) - 1;
        WV_INC_MED(0);
        WV_INC_MED(1);
        WV_DEC_MED(2);
    } else {
        base = (xu32)(WV_GET_MED(0) + WV_GET_MED(1)) + (xu32)WV_GET_MED(2) * (xu32)(t - 2);
        add = (xu32)WV_GET_MED(2) - 1;
        WV_INC_MED(0);
        WV_INC_MED(1);
        WV_INC_MED(2);
    }
    if (add >= 0x2000000u)
        goto bad;
    xu32 r = base + wv_tail(b, add);
    if (b->pos >= b->n * 8)
        goto bad;
    return xbl(b, 1) ? ~(int)r : (int)r;
bad:
    *last = 1;
    return 0;
}
static int wv_int(xwv *s, int v) {
    xu32 bit = ((xu32)v & (xu32)s->and_) | (xu32)s->or_;
    return (int)((((xu32)v + bit) << s->shift) - bit);
}
static long long wv_apply(int w, long long a) {
    return (w * a + 512) >> 10;
}
#define WV_UPD_CLIP(w, dl, smp, in)                                                                \
    if ((smp) && (in)) {                                                                           \
        if (((smp) ^ (in)) < 0) {                                                                  \
            (w) -= (dl);                                                                           \
            if ((w) < -1024)                                                                       \
                (w) = -1024;                                                                       \
        } else {                                                                                   \
            (w) += (dl);                                                                           \
            if ((w) > 1024)                                                                        \
                (w) = 1024;                                                                        \
        }                                                                                          \
    }
/* one block: decodes its samples into o (stereo) */
static int wv_block(xdec *d, xwv *s, const xu8 *h, short *o, int maxn) {
    xu32 flags = xc_le32(h + 24), ns = xc_le32(h + 20);
    long bl = (long)xc_le32(h + 4) + 8;
    int stereo = !(flags & 4), bytes = (int)(flags & 3) + 1;
    if (flags & (8 | 0x80 | 0x80000000u)) /* hybrid (lossy), float or DSD: not here */
        return -1;
    if (ns > (xu32)maxn)
        return -1;
    memset(s->t, 0, sizeof s->t);
    memset(s->ch, 0, sizeof s->ch);
    s->nterms = 0;
    s->and_ = s->or_ = 0;
    s->shift = (int)((flags >> 13) & 31);
    s->sent = 0;
    int have_data = 0;
    long o2 = 32;
    while (o2 + 2 <= bl) {
        int id = h[o2];
        long sz = h[o2 + 1];
        o2 += 2;
        if (id & 0x80) {
            if (o2 + 2 > bl)
                break;
            sz |= (long)h[o2] << 8 | (long)h[o2 + 1] << 16;
            o2 += 2;
        }
        sz *= 2;
        long real = id & 0x40 ? sz - 1 : sz;
        if (o2 + sz > bl)
            break;
        const xu8 *m = h + o2;
        switch (id & 0x3f) {
            case 2: /* decorrelation terms */
                if (real > 16)
                    return -1;
                s->nterms = (int)real;
                for (int i = 0; i < real; i++) {
                    s->t[real - i - 1].value = (m[i] & 31) - 5;
                    s->t[real - i - 1].delta = m[i] >> 5;
                    if (s->t[real - i - 1].value < -3 || s->t[real - i - 1].value > 18 ||
                        (!stereo && s->t[real - i - 1].value < 0))
                        return -1;
                }
                break;
            case 3: { /* weights */
                int w = (int)(real >> stereo), k = 0;
                if (w > s->nterms)
                    return -1;
                for (int i = 0; i < w; i++) {
                    xwvterm *t = &s->t[s->nterms - i - 1];
                    t->wa = (signed char)m[k++] * 8;
                    if (t->wa > 0)
                        t->wa += (t->wa + 64) >> 7;
                    if (stereo) {
                        t->wb = (signed char)m[k++] * 8;
                        if (t->wb > 0)
                            t->wb += (t->wb + 64) >> 7;
                    }
                }
                break;
            }
            case 4: { /* samples */
                long k = 0;
#define WV_E() (k + 2 <= real ? wv_exp2((short)xc_le16(m + (k += 2) - 2)) : 0)
                for (int i = s->nterms - 1; i >= 0 && k < real; i--) {
                    xwvterm *t = &s->t[i];
                    if (t->value > 8) {
                        t->sa[0] = WV_E();
                        t->sa[1] = WV_E();
                        if (stereo) {
                            t->sb[0] = WV_E();
                            t->sb[1] = WV_E();
                        }
                    } else if (t->value < 0) {
                        t->sa[0] = WV_E();
                        t->sb[0] = WV_E();
                    } else
                        for (int j = 0; j < t->value; j++) {
                            t->sa[j] = WV_E();
                            if (stereo)
                                t->sb[j] = WV_E();
                        }
                }
#undef WV_E
                break;
            }
            case 5: /* entropy */
                if (real < 6 * (stereo + 1))
                    return -1;
                for (int c = 0; c <= stereo; c++)
                    for (int j = 0; j < 3; j++)
                        s->ch[c].median[j] = wv_exp2((short)xc_le16(m + c * 6 + j * 2));
                break;
            case 9: /* int32 info */
                if (real >= 4) {
                    s->sent = m[0];
                    if (m[1])
                        s->shift = m[1];
                    else if (m[2]) {
                        s->and_ = s->or_ = 1;
                        s->shift = m[2];
                    } else if (m[3]) {
                        s->and_ = 1;
                        s->shift = m[3];
                    }
                }
                break;
            case 10: /* the samples themselves */
                s->b = (xbr){m, real, 0};
                have_data = 1;
                break;
            case 0x27: /* own sample rate */
                if (real >= 3)
                    d->srate = (int)(m[0] | m[1] << 8 | m[2] << 16);
                break;
        }
        o2 += sz;
    }
    if (!have_data || s->sent || s->shift > 31)
        return -1;
    s->zero = s->one = s->zeroes = 0;
    int pos = 0, bits = bytes * 8;
    for (xu32 i = 0; i < ns; i++) {
        int last, L, R = 0;
        L = wv_value(s, 0, &last);
        if (last)
            return (int)i;
        if (stereo) {
            R = wv_value(s, 1, &last);
            if (last)
                return (int)i;
        }
        for (int k = 0; k < s->nterms; k++) {
            xwvterm *t = &s->t[k];
            int tv = t->value, j;
            if (tv > 0) {
                int A, B = 0;
                if (tv > 8) {
                    if (tv & 1) {
                        A = (int)(2u * (xu32)t->sa[0] - (xu32)t->sa[1]);
                        B = (int)(2u * (xu32)t->sb[0] - (xu32)t->sb[1]);
                    } else {
                        A = (int)(3u * (xu32)t->sa[0] - (xu32)t->sa[1]) >> 1;
                        B = (int)(3u * (xu32)t->sb[0] - (xu32)t->sb[1]) >> 1;
                    }
                    t->sa[1] = t->sa[0];
                    t->sb[1] = t->sb[0];
                    j = 0;
                } else {
                    A = t->sa[pos];
                    B = t->sb[pos];
                    j = (pos + tv) & 7;
                }
                int L2 = (int)(L + wv_apply(t->wa, A));
                if (A && L)
                    t->wa -= ((((L ^ A) >> 30) & 2) - 1) * t->delta;
                t->sa[j] = L = L2;
                if (stereo) {
                    int R2 = (int)(R + wv_apply(t->wb, B));
                    if (B && R)
                        t->wb -= ((((R ^ B) >> 30) & 2) - 1) * t->delta;
                    t->sb[j] = R = R2;
                }
            } else if (tv == -1) {
                int L2 = (int)(L + wv_apply(t->wa, t->sa[0]));
                WV_UPD_CLIP(t->wa, t->delta, t->sa[0], L);
                L = L2;
                int R2 = (int)(R + wv_apply(t->wb, L2));
                WV_UPD_CLIP(t->wb, t->delta, L2, R);
                R = R2;
                t->sa[0] = R;
            } else {
                int R2 = (int)(R + wv_apply(t->wb, t->sb[0]));
                WV_UPD_CLIP(t->wb, t->delta, t->sb[0], R);
                R = R2;
                if (tv == -3) {
                    R2 = t->sa[0];
                    t->sa[0] = R;
                }
                int L2 = (int)(L + wv_apply(t->wa, R2));
                WV_UPD_CLIP(t->wa, t->delta, R2, L);
                L = L2;
                t->sb[0] = L;
            }
        }
        pos = (pos + 1) & 7;
        if (stereo && (flags & 0x10)) {
            R = (int)((xu32)R - (xu32)(L >> 1));
            L = (int)((xu32)L + (xu32)R);
        }
        o[i * 2] = xc_to16(wv_int(s, L), bits);
        o[i * 2 + 1] = stereo ? xc_to16(wv_int(s, R), bits) : o[i * 2];
    }
    return (int)ns;
}
static int wv_frame(xdec *d, long idx, short *o) {
    xwv *s = d->s;
    if (idx >= s->nb)
        return 0;
    const xu8 *h = d->f + s->bo[idx];
    return wv_block(d, s, h, o, (int)d->flen);
}
static void wv_seek(xdec *d, long fr) {
    xwv *s = d->s;
    long lo = 0, hi = s->nb - 1;
    while (lo < hi) { /* the last block that starts at or before fr */
        long m = (lo + hi + 1) / 2;
        if ((long)xc_le32(d->f + s->bo[m] + 16) <= fr)
            lo = m;
        else
            hi = m - 1;
    }
    d->fidx = lo;
    d->skip = (int)(fr - (long)xc_le32(d->f + s->bo[lo] + 16));
    if (d->skip < 0)
        d->skip = 0;
    d->have = d->at = 0;
}
static int wv_open(xdec *d) {
    const xu8 *f = d->f;
    long n = d->n;
    if (n < 32 || memcmp(f, "wvpk", 4))
        return 0;
    xwv *s = malloc(sizeof *s);
    if (!s)
        return 0;
    memset(s, 0, sizeof *s);
    /* index: the blocks that start a frame and carry samples */
    long cap = 1024, o = 0, maxs = 0;
    s->bo = malloc((size_t)cap * sizeof(long));
    xu32 first = 0;
    int got = 0;
    while (s->bo && o + 32 <= n && !memcmp(f + o, "wvpk", 4)) {
        long bl = (long)xc_le32(f + o + 4) + 8;
        xu32 fl = xc_le32(f + o + 24), ns = xc_le32(f + o + 20);
        if (bl < 32 || o + bl > n)
            break;
        if ((fl & 0x800) && ns) {
            if (!got) {
                first = fl;
                got = 1;
                d->total = (long)xc_le32(f + o + 12);
                if (xc_le32(f + o + 12) == 0xffffffffu)
                    d->total = 0;
            }
            if (s->nb == cap) {
                long *nb = realloc(s->bo, (size_t)cap * 2 * sizeof(long));
                if (!nb)
                    break;
                s->bo = nb;
                cap *= 2;
            }
            s->bo[s->nb++] = o;
            if ((long)ns > maxs)
                maxs = (long)ns;
        }
        o += bl;
    }
    if (!got || !s->bo || maxs > 1 << 20)
        return 0;
    if (first & (8 | 0x80 | 0x80000000u))
        return 0; /* lossy hybrid, float or DSD */
    int ri = (int)((first >> 23) & 15);
    d->srate = ri < 15 ? wv_rates[ri] : 0;
    d->chans = (first & 4) && !(first & 0x40000000) ? 1 : 2;
    d->bits = (int)(first & 3) * 8 + 8;
    d->s = s;
    d->name = "WavPack";
    d->frame = wv_frame;
    d->fcount = s->nb;
    d->flen = maxs;
    if (!xf_init(d, maxs))
        return 0;
    d->seek = wv_seek;
    if (ri == 15) { /* the rate is in the first block */
        wv_block(d, s, f + s->bo[0], d->buf, (int)maxs);
    }
    if (!d->total) {
        const xu8 *h = f + s->bo[s->nb - 1];
        d->total = (long)xc_le32(h + 16) + (long)xc_le32(h + 20);
    }
    xc_rate(d);
    return d->srate > 0;
}

/* ---------- DSD: one bit per sample at 2.8 MHz and more, filtered down to 44.1 or 48 kHz ----------
 * Stage 1 works on whole bytes with tables (8 bits at once) and gives twice the end rate, stage 2
 * halves that with a sharp filter. The file is read in parts, a DSD file is far too big. */
#define DSD_K 512 /* end frames per round */
typedef struct {
    int nch, dff, rev, d1, t1, t2;
    long data, nbytes, block, pos; /* pos: bytes per channel done */
    float *lut, *h2, *y1[2];
    xu8 *raw, *hist[2];
    short out[DSD_K * 2];
    int have, at;
} xdsd;
static long xc_fetch(xdec *d, void *b, long off, long len) {
    if (d->f) {
        if (off < 0 || off >= d->n)
            return 0;
        if (len > d->n - off)
            len = d->n - off;
        memcpy(b, d->f + off, (size_t)len);
        return len;
    }
    return d->fetch ? d->fetch(d, b, off, len) : 0;
}
static double xc_i0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 40; k++) {
        t *= (x / (2 * k)) * (x / (2 * k));
        s += t;
    }
    return s;
}
/* low pass with n taps, cut-off fc (part of the sample rate), Kaiser window */
static void xc_lowpass(float *h, int n, double fc, double beta) {
    double sum = 0, m = (n - 1) / 2.0;
    for (int i = 0; i < n; i++) {
        double x = i - m, r = 2 * (x / m < -1 ? -1 : x / m > 1 ? 1 : x / m) / 2;
        double s = x == 0 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
        double w = xc_i0(beta * sqrt(1 - r * r)) / xc_i0(beta);
        h[i] = (float)(s * w);
        sum += h[i];
    }
    for (int i = 0; i < n; i++)
        h[i] = (float)(h[i] / sum);
}
/* bytes [pos, pos+len) of channel c into b */
static int dsd_get(xdec *d, xdsd *s, int c, long pos, long len, xu8 *b) {
    if (s->dff) { /* bytes of the channels in turn */
        long k = len * s->nch, got = xc_fetch(d, s->raw, s->data + pos * s->nch, k);
        for (long i = 0; i < len; i++)
            b[i] = i * s->nch + c < got ? s->raw[i * s->nch + c] : 0x69;
        return 1;
    }
    while (len > 0) { /* blocks of 4096 bytes per channel */
        long bl = pos / s->block, w = pos % s->block, k = s->block - w;
        if (k > len)
            k = len;
        long got = xc_fetch(d, b, s->data + (bl * s->nch + c) * s->block + w, k);
        if (got < k)
            memset(b + (got > 0 ? got : 0), 0x69, (size_t)(k - (got > 0 ? got : 0)));
        b += k;
        pos += k;
        len -= k;
    }
    return 1;
}
static int dsd_round(xdec *d, xdsd *s) {
    long per = (long)DSD_K * 2 * s->d1; /* bytes per channel this round */
    if (s->pos >= s->nbytes)
        return 0;
    if (per > s->nbytes - s->pos)
        per = s->nbytes - s->pos;
    int frames = (int)(per / (2 * s->d1));
    if (!frames)
        return 0;
    per = (long)frames * 2 * s->d1;
    for (int c = 0; c < s->nch && c < 2; c++) {
        xu8 *h = s->hist[c];
        dsd_get(d, s, c, s->pos, per, h + s->t1);
        if (s->rev)
            for (long i = 0; i < per; i++) {
                xu8 v = h[s->t1 + i];
                v = (xu8)((v & 0xF0) >> 4 | (v & 0x0F) << 4);
                v = (xu8)((v & 0xCC) >> 2 | (v & 0x33) << 2);
                h[s->t1 + i] = (xu8)((v & 0xAA) >> 1 | (v & 0x55) << 1);
            }
        float *y = s->y1[c];
        for (int j = 0; j < frames * 2; j++) { /* stage 1 */
            const xu8 *e = h + s->t1 + (long)(j + 1) * s->d1 - 1; /* newest byte */
            float a = 0;
            for (int k = 0; k < s->t1; k++)
                a += s->lut[k * 256 + e[-k]];
            y[s->t2 + j] = a;
        }
        for (int j = 0; j < frames; j++) { /* stage 2 */
            const float *e = y + s->t2 + 2 * j + 1;
            float a = 0;
            for (int k = 0; k < s->t2; k++)
                a += s->h2[k] * e[-k];
            long v = (long)(a * 46000.0f);
            s->out[j * 2 + c] = xc_clip(v);
        }
        memmove(h, h + per, (size_t)s->t1);
        memmove(y, y + frames * 2, (size_t)s->t2 * sizeof(float));
    }
    if (s->nch == 1)
        for (int j = 0; j < frames; j++)
            s->out[j * 2 + 1] = s->out[j * 2];
    s->pos += per;
    s->have = frames;
    s->at = 0;
    return frames;
}
static int dsd_read(xdec *d, short *o, int n) {
    xdsd *s = d->s;
    int g = 0;
    while (g < n) {
        if (s->at >= s->have && !dsd_round(d, s))
            break;
        int k = s->have - s->at;
        if (k > n - g)
            k = n - g;
        memcpy(o + g * 2, s->out + s->at * 2, (size_t)k * 4);
        s->at += k;
        g += k;
    }
    return g;
}
static void dsd_seek(xdec *d, long fr) {
    xdsd *s = d->s;
    s->pos = fr * 2 * s->d1;
    if (s->pos > s->nbytes)
        s->pos = s->nbytes;
    for (int c = 0; c < 2; c++) {
        memset(s->hist[c], 0x69, (size_t)s->t1); /* silence in DSD */
        memset(s->y1[c], 0, (size_t)s->t2 * sizeof(float));
    }
    s->have = s->at = 0;
}
static int dsd_setup(xdec *d, xdsd *s, long rate) {
    long base = rate % 44100 == 0 ? 44100 : rate % 48000 == 0 ? 48000 : 0;
    if (!base || rate < base * 32 || rate > base * 1024 || s->nch < 1 || s->nch > 8)
        return 0;
    s->d1 = (int)(rate / (2 * base) / 8); /* bytes per stage-1 sample */
    s->t1 = 12 * s->d1;
    s->t2 = 128;
    s->lut = malloc((size_t)s->t1 * 256 * sizeof(float));
    float *h1 = malloc((size_t)s->t1 * 8 * sizeof(float));
    s->h2 = malloc((size_t)s->t2 * sizeof(float));
    long per = (long)DSD_K * 2 * s->d1;
    s->raw = malloc((size_t)per * s->nch + 16);
    for (int c = 0; c < 2; c++) {
        s->hist[c] = malloc((size_t)(s->t1 + per));
        s->y1[c] = malloc((size_t)(s->t2 + DSD_K * 2) * sizeof(float));
        if (!s->hist[c] || !s->y1[c])
            return 0;
    }
    if (!s->lut || !h1 || !s->h2 || !s->raw)
        return 0;
    xc_lowpass(h1, s->t1 * 8, 0.95 * base / rate, 8);
    xc_lowpass(s->h2, s->t2, 21000.0 / (2 * base), 9);
    for (int k = 0; k < s->t1; k++)
        for (int b = 0; b < 256; b++) {
            float a = 0;
            for (int j = 0; j < 8; j++)
                a += (b >> j & 1) ? h1[k * 8 + j] : -h1[k * 8 + j];
            s->lut[k * 256 + b] = a;
        }
    free(h1);
    d->s = s;
    d->srate = (int)base;
    d->chans = s->nch;
    d->bits = 1;
    d->total = s->nbytes / (2 * s->d1);
    d->kbps = (int)(rate * s->nch / 1000);
    d->read = dsd_read;
    d->seek = dsd_seek;
    dsd_seek(d, 0);
    return 1;
}
static int dsd_open(xdec *d, const xu8 *h, long hn) {
    xdsd *s = malloc(sizeof *s);
    if (!s)
        return 0;
    memset(s, 0, sizeof *s);
    if (hn >= 92 && !memcmp(h, "DSD ", 4) && !memcmp(h + 28, "fmt ", 4)) { /* .dsf */
        long fl = (long)xc_le64(h + 32);
        if (xc_le32(h + 40) != 1 || xc_le32(h + 44) != 0 || fl < 52 || 28 + fl + 12 > hn)
            return 0;
        s->nch = (int)xc_le32(h + 52);
        long rate = (long)xc_le32(h + 56);
        int bps = (int)xc_le32(h + 60);
        s->nbytes = (long)(xc_le64(h + 64) / 8);
        s->block = (long)xc_le32(h + 72);
        const xu8 *dc = h + 28 + fl;
        if (memcmp(dc, "data", 4) || s->block < 16 || s->block > 1 << 20 ||
            (bps != 1 && bps != 8))
            return 0;
        s->data = 28 + fl + 12;
        s->rev = bps == 1;
        d->name = "DSD (DSF)";
        return dsd_setup(d, s, rate);
    }
    if (hn >= 16 && !memcmp(h, "FRM8", 4) && !memcmp(h + 12, "DSD ", 4)) { /* .dff */
        long o = 16, rate = 0;
        int dsd = 0;
        while (o + 12 <= hn) {
            long l = (long)xc_be64(h + o + 4);
            const xu8 *c = h + o + 12;
            if (l < 0)
                return 0;
            if (!memcmp(h + o, "PROP", 4)) {
                long q = 4, e = l;
                while (q + 12 <= e && o + 12 + q + 12 <= hn) {
                    long sl = (long)xc_be64(c + q + 4);
                    const xu8 *sc = c + q + 12;
                    if (sl < 0 || o + 12 + q + 12 + sl > hn)
                        return 0;
                    if (!memcmp(c + q, "FS  ", 4) && sl >= 4)
                        rate = (long)xc_be32(sc);
                    if (!memcmp(c + q, "CHNL", 4) && sl >= 2)
                        s->nch = (int)xc_be16(sc);
                    if (!memcmp(c + q, "CMPR", 4) && sl >= 4 && !memcmp(sc, "DSD ", 4))
                        dsd = 1;
                    q += 12 + sl + (sl & 1);
                }
            }
            if (!memcmp(h + o, "DSD ", 4)) {
                if (!dsd || s->nch < 1)
                    return 0;
                s->data = o + 12;
                s->nbytes = l / s->nch;
                s->dff = 1;
                d->name = "DSD (DFF)";
                return dsd_setup(d, s, rate);
            }
            o += 12 + l + (l & 1);
        }
    }
    return 0;
}

/* ---------- Matroska audio: the packets of the first audio track, put together again as a
 * stream the decoders know (MP3, FLAC, Ogg Vorbis) or decoded here (PCM, ALAC, AC3) ---------- */
typedef struct {
    long off;
    int len;
} xpkt;
typedef struct {
    const xu8 *f;
    long n;
    long tscale;
    double dur;
    int track, found, chans, bits;
    double rate;
    char codec[32];
    const xu8 *priv, *strip;
    long privn, stripn;
    xpkt *pk;
    long np, cap;
    char *title, *artist;
} xmka;
/* a variable length number; id keeps the length bits. -2 = damaged, -1 = unknown size */
static long long mka_vint(const xu8 *p, long n, int *len, int id) {
    if (n < 1 || !p[0])
        return -2;
    int l = 1;
    while (!(p[0] & (0x80 >> (l - 1))))
        l++;
    if (l > 8 || l > n)
        return -2;
    long long v = id ? p[0] : p[0] & (0xFF >> l);
    int ones = (p[0] & (0xFF >> l)) == (0xFF >> l);
    for (int i = 1; i < l; i++) {
        v = v << 8 | p[i];
        ones &= p[i] == 0xFF;
    }
    *len = l;
    return !id && ones ? -1 : v;
}
static xu64 mka_uint(const xu8 *p, long l) {
    xu64 v = 0;
    for (long i = 0; i < l && i < 8; i++)
        v = v << 8 | p[i];
    return v;
}
static double mka_float(const xu8 *p, long l) {
    if (l == 4) {
        union {
            xu32 i;
            float f;
        } u;
        u.i = xc_be32(p);
        return u.f;
    }
    if (l == 8) {
        union {
            xu64 i;
            double f;
        } u;
        u.i = xc_be64(p);
        return u.f;
    }
    return 0;
}
static void mka_str(char *o, const xu8 *p, long l) {
    int k = 0;
    for (long i = 0; i < l && k < 69 && p[i]; i++) {
        int c = p[i];
        if (c >= 0xC0 && i + 1 < l) { /* UTF-8: German letters, the rest as ? */
            c = (c & 31) << 6 | (p[++i] & 63);
            c = c == 0xE4   ? 0x84
                : c == 0xF6 ? 0x94
                : c == 0xFC ? 0x81
                : c == 0xC4 ? 0x8E
                : c == 0xD6 ? 0x99
                : c == 0xDC ? 0x9A
                : c == 0xDF ? 0xE1
                            : '?';
        } else if (c >= 0x80)
            c = '?';
        o[k++] = (char)c;
    }
    o[k] = 0;
}
static int mka_add(xmka *m, long off, long len) {
    if (len <= 0 || off < 0 || off + len > m->n)
        return 1;
    if (m->np == m->cap) {
        long nc = m->cap ? m->cap * 2 : 4096;
        xpkt *q = realloc(m->pk, (size_t)nc * sizeof(xpkt));
        if (!q)
            return 0;
        m->pk = q;
        m->cap = nc;
    }
    m->pk[m->np].off = off;
    m->pk[m->np].len = (int)len;
    m->np++;
    return 1;
}
static void mka_block(xmka *m, long o, long e) {
    int l;
    long long tr = mka_vint(m->f + o, e - o, &l, 0);
    if (tr != m->track || o + l + 3 > e)
        return;
    o += l + 2;
    int fl = m->f[o++];
    int lace = (fl >> 1) & 3;
    if (!lace) {
        mka_add(m, o, e - o);
        return;
    }
    int cnt = m->f[o++] + 1;
    long sz[256], sum = 0;
    if (lace == 1) { /* Xiph */
        for (int i = 0; i < cnt - 1; i++) {
            long v = 0;
            int b;
            do {
                if (o >= e)
                    return;
                b = m->f[o++];
                v += b;
            } while (b == 255);
            sz[i] = v;
            sum += v;
        }
    } else if (lace == 3) { /* EBML */
        long long v = mka_vint(m->f + o, e - o, &l, 0);
        if (v < 0)
            return;
        o += l;
        sz[0] = (long)v;
        sum = sz[0];
        for (int i = 1; i < cnt - 1; i++) {
            long long dlt = mka_vint(m->f + o, e - o, &l, 0);
            if (dlt < 0)
                return;
            o += l;
            dlt -= (1LL << (7 * l - 1)) - 1;
            sz[i] = sz[i - 1] + (long)dlt;
            if (sz[i] < 0)
                return;
            sum += sz[i];
        }
    } else { /* fixed */
        for (int i = 0; i < cnt - 1; i++)
            sz[i] = (e - o) / cnt;
        sum = (e - o) / cnt * (cnt - 1);
    }
    if (o + sum > e)
        return;
    sz[cnt - 1] = e - o - sum;
    for (int i = 0; i < cnt; i++) {
        mka_add(m, o, sz[i]);
        o += sz[i];
    }
}
static int mka_top(long long id) {
    return id == 0x1F43B675 || id == 0x1C53BB6B || id == 0x1254C367 || id == 0x1654AE6B ||
           id == 0x1549A966 || id == 0x114D9B74 || id == 0x1941A469 || id == 0x1043A770;
}
/* the elements between o and e; returns where it stopped */
static long mka_walk(xmka *m, long o, long e, int depth, long long parent) {
    const xu8 *f = m->f;
    /* what is being collected in a TrackEntry and in a SimpleTag */
    long long tnum = -1, ttype = 0;
    const xu8 *tag_name = 0;
    long tag_nl = 0;
    while (o < e) {
        int il, sl;
        long long id = mka_vint(f + o, e - o, &il, 1);
        if (id < 0)
            return e;
        long long sz = mka_vint(f + o + il, e - o - il, &sl, 0);
        if (sz == -2)
            return e;
        if (depth >= 2 && parent == 0x1F43B675 && mka_top(id))
            return o; /* a cluster of unknown size ends here */
        long b = o + il + sl, be = sz < 0 || b + sz > e ? e : b + (long)sz;
        const xu8 *c = f + b;
        long cl = be - b;
        switch (id) {
            case 0x18538067: /* Segment */
            case 0x1654AE6B: /* Tracks */
            case 0x1F43B675: /* Cluster */
            case 0xA0:       /* BlockGroup */
            case 0xE1:       /* Audio */
            case 0x6D80:     /* ContentEncodings */
            case 0x6240:     /* ContentEncoding */
            case 0x5034:     /* ContentCompression */
            case 0x1254C367: /* Tags */
            case 0x7373:     /* Tag */
            case 0x1549A966: /* Info */
                if (depth < 8) {
                    long r = mka_walk(m, b, be, depth + 1, id);
                    if (sz < 0) {
                        o = r;
                        continue;
                    }
                }
                break;
            case 0xAE: /* TrackEntry: the first audio track counts */
                if (m->found == 1)
                    break;
                m->found = 0;
                m->codec[0] = 0;
                m->priv = m->strip = 0;
                m->privn = m->stripn = 0;
                m->rate = 8000;
                m->chans = 1;
                m->bits = 0;
                mka_walk(m, b, be, depth + 1, id);
                m->found = m->found == 2;
                break;
            case 0xD7: /* TrackNumber */
                tnum = (long long)mka_uint(c, cl);
                m->track = (int)tnum;
                if (ttype == 2)
                    m->found = 2;
                break;
            case 0x83: /* TrackType: 2 = audio */
                ttype = (long long)mka_uint(c, cl);
                if (ttype == 2 && tnum >= 0)
                    m->found = 2;
                break;
            case 0x86: { /* CodecID */
                int k = 0;
                for (; k < cl && k < 31; k++)
                    m->codec[k] = (char)c[k];
                m->codec[k] = 0;
                break;
            }
            case 0x63A2:
                m->priv = c;
                m->privn = cl;
                break;
            case 0x5255: /* ContentCompSettings: header stripping */
                m->strip = c;
                m->stripn = cl;
                break;
            case 0xB5:
                m->rate = mka_float(c, cl);
                break;
            case 0x9F:
                m->chans = (int)mka_uint(c, cl);
                break;
            case 0x6264:
                m->bits = (int)mka_uint(c, cl);
                break;
            case 0x2AD7B1:
                m->tscale = (long)mka_uint(c, cl);
                break;
            case 0x4489:
                m->dur = mka_float(c, cl);
                break;
            case 0xA3: /* SimpleBlock */
            case 0xA1: /* Block */
                if (m->found == 1)
                    mka_block(m, b, be);
                break;
            case 0x67C8: /* SimpleTag */
                tag_name = 0;
                mka_walk(m, b, be, depth + 1, id);
                break;
            case 0x45A3: /* TagName */
                tag_name = c;
                tag_nl = cl;
                break;
            case 0x4487: /* TagString */
                if (tag_name && tag_nl == 5 && !memcmp(tag_name, "TITLE", 5) && m->title &&
                    !m->title[0])
                    mka_str(m->title, c, cl);
                if (tag_name && tag_nl == 6 && !memcmp(tag_name, "ARTIST", 6) && m->artist &&
                    !m->artist[0])
                    mka_str(m->artist, c, cl);
                break;
        }
        if (sz < 0)
            return e;
        o = be;
    }
    return o;
}
/* Ogg pages for Vorbis */
static xu32 ogg_crc(const xu8 *p, long n) {
    xu32 c = 0;
    for (long i = 0; i < n; i++) {
        c ^= (xu32)p[i] << 24;
        for (int k = 0; k < 8; k++)
            c = c & 0x80000000u ? c << 1 ^ 0x04c11db7u : c << 1;
    }
    return c;
}
typedef struct {
    xu8 *o;
    long n, cap;
    xu32 seq;
    xu8 seg[255];
    int nseg;
    long body; /* start of the data of the page in progress, in the temporary buffer */
    xu8 *tmp;
    long tn;
    long long gran;
    int cont, bos;
} xogg;
static int ogg_flush(xogg *g, int eos) {
    if (!g->nseg && !eos)
        return 1;
    long need = 27 + g->nseg + g->tn;
    if (g->n + need > g->cap)
        return 0;
    xu8 *p = g->o + g->n;
    memcpy(p, "OggS", 4);
    p[4] = 0;
    p[5] = (xu8)((g->cont ? 1 : 0) | (g->bos ? 2 : 0) | (eos ? 4 : 0));
    for (int i = 0; i < 8; i++)
        p[6 + i] = (xu8)((xu64)g->gran >> (8 * i));
    memcpy(p + 14, "XZN1", 4);
    for (int i = 0; i < 4; i++)
        p[18 + i] = (xu8)(g->seq >> (8 * i));
    memset(p + 22, 0, 4);
    p[26] = (xu8)g->nseg;
    memcpy(p + 27, g->seg, (size_t)g->nseg);
    memcpy(p + 27 + g->nseg, g->tmp, (size_t)g->tn);
    xu32 c = ogg_crc(p, need);
    for (int i = 0; i < 4; i++)
        p[22 + i] = (xu8)(c >> (8 * i));
    g->n += need;
    g->seq++;
    g->nseg = 0;
    g->tn = 0;
    g->bos = 0;
    g->gran = -1;
    return 1;
}
static int ogg_packet(xogg *g, const xu8 *d, long l, long long gran, int own_page) {
    long done = 0;
    g->cont = 0;
    for (;;) {
        long k = l - done > 255 ? 255 : l - done;
        if (g->nseg == 255 || g->tn + k > 60000) {
            int midpacket = done > 0 || k == 255;
            (void)midpacket;
            if (!ogg_flush(g, 0))
                return 0;
            g->cont = done > 0;
        }
        g->seg[g->nseg++] = (xu8)k;
        memcpy(g->tmp + g->tn, d + done, (size_t)k);
        g->tn += k;
        done += k;
        if (k < 255)
            break;
        if (done == l) { /* a multiple of 255: an empty segment ends it */
            if (g->nseg == 255) {
                if (!ogg_flush(g, 0))
                    return 0;
                g->cont = 1;
            }
            g->seg[g->nseg++] = 0;
            break;
        }
    }
    g->gran = gran;
    g->cont = 0;
    if (own_page)
        return ogg_flush(g, 0);
    if (g->tn > 4000)
        return ogg_flush(g, 0);
    return 1;
}
/* the block sizes of the Vorbis modes, read from the end of the setup header */
static int vorbis_modes(const xu8 *s, long n, int *flags) {
    long top = n * 8 - 1;
    while (top >= 0 && !(s[top >> 3] >> (top & 7) & 1))
        top--;
    if (top < 0) /* top is the framing bit */
        return 0;
    long p = top; /* bits below p */
#define VB(i) ((s[(i) >> 3] >> ((i) & 7)) & 1)
    int cand = 0, fl[64];
    long at[65];
    at[0] = p;
    while (cand < 64 && p >= 41) {
        long q = p - 41; /* this mode: bits q .. p-1, the blockflag at q */
        int mapping = 0, zeros = 1;
        for (int i = 0; i < 8; i++)
            mapping |= VB(q + 33 + i) << i;
        for (int i = 0; i < 32; i++)
            if (VB(q + 1 + i))
                zeros = 0;
        if (!zeros || mapping > 63)
            break;
        fl[cand++] = VB(q);
        p = q;
        at[cand] = p;
    }
    for (int k = cand; k >= 1; k--) { /* k modes: the 6 bits before must say k-1 */
        long q = at[k] - 6;
        if (q < 0)
            continue;
        int v = 0;
        for (int i = 0; i < 6; i++)
            v |= VB(q + i) << i;
        if (v == k - 1) {
            for (int i = 0; i < k; i++)
                flags[i] = fl[k - 1 - i];
            return k;
        }
    }
#undef VB
    return 0;
}
static long mka_build_vorbis(xmka *m, xu8 **out) {
    const xu8 *p = m->priv;
    long n = m->privn;
    if (!p || n < 3 || p[0] != 2)
        return 0;
    long o = 1, l1 = 0, l2 = 0;
    int b;
    do {
        if (o >= n)
            return 0;
        b = p[o++];
        l1 += b;
    } while (b == 255);
    do {
        if (o >= n)
            return 0;
        b = p[o++];
        l2 += b;
    } while (b == 255);
    if (o + l1 + l2 > n || l1 < 30)
        return 0;
    const xu8 *h1 = p + o, *h2 = h1 + l1, *h3 = h2 + l2;
    long l3 = n - o - l1 - l2;
    int bs0 = 1 << (h1[28] & 15), bs1 = 1 << (h1[28] >> 4), flags[64];
    int nm = vorbis_modes(h3, l3, flags), mb = 0;
    while ((1 << mb) < nm)
        mb++;
    long total = n + 64;
    for (long i = 0; i < m->np; i++)
        total += m->pk[i].len;
    xogg g;
    memset(&g, 0, sizeof g);
    g.cap = total + total / 200 + (m->np + 300) * 30 + 4096;
    g.o = malloc((size_t)g.cap);
    g.tmp = malloc(70000);
    if (!g.o || !g.tmp)
        return 0;
    g.bos = 1;
    g.gran = 0;
    if (!ogg_packet(&g, h1, l1, 0, 1) || !ogg_packet(&g, h2, l2, 0, 0) ||
        !ogg_packet(&g, h3, l3, 0, 1))
        return 0;
    long long gran = 0;
    int prev = -1;
    for (long i = 0; i < m->np; i++) {
        const xu8 *d = m->f + m->pk[i].off;
        int bs = bs0;
        if (nm && m->pk[i].len > 0 && !(d[0] & 1)) {
            int mode = (d[0] >> 1) & ((1 << mb) - 1);
            bs = mode < nm && flags[mode] ? bs1 : bs0;
        }
        if (prev >= 0)
            gran += (prev + bs) / 4;
        prev = bs;
        if (!ogg_packet(&g, d, m->pk[i].len, gran, 0))
            return 0;
    }
    if (!ogg_flush(&g, 1))
        return 0;
    /* the last page closes the stream */
    *out = g.o;
    return g.n;
}
static int mka_alac_frame(xdec *d, long idx, short *o) {
    xcaf *c = d->s;
    if (idx >= c->np)
        return 0;
    int k = alac_packet(&c->a, d->f + c->pk[idx], c->pk[idx + 1] - c->pk[idx], o);
    return k < 0 ? 0 : k;
}
static int ac3_open(xdec *d);
/* 0: not usable, 1: ready (PCM, ALAC, AC3), 2: d->f is now a new stream to look at again */
static int mka_open(xdec *d) {
    const xu8 *f = d->f;
    long n = d->n;
    if (n < 16 || xc_be32(f) != 0x1A45DFA3)
        return 0;
    xmka *m = malloc(sizeof *m);
    if (!m)
        return 0;
    memset(m, 0, sizeof *m);
    m->f = f;
    m->n = n;
    m->tscale = 1000000;
    m->title = d->title;
    m->artist = d->artist;
    mka_walk(m, 0, n, 0, 0);
    if (m->found != 1 || !m->np)
        return 0;
    /* header stripping: the removed bytes go back in front of every packet */
    long sl = m->stripn, total = 0;
    for (long i = 0; i < m->np; i++)
        total += m->pk[i].len + sl;
    const char *cd = m->codec;
    if (!strcmp(cd, "A_VORBIS")) {
        xu8 *o;
        long l = mka_build_vorbis(m, &o);
        if (!l)
            return 0;
        d->f = o;
        d->n = l;
        return 2;
    }
    int flac = !strcmp(cd, "A_FLAC");
    long pre = flac ? m->privn : 0;
    xu8 *o = malloc((size_t)(total + pre + 16));
    if (!o)
        return 0;
    long at = 0;
    if (flac) {
        if (!m->priv || m->privn < 4 || memcmp(m->priv, "fLaC", 4))
            return 0;
        memcpy(o, m->priv, (size_t)m->privn);
        at = m->privn;
    }
    long *offs = 0;
    int alac = !strcmp(cd, "A_ALAC");
    if (alac && !(offs = malloc((size_t)(m->np + 1) * sizeof(long))))
        return 0;
    for (long i = 0; i < m->np; i++) {
        if (offs)
            offs[i] = at;
        if (sl)
            memcpy(o + at, m->strip, (size_t)sl);
        memcpy(o + at + sl, f + m->pk[i].off, (size_t)m->pk[i].len);
        at += sl + m->pk[i].len;
    }
    if (offs)
        offs[m->np] = at;
    d->f = o;
    d->n = at;
    if (flac || !strncmp(cd, "A_MPEG/L", 8))
        return 2;
    d->srate = (int)(m->rate + 0.5);
    d->chans = m->chans;
    if (!strcmp(cd, "A_AC3"))
        return ac3_open(d);
    if (alac) {
        xcaf *c = malloc(sizeof *c);
        if (!c || !m->priv || !alac_config(&c->a, m->priv, m->privn))
            return 0;
        c->pk = offs;
        c->np = m->np;
        c->prime = 0;
        d->s = c;
        d->bits = c->a.depth;
        d->name = "ALAC";
        d->frame = mka_alac_frame;
        d->fcount = m->np;
        d->flen = c->a.flen;
        d->total = m->dur > 0 ? (long)(m->dur * m->tscale / 1e9 * d->srate) : m->np * d->flen;
        if (!xf_init(d, c->a.flen))
            return 0;
        xc_rate(d);
        return 1;
    }
    if (!strncmp(cd, "A_PCM/", 6)) {
        int be = !strcmp(cd, "A_PCM/INT/BIG"), flt = !strcmp(cd, "A_PCM/FLOAT/IEEE");
        int bits = m->bits ? m->bits : flt ? 32 : 16;
        d->name = "PCM";
        return xpcm_setup(d, 0, at, bits, be, flt, bits == 8);
    }
    return 0;
}

/* ---------- AC3 (Dolby Digital), after ATSC A/52. The tables latab and hth are from the
 * standard; the rest of the bit allocation is computed as it describes. ---------- */
static const xu8 ac3_latab[256] = {
    0x40, 0x3f, 0x3e, 0x3d, 0x3c, 0x3b, 0x3a, 0x39, 0x38, 0x37, 0x36, 0x35, 0x34, 0x34, 0x33, 0x32,
    0x31, 0x30, 0x2f, 0x2f, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x29, 0x29, 0x28, 0x27, 0x26, 0x26,
    0x25, 0x24, 0x24, 0x23, 0x23, 0x22, 0x21, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c,
    0x1c, 0x1b, 0x1b, 0x1a, 0x1a, 0x19, 0x19, 0x18, 0x18, 0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x15,
    0x14, 0x14, 0x13, 0x13, 0x13, 0x12, 0x12, 0x12, 0x11, 0x11, 0x11, 0x10, 0x10, 0x10, 0x0f, 0x0f,
    0x0f, 0x0e, 0x0e, 0x0e, 0x0d, 0x0d, 0x0d, 0x0d, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned short ac3_hth[50][3] = {
    {0x4d0, 0x4f0, 0x580}, {0x4d0, 0x4f0, 0x580}, {0x440, 0x460, 0x4b0}, {0x400, 0x410, 0x450},
    {0x3e0, 0x3e0, 0x420}, {0x3c0, 0x3d0, 0x3f0}, {0x3b0, 0x3c0, 0x3e0}, {0x3b0, 0x3b0, 0x3d0},
    {0x3a0, 0x3b0, 0x3c0}, {0x3a0, 0x3a0, 0x3b0}, {0x3a0, 0x3a0, 0x3b0}, {0x3a0, 0x3a0, 0x3b0},
    {0x3a0, 0x3a0, 0x3a0}, {0x390, 0x3a0, 0x3a0}, {0x390, 0x390, 0x3a0}, {0x390, 0x390, 0x3a0},
    {0x380, 0x390, 0x3a0}, {0x380, 0x380, 0x3a0}, {0x370, 0x380, 0x3a0}, {0x370, 0x380, 0x3a0},
    {0x360, 0x370, 0x390}, {0x360, 0x370, 0x390}, {0x350, 0x360, 0x390}, {0x350, 0x360, 0x390},
    {0x340, 0x350, 0x380}, {0x340, 0x350, 0x380}, {0x330, 0x340, 0x380}, {0x320, 0x340, 0x370},
    {0x310, 0x320, 0x360}, {0x300, 0x310, 0x350}, {0x2f0, 0x300, 0x340}, {0x2f0, 0x2f0, 0x330},
    {0x2f0, 0x2f0, 0x320}, {0x2f0, 0x2f0, 0x310}, {0x300, 0x2f0, 0x300}, {0x310, 0x300, 0x2f0},
    {0x340, 0x320, 0x2f0}, {0x390, 0x350, 0x2f0}, {0x3e0, 0x390, 0x300}, {0x420, 0x3e0, 0x310},
    {0x460, 0x420, 0x330}, {0x490, 0x450, 0x350}, {0x4a0, 0x4a0, 0x3c0}, {0x460, 0x490, 0x410},
    {0x440, 0x460, 0x470}, {0x440, 0x440, 0x4a0}, {0x520, 0x480, 0x460}, {0x800, 0x630, 0x440},
    {0x840, 0x840, 0x450}, {0x840, 0x840, 0x4e0}};
static const xu8 ac3_baptab[64] = {0,  1,  1,  1,  1,  1,  2,  2,  3,  3,  3,  4,  4,  5,  5,  6,
                                   6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9,  10,
                                   10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14,
                                   14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15};
static const short ac3_slowdec[4] = {0x0f, 0x11, 0x13, 0x15};
static const short ac3_fastdec[4] = {0x3f, 0x53, 0x67, 0x7b};
static const short ac3_slowgain[4] = {0x540, 0x4d8, 0x478, 0x410};
static const short ac3_dbpb[4] = {0x000, 0x700, 0x900, 0xb00};
static const int ac3_floor[8] = {0x2f0, 0x2b0, 0x270, 0x230, 0x1f0, 0x170, 0x0f0, -2048};
static const short ac3_fastgain[8] = {0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400};
static const short ac3_rates[19] = {32,  40,  48,  56,  64,  80,  96,  112, 128, 160,
                                    192, 224, 256, 320, 384, 448, 512, 576, 640};
static const xu8 ac3_qbits[16] = {0, 0, 0, 3, 0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16};
static xu8 ac3_bndtab[51], ac3_masktab[256];
static float ac3_win[256];
static int ac3_init_done;
static void ac3_tables(void);
static void ac3_init(void) {
    if (ac3_init_done)
        return;
    int b = 0, k = 0;
    for (; b < 28; b++)
        ac3_bndtab[b] = (xu8)k++;
    static const xu8 w[4] = {3, 6, 12, 24}, c[4] = {7, 6, 4, 5};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < c[i]; j++) {
            ac3_bndtab[b++] = (xu8)k;
            k += w[i];
        }
    ac3_bndtab[50] = (xu8)k; /* 253 */
    for (int bin = 0, band = 0; bin < 256; bin++) {
        while (band < 49 && bin >= ac3_bndtab[band + 1])
            band++;
        ac3_masktab[bin] = (xu8)band;
    }
    /* Kaiser-Bessel derived window, alpha 5 */
    double sum = 0, loc[256], a2 = (5.0 * M_PI / 256) * (5.0 * M_PI / 256);
    for (int i = 0; i < 256; i++) {
        double t = i * (256 - i) * a2, bs = 1;
        for (int j = 50; j > 0; j--)
            bs = bs * t / (j * j) + 1;
        sum += bs;
        loc[i] = sum;
    }
    sum++;
    for (int i = 0; i < 256; i++)
        ac3_win[i] = (float)sqrt(loc[i] / sum);
    ac3_tables();
    ac3_init_done = 1;
}
/* DCT-IV of size m (256 or 128) in place, by an FFT of m/2 points. The twiddles come from
 * tables made once: [0] for m = 256, [1] for m = 128 */
static float ac3_pre[2][128][2], ac3_post[2][128][2], ac3_fw[2][64][2];
static void ac3_tables(void) {
    for (int t = 0; t < 2; t++) {
        int m = t ? 128 : 256, h = m / 2;
        for (int k = 0; k < h; k++) {
            double a = -M_PI * (k + 0.25) / m, q = -M_PI * k / m;
            ac3_pre[t][k][0] = (float)cos(a);
            ac3_pre[t][k][1] = (float)sin(a);
            ac3_post[t][k][0] = (float)cos(q);
            ac3_post[t][k][1] = (float)sin(q);
        }
        for (int j = 0; j < h / 2; j++) {
            double a = -2 * M_PI * j / h;
            ac3_fw[t][j][0] = (float)cos(a);
            ac3_fw[t][j][1] = (float)sin(a);
        }
    }
}
static void ac3_dct4(float *x, int m) {
    float re[128], im[128];
    int h = m / 2, t = m == 128;
    for (int k = 0; k < h; k++) {
        float c = ac3_pre[t][k][0], s = ac3_pre[t][k][1], xr = x[2 * k], xi = x[m - 1 - 2 * k];
        re[k] = xr * c - xi * s;
        im[k] = xr * s + xi * c;
    }
    for (int i = 1, j = 0; i < h; i++) { /* bit reversal */
        int bit = h >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float v = re[i];
            re[i] = re[j];
            re[j] = v;
            v = im[i];
            im[i] = im[j];
            im[j] = v;
        }
    }
    for (int len = 2; len <= h; len <<= 1) {
        int step = h / len;
        for (int i = 0; i < h; i += len)
            for (int j = 0; j < len / 2; j++) {
                float wr = ac3_fw[t][j * step][0], wi = ac3_fw[t][j * step][1];
                int p = i + j, q = p + len / 2;
                float tr = re[q] * wr - im[q] * wi, ti = re[q] * wi + im[q] * wr;
                re[q] = re[p] - tr;
                im[q] = im[p] - ti;
                re[p] += tr;
                im[p] += ti;
            }
    }
    for (int k = 0; k < h; k++) {
        float c = ac3_post[t][k][0], s = ac3_post[t][k][1];
        x[2 * k] = re[k] * c - im[k] * s;
        x[m - 1 - 2 * k] = -(re[k] * s + im[k] * c);
    }
}
typedef struct {
    long *fo; /* frame offsets */
    int nch, lfe, acmod;
    float clev, slev;
    /* state that blocks may reuse */
    int cplinu, chincpl[5], phsflginu, cplbegf, cplendf, ncplbnd, cplbndstrc[18], cplstrt,
        cplend;
    float cplco[5][18];
    int rematflg[4];
    int expstr[7], endmant[7], strtmant[7];
    signed char exps[7][256];
    xu8 bap[7][256];
    int sdcy, fdcy, sgain, dbknee, floor_, csnr, fsnr[7], fgain[7], cplfleak, cplsleak;
    int deltbae[7], deltnseg[7];
    xu8 deltoffst[7][8], deltlen[7][8], deltba[7][8];
    int dith[5], blksw[5];
    float coef[7][256], delay[6][256];
    float dyn;
    xu32 rnd;
    int fscod;
} xac3;
/* the bit allocation of channel c (0-4 full, 5 coupling, 6 LFE) for bins start..end */
static void ac3_alloc(xac3 *a, int c, int start, int end, int lfe) {
    short psd[256], bpsd[50], excite[50], mask[50];
    if (end <= start)
        return;
    for (int bin = start; bin < end; bin++)
        psd[bin] = (short)(3072 - (a->exps[c][bin] << 7));
    int bin = start, band = ac3_masktab[start];
    do {
        int v = psd[bin++], be = ac3_bndtab[band + 1] < end ? ac3_bndtab[band + 1] : end;
        for (; bin < be; bin++) {
            int mx = v > psd[bin] ? v : psd[bin], adr = mx - ((v + psd[bin] + 1) >> 1);
            v = mx + ac3_latab[adr > 255 ? 255 : adr];
        }
        bpsd[band++] = (short)v;
    } while (end > ac3_bndtab[band]);
    int bs = ac3_masktab[start], bend = ac3_masktab[end - 1] + 1, begin, fl = 0, sl = 0, lc = 0;
    int fgain = ac3_fastgain[a->fgain[c]];
#define LC1(a_, b0, b1, cc) ((b0) + 256 == (b1) ? (cc) : (b0) > (b1) ? ((a_) - 64 > 0 ? (a_) - 64 : 0) : (a_))
    if (bs == 0) {
        lc = LC1(0, bpsd[0], bpsd[1], 384);
        excite[0] = (short)(bpsd[0] - fgain - lc);
        lc = LC1(lc, bpsd[1], bpsd[2], 384);
        excite[1] = (short)(bpsd[1] - fgain - lc);
        begin = 7;
        for (int b = 2; b < 7; b++) {
            if (!(lfe && b == 6))
                lc = LC1(lc, bpsd[b], bpsd[b + 1], 384);
            fl = bpsd[b] - fgain;
            sl = bpsd[b] - a->sgain;
            excite[b] = (short)(fl - lc);
            if (!(lfe && b == 6) && bpsd[b] <= bpsd[b + 1]) {
                begin = b + 1;
                break;
            }
        }
        int e1 = bend < 22 ? bend : 22;
        for (int b = begin; b < e1; b++) {
            if (!(lfe && b == 6)) {
                if (b < 7)
                    lc = LC1(lc, bpsd[b], bpsd[b + 1], 384);
                else if (b < 20)
                    lc = LC1(lc, bpsd[b], bpsd[b + 1], 320);
                else
                    lc = lc - 128 > 0 ? lc - 128 : 0;
            }
            fl = fl - a->fdcy > bpsd[b] - fgain ? fl - a->fdcy : bpsd[b] - fgain;
            sl = sl - a->sdcy > bpsd[b] - a->sgain ? sl - a->sdcy : bpsd[b] - a->sgain;
            excite[b] = (short)(fl - lc > sl ? fl - lc : sl);
        }
        begin = 22;
    } else {
        begin = bs;
        fl = (a->cplfleak << 8) + 768;
        sl = (a->cplsleak << 8) + 768;
    }
#undef LC1
    for (int b = begin; b < bend; b++) {
        fl = fl - a->fdcy > bpsd[b] - fgain ? fl - a->fdcy : bpsd[b] - fgain;
        sl = sl - a->sdcy > bpsd[b] - a->sgain ? sl - a->sdcy : bpsd[b] - a->sgain;
        excite[b] = (short)(fl > sl ? fl : sl);
    }
    for (int b = bs; b < bend; b++) {
        int ex = excite[b];
        if (bpsd[b] < a->dbknee)
            ex += (a->dbknee - bpsd[b]) >> 2;
        int h = ac3_hth[b][a->fscod];
        mask[b] = (short)(h > ex ? h : ex);
    }
    if (a->deltbae[c] == 0 || a->deltbae[c] == 1) {
        int b = 0;
        for (int sg = 0; sg < a->deltnseg[c]; sg++) {
            b += a->deltoffst[c][sg];
            int dl = a->deltba[c][sg] >= 4 ? (a->deltba[c][sg] - 3) * 128 : (a->deltba[c][sg] - 4) * 128;
            for (int i = 0; i < a->deltlen[c][sg] && b < 50; i++, b++)
                mask[b] = (short)(mask[b] + dl);
        }
    }
    int snr = ((a->csnr - 15) * 16 + a->fsnr[c]) * 4;
    if (snr == -960) {
        memset(a->bap[c] + start, 0, (size_t)(end - start));
        return;
    }
    bin = start;
    band = ac3_masktab[start];
    int be;
    do {
        int m = mask[band] - snr - a->floor_;
        m = ((m > 0 ? m : 0) & 0x1FE0) + a->floor_;
        be = ac3_bndtab[++band];
        if (be > end)
            be = end;
        for (; bin < be; bin++) {
            int ad = (psd[bin] - m) >> 5;
            a->bap[c][bin] = ac3_baptab[ad < 0 ? 0 : ad > 63 ? 63 : ad];
        }
    } while (end > be);
}
static int ac3_exps(xbr *b, int str, int ngrp, int absexp, signed char *o) {
    int prev = absexp, gs = str == 3 ? 4 : str;
    for (int g = 0; g < ngrp; g++) {
        int m = (int)xbm(b, 7);
        if (m >= 125)
            return 0;
        int dd[3] = {m / 25, (m % 25) / 5, m % 5};
        for (int i = 0; i < 3; i++) {
            prev += dd[i] - 2;
            if (prev < 0 || prev > 24)
                return 0;
            for (int j = 0; j < gs; j++)
                *o++ = (signed char)prev;
        }
    }
    return 1;
}
typedef struct {
    int n1, n2, n4;
    float v1[3], v2[3], v4[2];
} xac3g;
static float ac3_mant(xbr *b, int bap, xac3g *g) {
    switch (bap) {
        case 1:
            if (!g->n1) {
                int m = (int)xbm(b, 5);
                if (m > 26)
                    m = 26;
                g->v1[0] = (m / 9 - 1) * (2.0f / 3);
                g->v1[1] = ((m % 9) / 3 - 1) * (2.0f / 3);
                g->v1[2] = (m % 3 - 1) * (2.0f / 3);
                g->n1 = 3;
            }
            return g->v1[3 - g->n1--];
        case 2:
            if (!g->n2) {
                int m = (int)xbm(b, 7);
                if (m > 124)
                    m = 124;
                g->v2[0] = (m / 25 - 2) * (2.0f / 5);
                g->v2[1] = ((m % 25) / 5 - 2) * (2.0f / 5);
                g->v2[2] = (m % 5 - 2) * (2.0f / 5);
                g->n2 = 3;
            }
            return g->v2[3 - g->n2--];
        case 3: {
            int m = (int)xbm(b, 3);
            return (m > 6 ? 0 : m - 3) * (2.0f / 7);
        }
        case 4:
            if (!g->n4) {
                int m = (int)xbm(b, 7);
                if (m > 120)
                    m = 120;
                g->v4[0] = (m / 11 - 5) * (2.0f / 11);
                g->v4[1] = (m % 11 - 5) * (2.0f / 11);
                g->n4 = 2;
            }
            return g->v4[2 - g->n4--];
        case 5: {
            int m = (int)xbm(b, 4);
            return (m > 14 ? 0 : m - 7) * (2.0f / 15);
        }
        default: {
            int q = ac3_qbits[bap];
            return (float)xbms(b, q) / (float)(1 << (q - 1));
        }
    }
}
static float ac3_dither(xac3 *a) {
    a->rnd = a->rnd * 1664525u + 1013904223u;
    return ((int)(a->rnd >> 8) - (1 << 23)) * (0.707f / (1 << 23));
}
static float ac3_pow2[32];
/* one audio block; returns 0 if the frame is damaged */
static int ac3_block(xac3 *a, xbr *b, int blk) {
    int nch = a->nch;
    for (int c = 0; c < nch; c++)
        a->blksw[c] = (int)xbm(b, 1);
    for (int c = 0; c < nch; c++)
        a->dith[c] = (int)xbm(b, 1);
    if (xbm(b, 1)) {
        int v = (int)xbm(b, 8), e = (v >> 5) - ((v >> 7) << 3);
        a->dyn = (float)ldexp((32 + (v & 31)) / 32.0, e);
    } else if (!blk)
        a->dyn = 1;
    if (a->acmod == 0 && xbm(b, 1))
        xbm(b, 8);
    if (xbm(b, 1)) { /* coupling strategy */
        a->cplinu = (int)xbm(b, 1);
        if (a->cplinu) {
            for (int c = 0; c < nch; c++)
                a->chincpl[c] = (int)xbm(b, 1);
            a->phsflginu = a->acmod == 2 ? (int)xbm(b, 1) : 0;
            a->cplbegf = (int)xbm(b, 4);
            a->cplendf = (int)xbm(b, 4);
            int nsub = 3 + a->cplendf - a->cplbegf;
            if (nsub < 1 || nsub > 18)
                return 0;
            a->ncplbnd = nsub;
            a->cplbndstrc[0] = 0;
            for (int i = 1; i < nsub; i++) {
                a->cplbndstrc[i] = (int)xbm(b, 1);
                a->ncplbnd -= a->cplbndstrc[i];
            }
            a->cplstrt = a->cplbegf * 12 + 37;
            a->cplend = (a->cplendf + 3) * 12 + 37;
        } else
            for (int c = 0; c < nch; c++)
                a->chincpl[c] = 0;
    } else if (!blk)
        return 0;
    if (a->cplinu) {
        int any = 0;
        for (int c = 0; c < nch; c++)
            if (a->chincpl[c] && xbm(b, 1)) {
                int mst = (int)xbm(b, 2) * 3;
                any = 1;
                for (int bd = 0; bd < a->ncplbnd; bd++) {
                    int e = (int)xbm(b, 4), m = (int)xbm(b, 4);
                    float v = e == 15 ? m / 16.0f : (m + 16) / 32.0f;
                    a->cplco[c][bd] = (float)ldexp(v, -(e + mst));
                }
            }
        if (a->acmod == 2 && a->phsflginu && any)
            for (int bd = 0; bd < a->ncplbnd; bd++)
                if (xbm(b, 1))
                    a->cplco[1][bd] = -a->cplco[1][bd];
    }
    if (a->acmod == 2 && xbm(b, 1)) { /* rematrixing */
        int nr = !a->cplinu || a->cplbegf > 2 ? 4 : a->cplbegf > 0 ? 3 : 2;
        for (int i = 0; i < 4; i++)
            a->rematflg[i] = i < nr ? (int)xbm(b, 1) : 0;
    }
    int cplstr = a->cplinu ? (int)xbm(b, 2) : 0;
    for (int c = 0; c < nch; c++)
        a->expstr[c] = (int)xbm(b, 2);
    int lfestr = a->lfe ? (int)xbm(b, 1) : 0;
    if (!blk && (!a->expstr[0] || (a->cplinu && !cplstr)))
        return 0;
    for (int c = 0; c < nch; c++)
        if (a->expstr[c]) {
            if (a->chincpl[c] && a->cplinu)
                a->endmant[c] = a->cplstrt;
            else {
                int bw = (int)xbm(b, 6);
                if (bw > 60)
                    return 0;
                a->endmant[c] = bw * 3 + 73;
            }
        }
    if (a->cplinu && cplstr) {
        int ae = (int)xbm(b, 4) << 1, ng = (a->cplend - a->cplstrt) / (3 << (cplstr - 1));
        a->exps[5][a->cplstrt - 1] = (signed char)ae; /* not used, the first is relative */
        if (!ac3_exps(b, cplstr, ng, ae, a->exps[5] + a->cplstrt))
            return 0;
    }
    for (int c = 0; c < nch; c++)
        if (a->expstr[c]) {
            int gs = 3 << (a->expstr[c] - 1), ng = (a->endmant[c] + gs - 4) / gs;
            a->exps[c][0] = (signed char)xbm(b, 4);
            if (!ac3_exps(b, a->expstr[c], ng, a->exps[c][0], a->exps[c] + 1))
                return 0;
            xbm(b, 2); /* gainrng */
        }
    if (a->lfe && lfestr) {
        a->exps[6][0] = (signed char)xbm(b, 4);
        if (!ac3_exps(b, 1, 2, a->exps[6][0], a->exps[6] + 1))
            return 0;
    }
    if (xbm(b, 1)) { /* bit allocation parameters */
        a->sdcy = ac3_slowdec[xbm(b, 2)];
        a->fdcy = ac3_fastdec[xbm(b, 2)];
        a->sgain = ac3_slowgain[xbm(b, 2)];
        a->dbknee = ac3_dbpb[xbm(b, 2)];
        a->floor_ = ac3_floor[xbm(b, 3)];
    } else if (!blk)
        return 0;
    if (xbm(b, 1)) { /* SNR offsets */
        a->csnr = (int)xbm(b, 6);
        if (a->cplinu) {
            a->fsnr[5] = (int)xbm(b, 4);
            a->fgain[5] = (int)xbm(b, 3);
        }
        for (int c = 0; c < nch; c++) {
            a->fsnr[c] = (int)xbm(b, 4);
            a->fgain[c] = (int)xbm(b, 3);
        }
        if (a->lfe) {
            a->fsnr[6] = (int)xbm(b, 4);
            a->fgain[6] = (int)xbm(b, 3);
        }
    } else if (!blk)
        return 0;
    if (a->cplinu && xbm(b, 1)) {
        a->cplfleak = (int)xbm(b, 3);
        a->cplsleak = (int)xbm(b, 3);
    }
    if (xbm(b, 1)) { /* delta bit allocation */
        if (a->cplinu)
            a->deltbae[5] = (int)xbm(b, 2);
        for (int c = 0; c < nch; c++)
            a->deltbae[c] = (int)xbm(b, 2);
        for (int c = 0; c < 6; c++) {
            int cc = c == 0 ? 5 : c - 1; /* the coupling channel first */
            if (cc < 5 && cc >= nch)
                continue;
            if (cc == 5 && !a->cplinu)
                continue;
            if (a->deltbae[cc] == 1) {
                a->deltnseg[cc] = (int)xbm(b, 3) + 1;
                for (int s = 0; s < a->deltnseg[cc]; s++) {
                    a->deltoffst[cc][s] = (xu8)xbm(b, 5);
                    a->deltlen[cc][s] = (xu8)xbm(b, 4);
                    a->deltba[cc][s] = (xu8)xbm(b, 3);
                }
            }
        }
    } else if (!blk)
        for (int c = 0; c < 7; c++)
            a->deltbae[c] = 2;
    if (xbm(b, 1)) { /* skip */
        int l = (int)xbm(b, 9);
        b->pos += l * 8L;
    }
    /* bit allocation and mantissas */
    if (a->cplinu)
        ac3_alloc(a, 5, a->cplstrt, a->cplend, 0);
    for (int c = 0; c < nch; c++)
        ac3_alloc(a, c, 0, a->endmant[c], 0);
    if (a->lfe)
        ac3_alloc(a, 6, 0, 7, 1);
    xac3g g = {0};
    int gotcpl = 0;
    for (int c = 0; c < nch; c++) {
        float *co = a->coef[c];
        memset(co, 0, 256 * sizeof(float));
        for (int i = 0; i < a->endmant[c]; i++) {
            int bp = a->bap[c][i];
            float m = bp ? ac3_mant(b, bp, &g) : a->dith[c] ? ac3_dither(a) : 0;
            co[i] = m * ac3_pow2[a->exps[c][i]];
        }
        if (a->cplinu && a->chincpl[c] && !gotcpl) {
            for (int i = a->cplstrt; i < a->cplend; i++) {
                int bp = a->bap[5][i];
                /* bap 0 in coupling: dithered later per channel */
                a->coef[5][i] = bp ? ac3_mant(b, bp, &g) * ac3_pow2[a->exps[5][i]] : 1e30f;
            }
            gotcpl = 1;
        }
    }
    if (a->lfe)
        for (int i = 0; i < 7; i++) {
            int bp = a->bap[6][i];
            if (bp)
                ac3_mant(b, bp, &g);
        }
    if (xb_left(b) < 0)
        return 0;
    /* coupling: the channels get the coupling channel, scaled per band */
    if (a->cplinu)
        for (int c = 0; c < nch; c++) {
            if (!a->chincpl[c])
                continue;
            int bin = a->cplstrt, bd = -1;
            for (int sb = 0; sb < a->cplendf + 3 - a->cplbegf; sb++) {
                if (!a->cplbndstrc[sb])
                    bd++;
                for (int k = 0; k < 12; k++, bin++) {
                    float v = a->coef[5][bin];
                    if (v == 1e30f)
                        v = a->dith[c] ? ac3_dither(a) * ac3_pow2[a->exps[5][bin]] : 0;
                    a->coef[c][bin] = v * a->cplco[c][bd < 0 ? 0 : bd] * 8;
                }
            }
        }
    if (a->acmod == 2) { /* rematrixing */
        static const short rb[5] = {13, 25, 37, 61, 253};
        int end = a->endmant[0] < a->endmant[1] ? a->endmant[0] : a->endmant[1];
        if (a->cplinu && a->cplstrt < end)
            end = a->cplstrt;
        for (int r = 0; r < 4; r++)
            if (a->rematflg[r])
                for (int i = rb[r]; i < rb[r + 1] && i < end; i++) {
                    float l = a->coef[0][i], rr = a->coef[1][i];
                    a->coef[0][i] = l + rr;
                    a->coef[1][i] = l - rr;
                }
    }
    return 1;
}
/* inverse transform of channel c, 256 new samples into out */
static void ac3_imdct(xac3 *a, int c, float *out) {
    float y[512], *x = a->coef[c];
    if (!a->blksw[c]) {
        float u[256];
        memcpy(u, x, sizeof u);
        ac3_dct4(u, 256);
        for (int n = 0; n < 128; n++)
            y[n] = u[n + 128];
        for (int n = 128; n < 384; n++)
            y[n] = -u[383 - n];
        for (int n = 384; n < 512; n++)
            y[n] = -u[n - 384];
    } else {
        float u1[128], u2[128];
        for (int k = 0; k < 128; k++) {
            u1[k] = x[2 * k];
            u2[k] = x[2 * k + 1];
        }
        ac3_dct4(u1, 128);
        ac3_dct4(u2, 128);
        for (int n = 0; n < 128; n++) {
            y[n] = u1[n];
            y[255 - n] = -u1[n];
            y[256 + n] = -u2[127 - n];
            y[384 + n] = -u2[n];
        }
    }
    float *dl = a->delay[c];
    for (int n = 0; n < 256; n++) {
        out[n] = (y[n] * ac3_win[n] + dl[n]) * -2;
        dl[n] = y[256 + n] * ac3_win[255 - n];
    }
}
static int ac3_frame(xdec *d, long idx, short *o) {
    xac3 *a = d->s;
    long st = a->fo[idx], en = a->fo[idx + 1];
    xbr b = {d->f + st, en - st, 0};
    xbm(&b, 32);
    a->fscod = (int)xbm(&b, 2);
    xbm(&b, 6);
    int bsid = (int)xbm(&b, 5);
    xbm(&b, 3);
    int acmod = (int)xbm(&b, 3);
    if (bsid > 8 || a->fscod > 2) {
        memset(o, 0, 1536 * 4);
        return 1536;
    }
    static const float clevs[4] = {0.707f, 0.595f, 0.5f, 0.595f}, slevs[4] = {0.707f, 0.5f, 0, 0.5f};
    a->clev = 0.707f;
    a->slev = 0.707f;
    if ((acmod & 1) && acmod != 1)
        a->clev = clevs[xbm(&b, 2)];
    if (acmod & 4)
        a->slev = slevs[xbm(&b, 2)];
    if (acmod == 2)
        xbm(&b, 2);
    static const xu8 chans[8] = {2, 1, 2, 3, 3, 4, 4, 5};
    if (acmod != a->acmod) { /* other channels: start the transforms afresh */
        memset(a->delay, 0, sizeof a->delay);
        a->acmod = acmod;
    }
    a->nch = chans[acmod];
    a->lfe = (int)xbm(&b, 1);
    for (int k = 0; k <= (acmod == 0); k++) {
        xbm(&b, 5);
        if (xbm(&b, 1))
            xbm(&b, 8);
        if (xbm(&b, 1))
            xbm(&b, 8);
        if (xbm(&b, 1))
            xbm(&b, 7);
    }
    xbm(&b, 2);
    if (xbm(&b, 1))
        xbm(&b, 14);
    if (xbm(&b, 1))
        xbm(&b, 14);
    if (xbm(&b, 1)) {
        int l = (int)xbm(&b, 6);
        b.pos += (l + 1) * 8L;
    }
    /* the mix down to two channels */
    float gl[5] = {0}, gr[5] = {0};
    switch (acmod) {
        case 0:
        case 2:
            gl[0] = 1, gr[1] = 1;
            break;
        case 1:
            gl[0] = gr[0] = 0.707f;
            break;
        case 3:
            gl[0] = 1, gl[1] = gr[1] = a->clev, gr[2] = 1;
            break;
        case 4:
            gl[0] = 1, gr[1] = 1, gl[2] = gr[2] = a->slev * 0.707f;
            break;
        case 5:
            gl[0] = 1, gl[1] = gr[1] = a->clev, gr[2] = 1, gl[3] = gr[3] = a->slev * 0.707f;
            break;
        case 6:
            gl[0] = 1, gr[1] = 1, gl[2] = a->slev, gr[3] = a->slev;
            break;
        case 7:
            gl[0] = 1, gl[1] = gr[1] = a->clev, gr[2] = 1, gl[3] = a->slev, gr[4] = a->slev;
            break;
    }
    float sl = 0, sr = 0;
    for (int c = 0; c < 5; c++) {
        sl += gl[c];
        sr += gr[c];
    }
    float norm = 1 / (sl > sr ? sl : sr);
    for (int blk = 0; blk < 6; blk++) {
        float pcm[5][256];
        if (!ac3_block(a, &b, blk)) {
            memset(o + blk * 512, 0, (size_t)(6 - blk) * 512 * 2);
            memset(a->delay, 0, sizeof a->delay);
            return 1536;
        }
        for (int c = 0; c < a->nch; c++) {
            for (int i = 0; i < 256; i++)
                a->coef[c][i] *= a->dyn;
            ac3_imdct(a, c, pcm[c]);
        }
        for (int i = 0; i < 256; i++) {
            float l = 0, r = 0;
            for (int c = 0; c < a->nch; c++) {
                l += gl[c] * pcm[c][i];
                r += gr[c] * pcm[c][i];
            }
            o[(blk * 256 + i) * 2] = xc_clip((long)(l * norm * 32768.0f));
            o[(blk * 256 + i) * 2 + 1] = xc_clip((long)(r * norm * 32768.0f));
        }
    }
    return 1536;
}
static int ac3_size(const xu8 *p) { /* bytes of the frame that starts at p, 0 if none */
    if (p[0] != 0x0B || p[1] != 0x77)
        return 0;
    int fs = p[4] >> 6, fc = p[4] & 63, bsid = p[5] >> 3;
    if (fs > 2 || fc > 37 || bsid > 8)
        return 0;
    int r = ac3_rates[fc >> 1];
    if (fs == 0)
        return r * 4;
    if (fs == 2)
        return r * 6;
    return (r * 320 / 147 + (fc & 1)) * 2;
}
static int ac3_open(xdec *d) {
    const xu8 *f = d->f;
    long n = d->n, o = 0;
    if (n > 10 && !memcmp(f, "ID3", 3))
        o = 10 + ((f[6] & 127) << 21 | (f[7] & 127) << 14 | (f[8] & 127) << 7 | (f[9] & 127));
    while (o + 8 < n && o < 4096 && !ac3_size(f + o) && d->bits == 99) /* search only for .ac3 */
        o++;
    if (o + 8 >= n || !ac3_size(f + o))
        return 0;
    int first = ac3_size(f + o);
    if (o + first + 8 < n && !ac3_size(f + o + first))
        return 0; /* two frames in a row, otherwise it is not AC3 */
    xac3 *a = malloc(sizeof *a);
    if (!a)
        return 0;
    memset(a, 0, sizeof *a);
    a->acmod = -1;
    a->rnd = 1;
    ac3_init();
    for (int i = 0; i < 32; i++)
        ac3_pow2[i] = (float)ldexp(1.0, -i);
    long cap = n / first + 16, nf = 0;
    a->fo = malloc((size_t)(cap + 1) * sizeof(long));
    if (!a->fo)
        return 0;
    while (o + 8 <= n && nf < cap) {
        int l = ac3_size(f + o);
        if (!l) { /* lost: look for the next frame */
            o++;
            continue;
        }
        if (o + l > n)
            break;
        a->fo[nf++] = o;
        o += l;
    }
    if (!nf)
        return 0;
    a->fo[nf] = o;
    const xu8 *h = f + a->fo[0];
    int fs = h[4] >> 6, acm = h[6] >> 5;
    static const int rates[3] = {48000, 44100, 32000};
    static const xu8 chans[8] = {2, 1, 2, 3, 3, 4, 4, 5};
    d->srate = rates[fs];
    d->chans = chans[acm];
    d->bits = 16;
    d->kbps = ac3_rates[(h[4] & 63) >> 1];
    d->s = a;
    d->name = "AC3";
    d->frame = ac3_frame;
    d->fcount = nf;
    d->flen = 1536;
    d->total = nf * 1536;
    return xf_init(d, 1536);
}

/* ---------- which decoder: by the content, the name only helps for DSD ---------- */
/* 1: ready, 2: d->f/d->n now hold a stream for the other decoders (from Matroska), 0: unknown */
static int xd_open(xdec *d, const char *path) {
    int r = mka_open(d);
    if (r)
        return r;
    if (d->n >= 92 && dsd_open(d, d->f, d->n > 65536 ? 65536 : d->n))
        return 1;
    if (aiff_open(d) || caf_open(d) || tta_open(d) || wv_open(d))
        return 1;
    size_t pl = path ? strlen(path) : 0;
    if (pl > 4 && (path[pl - 1] | 32) == '3' && (path[pl - 2] | 32) == 'c' && (path[pl - 3] | 32) == 'a')
        d->bits = 99; /* .ac3: the first frame may start a little later */
    if (ac3_open(d))
        return 1;
    d->bits = 0;
    return 0;
}
