/* The analog clock face, shared by the screen saver in the kernel and the Clock program in ring 3.
 * No floating point: angles come from a sine table, lengths are fixed point with 8 fractional
 * bits (256 = one pixel). Needs ui_rgb, rtc_read, kmalloc and kfree from the including file. */
#pragma once
#include "sintab.h"
#define CN 1024
typedef struct {
    int pw, ph;   /* picture size in pixels */
    u32 sig;      /* palette the face was drawn with */
    u32 *face;    /* face without hands, RGB */
    u32 *work;    /* face with hands, RGB */
    u8 *px[2];    /* the two palette pictures */
    int cur, sec; /* picture shown, second drawn (-1: none yet) */
    rtc_t t;
    u32 ck[CN]; /* cache RGB -> palette index */
    u8 cv[CN];
} clk_t;

static u32 rgb256(int i) {
    if (i < 16)
        return ui_rgb(i);
    static const u8 L[6] = {0, 95, 135, 175, 215, 255};
    if (i < 232) {
        int v = i - 16;
        return (u32)L[v / 36] << 16 | (u32)L[(v / 6) % 6] << 8 | L[v % 6];
    }
    u32 g = 8 + 10 * (i - 232);
    return g << 16 | g << 8 | g;
}
static u8 nearest(clk_t *c, u32 rgb) {
    u32 h = (rgb * 2654435761u) >> 22;
    if (c->ck[h] == rgb)
        return c->cv[h];
    int r = rgb >> 16, g = (rgb >> 8) & 255, b = rgb & 255, best = 0;
    long bd = -1;
    for (int i = 0; i < 256; i++) {
        u32 q = rgb256(i);
        long dr = r - (int)(q >> 16), dg = g - (int)((q >> 8) & 255), db = b - (int)(q & 255);
        long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
        if (bd < 0 || d < bd) {
            bd = d;
            best = i;
        }
    }
    c->ck[h] = rgb;
    c->cv[h] = (u8)best;
    return (u8)best;
}
/* a over b with coverage k (0..256) */
static u32 blend(u32 a, u32 b, int k) {
    if (k <= 0)
        return b;
    if (k >= 256)
        return a;
    u32 r = (((a >> 16) & 255) * k + ((b >> 16) & 255) * (256 - k)) >> 8;
    u32 g = (((a >> 8) & 255) * k + ((b >> 8) & 255) * (256 - k)) >> 8;
    u32 l = ((a & 255) * k + (b & 255) * (256 - k)) >> 8;
    return r << 16 | g << 8 | l;
}
static long isqrt(long v) {
    long r = 0, bit = 1L << 40;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
        bit >>= 2;
    }
    return r;
}
static int clamp256(long v) {
    return v < 0 ? 0 : v > 256 ? 256 : (int)v;
}
/* Unit vector for an angle in half degrees, 0 = 12 o'clock, clockwise (y points down). */
static void dir(int a, int *ux, int *uy) {
    a = ((a % 720) + 720) % 720;
    *ux = sintab[a];
    *uy = -sintab[(a + 180) % 720];
}
/* Coverage of the pixel at (x, y) by a stroke along (ux, uy) from a0 to a1 with half width hw and
 * round ends. Everything relative to the centre, in 1/256 pixels. */
static int stroke(long x, long y, int ux, int uy, long a0, long a1, long hw) {
    long perp = (x * uy - y * ux) >> 14;
    if (perp < 0)
        perp = -perp;
    if (perp > hw + 256)
        return 0;
    long t = (x * ux + y * uy) >> 14, d;
    if (t >= a0 && t <= a1)
        d = perp;
    else {
        long e = t < a0 ? a0 : a1, ex = x - e * ux / 16384, ey = y - e * uy / 16384;
        d = isqrt(ex * ex + ey * ey);
    }
    return clamp256(hw + 128 - d);
}
static u32 palsig(void) {
    u32 s = 0;
    for (int i = 0; i < 16; i++)
        s = s * 31 + ui_rgb(i);
    return s;
}
static long radius(clk_t *c) {
    int m = c->pw < c->ph ? c->pw : c->ph;
    return (long)(m / 2 - 2) * 256;
}
/* The face: background, dial, ring and the 60 marks. Drawn again only after a resize or a
 * change of the colours. */
