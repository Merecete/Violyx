/* Vio-Island - a small island in the synthwave look of Violyx, to walk around in.
   Copyright (c) 2026, Merecete
   SPDX-License-Identifier: BSD-3-Clause
   Arrows walk and turn, Shift runs, M switches the music on and off, F the full screen. The Arcade Bar in the
   south west can be entered through its door. Everything is drawn by the program itself:
   the floor is cast row by row, buildings and rocks are polygons with a depth buffer, and the
   palms are silhouettes like on the screen saver. The music is made while it plays. */
#include "violyx.h"
#include <math.h>

typedef unsigned int u32;
typedef unsigned char u8;

/* ---------- colours: the 256 of Violyx, with ordered dithering for soft gradients ---------- */
static u8 ncache[32768];
static u8 near(u32 rgb) {
    u32 r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    u32 k = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
    if (ncache[k])
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
        long d = 2L * ((int)r - qr) * ((int)r - qr) + 4L * ((int)g - qg) * ((int)g - qg) +
                 3L * ((int)b - qb) * ((int)b - qb);
        if (bd < 0 || d < bd) {
            bd = d;
            best = i;
        }
    }
    ncache[k] = (u8)best;
    return (u8)best;
}
static u32 mix(u32 a, u32 b, float t) { /* a..b, t from 0 to 1 */
    if (t <= 0)
        return a;
    if (t >= 1)
        return b;
    u32 o = 0;
    for (int s = 0; s <= 16; s += 8) {
        int x = (a >> s) & 255, y = (b >> s) & 255;
        o |= (u32)(x + (int)((y - x) * t)) << s;
    }
    return o;
}
static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static u8 dith(u32 rgb, int x, int y) {
    int o = (bayer[y & 3][x & 3] - 8) * 3;
    u32 v = 0;
    for (int s = 0; s <= 16; s += 8) {
        int c = (int)((rgb >> s) & 255) + o;
        v |= (u32)(c < 0 ? 0 : c > 255 ? 255 : c) << s;
    }
    return near(v);
}
static u32 hash(u32 a) {
    a ^= a >> 16;
    a *= 0x7feb352d;
    a ^= a >> 15;
    a *= 0x846ca68b;
    a ^= a >> 16;
    return a;
}
static float frac(float v) {
    return v - (float)floor(v);
}

/* ---------- the picture ---------- */
static int PW, PH, HY, CX; /* size, horizon, centre */
static float FOC;          /* pixels for one unit at the distance of one unit */
static u8 *img;
static float *zb; /* depth: the distance straight ahead */
static float camx, camz, yaw, eye = 1.65f, cs, sn;
static float now; /* seconds since the start */
#define EYE 1.65f

/* world to camera: X right, Y up (from the eye), Z ahead */
static void cam(float x, float y, float z, float *X, float *Y, float *Z) {
    float dx = x - camx, dz = z - camz;
    *X = dx * cs - dz * sn;
    *Z = dx * sn + dz * cs;
    *Y = y - eye;
}

/* ---------- the island ---------- */
/* 0.25 m per cell, x -140..140 (east), z -80..80 (north) */
#define RES 0.25f
#define GW  1120
#define GH  640
#define GX0 (-140.0f)
#define GZ0 (-80.0f)
enum { T_WATER, T_LAND, T_BEACH, T_ROAD, T_EDGE, T_DASH, T_DIRT, T_DIRTE, T_COURT, T_COURTL, T_FLOOR, T_PARK,
       T_PARKL, T_SHOP, T_DINER };
static u8 *ter;   /* the kind of ground */
static u8 *solid; /* where one cannot walk: 1 */
static u8 *glow;  /* the light of the street lamps on the ground */
/* for sharp edges up close: distances, read between the cells */
/* to the middle of the main street (cm) and the way along it (dm); the same for the nearest other
 * road, which lies under the main street where they meet */
static unsigned short *rdist, *ralong, *rdist1, *ralong1;
static u8 *rway;
static short *cdist;                   /* to the coast (dm), positive on land */

static const float coast[][2] = {
    {-112, -5}, {-92, 7},   {-67, 17},  {-52, 30},  {-47, 48},  {-30, 47}, {-17, 40},
    {22, 44},   {57, 58},   {72, 68},   {92, 61},   {114, 48},  {92, 40},  {67, 26},
    {57, 7},    {42, -10},  {32, -10},  {27, -25},  {26, -50},  {7, -53},  {0, -35},
    {-10, -30}, {-47, -32}, {-58, -36}, {-58, -52}, {-87, -58}, {-101, -50}, {-112, -25}};
#define NCOAST (int)(sizeof coast / sizeof coast[0])

typedef struct {
    const float (*p)[2];
    int n;
    float half; /* half the width */
    int dirt;
} road_t;
static const float main_rd[][2] = {{-72, -27}, {-79, -21}, {-86, -10}, {-84, 2},  {-70, 11},
                                   {-40, 22},  {-17, 27},  {22, 32},   {57, 41},  {82, 46}};
static const float east_rd[][2] = {{82, 46}, {92, 48}, {99, 51}};
static const float south_rd[][2] = {{-29, 21}, {-26, 6}, {-10, -6}, {6, -18}, {13, -34}, {15, -41}};
static const road_t roads[] = {{main_rd, 10, 3.6f, 0}, {east_rd, 3, 2.0f, 1}, {south_rd, 6, 2.0f, 1}};
#define NROADS 3

static int inside(float x, float z, const float (*p)[2], int n) {
    int c = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i][1] > z) != (p[j][1] > z) &&
            x < (p[j][0] - p[i][0]) * (z - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0])
            c = !c;
    return c;
}
/* distance to a polyline and the way along it */
static float seg_dist(float x, float z, const float (*p)[2], int n, int closed, float *along) {
    float best = 1e9f, s = 0, bs = 0;
    for (int i = 0; i < n - 1 + closed; i++) {
        float ax = p[i][0], az = p[i][1], bx = p[(i + 1) % n][0], bz = p[(i + 1) % n][1];
        float vx = bx - ax, vz = bz - az, l2 = vx * vx + vz * vz;
        float t = l2 > 0 ? ((x - ax) * vx + (z - az) * vz) / l2 : 0;
        if (t < 0)
            t = 0;
        if (t > 1)
            t = 1;
        float dx = x - ax - t * vx, dz = z - az - t * vz, d = dx * dx + dz * dz;
        float len = (float)sqrt(l2);
        if (d < best) {
            best = d;
            bs = s + t * len;
        }
        s += len;
    }
    if (along)
        *along = bs;
    return (float)sqrt(best);
}
static int cell(float x, float z) { /* index of the cell, -1 outside */
    int i = (int)((x - GX0) / RES), j = (int)((z - GZ0) / RES);
    if (i < 0 || j < 0 || i >= GW || j >= GH)
        return -1;
    return j * GW + i;
}
#define PK_X0 (-83.0f)
#define PK_X1 (-61.0f)
#define PK_Z0 (-34.3f)
#define PK_Z1 (-26.0f)
#define TW_X  (-36.0f)
#define TW_Z  37.0f
#define TW_S  14.0f
#define TW_R  0.33f
#define TW_G  4.5f
static void tower_local(float x, float z, float *lx, float *lz) {
    float c = (float)cos(TW_R), s = (float)sin(TW_R), dx = x - TW_X, dz = z - TW_Z;
    *lx = dx * c + dz * s;
    *lz = -dx * s + dz * c;
}
static void tower_world(float lx, float lz, float *x, float *z) {
    float c = (float)cos(TW_R), s = (float)sin(TW_R);
    *x = TW_X + lx * c - lz * s;
    *z = TW_Z + lx * s + lz * c;
}
static int kind_at(float x, float z) {
    int c = cell(x, z);
    return c < 0 ? T_WATER : ter[c];
}

/* ---------- buildings and other things, as polygons ---------- */
enum { S_FLAT, S_WIN, S_TOWER, S_SCREEN, S_SIGN, S_STRIPE, S_BOARD, S_ROOF, S_CEIL, S_BOTTLES,
       S_LAMP, S_SHOPW, S_CEIL2, S_PC, S_SOFT, S_LOGO, S_FLATB, S_DINER, S_GLASS, S_MENU };
typedef struct {
    float p[4][3], uv[4][2];
    int n;
    u8 sh, edges; /* shader, a bit for each edge that gets a bright line */
    u32 fill, line;
    int id;  /* for the windows that are lit and for signs */
    float cx, cz, r; /* centre and size, to leave out what is far away */
} face_t;
#define MAXF 6000
static face_t *faces;
static int nfaces;
static void quad(const float a[3], const float b[3], const float c[3], const float d[3], float w,
                 float h, int sh, u32 fill, u32 line, int edges, int id) {
    if (nfaces >= MAXF)
        return;
    face_t *f = &faces[nfaces++];
    const float *q[4] = {a, b, c, d};
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++)
            f->p[i][k] = q[i][k];
    /* u along a-b, v up along a-d: a (0,0) b (w,0) c (w,h) d (0,h) */
    f->uv[0][0] = 0, f->uv[0][1] = 0;
    f->uv[1][0] = w, f->uv[1][1] = 0;
    f->uv[2][0] = w, f->uv[2][1] = h;
    f->uv[3][0] = 0, f->uv[3][1] = h;
    f->n = 4;
    f->sh = (u8)sh;
    f->edges = (u8)edges;
    f->fill = fill;
    f->line = line;
    f->id = id;
}
static void tri(const float a[3], const float b[3], const float c[3], u32 fill, u32 line, int edges) {
    if (nfaces >= MAXF)
        return;
    face_t *f = &faces[nfaces++];
    const float *q[3] = {a, b, c};
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++)
            f->p[i][k] = q[i][k];
    for (int i = 0; i < 3; i++)
        f->uv[i][0] = f->uv[i][1] = 0;
    f->n = 3;
    f->sh = S_FLAT;
    f->edges = (u8)edges;
    f->fill = fill;
    f->line = line;
    f->id = 0;
}
static void bound_faces(int from) { /* centre and radius of the new faces */
    for (int i = from; i < nfaces; i++) {
        face_t *f = &faces[i];
        float x = 0, z = 0, r = 0;
        for (int k = 0; k < f->n; k++)
            x += f->p[k][0], z += f->p[k][2];
        x /= (float)f->n;
        z /= (float)f->n;
        for (int k = 0; k < f->n; k++) {
            float dx = f->p[k][0] - x, dz = f->p[k][2] - z, dy = f->p[k][1];
            float d = (float)sqrt(dx * dx + dz * dz + dy * dy * 0.1f);
            if (d > r)
                r = d;
        }
        f->cx = x;
        f->cz = z;
        f->r = r;
    }
}
/* marks a turned rectangle as solid (or free with v 0) */
static void mark_rect(float cx, float cz, float w, float d, float rot, int v) {
    float c = (float)cos(rot), s = (float)sin(rot), R = (float)sqrt(w * w + d * d) / 2 + 0.5f;
    for (float z = cz - R; z <= cz + R; z += RES)
        for (float x = cx - R; x <= cx + R; x += RES) {
            float lx = (x - cx) * c + (z - cz) * s, lz = -(x - cx) * s + (z - cz) * c;
            if (lx > -w / 2 && lx < w / 2 && lz > -d / 2 && lz < d / 2) {
                int k = cell(x, z);
                if (k >= 0)
                    solid[k] = (u8)v;
            }
        }
}
static void mark_disc(float cx, float cz, float r) {
    for (float z = cz - r; z <= cz + r; z += RES)
        for (float x = cx - r; x <= cx + r; x += RES)
            if ((x - cx) * (x - cx) + (z - cz) * (z - cz) <= r * r) {
                int k = cell(x, z);
                if (k >= 0)
                    solid[k] = 1;
            }
}
/* a box: centre, width (along x before turning), depth, height, from height y0, turned by rot.
 * sides: shader of the four walls; top: 1 with a roof */
static int box_id;
static void box(float cx, float cz, float w, float d, float h, float y0, float rot, int sh,
                u32 fill, u32 line, int top, int mark) {
    float c = (float)cos(rot), s = (float)sin(rot), P[8][3];
    float lx[4] = {-w / 2, w / 2, w / 2, -w / 2}, lz[4] = {-d / 2, -d / 2, d / 2, d / 2};
    for (int i = 0; i < 4; i++) {
        float x = cx + lx[i] * c - lz[i] * s, z = cz + lx[i] * s + lz[i] * c;
        P[i][0] = P[i + 4][0] = x;
        P[i][2] = P[i + 4][2] = z;
        P[i][1] = y0;
        P[i + 4][1] = y0 + h;
    }
    int from = nfaces;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        float len = i % 2 ? d : w;
        quad(P[i], P[j], P[j + 4], P[i + 4], len, h, sh, fill, line, 15, box_id * 8 + i);
    }
    if (top)
        quad(P[4], P[5], P[6], P[7], w, d, top == 2 ? S_FLAT : S_ROOF, top == 2 ? fill : 0x120820,
             line, 15, box_id * 8 + 4);
    box_id++;
    bound_faces(from);
    if (mark)
        mark_rect(cx, cz, w, d, rot, 1);
}

/* hills: soft bumps of the ground, drawn as the same wire grid as the floor; one walks over them */
static const struct {
    float x, z, r, h;
} hills[] = {{-28, -20, 16, 9}, {36, 8, 20, 12}, {-5, -22, 11, 6}, {16, -5, 12, 7},
             {-50, -24, 12, 7}, {-102, -30, 14, 8}};
#define NHILLS 6
/* how much a hill may rise at a place, 0..1: it flattens towards roads, buildings and the water,
 * so they stay level. One value per metre, read between them. */
#define HFW 280
#define HFH 160
static float hfac[HFW * HFH];
static int natural(float x, float z) {
    int c = cell(x, z);
    if (c < 0 || solid[c])
        return 0;
    return ter[c] == T_LAND || ter[c] == T_BEACH;
}
static void build_hfac(void) {
    for (int j = 0; j < HFH; j++)
        for (int i = 0; i < HFW; i++) {
            float x = GX0 + (float)i, z = GZ0 + (float)j, f = 1;
            for (int r = 0; r <= 6 && f == 1; r++)
                for (int a = 0; a < (r ? 16 : 1); a++) {
                    float an = (float)a * 0.3927f;
                    if (!natural(x + (float)r * (float)cos(an), z + (float)r * (float)sin(an))) {
                        f = (float)r / 6;
                        break;
                    }
                }
            hfac[j * HFW + i] = f * f * (3 - 2 * f);
        }
}
static float hill_f(float x, float z) {
    float fx = x - GX0, fz = z - GZ0;
    int i = (int)floor(fx), j = (int)floor(fz);
    if (i < 0 || j < 0 || i >= HFW - 1 || j >= HFH - 1)
        return 0;
    float a = fx - (float)i, b = fz - (float)j;
    const float *p = &hfac[j * HFW + i];
    return (p[0] * (1 - a) + p[1] * a) * (1 - b) + (p[HFW] * (1 - a) + p[HFW + 1] * a) * b;
}
static float hill_h(float x, float z) {
    float h = 0;
    for (int i = 0; i < NHILLS; i++) {
        float dx = x - hills[i].x, dz = z - hills[i].z, d = (float)sqrt(dx * dx + dz * dz);
        if (d < hills[i].r) {
            float c = (float)cos(d / hills[i].r * 1.5707963f);
            h += hills[i].h * c * c * (0.8f + 0.2f * (float)sin(x * 0.31f + (float)i) * (float)cos(z * 0.27f + (float)i));
        }
    }
    return h > 0 ? h * hill_f(x, z) : 0;
}
static void hill_mesh(int n) {
    int from = nfaces;
    float r = hills[n].r, g = 3;
    int i0 = (int)floor((hills[n].x - r) / g), i1 = (int)floor((hills[n].x + r) / g) + 1;
    int j0 = (int)floor((hills[n].z - r) / g), j1 = (int)floor((hills[n].z + r) / g) + 1;
    for (int j = j0; j < j1; j++)
        for (int i = i0; i < i1; i++) {
            float P[4][3];
            int any = 0;
            for (int k = 0; k < 4; k++) {
                float x = (float)(i + (k == 1 || k == 2)) * g, z = (float)(j + (k >= 2)) * g;
                P[k][0] = x;
                P[k][2] = z;
                P[k][1] = hill_h(x, z);
                if (P[k][1] > 0.03f)
                    any = 1;
            }
            if (!any)
                continue;
            /* two triangles; only the lines of the grid get a bright edge, not the diagonal */
            tri(P[0], P[1], P[2], 0x12001E, 0xFF2FD0, 3);
            tri(P[0], P[2], P[3], 0x12001E, 0xFF2FD0, 6);
        }
    bound_faces(from);
}
/* ---------- palms ---------- */
#define MAXP 120
static struct {
    float x, z, y;
    int id;
} palms[MAXP];
static int npalms;

