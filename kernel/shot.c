#include "os.h"
#include "viofs.h"
/* Print Screen: the screen as a PNG picture in /home/Screenshots. The picture is taken right
 * when the key comes (shot_take, from key_poll, also while a menu is open) and kept in memory;
 * the main loop saves it (shot_save), when no file operation of the system is going on.
 * The PNG is compressed with deflate and the fixed Huffman codes: a desktop of text repeats
 * itself from pixel to pixel and from line to line, so comparing with the pixel before and the
 * line above is enough. */
static u8 *buf;           /* the whole PNG file */
static u64 bn, bcap;      /* its length and room */
static int bad, have;     /* out of memory; a picture waits to be saved */
static u32 bits, nbits;   /* bits not yet written */
static u32 crctab[256];
char shot_note[100];      /* shown in the status line for a moment */
u64 shot_until;
static void ob(u8 b) {
    if (bn >= bcap) {
        if (bad)
            return;
        u64 nc = bcap ? bcap * 2 : 1 << 20;
        u8 *n = kmalloc(nc);
        if (!n) {
            bad = 1;
            return;
        }
        if (buf) {
            memcpy(n, buf, bn);
            kfree(buf);
        }
        buf = n;
        bcap = nc;
    }
    buf[bn++] = b;
}
static void pbits(u32 v, int n) { /* n bits, the lowest first */
    bits |= v << nbits;
    nbits += (u32)n;
    while (nbits >= 8) {
        ob((u8)bits);
        bits >>= 8;
        nbits -= 8;
    }
}
static void huff(u32 code, int n) { /* Huffman codes go in with their highest bit first */
    u32 r = 0;
    for (int i = 0; i < n; i++)
        r |= ((code >> i) & 1) << (n - 1 - i);
    pbits(r, n);
}
static void lit(int v) { /* a literal byte, or 256 for the end */
    if (v < 144)
        huff(0x30 + (u32)v, 8);
    else if (v < 256)
        huff(0x190 + (u32)(v - 144), 9);
    else if (v < 280)
        huff((u32)(v - 256), 7);
    else
        huff(0xC0 + (u32)(v - 280), 8);
}
static const u16 lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                              31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                              33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                              1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static void match(int len, int dist) {
    int i = 28;
    while (lbase[i] > len)
        i--;
    lit(257 + i);
    pbits((u32)(len - lbase[i]), lext[i]);
    int j = 29;
    while (dbase[j] > dist)
        j--;
    huff((u32)j, 5);
    pbits((u32)(dist - dbase[j]), j < 4 ? 0 : j / 2 - 1);
}
static void be32(u32 v) {
    ob((u8)(v >> 24));
    ob((u8)(v >> 16));
    ob((u8)(v >> 8));
    ob((u8)v);
}
static u32 crc(const u8 *p, u64 n) {
    u32 c = 0xFFFFFFFFu;
    for (u64 i = 0; i < n; i++)
        c = crctab[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}
/* one chunk: length, name, data, CRC; the data comes from fill */
static void chunk_end(u64 start) { /* start: where the name begins */
    u64 len = bn - start - 4;
    buf[start - 4] = (u8)(len >> 24);
    buf[start - 3] = (u8)(len >> 16);
    buf[start - 2] = (u8)(len >> 8);
    buf[start - 1] = (u8)len;
    be32(crc(buf + start, bn - start));
}
static u64 chunk_begin(const char *name) {
    be32(0);
    u64 s = bn;
    for (int i = 0; i < 4; i++)
        ob((u8)name[i]);
    return s;
}
/* the five lines (with the filter byte in front) the matches can reach back to */
#define NR 5
static u8 *rows[NR];
static int L; /* bytes per line */
static int mlen(int r, int c, int d, int max) {
    int sr = r, s = c - d;
    while (s < 0) {
        sr--;
        s += L;
    }
    if (sr < 0 || r - sr > 4)
        return 0;
    const u8 *cur = rows[r % NR] + c, *p = rows[sr % NR] + s;
    int left = L - s, n = 0;
    while (n < max) {
        if (!left) {
            sr++;
            p = rows[sr % NR];
            left = L;
        }
        if (*p != cur[n])
            break;
        p++;
        left--;
        n++;
    }
    return n;
}
void shot_take(void) {
    if (have)
        return; /* the last one is not saved yet */
    u8 *fb;
    u32 pitch, bpp, rs, gs, bs;
    int x0, y0, w, h;
    ui_screen(&fb, &pitch, &bpp, &rs, &gs, &bs, &x0, &y0, &w, &h);
    if (!fb || w <= 0 || h <= 0)
        return;
    if (!crctab[1])
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crctab[i] = c;
        }
    L = 1 + w * 3;
    for (int i = 0; i < NR; i++)
        if (!(rows[i] = kmalloc((size_t)L)))
            goto out;
    bn = 0;
    bad = 0;
    bits = nbits = 0;
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    for (int i = 0; i < 8; i++)
        ob(sig[i]);
    u64 s = chunk_begin("IHDR");
    be32((u32)w);
    be32((u32)h);
    ob(8); /* 8 bits, RGB, deflate, no filters but "none", not interlaced */
    ob(2);
    ob(0);
    ob(0);
    ob(0);
    chunk_end(s);
    s = chunk_begin("IDAT");
    ob(0x78);
    ob(0x01);
    pbits(1, 1); /* the only and last block, fixed codes */
    pbits(1, 2);
    u32 a1 = 1, a2 = 0; /* Adler-32 of the raw lines */
    for (int y = 0; y < h && !bad; y++) {
        u8 *r = rows[y % NR];
        const u8 *src = fb + (u64)(y0 + y) * pitch + (u64)x0 * (bpp / 8);
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            const u8 *p = src + (u64)x * (bpp / 8);
            u32 v = bpp == 32 ? *(const volatile u32 *)p : (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
            r[1 + x * 3] = (u8)(v >> rs);
            r[2 + x * 3] = (u8)(v >> gs);
            r[3 + x * 3] = (u8)(v >> bs);
        }
        for (int i = 0; i < L; i++) {
            a1 = (a1 + r[i]) % 65521;
            a2 = (a2 + a1) % 65521;
        }
        for (int c = 0; c < L;) {
            int max = L - c > 258 ? 258 : L - c, best = 0, bd = 0;
            /* the pixel before, patterns of the background, the lines above */
            const int ds[8] = {3, L, 6, 12, 2 * L, 24, 3 * L, 4 * L};
            for (int i = 0; i < 8 && best < max; i++) {
                if (ds[i] > 32768)
                    continue;
                int n = mlen(y, c, ds[i], max);
                if (n > best) {
                    best = n;
                    bd = ds[i];
                }
            }
            if (best >= 3) {
                match(best, bd);
                c += best;
            } else
                lit(r[c++]);
        }
    }
    lit(256);
    if (nbits)
        pbits(0, 8 - (int)nbits);
    be32(a2 << 16 | a1);
    if (!bad)
        chunk_end(s);
    s = chunk_begin("IEND");
    if (!bad)
        chunk_end(s);
    if (!bad)
        have = 1;
out:
    for (int i = 0; i < NR; i++) {
        if (rows[i])
            kfree(rows[i]);
        rows[i] = 0;
    }
    if (!have) {
        ksnprintf(shot_note, sizeof shot_note, "%s",
                  TR("Screenshot: zu wenig Speicher", "Screenshot: not enough memory"));
        shot_until = ticks + 300;
    }
}
static int two(char *o, int v) {
    o[0] = (char)('0' + v / 10 % 10);
    o[1] = (char)('0' + v % 10);
    return 2;
}
/* saves the waiting picture; 1 when something changed on the screen (the note) */
int shot_save(void) {
    if (!have)
        return 0;
    have = 0;
    rtc_t t;
    rtc_read(&t);
    char name[64], path[96];
    int k = 0;
    const char *p = "Screenshot ";
    while (*p)
        name[k++] = *p++;
    name[k++] = (char)('0' + t.year / 1000 % 10);
    name[k++] = (char)('0' + t.year / 100 % 10);
    k += two(name + k, t.year % 100);
    name[k++] = '-';
    k += two(name + k, t.mon);
    name[k++] = '-';
    k += two(name + k, t.day);
    name[k++] = ' ';
    k += two(name + k, t.h);
    name[k++] = '-';
    k += two(name + k, t.m);
    name[k++] = '-';
    k += two(name + k, t.s);
    name[k] = 0;
    u32 now = fs_now();
    fsx_mkdir("/home/Screenshots", now);
    ksnprintf(path, sizeof path, "/home/Screenshots/%s.png", name);
    for (int i = 2; i < 100 && fsx_exists(path); i++)
        ksnprintf(path, sizeof path, "/home/Screenshots/%s (%d).png", name, i);
    int r = 0;
    for (u64 o = 0; o < bn && !r;) { /* in parts of 1 MB */
        u64 n = bn - o < (1 << 20) ? bn - o : (1 << 20);
        r = o ? fsx_append(path, buf + o, n, now) : fsx_write(path, buf, n, now);
        o += n;
    }
    if (r)
        fsx_remove(path);
    if (bcap > (4u << 20)) { /* a big one (a photo): give the memory back */
        kfree(buf);
        buf = 0;
        bcap = 0;
    }
    if (r)
        ksnprintf(shot_note, sizeof shot_note, "%s",
                  r == VE_NOSPC ? TR("Screenshot: kein Platz mehr in /home",
                                     "Screenshot: no room left in /home")
                                : TR("Screenshot konnte nicht gespeichert werden",
                                     "Screenshot could not be saved"));
    else
        ksnprintf(shot_note, sizeof shot_note, "%s Screenshots/%s", TR("Gespeichert:", "Saved:"),
                  path + 18); /* the name as saved, with a number if there was one */
    shot_until = ticks + 400;
    return 1;
}
