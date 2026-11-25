/* Screen savers: the Violyx logo with twinkling stars, wire frame models, a seven-segment clock,
 * the analog clock and a synthwave sunset. They draw a picture of (W*4) x (H*8) pixels in the 256
 * colours, which ui_blit puts on the screen. Any key ends them (the key is not passed on). */
#include <stdint.h>
#include <string.h>
#include "os.h"
#include "sintab.h"
#include "sevenseg.h"

int cfg_saver, cfg_swait = 2; /* screen saver (0 off) and waiting time (index into saver_min) */
const int saver_min[SWAITS] = {1, 2, 3, 5, 10, 15, 30};
static u8 *buf;
static int pw, ph;

/* Installed screen savers (packages of kind 1): cfg_saver == SAVERS means the one named in
 * cfg_saver_pkg. */
char cfg_saver_pkg[24];
static char stitle[24];
static prog_info *spk;
static int nspk;
static void spk_scan(void) {
    if (!spk)
        spk = kmalloc(sizeof(prog_info) * 16);
    nspk = spk ? prog_scan(spk, 16, 1) : 0;
}
static int spk_find(const char *n) {
    for (int i = 0; i < nspk; i++)
        if (!strcmp(spk[i].name, n))
            return i;
    return -1;
}
const char *saver_name(int i) {
    static const char *de[SAVERS] = {"Aus",        "Violyx-Logo", "Drahtmodelle",
                                     "Digitaluhr", "Analoguhr",   "Synthwave"},
                      *en[SAVERS] = {"Off",           "Violyx logo",  "Wire frames",
                                     "Digital clock", "Analog clock", "Synthwave"};
    if (i >= SAVERS) {
        if (!stitle[0]) { /* the title of the package, looked up once */
            spk_scan();
            int k = spk_find(cfg_saver_pkg);
            strlcpy_(stitle, k >= 0 ? spk[k].title : cfg_saver_pkg, 24);
        }
        return stitle;
    }
    return TR(de[i], en[i]);
}
/* Next or previous screen saver in the settings: the built-in ones, then the installed ones */
void saver_cycle(int d) {
    spk_scan();
    int all = SAVERS + nspk, k = spk_find(cfg_saver_pkg);
    int pos = cfg_saver < SAVERS ? cfg_saver : k >= 0 ? SAVERS + k : 0;
    pos = (pos + all + d) % all;
    if (pos < SAVERS) {
        cfg_saver = pos;
        return;
    }
    cfg_saver = SAVERS;
    strlcpy_(cfg_saver_pkg, spk[pos - SAVERS].name, 24);
    strlcpy_(stitle, spk[pos - SAVERS].title, 24);
}
void saver_choose(const char *pkg) {
    strlcpy_(cfg_saver_pkg, pkg, 24);
    stitle[0] = 0;
    cfg_saver = SAVERS;
    cfg_save();
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
    static const u8 cl[] = {L_ROW1, L_ROW2, L_ROW3, L_ROW4, L_ROW5};
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
                rect(u + sh, v + sh, P, P, L_SHADE); /* shadow */
                rect(u, v, P, P, cl[y]);
            }
}

