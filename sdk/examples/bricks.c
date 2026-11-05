/* Bricks - a block breaking game for Violyx.
   SPDX-License-Identifier: MIT
   Left/Right or A/D move the paddle, Space launches the ball, P pauses. */
#include "violyx.h"

#define COLS 12            /* bricks per row */
#define ROWS 6
#define BW 4               /* brick width in cells */
#define FW 60              /* field size in cells */
#define FH 18
#define FIX 256            /* the ball position is stored in 1/256 of a cell */
#define KEY_LEFT 0x4B      /* scan codes for vx_held */
#define KEY_RIGHT 0x4D
#define KEY_A 0x1E
#define KEY_D 0x20

static char bricks[ROWS][COLS];
static int paddle, width, wide_time;
static int ball_x, ball_y, ball_dx, ball_dy, stuck;
static int cap_x, cap_y;   /* falling capsule, cap_y < 0 means none */
static int score, lives, level, left;
static long best;

static const uint8_t colours[ROWS] = { VC_HOT, VC_ORCHID, VC_NEON, VC_VIOLET, VC_LAVENDER, VC_GLOW };

static void build_level(void) {
    left = 0;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) {
            int on = 1;
            if (level % 3 == 2) on = (x + y) % 2 == 0;          /* chess board */
            if (level % 3 == 0) on = x != 5 && x != 6;          /* two towers */
            bricks[y][x] = (char)on;
            left += on;
        }
}

static void new_ball(void) {
    stuck = 1;
    ball_dx = 90;
    ball_dy = -(55 + level * 5);   /* cells are tall, so vertical speed is lower */
    cap_y = -1;
}

static void new_game(void) {
    score = 0; lives = 3; level = 1;
    paddle = FW / 2 - 4; width = 9; wide_time = 0;
    build_level();
    new_ball();
}

static void hit_brick(int r, int c) {
    bricks[r][c] = 0;
    left--;
    score += (ROWS - r) * 10;
    if (cap_y < 0 && vx_random(6) == 0) { cap_x = c * BW + 8; cap_y = (r + 2) * FIX; }
}

/* Moves the ball one small step and bounces it. Returns 0 when it is lost. */
static int move_ball(void) {
    if (stuck) {
        ball_x = (paddle + width / 2) * FIX;
        ball_y = (FH - 2) * FIX;
        return 1;
    }
    int nx = ball_x + ball_dx / 2, ny = ball_y + ball_dy / 2;
    if (nx < 0 || nx >= FW * FIX) { ball_dx = -ball_dx; nx = ball_x; }
    if (ny < 0) { ball_dy = -ball_dy; ny = ball_y; }

    int cx = nx / FIX, cy = ny / FIX;
    int r = cy - 2, c = (cx - 6) / BW;
    if (cx >= 6 && r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c]) {
        hit_brick(r, c);
        if (ball_y / FIX != cy) ball_dy = -ball_dy; else ball_dx = -ball_dx;
        return 1;
    }
    if (ball_dy > 0 && cy == FH - 1 && cx >= paddle && cx < paddle + width) {
        int middle = paddle + width / 2;
        ball_dx = (cx - middle) * 200 / width * 2 + (ball_dx > 0 ? 8 : -8);
        ball_dy = -ball_dy;
        ny = (FH - 2) * FIX;
    }
    ball_x = nx;
    ball_y = ny;
    return cy < FH;
}

static void update(void) {
    if (vx_held(KEY_LEFT) || vx_held(KEY_A)) paddle -= 2;
    if (vx_held(KEY_RIGHT) || vx_held(KEY_D)) paddle += 2;
    if (paddle < 0) paddle = 0;
    if (paddle > FW - width) paddle = FW - width;

    if (wide_time && --wide_time == 0) width = 9;
    if (cap_y >= 0) {
        cap_y += 40;
        if (cap_y / FIX >= FH - 1) {
            if (cap_x >= paddle && cap_x < paddle + width) { width = 15; wide_time = 300; score += 50; }
            cap_y = -1;
        }
    }
    for (int i = 0; i < 2; i++)
        if (!move_ball()) {
            if (--lives > 0) new_ball();
            return;
        }
    if (!left) { level++; build_level(); new_ball(); }
}

static void draw(int state) {
    uint8_t text = VX_ATTR(VC_BLACK, VC_GLOW), dim = VX_ATTR(VC_BLACK, VC_MID);
    vx_clear(dim);
    vx_text(0, 0, vx_english() ? "Score" : "Punkte", dim);  vx_number(7, 0, score, text);
    vx_text(16, 0, vx_english() ? "Best" : "Rekord", dim);  vx_number(23, 0, best, text);
    vx_text(34, 0, "Level", dim);                           vx_number(40, 0, level, text);
    vx_text(46, 0, vx_english() ? "Lives" : "Leben", dim);  vx_number(53, 0, lives, text);

    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (bricks[y][x])
                for (int i = 0; i < BW - 1; i++)
                    vx_put(6 + x * BW + i, y + 3, (char)0xDB, VX_ATTR(VC_BLACK, colours[y]));
    for (int i = 0; i < width; i++)
        vx_put(paddle + i, FH, (char)0xDF, VX_ATTR(VC_BLACK, VC_NEON));
    vx_put(ball_x / FIX, ball_y / FIX + 1, 'O', text);
    if (cap_y >= 0) vx_put(cap_x, cap_y / FIX + 1, 'W', VX_ATTR(VC_BLACK, VC_HOT));

    if (state == 1) vx_text(20, 11, vx_english() ? "Space launches the ball" : "Leertaste startet den Ball", text);
    if (state == 2) vx_text(27, 11, "Pause", text);
    if (state == 3) vx_text(16, 11, vx_english() ? "Game over. Enter = again" : "Vorbei. Enter = nochmal", text);
    vx_show();
}

int main(void) {
    int paused = 0;
    vx_read("best", &best, sizeof best);
    new_game();
    for (;;) {
        long next = vx_ms() + 40;
        int key;
        while (vx_ms() < next) {
            vx_wait(next - vx_ms());
            while ((key = vx_key()) != 0) {
                if (key == ' ') stuck = 0;
                if (key == 'p' || key == 'P') paused = !paused;
                if (key == VK_ENTER && lives <= 0) new_game();
            }
        }
        if (lives > 0 && !paused) update();
        if (score > best) { best = score; vx_write("best", &best, sizeof best); }
        draw(lives <= 0 ? 3 : paused ? 2 : stuck ? 1 : 0);
    }
}
