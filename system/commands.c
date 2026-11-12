/* Commands: the command line of Violyx, a program in ring 3 */
#include "common.h"
#include "web/web.h"
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
/* A report of the system, line by line */
static void sh_report(sh_t *s, int kind) {
    char *b = kmalloc(65536);
    if (!b)
        return;
    int n = sys_report(kind, b, 65536);
    if (n < 0)
        sh_err(s, n);
    else {
        char *p = b;
        while (*p) {
            char *e = strchr(p, '\n');
            if (e)
                *e = 0;
            sh_put(s, p);
            if (!e)
                break;
            p = e + 1;
        }
    }
    kfree(b);
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
    {"cc", "DATEI.c [-o X.vpk] [-s 60x18] [-t Titel]", "FILE.c [-o X.vpk] [-s 60x18] [-t Title]",
     "Übersetzt ein C-Programm zu einem Paket.", "Compiles a C program into a package."},
    {"df", "", "", "Zeigt den freien Platz der Systemplatte.",
     "Shows the free space of the system disk."},
    {"eject", "usb1 / cd1", "usb1 / cd1", "Meldet einen USB-Stick sicher ab oder öffnet das CD-Laufwerk.",
     "Signs a USB stick off safely or opens the CD drive."},
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
                int n = http_fetch(a, 0, w, HOLE_MAX, &r, sh_prog, HTTP_NOCOOKIE);
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
        reboot();
    } else if (!strcmp(c, "mem")) {
        sh_report(s, REP_MEM);
    } else if ((!strcmp(c, "zeit") || !strcmp(c, "time"))) {
        rtc_t r;
        rtc_read(&r);
        if (cfg_lang)
            ksnprintf(b, 80, "%d-%02d-%02d %02d:%02d:%02d", r.year, r.mon, r.day, r.h, r.m, r.s);
        else
            ksnprintf(b, 80, "%02d.%02d.%d %02d:%02d:%02d", r.day, r.mon, r.year, r.h, r.m, r.s);
        sh_put(s, b);
    } else if (!strcmp(c, "usb"))
        sh_report(s, REP_USB);
    else if (!strcmp(c, "pci") || !strcmp(c, "geraete") || !strcmp(c, "devices")) {
        sh_report(s, !strcmp(a, "alle") || !strcmp(a, "all") ? REP_DEVICES_ALL : REP_DEVICES);
    } else if (!strcmp(c, "sound")) {
        sh_report(s, REP_SOUND);
        snd_tone(523, 140, 70, 1, 0);
        snd_tone(659, 140, 70, 1, 150);
        snd_tone(784, 220, 70, 1, 300);
    } else if (!strcmp(c, "netdiag"))
        sh_report(s, REP_NETDIAG);
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
    else if (!strcmp(c, "df"))
        sh_report(s, REP_DF);
    else if (!strcmp(c, "cd")) {
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
    } else if (!strcmp(c, "cc")) {
        /* the files become whole paths from here, the rest goes to the compiler as it is */
        static char cl[VFS_PATH];
        int n = 0, err = 0, path_next = 1, first = 1;
        cl[0] = 0;
        const char *w = a;
        while (*w && !err) {
            while (*w == ' ')
                w++;
            if (!*w)
                break;
            char wd[VFS_PATH];
            int k = 0, q = *w == '"';
            if (q)
                w++;
            while (*w && (q ? *w != '"' : *w != ' ') && k < VFS_PATH - 1)
                wd[k++] = *w++;
            if (q && *w == '"')
                w++;
            wd[k] = 0;
            const char *out = wd;
            if (wd[0] != '-' && (path_next || first)) {
                if (sh_path(s, wd, p)) {
                    err = 1;
                    break;
                }
                out = p;
                first = 0;
            }
            path_next = !strcmp(wd, "-o") || !strcmp(wd, "-I");
            int has_sp = strchr(out, ' ') != 0;
            int m = ksnprintf(cl + n, sizeof cl - (size_t)n, has_sp ? "%s\"%s\"" : "%s%s",
                              n ? " " : "", out);
            if (m < 0 || n + m >= (int)sizeof cl - 1) {
                sh_err(s, VE_INVAL);
                err = 1;
                break;
            }
            n += m;
        }
        if (!err) {
            if (!*a)
                sh_put(s, TR("cc DATEI.c  (siehe help cc)", "cc FILE.c  (see help cc)"));
            else {
                int r = prog_open("cc", cl);
                if (r < 0)
                    sh_err(s, r);
            }
        }
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
void prog_main(void) {
    app_shell();
}
