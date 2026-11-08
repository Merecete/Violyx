/* Screen savers: the Violyx logo with twinkling stars, wire frame models, a seven-segment clock,
 * the analog clock and a synthwave sunset. They draw a picture of (W*4) x (H*8) pixels in the 256
 * colours, which ui_blit puts on the screen. Any key ends them (the key is not passed on). */
#include <stdint.h>
#include <string.h>
#include "os.h"
#include "sintab.h"

int cfg_saver, cfg_swait = 2; /* screen saver (0 off) and waiting time (index into saver_min) */
const int saver_min[SWAITS] = {1, 2, 3, 5, 10, 15, 30};
static u8 *buf;
static int pw, ph;

const char *saver_name(int i) {
    static const char *de[SAVERS] = {"Aus",        "Violyx-Logo", "Drahtmodelle",
                                     "Digitaluhr", "Analoguhr",   "Synthwave"},
                      *en[SAVERS] = {"Off",           "Violyx logo",  "Wire frames",
                                     "Digital clock", "Analog clock", "Synthwave"};
    return TR(de[i], en[i]);
}
static int isin(int a) { /* a in half degrees, result * 16384 */
    a = ((a % 720) + 720) % 720;
    return sintab[a];
}
static int icos(int a) {
    return isin(a + 180);
}
static u32 rnd(u32 *s) {
    *s = *s * 1103515245u + 12345u;
    return *s >> 16;
}
static void pset(int x, int y, u8 c) {
    if (x >= 0 && y >= 0 && x < pw && y < ph)
        buf[y * pw + x] = c;
}
static void rect(int x, int y, int w, int h, u8 c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            pset(i, j, c);
}
static void line(int x0, int y0, int x1, int y1, u8 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int g = 0; g < 4096; g++) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}
/* Nearest colour of the colour cube and grey ramp (16-255), so the colour scheme does not change
 * it. Cached for 15-bit colours. */
static u8 *ncache; /* 32768 entries, 0 = not known yet */
static u8 near(u32 rgb) {
    u32 r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    u32 k = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
    if (ncache && ncache[k])
        return ncache[k];
    static const u8 L[6] = {0, 95, 135, 175, 215, 255};
    int best = 16;
    long bd = -1;
    for (int i = 16; i < 256; i++) {
        int qr, qg, qb;
        if (i < 232) {
            qr = L[(i - 16) / 36];
            qg = L[((i - 16) / 6) % 6];
            qb = L[(i - 16) % 6];
        } else
            qr = qg = qb = 8 + 10 * (i - 232);
        long d = 2L * (r - qr) * (r - qr) + 4L * (g - qg) * (g - qg) + 3L * (b - qb) * (b - qb);
        if (bd < 0 || d < bd) {
            bd = d;
            best = i;
        }
    }
    if (ncache)
        ncache[k] = (u8)best;
    return (u8)best;
}
static u32 mix(u32 a, u32 b, int k, int n) { /* a..b, step k of n */
    u32 o = 0;
    for (int s = 0; s <= 16; s += 8) {
        int x = (a >> s) & 255, y = (b >> s) & 255;
        o |= (u32)(x + (y - x) * k / n) << s;
    }
    return o;
}
static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
/* colour with ordered dithering: smooth gradients from the 256 colours */
static u8 dith(u32 rgb, int x, int y) {
    int o = (bayer[y & 3][x & 3] - 8) * 3;
    u32 v = 0;
    for (int s = 0; s <= 16; s += 8) {
        int c = (int)((rgb >> s) & 255) + o;
        v |= (u32)(c < 0 ? 0 : c > 255 ? 255 : c) << s;
    }
    return near(v);
}

