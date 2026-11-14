#include "cdda.h"
#ifdef HOST
#include <string.h>
#else
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strlen(const char *);
#endif
#include "cp437.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define SS 2352u /* bytes per audio sector */
static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
int cda_toc(cdda *a, const u8 *t, int len) {
    memset(a, 0, sizeof *a);
    if (len < 12)
        return -1;
    int dl = t[0] << 8 | t[1];
    if (dl + 2 < len)
        len = dl + 2;
    int first = t[2], last = t[3];
    if (first < 1 || last > CDA_MAX || first > last)
        return -1;
    /* every track and the lead-out, in order */
    int n = 0, haslead = 0;
    u32 st[CDA_MAX + 1];
    u8 tn[CDA_MAX + 1], data[CDA_MAX + 1];
    for (int o = 4; o + 8 <= len && n <= CDA_MAX; o += 8) {
        const u8 *d = t + o;
        u32 lba = be32(d + 4);
        if (lba > 500000) /* more than 111 minutes: not a CD */
            return -1;
        if (d[2] == 0xAA) {
            a->lead = lba;
            haslead = 1;
            break;
        }
        if (d[2] < first || d[2] > last || (n && (d[2] <= tn[n - 1] || lba <= st[n - 1])))
            return -1;
        st[n] = lba;
        tn[n] = d[2];
        data[n] = (d[1] & 4) != 0;
        n++;
    }
    if (!haslead || !n || a->lead <= st[n - 1])
        return -1;
    a->first = first;
    a->last = last;
    for (int i = 0; i < n; i++) {
        a->tstart[tn[i]] = st[i];
        a->isdata[tn[i]] = data[i];
        if (data[i])
            continue;
        u32 end = i + 1 < n ? st[i + 1] : a->lead;
        /* audio before a data track of a later session (CD Extra): the gap is not music */
        if (i + 1 < n && data[i + 1] && end - st[i] > 11400 + 300)
            end -= 11400;
        a->num[a->n] = tn[i];
        a->start[a->n] = st[i];
        a->len[a->n] = end - st[i];
        a->n++;
    }
    if (!a->n)
        return -1;
    /* the TOC for MusicBrainz: first, last audio track, end of the audio, offsets (+150) */
    int la = a->n - 1;
    u32 lead = a->start[la] + a->len[la];
    char *o = a->toc;
    int k = 0;
    u32 v[CDA_MAX + 3];
    int nv = 0;
    v[nv++] = (u32)tn[0];
    v[nv++] = a->num[la];
    v[nv++] = lead + 150;
    for (int i = 0; i < n && tn[i] <= a->num[la]; i++)
        v[nv++] = st[i] + 150;
    for (int i = 0; i < nv; i++) {
        char d[12];
        int m = 0;
        u32 x = v[i];
        do
            d[m++] = (char)('0' + x % 10);
        while ((x /= 10) && m < 11);
        if (i)
            o[k++] = ' ';
        while (m)
            o[k++] = d[--m];
    }
    o[k++] = '\n';
    o[k] = 0;
    cda_names(a);
    return 0;
}
static char latin(u8 c) {
    if (c >= 32 && c < 127)
        return (char)c;
    for (unsigned i = 0; i < sizeof cp437_map / sizeof cp437_map[0]; i++)
        if (cp437_map[i].u == c)
            return (char)cp437_map[i].c;
    return c < 32 ? 0 : '?';
}
/* CD-Text: packs of 18 bytes; titles (0x80) and performers (0x81) of block 0 in ISO 8859-1. The
 * texts of consecutive tracks follow each other, each ended by a 0. A tab means "as before". */
