/* FLAC encoder for XZN's CD rips: 16-bit stereo at 44.1 kHz, written from the published format
 * description (xiph.org). Every block tries the four stereo modes and the fixed predictors of
 * order 0 to 4, the residual is Rice coded with up to 64 partitions. Lossless, and a CD usually
 * shrinks to 55 to 70 percent. BSD-3-Clause, part of Violyx. */
#pragma once
#include <stdint.h>
#define XF_BLOCK 4096
#define XF_FRAME (XF_BLOCK * 2 * 3 + 64) /* largest frame: verbatim with a 17-bit side channel */
typedef struct {
    uint8_t *o;
    long n;
    uint64_t acc;
    int nb;
} xf_bw;
static void xf_put(xf_bw *w, uint32_t v, int bits) {
    while (bits > 0) {
        int k = bits > 24 ? 24 : bits;
        bits -= k;
        w->acc = w->acc << k | ((v >> bits) & ((1u << k) - 1));
        w->nb += k;
        while (w->nb >= 8) {
            w->nb -= 8;
            w->o[w->n++] = (uint8_t)(w->acc >> w->nb);
        }
    }
}
static void xf_align(xf_bw *w) {
    if (w->nb)
        xf_put(w, 0, 8 - w->nb);
}
static void xf_unary(xf_bw *w, uint32_t q) { /* q zeros, then a one */
    while (q >= 24) {
        xf_put(w, 0, 24);
        q -= 24;
    }
    xf_put(w, 1, (int)q + 1);
}
static uint8_t xf_crc8(const uint8_t *p, long n) {
    uint8_t c = 0;
    for (long i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (uint8_t)(c & 0x80 ? c << 1 ^ 0x07 : c << 1);
    }
    return c;
}
static uint16_t xf_crc16(const uint8_t *p, long n) {
    uint16_t c = 0;
    for (long i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++)
            c = (uint16_t)(c & 0x8000 ? c << 1 ^ 0x8005 : c << 1);
    }
    return c;
}
/* residual of the fixed predictor of order k from position k on */
static void xf_resid(const int32_t *x, int n, int k, int32_t *r) {
    for (int i = k; i < n; i++) {
        int32_t v = x[i];
        switch (k) {
            case 1:
                v -= x[i - 1];
                break;
            case 2:
                v -= 2 * x[i - 1] - x[i - 2];
                break;
            case 3:
                v -= 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3];
                break;
            case 4:
                v -= 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4];
                break;
        }
        r[i] = v;
    }
}
static uint32_t xf_zz(int32_t v) {
    return (uint32_t)v << 1 ^ (uint32_t)(v >> 31);
}
/* the best Rice parameter for n values with the sum s, and its cost in bits */
static uint64_t xf_rice(uint64_t s, int n, int *kp) {
    uint64_t best = ~0ull;
    for (int k = 0; k <= 14; k++) {
        uint64_t c = (uint64_t)n * (k + 1) + (s >> k);
        if (c < best) {
            best = c;
            *kp = k;
        }
    }
    return best + 4;
}
/* cost and partitioning of a residual; po = chosen partition order */
static uint64_t xf_part(const int32_t *r, int n, int ord, int *po) {
    static uint64_t sum[64];
    uint64_t best = ~0ull;
    for (int p = 6; p >= 0; p--) {
        int parts = 1 << p, len = n >> p;
        if (len << p != n || len <= ord)
            continue;
        for (int j = 0; j < parts; j++) {
            uint64_t s = 0;
            for (int i = j ? j * len : ord; i < (j + 1) * len; i++)
                s += xf_zz(r[i]);
            sum[j] = s;
        }
        uint64_t c = 6;
        for (int j = 0; j < parts; j++) {
            int k;
            c += xf_rice(sum[j], j ? len : len - ord, &k);
        }
        if (c < best) {
            best = c;
            *po = p;
        }
    }
    return best;
}
/* cost of a channel in bits with its best order */
static uint64_t xf_cost(const int32_t *x, int n, int bps, int *ordp, int *pop) {
    static int32_t r[XF_BLOCK];
    int same = 1;
    for (int i = 1; i < n && same; i++)
        same = x[i] == x[0];
    if (same) {
        *ordp = -1;
        return 8 + (uint64_t)bps;
    }
    uint64_t best = 8 + (uint64_t)n * bps; /* verbatim */
    *ordp = -2;
    for (int k = 0; k <= 4 && k < n; k++) {
        int po = 0;
        xf_resid(x, n, k, r);
        uint64_t c = 8 + (uint64_t)k * bps + xf_part(r, n, k, &po);
        if (c < best) {
            best = c;
            *ordp = k;
            *pop = po;
        }
    }
    return best;
}
static void xf_sub(xf_bw *w, const int32_t *x, int n, int bps, int ord, int po) {
    static int32_t r[XF_BLOCK];
    if (ord == -1) {
        xf_put(w, 0, 8);
        xf_put(w, (uint32_t)x[0], bps);
        return;
    }
    if (ord == -2) {
        xf_put(w, 1 << 1, 8);
        for (int i = 0; i < n; i++)
            xf_put(w, (uint32_t)x[i], bps);
        return;
    }
    xf_put(w, (uint32_t)(8 | ord) << 1, 8);
    for (int i = 0; i < ord; i++)
        xf_put(w, (uint32_t)x[i], bps);
    xf_resid(x, n, ord, r);
    xf_put(w, 0, 2); /* Rice with 4-bit parameters */
    xf_put(w, (uint32_t)po, 4);
    int parts = 1 << po, len = n >> po;
    for (int j = 0; j < parts; j++) {
        int a = j ? j * len : ord, b = (j + 1) * len, k;
        uint64_t s = 0;
        for (int i = a; i < b; i++)
            s += xf_zz(r[i]);
        xf_rice(s, b - a, &k);
        xf_put(w, (uint32_t)k, 4);
        for (int i = a; i < b; i++) {
            uint32_t u = xf_zz(r[i]);
            xf_unary(w, u >> k);
            if (k)
                xf_put(w, u & ((1u << k) - 1), k);
        }
    }
}
/* One frame from n (1..XF_BLOCK) stereo samples; o needs XF_FRAME bytes. Returns its length. */
static long xf_frame(uint8_t *o, const int16_t *pcm, int n, uint32_t num) {
    static int32_t ch[4][XF_BLOCK]; /* left, right, side, mid */
    for (int i = 0; i < n; i++) {
        int32_t l = pcm[i * 2], r = pcm[i * 2 + 1];
        ch[0][i] = l;
        ch[1][i] = r;
        ch[2][i] = l - r;
        ch[3][i] = (l + r) >> 1;
    }
    int ord[4], po[4] = {0, 0, 0, 0};
    uint64_t c[4];
    for (int k = 0; k < 4; k++)
        c[k] = xf_cost(ch[k], n, k == 2 ? 17 : 16, &ord[k], &po[k]);
    /* independent, left/side, side/right, mid/side */
    static const int pa[4][2] = {{0, 1}, {0, 2}, {2, 1}, {3, 2}};
    int mode = 0;
    uint64_t best = c[0] + c[1];
    for (int m = 1; m < 4; m++)
        if (c[pa[m][0]] + c[pa[m][1]] < best) {
            best = c[pa[m][0]] + c[pa[m][1]];
            mode = m;
        }
    xf_bw w = {o, 0, 0, 0};
    xf_put(&w, 0xFFF8, 16);
    xf_put(&w, n == XF_BLOCK ? 12 : 7, 4);
    xf_put(&w, 9, 4); /* 44.1 kHz */
    xf_put(&w, mode ? 7 + (uint32_t)mode : 1, 4);
    xf_put(&w, 4 << 1, 4); /* 16 bit */
    /* frame number, coded like UTF-8 */
    if (num < 0x80)
        xf_put(&w, num, 8);
    else if (num < 0x800) {
        xf_put(&w, 0xC0 | num >> 6, 8);
        xf_put(&w, 0x80 | (num & 63), 8);
    } else if (num < 0x10000) {
        xf_put(&w, 0xE0 | num >> 12, 8);
        xf_put(&w, 0x80 | (num >> 6 & 63), 8);
        xf_put(&w, 0x80 | (num & 63), 8);
    } else {
        xf_put(&w, 0xF0 | (num >> 18 & 7), 8);
        xf_put(&w, 0x80 | (num >> 12 & 63), 8);
        xf_put(&w, 0x80 | (num >> 6 & 63), 8);
        xf_put(&w, 0x80 | (num & 63), 8);
    }
    if (n != XF_BLOCK)
        xf_put(&w, (uint32_t)(n - 1), 16);
    xf_put(&w, xf_crc8(o, w.n), 8);
    for (int s = 0; s < 2; s++) {
        int k = pa[mode][s];
        xf_sub(&w, ch[k], n, k == 2 ? 17 : 16, ord[k], po[k]);
    }
    xf_align(&w);
    uint16_t crc = xf_crc16(o, w.n);
    xf_put(&w, crc, 16);
    return w.n;
}
/* CP437 (the characters of Violyx) to UTF-8 for the tags */
static const uint16_t xf_cp437[128] = {
    0xC7,   0xFC,   0xE9,   0xE2,   0xE4,   0xE0,   0xE5,   0xE7,   0xEA,   0xEB,   0xE8,   0xEF,
    0xEE,   0xEC,   0xC4,   0xC5,   0xC9,   0xE6,   0xC6,   0xF4,   0xF6,   0xF2,   0xFB,   0xF9,
    0xFF,   0xD6,   0xDC,   0xA2,   0xA3,   0xA5,   0x20A7, 0x192,  0xE1,   0xED,   0xF3,   0xFA,
    0xF1,   0xD1,   0xAA,   0xBA,   0xBF,   0x2310, 0xAC,   0xBD,   0xBC,   0xA1,   0xAB,   0xBB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557,
    0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
    0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x3B1,  0xDF,   0x393,  0x3C0,  0x3A3,  0x3C3,  0xB5,   0x3C4,  0x3A6,  0x398,  0x3A9,  0x3B4,
    0x221E, 0x3C6,  0x3B5,  0x2229, 0x2261, 0xB1,   0x2265, 0x2264, 0x2320, 0x2321, 0xF7,   0x2248,
    0xB0,   0x2219, 0xB7,   0x221A, 0x207F, 0xB2,   0x25A0, 0xA0};
