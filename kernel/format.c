#include "format.h"
#ifdef HOST
#include <string.h>
#else
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strlen(const char *);
#endif
#include "cp437.h"
#include "upcase.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define PSTART 2048 /* the partition starts at 1 MiB, like on Windows and Linux */
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
static u16 cp2u(u8 c) {
    if (c < 0x80)
        return c;
    for (unsigned i = 0; i < sizeof cp437_map / sizeof cp437_map[0]; i++)
        if (cp437_map[i].c == c)
            return cp437_map[i].u;
    return '_';
}
/* Writing with a progress report every 256 sectors */
typedef struct {
    const vfs_dev *d;
    u64 done, total;
    void (*prog)(u64, u64);
} wr_t;
static u8 zero[512];
static int put(wr_t *w, u64 s, const void *b) {
    if (s >= w->d->sectors || w->d->wr(w->d->start + s, b))
        return VE_IO;
    if (w->prog && !(++w->done % 256))
        w->prog(w->done, w->total);
    return 0;
}
static int zeros(wr_t *w, u64 s, u64 n) {
    if (w->d->wrn) { /* 128 sectors per command */
        static u8 z[128 * 512];
        while (n) {
            u64 k = n > 128 ? 128 : n;
            if (s + k > w->d->sectors || w->d->wrn(w->d->start + s, (u32)k, z))
                return VE_IO;
            u64 was = w->done;
            w->done += k;
            if (w->prog && was / 256 != w->done / 256)
                w->prog(w->done, w->total);
            s += k;
            n -= k;
        }
        return 0;
    }
    for (u64 i = 0; i < n; i++)
        if (put(w, s + i, zero))
            return VE_IO;
    return 0;
}
/* Clears the old partition table and the end of the drive, where a GPT keeps its copy */
static int wipe(wr_t *w) {
    u64 n = w->d->sectors;
    if (zeros(w, 0, PSTART))
        return VE_IO;
    return zeros(w, n - 34, 34);
}
static int mbr(wr_t *w, u8 type, u32 serial) {
    static u8 b[512];
    memset(b, 0, 512);
    W32(b, 440, serial);
    u8 *e = b + 446;
    e[1] = 0xFE; /* CHS values are unused: the maximum, as usual */
    e[2] = 0xFF;
    e[3] = 0xFF;
    e[4] = type;
    e[5] = 0xFE;
    e[6] = 0xFF;
    e[7] = 0xFF;
    W32(e, 8, PSTART);
    W32(e, 12, (u32)(w->d->sectors - PSTART));
    b[510] = 0x55;
    b[511] = 0xAA;
    return put(w, 0, b);
}
static int check(const vfs_dev *d) {
    if (d->start || d->sectors <= PSTART + 64)
        return VE_NOSPC;
    if (d->sectors > 0xFFFFFFFFull)
        return VE_INVAL; /* MBR partitions end at 2 TB */
    return 0;
}

