/* Host test of the game logic: gcc -g -fsanitize=address,undefined -DHOST tools/gametest.c -o
 * gametest */
#include "../kernel/games.c"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static int botdir(chomp_t *p) {
    static int seen[MH][MW], qx[400], qy[400], first[MH][MW];
    memset(seen, 0, sizeof seen);
    int h = 0, t = 0;
    seen[p->y][p->x] = 1;
    qx[t] = p->x;
    qy[t++] = p->y;
    first[p->y][p->x] = -1;
    while (h < t) {
        int x = qx[h], y = qy[h++];
        if (p->m[y][x] == '.' || p->m[y][x] == 'o')
            if (first[y][x] >= 0)
                return first[y][x];
        for (int d = 0; d < 4; d++) {
            int u = x + DX[d], v = y + DY[d];
            if (!ch_open(p, u, v))
                continue;
            u = (u + MW) % MW;
            if (seen[v][u])
                continue;
            seen[v][u] = 1;
            first[v][u] = first[y][x] < 0 ? d : first[y][x];
            qx[t] = u;
            qy[t++] = v;
        }
    }
    return p->d;
}
int main(void) {
    static inv_t I;
    static chomp_t C;
    static mines_t M;
    long wins = 0, lost = 0, wave = 0, lvl = 0, cleared = 0, caught = 0, eaten = 0;
    /* Maze: are all dots reachable? */ {
        int seen[MH][MW] = {{0}}, qx[400], qy[400], h = 0, t = 0;
        memset(&C, 0, sizeof C);
        C.rng = 1;
        ch_reset(&C);
        qx[0] = 9;
        qy[0] = 13;
        seen[13][9] = 1;
        t = 1;
        while (h < t) {
            int x = qx[h], y = qy[h++];
            for (int d = 0; d < 4; d++) {
                int u = x + DX[d], v = y + DY[d];
                if (!ch_open(&C, u, v))
                    continue;
                u = (u + MW) % MW;
                if (!seen[v][u]) {
                    seen[v][u] = 1;
                    qx[t] = u;
                    qy[t++] = v;
                }
            }
        }
        int miss = 0, dots = 0;
        for (int y = 0; y < MH; y++) {
            assert(strlen(MAZE[y]) == MW);
            for (int x = 0; x < MW; x++) {
                if (MAZE[y][x] == '.' || MAZE[y][x] == 'o') {
                    dots++;
                    if (!seen[y][x])
                        miss++;
                }
                assert(MAZE[y][x] == MAZE[y][MW - 1 - x] || MAZE[y][x] == 'P' ||
                       MAZE[y][MW - 1 - x] == 'P');
            }
        }
        printf("Labyrinth: %d Punkte, %d unerreichbar\n", dots, miss);
        assert(!miss);
    }
    for (int s = 1; s <= 300; s++) {
        I.rng = s;
        inv_reset(&I);
        I.state = G_RUN;
        for (int f = 0; f < 30000 && I.state == G_RUN; f++) {
            int w0 = I.wave;
            if (rand() % 500 == 0)
                inv_resize(&I, 40 + rand() % 200, 14 + rand() % 80);
            inv_step(&I, rand() % 3 == 0, rand() % 3 == 0, 1);
            assert(I.px >= 1 && I.px <= I.fw - 2);
            assert(I.left >= 0 && I.left <= 45);
            int n = 0;
            for (int r = 0; r < IR; r++)
                for (int c = 0; c < IC; c++)
                    n += I.al[r][c];
            assert(n == I.left);
            if (I.wave > w0)
                wave++;
        }
    }
    for (int s = 1; s <= 300; s++) {
        C.rng = s;
        ch_reset(&C);
        C.state = G_RUN;
        for (int f = 0; f < 40000 && C.state == G_RUN; f++) {
            int l = C.lives, sc = C.score, lv = C.level;
            if (s % 2)
                C.lives = 3, l = 3, C.want = botdir(&C);
            else if (rand() % 6 == 0)
                C.want = rand() % 4;
            ch_step(&C);
            assert(C.x >= 0 && C.x < MW && C.y >= 0 && C.y < MH);
            char c = C.m[C.y][C.x];
            assert(c != '#' && c != '-' && c != 'G');
            for (int i = 0; i < GN; i++) {
                ghost_t *g = &C.g[i];
                assert(g->x >= 0 && g->x < MW && g->y >= 0 && g->y < MH);
                if (!g->wait)
                    assert(C.m[g->y][g->x] != '#');
            }
            int d = 0;
            for (int y = 0; y < MH; y++)
                for (int x = 0; x < MW; x++)
                    d += C.m[y][x] == '.' || C.m[y][x] == 'o';
            assert(d == C.dots);
            if (C.lives < l)
                caught++;
            if (C.score >= sc + 200)
                eaten++;
            if (C.level > lv)
                lvl++;
        }
    }
    for (int s = 1; s <= 3000; s++) {
        M.rng = s;
        mi_reset(&M, s % 3);
        for (int f = 0; f < 5000 && M.state != G_OVER && M.state != G_PAUSE;
             f++) { /* a reasonably clever player */
            M.cx = rand() % M.w;
            M.cy = rand() % M.h;
            if (M.placed && (rand() % 4) == 0 && !(M.c[M.cy][M.cx] & C_OPEN) &&
                (M.c[M.cy][M.cx] & C_MINE) && !(M.c[M.cy][M.cx] & C_FLAG)) {
                mi_flag(&M);
                continue;
            }
            if (M.placed && (M.c[M.cy][M.cx] & C_MINE) && rand() % 40)
                continue;
            int first = !M.placed;
            mi_reveal(&M);
            if (first) {
                assert(M.state == G_RUN);
                assert(!(M.c[M.cy][M.cx] & C_MINE));
                int n = 0, o = 0;
                for (int y = 0; y < M.h; y++)
                    for (int x = 0; x < M.w; x++) {
                        n += M.c[y][x] & C_MINE;
                        o += (M.c[y][x] & C_OPEN) != 0;
                    }
                assert(n == M.n);
                assert(o == M.open);
                assert(mi_near(&M, M.cx, M.cy, C_MINE) == 0);
            }
        }
        if (M.state == G_PAUSE)
            wins++;
        else if (M.state == G_OVER)
            lost++;
        else
            cleared++;
    }
    printf(
        "Invaders: %ld Wellen geschafft\nChomp: %ld Level, %ld gefangen, %ld Geister gegessen\nMines: %ld gewonnen, %ld verloren, %ld offen\n",
        wave, lvl, caught, eaten, wins, lost, cleared);
    fflush(stdout);
    assert(wave && lvl && caught && wins && lost);
    return 0;
}
