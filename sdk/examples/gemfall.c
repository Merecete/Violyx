/* Gemfall - a falling gem puzzle for Violyx.
   SPDX-License-Identifier: BSD-3-Clause
   Copyright (c) 2026, Merecete
   Three gems fall as a column. Left/Right move it, Up or Space rolls the gems, Down drops faster.
   Three or more of a colour in a row, a column or a diagonal vanish, and the gems above fall:
   that can set off a chain. Now and then a magic column comes: where it lands, every gem of the
   colour below it vanishes. Every 30 gems the level goes up and the gems fall faster.
   P pauses, Enter starts again after the game. F5 zooms the window, and the gems grow with it. */
#include "violyx.h"

#define FW       6 /* the well: 6 gems wide, 13 high */
#define FH       13
#define NCOL     6 /* gem colours */
#define MAGIC    7
#define FRAME_MS 25
#define KEY_DOWN 0x50 /* scan code for vx_held */
#define KEY_S    0x1F

static uint8_t well[FH][FW];
static uint8_t mark[FH][FW];
static int px, py, pg[3], nx[3]; /* falling column: x, row of its top gem, gems top to bottom */
static int magic_in;             /* pieces until the next magic column */
static long score, best, gems;
static int level, chain, over, paused, flash, fall_ms, pieces;
static long next_fall;
static int x0_ = 8, y0_ = 3; /* where the well is drawn */
static int info;             /* 1 the Info window, 2 the licence text; the game waits meanwhile */
static int lic_top;

/* Every gem has its colour, a light edge on the left and a shadow on the right */
static const uint8_t rgb[NCOL + 1][3][3] = {
    {{0}},
    {{255, 0, 95}, {255, 135, 175}, {135, 0, 0}},     /* ruby */
    {{255, 135, 0}, {255, 215, 135}, {175, 95, 0}},   /* amber */
    {{255, 215, 0}, {255, 255, 175}, {175, 135, 0}},  /* topaz */
    {{0, 215, 95}, {135, 255, 175}, {0, 95, 0}},      /* emerald */
    {{0, 135, 255}, {135, 215, 255}, {0, 0, 175}},    /* sapphire */
    {{175, 95, 255}, {215, 175, 255}, {95, 0, 175}}}; /* amethyst */
static uint8_t gc[NCOL + 1][3];
static void colours(void) {
    for (int c = 1; c <= NCOL; c++)
        for (int k = 0; k < 3; k++)
            gc[c][k] = vx_rgb(rgb[c][k][0], rgb[c][k][1], rgb[c][k][2]);
}