/* ---------- FAT32 ---------- */
int fmt_fat32(const vfs_dev *d, const char *label, u32 serial, void (*prog)(u64, u64)) {
    int r = check(d);
    if (r)
        return r;
    u64 pn = d->sectors - PSTART, bytes = pn * 512;
    /* Cluster sizes as Windows chooses them */
    u32 spc = bytes <= (260ull << 20)  ? 1
              : bytes <= (8ull << 30)  ? 8
              : bytes <= (16ull << 30) ? 16
              : bytes <= (32ull << 30) ? 32
                                       : 64;
    u32 rsvd = 32, fz = 1, cl = 0;
    for (int i = 0; i < 64; i++) {
        u64 meta = (u64)rsvd + 2ull * fz;
        if (meta >= pn)
            return VE_NOSPC;
        cl = (u32)((pn - meta) / spc);
        u32 nfz = (u32)(((u64)cl + 2) * 4 / 512 + 1);
        if (nfz <= fz)
            break;
        fz = nfz;
    }
    rsvd += (8 - (rsvd + 2 * fz) % 8) % 8; /* data area on a 4 KiB boundary */
    cl = (u32)((pn - rsvd - 2ull * fz) / spc);
    if ((u64)cl + 2 > (u64)fz * 128)
        cl = fz * 128 - 2;
    if (cl < 65525 || cl > 0x0FFFFFF5)
        return VE_NOSPC; /* below 65525 clusters it would count as FAT16 */
    u32 data = rsvd + 2 * fz;
    wr_t w = {d, 0, PSTART + 34 + 2ull * fz + spc + 8, prog};
    if (wipe(&w))
        return VE_IO;
    u64 P = PSTART;
    static u8 b[512];
    char lab[11];
    memset(lab, ' ', 11);
    int ln = 0;
    for (int i = 0; label && label[i] && ln < 11; i++) {
        u8 c = (u8)label[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c < 32 || c >= 0x80 || c == '"' || c == '*' || c == '+' || c == ',' || c == '.' ||
            c == '/' || c == ':' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' ||
            c == '[' || c == '\\' || c == ']' || c == '|')
            c = '_';
        lab[ln++] = (char)c;
    }
    /* FATs: everything free, only the root directory in cluster 2 */
    for (u32 f = 0; f < 2; f++) {
        memset(b, 0, 512);
        W32(b, 0, 0x0FFFFFF8);
        W32(b, 4, 0x0FFFFFFF);
        W32(b, 8, 0x0FFFFFFF);
        if (put(&w, P + rsvd + f * fz, b) || zeros(&w, P + rsvd + f * fz + 1, fz - 1))
            return VE_IO;
    }
    if (zeros(&w, P + data, spc))
        return VE_IO;
    if (ln) {
        memset(b, 0, 512);
        memcpy(b, lab, 11);
        b[11] = 0x08; /* volume label */
        if (put(&w, P + data, b))
            return VE_IO;
    }
    memset(b, 0, 512);
    W32(b, 0, 0x41615252);
    W32(b, 484, 0x61417272);
    W32(b, 488, cl - 1);
    W32(b, 492, 3);
    W32(b, 508, 0xAA550000);
    if (put(&w, P + 1, b) || put(&w, P + 7, b))
        return VE_IO;
    memset(b, 0, 512);
    b[0] = 0xEB;
    b[1] = 0x58;
    b[2] = 0x90;
    memcpy(b + 3, "VIOLYX  ", 8);
    W16(b, 11, 512);
    b[13] = (u8)spc;
    W16(b, 14, rsvd);
    b[16] = 2;
    b[21] = 0xF8;
    W16(b, 24, 63);
    W16(b, 26, 255);
    W32(b, 28, PSTART);
    W32(b, 32, (u32)pn);
    W32(b, 36, fz);
    W32(b, 44, 2);
    W16(b, 48, 1);
    W16(b, 50, 6);
    b[64] = 0x80;
    b[66] = 0x29;
    W32(b, 67, serial);
    memcpy(b + 71, ln ? lab : "NO NAME    ", 11);
    memcpy(b + 82, "FAT32   ", 8);
    b[90] = 0xCD; /* not bootable: int 18h hands back to the BIOS */
    b[91] = 0x18;
    b[510] = 0x55;
    b[511] = 0xAA;
    if (put(&w, P + 6, b))
        return VE_IO;
    d->fl();
    if (put(&w, P, b))
        return VE_IO;
    if (mbr(&w, 0x0C, serial))
        return VE_IO;
    d->fl();
    return 0;
}