/* ---------- drawing primitives with depth ---------- */
static float gz; /* the depth of what is drawn now */
static void pset(int x, int y, u8 c) {
    if (x >= 0 && y >= 0 && x < PW && y < PH && gz <= zb[y * PW + x]) {
        img[y * PW + x] = c;
        zb[y * PW + x] = gz;
    }
}
static void rect(int x, int y, int w, int h, u8 c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            pset(x + i, y + j, c);
}
static void line2(int x0, int y0, int x1, int y1, u8 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    if (dx > 4000 || -dy > 4000)
        return;
    for (;;) {
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
static void tline(int x0, int y0, int x1, int y1, int w, u8 c) {
    if (w <= 1) {
        line2(x0, y0, x1, y1, c);
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
static int isin(int a) { /* half degrees, 16384 = 1 */
    return (int)(sin((double)a * 3.14159265 / 360.0) * 16384);
}
static int icos(int a) {
    return isin(a + 180);
}
static int isqrt(long v) {
    return v <= 0 ? 0 : (int)sqrt((double)v);
}
static u32 rnd(u32 *s) {
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}
/* the palm of the screen saver; k: pixels for the height of the eye */
static void palm_shape(int id, int bx, int by, int k, int side, int ox, int oy, u8 c) {
    u32 r = (u32)id * 2654435761u + 77;
    rnd(&r);
    int hp = 520 + (int)(rnd(&r) % 300);
    int lean = side * 60 + (int)(rnd(&r) % 260) - 130;
    int bend = (int)(rnd(&r) % 3);
    int nf = 6 + (int)(rnd(&r) % 3), fl = 230 + (int)(rnd(&r) % 120);
    const int SU = 256;
    bx += ox;
    by += oy;
    int steps = hp * k / SU / 2 + 6, tx = bx, ty = by;
    if (steps > 600)
        steps = 600;
    for (int i = 1; i <= steps; i++) {
        long t = (long)i * 1024 / steps;
        long cv = bend == 0 ? t * t / 1024 : bend == 1 ? t : (t * t / 1024 * t / 1024);
        int x = bx + (int)(lean * cv / 1024) * k / SU, y = by - (int)(hp * t / 1024) * k / SU;
        int w = (int)((34 - 16 * t / 1024) * k / SU);
        tline(tx, ty, x, y, w < 1 ? 1 : w, c);
        tx = x;
        ty = y;
    }
    int cn = 7 * k / SU;
    if (cn >= 2 && !ox)
        for (int i = -1; i <= 1; i++)
            rect(tx + i * cn - cn / 2, ty + cn / 3, cn, cn, c);
    for (int j = 0; j < nf; j++) {
        int th = -25 + j * 230 / (nf - 1) + (int)(rnd(&r) % 24) - 12;
        int dr = 300 + (int)(rnd(&r) % 450);
        int L = (fl + (int)(rnd(&r) % 80)) * k / SU;
        if (th > 70 && th < 110)
            L = L * 3 / 4;
        int cx_ = icos(th * 2), sy_ = isin(th * 2), n = L / 3 > 12 ? L / 3 : 12;
        if (n > 60)
            n = 60;
        int px = tx, py = ty, rw = L / 45 + 1;
        for (int s = 1; s <= n; s++) {
            int x = tx + (int)((long)L * s * cx_ / n / 16384);
            int y = ty - (int)((long)L * s * sy_ / n / 16384) + (int)((long)L * dr / 1024 * s * s / n / n);
            tline(px, py, x, y, rw > 3 ? 3 : rw, c);
            if (s < n && !ox) {
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
                int ll = (int)((long)L * 26 / 100 * isin(s * 300 / n + 60) / 16384);
                for (int q = 0; q < 2; q++) {
                    long dx = (q ? -nx * 6 / 10 : nx) + ux * 7 / 10,
                         dy = (q ? -ny * 6 / 10 : ny) + uy * 7 / 10 + 500;
                    int dl = isqrt(dx * dx + dy * dy);
                    if (!dl)
                        continue;
                    line2(x, y, x + (int)(dx * ll / dl), y + (int)(dy * ll / dl), c);
                }
            }
            px = x;
            py = y;
        }
    }
}

/* ---------- letters for the signs and the logo ---------- */
/* the Violyx logo: 5 rows, coloured from white at the top to dark violet at the bottom */
#define LOGO_W 33
static const char *logo_rows[5] = {
    "X...X.XXX..XXX..X.....X...X.X...X",
    "X...X..X..X...X.X......X.X...X.X.",
    "X...X..X..X...X.X.......X.....X..",
    ".X.X...X..X...X.X.......X....X.X.",
    "..X...XXX..XXX..XXXXX...X...X...X"};
static const u32 logo_col[5] = {0xFD00BB, 0xEB00B4, 0xCB01B5, 0x981EC9, 0x481186};
static int logo_at(int lx, int ly) {
    return lx >= 0 && ly >= 0 && lx < LOGO_W && ly < 5 && logo_rows[ly][lx] == 'X';
}
/* 5x7 letters for the neon signs */
static const char *glyph(char ch) {
    switch (ch) {
        case 'A': return ".XXX.X...XX...XXXXXXX...XX...XX...X";
        case 'B': return "XXXX.X...XX...XXXXX.X...XX...XXXXX.";
        case 'C': return ".XXX.X...XX....X....X....X...X.XXX.";
        case 'D': return "XXXX.X...XX...XX...XX...XX...XXXXX.";
        case 'E': return "XXXXXX....X....XXXX.X....X....XXXXX";
        case 'I': return ".XXX...X....X....X....X....X...XXX.";
        case 'N': return "X...XXX..XX.X.XX..XXX...XX...XX...X";
        case 'R': return "XXXX.X...XX...XXXXX.X.X..X..X.X...X";
        case 'S': return ".XXXXX....X.....XXX.....X....XXXXX.";
        case 'T': return "XXXXX..X....X....X....X....X....X..";
        case 'F': return "XXXXXX....X....XXXX.X....X....X....";
        case 'W': return "X...XX...XX...XX.X.XX.X.XXX.XXX...X";
        case 'M': return "X...XXX.XXX.X.XX...XX...XX...XX...X";
        case 'P': return "XXXX.X...XX...XXXXX.X....X....X....";
        case 'O': return ".XXX.X...XX...XX...XX...XX...X.XXX.";
        case 'H': return "X...XX...XX...XXXXXXX...XX...XX...X";
        case 'G': return ".XXX.X...XX....X.XXXX...XX...X.XXX.";
        case 'L': return "X....X....X....X....X....X....XXXXX";
        case 'U': return "X...XX...XX...XX...XX...XX...X.XXX.";
        case 'Y': return "X...XX...X.X.X...X....X....X....X..";
        case 'V': return "X...XX...XX...XX...X.X.X..X.X...X..";
        case 'X': return "X...XX...X.X.X...X...X.X.X...XX...X";
        default: return 0;
    }
}
/* the text of sign number id and its colour */
static const char *sign_text[] = {"ARCADE BAR", "DINER", "ARCADE", "BAR", "VIO ISLAND", "VIOLYX COMPUTER", "SOFTWARE",
                                  "MENU", "EAT"};
static const u32 sign_col[] = {0xFF40D0, 0x40F0FF, 0xFF40D0, 0x40F0FF, 0xFFE45C, 0xC060FF, 0xFFE45C, 0xFFE45C, 0xFF40D0};
static int sign_pix(int id, float u, float v, float w, float h) {
    /* the text fills the face with a margin; 1 where a letter is */
    const char *t = sign_text[id];
    int n = 0;
    while (t[n])
        n++;
    float cw = w / ((float)n * 6 + 1), ch = h / 9;
    float cs_ = cw < ch ? cw : ch; /* square dots */
    float tw = cs_ * ((float)n * 6 - 1), th = cs_ * 7;
    float x = (u - (w - tw) / 2) / cs_, y = ((h - th) / 2 + th - v) / cs_;
    if (x < 0 || y < 0 || y >= 7)
        return 0;
    int i = (int)x / 6, cx = (int)x % 6;
    if (i >= n || cx >= 5)
        return 0;
    const char *g = glyph(t[i]);
    return g && g[(int)y * 5 + cx] == 'X';
}

/* ---------- shading of the faces ---------- */
static float face_w, face_h; /* size of the face in metres, for the signs */
static u32 shade(const face_t *f, float u, float v) {
    switch (f->sh) {
        case S_WIN: { /* windows on a dark wall, some lit; only whole ones below the roof */
            float wu = frac(u / 2.6f), wv = frac(v / 3.2f);
            if (v > 0.8f && wu > 0.22f && wu < 0.78f && wv > 0.28f && wv < 0.78f &&
                (float)floor(v / 3.2f) * 3.2f + 2.6f < face_h) {
                u32 h = hash((u32)f->id * 977u + (u32)(int)(u / 2.6f) * 131u + (u32)(int)(v / 3.2f));
                return h % 5 == 0 ? 0xFF6AD5 : h % 5 == 1 ? 0x50D8FF : h % 5 == 2 ? 0xFFC060 : 0x2A1A48;
            }
            return f->fill;
        }
        case S_TOWER: { /* glass with a grid, the logo near the top on the side to the street */
            if ((f->id & 7) < 4 && v > 4.5f && v < 11.0f) { /* on all four sides */
                float m = face_w * 0.06f, dw = (face_w - 2 * m) / LOGO_W;
                float y0 = 7.7f + dw * 2.5f, fx = (u - m) / dw, fy = (y0 - v) / dw;
                if (fx >= 0 && fy >= 0 && logo_at((int)fx, (int)fy))
                    return logo_col[(int)fy];
                if (fx >= 0.5f && fy >= 0.5f && logo_at((int)(fx - 0.5f), (int)(fy - 0.5f)))
                    return 0x160E26; /* the shadow */
                return 0x07060C;
            }
            float wu = frac(u / 1.8f), wv = frac(v / 3.5f);
            if (wu < 0.08f || wv < 0.1f)
                return 0x6A3AA8;
            u32 h = hash((u32)f->id * 31u + (u32)(int)(u / 1.8f) * 977u + (u32)(int)(v / 3.5f));
            return h % 7 == 0 ? 0x7A5AE0 : h % 7 == 1 ? 0x40C8F0 : 0x1A1040;
        }
        case S_SCREEN: { /* the screens of the arcade machines: little games */
            float t = now * 1.5f + (float)f->id;
            int g = f->id % 3;
            if (u < 0.05f || u > face_w - 0.05f || v < 0.05f || v > face_h - 0.05f)
                return 0x101010;
            float x = u / face_w, y = v / face_h;
            if (g == 0) { /* invaders */
                int ix = (int)((x + 0.1f * (float)sin(t)) * 8), iy = (int)(y * 10);
                if (iy >= 5 && iy <= 8 && ix >= 1 && ix <= 6 && ((int)(x * 32) + iy) % 2 == 0)
                    return 0x40FF60;
                if (iy == 1 && fabs(x - 0.5 - 0.3 * sin(t * 0.7)) < 0.06)
                    return 0x40C0FF;
                return 0x000818;
            }
            if (g == 1) { /* pong */
                float bx = 0.5f + 0.42f * (float)sin(t * 1.3f), by = 0.5f + 0.4f * (float)sin(t * 0.9f);
                if (fabs(x - bx) < 0.04 && fabs(y - by) < 0.04)
                    return 0xFFFFFF;
                if ((x < 0.08f && fabs(y - by) < 0.15) || (x > 0.92f && fabs(y - by * 0.9f - 0.05f) < 0.15))
                    return 0xE0E0E0;
                if (fabs(x - 0.5) < 0.01 && frac(y * 8) < 0.5f)
                    return 0x606060;
                return 0x000000;
            }
            /* a racing game: road to the horizon */
            if (y > 0.55f)
                return y > 0.55f + 0.04f * (float)sin(x * 20) ? 0xFF6A30 : 0x200830;
            {
                float w = (0.55f - y) * 0.9f + 0.02f, d = fabs(x - 0.5f);
                if (d < w) {
                    if (d > w - 0.03f)
                        return 0xFFFFFF;
                    if (d < 0.01f && frac(1 / (0.6f - y) + t) < 0.5f)
                        return 0xFFE45C;
                    return 0x303040;
                }
                return frac(1 / (0.6f - y) + t) < 0.5f ? 0x20A020 : 0x108010;
            }
        }
        case S_SIGN: /* neon letters */
            if (sign_pix(f->id, u, v, face_w, face_h)) {
                float fl = (float)sin(now * 3 + (float)f->id) * 0.5f + 0.5f;
                return mix(sign_col[f->id], 0xFFFFFF, 0.25f * fl);
            }
            return f->fill;
        case S_STRIPE: /* the lighthouse: red and white */
            return ((int)(v / 3.0f)) % 2 ? 0x140A22 : 0x8A3AD8;
        case S_BOARD: /* the backboard of a basket */
            if (u > face_w * 0.3f && u < face_w * 0.7f && v > face_h * 0.15f && v < face_h * 0.55f &&
                (u < face_w * 0.34f || u > face_w * 0.66f || v < face_h * 0.2f || v > face_h * 0.5f))
                return 0xFF6020;
            return 0xE0E0F0;
        case S_ROOF:
            return frac(u / 3) < 0.04f || frac(v / 3) < 0.04f ? 0x3A1060 : f->fill;
        case S_CEIL: /* the ceiling of the bar: lines of neon light */
            return frac(v / 2.5f) < 0.06f ? (((int)(v / 2.5f)) % 2 ? 0xFF40D0 : 0x40F0FF) : 0x0A0414;
        case S_BOTTLES: { /* the shelf behind the bar */
            float bu = frac(u / 0.25f), bv = frac(v / 0.6f);
            if (bv < 0.06f)
                return 0x6A3A20;
            if (bu > 0.25f && bu < 0.75f && bv < 0.65f + 0.2f * (bu > 0.4f && bu < 0.6f)) {
                u32 h = hash((u32)(int)(u / 0.25f) * 13u + (u32)(int)(v / 0.6f));
                return h % 4 == 0 ? 0x40C060 : h % 4 == 1 ? 0xC06030 : h % 4 == 2 ? 0x6080E0 : 0xE0D0A0;
            }
            return 0x1A0A10;
        }
        case S_FLATB: { /* a low building with a flat roof: one row of windows, a neon band */
            if (v > face_h - 0.6f && v < face_h - 0.42f)
                return f->line;
            if (v > face_h - 0.25f)
                return 0x2A1A3A;
            float wu = frac(u / 3.0f);
            if (v > 1.0f && v < 2.5f && wu > 0.22f && wu < 0.78f && face_w > 1.2f) {
                if (v < 1.08f || v > 2.42f || wu < 0.25f || wu > 0.75f)
                    return 0x4A3A5A;
                u32 h = hash((u32)f->id * 977u + (u32)(int)(u / 3.0f) * 131u);
                return h % 4 == 0 ? 0xFF6AD5 : h % 4 == 1 ? 0x50D8FF : h % 4 == 2 ? 0xFFC060 : 0x2A1A48;
            }
            return f->fill;
        }
        case S_DINER: { /* the diner: a long lit window band between two chrome stripes */
            if (v > face_h - 0.6f && v < face_h - 0.42f)
                return f->line;
            if ((v > 0.7f && v < 0.85f) || (v > 2.75f && v < 2.9f))
                return 0xC8C8D8;
            if (v > 1.0f && v < 2.6f && face_w > 1.2f) {
                if (frac(u / 1.4f) < 0.06f || v < 1.06f || v > 2.54f)
                    return 0x9898A8;
                return frac((u + v) * 0.6f) < 0.06f ? 0xFFE0A0 : 0xD08840;
            }
            return f->fill;
        }
        case S_GLASS: /* the windows of the cars */
            return frac((u + v) * 1.4f) < 0.12f ? 0x3A7A98 : 0x0A2030;
        case S_MENU: { /* the menu board in the diner */
            if (v > face_h * 0.66f)
                return sign_pix(7, u, v - face_h * 0.66f, face_w, face_h * 0.34f) ? 0xFFE45C : 0x101018;
            float r = v / (face_h * 0.66f) * 5, fr = frac(r);
            if (fr < 0.25f || fr > 0.65f || u < face_w * 0.06f || u > face_w * 0.94f)
                return 0x101018;
            float x = u / face_w;
            if (x > 0.75f)
                return x > 0.8f ? 0x40F0FF : 0x101018; /* the price */
            if (x < 0.15f + 0.4f * frac((float)(int)r * 0.37f + 0.2f))
                return frac(u * 3) < 0.7f ? 0xF0E8FF : 0x101018; /* the name */
            return frac(u * 6) < 0.3f ? 0x606070 : 0x101018; /* the dots */
        }
        case S_SHOPW: /* the shop in the tower: big windows, above them the dark base */
            if (v > 0.5f && v < 3.5f) {
                if (frac(u / 1.8f) < 0.07f || v < 0.62f || v > 3.38f)
                    return 0x6A3AA8;
                return frac((u + v) * 0.45f) < 0.07f ? 0x2A5A78 : 0x0C1C2C;
            }
            return 0x1A1040;
        case S_CEIL2: { /* panels of light */
            float a = frac(u / 2.4f), b = frac(v / 2.4f);
            return a > 0.2f && a < 0.8f && b > 0.2f && b < 0.8f ? 0x8A7AC8 : 0x18142A;
        }
        case S_PC: { /* a Violyx desktop on a computer of the shop */
            float x = u / face_w, y = 1 - v / face_h;
            if (x < 0.06f || x > 0.94f || y < 0.07f || y > 0.93f)
                return 0x2A2830; /* the frame of the tube */
            x = (x - 0.06f) / 0.88f;
            y = (y - 0.07f) / 0.86f;
            if (y < 0.1f)
                return x < 0.3f && frac(x * 20) < 0.6f ? 0xE040FF : 0x3A2A80;
            float wx = 0.12f + 0.1f * (float)(f->id % 3), wy = 0.22f + 0.05f * (float)(f->id % 2);
            if (x > wx && x < wx + 0.62f && y > wy && y < wy + 0.55f) {
                if (x < wx + 0.01f || x > wx + 0.61f || y > wy + 0.54f)
                    return 0xE040FF;
                if (y < wy + 0.09f)
                    return 0x7A3AD0;
                float ly = (y - wy - 0.12f) / 0.07f;
                if (ly >= 0 && frac(ly) < 0.5f && x < wx + 0.1f + 0.4f * frac((float)(int)ly * 0.37f + 0.3f))
                    return 0xD8C8FF;
                if (ly >= 0 && (int)ly == (int)(now * 1.3f) % 5 && frac(ly) < 0.5f && frac(now * 2) < 0.5f &&
                    x > wx + 0.04f && x < wx + 0.07f)
                    return 0xFFFFFF; /* the cursor */
                return 0x120A24;
            }
            return 0x241050;
        }
        case S_SOFT: { /* shelves with boxes of software */
            float sv = frac(v / 0.55f);
            if (sv < 0.08f)
                return 0x5A3A2A;
            float bu = frac(u / 0.24f);
            if (bu < 0.08f || sv > 0.85f)
                return 0x100818;
            u32 h = hash((u32)(int)(u / 0.24f) * 31u + (u32)(int)(v / 0.55f) * 7u);
            static const u32 bc[6] = {0xE040FF, 0x40C8F0, 0xFFE45C, 0xFF6A40, 0x60E080, 0xF0F0F0};
            u32 c = bc[h % 6];
            if (sv > 0.45f && sv < 0.55f)
                return 0x101010; /* the label */
            return c;
        }
        case S_LOGO: { /* the Violyx logo filling the face */
            float m = face_w * 0.07f, dw = (face_w - 2 * m) / LOGO_W;
            float fx = (u - m) / dw, fy = (face_h / 2 + dw * 2.5f - v) / dw;
            if (fx >= 0 && fy >= 0 && logo_at((int)fx, (int)fy))
                return logo_col[(int)fy];
            if (fx >= 0.5f && fy >= 0.5f && logo_at((int)(fx - 0.5f), (int)(fy - 0.5f)))
                return 0x160E26;
            return 0x07060C;
        }
        case S_LAMP: /* the light of the lighthouse, it flashes when the beam passes */
            return mix(0x806020, 0xFFF8C0, (float)pow(0.5 + 0.5 * sin(now * 2.2 + u), 6));
        default:
            return f->fill;
    }
}

/* ---------- maths that the small math.h has not ---------- */
static float atanf_(float x) {
    int inv = x > 1 || x < -1;
    if (inv)
        x = 1 / x;
    float y = x * x;
    float r = x * (0.9998660f + y * (-0.3302995f + y * (0.1801410f + y * (-0.0851330f + y * 0.0208351f))));
    if (inv)
        r = (x > 0 ? 1.5707963f : -1.5707963f) - r;
    return r;
}
static float atan2f_(float y, float x) {
    if (x > 0)
        return atanf_(y / x);
    if (x < 0)
        return atanf_(y / x) + (y >= 0 ? 3.1415927f : -3.1415927f);
    return y > 0 ? 1.5707963f : y < 0 ? -1.5707963f : 0;
}
static float tan_(float a) {
    return (float)(sin(a) / cos(a));
}
static float wrap(float a) { /* -pi..pi */
    while (a > 3.1415927f)
        a -= 6.2831853f;
    while (a < -3.1415927f)
        a += 6.2831853f;
    return a;
}

/* ---------- polygons: cut at the near plane, filled with depth, bright edges ---------- */
#define ZNEAR 0.08f
typedef struct {
    float X, Y, Z, u, v;
} cv_t;
typedef struct {
    float x, y, iz, uz, vz;
} sv_t;
static int clip_near(const cv_t *in, int n, cv_t *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const cv_t *a = &in[i], *b = &in[(i + 1) % n];
        int ia = a->Z >= ZNEAR, ib = b->Z >= ZNEAR;
        if (ia)
            out[m++] = *a;
        if (ia != ib) {
            float t = (ZNEAR - a->Z) / (b->Z - a->Z);
            cv_t c = {a->X + t * (b->X - a->X), a->Y + t * (b->Y - a->Y), ZNEAR,
                      a->u + t * (b->u - a->u), a->v + t * (b->v - a->v)};
            out[m++] = c;
        }
    }
    return m;
}
static void raster(const sv_t *v, int n, const face_t *f) {
    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) {
        if (v[i].y < miny)
            miny = v[i].y;
        if (v[i].y > maxy)
            maxy = v[i].y;
    }
    int y0 = (int)-floor(0.5f - miny), y1 = (int)floor(maxy - 0.5f);
    if (y0 < 0)
        y0 = 0;
    if (y1 > PH - 1)
        y1 = PH - 1;
    u8 fc = near(f->fill);
    int flat = f->sh == S_FLAT;
    for (int y = y0; y <= y1; y++) {
        float yc = (float)y + 0.5f, xl = 1e9f, xr = -1e9f;
        sv_t L = {0}, R = {0};
        for (int i = 0; i < n; i++) {
            const sv_t *a = &v[i], *b = &v[(i + 1) % n];
            if ((a->y <= yc && b->y > yc) || (b->y <= yc && a->y > yc)) {
                float t = (yc - a->y) / (b->y - a->y);
                sv_t p = {a->x + t * (b->x - a->x), yc, a->iz + t * (b->iz - a->iz),
                          a->uz + t * (b->uz - a->uz), a->vz + t * (b->vz - a->vz)};
                if (p.x < xl) {
                    xl = p.x;
                    L = p;
                }
                if (p.x > xr) {
                    xr = p.x;
                    R = p;
                }
            }
        }
        if (xl > xr)
            continue;
        int xa = (int)-floor(0.5f - xl), xb = (int)floor(xr - 0.5f);
        if (xa < 0)
            xa = 0;
        if (xb > PW - 1)
            xb = PW - 1;
        float dx = xr - xl;
        for (int x = xa; x <= xb; x++) {
            float t = dx > 0 ? ((float)x + 0.5f - xl) / dx : 0;
            float iz = L.iz + t * (R.iz - L.iz);
            if (iz <= 0)
                continue;
            float z = 1 / iz;
            int o = y * PW + x;
            if (z >= zb[o])
                continue;
            zb[o] = z;
            if (flat)
                img[o] = fc;
            else {
                float u = (L.uz + t * (R.uz - L.uz)) * z, vv = (L.vz + t * (R.vz - L.vz)) * z;
                u32 c = shade(f, u, vv);
                if (z > 60)
                    c = mix(c, 0x3A0C50, (z - 60) / 300);
                img[o] = dith(c, x, y);
            }
        }
    }
}
/* a bright edge, in front of its own face */
static void edge3(cv_t a, cv_t b, u32 col) {
    if (a.Z < ZNEAR && b.Z < ZNEAR)
        return;
    if (a.Z < ZNEAR || b.Z < ZNEAR) {
        float t = (ZNEAR - a.Z) / (b.Z - a.Z);
        cv_t c = {a.X + t * (b.X - a.X), a.Y + t * (b.Y - a.Y), ZNEAR, 0, 0};
        if (a.Z < ZNEAR)
            a = c;
        else
            b = c;
    }
    float x0 = (float)CX + FOC * a.X / a.Z, y0 = (float)HY - FOC * a.Y / a.Z;
    float x1 = (float)CX + FOC * b.X / b.Z, y1 = (float)HY - FOC * b.Y / b.Z;
    float dx = x1 - x0, dy = y1 - y0, len = fabs(dx) > fabs(dy) ? (float)fabs(dx) : (float)fabs(dy);
    if (len > 6000)
        return;
    int n = (int)len + 1;
    float ia = 1 / a.Z, ib = 1 / b.Z;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        int x = (int)(x0 + dx * t), y = (int)(y0 + dy * t);
        if (x < 0 || y < 0 || x >= PW || y >= PH)
            continue;
        float z = 1 / (ia + t * (ib - ia));
        int o = y * PW + x;
        if (z <= zb[o] * 1.004f + 0.03f) {
            img[o] = dith(z > 25 ? mix(col, 0x3A1060, (z - 25) / 220) : col, x, y);
            zb[o] = z * 0.996f;
        }
    }
}
static void draw_face(const face_t *f) {
    float dx = f->cx - camx, dz = f->cz - camz;
    float d = (float)sqrt(dx * dx + dz * dz);
    if (d - f->r > 320)
        return;
    float Zc = dx * sn + dz * cs, Xc = dx * cs - dz * sn;
    if (Zc < -f->r)
        return; /* behind */
    if (Xc - f->r > (Zc + f->r) * (float)CX / FOC + 2 || Xc + f->r < -(Zc + f->r) * (float)CX / FOC - 2)
        return; /* left or right of the view */
    cv_t c[4], k[8];
    for (int i = 0; i < f->n; i++) {
        cam(f->p[i][0], f->p[i][1], f->p[i][2], &c[i].X, &c[i].Y, &c[i].Z);
        c[i].u = f->uv[i][0];
        c[i].v = f->uv[i][1];
    }
    int m = clip_near(c, f->n, k);
    if (m >= 3) {
        sv_t s[8];
        for (int i = 0; i < m; i++) {
            float iz = 1 / k[i].Z;
            s[i].x = (float)CX + FOC * k[i].X * iz;
            s[i].y = (float)HY - FOC * k[i].Y * iz;
            s[i].iz = iz;
            s[i].uz = k[i].u * iz;
            s[i].vz = k[i].v * iz;
        }
        face_w = f->uv[2][0];
        face_h = f->uv[2][1];
        raster(s, m, f);
    }
    for (int i = 0; i < f->n; i++)
        if (f->edges >> i & 1)
            edge3(c[i], c[(i + 1) % f->n], f->line);
}

/* ---------- sky, sun and the distant islands ---------- */
#define NSTARS 260
static float star_a[NSTARS], star_e[NSTARS];
static int star_p[NSTARS];
static const float SUN_A = 1.45f; /* the sun is in the east, over the end of the main street */
static float sun_x, sun_y, sun_r;  /* on the screen; sun_r 0 when it is not in view */
static float mountain(float a) {
    float h = 0.020f * (float)sin(3 * a + 1) + 0.012f * (float)sin(7 * a + 2) + 0.007f * (float)sin(13 * a) - 0.006f;
    return h > 0 ? h : 0;
}
static void sky(void) {
    for (int y = 0; y < HY; y++) {
        u32 c = y < HY / 2 ? mix(0x0B0326, 0x3A0C5C, (float)y / (float)(HY / 2))
                           : mix(0x3A0C5C, 0xE0307E, (float)(y - HY / 2) / (float)(HY - HY / 2));
        for (int x = 0; x < PW; x++) {
            img[y * PW + x] = dith(c, x, y);
            zb[y * PW + x] = 1e9f;
        }
    }
    u8 sc = near(0xC0B0FF);
    for (int i = 0; i < NSTARS; i++) {
        float d = wrap(star_a[i] - yaw);
        if (d > 1.3f || d < -1.3f)
            continue;
        float cd = (float)cos(d);
        int x = CX + (int)(FOC * (float)sin(d) / cd), y = HY - (int)(FOC * (float)tan_(star_e[i]) / cd);
        if (x >= 0 && x < PW && y >= 0 && y < HY * 2 / 3 && ((int)(now * 1000) + star_p[i]) % 3000 < 2300)
            img[y * PW + x] = sc;
    }
    /* the sun rises within 25 seconds and stays */
    float rise = now > 25 ? 1 : now / 25, ang_r = 0.17f;
    float el = -ang_r + (ang_r * 1.45f) * rise;
    float d = wrap(SUN_A - yaw);
    sun_r = 0;
    if (d < 1.4f && d > -1.4f) {
        float cd = (float)cos(d);
        sun_x = (float)CX + FOC * (float)sin(d) / cd;
        sun_y = (float)HY - FOC * (float)tan_(el) / cd;
        sun_r = FOC * ang_r;
        int R = (int)sun_r, sy = (int)sun_y, sx = (int)sun_x;
        for (int y = sy - R; y <= sy + R && y < HY; y++) {
            if (y < 0)
                continue;
            int dy = y - sy, half = (int)sqrt((double)(R * R - dy * dy));
            int rel = (y - (sy - R)) * 100 / (2 * R + 1);
            if (rel > 50) {
                int band = (rel - 50) / 9, in = (rel - 50) % 9;
                if (in < 1 + band)
                    continue;
            }
            u32 c = mix(0xFFE45C, 0xFF2F7F, (float)(y - (sy - R)) / (float)(2 * R + 1));
            for (int x = sx - half; x <= sx + half; x++)
                if (x >= 0 && x < PW)
                    img[y * PW + x] = dith(c, x, y);
        }
    }
    for (int x = 0; x < PW; x++) { /* far islands on the horizon, in front of the sun */
        float a = yaw + atanf_(((float)x + 0.5f - (float)CX) / FOC);
        int h = (int)(FOC * mountain(a));
        for (int y = HY - h; y < HY; y++)
            if (y >= 0)
                img[y * PW + x] = dith(mix(0x2A0A40, 0x14062A, (float)(y - (HY - h)) / (float)(h + 1)), x, y);
    }
}

/* ---------- the floor, cast row by row ---------- */
/* up close: the coast and the roads from distances read between the cells, so their edges are
 * sharp and smooth instead of steps of 25 cm */
static int near_kind(float px, float pz, int k) {
    float fi = (px - GX0) * 4 - 0.5f, fj = (pz - GZ0) * 4 - 0.5f;
    int i = (int)floor(fi), j = (int)floor(fj);
    if (i < 0 || j < 0 || i >= GW - 1 || j >= GH - 1)
        return k;
    float a = fi - (float)i, b = fj - (float)j;
    int o = j * GW + i;
    float cd = ((float)cdist[o] * (1 - a) + (float)cdist[o + 1] * a) * (1 - b) +
               ((float)cdist[o + GW] * (1 - a) + (float)cdist[o + GW + 1] * a) * b;
    if (cd <= 0)
        return T_WATER;
    int nk = cd < 50 || (pz > 0 && px < 100 && cd < 90) ? T_BEACH : T_LAND;
    int n = o + (b > 0.5f ? GW : 0) + (a > 0.5f ? 1 : 0);
    if (rdist[o] < 65535 && rdist[o + 1] < 65535 && rdist[o + GW] < 65535 && rdist[o + GW + 1] < 65535) {
        float rd = (((float)rdist[o] * (1 - a) + (float)rdist[o + 1] * a) * (1 - b) +
                    ((float)rdist[o + GW] * (1 - a) + (float)rdist[o + GW + 1] * a) * b) / 100;
        float al = (float)ralong[n] / 10, h = roads[0].half;
        if (rd < h)
            return rd > h - 0.3f ? T_EDGE : rd < 0.12f && frac(al / 6) < 0.5f ? T_DASH : T_ROAD;
    }
    if (rdist1[o] < 65535 && rdist1[o + 1] < 65535 && rdist1[o + GW] < 65535 && rdist1[o + GW + 1] < 65535) {
        float rd = (((float)rdist1[o] * (1 - a) + (float)rdist1[o + 1] * a) * (1 - b) +
                    ((float)rdist1[o + GW] * (1 - a) + (float)rdist1[o + GW + 1] * a) * b) / 100;
        float al = (float)ralong1[n] / 10, h = roads[rway[n]].half;
        if (rd < h)
            return rd > h - 0.3f && frac(al / 2) < 0.6f ? T_DIRTE : T_DIRT;
    }
    if (k == T_ROAD || k == T_EDGE || k == T_DASH || k == T_DIRT || k == T_DIRTE || k == T_WATER)
        return nk; /* just beside the road or the water */
    return k == T_BEACH || k == T_LAND ? nk : k;
}
static void floor_rows(void) {
    u32 haze = 0x3A0C50;
    for (int y = HY; y < PH; y++) {
        float dy = (float)y + 0.5f - (float)HY;
        float Z = FOC * eye / dy, st = Z / FOC;
        float px = camx + sn * Z + cs * ((0.5f - (float)CX) * st);
        float pz = camz + cs * Z - sn * ((0.5f - (float)CX) * st);
        float ddx = cs * st, ddz = -sn * st;
        /* how much floor one pixel covers along x and along z: a line at least that wide is
         * never missed between two pixels */
        float dZ = Z / dy, acs = (float)fabs(cs), asn = (float)fabs(sn);
        float fx = acs * st + asn * dZ, fz = asn * st + acs * dZ;
        float w = st * 0.9f, ht = Z / 170;
        if (w < 0.05f)
            w = 0.05f;
        float wx = fx > 0.05f ? fx : 0.05f, wz = fz > 0.05f ? fz : 0.05f;
        float lamt = Z < 30 ? 1 : Z > 110 ? 0 : 1 - (Z - 30) / 80;
        float lx3 = lamt * (wx > 1.2f ? 1.2f / wx : 1), lz3 = lamt * (wz > 1.2f ? 1.2f / wz : 1);
        u32 gridc = mix(0xFF2FD0, 0x4A1060, Z / 90);
        for (int x = 0; x < PW; x++, px += ddx, pz += ddz) {
            int i = (int)((px - GX0) * 4), j = (int)((pz - GZ0) * 4);
            int k = (i < 0 || j < 0 || i >= GW || j >= GH) ? T_WATER : ter[j * GW + i];
            if (Z < 45 && k != T_COURT && k != T_COURTL && k != T_FLOOR && k != T_PARK && k != T_PARKL &&
                k != T_SHOP && k != T_DINER && !(k == T_EDGE && ((px > PK_X0 - 0.3f && px < PK_X1 + 0.3f && pz > PK_Z0 && pz < PK_Z1 + 0.3f))))
                k = near_kind(px, pz, k);
            u32 c;
            switch (k) {
                case T_WATER: {
                    c = 0x0A0620;
                    float gx = frac(px / 8 + wx / 16), gz2 = frac(pz / 8 + wz / 16);
                    if (lamt > 0 && (gx < wx / 8 || gz2 < wz / 8))
                        c = mix(c, 0x203080, (gx < wx / 8 ? lx3 : lz3) * 0.8f);
                    break;
                }
                case T_BEACH:
                    c = 0x241030;
                    if (frac((px + pz) / 1.3f) < ((wx + wz) / 2.6f > 0.07f ? (wx + wz) / 2.6f : 0.07f))
                        c = mix(c, 0x9A8436, (lx3 + lz3) * 0.35f + 0.08f);
                    break;
                case T_ROAD: c = 0x1C0C30; break;
                case T_EDGE: c = 0x2FF0FF; break;
                case T_DASH: c = 0xFFE45C; break;
                case T_DIRT: c = 0x2A0E1C; break;
                case T_DIRTE: c = 0x8A3A70; break;
                case T_COURT: c = 0x161C60; break;
                case T_COURTL: c = 0xE0E0FF; break;
                case T_FLOOR:
                    c = (((int)floor(px) + (int)floor(pz)) & 1) ? 0x3A0A4A : 0x0A0410;
                    break;
                case T_PARK: c = 0x1C0C30; break;
                case T_DINER: /* black and white tiles */
                    c = (((int)floor(px * 2) + (int)floor(pz * 2)) & 1) ? 0xB0A8C0 : 0x141020;
                    break;
                case T_PARKL: c = 0xC8C8E0; break;
                case T_SHOP: { /* tiles along the walls of the tower */
                    float lx, lz;
                    tower_local(px, pz, &lx, &lz);
                    float fx2 = frac(lx / 1.2f), fz2 = frac(lz / 1.2f);
                    c = fx2 < 0.04f || fz2 < 0.04f ? 0x0E0A18 : ((((int)floor(lx / 1.2f) + (int)floor(lz / 1.2f)) & 1) ? 0x2E2848 : 0x1E1A34);
                    break;
                }
                default: { /* land: the wire grid */
                    c = 0x12001E;
                    float gx = frac(px / 3 + wx / 6), gz2 = frac(pz / 3 + wz / 6);
                    if (lamt > 0 && (gx < wx / 3 || gz2 < wz / 3))
                        c = mix(c, gridc, gx < wx / 3 ? lx3 : lz3);
                    else if (lamt <= 0 && ((int)(pz / 3) & 1))
                        c = 0x1E0430;
                }
            }
            if (i >= 0 && j >= 0 && i < GW && j < GH && glow[j * GW + i])
                c = mix(c, 0xFFA0D8, (float)glow[j * GW + i] / 600);
            int o = y * PW + x;
            img[o] = dith(ht > 0 ? mix(c, haze, ht * ht) : c, x, y);
            zb[o] = Z;
        }
    }
}

/* ---------- palms, far ones first ---------- */
static void draw_palms(void) {
    int ord[MAXP], n = 0;
    float Zs[MAXP], Xs[MAXP];
    for (int i = 0; i < npalms; i++) {
        float X, Y, Z;
        cam(palms[i].x, 0, palms[i].z, &X, &Y, &Z);
        if (Z < 1.0f || Z > 260 || fabs(X) > Z * (float)CX / FOC + 12)
            continue;
        Zs[i] = Z;
        Xs[i] = X;
        int j = n++;
        while (j > 0 && Zs[ord[j - 1]] < Z) {
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = i;
    }
    u8 black = near(0x000000), rimc = near(0xFF4FA0);
    for (int q = 0; q < n; q++) {
        int i = ord[q];
        float Z = Zs[i];
        int bx = CX + (int)(FOC * Xs[i] / Z), by = HY + (int)(FOC * (eye - palms[i].y) / Z);
        int k = (int)(FOC * EYE / Z * 1.7f);
        int side = palms[i].id & 1 ? 1 : -1;
        gz = Z;
        if (k >= 24)
            palm_shape(palms[i].id, bx, by, k, side, sun_r > 0 && sun_x < (float)bx ? -1 : 1, -1, rimc);
        palm_shape(palms[i].id, bx, by, k, side, 0, 0, black);
    }
}

/* ---------- building the island ---------- */
/* the kind of a cell from the road distances: the main street lies on top */
static int road_kind(int o, int k) {
    float rd = (float)rdist[o] / 100, al = (float)ralong[o] / 10, h = roads[0].half;
    if (rdist[o] < 65535 && rd < h)
        return rd > h - 0.3f ? T_EDGE : rd < 0.12f && frac(al / 6) < 0.5f ? T_DASH : T_ROAD;
    if (rdist1[o] < 65535) {
        float r1 = (float)rdist1[o] / 100, a1 = (float)ralong1[o] / 10, h1 = roads[rway[o]].half;
        if (r1 < h1)
            return r1 > h1 - 0.3f && frac(a1 / 2) < 0.6f ? T_DIRTE : T_DIRT;
    }
    return k;
}
#define BAR_X (-72.0f)
#define BAR_Z (-40.0f)
#define BAR_W 16.0f
#define BAR_D 11.0f
#define BAR_H 4.2f
#define DN_X 66.0f
#define DN_Z 54.0f
#define DN_W 16.0f
#define DN_D 8.0f
#define DN_H 4.5f
static void build_ground(void) {
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++) {
            float x = GX0 + ((float)i + 0.5f) * RES, z = GZ0 + ((float)j + 0.5f) * RES;
            u8 k = T_WATER;
            int in = inside(x, z, coast, NCOAST), o = j * GW + i;
            float cd = seg_dist(x, z, coast, NCOAST, 1, 0) * 10;
            cdist[o] = (short)(in ? (cd > 3000 ? 3000 : cd) : -(cd > 3000 ? 3000 : cd));
            rdist[o] = rdist1[o] = 65535;
            for (int r = 0; r < NROADS; r++) {
                float al, rd = seg_dist(x, z, roads[r].p, roads[r].n, 0, &al);
                if (rd >= 12)
                    continue;
                if (!r) {
                    rdist[o] = (unsigned short)(rd * 100);
                    ralong[o] = (unsigned short)(al * 10);
                } else if (rd * 100 < rdist1[o]) {
                    rdist1[o] = (unsigned short)(rd * 100);
                    ralong1[o] = (unsigned short)(al * 10);
                    rway[o] = (u8)r;
                }
            }
            if (in) {
                k = T_LAND;
                float d = cd / 10;
                if (d < 5 || (z > 0 && d < 9 && x < 100))
                    k = T_BEACH;
                k = road_kind(o, k);
                /* the basketball court, its lines */
                if (x > -64 && x < -36 && z > -9.5f && z < 5.5f) {
                    float lx = x + 50, lz = z + 2, ax = fabs(lx);
                    int ln = x < -63.85f || x > -36.15f || z < -9.35f || z > 5.35f || fabs(lx) < 0.08f ||
                             fabs((float)sqrt(lx * lx + lz * lz) - 1.8f) < 0.08f ||
                             (ax > 8.2f && ax < 8.36f && fabs(lz) < 2.45f) || (ax > 8.2f && fabs(fabs(lz) - 2.45f) < 0.08f) ||
                             fabs((float)sqrt((ax - 12.4f) * (ax - 12.4f) + lz * lz) - 6.75f) < 0.08f;
                    k = ln ? T_COURTL : T_COURT;
                }
                if (fabs(x - BAR_X) < BAR_W / 2 && fabs(z - BAR_Z) < BAR_D / 2)
                    k = T_FLOOR;
                if (fabs(x - DN_X) < DN_W / 2 && fabs(z - DN_Z) < DN_D / 2)
                    k = T_DINER;
                /* the car park in front of the bar: stalls on both sides of the way to the door */
                if (x > PK_X0 && x < PK_X1 && z > PK_Z0 && z < PK_Z1) {
                    k = T_PARK;
                    float sx = frac((x - PK_X0) / 2.6f) * 2.6f;
                    if (z < PK_Z0 + 5 && (x < BAR_X - 2.5f || x > BAR_X + 2.5f) && (sx < 0.12f || sx > 2.48f))
                        k = T_PARKL;
                    if (fabs(z - (PK_Z0 + 5)) < 0.06f && fabs(x - BAR_X) > 2.5f)
                        k = T_PARKL;
                    int curb = x < PK_X0 + 0.25f || x > PK_X1 - 0.25f || (z > PK_Z1 - 0.25f && fabs(x - BAR_X) > 4.5f);
                    if (curb)
                        k = T_EDGE;
                }
                {
                    float lx, lz;
                    tower_local(x, z, &lx, &lz);
                    if (fabs(lx) < TW_S / 2 && fabs(lz) < TW_S / 2)
                        k = T_SHOP;
                }
            }
            ter[j * GW + i] = k;
        }
}
static void P3(float *p, float x, float y, float z) {
    p[0] = x;
    p[1] = y;
    p[2] = z;
}
/* a flat sign or picture on a wall: x0,z0 to x1,z1 along the wall, from y0 to y1 */
static void panel(float x0, float z0, float x1, float z1, float y0, float y1, int sh, u32 fill, u32 line,
                  int id) {
    float a[3], b[3], c[3], d[3];
    P3(a, x0, y0, z0);
    P3(b, x1, y0, z1);
    P3(c, x1, y1, z1);
    P3(d, x0, y1, z0);
    float w = (float)sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
    int from = nfaces;
    quad(a, b, c, d, w, y1 - y0, sh, fill, line, line ? 15 : 0, id);
    bound_faces(from);
}
static void pyramid_roof(float cx, float cz, float w, float d, float y, float h, float rot, u32 fill, u32 line) {
    float c = (float)cos(rot), s = (float)sin(rot), P[4][3], A[3] = {cx, y + h, cz};
    float lx[4] = {-w / 2, w / 2, w / 2, -w / 2}, lz[4] = {-d / 2, -d / 2, d / 2, d / 2};
    for (int i = 0; i < 4; i++)
        P3(P[i], cx + lx[i] * c - lz[i] * s, y, cz + lx[i] * s + lz[i] * c);
    int from = nfaces;
    for (int i = 0; i < 4; i++)
        tri(P[i], P[(i + 1) % 4], A, fill, line, 7);
    bound_faces(from);
}
/* a car of the eighties: a flat wedge with a glass cabin, round wheels, pop-up headlights and a
 * band of red lights at the back; the nose points to -z */
static float car_x, car_z;
static void CP(float *p, float lx, float y, float lz) {
    P3(p, car_x + lx, y, car_z + lz);
}
static void cquad(const float *a, const float *b, const float *c, const float *d, int sh, u32 fill, u32 line,
                  int edges) {
    quad(a, b, c, d, 1, 1, sh, fill, line, edges, 0);
}
static void wheel(float lx, float lz, float side) {
    float o = lx + side * 0.12f, in = lx - side * 0.12f, R0 = 0.33f, cy = 0.33f, P[8][2];
    for (int k = 0; k < 8; k++) {
        float an = (float)k * 0.7854f + 0.3927f;
        P[k][0] = lz + R0 * (float)cos(an);
        P[k][1] = cy + R0 * (float)sin(an);
    }
    for (int k = 0; k < 8; k++) { /* the tyre */
        int n = (k + 1) % 8;
        float a[3], b[3], c[3], d[3];
        CP(a, in, P[k][1], P[k][0]);
        CP(b, in, P[n][1], P[n][0]);
        CP(c, o, P[n][1], P[n][0]);
        CP(d, o, P[k][1], P[k][0]);
        cquad(a, b, c, d, S_FLAT, 0x0A0A0E, 0x303040, 2);
    }
    float C[3], hc[3];
    CP(C, o + side * 0.005f, cy, lz);
    for (int k = 0; k < 8; k++) { /* the side of the tyre and the silver hub cap */
        int n = (k + 1) % 8;
        float a[3], b[3];
        CP(a, o + side * 0.005f, P[k][1], P[k][0]);
        CP(b, o + side * 0.005f, P[n][1], P[n][0]);
        tri(a, b, C, 0x121218, 0x404050, 1);
        CP(a, o + side * 0.01f, cy + (P[k][1] - cy) * 0.55f, lz + (P[k][0] - lz) * 0.55f);
        CP(b, o + side * 0.01f, cy + (P[n][1] - cy) * 0.55f, lz + (P[n][0] - lz) * 0.55f);
        CP(hc, o + side * 0.01f, cy, lz);
        tri(a, b, hc, 0x9090A8, 0xC8C8E0, 1);
    }
}
static void car(float cx, float cz, u32 col) {
    int from = nfaces;
    car_x = cx;
    car_z = cz;
    u32 body = 0x1A0A2A;
    /* the side profile: nose, hood, deck, tail */
    static const float pz[6] = {-2.15f, -2.15f, -0.9f, 1.9f, 2.15f, 2.15f};
    static const float py[6] = {0.28f, 0.55f, 0.72f, 0.75f, 0.72f, 0.28f};
    for (int s = -1; s <= 1; s += 2) {
        float x = 0.9f * (float)s, A[3], B[3], C[3], D[3], E[3], F[3], Cb[3], Db[3];
        CP(A, x, py[0], pz[0]);
        CP(B, x, py[1], pz[1]);
        CP(C, x, py[2], pz[2]);
        CP(D, x, py[3], pz[3]);
        CP(E, x, py[4], pz[4]);
        CP(F, x, py[5], pz[5]);
        CP(Cb, x, 0.28f, pz[2]);
        CP(Db, x, 0.28f, pz[3]);
        cquad(A, B, C, Cb, S_FLAT, body, col, 11);
        cquad(Cb, C, D, Db, S_FLAT, body, col, 10);
        cquad(Db, D, E, F, S_FLAT, body, col, 14);
        /* a neon stripe along the side */
        float a[3], b[3], c[3], d[3], xs = x + 0.01f * (float)s;
        CP(a, xs, 0.47f, -1.9f);
        CP(b, xs, 0.47f, 2.0f);
        CP(c, xs, 0.52f, 2.0f);
        CP(d, xs, 0.52f, -1.9f);
        cquad(a, b, c, d, S_FLAT, col, 0, 0);
    }
    for (int k = 0; k < 5; k++) { /* across: the front, the hood, the deck, the tail slope, the back */
        float a[3], b[3], c[3], d[3];
        CP(a, -0.9f, py[k], pz[k]);
        CP(b, 0.9f, py[k], pz[k]);
        CP(c, 0.9f, py[k + 1], pz[k + 1]);
        CP(d, -0.9f, py[k + 1], pz[k + 1]);
        cquad(a, b, c, d, S_FLAT, body, col, 0);
    }
    /* the cabin: windscreen, roof, rear window and the two side windows */
    float W[4][3], T[4][3];
    CP(W[0], -0.78f, 0.74f, -0.7f);
    CP(W[1], 0.78f, 0.74f, -0.7f);
    CP(W[2], 0.78f, 0.75f, 1.5f);
    CP(W[3], -0.78f, 0.75f, 1.5f);
    CP(T[0], -0.64f, 1.12f, -0.05f);
    CP(T[1], 0.64f, 1.12f, -0.05f);
    CP(T[2], 0.64f, 1.12f, 1.0f);
    CP(T[3], -0.64f, 1.12f, 1.0f);
    cquad(W[0], W[1], T[1], T[0], S_GLASS, 0, col, 15);
    cquad(T[0], T[1], T[2], T[3], S_FLAT, body, col, 15);
    cquad(W[3], W[2], T[2], T[3], S_GLASS, 0, col, 15);
    cquad(W[1], W[2], T[2], T[1], S_GLASS, 0, col, 15);
    cquad(W[3], W[0], T[0], T[3], S_GLASS, 0, col, 15);
    /* lights: two at the front, a red band at the back with the number plate */
    for (int s = -1; s <= 1; s += 2) {
        float a[3], b[3], c[3], d[3], x0 = 0.45f * (float)s, x1 = 0.8f * (float)s;
        CP(a, x0, 0.38f, -2.16f);
        CP(b, x1, 0.38f, -2.16f);
        CP(c, x1, 0.48f, -2.16f);
        CP(d, x0, 0.48f, -2.16f);
        cquad(a, b, c, d, S_FLAT, 0xF0F0C0, 0, 0);
    }
    {
        float a[3], b[3], c[3], d[3];
        CP(a, 0.82f, 0.56f, 2.16f);
        CP(b, -0.82f, 0.56f, 2.16f);
        CP(c, -0.82f, 0.66f, 2.16f);
        CP(d, 0.82f, 0.66f, 2.16f);
        cquad(a, b, c, d, S_FLAT, 0xFF2040, 0, 0);
        CP(a, 0.25f, 0.36f, 2.16f);
        CP(b, -0.25f, 0.36f, 2.16f);
        CP(c, -0.25f, 0.48f, 2.16f);
        CP(d, 0.25f, 0.48f, 2.16f);
        cquad(a, b, c, d, S_FLAT, 0xE0E0F0, 0, 0);
    }
    for (int w = 0; w < 4; w++)
        wheel(w & 1 ? 0.92f : -0.92f, w & 2 ? 1.35f : -1.4f, w & 1 ? 1.0f : -1.0f);
    bound_faces(from);
    mark_rect(cx, cz, 2.2f, 4.5f, 0, 1);
}
static void build_bar(void) {
    float x0 = BAR_X - BAR_W / 2, x1 = BAR_X + BAR_W / 2, z0 = BAR_Z - BAR_D / 2, z1 = BAR_Z + BAR_D / 2;
    u32 wall = 0x1A0A2A, line = 0xFF40D0;
    float t = 0.3f;
    box(BAR_X, z0, BAR_W, t, BAR_H, 0, 0, S_FLATB, wall, line, 0, 1);       /* south */
    box(x0, BAR_Z, t, BAR_D, BAR_H, 0, 0, S_FLATB, wall, line, 0, 1);       /* west */
    box(x1, BAR_Z, t, BAR_D, BAR_H, 0, 0, S_FLAT, wall, line, 0, 1);        /* east, behind the bar */
    float door = 1.2f, dh = 2.7f, lw = (BAR_W / 2 - door);
    box(x0 + lw / 2, z1, lw, t, BAR_H, 0, 0, S_FLATB, wall, line, 0, 1);    /* north, left of the door */
    box(x1 - lw / 2, z1, lw, t, BAR_H, 0, 0, S_FLATB, wall, line, 0, 1);    /* right of it */
    box(BAR_X, z1, door * 2, t, BAR_H - dh, dh, 0, S_FLAT, wall, line, 0, 0); /* over it */
    /* the ceiling with its stripes of light */
    {
        float a[3], b[3], c[3], d[3];
        P3(a, x0, BAR_H, z0);
        P3(b, x1, BAR_H, z0);
        P3(c, x1, BAR_H, z1);
        P3(d, x0, BAR_H, z1);
        int from = nfaces;
        quad(a, b, c, d, BAR_W, BAR_D, S_CEIL, 0x0A0414, 0, 0, 0);
        bound_faces(from);
    }
    /* outside: the sign over the door */
    panel(BAR_X + 4.5f, z1 + 0.17f, BAR_X - 4.5f, z1 + 0.17f, 2.9f, 4.0f, S_SIGN, 0x08040E, 0x40F0FF, 0);
    /* inside: the bar along the east wall, the shelf with bottles and the stools */
    box(x1 - 2.2f, BAR_Z - 0.5f, 0.9f, 7.0f, 1.1f, 0, 0, S_FLAT, 0x3A1A50, 0xFF40D0, 2, 1);
    panel(x1 - 0.17f, BAR_Z + 3.0f, x1 - 0.17f, BAR_Z - 3.5f, 1.2f, 2.6f, S_BOTTLES, 0, 0, 0);
    panel(x1 - 0.17f, BAR_Z + 1.5f, x1 - 0.17f, BAR_Z - 2.0f, 2.9f, 3.7f, S_SIGN, 0x08040E, 0, 3);
    for (int i = 0; i < 4; i++)
        box(x1 - 3.3f, BAR_Z - 3.2f + (float)i * 1.8f, 0.42f, 0.42f, 0.75f, 0, 0, S_FLAT, 0x2A2A3A,
            0x40F0FF, 2, 1);
    /* the arcade machines along the west wall */
    static const u32 mc[5] = {0xFF40D0, 0x40F0FF, 0xFFE45C, 0x60FF80, 0xFF8040};
    for (int i = 0; i < 5; i++) {
        float mz = BAR_Z - 4.0f + (float)i * 1.9f, mx = x0 + 0.75f;
        box(mx, mz, 0.9f, 0.85f, 1.95f, 0, 0, S_FLAT, 0x0E0818, mc[i], 2, 1);
        panel(mx + 0.46f, mz - 0.3f, mx + 0.46f, mz + 0.3f, 1.0f, 1.55f, S_SCREEN, 0, 0, i);
        panel(mx + 0.46f, mz - 0.4f, mx + 0.46f, mz + 0.4f, 1.65f, 1.9f, S_FLAT, mc[i], 0, 0);
    }
    /* "ARCADE" on the south wall inside, a pool table in the middle */
    panel(BAR_X + 3.5f, z0 + 0.17f, BAR_X - 3.5f, z0 + 0.17f, 2.6f, 3.8f, S_SIGN, 0x08040E, 0, 2);
    box(BAR_X - 0.5f, BAR_Z - 0.5f, 2.4f, 1.3f, 0.82f, 0, 0, S_FLAT, 0x3A1A10, 0x60FF80, 0, 1);
    {
        float a[3], b[3], c[3], d[3], X0 = BAR_X - 1.6f, X1 = BAR_X + 0.6f, Z0 = BAR_Z - 1.05f, Z1 = BAR_Z + 0.05f;
        P3(a, X0, 0.83f, Z0);
        P3(b, X1, 0.83f, Z0);
        P3(c, X1, 0.83f, Z1);
        P3(d, X0, 0.83f, Z1);
        int from = nfaces;
        quad(a, b, c, d, 2.2f, 1.1f, S_FLAT, 0x106838, 0x60FF80, 15, 0);
        bound_faces(from);
    }
    /* a jukebox in the north west corner, glowing */
    box(x0 + 0.7f, z1 - 0.8f, 0.9f, 0.7f, 1.6f, 0, 0, S_FLAT, 0x5A1A6A, 0xFFE45C, 2, 1);
    /* two cars in the car park, their noses to the bar */
    car(BAR_X - 7.8f, PK_Z0 + 2.6f, 0xFF40D0);
    car(BAR_X + 7.8f, PK_Z0 + 2.6f, 0x40F0FF);
    /* free again: the inside and the door (the walls marked the floor between them too) */
    mark_rect(BAR_X, z1, door * 2 - 0.1f, 1.0f, 0, 0);
}
/* the diner at the east end: a long window band, inside a counter with stools, booths at the
 * windows, a menu board and a jukebox */
static void build_diner(void) {
    float x0 = DN_X - DN_W / 2, x1 = DN_X + DN_W / 2, z0 = DN_Z - DN_D / 2, z1 = DN_Z + DN_D / 2, t = 0.3f;
    u32 wall = 0x1A1030, line = 0x40F0FF;
    float door = 1.1f, dh = 2.7f, lw = DN_W / 2 - door;
    box(DN_X, z1, DN_W, t, DN_H, 0, 0, S_DINER, wall, line, 0, 1);
    box(x0, DN_Z, t, DN_D, DN_H, 0, 0, S_DINER, wall, line, 0, 1);
    box(x1, DN_Z, t, DN_D, DN_H, 0, 0, S_DINER, wall, line, 0, 1);
    box(x0 + lw / 2, z0, lw, t, DN_H, 0, 0, S_DINER, wall, line, 0, 1);
    box(x1 - lw / 2, z0, lw, t, DN_H, 0, 0, S_DINER, wall, line, 0, 1);
    box(DN_X, z0, door * 2, t, DN_H - dh, dh, 0, S_FLAT, wall, line, 0, 0);
    panel(60, z0 - 0.1f, 72, z0 - 0.1f, 4.6f, 7.0f, S_SIGN, 0x08040E, 0x40F0FF, 1);
    {
        float a[3], b[3], c[3], d[3];
        P3(a, x0, DN_H, z0);
        P3(b, x1, DN_H, z0);
        P3(c, x1, DN_H, z1);
        P3(d, x0, DN_H, z1);
        int from = nfaces;
        quad(a, b, c, d, DN_W, DN_D, S_CEIL2, 0x18142A, 0, 0, 0);
        bound_faces(from);
    }
    /* the counter along the back with its stools, a coffee machine and a mixer on it */
    box(DN_X - 0.5f, z1 - 1.7f, 12, 0.7f, 1.05f, 0, 0, S_FLAT, 0xC8C8D8, 0xFF40D0, 2, 1);
    for (int i = 0; i < 7; i++) {
        float sx = DN_X - 6 + (float)i * 1.65f, sz = z1 - 2.55f;
        box(sx, sz, 0.12f, 0.12f, 0.65f, 0, 0, S_FLAT, 0xA0A0B0, 0xA0A0B0, 0, 0);
        box(sx, sz, 0.45f, 0.45f, 0.1f, 0.65f, 0, S_FLAT, 0xE02870, 0xFF80C0, 2, 0);
        mark_disc(sx, sz, 0.3f);
    }
    box(DN_X - 4.5f, z1 - 1.75f, 0.5f, 0.4f, 0.55f, 1.05f, 0, S_FLAT, 0x3A3A44, 0xC8C8D8, 2, 0);
    box(DN_X + 2.5f, z1 - 1.75f, 0.25f, 0.25f, 0.45f, 1.05f, 0, S_FLAT, 0x70E0F0, 0xC8C8D8, 2, 0);
    /* behind the counter: shelves and the menu board */
    panel(DN_X - 7, z1 - 0.17f, DN_X - 2, z1 - 0.17f, 1.3f, 2.3f, S_BOTTLES, 0, 0, 0);
    panel(DN_X - 1, z1 - 0.17f, DN_X + 5, z1 - 0.17f, 2.1f, 3.9f, S_MENU, 0, 0xFFE45C, 0);
    panel(x0 + 0.17f, DN_Z - 1.5f, x0 + 0.17f, DN_Z + 1.5f, 2.6f, 3.6f, S_SIGN, 0x08040E, 0, 8);
    /* booths at the windows, left and right of the door */
    static const float bx[4] = {-6.0f, -2.8f, 2.8f, 6.0f};
    for (int i = 0; i < 4; i++) {
        float cx = DN_X + bx[i], cz = z0 + 0.95f;
        box(cx, cz, 0.85f, 1.3f, 0.75f, 0, 0, S_FLAT, 0xE0E0F0, 0x40F0FF, 2, 1);
        for (int s = -1; s <= 1; s += 2) {
            box(cx + (float)s * 0.75f, cz, 0.5f, 1.3f, 0.45f, 0, 0, S_FLAT, 0xC02060, 0xFF80C0, 2, 1);
            box(cx + (float)s * 1.05f, cz, 0.15f, 1.3f, 1.15f, 0, 0, S_FLAT, 0xC02060, 0xFF80C0, 2, 1);
        }
    }
    /* the jukebox in the corner */
    box(x1 - 0.7f, z1 - 0.8f, 0.9f, 0.7f, 1.6f, 0, 0, S_FLAT, 0x5A1A6A, 0xFFE45C, 2, 1);
    mark_rect(DN_X, z0, door * 2 - 0.1f, 1.0f, 0, 0);
}
/* street lamps along the main street, every 22 metres on changing sides, with a pool of light */
static void build_lamps(void) {
    int side = 1, n = 0;
    float acc = 10;
    for (int seg = 1; seg < 9; seg++) {
        float ax = main_rd[seg][0], az = main_rd[seg][1], bx = main_rd[seg + 1][0], bz = main_rd[seg + 1][1];
        float dx = bx - ax, dz = bz - az, l = (float)sqrt(dx * dx + dz * dz);
        for (; acc < l; acc += 22, side = -side) {
            float nx = -dz / l * (float)side, nz = dx / l * (float)side;
            float x = ax + dx * acc / l + nx * 4.6f, z = az + dz * acc / l + nz * 4.6f;
            if (!natural(x, z) || !natural(x + nx, z + nz))
                continue;
            int from = nfaces;
            float hx = x - nx * 1.4f, hz = z - nz * 1.4f, rot = atan2f_(nz, nx);
            box(x, z, 0.16f, 0.16f, 5.3f, 0, 0, S_FLAT, 0x2A2A3A, 0x7070B0, 0, 1);
            box((x + hx) / 2, (z + hz) / 2, 1.5f, 0.1f, 0.1f, 5.1f, rot, S_FLAT, 0x2A2A3A, 0x7070B0, 2, 0);
            box(hx, hz, 0.6f, 0.3f, 0.16f, 4.95f, rot, S_FLAT, 0x2A2A3A, 0xFF80C0, 2, 0);
            {
                float c = (float)cos(rot), s = (float)sin(rot), a[3], b[3], cc[3], d[3];
                P3(a, hx - 0.28f * c + 0.13f * s, 4.94f, hz - 0.28f * s - 0.13f * c);
                P3(b, hx + 0.28f * c + 0.13f * s, 4.94f, hz + 0.28f * s - 0.13f * c);
                P3(cc, hx + 0.28f * c - 0.13f * s, 4.94f, hz + 0.28f * s + 0.13f * c);
                P3(d, hx - 0.28f * c - 0.13f * s, 4.94f, hz - 0.28f * s + 0.13f * c);
                quad(a, b, cc, d, 0.56f, 0.26f, S_FLAT, 0xFFF0F8, 0, 0, 0);
            }
            bound_faces(from);
            for (float gz = hz - 7; gz <= hz + 7; gz += RES)
                for (float gx = hx - 7; gx <= hx + 7; gx += RES) {
                    float d = (float)sqrt((gx - hx) * (gx - hx) + (gz - hz) * (gz - hz));
                    int c = cell(gx, gz);
                    if (d < 7 && c >= 0) {
                        int v = (int)(255 * (1 - d / 7) * (1 - d / 7));
                        if (v > glow[c])
                            glow[c] = (u8)v;
                    }
                }
            n++;
        }
        acc -= l;
    }
}
/* benches along the way to the lighthouse, facing it */
static void build_benches(void) {
    float acc = 7;
    int side = 1;
    for (int seg = 0; seg < 5; seg++) {
        float ax = south_rd[seg][0], az = south_rd[seg][1], bx = south_rd[seg + 1][0], bz = south_rd[seg + 1][1];
        float dx = bx - ax, dz = bz - az, l = (float)sqrt(dx * dx + dz * dz);
        for (; acc < l; acc += 10, side = -side) {
            float nx = -dz / l * (float)side, nz = dx / l * (float)side;
            float x = ax + dx * acc / l + nx * 3.1f, z = az + dz * acc / l + nz * 3.1f;
            if (!natural(x, z) || !natural(x + nx * 0.8f, z + nz * 0.8f))
                continue;
            float rot = atan2f_(dz, dx), y = hill_h(x, z);
            float bxk = x + nx * 0.25f, bzk = z + nz * 0.25f;
            box(x, z, 1.7f, 0.45f, 0.07f, y + 0.42f, rot, S_FLAT, 0x5A2A3A, 0xFF9A50, 2, 1);
            box(bxk, bzk, 1.7f, 0.06f, 0.42f, y + 0.55f, rot, S_FLAT, 0x5A2A3A, 0xFF9A50, 2, 0);
            for (int k = -1; k <= 1; k += 2) {
                float lx = x + (float)k * 0.7f * dx / l, lz = z + (float)k * 0.7f * dz / l;
                box(lx, lz, 0.08f, 0.4f, 0.42f, y, rot, S_FLAT, 0x2A2A34, 0x606070, 0, 0);
            }
        }
        acc -= l;
    }
}
/* a box inside the tower, in its own turned coordinates */
static void tbox(float lx, float lz, float w, float d, float h, float y0, int sh, u32 fill, u32 line, int top,
                 int mark) {
    float x, z;
    tower_world(lx, lz, &x, &z);
    box(x, z, w, d, h, y0, TW_R, sh, fill, line, top, mark);
}
static void tpanel(float lx0, float lz0, float lx1, float lz1, float y0, float y1, int sh, u32 fill, u32 line,
                   int id) {
    float x0, z0, x1, z1;
    tower_world(lx0, lz0, &x0, &z0);
    tower_world(lx1, lz1, &x1, &z1);
    panel(x0, z0, x1, z1, y0, y1, sh, fill, line, id);
}
/* the Violyx-Tower, turned a little towards the street, with the logo on two sides; on the
 * ground floor a computer shop with its door to the street */
static void build_tower(void) {
    float H = TW_S / 2, t = 0.3f, door = 1.3f;
    u32 line = 0x9A6AFF;
    tbox(0, 0, TW_S, TW_S, 48 - TW_G, TW_G, S_TOWER, 0x1A1040, line, 1, 0);
    tbox(0, 0, 0.5f, 0.5f, 9, 48, S_FLAT, 0x808090, 0xFF4040, 0, 0);
    /* the walls of the shop: glass to the outside */
    tbox(0, H - t / 2, TW_S, t, TW_G, 0, S_SHOPW, 0, line, 0, 1);
    tbox(-H + t / 2, 0, t, TW_S, TW_G, 0, S_SHOPW, 0, line, 0, 1);
    tbox(H - t / 2, 0, t, TW_S, TW_G, 0, S_SHOPW, 0, line, 0, 1);
    float lw = H - door;
    tbox(-H + lw / 2, -H + t / 2, lw, t, TW_G, 0, S_SHOPW, 0, line, 0, 1);
    tbox(H - lw / 2, -H + t / 2, lw, t, TW_G, 0, S_SHOPW, 0, line, 0, 1);
    tbox(0, -H + t / 2, door * 2, t, TW_G - 2.8f, 2.8f, S_FLAT, 0x1A1040, line, 0, 0);
    /* the sign over the door and the ceiling */
    tpanel(-5.5f, -H - 0.02f, 5.5f, -H - 0.02f, 3.55f, 4.4f, S_SIGN, 0x08040E, 0xC060FF, 5);
    {
        float a[3], b[3], c[3], d[3], x, z;
        tower_world(-H, -H, &x, &z);
        P3(a, x, TW_G - 0.05f, z);
        tower_world(H, -H, &x, &z);
        P3(b, x, TW_G - 0.05f, z);
        tower_world(H, H, &x, &z);
        P3(c, x, TW_G - 0.05f, z);
        tower_world(-H, H, &x, &z);
        P3(d, x, TW_G - 0.05f, z);
        int from = nfaces;
        quad(a, b, c, d, TW_S, TW_S, S_CEIL2, 0x18142A, 0, 0, 0);
        bound_faces(from);
    }
    /* inside: the counter with a computer, tables with computers along the left wall, shelves
     * with software on the right, the logo on the back wall */
    tbox(2.0f, 4.2f, 5.0f, 0.9f, 1.05f, 0, S_FLAT, 0x2A1A40, 0xC060FF, 2, 1);
    tbox(1.0f, 4.35f, 0.5f, 0.45f, 0.18f, 1.05f, S_FLAT, 0xD8D0B8, 0x808080, 2, 0);
    tbox(1.0f, 4.35f, 0.5f, 0.45f, 0.45f, 1.23f, S_FLAT, 0xD8D0B8, 0x808080, 2, 0);
    tpanel(1.22f, 4.585f, 0.78f, 4.585f, 1.27f, 1.64f, S_PC, 0, 0, 0); /* the screen faces the cashier */
    tbox(2.4f, 4.3f, 0.45f, 0.38f, 0.14f, 1.05f, S_FLAT, 0x2A2A34, 0x808090, 2, 0); /* the till */
    tbox(2.4f, 4.4f, 0.3f, 0.08f, 0.16f, 1.19f, S_FLAT, 0x2A2A34, 0x808090, 2, 0);
    tpanel(2.27f, 4.355f, 2.53f, 4.355f, 1.22f, 1.33f, S_FLAT, 0x30E070, 0, 0);
    for (int i = 0; i < 3; i++) {
        float lz = -3.5f + (float)i * 3.2f, lx = -H + 1.0f;
        tbox(lx, lz, 1.0f, 1.8f, 0.75f, 0, S_FLAT, 0x2A1A40, 0x40C8F0, 2, 1);
        tbox(lx - 0.15f, lz, 0.5f, 0.55f, 0.2f, 0.75f, S_FLAT, 0xD8D0B8, 0x808080, 2, 0);
        tbox(lx - 0.15f, lz, 0.5f, 0.5f, 0.48f, 0.95f, S_FLAT, 0xD8D0B8, 0x808080, 2, 0);
        tpanel(lx + 0.11f, lz - 0.21f, lx + 0.11f, lz + 0.21f, 1.0f, 1.39f, S_PC, 0, 0, i + 1);
    }
    tpanel(H - 0.32f, 4.5f, H - 0.32f, -4.0f, 0.2f, 2.4f, S_SOFT, 0, 0, 0);
    tpanel(H - 0.32f, 2.5f, H - 0.32f, -2.0f, 2.7f, 3.5f, S_SIGN, 0x08040E, 0, 6);
    tpanel(-4.5f, H - 0.32f, 4.5f, H - 0.32f, 2.0f, 3.8f, S_LOGO, 0, 0xC060FF, 0);
    {
        float x, z;
        tower_world(0, -H, &x, &z); /* the door is free */
        mark_rect(x, z, door * 2 - 0.1f, 1.0f, TW_R, 0);
    }
}
static void build_world(void) {
    build_ground();
    build_bar();
    build_tower();
    build_diner();
    /* small houses with pointed roofs */
    static const float hs[4][4] = {{-14, 14, 7, 0.10f}, {1, 15, 8.5f, -0.05f}, {16, 17, 6.5f, 0.15f}, {-2, 3, 6, 0.3f}};
    static const u32 hc[4] = {0xFF2FD0, 0x40F0FF, 0xFFE45C, 0xFF8040};
    for (int i = 0; i < 4; i++) {
        box(hs[i][0], hs[i][1], 8, 7, hs[i][2], 0, hs[i][3], S_WIN, 0x1C0C2C, hc[i], 0, 1);
        pyramid_roof(hs[i][0], hs[i][1], 8.6f, 7.6f, hs[i][2], 3.2f, hs[i][3], 0x2A0A30, hc[i]);
    }
    /* the lighthouse in the south */
    {
        int from = nfaces;
        float cx = 16, cz = -46;
        for (int i = 0; i < 8; i++) {
            float a0 = (float)i * 0.7854f, a1 = a0 + 0.7854f, A[3], B[3], C[3], D[3];
            P3(A, cx + 2.6f * (float)cos(a0), 0, cz + 2.6f * (float)sin(a0));
            P3(B, cx + 2.6f * (float)cos(a1), 0, cz + 2.6f * (float)sin(a1));
            P3(C, cx + 1.6f * (float)cos(a1), 18, cz + 1.6f * (float)sin(a1));
            P3(D, cx + 1.6f * (float)cos(a0), 18, cz + 1.6f * (float)sin(a0));
            quad(A, B, C, D, 2.0f, 18, S_STRIPE, 0, 0xB060FF, 2, 0);
            P3(A, cx + 1.9f * (float)cos(a0), 18, cz + 1.9f * (float)sin(a0));
            P3(B, cx + 1.9f * (float)cos(a1), 18, cz + 1.9f * (float)sin(a1));
            P3(C, cx + 1.9f * (float)cos(a1), 20.5f, cz + 1.9f * (float)sin(a1));
            P3(D, cx + 1.9f * (float)cos(a0), 20.5f, cz + 1.9f * (float)sin(a0));
            quad(A, B, C, D, a0, 2.5f, S_LAMP, 0, 0xFFE45C, 15, 0);
            faces[nfaces - 1].uv[0][0] = faces[nfaces - 1].uv[3][0] = a0; /* u: the direction */
            faces[nfaces - 1].uv[1][0] = faces[nfaces - 1].uv[2][0] = a1;
            float T[3];
            P3(T, cx, 23, cz);
            P3(A, cx + 2.1f * (float)cos(a0), 20.5f, cz + 2.1f * (float)sin(a0));
            P3(B, cx + 2.1f * (float)cos(a1), 20.5f, cz + 2.1f * (float)sin(a1));
            tri(A, B, T, 0x2A0A40, 0xB060FF, 7);
        }
        bound_faces(from);
        mark_disc(cx, cz, 2.9f);
    }
    /* the basketball court: two baskets */
    for (int s = -1; s <= 1; s += 2) {
        float bx = -50 + (float)s * 13.4f;
        box(bx + (float)s * 0.9f, -2, 0.2f, 0.2f, 3.4f, 0, 0, S_FLAT, 0x9090A0, 0xE0E0FF, 0, 1);
        if (s < 0)
            panel(bx, -2.9f, bx, -1.1f, 2.9f, 3.95f, S_BOARD, 0, 0xE0E0FF, 0);
        else
            panel(bx, -1.1f, bx, -2.9f, 2.9f, 3.95f, S_BOARD, 0, 0xE0E0FF, 0);
        box(bx + (float)s * 0.45f, -2, 0.9f, 0.12f, 0.12f, 3.3f, 0, S_FLAT, 0x9090A0, 0xE0E0FF, 2, 0);
        box(bx - (float)s * 0.3f, -2, 0.45f, 0.45f, 0.04f, 3.05f, 0, S_FLAT, 0xFF6020, 0xFF6020, 0, 0);
    }
    /* the hills, flat towards everything that was built; then lamps and benches */
    build_hfac();
    for (int i = 0; i < NHILLS; i++)
        hill_mesh(i);
    build_lamps();
    build_benches();
    /* palms: along the main street, on the beach, around the bar and the diner */
    u32 rs = 4242;
    int tries = 0;
    for (int seg = 0; seg < 9 && npalms < MAXP; seg++) {
        float ax = main_rd[seg][0], az = main_rd[seg][1], bx = main_rd[seg + 1][0], bz = main_rd[seg + 1][1];
        float dx = bx - ax, dz = bz - az, l = (float)sqrt(dx * dx + dz * dz);
        for (float t = 6; t < l && npalms < MAXP; t += 13) {
            for (int side = -1; side <= 1 && npalms < MAXP; side += 2) {
                float x = ax + dx * t / l - dz / l * 7.0f * (float)side, z = az + dz * t / l + dx / l * 7.0f * (float)side;
                palms[npalms].x = x;
                palms[npalms].z = z;
                palms[npalms].id = npalms * 7 + 3;
                npalms++;
            }
        }
    }
    while (npalms < 95 && tries++ < 20000) {
        float x = GX0 + (float)(rnd(&rs) % 28000) / 100, z = GZ0 + (float)(rnd(&rs) % 16000) / 100;
        int k = kind_at(x, z);
        if (k != T_BEACH && !(k == T_LAND && rnd(&rs) % 6 == 0))
            continue;
        palms[npalms].x = x;
        palms[npalms].z = z;
        palms[npalms].id = npalms * 7 + 3;
        npalms++;
    }
    /* keep only palms on free ground, away from roads, buildings and each other */
    int m = 0;
    for (int i = 0; i < npalms; i++) {
        float x = palms[i].x, z = palms[i].z;
        int ok = kind_at(x, z) == T_LAND || kind_at(x, z) == T_BEACH;
        for (float a = 0; a < 6.28f && ok; a += 0.785f)
            for (float r = 1; r <= 3; r += 1) {
                int k = kind_at(x + r * (float)cos(a), z + r * (float)sin(a)), c = cell(x + r * (float)cos(a), z + r * (float)sin(a));
                if ((k != T_LAND && k != T_BEACH && k != T_WATER) || (c >= 0 && solid[c]))
                    ok = 0;
            }
        for (int j = 0; j < m && ok; j++)
            if ((palms[j].x - x) * (palms[j].x - x) + (palms[j].z - z) * (palms[j].z - z) < 25)
                ok = 0;
        if (ok)
            palms[m++] = palms[i];
    }
    npalms = m;
    for (int i = 0; i < npalms; i++) {
        mark_disc(palms[i].x, palms[i].z, 0.5f);
        palms[i].y = hill_h(palms[i].x, palms[i].z);
    }
    /* stars */
    u32 ss = 99;
    for (int i = 0; i < NSTARS; i++) {
        star_a[i] = (float)(rnd(&ss) % 6283) / 1000;
        star_e[i] = 0.06f + (float)(rnd(&ss) % 1000) / 1000 * 0.9f;
        star_p[i] = (int)(rnd(&ss) % 3000);
    }
}

/* ---------- walking ---------- */
static int free_at(float x, float z) {
    int c = cell(x, z);
    return c >= 0 && ter[c] != T_WATER && !solid[c];
}
static int can_stand(float x, float z) {
    if (!free_at(x, z))
        return 0;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.785f;
        if (!free_at(x + 0.32f * (float)cos(a), z + 0.32f * (float)sin(a)))
            return 0;
    }
    return 1;
}
static const char *place(void) {
    int en = vx_english();
    if (fabs(camx - BAR_X) < BAR_W / 2 && fabs(camz - BAR_Z) < BAR_D / 2)
        return "Arcade Bar";
    if (fabs(camx - DN_X) < DN_W / 2 && fabs(camz - DN_Z) < DN_D / 2)
        return "Diner";
    {
        float lx, lz;
        tower_local(camx, camz, &lx, &lz);
        if (fabs(lx) < TW_S / 2 && fabs(lz) < TW_S / 2)
            return "Violyx Computer";
    }
    struct {
        float x, z, r;
        const char *de, *en;
    } pl[] = {{-36, 37, 22, "Violyx-Tower", "Violyx Tower"},
              {-50, -2, 16, "Basketballplatz", "Basketball court"},
              {66, 54, 16, "Diner", "Diner"},
              {16, -46, 14, "Leuchtturm", "Lighthouse"},
              {-28, -20, 13, "Huegel", "Hill"},
              {36, 8, 16, "Huegel", "Hill"},
              {-5, -22, 9, "Huegel", "Hill"},
              {16, -5, 9, "Huegel", "Hill"},
              {-50, -24, 9, "Huegel", "Hill"},
              {-102, -30, 11, "Huegel", "Hill"},
              {(PK_X0 + PK_X1) / 2, (PK_Z0 + PK_Z1) / 2, 11, "Parkplatz", "Car park"},
              {BAR_X, BAR_Z, 16, "Vor der Arcade Bar", "Outside the Arcade Bar"},
              {0, 14, 20, "Siedlung", "Houses"}};
    for (unsigned i = 0; i < sizeof pl / sizeof pl[0]; i++)
        if ((camx - pl[i].x) * (camx - pl[i].x) + (camz - pl[i].z) * (camz - pl[i].z) < pl[i].r * pl[i].r)
            return en ? pl[i].en : pl[i].de;
    int k = kind_at(camx, camz);
    if (k == T_BEACH)
        return en ? "Beach" : "Strand";
    if (k == T_ROAD || k == T_EDGE || k == T_DASH)
        return en ? "Main street" : "Hauptstrasse";
    if (k == T_DIRT || k == T_DIRTE)
        return en ? "Dirt road" : "Feldweg";
    return "Vio-Island";
}