static void new_piece(int *g) {
    if (--magic_in <= 0 && level >= 2) { /* now and then: the magic column */
        magic_in = 35 + vx_random(25);
        g[0] = g[1] = g[2] = MAGIC;
        return;
    }
    int n = level < 3 ? 5 : NCOL; /* the first two levels have five colours */
    for (int i = 0; i < 3; i++)
        g[i] = 1 + vx_random(n);
}
static int fall_time(void) {
    int t = 800 - (level - 1) * 70;
    return t < 90 ? 90 : t;
}
static void new_game(void) {
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            well[y][x] = 0;
    score = gems = pieces = 0;
    level = 1;
    over = paused = flash = 0;
    magic_in = 40;
    new_piece(nx);
    for (int i = 0; i < 3; i++)
        pg[i] = 0;
    px = -1;
}
/* the falling column fits at x, with its top gem in row y (rows above the well are free) */
static int fits(int x, int y) {
    if (x < 0 || x >= FW)
        return 0;
    for (int i = 0; i < 3; i++) {
        int r = y + i;
        if (r >= FH || (r >= 0 && well[r][x]))
            return 0;
    }
    return 1;
}
static void game_over(void) {
    over = 1;
    for (int i = 0; i < 4; i++)
        vx_tone(392 - i * 60, 140, 55, VX_SAW, i * 140);
}
static void spawn(void) {
    for (int i = 0; i < 3; i++)
        pg[i] = nx[i];
    new_piece(nx);
    px = FW / 2 - 1;
    py = -3;
    pieces++;
    fall_ms = fall_time();
    next_fall = vx_ms() + fall_ms;
    if (!fits(px, py + 1)) { /* no room below the top: the game is over */
        game_over();
    }
}
/* Marks every run of three or more; returns how many gems are marked */
static int find_runs(void) {
    static const int dir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    int n = 0;
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            mark[y][x] = 0;
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            int c = well[y][x];
            if (!c)
                continue;
            for (int d = 0; d < 4; d++) {
                int dx = dir[d][0], dy = dir[d][1];
                int bx = x - dx, by = y - dy;
                if (bx >= 0 && bx < FW && by >= 0 && by < FH && well[by][bx] == c)
                    continue; /* not the start of the run */
                int len = 0;
                while (x + dx * len >= 0 && x + dx * len < FW && y + dy * len >= 0 &&
                       y + dy * len < FH && well[y + dy * len][x + dx * len] == c)
                    len++;
                if (len >= 3)
                    for (int i = 0; i < len; i++)
                        mark[y + dy * i][x + dx * i] = 1;
            }
        }
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            n += mark[y][x];
    return n;
}
static void collapse(void) {
    for (int x = 0; x < FW; x++) {
        int w = FH - 1;
        for (int y = FH - 1; y >= 0; y--)
            if (well[y][x]) {
                int c = well[y][x];
                well[y][x] = 0;
                well[w--][x] = (uint8_t)c;
            }
    }
}
/* Removes the marked gems with points for them; the chain makes every step worth more */
static void clear_marked(int n) {
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            if (mark[y][x])
                well[y][x] = 0;
    score += (long)n * 10 * level * chain;
    int before = (int)(gems / 30);
    gems += n;
    if (gems / 30 > before) {
        level++;
        for (int i = 0; i < 4; i++)
            vx_tone(523 + i * 131, 90, 50, VX_SQUARE, i * 90);
    }
    collapse();
}
/* The column has landed: into the well, then the runs, one step of the chain at a time */
static void land(void) {
    if (pg[0] == MAGIC) {
        int below = py + 3 < FH ? well[py + 3][px] : 0, n = 0;
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FW; x++) {
                mark[y][x] = below && well[y][x] == below;
                n += mark[y][x];
            }
        for (int i = 0; i < 6; i++)
            vx_tone(660 + i * 110, 60, 50, VX_TRIANGLE, i * 50);
        px = -1;
        if (n) {
            chain = 1;
            score += 500L * level;
            flash = 12;
        } else
            spawn();
        return;
    }
    for (int i = 0; i < 3; i++) {
        if (py + i < 0) { /* part of the column is still above the well */
            game_over();
            px = -1;
            return;
        }
        well[py + i][px] = (uint8_t)pg[i];
    }
    px = -1;
    vx_tone(150, 40, 45, VX_TRIANGLE, 0);
    chain = 1;
    int n = find_runs();
    if (n) {
        flash = 12;
        vx_tone(523 + chain * 80, 90, 50, VX_SQUARE, 0);
    } else
        spawn();
}
static void step(void) {
    if (over || paused || info)
        return;
    if (flash) { /* the marked gems blink, then they vanish and the rest falls */
        if (--flash)
            return;
        int n = 0;
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FW; x++)
                n += mark[y][x];
        clear_marked(n);
        int m = find_runs();
        if (m) {
            chain++;
            flash = 12;
            vx_tone(523 + chain * 120, 90, 50, VX_SQUARE, 0);
            vx_tone(659 + chain * 120, 90, 50, VX_SQUARE, 80);
        } else
            spawn();
        return;
    }
    if (px < 0) {
        spawn();
        return;
    }
    int fast = vx_held(KEY_DOWN) || vx_held(KEY_S);
    long now = vx_ms();
    if (now < next_fall && !(fast && now >= next_fall - fall_ms + 30))
        return;
    next_fall = now + fall_ms;
    if (fits(px, py + 1)) {
        py++;
        if (fast)
            score += 1;
    } else
        land();
}
static void key(int k) {
    if (k == VK_MENU + 0) { /* Info */
        info = 1;
        return;
    }
    if (info) {
        if (info == 1 && (k == 'l' || k == 'L')) {
            info = 2;
            lic_top = 0;
        } else if (info == 2 && k == VK_UP)
            lic_top -= lic_top > 0;
        else if (info == 2 && k == VK_DOWN)
            lic_top++;
        else if (info == 2 && k == VK_PGUP)
            lic_top = lic_top > 8 ? lic_top - 8 : 0;
        else if (info == 2 && k == VK_PGDN)
            lic_top += 8;
        else if (k == VK_ENTER || k == VK_ESC || k == VK_BS || k == 'l' || k == 'L')
            info--;
        return;
    }
    if (k == 'p' || k == 'P' || k == VK_MENU + 1) {
        if (!over)
            paused = !paused;
        return;
    }
    if (over) {
        if (k == VK_ENTER)
            new_game();
        return;
    }
    if (paused || px < 0 || flash)
        return;
    if ((k == VK_LEFT || k == 'a' || k == 'A') && fits(px - 1, py)) {
        px--;
        vx_tone(330, 15, 25, VX_TRIANGLE, 0);
    }
    if ((k == VK_RIGHT || k == 'd' || k == 'D') && fits(px + 1, py)) {
        px++;
        vx_tone(330, 15, 25, VX_TRIANGLE, 0);
    }
    if (k == VK_UP || k == ' ' || k == 'w' || k == 'W') { /* the bottom gem goes to the top */
        int t = pg[2];
        pg[2] = pg[1];
        pg[1] = pg[0];
        pg[0] = t;
        vx_tone(587, 20, 30, VX_SQUARE, 0);
    }
}