/* ---------- exFAT ---------- */
static u32 bootsum(const u8 *s, int n, u32 c) {
    for (int i = 0; i < n; i++)
        c = ((c & 1) ? 0x80000000u : 0) + (c >> 1) + s[i];
    return c;
}
int fmt_exfat(const vfs_dev *d, const char *label, u32 serial, void (*prog)(u64, u64)) {
    int r = check(d);
    if (r)
        return r;
    u64 pn = d->sectors - PSTART, bytes = pn * 512;
    u32 sh = bytes <= (256ull << 20) ? 3 : bytes <= (32ull << 30) ? 6 : 8; /* 4K, 32K, 128K */
    u32 spc = 1u << sh, cs = 512u << sh;
    u32 fo = pn >= (64u << 11) ? 2048 : 128, fl = 1, heap = 0, cl = 0;
    for (int i = 0; i < 64; i++) {
        heap = (fo + fl + spc - 1) / spc * spc;
        if (heap >= pn)
            return VE_NOSPC;
        cl = (u32)((pn - heap) >> sh);
        u32 nfl = (u32)((((u64)cl + 2) * 4 + 511) / 512);
        if (nfl <= fl)
            break;
        fl = nfl;
    }
    u64 bml = ((u64)cl + 7) / 8;
    u32 nb = (u32)((bml + cs - 1) / cs), nu = (u32)((sizeof upcase_tab + cs - 1) / cs);
    if (cl < 16 || nb + nu + 1 >= cl)
        return VE_NOSPC;
    u32 cup = 2 + nb, croot = cup + nu, used = nb + nu + 1;
    wr_t w = {d, 0, PSTART + 34 + fl + ((u64)(nb + nu + 1) << sh) + 24, prog};
    if (wipe(&w))
        return VE_IO;
    u64 P = PSTART;
    static u8 b[512];
    /* FAT: the chains of bitmap, up-case table and root directory */
    for (u32 s = 0; s < fl; s++) {
        memset(b, 0, 512);
        for (u32 k = 0; k < 128; k++) {
            u32 c = s * 128 + k, v = 0;
            if (c == 0)
                v = 0xFFFFFFF8;
            else if (c == 1)
                v = 0xFFFFFFFF;
            else if (c >= 2 && c < 2 + used) {
                int last = c == 1 + nb || c == cup + nu - 1 || c == croot;
                v = last ? 0xFFFFFFFF : c + 1;
            }
            W32(b, (int)k * 4, v);
        }
        if (put(&w, P + fo + s, b))
            return VE_IO;
        if (s * 128 > 2 + used && s + 1 < fl) { /* the rest is free */
            if (zeros(&w, P + fo + s + 1, fl - s - 1))
                return VE_IO;
            break;
        }
    }
    /* Allocation bitmap */
    u64 bs = P + heap;
    for (u64 s = 0; s < ((u64)nb << sh); s++) {
        memset(b, 0, 512);
        for (u32 k = 0; k < 4096; k++) {
            u64 bit = s * 4096 + k;
            if (bit < used)
                b[k / 8] |= (u8)(1 << (k % 8));
        }
        if (put(&w, bs + s, b))
            return VE_IO;
    }
    /* Up-case table */
    u64 us = P + heap + ((u64)(cup - 2) << sh);
    u32 tchk = 0;
    for (u64 s = 0; s < ((u64)nu << sh); s++) {
        memset(b, 0, 512);
        u64 o = s * 512;
        if (o < sizeof upcase_tab) {
            u64 n = sizeof upcase_tab - o > 512 ? 512 : sizeof upcase_tab - o;
            memcpy(b, upcase_tab + o, n);
            tchk = bootsum(b, (int)n, tchk);
        }
        if (put(&w, us + s, b))
            return VE_IO;
    }
    /* Root directory: label, bitmap and up-case table */
    u64 rs = P + heap + ((u64)(croot - 2) << sh);
    if (zeros(&w, rs, spc))
        return VE_IO;
    memset(b, 0, 512);
    int ln = 0;
    for (int i = 0; label && label[i] && ln < 11; i++)
        W16(b, 2 + 2 * ln++, cp2u((u8)label[i]));
    b[0] = ln ? 0x83 : 0x03;
    b[1] = (u8)ln;
    b[32] = 0x81;
    W32(b, 32 + 20, 2);
    W64(b, 32 + 24, bml);
    b[64] = 0x82;
    W32(b, 64 + 4, tchk);
    W32(b, 64 + 20, cup);
    W64(b, 64 + 24, sizeof upcase_tab);
    if (put(&w, rs, b))
        return VE_IO;
    /* Boot region: main and backup with their checksum sector */
    static u8 boot[12][512];
    memset(boot, 0, sizeof boot);
    u8 *s0 = boot[0];
    s0[0] = 0xEB;
    s0[1] = 0x76;
    s0[2] = 0x90;
    memcpy(s0 + 3, "EXFAT   ", 8);
    W64(s0, 64, PSTART);
    W64(s0, 72, pn);
    W32(s0, 80, fo);
    W32(s0, 84, fl);
    W32(s0, 88, heap);
    W32(s0, 92, cl);
    W32(s0, 96, croot);
    W32(s0, 100, serial);
    W16(s0, 104, 0x0100);
    s0[108] = 9;
    s0[109] = (u8)sh;
    s0[110] = 1;
    s0[111] = 0x80;
    memset(s0 + 120, 0xF4, 390); /* boot code: hlt */
    s0[510] = 0x55;
    s0[511] = 0xAA;
    for (int i = 1; i <= 8; i++)
        W32(boot[i], 508, 0xAA550000);
    u32 c = 0;
    for (int i = 0; i < 11; i++)
        for (int k = 0; k < 512; k++)
            if (i || (k != 106 && k != 107 && k != 112))
                c = ((c & 1) ? 0x80000000u : 0) + (c >> 1) + boot[i][k];
    for (int k = 0; k < 128; k++)
        W32(boot[11], k * 4, c);
    for (int i = 0; i < 12; i++)
        if (put(&w, P + 12 + i, boot[i]))
            return VE_IO;
    d->fl();
    for (int i = 11; i >= 0; i--)
        if (put(&w, P + i, boot[i]))
            return VE_IO;
    if (mbr(&w, 0x07, serial))
        return VE_IO;
    d->fl();
    return 0;
}