void cda_text(cdda *a, const u8 *t, int len) {
    if (!t || len < 4 + 18)
        return;
    int dl = t[0] << 8 | t[1];
    if (dl + 2 < len)
        len = dl + 2;
    for (int type = 0x80; type <= 0x81; type++) {
        char(*dst)[CDA_TEXT] = type == 0x80 ? a->title : a->artist;
        char buf[CDA_TEXT];
        int bl = 0, trk = -1;
        for (int o = 4; o + 18 <= len; o += 18) {
            const u8 *p = t + o;
            if (p[0] != type || (p[3] & 0x70) || (p[3] & 0x80) || (p[1] & 0x80))
                continue; /* other blocks, double-byte texts and extensions are left out */
            if ((p[1] & 0x7F) != trk) {
                trk = p[1] & 0x7F;
                bl = 0;
            }
            for (int i = 4; i < 16; i++) {
                if (p[i]) {
                    if (bl < CDA_TEXT - 1)
                        buf[bl++] = (char)p[i];
                    continue;
                }
                buf[bl] = 0;
                if (trk <= CDA_MAX) {
                    if (bl == 1 && buf[0] == '\t' && trk > 1)
                        memcpy(dst[trk], dst[trk - 1], CDA_TEXT);
                    else {
                        int k = 0;
                        for (int j = 0; j < bl; j++) {
                            char c = latin((u8)buf[j]);
                            if (c && (k || c != ' '))
                                dst[trk][k++] = c;
                        }
                        while (k && dst[trk][k - 1] == ' ')
                            k--;
                        dst[trk][k] = 0;
                    }
                }
                trk++;
                bl = 0;
            }
        }
    }
    cda_names(a);
}
/* File names: "01 Title.wav", or "Track 01.wav" without CD-Text. Characters FAT does not allow in
 * names are left out, so a track can be copied anywhere as it is. */