static int xf_utf8(const char *s, uint8_t *o, int max) {
    int k = 0;
    for (; *s; s++) {
        unsigned c = (uint8_t)*s;
        if (c >= 128)
            c = xf_cp437[c - 128];
        if (c < 0x80 && k + 1 <= max)
            o[k++] = (uint8_t)c;
        else if (c < 0x800 && k + 2 <= max) {
            o[k++] = (uint8_t)(0xC0 | c >> 6);
            o[k++] = (uint8_t)(0x80 | (c & 63));
        } else if (c >= 0x800 && k + 3 <= max) {
            o[k++] = (uint8_t)(0xE0 | c >> 12);
            o[k++] = (uint8_t)(0x80 | (c >> 6 & 63));
            o[k++] = (uint8_t)(0x80 | (c & 63));
        } else
            break;
    }
    return k;
}
static void xf_le32(uint8_t *o, uint32_t v) {
    o[0] = (uint8_t)v;
    o[1] = (uint8_t)(v >> 8);
    o[2] = (uint8_t)(v >> 16);
    o[3] = (uint8_t)(v >> 24);
}
/* "fLaC", STREAMINFO and the tags (key=value pairs, CP437). o needs 2048 bytes. */
static long xf_head(uint8_t *o, uint64_t samples, const char *const *tags, int ntags) {
    long n = 0;
    o[n++] = 'f';
    o[n++] = 'L';
    o[n++] = 'a';
    o[n++] = 'C';
    o[n++] = 0; /* STREAMINFO, not the last block */
    o[n++] = 0;
    o[n++] = 0;
    o[n++] = 34;
    xf_bw w = {o + n, 0, 0, 0};
    xf_put(&w, XF_BLOCK, 16);
    xf_put(&w, XF_BLOCK, 16);
    xf_put(&w, 0, 24); /* frame sizes: unknown */
    xf_put(&w, 0, 24);
    xf_put(&w, 44100, 20);
    xf_put(&w, 1, 3);  /* 2 channels */
    xf_put(&w, 15, 5); /* 16 bits */
    xf_put(&w, (uint32_t)(samples >> 32) & 15, 4);
    xf_put(&w, (uint32_t)samples, 32);
    for (int i = 0; i < 16; i++) /* MD5: not computed */
        xf_put(&w, 0, 8);
    n += w.n;
    long h = n;
    o[n++] = 0x80 | 4; /* VORBIS_COMMENT, the last block */
    n += 3;
    static const char ven[] = "Violyx XZN";
    xf_le32(o + n, sizeof ven - 1);
    memcpy(o + n + 4, ven, sizeof ven - 1);
    n += 4 + (long)sizeof ven - 1;
    long cnt = n;
    n += 4;
    int c = 0;
    for (int i = 0; i < ntags; i++) {
        const char *e = tags[i];
        while (e && *e && *e != '=')
            e++;
        if (!e || *e != '=' || !e[1] || e == tags[i])
            continue; /* empty values are left out */
        int l = xf_utf8(tags[i], o + n + 4, 400);
        xf_le32(o + n, (uint32_t)l);
        n += 4 + l;
        c++;
    }
    xf_le32(o + cnt, (uint32_t)c);
    long bl = n - h - 4;
    o[h + 1] = (uint8_t)(bl >> 16);
    o[h + 2] = (uint8_t)(bl >> 8);
    o[h + 3] = (uint8_t)bl;
    return n;
}