static void face(clk_t *c) {
    u32 bg = ui_rgb(C_PANEL), hour = ui_rgb(C_LAV), minute = ui_rgb(C_MID);
    long R = radius(c), cx = (long)c->pw * 128, cy = (long)c->ph * 128;
    long hl = R / 7, ml = R / 18 + 64, top = R - 128;
    int ux[60], uy[60];
    for (int i = 0; i < 60; i++)
        dir(i * 12, &ux[i], &uy[i]);
    for (int py = 0; py < c->ph; py++)
        for (int px = 0; px < c->pw; px++) {
            long x = (long)px * 256 + 128 - cx, y = (long)py * 256 + 128 - cy;
            long r = isqrt(x * x + y * y);
            u32 v = bg;
            if (r < top + 256 && r > top - hl - 512)
                for (int i = 0; i < 60; i++) {
                    int big = i % 5 == 0;
                    int k = stroke(x, y, ux[i], uy[i], top - (big ? hl : ml), top,
                                   big ? R / 70 + 140 : R / 160 + 70);
                    if (k)
                        v = blend(big ? hour : minute, v, k);
                }
            c->face[py * c->pw + px] = v;
        }
}
static void hand(clk_t *c, u32 col, int a, long tail, long len, long hw, long off, int alpha) {
    int ux, uy;
    dir(a, &ux, &uy);
    long cx = (long)c->pw * 128 + off, cy = (long)c->ph * 128 + off;
    for (int py = 0; py < c->ph; py++)
        for (int px = 0; px < c->pw; px++) {
            long x = (long)px * 256 + 128 - cx, y = (long)py * 256 + 128 - cy;
            int k = stroke(x, y, ux, uy, -tail, len, hw);
            if (k) {
                u32 *p = &c->work[py * c->pw + px];
                *p = blend(col, *p, k * alpha / 256);
            }
        }
}
static void dot(clk_t *c, u32 col, long rad) {
    long cx = (long)c->pw * 128, cy = (long)c->ph * 128;
    for (int py = 0; py < c->ph; py++)
        for (int px = 0; px < c->pw; px++) {
            long x = (long)px * 256 + 128 - cx, y = (long)py * 256 + 128 - cy;
            if (x > rad + 256 || x < -rad - 256 || y > rad + 256 || y < -rad - 256)
                continue;
            u32 *p = &c->work[py * c->pw + px];
            *p = blend(col, *p, clamp256(rad + 128 - isqrt(x * x + y * y)));
        }
}
static void freebufs(clk_t *c) {
    kfree(c->face);
    kfree(c->work);
    kfree(c->px[0]);
    kfree(c->px[1]);
    c->face = c->work = 0;
    c->px[0] = c->px[1] = 0;
}
/* Draws the clock for the time in c->t into the picture that is not shown, then shows it. */
static void render(clk_t *c, int pw, int ph) {
    if (pw != c->pw || ph != c->ph || !c->face) {
        freebufs(c);
        c->pw = pw;
        c->ph = ph;
        u64 n = (u64)pw * ph;
        c->face = kmalloc(n * 4);
        c->work = kmalloc(n * 4);
        c->px[0] = kmalloc(n);
        c->px[1] = kmalloc(n);
        if (!c->face || !c->work || !c->px[0] || !c->px[1]) {
            freebufs(c);
            return;
        }
        c->sig = 0;
    }
    u32 s = palsig();
    if (s != c->sig) {
        c->sig = s;
        memset(c->ck, 0xFF, sizeof c->ck);
        face(c);
    }
    memcpy(c->work, c->face, (u64)pw * ph * 4);
    long R = radius(c), sh = R / 40 + 96;
    int h = c->t.h % 12, m = c->t.m, sec = c->t.s;
    int ah = h * 60 + m, am = m * 12 + sec / 5, as = sec * 12;
    u32 shadow = ui_rgb(C_SHADOW);
    hand(c, shadow, ah, R / 10, R * 52 / 100, R / 28 + 120, sh, 140);
    hand(c, shadow, am, R / 10, R * 78 / 100, R / 40 + 90, sh, 140);
    hand(c, ui_rgb(C_GLOW), ah, R / 10, R * 52 / 100, R / 28 + 120, 0, 256);
    hand(c, ui_rgb(C_LAV), am, R / 10, R * 78 / 100, R / 40 + 90, 0, 256);
    hand(c, shadow, as, R / 5, R * 84 / 100, R / 110 + 60, sh, 120);
    hand(c, ui_rgb(C_HOT), as, R / 5, R * 84 / 100, R / 110 + 60, 0, 256);
    dot(c, ui_rgb(C_HOT), R / 22 + 200);
    dot(c, ui_rgb(C_PANEL), R / 70 + 60);
    int nx = c->cur ^ 1;
    u8 *o = c->px[nx];
    for (u64 i = 0, n = (u64)pw * ph; i < n; i++)
        o[i] = nearest(c, c->work[i]);
    c->cur = nx;
}
#ifndef RING3
/* The analog clock as a picture of its own, for the screen saver. clock_picture(0, 0) frees it. */
const u8 *clock_picture(int pw, int ph) {
    static clk_t *c;
    if (!pw) {
        if (c) {
            freebufs(c);
            kfree(c);
            c = 0;
        }
        return 0;
    }
    if (!c) {
        c = kmalloc(sizeof *c);
        if (!c)
            return 0;
        memset(c, 0, sizeof *c);
        c->sec = -1;
    }
    rtc_read(&c->t);
    if (c->t.s != c->sec || pw != c->pw || ph != c->ph) {
        c->sec = c->t.s;
        render(c, pw, ph);
    }
    return c->face ? c->px[c->cur] : 0;
}
#endif