static int slen(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}
/* The well and the next column are pictures: every text cell shows 4x8 of their pixels, so the
 * gems can be any size and grow with the window (F5). A gem: its colour, a light edge at the top
 * and left, a shadow at the bottom and right, a small shine and a dark gap to its neighbours. */
#define IMW 120 /* largest picture: 6 gems of 20 pixels */
#define IMH 264
static uint8_t img[IMW * IMH], nimg[24 * 64];
static void gem_px(uint8_t *im, int iw, int x0, int y0, int g, int c, int lit) {
    int k = c == MAGIC ? 1 + (int)(vx_ms() / 120) % NCOL : c; /* magic: all colours in turn */
    uint8_t white = vx_rgb(255, 255, 255), gap = vx_rgb(8, 6, 15);
    for (int y = 0; y < g; y++)
        for (int x = 0; x < g; x++) {
            uint8_t v = gc[k][0];
            int e = g >= 12 ? 2 : 1, sh = g >= 12 ? 2 : 1; /* width of the edges, of the shine */
            if (x == 0 || y == 0 || x == g - 1 || y == g - 1)
                v = gap;
            else if (lit)
                v = white;
            else if (x <= e || y <= e)
                v = x + y < g ? gc[k][1] : gc[k][2]; /* top left light, the corners split */
            else if (x >= g - 1 - e || y >= g - 1 - e)
                v = gc[k][2];
            else if (x > e + 1 && x <= e + 1 + sh && y > e + 1 && y <= e + 1 + sh)
                v = white; /* the shine */
            if (c == MAGIC && !lit && (x == g / 2 || y == g / 2) && x > 2 && y > 2 && x < g - 3 &&
                y < g - 3)
                v = white; /* the sparkle of the magic column */
            im[(y0 + y) * iw + x0 + x] = v;
        }
}
static const char *const bsd[] = {
    "Gemfall",
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
    "   notice, this list of conditions and the following disclaimer in the",
    "   documentation and/or other materials provided with the distribution.",
    "",
    "3. Neither the name of the copyright holder nor the names of its",
    "   contributors may be used to endorse or promote products derived from",
    "   this software without specific prior written permission.",
    "",
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS",
    "IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED",
    "TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A",
    "PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT",
    "HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,",
    "SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED",
    "TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR",
    "PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF",
    "LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING",
    "NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS",
    "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.",
    0};
