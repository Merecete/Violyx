#include "os.h"
#define T   AT(C_PANEL, C_ORCH)
#define DIM AT(C_PANEL, C_MID)
#define HI  AT(C_PANEL, C_NEON)
#define SEL AT(C_SEL, C_GLOW)
#define ST  AT(C_VOID, C_VIOLET)
#define SI  AT(C_VOID, C_NEON)
static void simple_free(win_t *w) {
    kfree(w->st);
}
static void fm_free_direct(void *f);
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
static const char *base(const char *p) {
    const char *s = p;
    for (; *p; p++)
        if (*p == '/' && p[1])
            s = p + 1;
    return s;
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
void app_write(const char *f) {
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
/* ---------- Files ---------- */
typedef struct {
    char cwd[VFS_PATH];
    fs_item *it;
    int n, s, top;
    u32 gen;
} fm_t;
static void sizes(u64 v, char *o, int n) {
    if (v >= 1024 * 1024)
        ksnprintf(o, n, "%ld MB", v >> 20);
    else if (v >= 1024)
        ksnprintf(o, n, "%ld KB", v >> 10);
    else
        ksnprintf(o, n, "%ld B", v);
}
static void fm_load(fm_t *f) {
    f->gen = fsx_gen;
    kfree(f->it);
    f->it = 0;
    f->n = fs_load(f->cwd, &f->it);
    if (f->n < 0) {
        f->n = 0;
    }
    if (f->s >= f->n)
        f->s = f->n ? f->n - 1 : 0;
}
static void fm_path(fm_t *f, const char *name, char *o, int n) {
    ksnprintf(o, n, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", name);
}
static void fm_draw(win_t *w) {
    fm_t *f = w->st;
    win_title(w, TR("Dateien", "Files"));
    cfill(w, T);
    ctext(w, 1, 0, HI, f->cwd);
    int atd = !strcmp(f->cwd, "/drives");
    int vis = ch(w) - 2;
    if (f->s < f->top)
        f->top = f->s;
    if (f->s >= f->top + vis)
        f->top = f->s - vis + 1;
    for (int i = 0; i < vis && f->top + i < f->n; i++) {
        fs_item *e = &f->it[f->top + i];
        char l[300], z[16];
        if (e->dir)
            ksnprintf(l, 300, "[%s]", e->name);
        else
            ksnprintf(l, 300, "%s", e->name);
        int sel = f->top + i == f->s;
        if (sel)
            for (int x = 0; x < cw(w); x++)
                cput(w, x, i + 1, ' ', SEL);
        ctext(w, 1, i + 1, sel ? SEL : T, l);
        if (!e->dir) {
            sizes(e->size, z, 16);
            ctext(w, cw(w) - 10, i + 1, sel ? SEL : DIM, z);
        } else if (atd) {
            char d[60];
            fsx_info(e->name, d, 60);
            ctext(w, 9, i + 1, sel ? SEL : DIM, d);
        }
    }
    if (!f->n)
        ctext(w, 1, 2, DIM,
              atd ? TR("Kein USB-Laufwerk gefunden. Stecke einen Stick ein (FAT32 oder exFAT).",
                       "No USB drive found. Plug in a stick (FAT32 or exFAT).")
                  : TR("Ordner ist leer.", "Folder is empty."));
    if (atd && !fs_ok()) {
        char st[64];
        ksnprintf(st, 64, TR("Systemplatte: %s", "System disk: %s"), fs_status());
        ctext(w, 1, ch(w) - 2, DIM, st);
    }
    if (atd)
        ctext(w, 1, ch(w) - 1, DIM,
              TR("Enter Öffnen  Rück Hoch  E Auswerfen  F Formatieren",
                 "Enter Open  Bksp Up  E Eject  F Format"));
    else
        ctext(w, 1, ch(w) - 1, DIM,
              TR("Enter Öffnen Rück Hoch N Neu O Ordner C Kopie R Name Entf Weg",
                 "Enter Open Bksp Up N New O Folder C Copy R Name Del Delete"));
}
/* ---------- Formatting a USB drive ---------- */
static void fmt_prog(u64 a, u64 b) {
    int x, y;
    dlg_box(TR("Formatieren", "Format"), 60, 8, &x, &y);
    text(x + 3, y + 2, AT(C_RAISE, C_LAV), TR("Formatiere ...", "Formatting ..."));
    int w = b ? (int)(a * 52 / b) : 0;
    if (w > 52)
        w = 52;
    fill(x + 3, y + 4, 54, 1, 0xB0, AT(C_VOID, C_DIM));
    fill(x + 3, y + 4, w, 1, 0xDB, AT(C_VOID, C_NEON));
    text(x + 3, y + 6, AT(C_RAISE, C_LAV),
         TR("Bitte den Stick nicht abziehen.", "Please do not unplug the stick."));
    ui_show();
}
/* Asks step by step: file system, name, a warning (No is preselected) and a typed word */
static void drive_format(const char *name) {
    const char *T_ = TR("Formatieren", "Format");
    int r = fsx_can_format(name);
    if (r) {
        dlg_msg(T_, r == VE_DENIED ? TR("Violyx läuft von diesem Laufwerk, es bleibt unverändert.",
                                        "Violyx runs from this drive, it stays unchanged.")
                                   : vfs_errstr(r, cfg_lang));
        return;
    }
    char info[60], t[100];
    fsx_info(name, info, 60);
    int ex = 0, x, y;
    for (;;) {
        ksnprintf(t, 100, TR("%s formatieren", "Format %s"), name);
        dlg_box(t, 70, 12, &x, &y);
        ksnprintf(t, 100, TR("Laufwerk:     %s", "Drive:        %s"), info);
        text(x + 3, y + 2, AT(C_RAISE, C_LAV), t);
        text(x + 3, y + 4, AT(C_RAISE, C_LAV), TR("Dateisystem:", "File system:"));
        text(x + 17, y + 4, AT(C_SEL, C_GLOW), ex ? " < exFAT > " : " < FAT32 > ");
        text(x + 3, y + 6, AT(C_RAISE, C_MID),
             ex ? TR("Für große Dateien über 4 GB, Windows, macOS, Linux.",
                     "For large files over 4 GB, Windows, macOS, Linux.")
                : TR("Läuft überall, auch an Autoradio und TV. Bis 4 GB je Datei.",
                     "Works everywhere, also in cars and TVs. Up to 4 GB per file."));
        text(x + 3, y + 9, AT(C_RAISE, C_DIM),
             TR("Pfeile wählen   Enter weiter   Esc abbrechen",
                "Arrows choose   Enter continue   Esc cancel"));
        ui_show();
        key_t k;
        if (!ui_wait(&k))
            continue;
        if (k.key == K_ESC)
            return;
        if (k.key == K_ENTER)
            break;
        if (k.key == K_LEFT || k.key == K_RIGHT || k.key == ' ')
            ex = !ex;
    }
    char lab[12] = "VIOLYX";
    dlg_fresh = 1;
    if (!dlg_input(TR("Name des Laufwerks", "Drive name"),
                   TR("Name, bis 11 Zeichen:", "Name, up to 11 characters:"), lab, 12))
        return;
    int yes = 0;
    for (;;) {
        dlg_box(TR("ACHTUNG", "WARNING"), 64, 12, &x, &y);
        u8 h = AT(C_HOT, C_VOID);
        fill(x + 2, y + 2, 60, 2, ' ', h);
        text(x + 4, y + 2, h,
             TR("ALLE DATEN AUF DIESEM LAUFWERK WERDEN", "ALL DATA ON THIS DRIVE WILL BE"));
        text(x + 4, y + 3, h, TR("UNWIDERRUFLICH GELÖSCHT.", "PERMANENTLY ERASED."));
        ksnprintf(t, 100, "%s: %s", name, info);
        text(x + 3, y + 5, AT(C_RAISE, C_LAV), t);
        text(x + 3, y + 7, AT(C_RAISE, C_GLOW), TR("Wirklich formatieren?", "Really format?"));
        text(x + 20, y + 9, yes ? AT(C_RAISE, C_LAV) : AT(C_SEL, C_GLOW), TR("  Nein  ", "  No  "));
        text(x + 34, y + 9, yes ? AT(C_HOT, C_VOID) : AT(C_RAISE, C_LAV), TR("  Ja  ", "  Yes  "));
        ui_show();
        key_t k;
        if (!ui_wait(&k))
            continue;
        if (k.key == K_ESC || k.key == 'n' || k.key == 'N')
            return;
        if (k.key == K_LEFT || k.key == K_RIGHT || k.key == K_TAB)
            yes = !yes;
        if (k.key == K_ENTER) {
            if (!yes)
                return;
            break;
        }
    }
    char wd[16];
    wd[0] = 0;
    if (!dlg_input(TR("Letzte Bestätigung", "Final confirmation"),
                   TR("Zum Formatieren FORMATIEREN eintippen:", "Type FORMAT to format:"), wd,
                   16) ||
        strcmp(wd, TR("FORMATIEREN", "FORMAT"))) {
        dlg_msg(T_,
                TR("Abgebrochen. Es wurde nichts verändert.", "Cancelled. Nothing was changed."));
        return;
    }
    fmt_prog(0, 1);
    r = fsx_format(name, ex, lab, fmt_prog);
    if (!r)
        ksnprintf(t, 100, TR("%s ist jetzt mit %s formatiert.", "%s is now formatted with %s."),
                  name, ex ? "exFAT" : "FAT32");
    dlg_msg(T_, !r              ? t
                : r == VE_NOSPC ? (ex ? TR("Das Laufwerk ist zu klein.", "The drive is too small.")
                                      : TR("Zu klein für FAT32, bitte exFAT wählen.",
                                           "Too small for FAT32, please choose exFAT."))
                : r == VE_INVAL ? TR("Laufwerke über 2 TB werden nicht unterstützt.",
                                     "Drives over 2 TB are not supported.")
                                : TR("Schreibfehler! Das Laufwerk ist unvollständig formatiert.",
                                     "Write error! The drive is incompletely formatted."));
}
static void fm_key(win_t *w, key_t k) {
    fm_t *f = w->st;
    char p[VFS_PATH + 260], q[VFS_PATH + 260], n[VFS_NAME + 1];
    int r = 0;
    if (k.key >= K_MENU && k.key < K_MENU + 7) { /* the Files menu uses the same keys */
        static const u16 mk[7] = {'c', 'r', K_DEL, 'n', 'o', 'e', 'f'};
        k.key = mk[k.key - K_MENU];
        k.mods = 0;
    }
    if (k.key == K_UP) {
        if (f->s)
            f->s--;
        return;
    }
    if (k.key == K_DOWN) {
        if (f->s < f->n - 1)
            f->s++;
        return;
    }
    if (k.key == K_BS) {
        if (strcmp(f->cwd, "/")) {
            ksnprintf(p, sizeof p, "%s/..", f->cwd);
            vfs_norm("/", p, f->cwd, VFS_PATH);
            f->s = f->top = 0;
            fm_load(f);
        }
        return;
    }
    if (k.key == 'n' || k.key == 'N') {
        app_write(0);
        return;
    }
    if (k.key == 'o' || k.key == 'O') {
        n[0] = 0;
        if (dlg_input(TR("Neuer Ordner", "New folder"), TR("Name:", "Name:"), n, VFS_NAME)) {
            fm_path(f, n, p, sizeof p);
            r = fsx_mkdir(p, fs_now());
            fm_load(f);
        }
    } else if (f->n && k.key == K_ENTER) {
        fm_path(f, f->it[f->s].name, p, sizeof p);
        if (f->it[f->s].dir) {
            vfs_ent e;
            r = fsx_stat(p, &e);
            if (r == VE_DENIED)
                return;
            if (!r) {
                strlcpy_(f->cwd, p, VFS_PATH);
                f->s = f->top = 0;
                fm_load(f);
            }
        } else {
            size_t L = strlen(p);
            if (L > 4 && !strcmp(p + L - 4, ".vpk")) {
                r = fs_ok() ? prog_install(p)
                    : dlg_confirm(TR("Programm", "Program"),
                                  TR("Dieses Programm jetzt starten?", "Start this program now?"))
                        ? prog_run_file(p, 0)
                        : VE_DENIED;
                if (r == VE_DENIED)
                    return;
                if (!r && fs_ok()) {
                    dlg_msg(TR("Programm", "Program"), TR("Installiert. Du findest es unter F2.",
                                                          "Installed. Find it under F2."));
                    return;
                }
            } else if (L > 4 && !strcmp(p + L - 4, ".vwp")) {
                r = wall_open_file(p);
                if (r == VE_DENIED)
                    return;
            } else if (view_is_image(p)) {
                app_view(p);
                return;
            } else {
                app_write(p);
                return;
            }
        }
    } else if (!strcmp(f->cwd, "/drives")) {
        if (f->n && (k.key == 'f' || k.key == 'F')) {
            drive_format(f->it[f->s].name);
            fm_load(f);
            return;
        }
        if (f->n && (k.key == 'e' || k.key == 'E')) {
            r = fsx_eject(f->it[f->s].name);
            if (!r) {
                ksnprintf(p, sizeof p,
                          TR("%s kann jetzt abgezogen werden.", "%s can be removed now."),
                          f->it[f->s].name);
                dlg_msg(TR("Auswerfen", "Eject"), p);
            }
        }
    } else if (f->n && (k.key == 'c' || k.key == 'C')) {
        char d[VFS_PATH];
        const char *nm = f->it[f->s].name;
        char t[VFS_PATH];
        d[0] = 0;
        if (!memcmp(f->cwd, "/drives/", 8)) {
            if (fs_ok())
                ksnprintf(d, VFS_PATH, "/home/%s", nm);
            else
                ksnprintf(d, VFS_PATH, "%s", nm);
        } else if (fsx_first(t, VFS_PATH))
            ksnprintf(d, VFS_PATH, "%s/%s", t, nm);
        else
            ksnprintf(d, VFS_PATH, "%s", nm);
        if (dlg_input(TR("Kopieren", "Copy"), TR("Ziel:", "Target:"), d, VFS_PATH - 1)) {
            fm_path(f, nm, p, sizeof p);
            if (vfs_norm(f->cwd, d, q, VFS_PATH))
                r = VE_INVAL;
            else {
                vfs_ent e;
                int ex = fsx_stat(q, &e);
                if (ex == VE_DENIED)
                    return;
                if (!ex && e.dir) {
                    ksnprintf(t, VFS_PATH, "%s/%s", strcmp(q, "/") ? q : "", nm);
                    strlcpy_(q, t, VFS_PATH);
                    ex = fsx_stat(q, &e);
                }
                if (!ex &&
                    !dlg_confirm(TR("Kopieren", "Copy"), TR("Ziel ersetzen?", "Replace target?")))
                    return;
                r = fsx_copy(p, q);
            }
            fm_load(f);
        }
    } else if (f->n && (k.key == 'r' || k.key == 'R')) {
        strlcpy_(n, f->it[f->s].name, VFS_NAME + 1);
        if (dlg_input(TR("Umbenennen", "Rename"), TR("Neuer Name oder Pfad:", "New name or path:"),
                      n, VFS_NAME)) {
            fm_path(f, f->it[f->s].name, p, sizeof p);
            if (vfs_norm(f->cwd, n, q, VFS_PATH))
                r = VE_INVAL;
            else
                r = fsx_rename(p, q);
            fm_load(f);
        }
    } else if (f->n && k.key == K_DEL) {
        char t[80];
        ksnprintf(t, 80,
                  f->it[f->s].dir
                      ? TR("Ordner %s samt Inhalt löschen?", "Delete folder %s with contents?")
                      : TR("%s löschen?", "Delete %s?"),
                  f->it[f->s].name);
        if (dlg_confirm(TR("Löschen", "Delete"), t)) {
            fm_path(f, f->it[f->s].name, p, sizeof p);
            r = fsx_rmtree(p);
            fm_load(f);
        }
    }
    if (r)
        dlg_msg(TR("Dateien", "Files"), vfs_errstr(r, cfg_lang));
}
static void fm_tick(win_t *w) {
    fm_t *f = w->st;
    if (f->gen == fsx_gen)
        return;
    if (!fsx_quiet(f->cwd)) {
        strlcpy_(f->cwd, "/drives", VFS_PATH);
        f->s = f->top = 0;
    }
    fm_load(f);
}
static void fm_free(win_t *w) {
    fm_t *f = w->st;
    kfree(f->it);
    kfree(f);
}
static void fm_free_direct(void *p) {
    fm_t *f = p;
    kfree(f->it);
    kfree(f);
}
static const app_t fm_app = {fm_draw, fm_key, fm_tick, 0, fm_free};
void app_files(void) {
    fm_t *f = kmalloc(sizeof *f);
    if (!f)
        return;
    memset(f, 0, sizeof *f);
    strlcpy_(f->cwd, fsx_home(), VFS_PATH);
    if (!fsx_quiet(f->cwd))
        strlcpy_(f->cwd, "/drives", VFS_PATH);
    fm_load(f);
    win_t *mw = win_open(TR("Dateien", "Files"), 3, 2, 64, 18, &fm_app, f);
    win_menu(
        mw, TR("Dateien", "Files"),
        TR("Kopieren (C)|Verschieben/Umbenennen (R)|Löschen (Entf)|Neue Datei (N)|Neuer Ordner "
           "(O)|Auswerfen (E)|Formatieren (F)",
           "Copy (C)|Move/Rename (R)|Delete (Del)|New file (N)|New folder (O)|Eject (E)|Format "
           "(F)"));
    if (!mw)
        fm_free_direct(f);
}
/* ---------- Commands ---------- */
#define SL 400 /* lines kept in the output */
#define SW 78
#define SH 32 /* earlier inputs, reached with the Up arrow */
typedef struct {
    char l[SL][SW + 1];
    int n;
    char in[SW];
    int il, ic; /* length and cursor position of the input */
    int top;
    char cwd[VFS_PATH];
    char hist[SH][SW];
    int hn, hp;
} sh_t;
static void sh_put(sh_t *s, const char *t) {
    if (s->n == SL) {
        memmove(s->l[0], s->l[1], (SL - 1) * (SW + 1));
        s->n--;
    }
    strlcpy_(s->l[s->n++], t, SW + 1);
    s->top = 0;
}
#define HOLE_MAX (1024 * 1024)
static sh_t *sh_cur;
static void sh_prog(const char *t) {
    sh_put(sh_cur, t);
    ui_render();
    ui_show();
}
typedef struct {
    sh_t *s;
    int n;
} shl;
static int sh_ls(const vfs_ent *e, void *x) {
    shl *l = x;
    char b[100], z[16];
    sizes(e->size, z, 16);
    if (e->dir)
        ksnprintf(b, 100, "[%s]", e->name);
    else
        ksnprintf(b, 100, "%24s %s", e->name, z);
    sh_put(l->s, b);
    l->n++;
    return 0;
}
static void sh_err(sh_t *s, int r) {
    if (r)
        sh_put(s, vfs_errstr(r, cfg_lang));
}
static int sh_path(sh_t *s, const char *a, char *o) {
    if (vfs_norm(s->cwd, a, o, VFS_PATH)) {
        sh_err(s, VE_INVAL);
        return -1;
    }
    return 0;
}
static void sh_line(const char *l, void *a) {
    sh_put(a, l);
}
/* All commands: names (aliases separated by spaces), arguments and a short description */
static const struct {
    const char *n, *ade, *aen, *de, *en;
} sh_cmds[] = {
    {"dir ls", "[ORDNER]", "[FOLDER]", "Zeigt den Inhalt eines Ordners mit den Größen.",
     "Shows what is in a folder, with sizes."},
    {"cd", "[ORDNER]", "[FOLDER]",
     "Wechselt den Ordner. .. geht eine Ebene hoch, ohne Angabe nach /.",
     "Changes the folder. .. goes up one level, nothing goes to /."},
    {"pwd", "", "", "Zeigt den aktuellen Ordner.", "Shows the current folder."},
    {"mkdir", "NAME", "NAME", "Legt einen Ordner an.", "Creates a folder."},
    {"del rm rmdir", "NAME", "NAME", "Löscht eine Datei oder einen leeren Ordner.",
     "Deletes a file or an empty folder."},
    {"cp", "QUELLE ZIEL", "SOURCE TARGET",
     "Kopiert Dateien und ganze Ordner, auch zwischen Platte und Stick.",
     "Copies files and whole folders, also between disk and stick."},
    {"mv", "QUELLE ZIEL", "SOURCE TARGET",
     "Verschiebt oder benennt um. Ist ZIEL ein Ordner, landet es darin.",
     "Moves or renames. If TARGET is a folder, it goes into it."},
    {"type cat", "DATEI", "FILE", "Zeigt eine Textdatei (bis 16 KB).",
     "Shows a text file (up to 16 KB)."},
    {"edit", "[DATEI]", "[FILE]", "Öffnet die Datei in Schreiben.", "Opens the file in Write."},
    {"view bild", "DATEI", "FILE", "Öffnet ein Bild im Bildbetrachter.",
     "Opens a picture in the image viewer."},
    {"df", "", "", "Zeigt den freien Platz der Systemplatte.",
     "Shows the free space of the system disk."},
    {"eject", "usb1", "usb1", "Meldet einen USB-Stick sicher ab.", "Signs a USB stick off safely."},
    {"format", "usb1", "usb1", "Formatiert einen USB-Stick mit FAT32 oder exFAT.",
     "Formats a USB stick with FAT32 or exFAT."},
    {"usb", "", "", "Listet USB-Controller und -Geräte.", "Lists USB controllers and devices."},
    {"install", "DATEI.vpk", "FILE.vpk", "Installiert ein Programm.", "Installs a program."},
    {"run", "NAME | DATEI.vpk", "NAME | FILE.vpk",
     "Startet ein installiertes Programm oder ein Paket.",
     "Starts an installed program or a package."},
    {"uninstall", "NAME", "NAME", "Entfernt ein Programm mit seinen Daten.",
     "Removes a program and its data."},
    {"web", "[ADRESSE]", "[ADDRESS]", "Öffnet den Browser.", "Opens the browser."},
    {"uhr clock", "", "", "Öffnet die Uhr.", "Opens the clock."},
    {"netz net", "", "", "Zeigt Netzwerkkarte, Adresse, Gateway und DNS.",
     "Shows network card, address, gateway and DNS."},
    {"ping", "NAME", "NAME", "Schickt vier Pings an einen Rechner.", "Sends four pings to a host."},
    {"hole fetch", "ADRESSE", "ADDRESS", "Lädt eine Seite und zeigt Status und Anfang.",
     "Loads a page and shows its status and beginning."},
    {"netdiag", "", "", "Ausführliche Netzwerkdiagnose.", "Detailed network diagnosis."},
    {"ver", "", "", "Zeigt die Version.", "Shows the version."},
    {"zeit time", "", "", "Zeigt Datum und Uhrzeit.", "Shows date and time."},
    {"mem", "", "", "Zeigt den Arbeitsspeicher.", "Shows the memory."},
    {"geraete devices pci", "[alle]", "[all]", "PCI-Geräte, mit alle auch Brücken und Ähnliches.",
     "PCI devices, with all also bridges and the like."},
    {"sound", "", "", "Soundgeräte und Ausgänge, mit Testton.",
     "Sound devices and outputs, with a test tone."},
    {"neustart restart reboot", "", "", "Startet Violyx geordnet neu.",
     "Restarts Violyx in order."},
    {"echo", "TEXT", "TEXT", "Gibt den Text aus.", "Prints the text."},
    {"cls clear", "", "", "Leert die Ausgabe.", "Clears the output."},
    {"hilfe help", "[BEFEHL]", "[COMMAND]", "Übersicht, oder die Hilfe zu einem Befehl.",
     "Overview, or the help for one command."},
};
#define NCMD ((int)(sizeof sh_cmds / sizeof sh_cmds[0]))
static int sh_is(const char *names, const char *w) {
    size_t l = strlen(w);
    for (const char *p = names; *p;) {
        const char *e = p;
        while (*e && *e != ' ')
            e++;
        if ((size_t)(e - p) == l && !memcmp(p, w, l))
            return 1;
        p = *e ? e + 1 : e;
    }
    return 0;
}
static void sh_help(sh_t *s, const char *a) {
    if (!*a) {
        static const char *de[] = {"Dateien:   dir cd pwd mkdir del cp mv type edit view df",
                                   "Sticks:    eject format usb",
                                   "Programme: install run uninstall web uhr",
                                   "Netz:      netz ping hole netdiag",
                                   "System:    ver zeit mem geraete sound neustart echo cls",
                                   "hilfe BEFEHL erklärt einen Befehl. Pfeil hoch: frühere",
                                   "Eingaben, Tab ergänzt Befehle und Namen. Namen mit",
                                   "Leerzeichen in Anführungszeichen: cp \"Mein Lied.mp3\" /home"},
                          *en[] = {"Files:     dir cd pwd mkdir del cp mv type edit view df",
                                   "Sticks:    eject format usb",
                                   "Programs:  install run uninstall web clock",
                                   "Network:   net ping fetch netdiag",
                                   "System:    ver time mem devices sound restart echo cls",
                                   "help COMMAND explains a command. Up arrow: earlier",
                                   "inputs, Tab completes commands and names. Names with",
                                   "spaces in quotes: cp \"My song.mp3\" /home"};
        for (int i = 0; i < 8; i++)
            sh_put(s, TR(de[i], en[i]));
        return;
    }
    for (int i = 0; i < NCMD; i++)
        if (sh_is(sh_cmds[i].n, a)) {
            char b[SW + 1];
            ksnprintf(b, sizeof b, "%s %s", a, TR(sh_cmds[i].ade, sh_cmds[i].aen));
            sh_put(s, b);
            ksnprintf(b, sizeof b, "  %s", TR(sh_cmds[i].de, sh_cmds[i].en));
            sh_put(s, b);
            if (strchr(sh_cmds[i].n, ' ')) {
                ksnprintf(b, sizeof b, TR("  Auch: %s", "  Also: %s"), sh_cmds[i].n);
                sh_put(s, b);
            }
            return;
        }
    sh_put(s,
           TR("Diesen Befehl gibt es nicht. Tippe: hilfe", "There is no such command. Type: help"));
}
/* One word: quotes allow spaces ("My song.mp3"). The word is ended with a 0. */
static char *sh_word(char **p) {
    char *s = *p;
    while (*s == ' ')
        s++;
    char *w = s;
    if (*s == '"') {
        w = ++s;
        while (*s && *s != '"')
            s++;
    } else
        while (*s && *s != ' ')
            s++;
    if (*s)
        *s++ = 0;
    while (*s == ' ')
        s++;
    *p = s;
    return w;
}
static void sh_exec(sh_t *s) {
    if (!fsx_quiet(s->cwd))
        strlcpy_(s->cwd, "/drives", VFS_PATH);
    char line[SW + 4];
    ksnprintf(line, SW + 4, "> %s", s->in);
    sh_put(s, line);
    if (s->in[0] && (!s->hn || strcmp(s->hist[s->hn - 1], s->in))) { /* remember the input */
        if (s->hn == SH) {
            memmove(s->hist[0], s->hist[1], (SH - 1) * SW);
            s->hn--;
        }
        strlcpy_(s->hist[s->hn++], s->in, SW);
    }
    s->hp = s->hn;
    char *rest = s->in, *c = sh_word(&rest);
    static char two[SW];
    strlcpy_(two, rest, SW); /* for mv/cp, which take two words */
    char *a = rest;
    size_t al = strlen(a);
    while (al && a[al - 1] == ' ')
        a[--al] = 0;
    if (a[0] == '"' && strcmp(c, "echo")) { /* one name in quotes */
        a++;
        al--;
        if (al && a[al - 1] == '"')
            a[--al] = 0;
    }
    char b[80];
    static char p[VFS_PATH], q[VFS_PATH];
    if (!*c) {
    } else if (!strcmp(c, "hilfe") || !strcmp(c, "help")) {
        sh_help(s, a);
    } else if (!strcmp(c, "cls") || !strcmp(c, "clear"))
        s->n = 0;
    else if (!strcmp(c, "ver"))
        sh_put(s, "Violyx " VERSION);
    else if (!strcmp(c, "echo"))
        sh_put(s, a);
    else if ((!strcmp(c, "netz") || !strcmp(c, "net"))) {
        for (int i = 0; i < 6; i++) {
            net_info(b, 80, i);
            if (b[0])
                sh_put(s, b);
        }
    } else if (!strcmp(c, "ping")) {
        if (!*a)
            sh_put(s, TR("Aufruf: ping NAME", "Usage: ping NAME"));
        else {
            u32 ip;
            char t[16];
            int r = net_resolve(a, &ip, t);
            if (r == -3)
                sh_put(s, net_status());
            else if (r == -2)
                sh_put(s, TR("Abgebrochen.", "Cancelled."));
            else if (r)
                sh_put(s, TR("Name nicht gefunden.", "Name not found."));
            else {
                int rx = 0, n = 0;
                ksnprintf(b, 80, TR("Ping an %s [%s]", "Pinging %s [%s]"), a, t);
                sh_put(s, b);
                ui_render();
                ui_show();
                for (int i = 1; i <= 4; i++) {
                    int ms = 0, ttl = 0;
                    r = net_ping(ip, i, &ms, &ttl);
                    if (r == -2) {
                        sh_put(s, TR("Abgebrochen.", "Cancelled."));
                        break;
                    }
                    n++;
                    if (r == 0) {
                        rx++;
                        if (ms)
                            ksnprintf(b, 80,
                                      TR("Antwort von %s: Folge %d, Zeit %d ms, TTL %d",
                                         "Reply from %s: seq %d, time %d ms, TTL %d"),
                                      t, i, ms, ttl);
                        else
                            ksnprintf(b, 80,
                                      TR("Antwort von %s: Folge %d, Zeit <10 ms, TTL %d",
                                         "Reply from %s: seq %d, time <10 ms, TTL %d"),
                                      t, i, ttl);
                    } else
                        ksnprintf(b, 80, TR("Zeitüberschreitung (Folge %d)", "Timed out (seq %d)"),
                                  i);
                    sh_put(s, b);
                    ui_render();
                    ui_show();
                }
                ksnprintf(b, 80, TR("%d gesendet, %d empfangen", "%d sent, %d received"), n, rx);
                sh_put(s, b);
            }
        }
    } else if ((!strcmp(c, "hole") || !strcmp(c, "fetch"))) {
        if (!*a)
            sh_put(s, TR("Aufruf: hole ADRESSE", "Usage: fetch ADDRESS"));
        else {
            char *w = kmalloc(HOLE_MAX);
            http_res r;
            if (!w)
                sh_put(s, TR("Nicht genug Speicher.", "Not enough memory."));
            else {
                sh_cur = s;
                int n = http_get(a, 0, w, HOLE_MAX, &r, sh_prog);
                if (n < 0)
                    sh_put(s, r.err);
                else {
                    ksnprintf(b, 80, "HTTP %d, %d bytes, %s%s", r.status, r.length,
                              r.tls ? TR("verschlüsselt, Zertifikat geprüft",
                                         "encrypted, certificate verified")
                                    : TR("UNVERSCHLÜSSELT", "NOT ENCRYPTED"),
                              r.truncated ? TR(", gekürzt", ", truncated") : "");
                    sh_put(s, b);
                    if (r.tls && tls_weak())
                        sh_put(s,
                               TR("Warnung: CPU ohne Zufallsgenerator, Verschlüsselung schwächer.",
                                  "Warning: CPU has no random generator, encryption is weaker."));
                    if (r.type[0]) {
                        ksnprintf(b, 80, TR("Typ: %s", "Type: %s"), r.type);
                        sh_put(s, b);
                    }
                    char *p = w;
                    for (int l = 0; l < 14 && *p;) {
                        char *e = p;
                        while (*e && *e != '\n')
                            e++;
                        char sv = *e;
                        *e = 0;
                        if (e > p && e[-1] == '\r')
                            e[-1] = 0;
                        for (char *q = p; *q; q++)
                            if (*q == '\t')
                                *q = ' ';
                        while (*p == ' ')
                            p++;
                        if (*p) {
                            sh_put(s, p);
                            l++;
                        }
                        *e = sv;
                        p = *e ? e + 1 : e;
                    }
                }
                kfree(w);
            }
        }
    } else if (!strcmp(c, "web"))
        web_app_open(*a ? a : 0);
    else if ((!strcmp(c, "neustart") || !strcmp(c, "restart") || !strcmp(c, "reboot"))) {
        outb(0xE9, 'S');
        reboot();
    } else if (!strcmp(c, "mem")) {
        ksnprintf(b, 80, TR("RAM %ld KB, Heap frei %ld KB", "RAM %ld KB, heap free %ld KB"),
                  mem_total_kb, heap_free() / 1024);
        sh_put(s, b);
    } else if ((!strcmp(c, "zeit") || !strcmp(c, "time"))) {
        rtc_t r;
        rtc_read(&r);
        if (cfg_lang)
            ksnprintf(b, 80, "%d-%02d-%02d %02d:%02d:%02d", r.year, r.mon, r.day, r.h, r.m, r.s);
        else
            ksnprintf(b, 80, "%02d.%02d.%d %02d:%02d:%02d", r.day, r.mon, r.year, r.h, r.m, r.s);
        sh_put(s, b);
    } else if (!strcmp(c, "usb"))
        usb_report(sh_line, s);
    else if (!strcmp(c, "pci") || !strcmp(c, "geraete") || !strcmp(c, "devices")) {
        static pci_t d[256];
        int n = pci_scan(d, 256), all = !strcmp(a, "alle") || !strcmp(a, "all"), shown = 0;
        for (int i = 0; i < n; i++) {
            if (!all && (d[i].cls == 6 || d[i].cls == 8 || d[i].cls == 5 || d[i].cls == 0x11 ||
                         d[i].cls == 0x13))
                continue;
            shown++;
            static const char *cn[] = {"?",           "Massenspeicher",
                                       "Netzwerk",    "Grafik",
                                       "Multimedia",  "Speicher",
                                       "Bruecke",     "Schnittstelle",
                                       "System",      "Eingabe",
                                       "Dock",        "CPU",
                                       "Seriell/USB", "Funk"},
                              *ce[] = {"?",      "Storage", "Network",    "Display", "Multimedia",
                                       "Memory", "Bridge",  "Comm",       "System",  "Input",
                                       "Dock",   "CPU",     "Serial/USB", "Wireless"};
            const char *mk = d[i].ven == 0x10DE   ? "NVIDIA"
                             : d[i].ven == 0x1002 ? "AMD"
                             : d[i].ven == 0x8086 ? "Intel"
                             : d[i].ven == 0x10EC ? "Realtek"
                             : d[i].ven == 0x1022 ? "AMD"
                                                  : "";
            char l[80];
            ksnprintf(l, 80, "%02x:%02x.%d  %04x:%04x  %s %s%s", d[i].bus, d[i].dev, d[i].fn,
                      d[i].ven, d[i].did, d[i].cls < 14 ? (cfg_lang ? ce : cn)[d[i].cls] : "?", mk,
                      d[i].cls == 4 && d[i].sub == 3 ? " (HD Audio)" : "");
            sh_put(s, l);
        }
        {
            char l[80];
            ksnprintf(l, 80,
                      TR("%d von %d Geräten, Brücken und Systemteile ausgeblendet (geraete alle)",
                         "%d of %d devices, bridges and system parts hidden (devices all)"),
                      shown, n);
            sh_put(s, l);
        }
    } else if (!strcmp(c, "sound")) {
        snd_report(sh_line, s);
        snd_tone(523, 140, 70, 1, 0);
        snd_tone(659, 140, 70, 1, 150);
        snd_tone(784, 220, 70, 1, 300);
    } else if (!strcmp(c, "netdiag"))
        net_diag(sh_line, s);
    else if (!strcmp(c, "install")) {
        if (vfs_norm(s->cwd, a, p, VFS_PATH))
            sh_err(s, VE_INVAL);
        else {
            int r = prog_install(p);
            if (r)
                sh_err(s, r);
            else
                sh_put(s, TR("Installiert.", "Installed."));
        }
    } else if (!strcmp(c, "run")) {
        size_t L = strlen(a);
        int r;
        if (L > 4 && !strcmp(a + L - 4, ".vpk")) {
            if (vfs_norm(s->cwd, a, p, VFS_PATH))
                r = VE_INVAL;
            else
                r = prog_run_file(p, 0);
        } else
            r = prog_run(a);
        if (r)
            sh_err(s, r);
    } else if (!strcmp(c, "uninstall")) {
        int r = prog_uninstall(a);
        if (r)
            sh_err(s, r);
        else
            sh_put(s, TR("Entfernt.", "Removed."));
    } else if (!strcmp(c, "eject")) {
        if (!*a && !memcmp(s->cwd, "/drives/", 8))
            a = s->cwd + 8;
        char dn[8];
        int k = 0;
        while (a[k] && a[k] != '/' && k < 7) {
            dn[k] = a[k];
            k++;
        }
        dn[k] = 0;
        if (!k)
            sh_put(s, TR("Aufruf: eject usb1", "Usage: eject usb1"));
        else {
            int r = fsx_eject(dn);
            if (r)
                sh_err(s, r);
            else {
                ksnprintf(b, 80, TR("%s kann jetzt abgezogen werden.", "%s can be removed now."),
                          dn);
                sh_put(s, b);
            }
        }
    } else if (!strcmp(c, "format")) {
        if (!*a)
            sh_put(s, TR("Aufruf: format usb1", "Usage: format usb1"));
        else
            drive_format(!memcmp(a, "/drives/", 8) ? a + 8 : a);
    } else if (!strcmp(c, "pwd"))
        sh_put(s, s->cwd);
    else if (!strcmp(c, "df") && !fs_ok())
        sh_put(s, fs_status());
    else if (!strcmp(c, "df")) {
        u64 t, f;
        u32 n;
        vfs_usage(&t, &f, &n);
        ksnprintf(b, 80,
                  TR("%ld MB frei von %ld MB, %d Dateien", "%ld MB free of %ld MB, %d files"),
                  f >> 20, t >> 20, n);
        sh_put(s, b);
    } else if (!strcmp(c, "cd")) {
        if (!sh_path(s, *a ? a : "/", p)) {
            vfs_ent e;
            int r = fsx_stat(p, &e);
            if (r)
                sh_err(s, r);
            else if (!e.dir)
                sh_err(s, VE_NOTDIR);
            else
                strlcpy_(s->cwd, p, VFS_PATH);
        }
    } else if (!strcmp(c, "dir") || !strcmp(c, "ls")) {
        if (!sh_path(s, *a ? a : ".", p)) {
            shl l = {s, 0};
            int r = fsx_list(p, sh_ls, &l);
            sh_err(s, r);
            if (!r) {
                ksnprintf(b, 80, TR("%d Einträge", "%d entries"), l.n);
                sh_put(s, b);
            }
        }
    } else if (!strcmp(c, "mkdir")) {
        if (!sh_path(s, a, p))
            sh_err(s, fsx_mkdir(p, fs_now()));
    } else if (!strcmp(c, "del") || !strcmp(c, "rm") || !strcmp(c, "rmdir")) {
        if (!sh_path(s, a, p)) {
            int r = fsx_remove(p);
            if (r)
                sh_err(s, r);
            else
                sh_put(s, TR("Gelöscht.", "Deleted."));
        }
    } else if (!strcmp(c, "mv") || !strcmp(c, "cp")) {
        char *r2 = two;
        a = sh_word(&r2);
        char *d = sh_word(&r2);
        if (!*a || !*d)
            sh_put(s, TR("Aufruf: mv/cp QUELLE ZIEL", "Usage: mv/cp SOURCE TARGET"));
        else if (!sh_path(s, a, p) && !sh_path(s, d, q)) {
            vfs_ent e;
            if (!fsx_stat(q, &e) && e.dir) {
                char t[VFS_PATH + 260];
                ksnprintf(t, sizeof t, "%s/%s", strcmp(q, "/") ? q : "", base(p));
                strlcpy_(q, t, VFS_PATH);
            }
            if (c[0] == 'm')
                sh_err(s, fsx_rename(p, q));
            else
                sh_err(s, fsx_copy(p, q));
        }
    } else if (!strcmp(c, "edit")) {
        if (*a) {
            if (!sh_path(s, a, p))
                app_write(p);
        } else
            app_write(0);
    } else if (!strcmp(c, "view") || !strcmp(c, "bild")) {
        if (!sh_path(s, a, p))
            app_view(p);
    } else if (!strcmp(c, "uhr") || !strcmp(c, "clock")) {
        app_clock();
    } else if (!strcmp(c, "type") || !strcmp(c, "cat")) {
        static char f[16384];
        if (!sh_path(s, a, p)) {
            int64_t n = fsx_read(p, f, sizeof f - 1);
            if (n < 0)
                sh_err(s, (int)n);
            else {
                f[n] = 0;
                char *t = f;
                while (t < f + n) {
                    char *e = t;
                    while (*e && *e != '\n')
                        e++;
                    char sv = *e;
                    *e = 0;
                    sh_put(s, t);
                    *e = sv;
                    t = e + 1;
                }
            }
        }
    } else
        sh_put(s, TR("Unbekannter Befehl. Tippe: hilfe", "Unknown command. Type: help"));
    s->in[0] = 0;
    s->il = s->ic = 0;
}
/* ---------- Tab completion ---------- */
#define NCAND 64
static char cand[NCAND][VFS_NAME + 1];
static int ncand, cdir[NCAND];
static const char *cpre;
static int cmp_pre(const char *a, const char *pre) {
    for (; *pre; a++, pre++) {
        u8 x = (u8)*a, y = (u8)*pre;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return 0;
    }
    return 1;
}
static int ccb(const vfs_ent *e, void *x) {
    (void)x;
    if (ncand < NCAND && cmp_pre(e->name, cpre)) {
        strlcpy_(cand[ncand], e->name, VFS_NAME + 1);
        cdir[ncand++] = e->dir;
    }
    return 0;
}
static void sh_complete(sh_t *s) {
    int q = 0, st = 0;
    for (int i = 0; i < s->ic; i++) {
        if (s->in[i] == '"')
            q = !q;
        else if (s->in[i] == ' ' && !q)
            st = i + 1;
    }
    int first = 1;
    for (int i = 0; i < st; i++)
        if (s->in[i] != ' ')
            first = 0;
    char tok[SW];
    int tl = s->ic - st;
    memcpy(tok, s->in + st, tl);
    tok[tl] = 0;
    char *t = tok[0] == '"' ? tok + 1 : tok;
    char dirp[SW];
    dirp[0] = 0;
    ncand = 0;
    if (first) {
        cpre = t;
        for (int i = 0; i < NCMD; i++)
            for (const char *p = sh_cmds[i].n; *p;) {
                const char *e = p;
                while (*e && *e != ' ')
                    e++;
                char w[24];
                int l = (int)(e - p) < 23 ? (int)(e - p) : 23;
                memcpy(w, p, l);
                w[l] = 0;
                if (ncand < NCAND && cmp_pre(w, cpre)) {
                    strlcpy_(cand[ncand], w, VFS_NAME + 1);
                    cdir[ncand++] = 0;
                }
                p = *e ? e + 1 : e;
            }
    } else {
        char *sl = strrchr(t, '/');
        if (sl) {
            int dl = (int)(sl - t) + 1;
            memcpy(dirp, t, dl);
            dirp[dl] = 0;
            cpre = sl + 1;
        } else
            cpre = t;
        static char p[VFS_PATH];
        if (vfs_norm(s->cwd, dirp[0] ? dirp : ".", p, VFS_PATH) || fsx_list(p, ccb, 0) < 0)
            return;
    }
    if (!ncand)
        return;
    /* the common beginning of all candidates */
    int cl = (int)strlen(cand[0]);
    for (int i = 1; i < ncand; i++) {
        int k = 0;
        while (k < cl && cand[i][k] && cmp_pre(cand[i] + k, (char[]){cand[0][k], 0}))
            k++;
        cl = k;
    }
    char nt[SW * 2];
    int quote = tok[0] == '"';
    char name[VFS_NAME + 1];
    memcpy(name, cand[0], cl);
    name[cl] = 0;
    if (strchr(name, ' ') || strchr(dirp, ' '))
        quote = 1;
    ksnprintf(nt, sizeof nt, "%s%s%s", quote ? "\"" : "", dirp, name);
    if (ncand == 1) {
        int k = (int)strlen(nt);
        if (cdir[0])
            nt[k++] = '/';
        else {
            if (quote)
                nt[k++] = '"';
            nt[k++] = ' ';
        }
        nt[k] = 0;
    } else { /* several: show them */
        char b[SW + 1];
        int bl = 0;
        b[0] = 0;
        for (int i = 0; i < ncand; i++) {
            int l = (int)strlen(cand[i]) + (cdir[i] ? 1 : 0);
            if (bl && bl + l + 2 > 60) {
                sh_put(s, b);
                bl = 0;
                b[0] = 0;
            }
            bl += ksnprintf(b + bl, sizeof b - bl, "%s%s%s", bl ? "  " : "", cand[i],
                            cdir[i] ? "/" : "");
        }
        if (bl)
            sh_put(s, b);
    }
    /* replace the word before the cursor */
    int nl = (int)strlen(nt), after = s->il - s->ic;
    if (st + nl + after >= SW)
        return;
    memmove(s->in + st + nl, s->in + s->ic, after + 1);
    memcpy(s->in + st, nt, nl);
    s->il = st + nl + after;
    s->ic = st + nl;
}
static void sh_draw(win_t *w) {
    win_title(w, TR("Befehle", "Commands"));
    sh_t *s = w->st;
    cfill(w, ST);
    int h = ch(w) - 1, st = s->n - h - s->top;
    if (st < 0)
        st = 0;
    for (int i = 0; i < h && st + i < s->n; i++)
        ctext(w, 0, i, ST, s->l[st + i]);
    int y = s->n - st;
    if (y > h)
        y = h;
    char pr[40];
    int cl = strlen(s->cwd);
    ksnprintf(pr, 40, "%s> ", cl > 24 ? s->cwd + cl - 24 : s->cwd);
    int pl = strlen(pr);
    ctext(w, 0, y, SI, pr);
    int o = s->ic > cw(w) - pl - 1 ? s->ic - cw(w) + pl + 1 : 0;
    for (int i = o; i < s->il; i++)
        cput(w, pl + i - o, y, s->in[i], AT(C_VOID, C_GLOW));
    w->cx = pl + s->ic - o;
    w->cy = y;
    w->cur = 1;
}
static void sh_key(win_t *w, key_t k) {
    sh_t *s = w->st;
    if (k.key == K_ENTER)
        sh_exec(s);
    else if (k.key == K_BS) {
        if (s->ic) {
            memmove(s->in + s->ic - 1, s->in + s->ic, s->il - s->ic + 1);
            s->ic--;
            s->il--;
        }
    } else if (k.key == K_DEL) {
        if (s->ic < s->il) {
            memmove(s->in + s->ic, s->in + s->ic + 1, s->il - s->ic);
            s->il--;
        }
    } else if (k.key == K_LEFT) {
        if (s->ic)
            s->ic--;
    } else if (k.key == K_RIGHT) {
        if (s->ic < s->il)
            s->ic++;
    } else if (k.key == K_HOME)
        s->ic = 0;
    else if (k.key == K_END)
        s->ic = s->il;
    else if (k.key == K_UP || k.key == K_DOWN) { /* earlier inputs */
        if (k.key == K_UP && s->hp > 0)
            s->hp--;
        else if (k.key == K_DOWN && s->hp < s->hn)
            s->hp++;
        else
            return;
        strlcpy_(s->in, s->hp < s->hn ? s->hist[s->hp] : "", SW);
        s->il = s->ic = (int)strlen(s->in);
    } else if (k.key == K_TAB)
        sh_complete(s);
    else if (k.key == K_PGUP) {
        if (s->top < s->n)
            s->top += ch(w) - 1;
    } else if (k.key == K_PGDN) {
        s->top -= ch(w) - 1;
        if (s->top < 0)
            s->top = 0;
    } else if (k.key >= 32 && k.key < 256 && !(k.mods & (M_CTRL | M_ALT)) && s->il < SW - 1) {
        memmove(s->in + s->ic + 1, s->in + s->ic, s->il - s->ic + 1);
        s->in[s->ic++] = (char)k.key;
        s->il++;
    }
}
static const app_t sh_app = {sh_draw, sh_key, 0, 0, simple_free};
void app_shell(void) {
    sh_t *s = kmalloc(sizeof *s);
    if (!s)
        return;
    memset(s, 0, sizeof *s);
    strlcpy_(s->cwd, fsx_home(), VFS_PATH);
    if (!fsx_quiet(s->cwd))
        strlcpy_(s->cwd, "/drives", VFS_PATH);
    sh_put(s, TR("Violyx Befehle. Tippe: hilfe   (Tab ergänzt, Pfeil hoch holt Früheres)",
                 "Violyx commands. Type: help   (Tab completes, Up arrow recalls)"));
    win_t *mw = win_open(TR("Befehle", "Commands"), 8, 4, 64, 15, &sh_app, s);
    win_menu(mw, TR("Befehle", "Commands"), "*Info");
    if (!mw)
        kfree(s);
}
/* ---------- Calculator ---------- */
/* Fixed point: 4 decimal places, 64 bit, up to about 900 trillion */
typedef long long i64;
#define SC     10000
#define I64MAX 0x7FFFFFFFFFFFFFFFLL
typedef struct {
    char in[32];
    int n;
    char hist[8][72];
    int hn;
} calc_t;
static const char *cp;
static int cerr;
static u64 udm(u64 n, u64 d, u64 *r) {
    u64 q = 0, m = 0;
    for (int i = 63; i >= 0; i--) {
        m = (m << 1) | ((n >> i) & 1);
        if (m >= d) {
            m -= d;
            q |= 1ULL << i;
        }
    }
    if (r)
        *r = m;
    return q;
}
static i64 cadd(i64 a, i64 b) {
    i64 r;
    if (__builtin_add_overflow(a, b, &r)) {
        cerr = 3;
        return 0;
    }
    return r;
}
static i64 cm(i64 a, i64 b) {
    i64 r;
    if (__builtin_mul_overflow(a, b, &r)) {
        cerr = 3;
        return 0;
    }
    return r;
}
static u64 mag(i64 v) {
    return v < 0 ? -(u64)v : (u64)v;
}
static i64 cmul(i64 a, i64 b) {
    int neg = (a < 0) != (b < 0);
    u64 af, bf, ai = udm(mag(a), SC, &af), bi = udm(mag(b), SC, &bf);
    i64 r = cm(cm((i64)ai, (i64)bi), SC);
    r = cadd(r, cm((i64)ai, (i64)bf));
    r = cadd(r, cm((i64)af, (i64)bi));
    r = cadd(r, (i64)udm(af * bf, SC, 0));
    return neg ? -r : r;
}
static i64 cdiv(i64 a, i64 b) {
    if (!b) {
        cerr = 2;
        return 0;
    }
    int neg = (a < 0) != (b < 0);
    u64 m, q = udm(mag(a), mag(b), &m);
    i64 r = cm((i64)q, SC);
    int f = 0;
    for (int i = 0; i < 4; i++) {
        if (m > 0x1999999999999999ULL) {
            cerr = 3;
            return 0;
        }
        m *= 10;
        u64 d = udm(m, mag(b), &m);
        f = f * 10 + (int)d;
    }
    r = cadd(r, f);
    return neg ? -r : r;
}
static i64 cmod(i64 a, i64 b) {
    if (!b) {
        cerr = 2;
        return 0;
    }
    u64 m;
    udm(mag(a), mag(b), &m);
    return a < 0 ? -(i64)m : (i64)m;
}
static i64 cexpr(void);
static void ws(void) {
    while (*cp == ' ')
        cp++;
}
static i64 catom(void) {
    ws();
    if (*cp == '-') {
        cp++;
        return -catom();
    }
    if (*cp == '(') {
        cp++;
        i64 v = cexpr();
        ws();
        if (*cp == ')')
            cp++;
        else
            cerr = 1;
        return v;
    }
    if ((*cp < '0' || *cp > '9') && *cp != ',' && *cp != '.') {
        cerr = 1;
        return 0;
    }
    i64 v = 0;
    while (*cp >= '0' && *cp <= '9') {
        if (v > 92233720368546LL) {
            cerr = 3;
            return 0;
        }
        v = v * 10 + *cp++ - '0';
    }
    v *= SC;
    if (*cp == ',' || *cp == '.') {
        cp++;
        int f = SC / 10;
        while (*cp >= '0' && *cp <= '9') {
            v += f * (*cp++ - '0');
            f /= 10;
        }
    }
    return v;
}
static i64 cterm(void) {
    i64 v = catom();
    for (;;) {
        ws();
        char o = *cp;
        if (o == 'x' || o == 'X')
            o = '*';
        if (o == ':')
            o = '/';
        if (o != '*' && o != '/' && o != '%')
            return v;
        cp++;
        i64 r = catom();
        v = o == '*' ? cmul(v, r) : o == '/' ? cdiv(v, r) : cmod(v, r);
        if (cerr)
            return 0;
    }
}
static i64 cexpr(void) {
    i64 v = cterm();
    for (;;) {
        ws();
        char o = *cp;
        if (o != '+' && o != '-')
            return v;
        cp++;
        i64 r = cterm();
        v = cadd(v, o == '+' ? r : -r);
        if (cerr)
            return 0;
    }
}
static void cfmt(i64 v, char *o) {
    u64 fr, ip = udm(mag(v), SC, &fr);
    char t[24];
    int n = 0, p = 0;
    if (v < 0)
        o[p++] = '-';
    do {
        u64 r;
        ip = udm(ip, 10, &r);
        t[n++] = '0' + (int)r;
    } while (ip);
    while (n)
        o[p++] = t[--n];
    if (fr) {
        o[p++] = cfg_lang ? '.' : ',';
        int f = (int)fr;
        int d = SC / 10;
        while (f && d) {
            o[p++] = '0' + f / d;
            f %= d;
            d /= 10;
        }
    }
    o[p] = 0;
}
static void ca_draw(win_t *w) {
    win_title(w, TR("Rechner", "Calculator"));
    calc_t *c = w->st;
    cfill(w, T);
    for (int i = 0; i < c->hn; i++)
        ctext(w, 1, i, DIM, c->hist[i]);
    int y = ch(w) - 1;
    ctext(w, 1, y, HI, "= ");
    ctext(w, 3, y, T, c->in);
    w->cx = 3 + c->n;
    w->cy = y;
    w->cur = 1;
}
static void ca_key(win_t *w, key_t k) {
    calc_t *c = w->st;
    if (k.key == K_BS) {
        if (c->n)
            c->in[--c->n] = 0;
    } else if (k.key == K_ENTER && c->n) {
        cp = c->in;
        cerr = 0;
        i64 v = cexpr();
        ws();
        if (*cp && !cerr)
            cerr = 1;
        int max = ch(w) - 1;
        if (max > 8)
            max = 8;
        while (c->hn >= max) {
            memmove(c->hist[0], c->hist[1], (c->hn - 1) * 72);
            c->hn--;
        }
        char r[32];
        if (!cerr)
            cfmt(v, r);
        ksnprintf(c->hist[c->hn], 72, "%s = %s", c->in,
                  cerr == 2   ? TR("Division durch 0", "Division by 0")
                  : cerr == 3 ? TR("Zahl zu groß", "Number too large")
                  : cerr      ? TR("Eingabefehler", "Input error")
                              : r);
        c->hn++;
        c->in[0] = 0;
        c->n = 0;
    } else if (k.key >= 32 && k.key < 127 && c->n < 31 && !(k.mods & (M_CTRL | M_ALT))) {
        c->in[c->n++] = k.key;
        c->in[c->n] = 0;
    }
}
static const app_t ca_app = {ca_draw, ca_key, 0, 0, simple_free};
void app_calc(void) {
    calc_t *c = kmalloc(sizeof *c);
    if (!c)
        return;
    memset(c, 0, sizeof *c);
    win_t *mw = win_open(TR("Rechner", "Calculator"), 36, 3, 42, 12, &ca_app, c);
    win_menu(mw, TR("Rechner", "Calculator"), "*Info");
    if (!mw)
        kfree(c);
}
/* ---------- Pong ---------- */
#define PH  4
#define WIN 7
typedef struct {
    int w, h, by, bx, vx, vy, py, cy, sp, sc, state, err;
    u32 rng;
} pong_t; /* Coordinates in 8.8 fixed point */
static int prnd(pong_t *p, int n) {
    p->rng = p->rng * 1103515245u + 12345u;
    return (p->rng >> 16) % n;
}
static void serve(pong_t *p, int dir) {
    p->bx = (p->w / 2) << 8;
    p->by = (1 + prnd(p, p->h - 2)) << 8;
    p->vx = dir * 0xB0;
    p->vy = (prnd(p, 2) ? 1 : -1) * (0x28 + prnd(p, 0x38));
    p->err = prnd(p, 5) - 2;
}
static void pong_reset(pong_t *p, win_t *w) {
    p->w = cw(w);
    p->h = ch(w);
    p->py = p->cy = ((p->h + 1 - PH) / 2) << 8;
    p->sp = p->sc = 0;
    p->state = 0;
    serve(p, 1);
}
static void bounce(pong_t *p, int pad, int dir) {
    int off = (p->by >> 8) - (pad >> 8) - PH / 2;
    p->vy = off * 0x38 + prnd(p, 0x18) - 0x0C;
    int s = (p->vx < 0 ? -p->vx : p->vx) + 0x0C;
    if (s > 0x1A0)
        s = 0x1A0;
    p->vx = dir * s;
}
static void pong_step(win_t *w) {
    pong_t *p = w->st;
    if (p->w != cw(w) || p->h != ch(w))
        pong_reset(p, w);
    if (p->state != 1)
        return;
    int maxp = (p->h - PH) << 8;
    if (key_down(0x48) || key_down(0x11))
        p->py -= 0xA0;
    if (key_down(0x50) || key_down(0x1F))
        p->py += 0xA0;
    if (p->py < 0x100)
        p->py = 0x100;
    if (p->py > maxp)
        p->py = maxp;
    int target = p->vx > 0 ? (p->by - ((PH / 2) << 8) + (p->err << 8)) : ((p->h - PH) / 2) << 8,
        spd = 0x48 + p->sp * 2, d = target - p->cy; /* with a reaction limit, so it can be beaten */
    p->cy += d > spd ? spd : d < -spd ? -spd : d;
    if (p->cy < 0x100)
        p->cy = 0x100;
    if (p->cy > maxp)
        p->cy = maxp;
    int ox = p->bx;
    p->bx += p->vx;
    p->by += p->vy;
    if (p->by < 0x100) {
        p->by = 0x200 - p->by;
        p->vy = -p->vy;
    }
    if (p->by >= (p->h) << 8) {
        p->by = (2 * p->h << 8) - p->by - 0x100;
        p->vy = -p->vy;
    }
    int lx = 3 << 8, rx = (p->w - 4) << 8, row = p->by >> 8;
    if (p->vx < 0 && ox >= lx && p->bx < lx && row >= p->py >> 8 && row < (p->py >> 8) + PH) {
        p->bx = lx;
        bounce(p, p->py, 1);
    } else if (p->vx > 0 && ox <= rx && p->bx > rx && row >= p->cy >> 8 &&
               row < (p->cy >> 8) + PH) {
        p->bx = rx;
        bounce(p, p->cy, -1);
    }
    if (p->bx < 0) {
        p->sc++;
        if (p->sc >= WIN)
            p->state = 2;
        else
            serve(p, 1);
    } else if (p->bx >= p->w << 8) {
        p->sp++;
        if (p->sp >= WIN)
            p->state = 2;
        else
            serve(p, -1);
    }
}
static void pong_draw(win_t *w) {
    pong_t *p = w->st;
    if (p->w != cw(w) || p->h != ch(w))
        pong_reset(p, w);
    u8 bg = AT(C_VOID, C_DIM);
    cfill(w, bg);
    char s[40];
    ksnprintf(s, 40, TR("Du  %d : %d  Violyx", "You  %d : %d  Violyx"), p->sp, p->sc);
    ctext(w, (p->w - (int)strlen(s)) / 2, 0, AT(C_VOID, C_GLOW), s);
    for (int y = 1; y < p->h; y += 2)
        cput(w, p->w / 2, y, 0xB3, bg);
    for (int i = 0; i < PH; i++) {
        cput(w, 2, (p->py >> 8) + i, 0xDB, AT(C_VOID, C_NEON));
        cput(w, p->w - 3, (p->cy >> 8) + i, 0xDB, AT(C_VOID, C_HOT));
    }
    if (p->state == 1 || p->state == 3)
        cput(w, p->bx >> 8, p->by >> 8, 0xFE, AT(C_VOID, C_GLOW));
    const char *m =
        p->state == 0   ? TR("Enter startet das Spiel", "Press Enter to start")
        : p->state == 3 ? TR("Pause - P geht weiter", "Paused - press P")
        : p->state == 2
            ? (p->sp >= WIN
                   ? TR("Gewonnen! Enter = Neues Spiel", "You win! Enter = New game")
                   : TR("Violyx gewinnt. Enter = Neues Spiel", "Violyx wins. Enter = New game"))
            : 0;
    if (m) {
        int l = 0;
        for (const char *q = m; *q; q++)
            l++;
        ctext(w, (p->w - l) / 2, p->h / 2, AT(C_VOID, C_LAV), m);
    }
    strlcpy_(w->foot, TR(" Pfeile oben/unten   P Pause ", " Up/Down arrows   P Pause "), 64);
}
static void pong_key(win_t *w, key_t k) {
    pong_t *p = w->st;
    if (k.key == K_ENTER) {
        if (p->state == 0)
            p->state = 1;
        else if (p->state == 2) {
            pong_reset(p, w);
            p->state = 1;
        }
    } else if (k.key == 'p' || k.key == 'P') {
        if (p->state == 1)
            p->state = 3;
        else if (p->state == 3)
            p->state = 1;
    }
}
static void pong_frame(win_t *w) {
    pong_t *p = w->st;
    int vx = p->vx, vy = p->vy, sc = p->sc, st = p->state;
    pong_step(w);
    if (st != 1)
        return;
    if (p->sc != sc || p->state != st) {
        snd_tone(180, 250, 55, 2, 0);
    } else if ((vx < 0) != (p->vx < 0))
        snd_tone(440, 50, 45, 0, 0);
    else if ((vy < 0) != (p->vy < 0))
        snd_tone(220, 35, 30, 0, 0);
}
static const app_t pong_app = {pong_draw, pong_key, 0, 0, simple_free, pong_frame};
void app_pong(void) {
    pong_t *p = kmalloc(sizeof *p);
    if (!p)
        return;
    memset(p, 0, sizeof *p);
    p->rng = (u32)ticks * 2654435761u + 1;
    win_t *w = win_open("Pong", 7, 2, 66, 20, &pong_app, p);
    if (w)
        w->fixed = 1;
    win_menu(w, "Pong", "*Info");
    if (!w) {
        kfree(p);
        return;
    }
    pong_reset(p, w);
}
/* ---------- Settings ---------- */
typedef struct {
    int sel, dirty;
    rtc_t t;
    const char *msg;
} set_t;
static int mdays(int m, int y) {
    static const u8 d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && y % 4 == 0 ? 29 : d[m - 1];
}
static void se_tick(win_t *w) {
    set_t *s = w->st;
    if (!s->dirty)
        rtc_read(&s->t);
}
static void se_draw(win_t *w) {
    set_t *s = w->st;
    win_title(w, TR("Einstellungen", "Settings"));
    static const char *lbd[] = {"Tag",      "Monat",       "Jahr",       "Stunde",
                                "Minute",   "Hintergrund", "Farbschema", "Sprache",
                                "Tastatur", "Lautstärke",  "Ausgabe",    "Bildschirmschoner",
                                "Wartezeit"},
                      *lbe[] = {"Day",        "Month",         "Year",     "Hour",     "Minute",
                                "Background", "Colour scheme", "Language", "Keyboard", "Volume",
                                "Output",     "Screen saver",  "Wait"};
    const char **lb = cfg_lang ? lbe : lbd;
    int v[] = {s->t.day, s->t.mon, s->t.year, s->t.h, s->t.m};
    cfill(w, T);
    ctext(w, 2, 0, HI, TR("Datum und Uhrzeit", "Date and time"));
    ctext(w, 2, 7, HI, TR("Darstellung", "Appearance"));
    for (int i = 0; i < 13; i++) {
        int y = i < 5 ? i + 1 : i + 3;
        char b[50];
        if (i == 11)
            ksnprintf(b, 50, "%17s  < %s >", lb[i], saver_name(cfg_saver));
        else if (i == 12)
            ksnprintf(b, 50, "%17s  < %d min >", lb[i], saver_min[cfg_swait]);
        else if (i == 10) {
            static const char *od[] = {"Automatisch", "Lautsprecher", "HDMI"},
                              *oe[] = {"Automatic", "Speakers", "HDMI"};
            if (snd_ok())
                ksnprintf(b, 50, "%17s  < %s >", lb[i], (cfg_lang ? oe : od)[cfg_out]);
            else
                ksnprintf(b, 50, "%17s  -", lb[i]);
        } else if (i == 9) {
            if (snd_ok())
                ksnprintf(b, 50, "%17s  < %d >", lb[i], cfg_vol);
            else
                ksnprintf(b, 50, "%17s  %s", lb[i], TR("keine Soundkarte", "no sound card"));
        } else if (i < 5)
            ksnprintf(b, 50, "%17s  < %02d >", lb[i], v[i]);
        else
            ksnprintf(b, 50, "%17s  < %s >", lb[i],
                      i == 5   ? wall_name(cfg_wall)
                      : i == 6 ? scheme_name(cfg_scheme)
                      : i == 7 ? (cfg_lang ? "English" : "Deutsch")
                               : (cfg_kbd ? "US" : TR("Deutsch", "German")));
        int sel = s->sel == i;
        if (sel)
            for (int x = 1; x < cw(w) - 1; x++)
                cput(w, x, y, ' ', SEL);
        ctext(w, 2, y, sel ? SEL : T, b);
    }
    ctext(w, 2, 17, s->msg ? AT(C_PANEL, C_HOT) : DIM,
          s->msg                      ? s->msg
          : s->dirty                  ? TR("Enter übernimmt die Uhrzeit", "Enter applies the time")
          : s->sel == 11 && cfg_saver ? TR("Enter zeigt eine Vorschau", "Enter shows a preview")
                                      : TR("Pfeile wählen und ändern", "Arrows select and change"));
}
static void se_key(win_t *w, key_t k) {
    set_t *s = w->st;
    if (k.key == K_UP)
        s->sel = (s->sel + 12) % 13;
    else if (k.key == K_DOWN)
        s->sel = (s->sel + 1) % 13;
    else if (k.key == K_LEFT || k.key == K_RIGHT) {
        int d = k.key == K_RIGHT ? 1 : -1;
        s->msg = 0;
        if (s->sel >= 5) {
            if (s->sel == 5)
                wall_set((cfg_wall + WALLS + d) % WALLS);
            else if (s->sel == 6)
                scheme_set((cfg_scheme + SCHEMES + d) % SCHEMES);
            else if (s->sel == 7)
                cfg_lang = !cfg_lang;
            else if (s->sel == 8)
                cfg_kbd = !cfg_kbd;
            else if (s->sel == 11)
                cfg_saver = (cfg_saver + SAVERS + d) % SAVERS;
            else if (s->sel == 12)
                cfg_swait = (cfg_swait + SWAITS + d) % SWAITS;
            else if (s->sel == 10) {
                cfg_out = (cfg_out + 3 + d) % 3;
                snd_select();
                snd_tone(523, 120, 60, 1, 0);
                snd_tone(784, 160, 60, 1, 130);
            } else {
                cfg_vol += d;
                if (cfg_vol < 0)
                    cfg_vol = 0;
                if (cfg_vol > 10)
                    cfg_vol = 10;
                snd_tone(660, 120, 60, 1, 0);
            }
            if (cfg_save())
                s->msg = TR("Nur bis zum nächsten Start gültig", "Valid until next restart only");
            return;
        }
        rtc_t *t = &s->t;
        s->dirty = 1;
        t->s = 0;
        if (s->sel == 0) {
            int m = mdays(t->mon, t->year);
            t->day = (t->day - 1 + m + d) % m + 1;
        } else if (s->sel == 1)
            t->mon = (t->mon - 1 + 12 + d) % 12 + 1;
        else if (s->sel == 2)
            t->year = 2000 + (t->year - 2000 + 100 + d) % 100;
        else if (s->sel == 3)
            t->h = (t->h + 24 + d) % 24;
        else
            t->m = (t->m + 60 + d) % 60;
        int m = mdays(t->mon, t->year);
        if (t->day > m)
            t->day = m;
    } else if (k.key == K_ENTER && s->sel == 11 && cfg_saver) {
        saver_run(cfg_saver); /* preview */
    } else if (k.key == K_ENTER && s->dirty) {
        rtc_set(&s->t);
        s->dirty = 0;
        s->msg = cfg_save()
                     ? TR("Nur bis zum nächsten Start gültig", "Valid until next restart only")
                     : TR("Uhrzeit gespeichert", "Time saved");
    }
}
static const app_t se_app = {se_draw, se_key, se_tick, 0, simple_free};
void app_settings(void) {
    set_t *s = kmalloc(sizeof *s);
    if (!s)
        return;
    memset(s, 0, sizeof *s);
    rtc_read(&s->t);
    if (!win_open(TR("Einstellungen", "Settings"), 18, 2, 46, 20, &se_app, s))
        kfree(s);
}
/* ---------- Programs overview ---------- */
static void l_write(void) {
    app_write(0);
}
static const struct {
    const char *n, *en;
    void (*fn)(void);
    const char *ic[3];
    u8 c;
} progs[] = {
    {"Schreiben",
     "Write",
     l_write,
     {"\xDA\xC4\xC4\xC4\xC4\xBF", "\xB3\xF0\xF0\xF0 \xB3", "\xC0\xC4\xC4\xC4\xC4\xD9"},
     C_NEON},
    {"Internet",
     "Internet",
     app_web,
     {"\xDA\xC4\xC2\xC2\xC4\xBF", "\xC3\xC4\xC5\xC5\xC4\xB4", "\xC0\xC4\xC1\xC1\xC4\xD9"},
     C_HOT},
    {"Dateien",
     "Files",
     app_files,
     {"\xDC\xDC\xDC   ", "\xDB\xDB\xDB\xDB\xDB\xDB", "\xDB\xDB\xDB\xDB\xDB\xDB"},
     C_VIOLET},
    {"Befehle",
     "Commands",
     app_shell,
     {"\xDA\xC4\xC4\xC4\xC4\xBF", "\xB3>_  \xB3", "\xC0\xC4\xC4\xC4\xC4\xD9"},
     C_ORCH},
    {"Rechner",
     "Calculator",
     app_calc,
     {"\xDA\xC4\xC4\xC4\xC4\xBF", "\xB3+-x:\xB3", "\xC0\xC4\xC4\xC4\xC4\xD9"},
     C_HOT},
    {"Kalender",
     "Calendar",
     app_calendar,
     {"\xDA\xC2\xC4\xC4\xC2\xBF", "\xC3\xC4\xC4\xC4\xC4\xB4", "\xC0\xC4\xC4\xC4\xC4\xD9"},
     C_ORCH},
    {"Uhr", "Clock", app_clock, {" /\xC4\xC4\\ ", "\xB3 \xC0\xC4 \xB3", " \\\xC4\xC4/ "}, C_NEON},
    {"Malen",
     "Paint",
     app_paint,
     {"\xDB\xDB\xB1\xB1\xB0\xB0", "\xDB\xDB\xB1\xB1\xB0\xB0", "  \xFE   "},
     C_HOT},
    {"XZN", "XZN", app_xzn, {"\xDB \xDB   ", "  \xDB \x0E ", "\xDB \xDB   "}, C_NEON},
    {"Pong", "Pong", app_pong, {"\xDB    \xDB", "\xDB \xFE  \xDB", "\xDB    \xDB"}, C_NEON},
    {"Invaders", "Invaders", app_invaders, {" /@\\  ", "  \xB3   ", " \xDC\xDB\xDC  "}, C_HOT},
    {"Chomp",
     "Chomp",
     app_chomp,
     {"\xDB\xDB\xDB\xDB\xDB\xDB", "<( \xF9 \xEA", "\xDB\xDB\xDB\xDB\xDB\xDB"},
     C_ORCH},
    {"Mines",
     "Mines",
     app_mines,
     {"\xFA\xFA 1 \xFA", "\xFA \x0F 2\xFA", "\xFA\xFA \x10 \xFA"},
     C_VIOLET},
    {"Snake", "Snake", app_snake, {" ~~~~ ", "  ~~@ ", "      "}, C_NEON},
    {"Bricks", "Bricks", app_bricks, {"##  ##", "  ##  ", "  O   "}, C_HOT},
    {"Novastrike", "Novastrike", app_novastrike, {"  V   ", "  :   ", " /A\\ "}, C_ORCH},
    {"Einstellungen",
     "Settings",
     app_settings,
     {"\xC4\xFE\xC4\xC4\xC4\xC4", "\xC4\xC4\xC4\xC4\xFE\xC4", "\xC4\xC4\xFE\xC4\xC4\xC4"},
     C_VIOLET},
    {"Systeminfo",
     "System info",
     app_info,
     {" \xDA\xC4\xC4\xBF ", " \xB3i \xB3 ", " \xC0\xC4\xC4\xD9 "},
     C_ORCH},
    {"Hilfe", "Help", app_help, {" \xDA\xC4\xC4\xBF ", " \xB3? \xB3 ", " \xC0\xC4\xC4\xD9 "}, C_HOT}
#ifdef LAUNCHER_TEST
    ,
    {"Hilfe", "Help", app_help, {"  99  ", "  99  ", "  99  "}, C_NEON},
    {"Hilfe", "Help", app_help, {"  10  ", "  10  ", "  10  "}, C_VIOLET},
    {"Hilfe", "Help", app_help, {"  11  ", "  11  ", "  11  "}, C_ORCH},
    {"Hilfe", "Help", app_help, {"  12  ", "  12  ", "  12  "}, C_HOT},
    {"Hilfe", "Help", app_help, {"  13  ", "  13  ", "  13  "}, C_NEON},
    {"Hilfe", "Help", app_help, {"  14  ", "  14  ", "  14  "}, C_VIOLET}
#endif
};
#define NP    ((int)(sizeof progs / sizeof progs[0]))
#define PC    4
#define PV    2 /* visible rows */
#define MAXIP 48
static prog_info ipr[MAXIP];
#include "xznicon.h"
void app_launcher(void) {
    static int s, top;
    int ni = prog_scan(ipr, MAXIP);
    for (;;) {
        int all = NP + ni;
        if (s >= all)
            s = all - 1;
        int x, y, rows = (all + PC - 1) / PC;
        dlg_box(TR("Programme", "Programs"), 66, 17, &x, &y);
        if (s / PC < top)
            top = s / PC;
        if (s / PC >= top + PV)
            top = s / PC - PV + 1; /* the view follows the selection */
        for (int i = top * PC; i < all && i < (top + PV) * PC; i++) {
            int tx = x + 3 + (i % PC) * 15, ty = y + 2 + (i / PC - top) * 7, sel = i == s;
            u8 bg = sel ? C_SEL : C_RAISE;
            fill(tx, ty, 15, 6, ' ', AT(bg, C_LAV));
            int inst = i >= NP;
            if (!inst && progs[i].fn == app_xzn) {
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 6; c++)
                        gput(tx + 4 + c, ty + 1 + r, 250, xzn_icon, 24, c, r);
            } else
                for (int r = 0; r < 3; r++) {
                    const char *ic = inst ? ipr[i - NP].icon[r] : progs[i].ic[r];
                    for (int c = 0; c < 6 && ic[c]; c++)
                        put(tx + 4 + c, ty + 1 + r, (u8)ic[c],
                            AT(bg, sel    ? C_GLOW
                                   : inst ? C_NEON
                                          : progs[i].c));
                }
            const char *pn = inst ? ipr[i - NP].title : TR(progs[i].n, progs[i].en);
            int l = strlen(pn);
            if (l > 14)
                l = 14;
            for (int c = 0; c < l; c++)
                put(tx + (15 - l) / 2 + c, ty + 4, (u8)pn[c], AT(bg, sel ? C_GLOW : C_LAV));
            if (!inst)
                text(tx + (15 - l) / 2, ty + 4, AT(bg, sel ? C_GLOW : C_LAV), pn);
        }
        if (top > 0)
            put(x + 33, y + 1, 0x1E, AT(C_RAISE, C_HOT));
        if (top + PV < rows)
            put(x + 33, y + 15, 0x1F, AT(C_RAISE, C_HOT));
        if (s >= NP)
            text(x + 3, y + 15, AT(C_RAISE, C_MID), TR("Entf deinstalliert", "Del uninstalls"));
        ui_show();
        key_t k;
        if (!ui_wait(&k))
            continue;
        if (k.key == K_ESC || k.key == K_F2)
            return;
        if (k.key == K_ENTER) {
            if (s < NP)
                progs[s].fn();
            else {
                int r = prog_run(ipr[s - NP].name);
                if (r)
                    dlg_msg(ipr[s - NP].title, vfs_errstr(r, cfg_lang));
            }
            return;
        }
        if (k.key == K_DEL && s >= NP) {
            char q[72];
            ksnprintf(q, 72, TR("%s und seine Daten löschen?", "Delete %s and its data?"),
                      ipr[s - NP].title);
            if (dlg_confirm(TR("Deinstallieren", "Uninstall"), q)) {
                int r = prog_uninstall(ipr[s - NP].name);
                if (r)
                    dlg_msg(TR("Deinstallieren", "Uninstall"), vfs_errstr(r, cfg_lang));
                ni = prog_scan(ipr, MAXIP);
            }
            continue;
        }
        if (k.key == K_RIGHT) {
            if (s < all - 1)
                s++;
        } else if (k.key == K_LEFT) {
            if (s)
                s--;
        } else if (k.key == K_DOWN) {
            if (s / PC < rows - 1) {
                s += PC;
                if (s >= all)
                    s = all - 1;
            }
        } else if (k.key == K_UP) {
            if (s >= PC)
                s -= PC;
        }
    }
}
/* ---------- System info ---------- */
static void in_draw(win_t *w) {
    char b[60];
    rtc_t r;
    rtc_read(&r);
    u64 s = ticks / 100;
    cfill(w, T);
    ctext(w, 1, 0, HI, "Violyx " VERSION);
    ksnprintf(b, 60, TR("RAM:      %ld MB", "RAM:      %ld MB"), mem_total_kb / 1024);
    ctext(w, 1, 2, T, b);
    ksnprintf(b, 60, TR("Heap:     %ld KB frei", "Heap:     %ld KB free"), heap_free() / 1024);
    ctext(w, 1, 3, T, b);
    ksnprintf(b, 60, TR("Laufzeit: %d:%02d:%02d", "Uptime:   %d:%02d:%02d"), (u32)(s / 3600),
              (u32)((s / 60) % 60), (u32)(s % 60));
    ctext(w, 1, 4, T, b);
    {
        u64 t_, f_;
        u32 n_;
        vfs_usage(&t_, &f_, &n_);
        ksnprintf(b, 60, TR("Platte:   %ld MB frei von %ld MB", "Disk:     %ld MB free of %ld MB"),
                  f_ >> 20, t_ >> 20);
        ctext(w, 1, 5, T, b);
    }
    ksnprintf(b, 60, TR("Status:   %s", "Status:   %s"), fs_status());
    ctext(w, 1, 6, T, b);
    ksnprintf(b, 60, TR("USB-Tastaturen: %d", "USB keyboards: %d"), usb_keyboards);
    ctext(w, 1, 7, T, b);
    ksnprintf(b, 60, TR("Netzwerk: %s", "Network:  %s"), net_status());
    ctext(w, 1, 8, T, b);
}
static const app_t in_app = {in_draw, 0, 0, 0, 0};
void app_info(void) {
    win_open("Systeminfo", 20, 6, 44, 11, &in_app, 0);
}
/* ---------- Help ---------- */
static const char *help_de[] = {"Willkommen bei Violyx!",
                                "",
                                "F1 Hilfe    F2 Programme   F3 Dateien   F4 Befehle",
                                "F5 Zoom     F6 Fenster wechseln   F7 Internet   F9 Rechner",
                                "F10 Menü    Alt+S/P/F/H öffnet Menüs direkt",
                                "Esc schließt das Fenster (auch Strg+W oder Alt+F3)",
                                "USB-Sticks (FAT32, exFAT): Ordner /drives unter F3",
                                "Strg+Pfeile verschiebt, Umschalt+Pfeile ändert die Größe",
                                "",
                                "Im Schreibprogramm:",
                                "Strg+S speichern   Strg+O öffnen",
                                "Strg+Pos1/Ende springt an Anfang/Ende",
                                "",
                                "Sprache: System, Einstellungen, Sprache",
                                0};
static const char *help_en[] = {"Welcome to Violyx!",
                                "",
                                "F1 Help     F2 Programs    F3 Files     F4 Commands",
                                "F5 Zoom     F6 Switch   F7 Internet   F9 Calculator",
                                "F10 Menu    Alt+S/P/W/H opens menus directly",
                                "Esc closes the window (also Ctrl+W or Alt+F3)",
                                "USB sticks (FAT32, exFAT): folder /drives in F3",
                                "Ctrl+Arrows moves, Shift+Arrows resizes",
                                "",
                                "In Write:",
                                "Ctrl+S save   Ctrl+O open",
                                "Ctrl+Home/End jumps to start/end",
                                "",
                                "Language: System, Settings, Language",
                                0};
static void he_draw(win_t *w) {
    win_title(w, TR("Hilfe", "Help"));
    cfill(w, T);
    const char **help = cfg_lang ? help_en : help_de;
    for (int i = 0; help[i]; i++)
        ctext(w, 1, i, i == 0 ? HI : T, help[i]);
}
static const app_t he_app = {he_draw, 0, 0, 0, 0};
void app_help(void) {
    win_open(TR("Hilfe", "Help"), 9, 4, 62, 16, &he_app, 0);
}
