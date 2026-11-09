/* Write: a small text editor, a program in ring 3 */
#include "common.h"
/* ---------- Write ---------- */
typedef struct {
    char *b;
    int len, cur, top, col, dirty;
    char file[VFS_PATH];
} ed_t;
#define ED_CAP (1024 * 1024)
static int ls(ed_t *e, int p) {
    while (p > 0 && e->b[p - 1] != '\n')
        p--;
    return p;
}
static int le(ed_t *e, int p) {
    while (p < e->len && e->b[p] != '\n')
        p++;
    return p;
}
static int lineno(ed_t *e, int p) {
    int n = 0;
    for (int i = 0; i < p; i++)
        if (e->b[i] == '\n')
            n++;
    return n;
}
static int linepos(ed_t *e, int n) {
    int p = 0;
    while (n > 0 && p < e->len) {
        if (e->b[p++] == '\n')
            n--;
    }
    return p;
}
static void ed_name(win_t *w, ed_t *e) {
    char t[40];
    ksnprintf(t, 40, "%s%s", e->dirty ? "* " : "",
              e->file[0] ? base(e->file) : TR("Unbenannt", "Untitled"));
    win_title(w, t);
}
static void ed_draw(win_t *w) {
    ed_t *e = w->st;
    int cl = lineno(e, e->cur), cc = e->cur - ls(e, e->cur), H_ = ch(w), W_ = cw(w);
    if (cl < e->top)
        e->top = cl;
    if (cl >= e->top + H_)
        e->top = cl - H_ + 1;
    if (cc < e->col)
        e->col = cc;
    if (cc >= e->col + W_)
        e->col = cc - W_ + 1;
    cfill(w, T);
    int p = linepos(e, e->top);
    for (int y = 0; y < H_ && p <= e->len; y++) {
        int x = 0;
        while (p < e->len && e->b[p] != '\n') {
            cput(w, x - e->col, y, (u8)e->b[p], T);
            x++;
            p++;
        }
        if (p >= e->len)
            break;
        p++;
    }
    w->cx = cc - e->col;
    w->cy = cl - e->top;
    w->cur = 1;
    if (w->w >= 52)
        ksnprintf(w->foot, 64,
                  TR(" Z %d  S %d   Strg+S Speichern  Strg+O Öffnen ",
                     " Ln %d  Col %d   Ctrl+S Save  Ctrl+O Open "),
                  cl + 1, cc + 1);
    else
        ksnprintf(w->foot, 64, TR(" Z %d  S %d ", " Ln %d  Col %d "), cl + 1, cc + 1);
    ed_name(w, e);
}
static void ed_save(ed_t *e) {
    if (!e->file[0]) {
        char n[VFS_PATH], p[VFS_PATH];
        ksnprintf(n, VFS_PATH, "%s/", fsx_home());
        if (!dlg_input(TR("Speichern unter", "Save as"),
                       TR("Pfad und Dateiname:", "Path and file name:"), n, VFS_PATH - 1))
            return;
        if (vfs_norm(fsx_home(), n, p, VFS_PATH)) {
            dlg_msg(TR("Speichern", "Save"), vfs_errstr(VE_INVAL, cfg_lang));
            return;
        }
        strlcpy_(e->file, p, VFS_PATH);
    }
    int r = fsx_write(e->file, e->b, e->len, fs_now());
    if (r)
        dlg_msg(TR("Speichern", "Save"), vfs_errstr(r, cfg_lang));
    else
        e->dirty = 0;
}
static void ed_ins(ed_t *e, char c) {
    if (e->len >= ED_CAP)
        return;
    memmove(e->b + e->cur + 1, e->b + e->cur, e->len - e->cur);
    e->b[e->cur++] = c;
    e->len++;
    e->dirty = 1;
}
static void ed_vert(ed_t *e, int d) {
    int c = e->cur - ls(e, e->cur), l = lineno(e, e->cur) + d;
    if (l < 0)
        l = 0;
    int p = linepos(e, l);
    int end = le(e, p);
    e->cur = p + c > end ? end : p + c;
}
static void ed_key(win_t *w, key_t k) {
    ed_t *e = w->st;
    int c = k.mods & M_CTRL;
    if (c) {
        if (k.key == 's')
            ed_save(e);
        else if (k.key == 'o') {
            char n[VFS_PATH];
            if (dlg_pick(TR("Öffnen", "Open"), n, VFS_PATH))
                app_write(n);
        } else if (k.key == K_HOME)
            e->cur = 0;
        else if (k.key == K_END)
            e->cur = e->len;
        return;
    }
    switch (k.key) {
        case K_LEFT:
            if (e->cur)
                e->cur--;
            break;
        case K_RIGHT:
            if (e->cur < e->len)
                e->cur++;
            break;
        case K_UP:
            ed_vert(e, -1);
            break;
        case K_DOWN:
            ed_vert(e, 1);
            break;
        case K_HOME:
            e->cur = ls(e, e->cur);
            break;
        case K_END:
            e->cur = le(e, e->cur);
            break;
        case K_PGUP:
            ed_vert(e, -ch(w));
            break;
        case K_PGDN:
            ed_vert(e, ch(w));
            break;
        case K_ENTER:
            ed_ins(e, '\n');
            break;
        case K_TAB:
            for (int i = 0; i < 4; i++)
                ed_ins(e, ' ');
            break;
        case K_BS:
            if (e->cur) {
                memmove(e->b + e->cur - 1, e->b + e->cur, e->len - e->cur);
                e->cur--;
                e->len--;
                e->dirty = 1;
            }
            break;
        case K_DEL:
            if (e->cur < e->len) {
                memmove(e->b + e->cur, e->b + e->cur + 1, e->len - e->cur - 1);
                e->len--;
                e->dirty = 1;
            }
            break;
        default:
            if (k.key >= 32 && k.key < 256 && !(k.mods & M_ALT))
                ed_ins(e, k.key);
    }
}
static int ed_cc(win_t *w) {
    ed_t *e = w->st;
    if (!e->dirty)
        return 1;
    int c = dlg_savechoice();
    if (c == 2)
        return 1;
    if (c == 1) {
        ed_save(e);
        return !e->dirty;
    }
    return 0;
}
static void ed_free(win_t *w) {
    ed_t *e = w->st;
    kfree(e->b);
    kfree(e);
}
static const app_t ed_app = {ed_draw, ed_key, 0, ed_cc, ed_free};
static void ed_open(const char *f) {
    ed_t *e = kmalloc(sizeof *e);
    char *b = kmalloc(ED_CAP + 1);
    if (!e || !b) {
        dlg_msg(TR("Schreiben", "Write"), TR("Nicht genug Speicher.", "Not enough memory."));
        kfree(e);
        kfree(b);
        return;
    }
    memset(e, 0, sizeof *e);
    e->b = b;
    if (f) {
        vfs_ent st;
        int sr = fsx_stat(f, &st);
        int64_t n = sr == VE_OK && st.size > ED_CAP ? VE_NOSPC : sr ? sr : fsx_read(f, b, ED_CAP);
        if (n == VE_NOSPC) {
            dlg_msg(TR("Öffnen", "Open"), TR("Datei ist zu groß für Schreiben (max. 1 MB).",
                                             "File is too large for Write (1 MB max)."));
            kfree(b);
            kfree(e);
            return;
        }
        if (n == VE_NOENT)
            n = 0;
        else if (n < 0) {
            dlg_msg(TR("Öffnen", "Open"), vfs_errstr((int)n, cfg_lang));
            kfree(b);
            kfree(e);
            return;
        }
        {
            int o = 0;
            for (int i = 0; i < n; i++)
                if (b[i] != '\r')
                    b[o++] = b[i];
            n = o;
        }
        e->len = (int)n;
        strlcpy_(e->file, f, VFS_PATH);
    }
    win_t *mw = win_open(TR("Unbenannt", "Untitled"), 6, 3, 62, 17, &ed_app, e);
    win_menu(mw, TR("Schreiben", "Write"), "*Info");
    if (!mw) {
        kfree(b);
        kfree(e);
    }
}
void prog_main(void) {
    static char a[VFS_PATH];
    ed_open(sys_arg(a, VFS_PATH) ? a : 0);
}
