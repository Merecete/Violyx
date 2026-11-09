/* Bricks - a block breaking game for Violyx.
   SPDX-License-Identifier: MIT
   Left/Right or A/D move the paddle, Space launches the ball, P pauses, M switches the music.
   Hit the ball with the paddle edge to speed it up: a fast ball smashes through several bricks.
   16 levels with tough bricks (two hits) and solid ones that never break; after the last level the
   walls come back faster. */
#include "violyx.h"
#include "chip.h"

#define COLS      12 /* bricks per row */
#define ROWS      8
#define FIX       256  /* the ball position is stored in 1/256 of a cell */
#define KEY_LEFT  0x4B /* scan codes for vx_held */
#define KEY_RIGHT 0x4D
#define KEY_A     0x1E
#define KEY_D     0x20
#define MAXSPEED  7
#define FRAME_MS  25

static int FW = 60, FH = 18; /* field size in cells, follows the window */
static int bw = 4, x0_ = 6;  /* brick width and left edge of the wall */
static char bricks[ROWS][COLS];
static int paddle, width, wide_time, base_width = 9;
static int ball_x, ball_y, ball_dx, ball_dy, stuck;
static int speed, punch,
    drop; /* speed 1..MAXSPEED sets steps per frame, punch counts bricks left to smash through */
static int cap_x, cap_y; /* falling capsule, cap_y < 0 means none */
static int score, lives, level, left;
static long best;

static const chip_song theme = {
    "c5 - e5 g5 - e5 c5 - a4 - c5 e5 - c5 a4 - f4 - a4 c5 - a4 f4 - g4 - b4 d5 - b4 g4 -",
    "c3 . g3 . c3 . g3 . a2 . e3 . a2 . e3 . f2 . c3 . f2 . c3 . g2 . d3 . g2 . b2 .",
    "k.h.s.h.k.h.s.h.", 110};

static const uint8_t colours[ROWS] = {VC_HOT,      VC_ORCHID, VC_NEON, VC_VIOLET,
                                      VC_LAVENDER, VC_GLOW,   VC_HOT,  VC_ORCHID};

/* The walls: # brick, = tough brick (two hits), X solid (the ball bounces off, it never breaks).
   Every brick can be reached, solid ones never close a brick in. */
#define NLEVELS 16
static const char *const levels[NLEVELS][ROWS] = {
    {"............", "############", "############", "############", "############", "............",
     "............", "............"},
    {"............", "#.#.#.#.#.#.", ".#.#.#.#.#.#", "#.#.#.#.#.#.", ".#.#.#.#.#.#", "#.#.#.#.#.#.",
     "............", "............"},
    {"============", "###..##..###", "###..##..###", "###..##..###", "###..##..###", "............",
     "............", "............"},
    {"............", "############", "############", "XX..XXXX..XX", "############", "############",
     "............", "............"},
    {".....##.....", "....####....", "...######...", "..##====##..", "...######...", "....####....",
     ".....##.....", "............"},
    {"X..........X", "X.########.X", "X.#======#.X", "X.#======#.X", "X.########.X", "X..........X",
     "............", "............"},
    {"============", "............", "############", "............", "XXXX....XXXX", "############",
     "............", "............"},
    {"..#......#..", "...#....#...", "..########..", ".##.####.##.", "############", "#.########.#",
     "#.#......#.#", "...##..##..."},
    {"############", "#X#X#X#X#X#X", "############", "====....====", "############", "............",
     "............", "............"},
    {"............", ".XXXX..XXXX.", ".#==#..#==#.", ".#==#..#==#.", ".####..####.", "............",
     "............", "............"},
    {"..##....##..", ".####..####.", "############", "############", ".##########.", "..########..",
     "...######...", "....####...."},
    {"============", "XXX.........", "....XXXXX...", "............", "############", "############",
     "............", "............"},
    {"XXXXX..XXXXX", "X====..====X", "X##########X", "X##########X", "X##......##X", "............",
     "............", "............"},
    {"#=#=#=#=#=#=", "X.X.X.X.X.X.", "############", "=#=#=#=#=#=#", ".X.X.X.X.X.X", "############",
     "............", "............"},
    {"====XXXX====", "####....####", "####.##.####", "..XX.##.XX..", "####.##.####", "############",
     "............", "............"},
    {"XX========XX", "X##########X", "X#XX####XX#X", "X##########X", "X###.XX.###X", "XX###..###XX",
     "............", "............"}};