/* ---------- 1: Violyx logo with stars that fade in and out ---------- */
#define NSTAR 400
static struct {
    short x, y;
    u16 ph, sp;
} star[NSTAR];
static int nstar;
static void logo_init(void) {
    u32 r = 4711;
    int lw = W, lh = H * 2;
    nstar = lw * lh / 90;
    if (nstar > NSTAR)
        nstar = NSTAR;
    for (int i = 0; i < nstar; i++) {
        star[i].x = (short)(rnd(&r) % lw);
        star[i].y = (short)(rnd(&r) % lh);
        star[i].ph = (u16)(rnd(&r) % 4000);
        star[i].sp = (u16)(2500 + rnd(&r) % 5000);
    }
}
static void logo_draw(u32 ms) {
    static const char *l[] = {
        "#   #  ###   ###   #     #   #  #   #", "#   #   #   #   #  #      # #    # # ",
        "#   #   #   #   #  #       #      #  ", " # #    #   #   #  #       #     # # ",
        "  #    ###   ###   #####   #    #   #"};
    static const u8 cl[] = {C_GLOW, C_LAV, C_NEON, C_VIOLET, C_SEL};
    static const u8 ramp[] = {C_VOID, C_RAISE, C_SEL, C_DIM, C_MID, C_LAV, C_GLOW};
    memset(buf, C_VOID, (u64)pw * ph);
    /* the lettering fills about 80 % of the width; P: pixels per point */
    int P = pw * 8 / 10 / 36;
    if (P * 5 > ph / 3)
        P = ph / 15;
    if (P < 2)
        P = 2;
    int ox = (pw - 36 * P) / 2, oy = (ph - 5 * P) / 2;
    for (int i = 0; i < nstar; i++) {
        int x = star[i].x, y = star[i].y;
        if (x * 4 >= ox - 2 * P && x * 4 < ox + 38 * P && y * 4 >= oy - 3 * P && y * 4 < oy + 8 * P)
            continue; /* no stars around the lettering */
        /* each star glows up and fades on its own rhythm, and rests dark in between */
        u32 t = (ms + star[i].ph * 7u) % star[i].sp;
        int half = star[i].sp / 3, lv = 0;
        if (t < (u32)half)
            lv = (int)(t * 12 / half);
        else if (t < (u32)half * 2)
            lv = (int)((half * 2 - t) * 12 / half);
        lv = lv > 6 ? 6 : lv;
        if (lv)
            rect(x * 4 + 1, y * 4 + 1, 2, 2, ramp[lv]);
        if (lv >= 5) { /* the brightest ones sparkle a little */
            pset(x * 4, y * 4 + 1, ramp[lv - 3]);
            pset(x * 4 + 3, y * 4 + 2, ramp[lv - 3]);
        }
    }
    for (int y = 0; y < 5; y++)
        for (int x = 0; l[y][x]; x++)
            if (l[y][x] == '#') {
                int u = ox + x * P, v = oy + y * P, sh = P / 3 < 1 ? 1 : P / 3;
                rect(u + sh, v + sh, P, P, C_PANEL); /* shadow */
                rect(u, v, P, P, cl[y]);
            }
}

