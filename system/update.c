/* Update: brings Violyx to a new version, online from merecete.org or from a .vup file (for
 * computers without internet). Every update is signed; without the right signature nothing is
 * installed. The kernel then puts the files into place and keeps the previous version. */
#include "common.h"
#include "web/web.h"
#include "../kernel/version.h"
#include "update_key.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#define BASE_URL "https://merecete.org/violyx/update/"
#define VUPMAX   (48u << 20)
enum {
    S_START, /* nothing checked yet */
    S_NEW,   /* a newer version is there (online or from a file) */
    S_NONE,  /* up to date */
    S_ERR
};
typedef struct {
    int st, top, file;   /* file: the update is in buf already (from a .vup) */
    char ver[24], fname[64], sha[65], alt[24], msg[160], path[VFS_PATH];
    u64 size;
    char *notes; /* lines, from the list or the file */
    u8 *buf;     /* the .vup */
    u64 n;
    int canup;   /* 0: the boot partition can take an update */
} up_t;
/* ---------- signature ---------- */
static int sig_ok(const u8 *msg, u64 n, const u8 *sig) {
    u8 h[32];
    if (mbedtls_sha256(msg, n, h, 0))
        return 0;
    mbedtls_ecp_group g;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&g);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int ok = !mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) &&
             !mbedtls_ecp_point_read_binary(&g, &q, update_pub, sizeof update_pub) &&
             !mbedtls_mpi_read_binary(&r, sig, 32) && !mbedtls_mpi_read_binary(&s, sig + 32, 32) &&
             !mbedtls_ecdsa_verify(&g, h, 32, &q, &r, &s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&g);
    return ok;
}
static int hexv(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
/* "1.2.3" against "1.2.10": < 0, 0, > 0 */
static int vercmp(const char *a, const char *b) {
    while (*a || *b) {
        long x = 0, y = 0;
        while (*a >= '0' && *a <= '9')
            x = x * 10 + *a++ - '0';
        while (*b >= '0' && *b <= '9')
            y = y * 10 + *b++ - '0';
        if (x != y)
            return x < y ? -1 : 1;
        if (*a)
            a++;
        if (*b)
            b++;
    }
    return 0;
}
/* the lines between "notes" and "end", for showing */
static void take_notes(up_t *u, const char *s, const char *e) {
    kfree(u->notes);
    u->notes = 0;
    u->top = 0;
    const char *a = 0;
    for (const char *p = s; p < e;) {
        const char *q = p;
        while (q < e && *q != '\n')
            q++;
        if (q - p == 5 && !memcmp(p, "notes", 5))
            a = q + 1;
        else if (a && q - p == 3 && !memcmp(p, "end", 3)) {
            u->notes = kmalloc((size_t)(p - a) + 1);
            if (u->notes) {
                memcpy(u->notes, a, (size_t)(p - a));
                u->notes[p - a] = 0;
            }
            return;
        }
        p = q + 1;
    }
}
/* the value of "key value" in a header */
static int field(const char *s, const char *e, const char *key, char *o, int n) {
    int kl = (int)strlen(key);
    for (const char *p = s; p < e;) {
        const char *q = p;
        while (q < e && *q != '\n')
            q++;
        if (q - p > kl && !memcmp(p, key, kl) && p[kl] == ' ') {
            int l = (int)(q - p - kl - 1);
            if (l >= n)
                l = n - 1;
            memcpy(o, p + kl + 1, l);
            o[l] = 0;
            return 1;
        }
        if (q - p == 5 && !memcmp(p, "notes", 5))
            return 0;
        p = q + 1;
    }
    return 0;
}
/* ---------- a .vup: signed, then the header ---------- */
static const char *vup_check(up_t *u) {
    if (u->n < 80 || memcmp(u->buf, "VUP1\n", 5))
        return TR("Das ist keine Violyx-Update-Datei.", "This is not a Violyx update file.");
    if (!sig_ok(u->buf, u->n - 64, u->buf + u->n - 64))
        return TR("Die Signatur stimmt nicht! Die Datei ist beschädigt oder nicht von Violyx. "
                  "Es wurde nichts verändert.",
                  "The signature is wrong! The file is damaged or not from Violyx. Nothing was "
                  "changed.");
    const char *s = (const char *)u->buf, *e = s;
    u64 lim = u->n < 65536 ? u->n : 65536;
    for (u64 i = 5; i + 5 <= lim; i++)
        if (!memcmp(s + i, "\nend\n", 5)) {
            e = s + i + 5;
            break;
        }
    if (e == s || !field(s, e, "version", u->ver, sizeof u->ver))
        return TR("Die Update-Datei ist unvollständig.", "The update file is incomplete.");
    take_notes(u, s, e);
    return 0;
}
/* ---------- online ---------- */
static win_t *pw;
static void up_draw(win_t *w);
static void show(win_t *w) { /* draws now, also in the middle of long work */
    up_draw(w);
    ui_present();
    ui_render();
}
static void prog_cb(const char *t) {
    up_t *u = pw->st;
    strlcpy_(u->msg, t, sizeof u->msg);
    show(pw);
}
static void check_online(win_t *w, up_t *u) {
    static char b[65536];
    http_res r;
    pw = w;
    u->st = S_START;
    strlcpy_(u->msg, TR("Frage merecete.org ...", "Asking merecete.org ..."), sizeof u->msg);
    show(w);
    int n = http_fetch(BASE_URL "update.txt", 0, b, sizeof b - 1, &r, prog_cb,
                       HTTP_NOCOOKIE | HTTP_NOGZIP);
    if (n <= 0 || r.status != 200) {
        u->st = S_ERR;
        if (n > 0 || r.status)
            ksnprintf(u->msg, sizeof u->msg,
                      TR("Der Update-Server antwortet mit HTTP %d.",
                         "The update server answers with HTTP %d."),
                      r.status);
        else
            ksnprintf(u->msg, sizeof u->msg, "%s %s",
                      TR("Keine Antwort vom Update-Server:", "No answer from the update server:"),
                      r.err[0] ? r.err : net_ready() ? "?" : TR("kein Netzwerk", "no network"));
        return;
    }
    b[n] = 0;
    char *sg = 0;
    for (char *p = b; (p = strstr(p, "\nsig ")); p++)
        sg = p + 1;
    u8 sig[64];
    int ok = sg && strlen(sg) >= 4 + 128;
    for (int i = 0; ok && i < 64; i++) {
        int h = hexv(sg[4 + i * 2]), l = hexv(sg[5 + i * 2]);
        ok = h >= 0 && l >= 0;
        sig[i] = (u8)(h << 4 | l);
    }
    if (!ok || memcmp(b, "violyx-update 1\n", 16) || !sig_ok((u8 *)b, (u64)(sg - b), sig)) {
        u->st = S_ERR;
        strlcpy_(u->msg,
                 TR("Die Update-Liste ist nicht richtig signiert. Es wird nichts geladen.",
                    "The update list is not signed correctly. Nothing is loaded."),
                 sizeof u->msg);
        return;
    }
    char sz[24];
    if (!field(b, sg, "version", u->ver, sizeof u->ver) ||
        !field(b, sg, "file", u->fname, sizeof u->fname) || !field(b, sg, "size", sz, 24) ||
        !field(b, sg, "sha256", u->sha, sizeof u->sha)) {
        u->st = S_ERR;
        strlcpy_(u->msg, TR("Die Update-Liste ist unvollständig.", "The update list is incomplete."),
                 sizeof u->msg);
        return;
    }
    u->size = 0;
    for (char *p = sz; *p >= '0' && *p <= '9'; p++)
        u->size = u->size * 10 + (u64)(*p - '0');
    take_notes(u, b, sg);
    u->file = 0;
    if (vercmp(u->ver, VERSION) > 0) {
        u->st = S_NEW;
        u->msg[0] = 0;
    } else {
        u->st = S_NONE;
        ksnprintf(u->msg, sizeof u->msg,
                  TR("Violyx ist aktuell, %s ist die neueste Version.",
                     "Violyx is up to date, %s is the newest version."),
                  VERSION);
    }
}
static int download(win_t *w, up_t *u) {
    if (!u->size || u->size > VUPMAX) {
        strlcpy_(u->msg, TR("Das Update ist zu groß.", "The update is too big."), sizeof u->msg);
        return 0;
    }
    kfree(u->buf);
    u->buf = kmalloc(u->size + 16384); /* the answer comes with its HTTP header */
    if (!u->buf) {
        strlcpy_(u->msg, TR("Zu wenig Arbeitsspeicher.", "Not enough memory."), sizeof u->msg);
        return 0;
    }
    char url[200];
    ksnprintf(url, sizeof url, BASE_URL "%s", u->fname);
    strlcpy_(u->msg, TR("Lade herunter ...", "Downloading ..."), sizeof u->msg);
    show(w);
    http_res r;
    pw = w;
    int n = http_fetch(url, 0, (char *)u->buf, (int)u->size + 16384, &r, prog_cb,
                       HTTP_NOCOOKIE | HTTP_NOGZIP);
    u->n = n > 0 ? (u64)n : 0;
    u8 h[32];
    char hx[65];
    if (n > 0 && r.status == 200 && u->n == u->size && !mbedtls_sha256(u->buf, u->n, h, 0)) {
        for (int i = 0; i < 32; i++)
            ksnprintf(hx + i * 2, 3, "%02x", h[i]);
        if (!strcmp(hx, u->sha))
            return 1;
    }
    ksnprintf(u->msg, sizeof u->msg, "%s (%d, HTTP %d%s%s)",
              TR("Das Herunterladen ist fehlgeschlagen oder die Datei ist beschädigt.",
                 "The download failed or the file is damaged."),
              n, r.status, r.err[0] ? ", " : "", r.err);
    return 0;
}
/* ---------- putting it in ---------- */
static void install(win_t *w, up_t *u) {
    const char *T_ = TR("Aktualisieren", "Update");
    if (!u->file && !download(w, u))
        return;
    const char *e = vup_check(u);
    if (e) {
        strlcpy_(u->msg, e, sizeof u->msg);
        u->st = S_ERR;
        return;
    }
    strlcpy_(u->msg, TR("Wird eingespielt ... bitte nicht ausschalten.",
                        "Installing ... please do not switch off."),
             sizeof u->msg);
    show(w);
    long r = upd_call(20, (long)u->buf, (long)u->n);
    if (r) {
        u->st = S_ERR;
        ksnprintf(u->msg, sizeof u->msg, "%s (%ld)",
                  TR("Das Einspielen ist fehlgeschlagen, die bisherige Version bleibt.",
                     "Installing failed, the current version stays."),
                  r);
        return;
    }
    char t[200];
    ksnprintf(t, sizeof t,
              TR("Violyx %s ist eingespielt. Beim nächsten Start zeigt ein Menü einmal beide "
                 "Versionen, die neue startet von selbst. Jetzt neu starten?",
                 "Violyx %s is installed. At the next start a menu shows both versions once, the "
                 "new one starts by itself. Restart now?"),
              u->ver);
    u->st = S_NONE;
    strlcpy_(u->msg, TR("Eingespielt. Wirksam nach dem nächsten Neustart.",
                        "Installed. Takes effect after the next restart."),
             sizeof u->msg);
    if (dlg_confirm(T_, t))
        reboot();
}
static void open_file(up_t *u, const char *p) {
    vfs_ent e;
    u->st = S_ERR;
    if (fsx_stat(p, &e) || e.dir || e.size < 80 || e.size > VUPMAX) {
        strlcpy_(u->msg, TR("Die Datei lässt sich nicht lesen.", "The file cannot be read."),
                 sizeof u->msg);
        return;
    }
    kfree(u->buf);
    u->buf = kmalloc(e.size);
    if (!u->buf) {
        strlcpy_(u->msg, TR("Zu wenig Arbeitsspeicher.", "Not enough memory."), sizeof u->msg);
        return;
    }
    int64_t n = fsx_read(p, u->buf, e.size);
    u->n = n > 0 ? (u64)n : 0;
    const char *er = u->n == e.size ? vup_check(u) : TR("Lesefehler.", "Read error.");
    if (er) {
        strlcpy_(u->msg, er, sizeof u->msg);
        return;
    }
    u->file = 1;
    u->st = S_NEW;
    int c = vercmp(u->ver, VERSION);
    ksnprintf(u->msg, sizeof u->msg,
              c > 0   ? TR("Signatur geprüft.", "Signature checked.")
              : c < 0 ? TR("Achtung: %s ist älter als die laufende Version %s.",
                           "Note: %s is older than the running version %s.")
                      : TR("%s läuft bereits, es würde neu eingespielt.",
                           "%s is running already, it would be installed again."),
              u->ver, VERSION);
}
/* ---------- window ---------- */
static int u8n(const char *s, int n) { /* bytes of the first n characters (UTF-8) */
    int i = 0;
    while (s[i] && n > 0) {
        i++;
        while ((s[i] & 0xC0) == 0x80)
            i++;
        n--;
    }
    return i;
}
/* writes a text wrapped, returns the next free line */
static int wrap(win_t *w, int x, int y, int ymax, int wid, u8 a, const char *s, int ind, int skip,
                int *lines) {
    static char t[200];
    int first = 1;
    while (*s) {
        int room = wid - (first ? 0 : ind), b = u8n(s, room), cut = b;
        if (s[b]) { /* break at the last space */
            for (int i = b; i > 0; i--)
                if (s[i] == ' ') {
                    cut = i;
                    break;
                }
        }
        if (cut > (int)sizeof t - 1)
            cut = sizeof t - 1;
        memcpy(t, s, cut);
        t[cut] = 0;
        if (*lines >= skip && y < ymax)
            ctext(w, x + (first ? 0 : ind), y++, a, t);
        (*lines)++;
        s += cut;
        while (*s == ' ')
            s++;
        first = 0;
    }
    return y;
}
static void up_draw(win_t *w) {
    up_t *u = w->st;
    win_title(w, TR("Aktualisieren", "Update"));
    cfill(w, T);
    int W_ = cw(w), H_ = ch(w), y = 1;
    char t[160];
    ksnprintf(t, sizeof t, TR("Installiert:  Violyx %s", "Installed:  Violyx %s"), VERSION);
    ctext(w, 2, y++, HI, t);
    if (u->alt[0]) {
        ksnprintf(t, sizeof t, TR("Vorherige:    Violyx %s (Z stellt sie wieder her)",
                                  "Previous:   Violyx %s (Z brings it back)"),
                  u->alt);
        ctext(w, 2, y++, DIM, t);
    }
    if (u->canup)
        y = wrap(w, 2, y + 1, H_ - 1, W_ - 4, DIM,
                 TR("Hier lässt sich nichts aktualisieren: Violyx läuft von CD, oder die "
                    "Startpartition fehlt.",
                    "Nothing can be updated here: Violyx runs from CD, or the boot partition is "
                    "missing."),
                 0, 0, &(int){0});
    y++;
    if (u->st == S_NEW) {
        ksnprintf(t, sizeof t, u->file ? TR("Update-Datei:  Violyx %s", "Update file:  Violyx %s")
                                       : TR("Neu:  Violyx %s", "New:  Violyx %s"),
                  u->ver);
        ctext(w, 2, y++, AT(C_PANEL, C_GLOW), t);
        if (u->notes) {
            ctext(w, 2, y++, DIM, TR("Was ist neu:", "What's new:"));
            int lines = 0;
            const char *p = u->notes;
            static char l[1200];
            while (*p) {
                const char *q = p;
                while (*q && *q != '\n')
                    q++;
                int n = (int)(q - p) < (int)sizeof l - 1 ? (int)(q - p) : (int)sizeof l - 1;
                memcpy(l, p, n);
                l[n] = 0;
                y = wrap(w, 3, y, H_ - 2, W_ - 5, T, l, 2, u->top, &lines);
                p = *q ? q + 1 : q;
            }
            if (u->top > lines - 1)
                u->top = lines > 0 ? lines - 1 : 0;
        }
    }
    if (u->msg[0])
        wrap(w, 2, strlen(u->msg) > (size_t)(W_ - 4) ? H_ - 2 : H_ - 1, H_, W_ - 4,
             AT(C_PANEL, C_HOT), u->msg, 0, 0, &(int){0});
    ksnprintf(w->foot, 64, "%s",
              u->st == S_NEW && !u->canup ? TR(" Enter installiert  O Datei  Esc ",
                                               " Enter installs  O file  Esc ")
                                          : TR(" Enter sucht online  O Update-Datei ",
                                               " Enter checks online  O update file "));
}
static void up_key(win_t *w, key_t k) {
    up_t *u = w->st;
    const char *T_ = TR("Aktualisieren", "Update");
    if (k.key == K_OPEN) {
        if (sys_arg(u->path, VFS_PATH))
            open_file(u, u->path);
        return;
    }
    if (k.key == K_UP && u->top > 0)
        u->top--;
    else if (k.key == K_DOWN)
        u->top++;
    else if (k.key == K_PGUP)
        u->top = u->top > 8 ? u->top - 8 : 0;
    else if (k.key == K_PGDN)
        u->top += 8;
    else if (k.key == K_ENTER) {
        if (u->canup) {
            dlg_msg(T_, TR("Hier lässt sich nichts aktualisieren.", "Nothing can be updated here."));
            return;
        }
        if (u->st != S_NEW) {
            check_online(w, u);
            return;
        }
        int c = vercmp(u->ver, VERSION);
        char t[200];
        ksnprintf(t, sizeof t,
                  c > 0 ? TR("Violyx %s jetzt einspielen? Deine Dateien und Programme bleiben.",
                             "Install Violyx %s now? Your files and programs stay.")
                        : TR("Violyx %s statt %s einspielen? Deine Dateien und Programme bleiben.",
                             "Install Violyx %s instead of %s? Your files and programs stay."),
                  u->ver, VERSION);
        if (dlg_confirm(T_, t))
            install(w, u);
    } else if ((k.key | 32) == 'o' && !(k.mods & (M_CTRL | M_ALT))) {
        if (dlg_pick(TR("Update-Datei öffnen (.vup)", "Open update file (.vup)"), u->path,
                     VFS_PATH))
            open_file(u, u->path);
    } else if ((k.key | 32) == 'z' && !(k.mods & (M_CTRL | M_ALT)) && u->alt[0]) {
        char t[200];
        ksnprintf(t, sizeof t,
                  TR("Zurück zu Violyx %s? Die Version %s wird dafür die vorherige.",
                     "Back to Violyx %s? Version %s becomes the previous one."),
                  u->alt, VERSION);
        if (!dlg_confirm(T_, t))
            return;
        strlcpy_(u->msg, TR("Bitte warten, nicht ausschalten ...", "Please wait, do not switch off ..."),
                 sizeof u->msg);
        show(w);
        long r = upd_call(21, 0, 0);
        if (r) {
            ksnprintf(u->msg, sizeof u->msg, "%s (%ld)",
                      TR("Das ging nicht.", "That did not work."), r);
            return;
        }
        upd_call(22, (long)u->alt, 0);
        strlcpy_(u->msg, TR("Erledigt, wirksam nach dem nächsten Neustart.",
                            "Done, takes effect after the next restart."),
                 sizeof u->msg);
        if (dlg_confirm(T_, TR("Jetzt neu starten?", "Restart now?")))
            reboot();
    }
}
static void up_free(win_t *w) {
    up_t *u = w->st;
    kfree(u->notes);
    kfree(u->buf);
    kfree(u);
}
static const app_t up_app = {up_draw, up_key, 0, 0, up_free};
void prog_main(void) {
    up_t *u = kmalloc(sizeof *u);
    if (!u)
        return;
    memset(u, 0, sizeof *u);
    u->canup = (int)upd_call(22, (long)u->alt, 0);
    win_t *mw = win_open(TR("Aktualisieren", "Update"), 10, 3, 70, 22, &up_app, u);
    if (!mw) {
        kfree(u);
        return;
    }
    win_menu(mw, TR("Aktualisieren", "Update"), "*Info");
    if (sys_arg(u->path, VFS_PATH))
        open_file(u, u->path);
}