enum {
    B_NONE,
    B_NORMAL,
    B_TOUGH,
    B_SOLID
};
static int steps; /* small steps since the ball last touched the paddle or a brick */

static void build_level(void) {
    left = 0;
    const char *const *l = levels[(level - 1) % NLEVELS];
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) {
            char c = l[y][x];
            int t = c == '#' ? B_NORMAL : c == '=' ? B_TOUGH : c == 'X' ? B_SOLID : B_NONE;
            bricks[y][x] = (char)t;
            if (t == B_NORMAL || t == B_TOUGH)
                left++;
        }
}

static void new_ball(void) {
    stuck = 1;
    ball_dx = 90;
    ball_dy = -60;                     /* cells are tall, so vertical speed is lower */
    int round = (level - 1) / NLEVELS; /* each pass through all levels is faster */
    speed = 1 + (level > 5 ? 2 : level / 2) + round;
    if (speed > MAXSPEED - 2)
        speed = MAXSPEED - 2;
    punch = 1;
    cap_y = -1;
}

static void new_game(void) {
    score = 0;
    lives = 3;
    level = 1;
    paddle = FW / 2 - 4;
    width = 9;
    wide_time = 0;
    build_level();
    new_ball();
}

static void hit_brick(int r, int c) {
    if (bricks[r][c] == B_TOUGH) { /* cracks first, breaks on the next hit */
        bricks[r][c] = B_NORMAL;
        score += 5;
        vx_tone(196, 50, 45, VX_SQUARE, 0);
        return;
    }
    bricks[r][c] = 0;
    left--;
    score += (ROWS - r) * 10 * (speed + 1) / 2;
    vx_tone(440 + (ROWS - r) * 110 + drop * 60, 60, 45, VX_SQUARE, drop * 25);
    drop++;
    if (cap_y < 0 && vx_random(6) == 0) {
        cap_x = x0_ + c * bw + 1;
        cap_y = (r + 2) * FIX;
    }
}

/* Moves the ball one small step and bounces it. Returns 0 when it is lost. */
static int move_ball(void) {
    if (stuck) {
        ball_x = (paddle + width / 2) * FIX;
        ball_y = (FH - 2) * FIX;
        return 1;
    }
    int nx = ball_x + ball_dx / 4, ny = ball_y + ball_dy / 4;
    if (nx < 0 || nx >= FW * FIX) {
        ball_dx = -ball_dx;
        nx = ball_x;
        vx_tone(220, 30, 30, VX_TRIANGLE, 0);
    }
    if (ny < 0) {
        ball_dy = -ball_dy;
        ny = ball_y;
        vx_tone(220, 30, 30, VX_TRIANGLE, 0);
    }

    int cx = nx / FIX, cy = ny / FIX;
    int r = cy - 2, c = (cx - x0_) / bw;
    if (cx >= x0_ && r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c] == B_SOLID) {
        /* solid: always bounces, also a fast ball */
        punch = 1;
        steps = 0;
        if (ball_y / FIX != cy)
            ball_dy = -ball_dy;
        else
            ball_dx = -ball_dx;
        vx_tone(1047, 25, 35, VX_TRIANGLE, 0);
        return 1;
    }
    if (cx >= x0_ && r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c]) {
        steps = 0;
        int tough = bricks[r][c] == B_TOUGH;
        hit_brick(r, c);
        if (tough) { /* a tough brick stops the ball like a wall */
            punch = 1;
            if (ball_y / FIX != cy)
                ball_dy = -ball_dy;
            else
                ball_dx = -ball_dx;
            return 1;
        }
        /* A fast ball keeps flying: it only bounces once its punch is used up */
        if (--punch > 0) {
            ball_x = nx;
            ball_y = ny;
            return 1;
        }
        punch = 1;
        if (ball_y / FIX != cy)
            ball_dy = -ball_dy;
        else
            ball_dx = -ball_dx;
        return 1;
    }
    /* Bouncing between solid bricks and the walls for a long time: a little nudge sideways */
    if (++steps > 6000) {
        steps = 0;
        ball_dx += ball_dx > 0 ? 23 : -23;
        if (ball_dx > 250 || ball_dx < -250)
            ball_dx = ball_dx > 0 ? 40 : -40;
    }
    if (ball_dy > 0 && cy == FH - 1 && cx >= paddle && cx < paddle + width) {
        steps = 0;
        int middle = paddle + width / 2;
        int off = cx - middle;
        if (off < 0)
            off = -off;
        ball_dx = (cx - middle) * 200 / width * 2 + (ball_dx > 0 ? 8 : -8);
        ball_dy = -ball_dy;
        ny = (FH - 2) * FIX;
        /* Hitting with the paddle edge is a smash, the middle slows the ball down again */
        if (off * 3 >= width)
            speed++;
        else if (off * 6 < width && speed > 1)
            speed--;
        if (speed > MAXSPEED)
            speed = MAXSPEED;
        punch = 1 + (speed + 1) / 2;
        drop = 0;
        vx_tone(off * 3 >= width ? 330 : 165, 70, 55, VX_SQUARE, 0);
    }
    ball_x = nx;
    ball_y = ny;
    return cy < FH;
}