/* ---------- the music: a small synthesizer that plays three synthwave pieces ---------- */
/* Five pieces. Each has two parts with their own chords, melody, arpeggio and drums, and is
 * played as intro, A, A with melody, B, a break without drums, A again and an outro (44 bars);
 * then comes another piece, chosen by chance. Chords: root as MIDI note, 1 = major. Melodies in
 * eighths over four bars, 0 rest, 1 hold. */
typedef struct {
    const char *name;
    int bpm, rootA[4], majA[4], rootB[4], majB[4];
    signed char melA[32], melB[32], arpA[16], arpB[16];
    const char *kickA, *snareA, *hatA, *kickB, *snareB, *hatB;
    int bass; /* 0 eighths with octaves, 1 rolling sixteenths, 2 syncopated */
    int lead; /* 0 saw, 1 square, 2 soft */
} piece_t;
#define NPIECES 5
static const piece_t pieces[NPIECES] = {
    {"Neon Drive", 104, {57, 53, 60, 55}, {0, 1, 1, 1}, {50, 53, 48, 55}, {0, 1, 1, 1},
     {76, 0, 74, 76, 1, 0, 69, 1, 72, 1, 0, 69, 72, 1, 74, 1, 76, 1, 79, 1, 76, 1, 74, 72, 74, 1, 1, 1, 71, 1, 74, 1},
     {69, 1, 72, 1, 74, 1, 76, 1, 77, 1, 1, 76, 74, 1, 1, 1, 76, 1, 72, 1, 67, 1, 72, 1, 74, 1, 1, 1, 71, 1, 1, 1},
     {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1, 2, 3}, {0, 2, 4, 2, 1, 3, 4, 3, 0, 2, 4, 2, 1, 3, 4, 3},
     "x...x...x...x...", "....x.......x...", "..x...x...x...xx", "x...x...x...x.x.", "....x.......x..x",
     "x.xxx.xxx.xxx.xx", 0, 0},
    {"Sunset Boulevard", 92, {53, 49, 56, 51}, {0, 1, 1, 1}, {46, 49, 53, 51}, {0, 1, 0, 1},
     {72, 1, 68, 1, 65, 1, 68, 72, 73, 1, 1, 72, 68, 1, 65, 1, 75, 1, 72, 1, 68, 1, 72, 1, 70, 1, 1, 1, 67, 1, 70, 1},
     {77, 1, 75, 73, 1, 1, 72, 1, 73, 1, 77, 1, 80, 1, 1, 1, 68, 1, 72, 1, 77, 1, 75, 1, 75, 1, 1, 1, 67, 1, 70, 1},
     {0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 4, 3, 2, 1}, {4, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 3},
     "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "x.....x.x.......", "....x.......x...",
     "xxxxxxxxxxxxxxxx", 2, 2},
    {"Midnight Grid", 118, {50, 46, 53, 48}, {0, 1, 1, 1}, {55, 46, 50, 48}, {0, 1, 0, 1},
     {69, 1, 74, 1, 77, 1, 76, 74, 74, 1, 1, 72, 70, 1, 69, 1, 69, 1, 72, 1, 77, 1, 81, 1, 79, 1, 1, 1, 76, 1, 72, 1},
     {79, 1, 77, 1, 74, 1, 77, 79, 77, 1, 74, 1, 70, 1, 74, 1, 81, 1, 79, 77, 1, 1, 74, 1, 76, 1, 1, 1, 72, 1, 76, 1},
     {0, 1, 2, 0, 1, 2, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3}, {0, 0, 2, 2, 1, 1, 3, 3, 2, 2, 4, 4, 3, 3, 1, 1},
     "x...x...x...x.x.", "....x.......x..x", ".xxx.xxx.xxx.xxx", "x..x..x...x..x..", "....x.......x...",
     "x.x.x.x.x.x.x.xx", 1, 1},
    {"Palm Highway", 110, {52, 48, 55, 50}, {0, 1, 1, 1}, {48, 50, 52, 52}, {1, 1, 0, 0},
     {71, 1, 76, 1, 79, 1, 78, 76, 76, 1, 1, 74, 72, 1, 71, 1, 71, 1, 74, 1, 79, 1, 83, 1, 81, 1, 1, 1, 78, 1, 74, 1},
     {76, 1, 79, 1, 81, 1, 79, 76, 78, 1, 81, 1, 74, 1, 78, 1, 79, 1, 1, 1, 76, 1, 71, 1, 76, 1, 1, 1, 1, 1, 0, 0},
     {0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1}, {2, 1, 0, 1, 3, 2, 1, 2, 4, 3, 2, 3, 2, 1, 0, 1},
     "x...x...x...x...", "....x.......x...", "x.xxx.xxx.xxx.xx", "x...x...x...x...", "....x..x....x...",
     "..x...x...x...x.", 0, 0},
    {"Arcade Dreams", 96, {48, 44, 51, 46}, {0, 1, 1, 1}, {53, 56, 48, 46}, {0, 1, 0, 1},
     {79, 1, 75, 1, 72, 1, 75, 79, 80, 1, 1, 79, 75, 1, 72, 1, 82, 1, 79, 1, 75, 1, 79, 1, 77, 1, 1, 1, 74, 1, 70, 1},
     {72, 1, 75, 1, 77, 1, 80, 1, 79, 1, 1, 1, 75, 1, 72, 1, 75, 1, 72, 1, 67, 1, 72, 1, 74, 1, 1, 1, 70, 1, 1, 1},
     {0, 2, 4, 2, 0, 2, 4, 2, 1, 3, 4, 3, 1, 3, 4, 3}, {4, 2, 0, 2, 4, 2, 0, 2, 3, 1, 0, 1, 3, 1, 0, 1},
     "x......xx.......", "....x.......x...", "x.x.x.x.x.x.x.x.", "x.x.....x.x.....", "....x.......x.x.",
     "xx.xxx.xxx.xxx.x", 2, 1}};