/* ---------- 2: wire frame models ---------- */
typedef struct {
    int nv, ne;
    short v[20][3];
    u8 e[30][2];
} model;
static model mods[6];
static int nmod;
static void mk(model *m, const short (*v)[3], int nv, int len2) {
    /* edges: all pairs of points with the given squared distance */
    m->nv = nv;
    m->ne = 0;
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++)
            m->v[i][k] = v[i][k];
    for (int i = 0; i < nv; i++)
        for (int j = i + 1; j < nv; j++) {
            long d = 0;
            for (int k = 0; k < 3; k++)
                d += (long)(v[i][k] - v[j][k]) * (v[i][k] - v[j][k]);
            if (d > len2 * 98L / 100 && d < len2 * 102L / 100 && m->ne < 30) {
                m->e[m->ne][0] = (u8)i;
                m->e[m->ne][1] = (u8)j;
                m->ne++;
            }
        }
}
static void wire_init(void) {
    static const short cube[8][3] = {{-100, -100, -100}, {100, -100, -100}, {-100, 100, -100},
                                     {100, 100, -100},   {-100, -100, 100}, {100, -100, 100},
                                     {-100, 100, 100},   {100, 100, 100}};
    static const short tet[4][3] = {
        {115, 115, 115}, {115, -115, -115}, {-115, 115, -115}, {-115, -115, 115}};
    static const short oct[6][3] = {{150, 0, 0},  {-150, 0, 0}, {0, 150, 0},
                                    {0, -150, 0}, {0, 0, 150},  {0, 0, -150}};
    static const short ico[12][3] = {{0, -60, -97}, {0, -60, 97}, {0, 60, -97}, {0, 60, 97},
                                     {-60, -97, 0}, {-60, 97, 0}, {60, -97, 0}, {60, 97, 0},
                                     {-97, 0, -60}, {97, 0, -60}, {-97, 0, 60}, {97, 0, 60}};
    static const short pyr[5][3] = {
        {-110, 80, -110}, {110, 80, -110}, {-110, 80, 110}, {110, 80, 110}, {0, -140, 0}};
    nmod = 0;
    mk(&mods[nmod++], cube, 8, 40000);
    mk(&mods[nmod++], oct, 6, 45000);
    mk(&mods[nmod++], ico, 12, 14400);
    mk(&mods[nmod++], tet, 4, 105800);
    /* a square pyramid: base edges and the four edges to the tip */
    model *m = &mods[nmod++];
    mk(m, pyr, 5, 48400);
    for (int i = 0; i < 4; i++) {
        m->e[m->ne][0] = (u8)i;
        m->e[m->ne][1] = 4;
        m->ne++;
    }
    /* a cube inside a cube, joined at the corners */
    m = &mods[nmod++];
    m->nv = 16;
    m->ne = 0;
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 3; k++) {
            m->v[i][k] = (short)(cube[i][k] * 13 / 10);
            m->v[8 + i][k] = (short)(cube[i][k] * 6 / 10);
        }
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++) {
            int diff = 0;
            for (int k = 0; k < 3; k++)
                diff += cube[i][k] != cube[j][k];
            if (diff == 1) {
                m->e[m->ne][0] = (u8)i;
                m->e[m->ne++][1] = (u8)j;
                m->e[m->ne][0] = (u8)(8 + i);
                m->e[m->ne++][1] = (u8)(8 + j);
            }
        }
    for (int i = 0; i < 8 && m->ne < 30; i++) {
        m->e[m->ne][0] = (u8)i;
        m->e[m->ne++][1] = (u8)(8 + i);
    }
}
static void wire_draw(u32 ms) {
    memset(buf, C_VOID, (u64)pw * ph);
    int k = (int)(ms / 12000) % nmod; /* a new model every 12 seconds */
    const model *m = &mods[k];
    int ax = (int)(ms / 23) % 720, ay = (int)(ms / 15) % 720, az = (int)(ms / 41) % 720;
    int sz = (pw < ph ? pw : ph) * 9 / 10,
        cx = pw / 2 + (int)((long)isin(ms / 60) * pw / 10 / 16384),
        cy = ph / 2 + (int)((long)icos(ms / 85) * ph / 12 / 16384);
    /* in and out: the model grows in at the start and shrinks away before the next one */
    int t = (int)(ms % 12000), grow = t < 800     ? t * 100 / 800
                                      : t > 11200 ? (12000 - t) * 100 / 800
                                                  : 100;
    int px[20], py[20], pz[20];
    for (int i = 0; i < m->nv; i++) {
        long x = m->v[i][0], y = m->v[i][1], z = m->v[i][2], u;
        u = (y * icos(ax) - z * isin(ax)) / 16384;
        z = (y * isin(ax) + z * icos(ax)) / 16384;
        y = u;
        u = (x * icos(ay) + z * isin(ay)) / 16384;
        z = (-x * isin(ay) + z * icos(ay)) / 16384;
        x = u;
        u = (x * icos(az) - y * isin(az)) / 16384;
        y = (x * isin(az) + y * icos(az)) / 16384;
        x = u;
        long d = z + 520;
        px[i] = cx + (int)(x * sz * grow / 100 / d * 5 / 4);
        py[i] = cy + (int)(y * sz * grow / 100 / d * 5 / 4);
        pz[i] = (int)z;
    }
    static const u8 depth[] = {C_GLOW, C_LAV, C_NEON, C_VIOLET, C_SEL};
    for (int e = 0; e < m->ne; e++) {
        int a = m->e[e][0], b = m->e[e][1], z = (pz[a] + pz[b]) / 2; /* nearer edges are brighter */
        int lv = (z + 170) * 5 / 341;
        lv = lv < 0 ? 0 : lv > 4 ? 4 : lv;
        line(px[a], py[a], px[b], py[b], depth[lv]);
        if (lv < 2) /* thicker in front */
            line(px[a] + 1, py[a], px[b] + 1, py[b], depth[lv]);
    }
    for (int i = 0; i < m->nv; i++)
        rect(px[i] - 1, py[i] - 1, 3, 3, pz[i] < -40 ? C_GLOW : C_LAV);
}

