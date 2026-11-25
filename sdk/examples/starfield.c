/* Screen saver "Sternenflug": a flight through violet stars. Built with --saver it appears in the
 * settings under Screen saver; Violyx starts it in full screen and ends it at the first key. */
#include "violyx.h"
#define NS 600
static struct {
    int x, y, z; /* z: distance, 1-4096 */
} st[NS];
static void place(int i, int far) {
    st[i].x = vx_random(8000) - 4000;
    st[i].y = vx_random(8000) - 4000;
    st[i].z = far ? 4096 : 64 + vx_random(4032);
}
/* the colour of a star: far ones dim violet, near ones bright and almost white */
static uint8_t tone(int z) {
    int k = 255 - z * 255 / 4096;
    return vx_rgb(k * 3 / 4 + 40 > 255 ? 255 : k * 3 / 4 + 40, k / 2, k > 200 ? 255 : k + 55);
}
int main(void) {
    long sz = vx_saver_size();
    if (sz < 0) { /* started as a program: say what it is */
        vx_clear(VX_ATTR(VC_BLACK, VC_LAVENDER));
        vx_text(2, 1, vx_english() ? "Starfield is a screen saver." : "Sternenflug ist ein Bildschirmschoner.",
                VX_ATTR(VC_BLACK, VC_GLOW));
        vx_text(2, 3, vx_english() ? "Choose it in the settings under Screen saver."
                                   : "Waehle ihn in den Einstellungen unter Bildschirmschoner.",
                VX_ATTR(VC_BLACK, VC_LAVENDER));
        vx_show();
        vx_wait(-1);
        return 0;
    }
    /* half the size of the screen: fast enough, Violyx stretches it */
    int w = (int)(sz >> 16) / 2, h = (int)(sz & 0xFFFF) / 2;
    uint8_t *px = vx_alloc((size_t)w * h);
    if (!px)
        return 0;
    for (int i = 0; i < NS; i++)
        place(i, 0);
    long last = vx_ms();
    for (;;) {
        long now = vx_ms(), dt = now - last;
        last = now;
        if (dt > 100)
            dt = 100;
        memset(px, 16, (size_t)w * h); /* 16: black of the colour cube */
        for (int i = 0; i < NS; i++) {
            st[i].z -= (int)(dt * 2);
            if (st[i].z < 16)
                place(i, 1);
            int sx = w / 2 + st[i].x * (w / 2) / st[i].z, sy = h / 2 + st[i].y * (w / 2) / st[i].z;
            if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
                place(i, 1);
                continue;
            }
            uint8_t c = tone(st[i].z);
            int r = st[i].z < 700 ? 2 : st[i].z < 1800 ? 1 : 0; /* near stars are bigger */
            for (int y = sy - r / 2; y <= sy + r / 2; y++)
                for (int x = sx - r / 2; x <= sx + (r + 1) / 2; x++)
                    if (x >= 0 && y >= 0 && x < w && y < h)
                        px[y * w + x] = c;
        }
        vx_saver_show(px, w, h);
        vx_wait(30);
    }
}