#define RATE 48000
#define BARS 44
static int music_on = 1, piece, mstep, mbar;
static long mpos, step_len;
static u32 noise = 1, mseed = 7;
static float bass_ph, bass_f, bass_env, bass_lp;
static float arp_ph, arp_f, arp_env;
static float lead_ph, lead_f, lead_env, lead_vib;
static int lead_gate, lead_wave;
static float pad_ph[6], pad_f[6], pad_lp, pad_amp, pad_cut = 0.005f;
static float kick_ph, kick_env, snare_env, snare_amt = 1, hat_env;
static float hat_hp;
#define DL 36000
static float dl_l[DL], dl_r[DL];
static int dl_p, dl_len;
static float midi_hz(int n) {
    return 440.0f * (float)pow(2.0, (n - 69) / 12.0);
}
static int chord_tone(int root, int major, int i) { /* i: 0..4 through the chord upwards */
    static const int mj[3] = {0, 4, 7}, mn[3] = {0, 3, 7};
    const int *t = major ? mj : mn;
    return root + t[i % 3] + 12 * (i / 3);
}
/* sections: 0 intro, 1 A, 2 A with melody, 3 B, 4 break, 5 A again, 6 outro */
static int section(int bar) {
    return bar < 4 ? 0 : bar < 12 ? 1 : bar < 20 ? 2 : bar < 28 ? 3 : bar < 32 ? 4 : bar < 40 ? 5 : 6;
}
static void music_step(void) {
    const piece_t *p = &pieces[piece];
    int ch = mbar % 4, s = mstep, sec = section(mbar), B = sec == 3;
    int root = B ? p->rootB[ch] : p->rootA[ch], maj = B ? p->majB[ch] : p->majA[ch];
    const char *kick = B ? p->kickB : p->kickA, *snare = B ? p->snareB : p->snareA, *hat = B ? p->hatB : p->hatA;
    int last = mbar == 11 || mbar == 19 || mbar == 27 || mbar == 39; /* a fill at the end */
    if (s == 0)
        for (int v = 0; v < 6; v++)
            pad_f[v] = midi_hz(chord_tone(root, maj, v / 2) + 12) * (v & 1 ? 1.004f : 0.996f);
    pad_cut = sec == 0 ? 0.004f + 0.004f * (float)(mbar * 16 + s) / 64 : sec == 4 ? 0.012f : 0.02f;
    /* drums */
    int drums = sec != 4 && !(sec == 0 && mbar < 2);
    if (drums && kick[s] == 'x' && !(last && s >= 12))
        kick_env = 1, kick_ph = 0;
    if (drums && sec != 0 && (snare[s] == 'x' || (last && s >= 12))) {
        snare_env = 1;
        snare_amt = last && s >= 12 ? 0.55f + 0.15f * (float)(s - 12) : 1;
    }
    if (mbar == 31 && s >= 8) { /* the end of the break: a roll that rises */
        snare_env = 1;
        snare_amt = 0.2f + 0.1f * (float)(s - 8);
    }
    if (drums && (sec >= 2 || mbar >= 8) && sec != 6 && hat[s] == 'x')
        hat_env = 1;
    /* bass */
    int bass = sec >= 1 && sec != 4;
    if (sec == 0 && mbar >= 2)
        bass = 1;
    if (sec == 4 && s == 0) { /* in the break only a long low note */
        bass_f = midi_hz(root - 12);
        bass_env = 0.7f;
    }
    if (bass) {
        int on = 0, oct = 0;
        if (p->bass == 0)
            on = s % 2 == 0, oct = s % 4 == 2;
        else if (p->bass == 1)
            on = 1, oct = s % 8 == 6;
        else
            on = s == 0 || s == 3 || s == 6 || s == 10 || s == 13, oct = s == 10;
        if (on) {
            bass_f = midi_hz(root - 12 + (oct ? 12 : 0));
            bass_env = p->bass == 1 && s % 4 ? 0.65f : 1;
        }
    }
    /* arpeggio and melody */
    if (sec >= 1 && sec != 6) {
        const signed char *ap = sec == 3 || sec == 5 ? p->arpB : p->arpA;
        arp_f = midi_hz(chord_tone(root, maj, ap[s]) + 12);
        arp_env = sec == 4 ? 0.8f : 1;
    }
    if ((sec == 2 || sec == 3 || sec == 5) && s % 2 == 0) {
        int m = (sec == 3 ? p->melB : p->melA)[ch * 8 + s / 2];
        lead_wave = sec == 5 ? (p->lead + 1) % 3 : p->lead; /* the last time with another sound */
        if (m > 1) {
            lead_f = midi_hz(m - (sec == 5 && lead_wave == 2 ? 12 : 0));
            lead_env = 1;
            lead_gate = 1;
        } else if (m == 0)
            lead_gate = 0; /* let the note fade instead of a click */
    } else if (sec == 4 || sec == 6 || sec <= 1)
        lead_gate = 0;
}
static void next_piece(void) {
    int n = piece;
    while (n == piece) {
        mseed = mseed * 1103515245u + 12345u;
        n = (int)((mseed >> 16) % NPIECES);
    }
    piece = n;
    step_len = (long)RATE * 60 / pieces[piece].bpm / 4;
    dl_len = (int)(step_len * 3);
    if (dl_len >= DL)
        dl_len = DL - 1;
}
/* band-limited steps for the saw and square waves, so they do not hiss */
static float blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt) {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}
static float saw(float t, float dt) {
    return 2 * t - 1 - blep(t, dt);
}
static float square(float t, float dt) {
    float t2 = t + 0.5f;
    if (t2 >= 1)
        t2 -= 1;
    return (t < 0.5f ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
}
static float arp_lp, lead_lp, sn_tone;
static void music_gen(short *out, int n) {
    for (int i = 0; i < n; i++) {
        if (mpos % step_len == 0) {
            music_step();
            if (++mstep == 16) {
                mstep = 0;
                if (++mbar == BARS) { /* the next piece */
                    mbar = 0;
                    next_piece();
                }
            }
        }
        mpos++;
        noise = noise * 1664525u + 1013904223u;
        float nz = (float)(int)(noise >> 9 & 0x7FFF) / 16384.0f - 1;
        /* kick: a sine that falls quickly in pitch */
        float k = 0;
        if (kick_env > 0.0005f) {
            kick_ph += (48 + 130 * kick_env * kick_env) / RATE;
            if (kick_ph >= 1)
                kick_ph -= 1;
            k = (float)sin(kick_ph * 6.2832f) * kick_env;
            kick_env *= 0.99978f;
        }
        float duck = 1 - 0.55f * kick_env; /* pads and bass make room for the kick */
        /* snare: noise and a short tone; hats: high noise */
        sn_tone += 185.0f / RATE;
        if (sn_tone >= 1)
            sn_tone -= 1;
        float sn_ = (nz * 0.6f + (float)sin(sn_tone * 6.2832f) * 0.5f * snare_env) * snare_env * snare_amt;
        snare_env *= 0.99962f;
        hat_hp += 0.55f * (nz - hat_hp);
        float ht = (nz - hat_hp) * hat_env;
        hat_env *= 0.9986f;
        /* bass: a saw through a filter that closes after each note */
        float bdt = bass_f / RATE;
        bass_ph += bdt;
        if (bass_ph >= 1)
            bass_ph -= 1;
        float cut = 0.015f + 0.10f * bass_env;
        bass_lp += cut * (saw(bass_ph, bdt) - bass_lp);
        float bs = bass_lp * (0.45f + 0.55f * bass_env) * duck;
        bass_env *= 0.99988f;
        /* pads: six detuned saws, soft and wide */
        float pl = 0, pr = 0;
        for (int v = 0; v < 6; v++) {
            float dt = pad_f[v] / RATE;
            pad_ph[v] += dt;
            if (pad_ph[v] >= 1)
                pad_ph[v] -= 1;
            float w = saw(pad_ph[v], dt);
            if (v & 1)
                pr += w;
            else
                pl += w;
        }
        pad_lp += pad_cut * ((pl + pr) - pad_lp);
        static float pad_side;
        pad_side += pad_cut * ((pl - pr) - pad_side);
        if (pad_amp < 1)
            pad_amp += 1.0f / 24000;
        /* arpeggio: a soft square, short */
        float adt = arp_f / RATE;
        arp_ph += adt;
        if (arp_ph >= 1)
            arp_ph -= 1;
        arp_lp += 0.22f * (square(arp_ph, adt) - arp_lp);
        float ar = arp_lp * arp_env;
        arp_env *= 0.99965f;
        /* melody: a saw with vibrato that comes in late */
        lead_vib += 5.2f / RATE;
        float ldt = lead_f * (1 + 0.005f * (float)sin(lead_vib * 6.2832f)) / RATE;
        lead_ph += ldt;
        if (lead_ph >= 1)
            lead_ph -= 1;
        if (lead_wave == 0)
            lead_lp += 0.18f * (saw(lead_ph, ldt) - lead_lp);
        else if (lead_wave == 1)
            lead_lp += 0.16f * (square(lead_ph, ldt) * 0.8f - lead_lp);
        else
            lead_lp += 0.06f * (square(lead_ph, ldt) * 1.6f - lead_lp); /* soft, almost a flute */
        float ld = lead_lp * lead_env;
        lead_env *= lead_gate ? 0.99998f : 0.9992f;
        /* echo on arpeggio and melody, the left and right sides crossed */
        float send = ar * 0.10f + ld * 0.09f;
        int rp = (dl_p + DL - dl_len) % DL;
        float el = dl_l[rp], er = dl_r[rp];
        dl_l[dl_p] = send + er * 0.42f;
        dl_r[dl_p] = el * 0.42f;
        dl_p = (dl_p + 1) % DL;
        float pg = 0.02f / pad_cut; /* a closed filter is quieter: make up for it */
        if (pg > 2.5f)
            pg = 2.5f;
        float pad = pad_lp * 0.045f * pg * pad_amp * duck, side = pad_side * 0.03f * pg * pad_amp * duck;
        float dry = k * 0.85f + sn_ * 0.42f + ht * 0.09f + bs * 0.42f + pad + ar * 0.10f + ld * 0.15f;
        float L = (dry + side + el * 0.7f) * 1.25f, R = (dry - side + er * 0.7f) * 1.25f;
        L = L / (1 + fabs(L));
        R = R / (1 + fabs(R));
        out[2 * i] = (short)(L * 30000);
        out[2 * i + 1] = (short)(R * 30000);
    }
}
static short mbuf[4800 * 2];
static void music_feed(void) {
    if (!music_on)
        return;
    long room = vx_sys(22, 0, 0, 0);
    if (room <= 0)
        return;
    if (room > 4800)
        room = 4800;
    music_gen(mbuf, (int)room);
    vx_sys(22, (long)mbuf, room, 0);
}
static void music_toggle(void) {
    music_on = !music_on;
    char c = music_on ? '1' : '0';
    vx_write("music", &c, 1);
    if (!music_on)
        vx_sys(23, 0, 0, 0); /* stop what is queued */
}

/* ---------- the Info window and the licence ---------- */
static int overlay, lic_top; /* 1 Info, 2 licence */
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
    0};