/* Follows the window size. The game keeps running when the window is zoomed with F5. */
static void fit_window(void) {
    FW = vx_cols();
    if (FW < 26)
        FW = 26; /* the whole wall must stay reachable */
    FH = vx_rows() - 1;
    bw = (FW - 2) / COLS;
    if (bw > 12)
        bw = 12;
    x0_ = (FW - bw * COLS) / 2;
    base_width = FW / 6;
    if (base_width < 9)
        base_width = 9;
    if (!wide_time)
        width = base_width;
    if (width > FW - 4)
        width = FW - 4;
    if (paddle > FW - width)
        paddle = FW - width;
    if (paddle < 0)
        paddle = 0;
    if (ball_x >= FW * FIX)
        ball_x = (FW - 1) * FIX;
    if (ball_y >= FH * FIX)
        ball_y = (FH - 2) * FIX;
    if (cap_x >= FW)
        cap_x = FW - 1;
    if (cap_y >= (FH - 1) * FIX)
        cap_y = -1;
}

static void update(void) {
    int ps = (FW + 20) / 30;
    if (vx_held(KEY_LEFT) || vx_held(KEY_A))
        paddle -= ps;
    if (vx_held(KEY_RIGHT) || vx_held(KEY_D))
        paddle += ps;
    if (paddle < 0)
        paddle = 0;
    if (paddle > FW - width)
        paddle = FW - width;

    /* The paddle runs at the same speed as before, the game itself is faster */
    if (wide_time && --wide_time == 0) {
        width = base_width;
        if (paddle > FW - width)
            paddle = FW - width;
    }
    if (cap_y >= 0) {
        cap_y += 28;
        if (cap_y / FIX >= FH - 1) {
            if (cap_x >= paddle && cap_x < paddle + width) {
                width = base_width * 3 / 2;
                wide_time = 450;
                score += 50;
                if (paddle > FW - width)
                    paddle = FW - width;
                vx_tone(880, 80, 50, VX_TRIANGLE, 0);
                vx_tone(1175, 120, 50, VX_TRIANGLE, 80);
            }
            cap_y = -1;
        }
    }
    for (int i = 0; i < 4 + speed; i++) {
        if (!move_ball()) {
            lives--;
            vx_tone(330, 120, 60, VX_SAW, 0);
            vx_tone(247, 120, 60, VX_SAW, 130);
            vx_tone(165, 260, 60, VX_SAW, 260);
            if (lives > 0)
                new_ball();
            return;
        }
        if (!left)
            break;
    }
    if (!left) {
        for (int i = 0; i < 4; i++)
            vx_tone(523 + i * 131, 110, 55, VX_SQUARE, i * 100);
        level++;
        build_level();
        new_ball();
    }
}

