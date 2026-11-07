/* Games: Invaders, Chomp, Mines. The game logic needs no screen and can be tested on the host. */
#ifdef HOST
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#else
#include "os.h"
#endif
static int rnd(u32 *r, int n) {
    *r = *r * 1103515245u + 12345u;
    return (int)((*r >> 16) % (u32)n);
}
/* ---------- Invaders ---------- */
#define IWMAX 240
#define IC    9
#define IR    5
#define IB    6
enum {
    G_READY,
    G_RUN,
    G_PAUSE,
    G_OVER
};
typedef struct {
    u8 al[IR][IC];
    u8 sh[2][IWMAX];
    int fw, fh, gap, ax, ay, dir, cnt, anim, left;
    int px, sx, sy, cool;
    int bx[IB], by[IB];
    int score, lives, wave, state, dead, bt;
    u32 rng;
} inv_t;
static void inv_wave(inv_t *p) {
    memset(p->al, 1, sizeof p->al);
    memset(p->sh, 0, sizeof p->sh);
    for (int b = 0; b < 4; b++)
        for (int x = 0; x < 7; x++) {
            int c = (2 * b + 1) * p->fw / 8 - 3 + x;
            p->sh[0][c] = 1;
            p->sh[1][c] = 1;
        }
    p->gap = p->fw / 11;
    if (p->gap < 5)
        p->gap = 5;
    if (p->gap > 16)
        p->gap = 16;
    p->ax = 4;
    p->dir = 1;
    p->ay = 1 + (p->wave > 5 ? 4 : p->wave - 1);
    p->cnt = 0;
    p->left = IR * IC;
    p->px = p->fw / 2;
    p->sy = -1;
    p->cool = 0;
    for (int i = 0; i < IB; i++)
        p->by[i] = -1;
}
static void inv_reset(inv_t *p) {
    u32 r = p->rng;
    int w = p->fw ? p->fw : 60, h = p->fh ? p->fh : 19;
    memset(p, 0, sizeof *p);
    p->rng = r;
    p->fw = w;
    p->fh = h;
    p->lives = 3;
    p->wave = 1;
    p->state = G_READY;
    inv_wave(p);
}
static void inv_shield(inv_t *p, int x, int y) {
    if (y >= p->fh - 5 && y < p->fh - 3 && x >= 0 && x < p->fw)
        p->sh[y - (p->fh - 5)][x] = 0;
}
static int inv_shield_at(inv_t *p, int x, int y) {
    return y >= p->fh - 5 && y < p->fh - 3 && x >= 0 && x < p->fw && p->sh[y - (p->fh - 5)][x];
}
static void inv_step(inv_t *p, int left, int right, int fire) {
    if (p->state != G_RUN)
        return;
    if (p->dead) {
        if (--p->dead == 0 && p->lives <= 0)
            p->state = G_OVER;
        return;
    }
    if (left && p->px > 1)
        p->px--;
    if (right && p->px < p->fw - 2)
        p->px++;
    if (p->cool)
        p->cool--;
    if (fire && p->sy < 0 && !p->cool) {
        p->sx = p->px;
        p->sy = p->fh - 2;
        p->cool = 8;
    }
    if (p->sy >= 0) {
        if (inv_shield_at(p, p->sx, p->sy)) {
            inv_shield(p, p->sx, p->sy);
            p->sy = -1;
        } else {
            int dx = p->sx - p->ax, dy = p->sy - p->ay;
            if (dx >= 0 && dy >= 0 && dx % p->gap < 3 && dy % 2 == 0 && dx / p->gap < IC &&
                dy / 2 < IR && p->al[dy / 2][dx / p->gap]) {
                int r = dy / 2;
                p->al[r][dx / p->gap] = 0;
                p->left--;
                p->score += (IR - r) * 10;
                p->sy = -1;
                if (!p->left) {
                    p->wave++;
                    inv_wave(p);
                    return;
                }
            } else if (--p->sy < 0)
                p->sy = -1;
        }
    }
    int period = 2 + p->left * 24 / (IR * IC) - p->wave / 2;
    if (period < 2)
        period = 2;
    if (++p->cnt >= period) {
        p->cnt = 0;
        p->anim ^= 1;
        int lo = IC, hi = -1, bot = -1;
        for (int r = 0; r < IR; r++)
            for (int c = 0; c < IC; c++)
                if (p->al[r][c]) {
                    if (c < lo)
                        lo = c;
                    if (c > hi)
                        hi = c;
                    if (r > bot)
                        bot = r;
                }
        int nx = p->ax + p->dir;
        if (nx + lo * p->gap < 0 || nx + hi * p->gap + 2 >= p->fw) {
            p->dir = -p->dir;
            p->ay++;
        } else
            p->ax = nx;
        int by = p->ay + bot * 2;
        for (int r = 0; r < IR; r++)
            for (int c = 0; c < IC; c++)
                if (p->al[r][c])
                    for (int k = 0; k < 3; k++)
                        inv_shield(p, p->ax + c * p->gap + k, p->ay + r * 2);
        if (by >= p->fh - 1) {
            p->lives = 0;
            p->dead = 40;
        }
    }
    if (++p->bt >= 3) {
        p->bt = 0;
        for (int i = 0; i < IB; i++)
            if (p->by[i] >= 0) {
                p->by[i]++;
                int x = p->bx[i], y = p->by[i];
                if (inv_shield_at(p, x, y)) {
                    inv_shield(p, x, y);
                    p->by[i] = -1;
                } else if (y == p->fh - 1 && x >= p->px - 1 && x <= p->px + 1) {
                    p->by[i] = -1;
                    p->lives--;
                    p->dead = 40;
                    for (int k = 0; k < IB; k++)
                        p->by[k] = -1;
                    return;
                } else if (y >= p->fh)
                    p->by[i] = -1;
            }
    }
    int maxb = 2 + p->wave;
    if (maxb > IB)
        maxb = IB;
    if (rnd(&p->rng, 14) == 0)
        for (int i = 0; i < maxb; i++)
            if (p->by[i] < 0) {
                int c = rnd(&p->rng, IC);
                for (int r = IR - 1; r >= 0; r--)
                    if (p->al[r][c]) {
                        p->bx[i] = p->ax + c * p->gap + 1;
                        p->by[i] = p->ay + r * 2 + 1;
                        break;
                    }
                break;
            }
}
/* Window size changed: adapt the field, keep the game */
static void inv_resize(inv_t *p, int w, int h) {
    if (w > IWMAX - 2)
        w = IWMAX - 2;
    if (w < 40)
        w = 40;
    if (h < 14)
        h = 14;
    if (w == p->fw && h == p->fh)
        return;
    p->fw = w;
    p->fh = h;
    int left = p->left, ay = p->ay;
    u8 al[IR][IC];
    memcpy(al, p->al, sizeof al);
    int wv = p->wave;
    p->wave = wv;
    int px = p->px;
    inv_wave(p);
    memcpy(p->al, al, sizeof al);
    p->left = left;
    p->ay = ay;
    while (p->ay + (IR - 1) * 2 >= h - 6 && p->ay > 1)
        p->ay--;
    p->px = px < 1 ? 1 : px > w - 2 ? w - 2 : px;
}
/* ---------- Chomp ---------- */
#define MW 19
#define MH 19
#define GN 4
static const char MAZE[MH][MW + 1] = {
    "###################", "#o.......#.......o#", "#.##.###.#.###.##.#", "#.................#",
    "#.##.#.#####.#.##.#", "#....#...#...#....#", "####.###.#.###.####", "####.#.......#.####",
    "####.#.##-##.#.####", ".......#GGG#.......", "####.#.#####.#.####", "#........#........#",
    "#.##.###.#.###.##.#", "#o.#.....P.....#.o#", "##.#.#.#####.#.#.##", "#....#...#...#....#",
    "#.######.#.######.#", "#.................#", "###################"};
