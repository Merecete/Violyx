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
    static const u8 cl[] = {L_ROW1, L_ROW2, L_ROW3, L_ROW4, L_ROW5};
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
                            limg[(v + 1) * lw + u + 1] = L_SHADE;
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
/* ---------- a picture from the image viewer ----------
 * Stored as it was decoded for the screen: picture cells of 4x8 pixels in 256 colours, scaled to
 * another screen size when needed. The file keeps where the picture came from. */
#define PFILE "/system/wallpaper.vpb"
#define PID   250 /* picture id of the background, programs get 1-249 */
static u8 *ppx, *pscr;       /* as stored, and scaled to the screen */
static int pc, pr, pon, pok; /* size in cells, set, file there */
static char psrc[VFS_PATH];
static void photo_scale(void) {
    gfx_forget(PID); /* the screen must not draw from the old memory any more */
    kfree(pscr);
    pscr = 0;
    if (!ppx)
        return;
    int w = W * 4, h = H * 8, sw = pc * 4, sh = pr * 8;
    pscr = kmalloc((u64)w * h);
    if (!pscr)
        return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            pscr[y * w + x] = ppx[(y * sh / h) * sw + x * sw / w];
}
static int photo_keep(const u8 *px, int cols, int rows, const char *src) {
    u8 *n = kmalloc((u64)cols * rows * 32);
    if (!n)
        return VE_NOSPC;
    memcpy(n, px, (u64)cols * rows * 32);
    kfree(ppx);
    ppx = n;
    pc = cols;
    pr = rows;
    strlcpy_(psrc, src, VFS_PATH);
    pon = 1;
    photo_scale();
    wall_photo_check();
    return 0;
}
int wall_photo_set(const u8 *px, int cols, int rows, const char *src) {
    if (cols < 1 || rows < 1 || cols > 240 || rows > 100)
        return VE_INVAL;
    int r = photo_keep(px, cols, rows, src);
    if (r || !fs_ok())
        return r;
    u64 n = 8 + VFS_PATH + (u64)cols * rows * 32;
    u8 *b = kmalloc(n);
    if (!b)
        return VE_NOSPC;
    memset(b, 0, 8 + VFS_PATH);
    memcpy(b, "VPB1", 4);
    b[4] = (u8)cols, b[5] = (u8)(cols >> 8), b[6] = (u8)rows, b[7] = (u8)(rows >> 8);
    strlcpy_((char *)b + 8, src, VFS_PATH);
    memcpy(b + 8 + VFS_PATH, px, (u64)cols * rows * 32);
    r = vfs_write(PFILE, b, n, fs_now());
    kfree(b);
    return r;
}
void wall_photo_clear(void) {
    if (!pon)
        return;
    pon = pok = 0;
    gfx_forget(PID);
    kfree(ppx);
    kfree(pscr);
    ppx = pscr = 0;
    if (fs_ok())
        vfs_remove(PFILE);
}
void wall_photo_load(void) {
    vfs_ent e;
    if (!fs_ok() || vfs_stat(PFILE, &e) || e.size < 8 + VFS_PATH || e.size > 8 + VFS_PATH + 768000)
        return;
    u8 *b = kmalloc(e.size);
    if (!b)
        return;
    int64_t n = vfs_read(PFILE, b, e.size);
    int c = b[4] | b[5] << 8, r = b[6] | b[7] << 8;
    b[8 + VFS_PATH - 1] = 0;
    if (n == (int64_t)e.size && !memcmp(b, "VPB1", 4) && c >= 1 && r >= 1 && c <= 240 &&
        r <= 100 && (u64)n == 8 + VFS_PATH + (u64)c * r * 32)
        photo_keep(b + 8 + VFS_PATH, c, r, (const char *)b + 8);
    kfree(b);
}
void wall_photo_check(void) {
    pok = pon && pscr && fsx_exists(psrc);
}
const char *wall_photo_name(void) {
    if (!pon)
        return 0;
    const char *b = strrchr(psrc, '/');
    return b ? b + 1 : psrc;
}
static int pw_, ph_;
void wall_paint(void) {
    if (pok) {
        if (pw_ != W || ph_ != H) {
            pw_ = W, ph_ = H;
            photo_scale();
        }
        if (pscr) {
            int of = ui_fixed;
            ui_fixed = 1;
            for (int y = 1; y < H - 1; y++)
                for (int x = 0; x < W; x++)
                    gput(x, y, PID, pscr, W * 4, x, y);
            ui_fixed = of;
            return;
        }
    }
    if (cfg_wall < 6) { /* the background of the settings */
        fill(0, 1, W, H - 2, 0xB1, AT(C_WALL, C_DESK));
        return;
    }
    int of = ui_fixed;
    ui_fixed = wkind == 1 ? 3 : 1; /* the logo with its own colours */
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
    wall_photo_clear();
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
