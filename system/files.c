/* Files: the file manager, a program in ring 3 */
#include "common.h"
/* ---------- Files ---------- */
typedef struct {
    char cwd[VFS_PATH];
    fs_item *it;
    int n, s, top;
    u32 gen;
} fm_t;
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
void prog_main(void) {
    app_files();
}