/* ---------- 3: digital clock of seven-segment digits ---------- */
static const u8 segs[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
/* one segment: beveled ends like on a real display */
static void seg(int x, int y, int len, int th, int vert, u8 c) {
    for (int i = 0; i < len; i++) {
        int edge = i < th / 2 ? i : len - 1 - i < th / 2 ? len - 1 - i : th / 2;
        int w = edge * 2 + 1 > th ? th : edge * 2 + 1;
        int o = (th - w) / 2;
        if (vert)
            rect(x + o, y + i, w, 1, c);
        else
            rect(x + i, y + o, 1, w, c);
    }
}
static void digit(int x, int y, int w, int h, int d, u8 on, u8 off) {
    int t = w / 5 < 2 ? 2 : w / 5, g = t / 3 + 1, hh = (h - t) / 2;
    u8 m = d < 0 ? 0 : segs[d];
    seg(x + g, y, w - 2 * g, t, 0, m & 1 ? on : off);           /* a */
    seg(x + w - t, y + g, hh - g, t, 1, m & 2 ? on : off);      /* b */
    seg(x + w - t, y + hh + g, hh - g, t, 1, m & 4 ? on : off); /* c */
    seg(x + g, y + 2 * hh, w - 2 * g, t, 0, m & 8 ? on : off);  /* d */
    seg(x, y + hh + g, hh - g, t, 1, m & 16 ? on : off);        /* e */
    seg(x, y + g, hh - g, t, 1, m & 32 ? on : off);             /* f */
    seg(x + g, y + hh, w - 2 * g, t, 0, m & 64 ? on : off);     /* g */
}
static void seven_draw(u32 ms) {
    memset(buf, C_VOID, (u64)pw * ph);
    rtc_t t;
    rtc_read(&t);
    int dw = pw / 9, dh = dw * 2, sw = dw / 2, sh = dh / 2, gap = dw / 4;
    int tw = 4 * dw + 3 * gap + dw / 2 + 2 * gap + 2 * sw + gap / 2;
    /* the clock wanders a little every minute, so nothing burns in */
    u32 r = (u32)(t.h * 60 + t.m) * 2654435761u;
    int mx = pw - tw, my = ph - dh;
    int x = mx > 0 ? (int)(rnd(&r) % (u32)(mx / 2 + 1)) + mx / 4 : 0,
        y = my > 0 ? (int)(rnd(&r) % (u32)(my / 2 + 1)) + my / 4 : 0;
    u8 on = C_NEON, off = C_RAISE;
    digit(x, y, dw, dh, t.h / 10, on, off);
    digit(x + dw + gap, y, dw, dh, t.h % 10, on, off);
    int cx = x + 2 * dw + 2 * gap, th = dw / 5 < 2 ? 2 : dw / 5;
    u8 col = t.s % 2 ? off : on; /* the colon blinks */
    rect(cx, y + dh / 3 - th / 2, th, th, col);
    rect(cx, y + dh * 2 / 3 - th / 2, th, th, col);
    int mx2 = cx + th + gap;
    digit(mx2, y, dw, dh, t.m / 10, on, off);
    digit(mx2 + dw + gap, y, dw, dh, t.m % 10, on, off);
    int sx = mx2 + 2 * dw + 2 * gap;
    digit(sx, y + dh - sh, sw, sh, t.s / 10, C_LAV, off);
    digit(sx + sw + gap / 2, y + dh - sh, sw, sh, t.s % 10, C_LAV, off);
    /* the date in small digits below */
    int qw = dw / 3, qh = qw * 2, qg = qw / 3, qx = x, qy = y + dh + dh / 4;
    off = C_PANEL;
    int v[8] = {t.day / 10, t.day % 10,         -2,         t.mon / 10, t.mon % 10,
                -2,         (t.year / 10) % 10, t.year % 10};
    for (int i = 0; i < 8; i++) {
        if (v[i] == -2)
            rect(qx, qy + qh - 2, 2, 2, C_LAV);
        else
            digit(qx, qy, qw, qh, v[i], C_LAV, off);
        qx += v[i] == -2 ? qg + 3 : qw + qg;
    }
    (void)ms;
}

/* ---------- 5: synthwave sunset ---------- */
static const char *palm[] = {
    ".......##...##.......", "....######.######....", "..####...###...####..",
    ".##....#######....##.", "#....###..#..###....#", "....##....#....##....",
    "...#......#......#...", "..#.......#.......#..", "..........#..........",
    "..........#..........", "..........##.........", "...........#.........",
    "...........#.........", "...........##........", "............#........",
    "............#........", "............#........", "...........##........",
    "...........#.........", "...........#.........", "..........##.........",
    "..........#..........", ".........###........."};
#define PALMW 21
#define PALMH 23
static void synth_draw(u32 ms) {
    int hy = ph * 58 / 100, cx = pw / 2;
    /* sky: deep violet to pink at the horizon */
    for (int y = 0; y < hy; y++) {
        u32 c = y < hy / 2 ? mix(0x0B0326, 0x3A0C5C, y, hy / 2)
                           : mix(0x3A0C5C, 0xE0307E, y - hy / 2, hy - hy / 2);
        for (int x = 0; x < pw; x++)
            buf[y * pw + x] = dith(c, x, y);
    }
    u32 sr = 99;
    for (int i = 0; i < pw * hy / 900; i++) { /* stars that twinkle */
        int x = (int)(rnd(&sr) % pw), y = (int)(rnd(&sr) % (hy / 2)), p = (int)(rnd(&sr) % 3000);
        if ((ms + (u32)p) % 3000 < 2300)
            pset(x, y, near(0xC0B0FF));
    }
    /* the sun rises within 25 seconds and stays */
    int R = ph * 22 / 100, rise = ms > 25000 ? 1000 : (int)(ms / 25);
    int sy = hy + R - (R + R * 45 / 100) * rise / 1000;
    for (int y = sy - R; y <= sy + R && y < hy; y++) {
        if (y < 0)
            continue;
        int dy = y - sy, half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= R * R)
            half++;
        /* stripes cut into the lower half, wider towards the bottom */
        int rel = (y - (sy - R)) * 100 / (2 * R);
        if (rel > 50) {
            int band = (rel - 50) / 9, in = (rel - 50) % 9;
            if (in < 1 + band)
                continue;
        }
        u32 c = mix(0xFFE45C, 0xFF2F7F, y - (sy - R), 2 * R);
        for (int x = cx - half; x <= cx + half; x++)
            if (x >= 0 && x < pw)
                buf[y * pw + x] = dith(c, x, y);
    }
    /* mountains in front of the sun */
    for (int x = 0; x < pw; x++) {
        int d = x < cx ? cx - x : x - cx;
        int hgt = (int)((long)(isin(x * 3) + 16384) * (ph / 30) / 32768) +
                  (d > pw / 5 ? (d - pw / 5) / 5 : 0);
        hgt += (int)((long)(isin(x * 11 + 90) + 16384) * (ph / 60) / 32768);
        for (int y = hy - hgt; y < hy; y++)
            if (y >= 0)
                buf[y * pw + x] = dith(mix(0x2A0A40, 0x14062A, y - (hy - hgt), hgt + 1), x, y);
    }
    /* ground with the grid */
    u8 ground = near(0x12001E), gridc = near(0xFF2FD0), roadc = near(0x1C0C30),
       edge = near(0x2FF0FF);
    for (int y = hy; y < ph; y++)
        memset(buf + y * pw, ground, pw);
    int D = ph - hy, speed = (int)(ms % 1000); /* the floor runs towards the viewer */
    for (int i = 1; i < 40; i++) {
        /* lines at depth z = i - speed: y = horizon + D * k / z */
        long z = (long)i * 1000 - speed;
        if (z <= 0)
            continue;
        int y = hy + (int)((long)D * 1000 / z);
        if (y >= ph)
            continue;
        u8 c = i > 12 ? near(0x80208A) : gridc;
        memset(buf + y * pw, c, pw);
    }
    for (int k = -14; k <= 14; k++) { /* lines to the vanishing point */
        int bx = cx + k * pw / 8;
        line(cx + k * 3, hy, bx, ph - 1, gridc);
    }
    /* the road: dark, with bright edges and a dashed centre line */
    for (int y = hy + 1; y < ph; y++) {
        int half = (y - hy) * (pw * 22 / 100) / D + 1;
        for (int x = cx - half; x <= cx + half; x++)
            pset(x, y, roadc);
        pset(cx - half, y, edge);
        pset(cx + half, y, edge);
        long z = (long)D * 1000 / (y - hy);
        if (((z + speed * 4) / 1500) % 2 && half > 6)
            for (int w = -half / 30; w <= half / 30; w++)
                pset(cx + w, y, near(0xFFE45C));
    }
    /* palms on both sides, from far to near: black silhouettes with a pink rim on top */
    u8 pc = near(0x000000), rim = near(0xFF4FA0);
    for (int i = 9; i >= 1; i--) {
        long z = (long)i * 1400 - (ms % 2800) / 2;
        if (z <= 100)
            continue;
        int gy = hy + (int)((long)D * 1000 / z), s = (int)(ph * 4000L / z); /* size in % */
        if (gy > ph + ph / 8 || s < 8)
            continue;
        int half = (gy - hy) * (pw * 22 / 100) / D + 1;
        int w = PALMW * s / 100 + 1, h = PALMH * s / 100 + 1;
        for (int side = -1; side <= 1; side += 2) {
            int bx = cx + side * (half + w / 2 + 2);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int u = x * PALMW / w, v = y * PALMH / h;
                    if (side > 0)
                        u = PALMW - 1 - u;
                    if (palm[v][u] != '#')
                        continue;
                    int top = !v || palm[(y - 1 < 0 ? 0 : (y - 1) * PALMH / h)][u] != '#' || !y;
                    pset(bx - w / 2 + x, gy - h + y, top && v < 8 ? rim : pc);
                }
        }
    }
}