#define NLIC 32
static int lic_max(int rows) {
    int h = rows - 4 < NLIC ? rows - 4 : NLIC; /* lines that fit */
    return NLIC - h > 0 ? NLIC - h : 0;
}
static void box_at(int x, int y, int w, int h, const char *title) {
    uint8_t bg = VX_ATTR(VC_PANEL, VC_LAVENDER), bar = VX_ATTR(VC_VIOLET, VC_GLOW);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            vx_put(x + i, y + j, ' ', j ? bg : bar);
    vx_text(x + 2, y, title, bar);
}
static void draw_info(int cols, int rows, int mode) {
    int en = vx_english();
    uint8_t bg = VX_ATTR(VC_PANEL, VC_LAVENDER), hot = VX_ATTR(VC_PANEL, VC_HOT),
            dim = VX_ATTR(VC_PANEL, VC_SOFT), btn = VX_ATTR(VC_SEL, VC_GLOW);
    if (mode == 2) { /* the licence, as large as the window allows */
        int w = cols - 4 < 74 ? cols - 4 : 74, h = rows - 2 < NLIC + 2 ? rows - 2 : NLIC + 2;
        int x = (cols - w) / 2, y = (rows - h) / 2;
        box_at(x, y, w, h,
               lic_max(rows) ? (en ? "Licence (Up/Down scroll, other key: back)" : "Lizenz (Hoch/Runter, andere Taste: zurueck)")
                             : (en ? "Licence (any key: back)" : "Lizenz (Taste: zurueck)"));
        for (int i = 0; i + 1 < h && lic[lic_top + i]; i++) {
            char line[80];
            int k = 0;
            for (const char *p = lic[lic_top + i]; *p && k < w - 4 && k < 79; p++)
                line[k++] = *p;
            line[k] = 0;
            vx_text(x + 2, y + 1 + i, line, lic_top + i ? bg : hot);
        }
        return;
    }
    int w = cols - 4 < 62 ? cols - 4 : 62, h = 14, x = (cols - w) / 2, y = (rows - h) / 2;
    if (y < 0)
        y = 0;
    box_at(x, y, w, h, "Info - Vio-Island");
    vx_text(x + 2, y + 2, "VIO-ISLAND", hot);
    vx_text(x + 2, y + 4, en ? "A small island in the synthwave look of Violyx." : "Eine kleine Insel im Synthwave-Look von Violyx.", bg);
    vx_text(x + 2, y + 5, en ? "Visit the Arcade Bar, the Diner and Violyx Computer." : "Besuche die Arcade Bar, das Diner und Violyx Computer.", bg);
    vx_text(x + 2, y + 6, en ? "The music is made by the program while it plays." : "Die Musik erzeugt das Programm, waehrend es spielt.", bg);
    vx_text(x + 2, y + 8, "Version 1.0", dim);
    vx_text(x + 2, y + 9, "(c) 2026 Merecete", dim);
    vx_text(x + 2, y + 10, en ? "Released under the BSD 3-Clause License." : "Veroeffentlicht unter der BSD 3-Clause License.", dim);
    vx_text(x + 2, y + 12, en ? " L Licence " : " L Lizenz ", btn);
    const char *c = en ? " Other key: close " : " Andere Taste: schliessen ";
    vx_text(x + w - 2 - (int)strlen(c), y + 12, c, btn);
}

