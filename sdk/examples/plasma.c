/* Screen saver "Plasma": slowly flowing waves of violet, pink and blue light. Built with --saver
 * (see starfield.c). Without a maths library: the sine table comes from a rotation. */
#include "violyx.h"
static int sn[1024]; /* sin over a full turn in 1024 steps, times 4096 */
static void table(void) {
    /* rotation by 2*pi/1024 per step: c' = c*cos - s*sin, s' = s*cos + c*sin, in 1/2^30 */
    long long c = 1LL << 30, s = 0, cs = 1073721611LL, sns = 6588356LL;
    for (int i = 0; i < 1024; i++) {
        sn[i] = (int)(s >> 18);
        long long c2 = (c * cs - s * sns) >> 30, s2 = (s * cs + c * sns) >> 30;
        c = c2;
        s = s2;
    }
}
static int S(int a) {
    return sn[a & 1023];
}
int main(void) {
    long sz = vx_saver_size();
    if (sz < 0) {
        vx_clear(VX_ATTR(VC_BLACK, VC_LAVENDER));
        vx_text(2, 1, vx_english() ? "Plasma is a screen saver." : "Plasma ist ein Bildschirmschoner.",
                VX_ATTR(VC_BLACK, VC_GLOW));
        vx_text(2, 3, vx_english() ? "Choose it in the settings under Screen saver."
                                   : "Waehle ihn in den Einstellungen unter Bildschirmschoner.",
                VX_ATTR(VC_BLACK, VC_LAVENDER));
        vx_show();
        vx_wait(-1);
        return 0;
    }
    table();
    /* a quarter of the screen: the waves are soft anyway */
    int w = (int)(sz >> 16) / 4, h = (int)(sz & 0xFFFF) / 4;
    uint8_t *px = vx_alloc((size_t)w * h);
    uint8_t pal[64];
    for (int i = 0; i < 64; i++) { /* deep violet, pink, light blue and back */
        int a = S(i * 16) / 32 + 128, b = S(i * 16 + 341) / 32 + 128, c = S(i * 16 + 682) / 32 + 128;
        pal[i] = vx_rgb(60 + a * 3 / 5, 10 + b / 4, 90 + c * 3 / 5 > 255 ? 255 : 90 + c * 3 / 5);
    }
    if (!px)
        return 0;
    for (;;) {
        int t = (int)(vx_ms() / 25);
        for (int y = 0; y < h; y++) {
            int vy = S(y * 1024 / h * 2 + t * 3);
            for (int x = 0; x < w; x++) {
                int v = S(x * 1024 / w * 3 + t * 2) + vy + S((x + y) * 1024 / (w + h) * 2 - t * 4) +
                        S(x * 7 - y * 5 + t * 5);
                px[y * w + x] = pal[((v + 16384) >> 9) & 63];
            }
        }
        vx_saver_show(px, w, h);
        vx_wait(40);
    }
}