static void draw(int state) {
    uint8_t text = VX_ATTR(VC_BLACK, VC_GLOW), dim = VX_ATTR(VC_BLACK, VC_MID);
    vx_clear(dim);
    vx_text(0, 0, vx_english() ? "Score" : "Punkte", dim);
    vx_number(7, 0, score, text);
    vx_text(16, 0, vx_english() ? "Best" : "Rekord", dim);
    vx_number(23, 0, best, text);
    vx_text(34, 0, "Level", dim);
    vx_number(40, 0, level, text);
    vx_text(46, 0, vx_english() ? "Lives" : "Leben", dim);
    vx_number(53, 0, lives, text);
    if (FW >= 66) {
        vx_text(56, 0, "Power", dim);
        vx_number(62, 0, speed, text);
    }

    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) {
            int t = bricks[y][x];
            if (!t)
                continue;
            char c = t == B_NORMAL ? (char)0xDB : t == B_TOUGH ? (char)0xB2 : (char)0xB1;
            uint8_t a = VX_ATTR(VC_BLACK, t == B_NORMAL  ? colours[y]
                                          : t == B_TOUGH ? VC_GLOW
                                                         : VC_MID);
            for (int i = 0; i < bw - 1; i++)
                vx_put(x0_ + x * bw + i, y + 3, c, a);
        }
    for (int i = 0; i < width; i++)
        vx_put(paddle + i, FH, (char)0xDF, VX_ATTR(VC_BLACK, VC_NEON));
    vx_put(ball_x / FIX, ball_y / FIX + 1, punch > 1 ? (char)0x0F : 'O',
           punch > 1 ? VX_ATTR(VC_BLACK, VC_HOT) : text);
    if (cap_y >= 0)
        vx_put(cap_x, cap_y / FIX + 1, 'W', VX_ATTR(VC_BLACK, VC_HOT));

    if (state == 1)
        vx_text(FW / 2 - 12, FH / 2 + 2,
                vx_english() ? "Space launches the ball" : "Leertaste startet den Ball", text);
    if (state == 2)
        vx_text(FW / 2 - 3, FH / 2 + 2, "Pause", text);
    if (state == 3)
        vx_text(FW / 2 - 12, FH / 2 + 2,
                vx_english() ? "Game over. Enter = again" : "Vorbei. Enter = nochmal", text);
    vx_show();
}

int main(void) {
    int paused = 0;
    vx_read("best", &best, sizeof best);
    chip_load_setting();
    vx_menu(vx_english() ? "*Info|Music on/off (M)|Pause (P)" : "*Info|Musik an/aus (M)|Pause (P)");
    new_game();
    for (;;) {
        long next = vx_ms() + FRAME_MS;
        int key;
        while (vx_ms() < next) {
            vx_wait(next - vx_ms());
            while ((key = vx_key()) != 0) {
                if (key == ' ' && stuck) {
                    stuck = 0;
                    drop = 0;
                    punch = 1 + (speed + 1) / 2;
                    vx_tone(392, 60, 45, VX_SQUARE, 0);
                }
                if (key == 'p' || key == 'P')
                    paused = !paused;
                if (key == 'm' || key == 'M' || key == VK_MENU + 1)
                    chip_toggle();
                if (key == VK_MENU + 2)
                    paused = !paused;
                if (key == VK_ENTER && lives <= 0)
                    new_game();
            }
        }
        fit_window();
        if (lives > 0 && !paused) {
            update();
            if (!stuck) {
                chip_play(&theme);
                chip_tick();
            }
        }
        if (score > best) {
            best = score;
            vx_write("best", &best, sizeof best);
        }
        draw(lives <= 0 ? 3 : paused ? 2 : stuck ? 1 : 0);
    }
}
