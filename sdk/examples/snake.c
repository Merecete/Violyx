/* Snake as an installable program, with a saved high score */
#include "violyx.h"
static int bx[240 * 100], by[240 * 100], W, H;
int main(void) {
    uint8_t bg = VX_ATTR(VC_BLACK, VC_DIM), sn = VX_ATTR(VC_BLACK, VC_NEON),
            fr = VX_ATTR(VC_BLACK, VC_HOT), tx = VX_ATTR(VC_BLACK, VC_GLOW);
    long best = 0;
    vx_read("best", &best, sizeof best);
    vx_menu("*Info");
    for (;;) {
        W = vx_cols() - 2;
        H = vx_rows() - 3;
        int len = 4, dx = 1, dy = 0, score = 0, fx = W / 2, fy = H / 2;
        for (int i = 0; i < len; i++) {
            bx[i] = 10 - i;
            by[i] = H / 2;
        }
        int run = 1;
        while (run) {
            long end = vx_ms() + (180 - (score > 200 ? 80 : score * 2 / 5)) * (dy ? 17 : 10) /
                                     10; /* cells are almost twice as tall as wide */
            while (vx_ms() < end) {
                vx_wait(end - vx_ms());
                int k = vx_key();
                if (k == VK_UP && !dy) {
                    dx = 0;
                    dy = -1;
                } else if (k == VK_DOWN && !dy) {
                    dx = 0;
                    dy = 1;
                } else if (k == VK_LEFT && !dx) {
                    dx = -1;
                    dy = 0;
                } else if (k == VK_RIGHT && !dx) {
                    dx = 1;
                    dy = 0;
                }
            }
            if (vx_cols() - 2 != W || vx_rows() - 3 != H)
                break; /* window changed: new round in the new field */
            int nx = bx[0] + dx, ny = by[0] + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H)
                run = 0;
            for (int i = 0; i < len; i++)
                if (bx[i] == nx && by[i] == ny)
                    run = 0;
            if (!run) {
                vx_tone(196, 160, 55, VX_SAW, 0);
                vx_tone(131, 320, 55, VX_SAW, 160);
                break;
            }
            int grow = nx == fx && ny == fy;
            if (grow) {
                vx_tone(660, 60, 45, VX_SQUARE, 0);
                vx_tone(880, 80, 45, VX_SQUARE, 60);
                len++;
                score += 10;
                fx = vx_random(W);
                fy = vx_random(H);
            }
            for (int i = len - 1; i > 0; i--) {
                bx[i] = bx[i - 1];
                by[i] = by[i - 1];
            }
            bx[0] = nx;
            by[0] = ny;
            vx_clear(bg);
            vx_text(1, 0, vx_english() ? "Score" : "Punkte", tx);
            vx_number(8, 0, score, tx);
            vx_text(16, 0, vx_english() ? "Best" : "Rekord", tx);
            vx_number(23, 0, best, tx);
            for (int x = 0; x < W + 2; x++) {
                vx_put(x, 1, (char)0xC4, bg);
                vx_put(x, H + 2, (char)0xC4, bg);
            }
            vx_put(fx + 1, fy + 2, (char)0x04, fr);
            for (int i = 0; i < len; i++)
                vx_put(bx[i] + 1, by[i] + 2, (char)0xDB, i ? sn : VX_ATTR(VC_BLACK, VC_GLOW));
            vx_show();
        }
        if (score > best) {
            best = score;
            vx_write("best", &best, sizeof best);
        }
        if (vx_cols() - 2 != W || vx_rows() - 3 != H)
            continue;
        vx_text(W / 2 - 10, H / 2 + 1,
                vx_english() ? "Game over. Enter = again" : "Vorbei. Enter = nochmal", tx);
        vx_show();
        for (;;) {
            vx_wait(-1);
            if (vx_key() == VK_ENTER)
                break;
        }
    }
}