/* ---------- running ---------- */
void saver_run(int kind) {
    if (kind < 1 || kind >= SAVERS)
        return;
    pw = W * 4;
    ph = H * 8;
    buf = kmalloc((u64)pw * ph);
    u8 *old = kmalloc((u64)pw * ph);
    int have = 0;
    if (!ncache)
        ncache = kmalloc(32768);
    if (!buf) {
        kfree(old);
        return;
    }
    if (ncache)
        memset(ncache, 0, 32768);
    if (kind == 1)
        logo_init();
    if (kind == 2)
        wire_init();
    ui_blank();
    u64 t0 = ticks;
    key_t k;
    while (!key_poll(&k)) {
        u32 ms = (u32)((ticks - t0) * 10);
        int fixed = 1;
        if (kind == 1)
            logo_draw(ms);
        else if (kind == 2) {
            wire_draw(ms);
            fixed = 0;
        } else if (kind == 3) {
            seven_draw(ms);
            fixed = 0;
        } else if (kind == 4) {
            const u8 *c = clock_picture(pw, ph);
            if (c)
                memcpy(buf, c, (u64)pw * ph);
            fixed = 0;
        } else
            synth_draw(ms);
        ui_blit(buf, have ? old : 0, pw, ph, fixed);
        if (old) { /* remember the picture: unchanged rows are not drawn again */
            memcpy(old, buf, (u64)pw * ph);
            have = 1;
        }
        u64 next = ticks + 4; /* 25 pictures a second */
        while (ticks < next)
            idle();
    }
    if (kind == 4)
        clock_picture(0, 0);
    kfree(buf);
    kfree(old);
    buf = 0;
    ui_render();
    ui_show();
}