/* ---------- the main loop ---------- */
int main(void) {
    int en = vx_english();
    char c = 0;
    if (vx_read("music", &c, 1) == 1)
        music_on = c != '0';
    mseed = (u32)vx_ms() | 1; /* a different piece first each time */
    piece = -1;
    next_piece();
    img = vx_alloc(960 * 800);
    zb = vx_alloc(960 * 800 * sizeof(float));
    ter = vx_alloc(GW * GH);
    solid = vx_alloc(GW * GH);
    faces = vx_alloc(MAXF * sizeof(face_t));
    rdist = vx_alloc(GW * GH * 2);
    ralong = vx_alloc(GW * GH * 2);
    rdist1 = vx_alloc(GW * GH * 2);
    ralong1 = vx_alloc(GW * GH * 2);
    rway = vx_alloc(GW * GH);
    glow = vx_alloc(GW * GH);
    cdist = vx_alloc(GW * GH * 2);
    if (!img || !zb || !ter || !solid || !faces || !rdist || !ralong || !rdist1 || !ralong1 || !rway || !glow || !cdist) {
        vx_message("Vio-Island", en ? "Not enough memory." : "Nicht genug Speicher.");
        return 0;
    }
    memset(glow, 0, GW * GH);
    vx_clear(VX_ATTR(VC_BLACK, VC_LAVENDER));
    vx_text(2, 1, en ? "Vio-Island is being built ..." : "Vio-Island wird gebaut ...", VX_ATTR(VC_BLACK, VC_LAVENDER));
    vx_show();
    build_world();
    vx_menu(en ? "Info|Music on/off (M)|Full screen (F)" : "Info|Musik an/aus (M)|Vollbild (F)");
    if (!vx_sys(23, 13, 0, 0))
        vx_sys(23, 13, 1, 0); /* the whole screen for the island */
    int full = 0, show_fps = 0, frames = 0, fps = 0;
    long fps_t = vx_ms();
    camx = -83;
    camz = 4;
    yaw = 1.19f;
    float bob = 0;
    long last = vx_ms(), t0 = last;
    for (;;) {
        long t = vx_ms();
        float dt = (float)(t - last) / 1000;
        last = t;
        if (dt > 0.1f)
            dt = 0.1f;
        now = (float)(t - t0) / 1000;
        int key;
        while ((key = vx_key()) != 0)
            if (overlay == 2) { /* the licence: Up and Down scroll, other keys go back */
                if (key == VK_UP && lic_top > 0)
                    lic_top--;
                else if (key == VK_DOWN && lic_top < lic_max(vx_rows()))
                    lic_top++;
                else if (key != VK_UP && key != VK_DOWN)
                    overlay = 1;
            } else if (overlay == 1)
                overlay = key == 'l' || key == 'L' ? 2 : 0, lic_top = 0;
            else if (key == VK_MENU)
                overlay = 1;
            else if (key == 'm' || key == 'M' || key == VK_MENU + 1)
                music_toggle();
            else if (key == 'i' || key == 'I')
                show_fps = !show_fps; /* frames per second, for testing */
            else if (key == 'f' || key == 'F' || key == VK_MENU + 2) {
                full = !full;
                vx_sys(23, 11, full, 0); /* without frame, menu bar and status line */
            }
        /* arrows: walk and turn, Shift runs */
        int run = vx_held(0x2A) || vx_held(0x36);
        float turn = 0, go = 0;
        if (!overlay) {
        if (vx_held(0x4B))
            turn -= 1;
        if (vx_held(0x4D))
            turn += 1;
        if (vx_held(0x48))
            go += 1;
        if (vx_held(0x50))
            go -= 0.6f;
        }
        yaw = wrap(yaw + turn * 1.9f * dt);
        cs = (float)cos(yaw);
        sn = (float)sin(yaw);
        if (go != 0) {
            float v = go * (run ? 11.0f : 5.0f) * dt, nx = camx + sn * v, nz = camz + cs * v;
            if (can_stand(nx, nz))
                camx = nx, camz = nz;
            else if (can_stand(nx, camz)) /* slide along walls */
                camx = nx;
            else if (can_stand(camx, nz))
                camz = nz;
            bob += dt * (run ? 13 : 9);
        }
        eye = EYE + hill_h(camx, camz) + 0.045f * (float)sin(bob);
        /* the picture: the window may change its size (F5) */
        int cols = vx_cols(), rows = vx_rows() - 1;
        if (cols < 4 || rows < 2) { /* a window too small to show anything */
            vx_show();
            music_feed();
            vx_wait(50);
            continue;
        }
        if (cols > 240)
            cols = 240;
        if (rows > 100)
            rows = 100;
        PW = cols * 4;
        PH = rows * 8;
        HY = PH * 56 / 100;
        CX = PW / 2;
        FOC = (float)PW * 0.62f;
        sky();
        floor_rows();
        for (int i = 0; i < nfaces; i++)
            draw_face(&faces[i]);
        draw_palms();
        vx_image(img, PW, PH);
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++)
                vx_pixcell(x, y, x, y);
        /* the line at the bottom */
        uint8_t hud = VX_ATTR(VC_BLACK, VC_LAVENDER), hot = VX_ATTR(VC_BLACK, VC_HOT);
        for (int x = 0; x < cols; x++)
            vx_put(x, rows, ' ', hud);
        vx_text(1, rows, place(), hot);
        const char *help = en ? "Arrows walk  Shift runs  M music  F full screen  Esc ends"
                              : "Pfeile laufen  Shift rennt  M Musik  F Vollbild  Esc beendet";
        int hl = (int)strlen(help);
        if (cols > hl + 24)
            vx_text(cols - hl - 1, rows, help, hud);
        if (music_on && cols > hl + 44) {
            vx_text(cols - hl - 22, rows, pieces[piece].name, VX_ATTR(VC_BLACK, VC_DIM));
        }
        frames++;
        if (t - fps_t >= 1000) {
            fps = frames;
            frames = 0;
            fps_t = t;
        }
        if (show_fps) {
            char fb[24] = "fps ";
            int v = fps, k = 4;
            if (v >= 10)
                fb[k++] = (char)('0' + v / 10 % 10);
            fb[k++] = (char)('0' + v % 10);
            fb[k] = 0;
            vx_text(cols / 2 - 3, rows, fb, VX_ATTR(VC_BLACK, VC_GLOW));
        }
        if (overlay)
            draw_info(cols, rows + 1, overlay);
        vx_show();
        music_feed();
        long spent = vx_ms() - t;
        if (spent < 30)
            vx_wait(30 - spent);
    }
}