/* ---------- 2: wire frame models ---------- */
typedef struct {
    int nv, ne;
    short v[20][3];
    u8 e[36][2];
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
            if (d > len2 * 98L / 100 && d < len2 * 102L / 100 && m->ne < 36) {
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
            m->v[i][k] = (short)(cube[i][k] * 11 / 10);
            m->v[8 + i][k] = (short)(cube[i][k] * 5 / 10);
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
    for (int i = 0; i < 8; i++) { /* 12 + 12 + 8 = 32 edges */
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

/* ---------- 3: digital clock of seven-segment digits (sevenseg.h) ---------- */
static void seven_draw(u32 ms) {
    memset(buf, C_VOID, (u64)pw * ph);
    rtc_t t;
    rtc_read(&t);
    int dw = pw / 9;
    if (dw > ph / 4) /* narrow screens: the date must fit below as well */
        dw = ph / 4;
    /* height with the date below: the digits, a gap of a quarter, the date digits */
    int dh = dw * 2 + dw / 2 + (dw / 3) * 2, tw = ss_width(dw);
    /* the clock wanders a little every minute, so nothing burns in */
    u32 r = (u32)(t.h * 60 + t.m) * 2654435761u;
    int mx = pw - tw, my = ph - dh;
    int x = mx > 0 ? (int)(rnd(&r) % (u32)(mx / 2 + 1)) + mx / 4 : 0,
        y = my > 0 ? (int)(rnd(&r) % (u32)(my / 2 + 1)) + my / 4 : 0;
    ssbuf s = {buf, pw, ph};
    ss_clock(&s, &t, x, y, dw, C_PANEL, C_NEON, C_RAISE, C_LAV);
    (void)ms;
}

/* ---------- 5: synthwave sunset ---------- */
/* A small 3D scene: the eye drives along a road over a wire frame floor, with hills at the sides
 * and palms. Everything moves at the same speed, so it fits together. World
 * units are 1/256: the eye is SU above the floor, and a point x, h (height), z (distance) is seen
 * at cx + sD * x / z, hy + sD * (SU - h) / z. */
#define SU    256
#define CELL  128      /* the floor grid: half a unit */
#define ROADW 240      /* half the width of the road */
#define XN    30       /* grid columns on each side */
#define ZFAR  (40 * SU)
static int s_hy, s_cx, s_D, *s_lim;
static int isqrt(long v) {
    long r = 0, b = 1L << 40;
    if (v <= 0)
        return 0;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return (int)r;
}
/* the height of the floor: flat around the road, hills further out, different on each side */
static int hill(int x, int zw) {
    int ax = x < 0 ? -x : x, x0 = 3 * SU;
    if (ax <= x0)
        return 0;
    int ramp = ax - x0 > 3 * SU ? 3 * SU : ax - x0;
    int ph1 = x < 0 ? 0 : 250;
    long v = isin((int)((long)zw * 720 / (9 * SU)) + x * 720 / (7 * SU) + ph1) +
             isin((int)((long)zw * 720 / (5 * SU)) - x * 720 / (4 * SU) + 100 + ph1) / 2 + 4000;
    if (v < 0)
        return 0;
    return (int)(v * ramp / 16384 * 2 / 5);
}
/* a line cut to the picture; with s_lim only where nothing nearer hides it (y above s_lim[x]) */
static void cline(long x0, long y0, long x1, long y1, u8 c) {
    long dx = x1 - x0, dy = y1 - y0;
    long t0 = 0, t1 = 1 << 16; /* Liang-Barsky in 1/65536 */
    long p[4] = {-dx, dx, -dy, dy}, q[4] = {x0, pw - 1 - x0, y0, ph - 1 - y0};
    for (int i = 0; i < 4; i++) {
        if (!p[i]) {
            if (q[i] < 0)
                return;
            continue;
        }
        long t = q[i] * 65536 / p[i];
        if (p[i] < 0) {
            if (t > t1)
                return;
            if (t > t0)
                t0 = t;
        } else {
            if (t < t0)
                return;
            if (t < t1)
                t1 = t;
        }
    }
    int ax = (int)(x0 + dx * t0 / 65536), ay = (int)(y0 + dy * t0 / 65536);
    int bx = (int)(x0 + dx * t1 / 65536), by = (int)(y0 + dy * t1 / 65536);
    int ddx = bx > ax ? bx - ax : ax - bx, ddy = by > ay ? ay - by : by - ay;
    int sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1, e = ddx + ddy;
    for (;;) {
        if (ax >= 0 && ay >= 0 && ax < pw && ay < ph && (!s_lim || ay < s_lim[ax]))
            buf[ay * pw + ax] = c;
        if (ax == bx && ay == by)
            break;
        int e2 = 2 * e;
        if (e2 >= ddy) {
            e += ddy;
            ax += sx;
        }
        if (e2 <= ddx) {
            e += ddx;
            ay += sy;
        }
    }
}
/* a thick line: squares of w pixels along it */
static void tline(int x0, int y0, int x1, int y1, int w, u8 c) {
    if (w <= 1) {
        cline(x0, y0, x1, y1, c);
        return;
    }
    int n = (x1 > x0 ? x1 - x0 : x0 - x1) + (y1 > y0 ? y1 - y0 : y0 - y1);
    if (n > 4000)
        return;
    for (int i = 0; i <= n; i++) {
        int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
        rect(x - w / 2, y - w / 2, w, w, c);
    }
}
/* palms: each one different, from a number. Drawn twice: first trunk and ribs moved a pixel
 * towards the sun in the pink of the sky (the rim light), then all of it black on top. */
static void palm_shape(int id, int bx, int by, int k, int side, int ox, int oy, u8 c) {
    /* k: pixels per 256 world units */
    u32 r = (u32)id * 2654435761u + 77;
    rnd(&r);
    int hp = 520 + (int)(rnd(&r) % 300);                  /* height */
    int lean = side * 60 + (int)(rnd(&r) % 260) - 130;   /* the top leans, mostly outwards */
    int bend = (int)(rnd(&r) % 3);                      /* how the trunk curves */
    int nf = 6 + (int)(rnd(&r) % 3), fl = 230 + (int)(rnd(&r) % 120);
    bx += ox;
    by += oy;
    /* the trunk, from the foot to the crown, thinner upwards */
    int steps = hp * k / SU / 2 + 6, tx = bx, ty = by;
    for (int i = 1; i <= steps; i++) {
        long t = (long)i * 1024 / steps;                  /* 0..1024 */
        long cv = bend == 0 ? t * t / 1024 : bend == 1 ? t : (t * t / 1024 * t / 1024);
        int x = bx + (int)(lean * cv / 1024) * k / SU, y = by - (int)(hp * t / 1024) * k / SU;
        int w = (int)((34 - 16 * t / 1024) * k / SU);
        tline(tx, ty, x, y, w < 1 ? 1 : w, c);
        tx = x;
        ty = y;
    }
    /* coconuts under the crown */
    int cn = 7 * k / SU;
    if (cn >= 2 && !ox)
        for (int i = -1; i <= 1; i++)
            rect(tx + i * cn - cn / 2, ty + cn / 3, cn, cn, c);
    /* the fronds: a curved rib with leaflets that hang down */
    for (int j = 0; j < nf; j++) {
        int th = -25 + j * 230 / (nf - 1) + (int)(rnd(&r) % 24) - 12; /* degrees, 90 is up */
        int dr = 300 + (int)(rnd(&r) % 450);                          /* droop, of 1024 */
        int L = (fl + (int)(rnd(&r) % 80)) * k / SU;                   /* length in pixels */
        if (th > 70 && th < 110)
            L = L * 3 / 4; /* the ones on top are shorter */
        int cx_ = icos(th * 2), sy_ = isin(th * 2), n = L / 3 > 12 ? L / 3 : 12;
        if (n > 60)
            n = 60;
        int px = tx, py = ty, rw = L / 45 + 1;
        for (int s = 1; s <= n; s++) {
            int x = tx + (int)((long)L * s * cx_ / n / 16384);
            int y = ty - (int)((long)L * s * sy_ / n / 16384) + (int)((long)L * dr / 1024 * s * s / n / n);
            tline(px, py, x, y, rw > 3 ? 3 : rw, c);
            if (s < n && !ox) { /* the rim light only on the trunk and the ribs */
                /* the direction of the rib, its normal pointing down, and gravity */
                long ux = cx_, uy = -sy_ + (long)2 * dr * s * 16 / n * L / (L + 1);
                int ul = isqrt(ux * ux + uy * uy);
                if (!ul)
                    ul = 1;
                ux = ux * 1024 / ul;
                uy = uy * 1024 / ul;
                long nx = -uy, ny = ux;
                if (ny < 0) {
                    nx = -nx;
                    ny = -ny;
                }
                /* longest in the middle; like a feather, both sides slanted towards the tip
                 * and hanging a little */
                int ll = (int)((long)L * 26 / 100 * isin(s * 300 / n + 60) / 16384);
                for (int q = 0; q < 2; q++) {
                    long dx = (q ? -nx * 6 / 10 : nx) + ux * 7 / 10,
                         dy = (q ? -ny * 6 / 10 : ny) + uy * 7 / 10 + 500;
                    int dl = isqrt(dx * dx + dy * dy);
                    if (!dl)
                        continue;
                    cline(x, y, x + dx * ll / dl, y + dy * ll / dl, c);
                }
            }
            px = x;
            py = y;
        }
    }
}
typedef struct {
    int z, kind, x, id; /* kind 1: a palm */
} sobj;
static void synth_draw(u32 ms) {
    int hy = ph * 58 / 100, cx = pw / 2, D = ph - hy;
    s_hy = hy;
    s_cx = cx;
    s_D = D;
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
    /* far mountains in front of the sun */
    for (int x = 0; x < pw; x++) {
        int d = x < cx ? cx - x : x - cx;
        int hgt = (int)((long)(isin(x * 3) + 16384) * (ph / 30) / 32768) +
                  (d > pw / 5 ? (d - pw / 5) / 5 : 0);
        hgt += (int)((long)(isin(x * 11 + 90) + 16384) * (ph / 60) / 32768);
        for (int y = hy - hgt; y < hy; y++)
            if (y >= 0)
                buf[y * pw + x] = dith(mix(0x2A0A40, 0x14062A, y - (hy - hgt), hgt + 1), x, y);
    }
    u8 ground = near(0x12001E);
    for (int y = hy; y < ph; y++)
        memset(buf + y * pw, ground, pw);
    /* the floor as a wire frame, from near to far: what is nearer hides what is behind it */
    long trav = (long)ms * SU / 1000; /* one unit a second */
    int *lim = kmalloc((u64)pw * 2 * sizeof(int));
    if (!lim)
        return;
    int *nl = lim + pw;
    for (int x = 0; x < pw; x++)
        lim[x] = ph;
    int pxs[2 * XN + 1], pys[2 * XN + 1], qxs[2 * XN + 1], qys[2 * XN + 1], have = 0;
    long k0 = trav / CELL + 1;
    for (long k = k0;; k++) {
        long zw = k * CELL, z = zw - trav;
        if (z > ZFAR)
            break;
        if (z < SU / 4)
            continue;
        u8 c = near(mix(0xFF2FD0, 0x4A1060, (int)(z > 24 * SU ? 24 * SU : z), 24 * SU));
        for (int i = 0; i <= 2 * XN; i++) {
            int x = (i - XN) * CELL, h = hill(x, (int)zw);
            qxs[i] = cx + (int)((long)D * x / z);
            qys[i] = hy + (int)((long)D * (SU - h) / z);
        }
        for (int x = 0; x < pw; x++)
            nl[x] = lim[x];
        /* the strip between this row and the one before: floor colour over what is behind */
        for (int i = 0; i < 2 * XN; i++) {
            int x0 = qxs[i], x1 = qxs[i + 1];
            if (x1 <= x0 || x1 < 0 || x0 >= pw)
                continue;
            for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < pw; x++) {
                int y = qys[i] + (int)((long)(qys[i + 1] - qys[i]) * (x - x0) / (x1 - x0));
                if (y < 0)
                    y = 0;
                if (y < nl[x]) {
                    for (int yy = y; yy < lim[x] && yy < ph; yy++)
                        buf[yy * pw + x] = ground;
                    nl[x] = y;
                }
            }
        }
        s_lim = lim;
        for (int i = 0; i <= 2 * XN; i++) {
            if (have)
                cline(pxs[i], pys[i], qxs[i], qys[i], c);
            if (i < 2 * XN)
                cline(qxs[i], qys[i], qxs[i + 1], qys[i + 1], c);
        }
        s_lim = 0;
        for (int x = 0; x < pw; x++)
            lim[x] = nl[x];
        for (int i = 0; i <= 2 * XN; i++) {
            pxs[i] = qxs[i];
            pys[i] = qys[i];
        }
        have = 1;
    }
    kfree(lim);
    /* the road: dark, with bright edges and a dashed centre line */
    u8 roadc = near(0x1C0C30), edge = near(0x2FF0FF), mid = near(0xFFE45C);
    /* far away the bright edges fade out, so the road vanishes at the horizon instead of
     * ending in a point */
    int fade = D / 4 > 1 ? D / 4 : 1;
    for (int y = hy + 1; y < ph; y++) {
        int half = (y - hy) * ROADW / SU, t = y - hy;
        u8 ec = edge, rc = roadc;
        if (t < fade) {
            ec = near(mix(0x1C0C30, 0x2FF0FF, t * t, fade * fade));
        }
        for (int x = cx - half; x <= cx + half; x++)
            pset(x, y, rc);
        pset(cx - half, y, ec);
        pset(cx + half, y, ec);
        if (half > 2)
            pset(cx - half + 1, y, ec);
        if (half > 2)
            pset(cx + half - 1, y, ec);
        long zw = (long)D * SU / (y - hy) + trav;
        if ((zw / (SU * 3 / 2)) % 2 && half > 6 && t >= fade / 3)
            for (int w = -half / 40; w <= half / 40; w++)
                pset(cx + w, y, mid);
    }
    /* palms on both sides of the road every three units, far ones first */
    sobj ob[160];
    int no = 0;
    for (long k = trav / SU + 1; no < 150; k++) {
        long z = k * SU - trav;
        if (z > 32 * SU)
            break;
        if (z < SU / 3)
            continue;
        if (k % 3 == 0)
            for (int side = -1; side <= 1; side += 2)
                ob[no++] = (sobj){(int)z, 1, side * (ROADW + 300), (int)k * 2 + (side > 0)};
    }
    for (int i = 1; i < no; i++) /* far to near */
        for (int j = i; j > 0 && ob[j].z > ob[j - 1].z; j--) {
            sobj t = ob[j];
            ob[j] = ob[j - 1];
            ob[j - 1] = t;
        }
    u8 black = near(0x000000), rimc = near(0xFF4FA0);
    for (int i = 0; i < no; i++) {
        sobj *o = &ob[i];
        int k = (int)((long)D * SU / o->z); /* pixels per unit */
        int bx = cx + (int)((long)D * o->x / o->z), by = hy + (int)((long)D * SU / o->z);
        int side = o->x < 0 ? -1 : 1;
        if (k >= 24)
            palm_shape(o->id, bx, by, k, side, -side, -1, rimc);
        palm_shape(o->id, bx, by, k, side, 0, 0, black);
    }
}

/* ---------- running ---------- */
static int sfresh;
long saver_size(void) {
    return buf ? (long)pw << 16 | ph : 0;
}
/* A picture of an installed screen saver, stretched to the screen */
void saver_frame(const u8 *px, int w, int h) {
    if (!buf)
        return;
    for (int y = 0; y < ph; y++) {
        const u8 *s = px + (u64)(y * h / ph) * w;
        u8 *o = buf + (u64)y * pw;
        for (int x = 0; x < pw; x++)
            o[x] = s[x * w / pw];
    }
    sfresh = 1;
}
void saver_run(int kind) {
    if (kind < 1 || kind > SAVERS)
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
    void *sp = 0;
    if (kind == SAVERS) { /* an installed one; without it, the logo */
        memset(buf, 0, (u64)pw * ph);
        sfresh = 1;
        sp = cfg_saver_pkg[0] ? prog_saver_start(cfg_saver_pkg) : 0;
        if (!sp)
            kind = 1;
    }
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
        if (sp) {
            u64 next = ticks + 4; /* the program runs until it waits, at most to the next picture */
            while (ticks < next) {
                int r = prog_saver_step(sp);
                if (r < 0) { /* it has ended or failed: the logo takes over */
                    prog_saver_stop(sp);
                    sp = 0;
                    kind = 1;
                    logo_init();
                    break;
                }
                if (!r)
                    idle();
            }
            if (sp) {
                if (sfresh)
                    ui_blit(buf, have ? old : 0, pw, ph, 1);
                if (sfresh && old) {
                    memcpy(old, buf, (u64)pw * ph);
                    have = 1;
                }
                sfresh = 0;
                continue;
            }
        }
        if (kind == 1) {
            logo_draw(ms);
            fixed = 2; /* the colours of the logo */
        }
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
    if (sp)
        prog_saver_stop(sp);
    if (kind == 4)
        clock_picture(0, 0);
    kfree(buf);
    kfree(old);
    buf = 0;
    ui_render();
    ui_show();
}