/* the Info window of the game, and its licence */
static void draw_info(void) {
    int en = vx_english(), W = vx_cols(), H = vx_rows();
    uint8_t bg = VX_ATTR(VC_PANEL, VC_GLOW), hi = VX_ATTR(VC_PANEL, VC_HOT),
            dim = VX_ATTR(VC_PANEL, VC_MID);
    int bw = info == 2 ? (W - 2 < 74 ? W - 2 : 74) : 40, bh = info == 2 ? H - 2 : 11;
    int x = (W - bw) / 2, y = (H - bh) / 2;
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++)
            vx_put(x + i, y + j, ' ', bg);
    if (info == 1) {
        const char *l[] = {"Gemfall", "Version 1.0", "(c) 2026 Merecete",
                           en ? "Licence: BSD-3-Clause" : "Lizenz: BSD-3-Clause"};
        int ly[] = {1, 3, 5, 6};
        for (int i = 0; i < 4; i++)
            vx_text(x + (bw - slen(l[i])) / 2, y + ly[i], l[i], i ? bg : hi);
        const char *h = en ? "L licence   Enter back" : "L Lizenz   Enter zurueck";
        vx_text(x + (bw - slen(h)) / 2, y + 9, h, dim);
        return;
    }
    int n = 0, rows = bh - 2;
    while (bsd[n])
        n++;
    if (lic_top > n - rows)
        lic_top = n - rows;
    if (lic_top < 0)
        lic_top = 0;
    for (int r = 0; r < rows && lic_top + r < n; r++)
        vx_text(x + 2, y + r, bsd[lic_top + r], lic_top + r ? bg : hi);
    vx_text(x + 2, y + bh - 1,
            en ? "Up/Down scroll   Enter back" : "Hoch/Runter blaettern   Enter zurueck", dim);
}
static void draw(void) {
    uint8_t text = VX_ATTR(VC_BLACK, VC_GLOW), dim = VX_ATTR(VC_BLACK, VC_MID),
            frame = VX_ATTR(VC_BLACK, VC_VIOLET);
    int en = vx_english(), W = vx_cols(), H = vx_rows();
    /* the gem size in pixels: the well as high as the window allows; a pixel is a quarter of a
     * cell wide and an eighth high, so a gem of g pixels is square on the screen */
    int g = (H - 2) * 8 / FH;
    if (g > IMW / FW)
        g = IMW / FW;
    while (g > 6 && (FW * g + 3) / 4 + 2 + 26 > W)
        g--;
    int iw = (FW * g + 3) / 4 * 4, ih = (FH * g + 7) / 8 * 8, cw = iw / 4, ch = ih / 8;
    x0_ = (W - (cw + 2 + 26)) / 2 + 1;
    if (x0_ < 1)
        x0_ = 1;
    y0_ = (H - ch - 1) / 2;
    if (y0_ < 0)
        y0_ = 0;
    vx_clear(dim);
    uint8_t bgc = vx_rgb(8, 6, 15), dot = vx_rgb(40, 30, 60);
    for (int i = 0; i < iw * ih; i++)
        img[i] = bgc;
    int ox = (iw - FW * g) / 2, oy = ih - FH * g; /* the well sits at the bottom of the picture */
    int blink = flash && (flash / 3) % 2;
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            if (well[y][x])
                gem_px(img, iw, ox + x * g, oy + y * g, g, well[y][x], blink && mark[y][x]);
            else
                img[(oy + y * g + g / 2) * iw + ox + x * g + g / 2] = dot;
    if (px >= 0 && !over)
        for (int i = 0; i < 3; i++)
            if (py + i >= 0)
                gem_px(img, iw, ox + px * g, oy + (py + i) * g, g, pg[i], 0);
    vx_image(img, iw, ih);
    for (int j = 0; j < ch; j++)
        for (int i = 0; i < cw; i++)
            vx_pixcell(x0_ + i, y0_ + j, i, j);
    /* its frame */
    int ww = cw;
    for (int y = 0; y < ch; y++) {
        vx_put(x0_ - 1, y0_ + y, (char)0xBA, frame);
        vx_put(x0_ + ww, y0_ + y, (char)0xBA, frame);
    }
    int by_ = y0_ + ch;
    vx_put(x0_ - 1, by_, (char)0xC8, frame);
    vx_put(x0_ + ww, by_, (char)0xBC, frame);
    for (int x = 0; x < ww; x++)
        vx_put(x0_ + x, by_, (char)0xCD, frame);
    /* the side: the next column, in the same size when there is room */
    int sx = x0_ + ww + 4, y = y0_, ng = g;
    while (ng > 6 && y + 3 + (3 * ng + 7) / 8 + 13 > H)
        ng--;
    if (ng > 20)
        ng = 20;
    int nw = (ng + 3) / 4 * 4, nh = (3 * ng + 7) / 8 * 8;
    vx_text(sx, y, "GEMFALL", VX_ATTR(VC_BLACK, VC_HOT));
    vx_text(sx, y + 2, en ? "Next" : "Als Naechstes", dim);
    uint8_t black = vx_rgb(8, 6, 15);
    for (int i = 0; i < nw * nh; i++)
        nimg[i] = black;
    for (int i = 0; i < 3; i++)
        gem_px(nimg, nw, 0, i * ng, ng, nx[i], 0);
    vx_image_slot(1, nimg, nw, nh);
    for (int j = 0; j < nh / 8; j++)
        for (int i = 0; i < nw / 4; i++)
            vx_pixcell_slot(sx + 1 + i, y + 3 + j, 1, i, j);
    y += 3 + nh / 8 + 1;
    vx_text(sx, y, en ? "Score" : "Punkte", dim);
    vx_number(sx + 9, y, score, text);
    vx_text(sx, y + 1, en ? "Best" : "Rekord", dim);
    vx_number(sx + 9, y + 1, best, text);
    vx_text(sx, y + 2, "Level", dim);
    vx_number(sx + 9, y + 2, level, text);
    vx_text(sx, y + 3, en ? "Gems" : "Steine", dim);
    vx_number(sx + 9, y + 3, gems, text);
    if (flash && chain > 1) {
        vx_text(sx, y + 4, en ? "Chain" : "Kette", VX_ATTR(VC_BLACK, VC_HOT));
        vx_number(sx + 9, y + 4, chain, VX_ATTR(VC_BLACK, VC_HOT));
    }
    if (W - sx >= 22 && y + 8 < H) {
        vx_text(sx, y + 5, en ? "Left/Right  move" : "Links/Rechts  bewegen", dim);
        vx_text(sx, y + 6, en ? "Up, Space   roll" : "Hoch, Leer    drehen", dim);
        vx_text(sx, y + 7, en ? "Down        faster" : "Runter        schneller", dim);
        vx_text(sx, y + 8, en ? "P pause" : "P Pause", dim);
    }
    if (paused || over) { /* a dark panel over the well for the message */
        int bx = x0_ - 1, by = y0_ + ch / 2 - 2, bw = ww + 2;
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < bw; i++)
                vx_put(bx + i, by + j, ' ', frame);
        for (int i = 1; i < bw - 1; i++) {
            vx_put(bx + i, by, (char)0xCD, frame);
            vx_put(bx + i, by + 4, (char)0xCD, frame);
        }
        const char *a = paused ? "Pause" : en ? "Game over" : "Vorbei";
        const char *b = paused ? (en ? "P: go on" : "P: weiter")
                        : en   ? "Enter: again"
                               : "Enter: neu";
        vx_text(bx + (bw - slen(a)) / 2, by + 1, a, VX_ATTR(VC_BLACK, VC_HOT));
        vx_text(bx + (bw - slen(b)) / 2, by + 3, b, text);
    }
    if (info)
        draw_info();
    vx_show();
}

int main(void) {
    vx_read("best", &best, sizeof best);
    colours();
    vx_menu("Info|Pause (P)");
    new_game();
    for (;;) {
        long next = vx_ms() + FRAME_MS;
        int k;
        while (vx_ms() < next) {
            vx_wait(next - vx_ms());
            while ((k = vx_key()) != 0)
                key(k);
        }
        step();
        if (score > best) {
            best = score;
            vx_write("best", &best, sizeof best);
        }
        draw();
    }
}
