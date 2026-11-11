/* Image viewer: shows PNG, JPEG, GIF, BMP and TGA files from the file manager, the same way the
 * browser shows pictures (decoded into 256-colour picture cells with the code of the vxdec helper).
 * Large images are scaled down to the window, small ones keep their size unless "fit" is on. A
 * program in ring 3. */
#include "os.h"

#define VMAX (48ul << 20) /* largest file the decoder accepts */
#define VOUT 800000       /* 12 + 240 * 4 * 100 * 8 */

typedef struct {
    char path[VFS_PATH];
    u8 *data;
    u64 n;
    u8 *px;
    int cols, rows, iw, ih; /* picture in cells, original size in pixels */
    int sw;                 /* shown width in pixels */
    int dc, dr;             /* window area the picture was decoded for */
    int lc, lr;             /* last area seen, to wait until resizing stops */
    u64 seen;
    int fit, err;
    u8 id;
} view_t;

int view_is_image(const char *name) {
    static const char *ext[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga"};
    int l = strlen(name);
    for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++) {
        int e = strlen(ext[i]);
        if (l <= e)
            continue;
        int k = 0;
        while (k < e) {
            char c = name[l - e + k];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (c != ext[i][k])
                break;
            k++;
        }
        if (k == e)
            return 1;
    }
    return 0;
}
static const char *base(const char *p) {
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}
/* Decodes the image for an area of cols x rows cells. */
static void decode(view_t *v, int cols, int rows) {
    v->dc = cols;
    v->dr = rows;
    if (!v->data || cols < 1 || rows < 1)
        return;
    if (cols > 240)
        cols = 240;
    if (rows > 100)
        rows = 100;
    u8 *o = kmalloc(VOUT);
    if (!o) {
        v->err = VE_NOSPC;
        return;
    }
    long k = prog_decode(2, v->data, v->n, o, VOUT, cols, rows,
                         ui_rgb(C_PANEL) | (long)(v->fit ? 1 : 0) << 24, 15000, 0);
    int c = k >= 12 ? o[0] | o[1] << 8 : 0, r = k >= 12 ? o[2] | o[3] << 8 : 0;
    if (k < 12 || c < 1 || c > 240 || r < 1 || r > 100 || k != 12 + (long)c * r * 32) {
        kfree(o);
        v->err = VE_FORMAT;
        return;
    }
    kfree(v->px);
    v->px = kmalloc((u64)c * r * 32);
    if (!v->px) {
        kfree(o);
        v->err = VE_NOSPC;
        return;
    }
    memcpy(v->px, o + 12, (u64)c * r * 32);
    v->iw = o[4] | o[5] << 8;
    v->ih = o[6] | o[7] << 8;
    v->sw = o[8] | o[9] << 8;
    kfree(o);
    v->cols = c;
    v->rows = r;
    v->id = gfx_newid();
    gfx_upload(v->id, v->px, c * 4, r * 8);
    v->err = 0;
}
static int load(view_t *v, const char *path) {
    vfs_ent e;
    int r = fsx_stat(path, &e);
    if (r)
        return r;
    if (e.dir)
        return VE_INVAL;
    if (e.size > VMAX)
        return VE_NOSPC;
    u8 *d = kmalloc(e.size ? e.size : 1);
    if (!d)
        return VE_NOSPC;
    int64_t n = fsx_read(path, d, e.size);
    if (n < 0) {
        kfree(d);
        return (int)n;
    }
    kfree(v->data);
    kfree(v->px);
    v->px = 0;
    v->data = d;
    v->n = (u64)n;
    v->cols = v->rows = v->iw = v->ih = 0;
    strlcpy_(v->path, path, VFS_PATH);
    return 0;
}
static void vw_draw(win_t *w) {
    view_t *v = w->st;
    win_title(w, base(v->path));
    cfill(w, AT(C_PANEL, C_LAV));
    int cols = cw(w), rows = ch(w) - 1;
    if (v->px) {
        int ox = (cols - v->cols) / 2, oy = (rows - v->rows) / 2;
        for (int y = 0; y < v->rows && y + oy < rows; y++)
            for (int x = 0; x < v->cols; x++)
                cputg(w, ox + x, oy + y, v->id, v->px, v->cols * 4, x, y);
    } else if (v->err) {
        const char *m = v->err == VE_NOSPC ? TR("Zu groß zum Anzeigen", "Too large to display")
                                           : TR("Dieses Bild kann nicht angezeigt werden",
                                                "This image cannot be displayed");
        ctext(w, (cols - (int)strlen(m)) / 2, rows / 2, AT(C_PANEL, C_HOT), m);
    }
    for (int x = 0; x < cols; x++)
        cput(w, x, rows, ' ', AT(C_RAISE, C_LAV));
    char b[80];
    if (v->iw)
        ksnprintf(b, 80, " %d x %d  %s", v->iw, v->ih,
                  v->fit          ? TR("angepasst", "fitted")
                  : v->sw < v->iw ? TR("verkleinert", "scaled down")
                                  : TR("Originalgröße", "original size"));
    else
        b[0] = 0;
    ctext(w, 0, rows, AT(C_RAISE, C_LAV), b);
    const char *h = TR("\x1B\x1A Bild  F Anpassen ", "\x1B\x1A Image  F Fit ");
    int l = 0;
    for (const char *p = h; *p; p++)
        if ((*p & 0xC0) != 0x80)
            l++;
    if (l + (int)strlen(b) < cols)
        ctext(w, cols - l, rows, AT(C_RAISE, C_MID), h);
}
/* Redecodes after a resize once the size has stayed the same for a quarter second. */
static void vw_frame(win_t *w) {
    view_t *v = w->st;
    int c = cw(w), r = ch(w) - 1;
    if (c == v->dc && r == v->dr)
        return;
    if (c != v->lc || r != v->lr) {
        v->lc = c;
        v->lr = r;
        v->seen = ticks;
        return;
    }
    if (ticks - v->seen >= 25)
        decode(v, c, r);
}
static void step(win_t *w, int d) {
    view_t *v = w->st;
    char dir[VFS_PATH];
    strlcpy_(dir, v->path, VFS_PATH);
    char *s = strrchr(dir, '/');
    if (!s)
        return;
    if (s == dir)
        s[1] = 0;
    else
        *s = 0;
    fs_item *it = 0;
    int n = fs_load(dir, &it);
    if (n <= 0) {
        kfree(it);
        return;
    }
    const char *me = base(v->path);
    int at = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(it[i].name, me))
            at = i;
    for (int k = 1; k < n; k++) {
        int i = ((at < 0 ? 0 : at) + d * k + n * k) % n;
        if (!it[i].dir && view_is_image(it[i].name)) {
            char p[VFS_PATH];
            ksnprintf(p, VFS_PATH, "%s/%s", strcmp(dir, "/") ? dir : "", it[i].name);
            if (!load(v, p))
                decode(v, cw(w), ch(w) - 1);
            break;
        }
    }
    kfree(it);
}
/* The picture as background: decoded once more for the whole screen, filling it */
static void as_wall(view_t *v) {
    if (!v->data)
        return;
    if (!dlg_confirm(TR("Hintergrund", "Background"),
                     TR("Dieses Bild als Hintergrund verwenden? Fehlt die Datei später, zum "
                        "Beispiel weil der USB-Stick nicht steckt, gilt wieder der Hintergrund "
                        "aus den Einstellungen.",
                        "Use this picture as background? When the file is missing later, for "
                        "example because the USB stick is not plugged in, the background from "
                        "the settings is used again.")))
        return;
    int sw, sh;
    ui_screen(&sw, &sh);
    if (sw > 240)
        sw = 240;
    if (sh > 100)
        sh = 100;
    u8 *o = kmalloc(VOUT);
    u8 *b = o ? kmalloc(8 + VFS_PATH + (u64)sw * sh * 32) : 0;
    long k = b ? prog_decode(2, v->data, v->n, o, VOUT, sw, sh, (long)1 << 25, 15000, 0) : -1;
    int c = k >= 12 ? o[0] | o[1] << 8 : 0, r = k >= 12 ? o[2] | o[3] << 8 : 0;
    int ok = 0;
    if (k == 12 + (long)c * r * 32 && c == sw && r == sh) {
        memset(b, 0, 8 + VFS_PATH);
        memcpy(b, "VPB1", 4);
        b[4] = (u8)c, b[5] = (u8)(c >> 8), b[6] = (u8)r, b[7] = (u8)(r >> 8);
        strlcpy_((char *)b + 8, v->path, VFS_PATH);
        memcpy(b + 8 + VFS_PATH, o + 12, (u64)c * r * 32);
        ok = !vx_sys(30, 18, (long)b, 0);
    }
    kfree(b);
    kfree(o);
    if (!ok)
        dlg_msg(TR("Hintergrund", "Background"),
                TR("Das Bild konnte nicht als Hintergrund gesetzt werden.",
                   "The picture could not be set as background."));
}
static void vw_key(win_t *w, key_t k) {
    view_t *v = w->st;
    if (k.key == K_MENU + 1 || k.key == K_LEFT || k.key == K_PGUP)
        step(w, -1);
    else if (k.key == K_MENU + 2 || k.key == K_RIGHT || k.key == K_PGDN || k.key == ' ')
        step(w, 1);
    else if (k.key == K_MENU + 3 || k.key == 'f' || k.key == 'F') {
        v->fit = !v->fit;
        decode(v, cw(w), ch(w) - 1);
    } else if (k.key == K_MENU + 4 || k.key == K_ENTER) {
        win_zoom(w);
        decode(v, cw(w), ch(w) - 1);
    } else if (k.key == K_MENU + 5 || k.key == 'h' || k.key == 'H' || k.key == 'b' ||
               k.key == 'B')
        as_wall(v);
}
static void vw_free(win_t *w) {
    view_t *v = w->st;
    kfree(v->data);
    kfree(v->px);
    kfree(v);
}
static const app_t vw_app = {vw_draw, vw_key, vw_frame, 0, vw_free, vw_frame};
static void app_view_(const char *path) {
    view_t *v = kmalloc(sizeof *v);
    if (!v)
        return;
    memset(v, 0, sizeof *v);
    int r = load(v, path);
    if (r) {
        dlg_msg(TR("Bildbetrachter", "Image viewer"),
                r == VE_NOSPC ? TR("Zu groß zum Anzeigen", "Too large to display")
                              : vfs_errstr(r, cfg_lang));
        kfree(v);
        return;
    }
    /* first decode for the whole screen, then size the window to the picture */
    int sw, sh;
    ui_screen(&sw, &sh);
    decode(v, sw - 4, sh - 6);
    int ww = v->px ? v->cols + 2 : 44, wh = v->px ? v->rows + 3 : 9;
    if (ww < 44)
        ww = 44;
    if (wh < 9)
        wh = 9;
    win_t *w = win_open(base(path), 2, 2, ww, wh, &vw_app, v);
    if (!w) {
        vw_free(&(win_t){.st = v});
        return;
    }
    v->dc = v->lc = cw(w);
    v->dr = v->lr = ch(w) - 1;
    win_menu(w, TR("Bildbetrachter", "Image viewer"),
             TR("*Info|Vorheriges Bild (Links)|Nächstes Bild (Rechts)|Anpassen an/aus (F)|"
                "Vollbild (Enter)|Als Hintergrund (H)",
                "*Info|Previous image (Left)|Next image (Right)|Fit on/off (F)|"
                "Full window (Enter)|Use as background (B)"));
}
void prog_main(void) {
    static char a[VFS_PATH];
    if (sys_arg(a, VFS_PATH) || dlg_pick(TR("Bild öffnen", "Open image"), a, VFS_PATH))
        app_view_(a);
}