static const int DX[4] = {0, -1, 0, 1}, DY[4] = {-1, 0, 1, 0};
typedef struct {
    int x, y, d, wait, cnt, px, py;
} ghost_t;
typedef struct {
    char m[MH][MW];
    int x, y, d, want, cnt, dots, score, lives, level, state, dead, fright, chain, phase, ppx, ppy,
        mouth;
    ghost_t g[GN];
    u32 rng;
} chomp_t;
static int ch_open(chomp_t *p, int x, int y) {
    if (y < 0 || y >= MH)
        return 0;
    if (x < 0 || x >= MW)
        return y == 9;
    char c = p->m[y][x];
    return c != '#' && c != '-' && c != 'G';
}
static void ch_pos(chomp_t *p) {
    p->x = 9;
    p->y = 13;
    p->d = 1;
    p->want = 1;
    p->cnt = 0;
    p->ppx = 9;
    p->ppy = 13;
    for (int i = 0; i < GN; i++) {
        ghost_t *g = &p->g[i];
        g->x = i ? 7 + i : 9;
        g->y = i ? 9 : 7;
        g->px = g->x;
        g->py = g->y;
        g->d = 1;
        g->cnt = 0;
        g->wait = i * 75;
    }
    p->fright = 0;
}
static void ch_level(chomp_t *p) {
    p->dots = 0;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            char c = MAZE[y][x];
            if (c == 'P')
                c = ' ';
            p->m[y][x] = c;
            if (c == '.' || c == 'o')
                p->dots++;
        }
    ch_pos(p);
}
static void ch_reset(chomp_t *p) {
    u32 r = p->rng;
    memset(p, 0, sizeof *p);
    p->rng = r;
    p->lives = 3;
    p->level = 1;
    p->state = G_READY;
    ch_level(p);
}
static void ch_catch(chomp_t *p) {
    for (int i = 0; i < GN; i++) {
        ghost_t *g = &p->g[i];
        if (g->wait)
            continue;
        int hit = (g->x == p->x && g->y == p->y) ||
                  (g->x == p->ppx && g->y == p->ppy && g->px == p->x && g->py == p->y);
        if (!hit)
            continue;
        if (p->fright) {
            p->chain++;
            p->score += 100 << p->chain;
            g->x = 8 + i % 3;
            g->y = 9;
            g->px = g->x;
            g->py = 9;
            g->wait = 100;
            g->cnt = 0;
        } else {
            p->lives--;
            p->dead = 45;
            return;
        }
    }
}
static void ghost_move(chomp_t *p, int i) {
    ghost_t *g = &p->g[i];
    int tx = p->x, ty = p->y, scatter = p->phase % 800 < 175;
    if (scatter) {
        tx = i & 1 ? MW - 2 : 1;
        ty = i & 2 ? MH - 2 : 1;
    } else if (i == 1) {
        tx += DX[p->d] * 4;
        ty += DY[p->d] * 4;
    } else if (i == 2) {
        if (rnd(&p->rng, 2)) {
            tx = rnd(&p->rng, MW);
            ty = rnd(&p->rng, MH);
        }
    } else if (i == 3) {
        int a = g->x - p->x, b = g->y - p->y;
        if (a * a + b * b < 36) {
            tx = 1;
            ty = MH - 2;
        }
    }
    int best = -1, bd = 0x7FFFFFFF, opts = 0;
    for (int d = 0; d < 4; d++) {
        if (d == ((g->d + 2) & 3))
            continue;
        int x = g->x + DX[d], y = g->y + DY[d];
        if (!ch_open(p, x, y))
            continue;
        opts++;
        int e = (x - tx) * (x - tx) + (y - ty) * (y - ty);
        if (p->fright)
            e = rnd(&p->rng, 1000);
        if (e < bd) {
            bd = e;
            best = d;
        }
    }
    (void)opts;
    if (best < 0)
        best = (g->d + 2) & 3;
    g->d = best;
    g->px = g->x;
    g->py = g->y;
    g->x += DX[best];
    g->y += DY[best];
    if (g->x < 0)
        g->x = MW - 1;
    else if (g->x >= MW)
        g->x = 0;
}
static void ch_step(chomp_t *p) {
    if (p->state != G_RUN)
        return;
    if (p->dead) {
        if (--p->dead == 0) {
            if (p->lives <= 0)
                p->state = G_OVER;
            else
                ch_pos(p);
        }
        return;
    }
    p->phase++;
    if (p->fright && --p->fright == 0)
        p->chain = 0;
    int sp = 4;
    if (++p->cnt >= sp) {
        p->cnt = 0;
        p->mouth ^= 1;
        p->ppx = p->x;
        p->ppy = p->y;
        if (ch_open(p, p->x + DX[p->want], p->y + DY[p->want]))
            p->d = p->want;
        if (ch_open(p, p->x + DX[p->d], p->y + DY[p->d])) {
            p->x += DX[p->d];
            p->y += DY[p->d];
            if (p->x < 0)
                p->x = MW - 1;
            else if (p->x >= MW)
                p->x = 0;
        }
        char *c = &p->m[p->y][p->x];
        if (*c == '.') {
            *c = ' ';
            p->dots--;
            p->score += 10;
        } else if (*c == 'o') {
            *c = ' ';
            p->dots--;
            p->score += 50;
            int f = 225 - p->level * 20;
            p->fright = f < 60 ? 60 : f;
            p->chain = 0;
        }
        ch_catch(p);
        if (p->dead)
            return;
        if (!p->dots) {
            p->level++;
            ch_level(p);
            return;
        }
    }
    int gs = 6 - (p->level > 2 ? 1 : 0);
    if (gs < 5)
        gs = 5;
    if (p->fright)
        gs = 9;
    for (int i = 0; i < GN; i++) {
        ghost_t *g = &p->g[i];
        if (g->wait) {
            if (--g->wait == 0) {
                g->x = 9;
                g->y = 7;
                g->px = 9;
                g->py = 7;
                g->d = rnd(&p->rng, 2) ? 1 : 3;
                g->cnt = 0;
            }
            continue;
        }
        if (++g->cnt >= gs) {
            g->cnt = 0;
            ghost_move(p, i);
        }
    }
    ch_catch(p);
}
/* ---------- Mines ---------- */
#define MNW 24
#define MNH 14
enum {
    C_MINE = 1,
    C_OPEN = 2,
    C_FLAG = 4
};
typedef struct {
    u8 c[MNH][MNW];
    int w, h, n, cx, cy, placed, open, flags, state, lvl;
    unsigned t0, t1;
    u32 rng;
} mines_t;
static void mi_reset(mines_t *p, int lvl) {
    static const int L[3][3] = {{9, 9, 10}, {16, 12, 30}, {24, 14, 60}};
    u32 r = p->rng;
    memset(p, 0, sizeof *p);
    p->rng = r;
    p->lvl = lvl;
    p->w = L[lvl][0];
    p->h = L[lvl][1];
    p->n = L[lvl][2];
    p->cx = p->w / 2;
    p->cy = p->h / 2;
    p->state = G_READY;
}
static int mi_near(mines_t *p, int x, int y, int bit) {
    int n = 0;
    for (int b = -1; b <= 1; b++)
        for (int a = -1; a <= 1; a++) {
            int u = x + a, v = y + b;
            if (u >= 0 && u < p->w && v >= 0 && v < p->h && (p->c[v][u] & bit))
                n++;
        }
    return n;
}
static void mi_place(mines_t *p, int x, int y) {
    int k = 0;
    while (k < p->n) {
        int a = rnd(&p->rng, p->w), b = rnd(&p->rng, p->h), dx = a - x, dy = b - y;
        if (p->c[b][a] & C_MINE)
            continue;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1)
            continue;
        p->c[b][a] |= C_MINE;
        k++;
    }
    p->placed = 1;
}
static void mi_open(mines_t *p, int x, int y) {
    static u8 qx[MNW * MNH], qy[MNW * MNH];
    int h = 0, t = 0;
    if (p->c[y][x] & (C_OPEN | C_FLAG))
        return;
    if (p->c[y][x] & C_MINE) {
        p->c[y][x] |= C_OPEN;
        p->state = G_OVER;
        return;
    }
    p->c[y][x] |= C_OPEN;
    p->open++;
    qx[t] = (u8)x;
    qy[t++] = (u8)y;
    while (h < t) {
        int a = qx[h], b = qy[h++];
        if (mi_near(p, a, b, C_MINE))
            continue;
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                int u = a + i, v = b + j;
                if (u < 0 || u >= p->w || v < 0 || v >= p->h ||
                    (p->c[v][u] & (C_OPEN | C_FLAG | C_MINE)))
                    continue;
                p->c[v][u] |= C_OPEN;
                p->open++;
                qx[t] = (u8)u;
                qy[t++] = (u8)v;
            }
    }
}
static void mi_reveal(mines_t *p) {
    if (p->state == G_OVER || p->state == G_PAUSE)
        return;
    int x = p->cx, y = p->cy;
    if (!p->placed) {
        mi_place(p, x, y);
        p->state = G_RUN;
    }
    if (p->c[y][x] & C_OPEN) {
        if (mi_near(p, x, y, C_MINE) == mi_near(p, x, y, C_FLAG))
            for (int b = -1; b <= 1 && p->state == G_RUN; b++)
                for (int a = -1; a <= 1 && p->state == G_RUN; a++) {
                    int u = x + a, v = y + b;
                    if (u >= 0 && u < p->w && v >= 0 && v < p->h)
                        mi_open(p, u, v);
                }
    } else
        mi_open(p, x, y);
    if (p->state == G_RUN && p->open == p->w * p->h - p->n)
        p->state = G_PAUSE;
}
static void mi_flag(mines_t *p) {
    if (p->state == G_OVER || p->state == G_PAUSE || (p->c[p->cy][p->cx] & C_OPEN))
        return;
    p->c[p->cy][p->cx] ^= C_FLAG;
    p->flags += (p->c[p->cy][p->cx] & C_FLAG) ? 1 : -1;
}
#ifndef HOST
/* ---------- Screen ---------- */
static const char *centre(win_t *w, int y, u8 a, const char *s) {
    ctext(w, (cw(w) - (int)strlen(s)) / 2, y, a, s);
    return s;
}
#define BG  AT(C_VOID, C_DIM)
#define TXT AT(C_VOID, C_GLOW)
static int k_l(void) {
    return key_down(0x4B) || key_down(0x1E);
}
static int k_r(void) {
    return key_down(0x4D) || key_down(0x20);
}
static int best_i, best_c;
static void inv_frame(win_t *w) {
    inv_t *p = w->st;
    inv_resize(p, cw(w) - 4, ch(w) - 1);
    int s0 = p->score, l0 = p->lives, y0 = p->sy, a0 = p->anim, st0 = p->state;
    inv_step(p, k_l(), k_r(), key_down(0x39));
    if (y0 < 0 && p->sy >= 0)
        snd_tone(1200, 70, 25, 0, 0);
    if (p->score > s0)
        snd_tone(150, 90, 45, 3, 0);
    if (p->lives < l0) {
        snd_tone(90, 450, 70, 3, 0);
        snd_tone(70, 450, 50, 2, 0);
    }
    if (a0 != p->anim) {
        static int m;
        static const short n[] = {98, 87, 78, 73};
        snd_tone(n[m++ & 3], 60, 35, 0, 0);
    }
    if (st0 == G_RUN && p->state == G_OVER) {
        snd_tone(220, 200, 55, 2, 200);
        snd_tone(165, 400, 55, 2, 420);
    }
    if (p->score > best_i)
        best_i = p->score;
}
static void inv_draw(win_t *w) {
    inv_t *p = w->st;
    cfill(w, BG);
    int ox = (cw(w) - p->fw) / 2, oy = 1, IH_ = p->fh;
    char s[70];
    ksnprintf(s, 70,
              TR("Punkte %d   Rekord %d   Welle %d   Leben %d",
                 "Score %d   Best %d   Wave %d   Lives %d"),
              p->score, best_i, p->wave, p->lives < 0 ? 0 : p->lives);
    centre(w, 0, TXT, s);
    static const char *S[3][2] = {{"/@\\", "\\@/"}, {"{#}", "}#{"}, {"<O>", ">O<"}};
    static const u8 C[3] = {C_HOT, C_NEON, C_ORCH};
    for (int r = 0; r < IR; r++)
        for (int c = 0; c < IC; c++)
            if (p->al[r][c]) {
                int k = r == 0 ? 0 : r < 3 ? 1 : 2;
                for (int i = 0; i < 3; i++)
                    cput(w, ox + p->ax + c * p->gap + i, oy + p->ay + r * 2, (u8)S[k][p->anim][i],
                         AT(C_VOID, C[k]));
            }
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < p->fw; x++)
            if (p->sh[y][x])
                cput(w, ox + x, oy + IH_ - 5 + y, 0xB1, AT(C_VOID, C_VIOLET));
    if (p->sy >= 0)
        cput(w, ox + p->sx, oy + p->sy, 0xB3, TXT);
    for (int i = 0; i < IB; i++)
        if (p->by[i] >= 0)
            cput(w, ox + p->bx[i], oy + p->by[i], '!', AT(C_VOID, C_HOT));
    if (p->dead && (p->dead & 4)) {
        for (int i = -1; i <= 1; i++)
            cput(w, ox + p->px + i, oy + IH_ - 1, '*', AT(C_VOID, C_HOT));
    } else if (!p->dead) {
        cput(w, ox + p->px - 1, oy + IH_ - 1, 0xDC, AT(C_VOID, C_NEON));
        cput(w, ox + p->px, oy + IH_ - 1, 0xDB, AT(C_VOID, C_NEON));
        cput(w, ox + p->px + 1, oy + IH_ - 1, 0xDC, AT(C_VOID, C_NEON));
    }
    if (p->state == G_READY) {
        centre(w, oy + IH_ - 7, TXT, TR("Enter startet", "Enter starts"));
        centre(
            w, oy + IH_ - 6, BG,
            TR("Pfeile bewegen, Leertaste schießt, P Pause", "Arrows move, Space fires, P pause"));
    } else if (p->state == G_PAUSE)
        centre(w, oy + IH_ - 7, TXT, "Pause");
    else if (p->state == G_OVER) {
        centre(w, oy + IH_ - 7, TXT, TR("Spiel vorbei", "Game over"));
        centre(w, oy + IH_ - 6, BG, TR("Enter für ein neues Spiel", "Enter for a new game"));
    }
}
static void inv_key(win_t *w, key_t k) {
    inv_t *p = w->st;
    if (k.key == K_ENTER) {
        if (p->state == G_READY)
            p->state = G_RUN;
        else if (p->state == G_OVER) {
            inv_reset(p);
            p->state = G_RUN;
        }
    } else if (k.key == 'p' || k.key == 'P') {
        if (p->state == G_RUN)
            p->state = G_PAUSE;
        else if (p->state == G_PAUSE)
            p->state = G_RUN;
    }
}
static void gfree(win_t *w) {
    kfree(w->st);
}
static const app_t inv_app = {inv_draw, inv_key, 0, 0, gfree, inv_frame};
void app_invaders(void) {
    inv_t *p = kmalloc(sizeof *p);
    if (!p)
        return;
    memset(p, 0, sizeof *p);
    p->rng = (u32)ticks * 2654435761u + 1;
    inv_reset(p);
    if (!win_open("Invaders", 6, 1, 66, 22, &inv_app, p))
        kfree(p);
}
static void ch_frame(win_t *w) {
    chomp_t *p = w->st;
    if (key_down(0x48) || key_down(0x11))
        p->want = 0;
    else if (key_down(0x4B) || key_down(0x1E))
        p->want = 1;
    else if (key_down(0x50) || key_down(0x1F))
        p->want = 2;
    else if (key_down(0x4D) || key_down(0x20))
        p->want = 3;
    int s0 = p->score, l0 = p->lives, f0 = p->fright, st0 = p->state;
    ch_step(p);
    if (p->score > s0) {
        static int m;
        int d = p->score - s0;
        if (d >= 100) {
            snd_tone(660, 70, 50, 1, 0);
            snd_tone(990, 110, 50, 1, 70);
        } else
            snd_tone(m++ & 1 ? 330 : 247, 45, 30, 1, 0);
    }
    if (p->fright > f0)
        for (int i = 0; i < 4; i++)
            snd_tone(300 + i * 120, 70, 40, 0, i * 60);
    if (p->lives < l0)
        for (int i = 0; i < 7; i++)
            snd_tone(700 - i * 80, 70, 50, 0, i * 70);
    if (st0 == G_RUN && p->state == G_OVER) {
        snd_tone(196, 300, 50, 2, 500);
        snd_tone(147, 500, 50, 2, 800);
    }
    if (p->score > best_c)
        best_c = p->score;
}
static void ch_draw(win_t *w) {
    chomp_t *p = w->st;
    cfill(w, BG);
    int tw = cw(w) / MW;
    if (tw < 2)
        tw = 2;
    int th = (ch(w) - 1) / MH;
    if (th < 1)
        th = 1;
    int ox = (cw(w) - MW * tw) / 2, oy = 1;
    char s[60];
    ksnprintf(
        s, 60,
        TR("Punkte %d  Rekord %d  Level %d  Leben %d", "Score %d  Best %d  Level %d  Lives %d"),
        p->score, best_c, p->level, p->lives < 0 ? 0 : p->lives);
    centre(w, 0, TXT, s);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            char c = p->m[y][x];
            int a = ox + x * tw, b = oy + y * th;
            if (c == '#') {
                for (int j = 0; j < th; j++)
                    for (int i = 0; i < tw; i++)
                        cput(w, a + i, b + j, 0xDB, AT(C_VOID, C_SEL));
            } else if (c == '-') {
                for (int i = 0; i < tw; i++)
                    cput(w, a + i, b + th / 2, 0xC4, AT(C_VOID, C_DIM));
            } else if (c == '.')
                cput(w, a + tw / 2 - 1, b + th / 2, 0xF9, AT(C_VOID, C_ORCH));
            else if (c == 'o')
                cput(w, a + tw / 2 - 1, b + th / 2, 0xFE,
                     AT(C_VOID, (ticks & 32) ? C_GLOW : C_HOT));
        }
    if (!p->dead || (p->dead & 4)) {
        static const char *M[4] = {"Vv", "<(", ")^", ")>"};
        const char *g = p->mouth ? M[p->d] : "()";
        if (p->d == 0 && p->mouth)
            g = "\\/";
        if (p->d == 2 && p->mouth)
            g = "/\\";
        cput(w, ox + p->x * tw + tw / 2 - 1, oy + p->y * th + th / 2, (u8)g[0], TXT);
        cput(w, ox + p->x * tw + tw / 2, oy + p->y * th + th / 2, (u8)g[1], TXT);
    }
    static const u8 GC[GN] = {C_HOT, C_NEON, C_ORCH, C_LAV};
    if (!p->dead)
        for (int i = 0; i < GN; i++) {
            ghost_t *g = &p->g[i];
            u8 a = AT(C_VOID,
                      p->fright ? ((p->fright < 50 && (p->fright & 8)) ? C_GLOW : C_MID) : GC[i]);
            cput(w, ox + g->x * tw + tw / 2 - 1, oy + g->y * th + th / 2, 0xEA, a);
            cput(w, ox + g->x * tw + tw / 2, oy + g->y * th + th / 2, p->fright ? '~' : '"', a);
        }
    if (p->state == G_READY) {
        centre(w, oy + 9 * th, TXT, TR("Enter startet", "Enter starts"));
    } else if (p->state == G_PAUSE)
        centre(w, oy + 9 * th, TXT, "Pause");
    else if (p->state == G_OVER)
        centre(w, oy + 9 * th, TXT, TR("Vorbei: Enter", "Game over: Enter"));
}
static void ch_key(win_t *w, key_t k) {
    chomp_t *p = w->st;
    if (k.key == K_UP)
        p->want = 0;
    else if (k.key == K_LEFT)
        p->want = 1;
    else if (k.key == K_DOWN)
        p->want = 2;
    else if (k.key == K_RIGHT)
        p->want = 3;
    else if (k.key == K_ENTER) {
        if (p->state == G_READY)
            p->state = G_RUN;
        else if (p->state == G_OVER) {
            ch_reset(p);
            p->state = G_RUN;
        }
    } else if (k.key == 'p' || k.key == 'P') {
        if (p->state == G_RUN)
            p->state = G_PAUSE;
        else if (p->state == G_PAUSE)
            p->state = G_RUN;
    }
}
static const app_t ch_app = {ch_draw, ch_key, 0, 0, gfree, ch_frame};
void app_chomp(void) {
    chomp_t *p = kmalloc(sizeof *p);
    if (!p)
        return;
    memset(p, 0, sizeof *p);
    p->rng = (u32)ticks * 2654435761u + 1;
    ch_reset(p);
    if (!win_open("Chomp", 17, 1, MW * 2 + 6, MH + 3, &ch_app, p))
        kfree(p);
}
static void mi_draw(win_t *w) {
    mines_t *p = w->st;
    cfill(w, AT(C_VOID, C_MID));
    char s[60];
    unsigned e = p->state == G_RUN ? (unsigned)(ticks / 100) - p->t0 : p->t1;
    ksnprintf(s, 60, TR("Minen %d   Zeit %d s", "Mines %d   Time %d s"), p->n - p->flags, e);
    centre(w, 0, TXT, s);
    int sx = cw(w) / p->w, sy = (ch(w) - 2) / p->h;
    if (sx < 2)
        sx = 2;
    if (sx > 16)
        sx = 16;
    if (sy < 1)
        sy = 1;
    if (sy > 6)
        sy = 6;
    int ox = (cw(w) - p->w * sx) / 2, oy = 1;
    static const u8 NC[9] = {0, C_ORCH, C_NEON, C_HOT, C_GLOW, C_HOT, C_NEON, C_GLOW, C_GLOW};
    for (int y = 0; y < p->h; y++)
        for (int x = 0; x < p->w; x++) {
            int v = p->c[y][x], cur = x == p->cx && y == p->cy, over = p->state == G_OVER;
            u8 c = ' ', a = AT(C_VOID, C_MID);
            if (v & C_OPEN) {
                if (v & C_MINE) {
                    c = 0x0F;
                    a = AT(C_VOID, C_HOT);
                } else {
                    int n = mi_near(p, x, y, C_MINE);
                    c = n ? (u8)('0' + n) : ' ';
                    a = AT(C_VOID, NC[n]);
                }
            } else if (v & C_FLAG) {
                c = (over && !(v & C_MINE)) ? 'X' : 0x10;
                a = AT(C_RAISE, C_HOT);
            } else if (over && (v & C_MINE)) {
                c = 0x0F;
                a = AT(C_RAISE, C_HOT);
            } else {
                c = 0xFA;
                a = AT(C_RAISE, C_MID);
            }
            if (cur)
                a = AT(C_SEL, C_GLOW);
            int cw_ = sy >= 2 ? sx - 1 : sx, ch_ = sy >= 2 ? sy - 1 : sy;
            if (sx < 3)
                cw_ = sx;
            for (int j = 0; j < ch_; j++)
                for (int i = 0; i < cw_; i++)
                    cput(w, ox + x * sx + i, oy + y * sy + j, ' ', a);
            cput(w, ox + x * sx + (cw_ - 1) / 2, oy + y * sy + (ch_ - 1) / 2, c, a);
        }
    int y = oy + p->h * sy;
    if (y >= ch(w))
        y = ch(w) - 1;
    if (p->state == G_OVER)
        centre(w, y, TXT,
               TR("Mine getroffen! N für ein neues Spiel", "You hit a mine! N for a new game"));
    else if (p->state == G_PAUSE)
        centre(w, y, TXT, TR("Gewonnen! N für ein neues Spiel", "You won! N for a new game"));
    else
        centre(w, y, AT(C_VOID, C_MID),
               TR("Leertaste Aufdecken  F Flagge  1-3 Größe  N Neu",
                  "Space Open  F Flag  1-3 Size  N New"));
}
static void mi_finish(mines_t *p, int was) {
    if (was == G_READY && p->state != G_READY)
        p->t0 = (unsigned)(ticks / 100);
    if (p->state == G_OVER || p->state == G_PAUSE)
        p->t1 = (unsigned)(ticks / 100) - p->t0;
}
static void mi_key(win_t *w, key_t k) {
    mines_t *p = w->st;
    int was = p->state;
    if (k.key == K_UP && p->cy)
        p->cy--;
    else if (k.key == K_DOWN && p->cy < p->h - 1)
        p->cy++;
    else if (k.key == K_LEFT && p->cx)
        p->cx--;
    else if (k.key == K_RIGHT && p->cx < p->w - 1)
        p->cx++;
    else if (k.key == ' ' || k.key == K_ENTER) {
        int o0 = p->open;
        mi_reveal(p);
        mi_finish(p, was);
        if (p->state == G_OVER && was != G_OVER) {
            snd_tone(80, 600, 75, 3, 0);
            snd_tone(60, 600, 50, 2, 0);
        } else if (p->state == G_PAUSE && was != G_PAUSE) {
            for (int i = 0; i < 5; i++)
                snd_tone(523 + i * 131, 120, 55, 0, i * 110);
        } else if (p->open > o0)
            snd_tone(p->open - o0 > 5 ? 880 : 660, 40, 35, 1, 0);
    } else if (k.key == 'f' || k.key == 'F') {
        mi_flag(p);
        snd_tone(440, 40, 30, 0, 0);
    } else if (k.key == 'n' || k.key == 'N')
        mi_reset(p, p->lvl);
    else if (k.key >= '1' && k.key <= '3')
        mi_reset(p, k.key - '1');
}
static const app_t mi_app = {mi_draw, mi_key, 0, 0, gfree, 0};
void app_mines(void) {
    mines_t *p = kmalloc(sizeof *p);
    if (!p)
        return;
    memset(p, 0, sizeof *p);
    p->rng = (u32)ticks * 2654435761u + 1;
    mi_reset(p, 1);
    if (!win_open("Mines", 14, 2, 52, 19, &mi_app, p))
        kfree(p);
}
#endif