void cda_names(cdda *a) {
    for (int i = 0; i < a->n; i++) {
        char *o = a->name[i];
        int t = a->num[i], k = 0;
        const char *s = a->title[t];
        if (s[0]) {
            o[k++] = (char)('0' + t / 10);
            o[k++] = (char)('0' + t % 10);
            o[k++] = ' ';
            int st = k;
            for (; *s && k < st + 56; s++) {
                char c = *s;
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                    c == '<' || c == '>' || c == '|')
                    c = c == ':' || c == '/' || c == '\\' || c == '|' ? '-' : 0;
                if (c && !(c == ' ' && o[k - 1] == ' '))
                    o[k++] = c;
            }
            while (k > st && (o[k - 1] == ' ' || o[k - 1] == '.'))
                k--;
            if (k == st)
                k = 0; /* nothing left of the title */
        }
        if (!k) { /* the number in front keeps two tracks with the same title apart */
            memcpy(o, "Track ", 6);
            k = 6;
            o[k++] = (char)('0' + t / 10);
            o[k++] = (char)('0' + t % 10);
        }
        memcpy(o + k, ".wav", 5);
    }
}
static int put4(u8 *h, int o, u32 v) {
    h[o] = (u8)v;
    h[o + 1] = (u8)(v >> 8);
    h[o + 2] = (u8)(v >> 16);
    h[o + 3] = (u8)(v >> 24);
    return o + 4;
}
static int sub(u8 *h, int o, const char *id, const char *s) {
    int l = (int)strlen(s);
    if (!l)
        return o;
    memcpy(h + o, id, 4);
    o = put4(h, o + 4, (u32)l + 1);
    for (int i = 0; i <= l; i++) { /* back to ISO 8859-1, as the disc had it */
        u8 c = (u8)s[i];
        if (c >= 128) {
            u8 x = '?';
            for (unsigned j = 0; j < sizeof cp437_map / sizeof cp437_map[0]; j++)
                if (cp437_map[j].c == c && cp437_map[j].u < 256)
                    x = (u8)cp437_map[j].u;
            c = x;
        }
        h[o + i] = c;
    }
    o += l + 1;
    if (o & 1)
        h[o++] = 0;
    return o;
}
/* RIFF, fmt, the tags as LIST INFO (title, performer, album, track number), data */
int cda_header(cdda *a, int i, u8 *h) {
    int t = a->num[i];
    memcpy(h, "RIFF", 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put4(h, 16, 16);
    h[20] = 1;
    h[21] = 0;
    h[22] = 2;
    h[23] = 0;
    put4(h, 24, 44100);
    put4(h, 28, 44100 * 4);
    h[32] = 4;
    h[33] = 0;
    h[34] = 16;
    h[35] = 0;
    int o = 36;
    const char *ar = a->artist[t][0] ? a->artist[t] : a->artist[0];
    if (a->title[t][0] || ar[0] || a->title[0][0]) {
        int l0 = o;
        memcpy(h + o, "LIST", 4);
        memcpy(h + o + 8, "INFO", 4);
        o += 12;
        char nr[4] = {(char)('0' + t / 10), (char)('0' + t % 10), 0, 0};
        if (nr[0] == '0') {
            nr[0] = nr[1];
            nr[1] = 0;
        }
        o = sub(h, o, "INAM", a->title[t]);
        o = sub(h, o, "IART", ar);
        o = sub(h, o, "IPRD", a->title[0]);
        o = sub(h, o, "ITRK", nr);
        put4(h, l0 + 4, (u32)(o - l0 - 8));
    }
    memcpy(h + o, "data", 4);
    u32 dl = a->len[i] * SS;
    o = put4(h, o + 4, dl);
    put4(h, 4, (u32)o - 8 + dl);
    return o;
}
static int which(cdda *a, const char *p) {
    while (*p == '/')
        p++;
    for (int i = 0; i < a->n; i++) {
        const char *x = a->name[i], *y = p;
        for (; *x && *y; x++, y++) {
            char c = *x, d = *y;
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (d >= 'A' && d <= 'Z')
                d += 32;
            if (c != d)
                break;
        }
        if (!*x && !*y)
            return i;
    }
    const char *x = "cdtoc.txt", *y = p;
    while (*x && *x == *y)
        x++, y++;
    return !*x && !*y ? a->n : -1;
}
int cda_stat(cdda *a, const char *p, vfs_ent *e) {
    const char *q = p;
    while (*q == '/')
        q++;
    if (!*q) {
        if (e) {
            memset(e, 0, sizeof *e);
            e->dir = 1;
        }
        return 0;
    }
    int i = which(a, p);
    if (i < 0)
        return VE_NOENT;
    if (e) {
        memset(e, 0, sizeof *e);
        static u8 h[512];
        const char *nm = i < a->n ? a->name[i] : "cdtoc.txt";
        memcpy(e->name, nm, strlen(nm) + 1);
        e->size = i < a->n ? (u64)cda_header(a, i, h) + (u64)a->len[i] * SS : strlen(a->toc);
    }
    return 0;
}
int cda_list(cdda *a, const char *p, int (*cb)(const vfs_ent *, void *), void *arg) {
    const char *q = p;
    while (*q == '/')
        q++;
    if (*q)
        return which(a, p) < 0 ? VE_NOENT : VE_NOTDIR;
    vfs_ent e;
    for (int i = 0; i <= a->n; i++) {
        cda_stat(a, i < a->n ? a->name[i] : "cdtoc.txt", &e);
        if (cb(&e, arg))
            break;
    }
    return 0;
}
int64_t cda_read_at(cdda *a, const char *p, void *b, u64 off, u64 max) {
    int i = which(a, p);
    if (i < 0) {
        const char *q = p;
        while (*q == '/')
            q++;
        return *q ? VE_NOENT : VE_ISDIR;
    }
    u8 *o = b;
    if (i == a->n) {
        u64 l = strlen(a->toc);
        if (off >= l)
            return 0;
        if (max > l - off)
            max = l - off;
        memcpy(o, a->toc + off, max);
        return (int64_t)max;
    }
    static u8 h[512], s[SS];
    u64 hl = (u64)cda_header(a, i, h), size = hl + (u64)a->len[i] * SS;
    if (off >= size)
        return 0;
    if (max > size - off)
        max = size - off;
    u64 n = 0;
    while (n < max) {
        u64 at = off + n;
        if (at < hl) {
            u64 k = hl - at < max - n ? hl - at : max - n;
            memcpy(o + n, h + at, k);
            n += k;
            continue;
        }
        u64 d = at - hl, in = d % SS;
        u32 lba = a->start[i] + (u32)(d / SS);
        if (!in && max - n >= SS) { /* whole sectors straight into the buffer */
            u32 cnt = (u32)((max - n) / SS);
            if (cnt > 32)
                cnt = 32;
            if (a->rd(a->ctx, lba, cnt, o + n))
                return n ? (int64_t)n : VE_IO;
            n += (u64)cnt * SS;
            continue;
        }
        if (a->rd(a->ctx, lba, 1, s))
            return n ? (int64_t)n : VE_IO;
        u64 k = SS - in < max - n ? SS - in : max - n;
        memcpy(o + n, s + in, k);
        n += k;
    }
    return (int64_t)n;
}
