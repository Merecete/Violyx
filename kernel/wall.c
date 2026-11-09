/* Backgrounds. A picture has 80x50 pixels: each text cell shows two almost square pixels with the
 * half-block character. Files: "VWP1" and 4000 colour bytes. Colours: 0-15 Violyx, 16-231 colour
 * cube, 232-255 grey. Pictures are stretched to any screen size. Paint runs in ring 3. */
#include "os.h"
#define PW    80
#define PH    50
#define WSZ   (PW * PH)
#define WFILE "/system/wallpaper.vwp"
static int wv_ok(const u8 *b, long n) {
    return n == WSZ + 4 && !memcmp(b, "VWP1", 4);
}
static u8 wimg[WSZ];
static int wkind;          /* 0 none, 1 logo, 2 own picture */
static u8 limg[240 * 200]; /* Logo at full grid resolution: W x 2H half cells */
static void logo(void) {
    static const char *l[] = {
        "#   #  ###   ###   #     #   #  #   #", "#   #   #   #   #  #      # #    # # ",
        "#   #   #   #   #  #       #      #  ", " # #    #   #   #  #       #     # # ",
        "  #    ###   ###   #####   #    #   #"};
    static const u8 cl[] = {C_GLOW, C_LAV, C_NEON, C_VIOLET, C_SEL};
    int lw = W, lh = H * 2;
    memset(limg, C_VOID, (u64)lw * lh);
    u32 r = 12345;
    int n = lw * lh / 90;
    for (int i = 0; i < n; i++) {
        r = r * 1103515245u + 12345u;
        int a = (r >> 16) % (lw * lh);
        r = r * 1103515245u + 12345u;
        int k = (r >> 16) & 7;
        limg[a] = k == 0 ? C_LAV : k < 3 ? C_DIM : k < 5 ? C_SEL : C_RAISE;
    }
    int s = lw / 54;
    if (s < 1)
        s = 1;
    if (s * 36 + 8 > lw)
        s = 1;
    int ox = (lw - 36 * s) / 2, oy = (lh - 5 * s) / 2;
    for (int y = -3; y < 8; y++)
        for (int x = -2; x < 38; x++)
            for (int j = 0; j < s; j++)
                for (int i = 0; i < s; i++) {
                    int u = ox + x * s + i, v = oy + y * s + j;
                    if (u >= 0 && u < lw && v >= 0 && v < lh)
                        limg[v * lw + u] = C_VOID;
                }
    for (int y = 0; y < 5; y++)
        for (int x = 0; l[y][x]; x++)
            if (l[y][x] == '#')
                for (int j = 0; j < s; j++)
                    for (int i = 0; i < s; i++) {
                        int u = ox + x * s + i, v = oy + y * s + j;
                        limg[v * lw + u] = cl[y];
                        if (u + 1 < lw && v + 1 < lh && limg[(v + 1) * lw + u + 1] == C_VOID)
                            limg[(v + 1) * lw + u + 1] = C_PANEL;
                    }
}
static int wfile(const char *p, u8 *out) {
    static u8 b[WSZ + 4];
    int64_t n = fsx_read(p, b, WSZ + 4);
    if (n < 0)
        return (int)n;
    if (!wv_ok(b, n))
        return VE_FORMAT;
    memcpy(out, b + 4, WSZ);
    return 0;
}
void wall_image(int i) {
    if (i == 6) {
        logo();
        wkind = 1;
    } else {
        if (wkind != 2 && !(fs_ok() && !wfile(WFILE, wimg)))
            memset(wimg, C_VOID, WSZ);
        wkind = 2;
    }
}
static int sxy(int x, int half) {
    return ((half * PH) / (H * 2)) * PW + (x * PW) / W;
}
/* Pictures keep their violet colours whatever the colour scheme. */
void wall_paint(void) {
    int of = ui_fixed;
    ui_fixed = 1;
    if (wkind == 1) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                put8(x, y, 0xDF, limg[y * 2 * W + x], limg[(y * 2 + 1) * W + x]);
    } else
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                put8(x, y, 0xDF, wimg[sxy(x, y * 2)], wimg[sxy(x, y * 2 + 1)]);
    ui_fixed = of;
}
int wall_use(const u8 *c) {
    memcpy(wimg, c, WSZ);
    wkind = 2;
    int r = 0;
    if (fs_ok()) {
        static u8 b[WSZ + 4];
        memcpy(b, "VWP1", 4);
        memcpy(b + 4, c, WSZ);
        r = vfs_write(WFILE, b, WSZ + 4, fs_now());
    }
    wall_set(7);
    cfg_save();
    return r;
}
int wall_open_file(const char *p) {
    static u8 c[WSZ];
    int r = wfile(p, c);
    if (r)
        return r;
    if (!dlg_confirm(TR("Hintergrund", "Background"),
                     TR("Dieses Bild als Hintergrund verwenden?", "Use this image as background?")))
        return VE_DENIED;
    return wall_use(c);
}
