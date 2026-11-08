/* Backgrounds and Paint. A picture has 80x50 pixels: each text cell shows two almost square pixels
 * with the half-block character. Files: "VWP1" and 4000 colour bytes. Colours: 0-15 Violyx, 16-231
 * colour cube, 232-255 grey. Pictures are stretched to any screen size. */
#ifdef HOST
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
#else
#include "os.h"
#endif
#define PW    80
#define PH    50
#define WSZ   (PW * PH)
#define WFILE "/system/wallpaper.vwp"
/* Fills the connected area. Each pixel goes on the stack at most once. */
static void pf_fill(u8 *c, int x, int y, u8 v) {
    static u16 st[WSZ];
    u8 o = c[y * PW + x];
    if (o == v)
        return;
    int n = 0;
    c[y * PW + x] = v;
    st[n++] = (u16)(y * PW + x);
    while (n) {
        int p = st[--n], a = p % PW, b = p / PW;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; i++) {
            int u = a + dx[i], w = b + dy[i];
            if (u < 0 || u >= PW || w < 0 || w >= PH || c[w * PW + u] != o)
                continue;
            c[w * PW + u] = v;
            st[n++] = (u16)(w * PW + u);
        }
    }
}
static int wv_ok(const u8 *b, long n) {
    return n == WSZ + 4 && !memcmp(b, "VWP1", 4);
}
#ifndef HOST
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
static int wall_use(const u8 *c) {
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
/* ---------- Paint ---------- */
static u8 cv[WSZ], und[WSZ];
static const u8 QP[16] = {16, 231, 244, 196, 208, 226, 46,     51,
                          21, 93,  201, 213, 94,  30,  C_NEON, C_VIOLET};
static const char QK[] = "0123456789abce";
static void pshow(int px, int py, int col, int pen) {
    int cur = py * PW + px;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int a = sxy(x, y * 2), b = sxy(x, y * 2 + 1);
            u8 t = cv[a], u = cv[b];
            if (a == cur)
                t = t == C_GLOW ? C_VOID : C_GLOW;
            if (b == cur)
                u = u == C_GLOW ? C_VOID : C_GLOW;
            put8(x, y, 0xDF, t, u);
        }
    int by = py >= PH - 6 ? 0 : H - 1;
    fill(0, by, W, 1, ' ', AT(C_RAISE, C_LAV));
    int x = 1;
    for (int i = 0; i < 14; i++) {
        put(x, by, QK[i], AT(C_RAISE, C_LAV));
        put8(x + 1, by, 0xDB, QP[i], C_RAISE);
        put8(x + 2, by, 0xDB, QP[i], C_RAISE);
        x += 3;
    }
    put8(x, by, 0xDB, col, C_RAISE);
    put8(x + 1, by, 0xDB, col, C_RAISE);
    put8(x + 2, by, 0xDB, col, C_RAISE);
    char s[40];
    ksnprintf(s, 40, " %d,%d %s Tab %s", px, py, pen ? "Pen" : "", TR("Farben", "Colours"));
    text(x + 3, by, AT(C_RAISE, C_GLOW), s);
    text(W - 18, by, AT(C_RAISE, C_MID), TR("F1 Hilfe  Esc Ende", "F1 Help  Esc Quit"));
    ui_show();
}
/* Colour picker with all 256 colours */
static int gridc(int c, int r) {
    if (r == 0)
        return c < 16 ? c : -1;
    if (r <= 6)
        return 16 + 36 * (r - 1) + c;
    return c < 24 ? 232 + c : -1;
}
static int ppick(int col) {
    int c = 0, r = 1;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 36; x++)
            if (gridc(x, y) == col) {
                c = x;
                r = y;
            }
    for (;;) {
        int bx = (W - 74) / 2, by = (H - 12) / 2;
        fill(bx, by, 74, 12, ' ', AT(C_RAISE, C_LAV));
        text(bx + 2, by, AT(C_RAISE, C_GLOW),
             TR("Farbe waehlen: Pfeile, Enter, Esc", "Choose colour: arrows, Enter, Esc"));
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 36; x++) {
                int v = gridc(x, y);
                if (v < 0)
                    continue;
                put8(bx + 1 + x * 2, by + 2 + y, x == c && y == r ? '[' : ' ', 231, (u8)v);
                put8(bx + 2 + x * 2, by + 2 + y, x == c && y == r ? ']' : ' ', 231, (u8)v);
            }
        int v = gridc(c, r);
        char s[20];
        ksnprintf(s, 20, "%d", v < 0 ? col : v);
        text(bx + 2, by + 11, AT(C_RAISE, C_LAV), s);
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ESC)
            return col;
        if (k.key == K_ENTER)
            return v < 0 ? col : v;
        if (k.key == K_LEFT && c)
            c--;
        else if (k.key == K_RIGHT && c < 35)
            c++;
        else if (k.key == K_UP && r)
            r--;
        else if (k.key == K_DOWN && r < 7)
            r++;
        while (gridc(c, r) < 0)
            c--;
    }
}
static void phelp(void) {
    int x, y;
    dlg_box(TR("Malprogramm", "Paint"), 60, 16, &x, &y);
    static const char *de[] = {"Pfeile     Bewegen",
                               "Leertaste  Einen Punkt malen",
                               "D          Stift auf oder ab",
                               "Tab        Alle 256 Farben, 0-9 a b c e direkt",
                               "P          Farbe aufnehmen",
                               "F          Fläche füllen",
                               "U          Rückgängig",
                               "N          Neues Bild",
                               "O / S      Öffnen / Speichern",
                               "T          Als Hintergrund verwenden"},
                      *en[] = {"Arrows     Move",
                               "Space      Paint one pixel",
                               "D          Pen up or down",
                               "Tab        All 256 colours, 0-9 a b c e direct",
                               "P          Pick colour",
                               "F          Fill area",
                               "U          Undo",
                               "N          New image",
                               "O / S      Open / Save",
                               "T          Use as background"};
    for (int i = 0; i < 10; i++)
        text(x + 4, y + 2 + i, AT(C_RAISE, C_LAV), TR(de[i], en[i]));
    text(x + 22, y + 13, AT(C_SEL, C_GLOW), " [ Enter ] ");
    ui_show();
    for (;;) {
        key_t k = key_wait();
        if (k.key == K_ENTER || k.key == K_ESC)
            return;
    }
}
/* Asks for the name and saves. 1 = saved */
static int psave(char *path) {
    char n[VFS_PATH], p[VFS_PATH];
    if (path[0])
        strlcpy_(n, path, VFS_PATH);
    else
        ksnprintf(n, VFS_PATH, "%s/bild.vwp", fsx_home());
    if (!dlg_input(TR("Speichern", "Save"), TR("Pfad und Dateiname:", "Path and file name:"), n,
                   VFS_PATH - 1))
        return 0;
    if (vfs_norm(fsx_home(), n, p, VFS_PATH))
        return 0;
    static u8 b[WSZ + 4];
    memcpy(b, "VWP1", 4);
    memcpy(b + 4, cv, WSZ);
    int r = fsx_write(p, b, WSZ + 4, fs_now());
    if (r) {
        dlg_msg(TR("Speichern", "Save"), vfs_errstr(r, cfg_lang));
        return 0;
    }
    strlcpy_(path, p, VFS_PATH);
    return 1;
}
static void paint_run(void);
void app_paint(void) {
    ui_fixed = 1; /* Paint shows pictures, so it keeps the violet colours */
    paint_run();
    ui_fixed = 0;
}
static void paint_run(void) {
    static int px = 40, py = 25, col = 196, pen;
    static char path[VFS_PATH];
    int dirty = 0;
    memset(cv, C_VOID, WSZ);
    if (wkind)
        memcpy(cv, wimg, WSZ);
    memcpy(und, cv, WSZ);
    for (;;) {
        pshow(px, py, col, pen);
        key_t k = key_wait();
        int K = k.key;
        int nx = px, ny = py;
        if (K == K_LEFT && px)
            nx--;
        else if (K == K_RIGHT && px < PW - 1)
            nx++;
        else if (K == K_UP && py)
            ny--;
        else if (K == K_DOWN && py < PH - 1)
            ny++;
        if (nx != px || ny != py) {
            px = nx;
            py = ny;
            if (pen) {
                cv[py * PW + px] = (u8)col;
                dirty = 1;
            }
            continue;
        }
        if (K == K_ESC) {
            if (!dirty)
                return;
            int c = dlg_savechoice();
            if (c == 0)
                continue;
            if (c == 2)
                return;
            if (psave(path))
                return;
            continue;
        }
        if (K == K_F1) {
            phelp();
            continue;
        }
        if (K == K_TAB) {
            col = ppick(col);
            continue;
        }
        if (K >= '0' && K <= '9') {
            col = QP[K - '0'];
            continue;
        }
        if (K >= 'a' && K <= 'c') {
            col = QP[K - 'a' + 10];
            continue;
        }
        if (K == 'e') {
            col = QP[13];
            continue;
        }
        if (K == 'p' || K == 'P') {
            col = cv[py * PW + px];
            continue;
        }
        if (K == 'd' || K == 'D') {
            pen = !pen;
            if (pen) {
                memcpy(und, cv, WSZ);
                cv[py * PW + px] = (u8)col;
                dirty = 1;
            }
            continue;
        }
        if (K == ' ') {
            memcpy(und, cv, WSZ);
            cv[py * PW + px] = (u8)col;
            dirty = 1;
            continue;
        }
        if (K == 'f' || K == 'F') {
            memcpy(und, cv, WSZ);
            pf_fill(cv, px, py, (u8)col);
            dirty = 1;
            continue;
        }
        if (K == 'u' || K == 'U') {
            static u8 t[WSZ];
            memcpy(t, cv, WSZ);
            memcpy(cv, und, WSZ);
            memcpy(und, t, WSZ);
            dirty = 1;
            continue;
        }
        if (K == 'n' || K == 'N') {
            memcpy(und, cv, WSZ);
            memset(cv, C_VOID, WSZ);
            dirty = 1;
            continue;
        }
        if (K == 't' || K == 'T') {
            int r = wall_use(cv);
            dlg_msg(TR("Malprogramm", "Paint"),
                    r ? vfs_errstr(r, cfg_lang) : TR("Hintergrund gesetzt.", "Background set."));
            continue;
        }
        if (K == 'o' || K == 'O') {
            char n[VFS_PATH];
            if (dlg_pick(TR("Bild öffnen", "Open image"), n, VFS_PATH)) {
                int r = wfile(n, cv);
                if (r)
                    dlg_msg(TR("Malprogramm", "Paint"), vfs_errstr(r, cfg_lang));
                else {
                    strlcpy_(path, n, VFS_PATH);
                    dirty = 0;
                    memcpy(und, cv, WSZ);
                }
            }
            continue;
        }
        if (K == 's' || K == 'S') {
            if (psave(path))
                dirty = 0;
            continue;
        }
    }
}
#endif
