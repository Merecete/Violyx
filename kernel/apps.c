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
void drive_format(const char *name) {
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
    char pn[40]; /* a picture from the image viewer */
    if (wall_photo_name())
        ksnprintf(pn, 30, "%s %s", TR("Bild", "Picture"), wall_photo_name());
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
                      i == 5   ? (wall_photo_name() ? pn : wall_name(cfg_wall))
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
            if (s->sel == 5) {
                if (wall_photo_name()) /* back to the background of the settings */
                    wall_photo_clear();
                else
                    wall_set((cfg_wall + WALLS + d) % WALLS);
            }
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
/* The overview draws itself from these, also below a dialog opened from it */
static int lp_s, lp_top, lp_ni;
static void lp_draw(void) {
    int s = lp_s, top = lp_top, all = NP + lp_ni;
    int x, y, rows = (all + PC - 1) / PC;
    dlg_frame(TR("Programme", "Programs"), 66, 17, &x, &y);
    for (int i = top * PC; i < all && i < (top + PV) * PC; i++) {
        int tx = x + 3 + (i % PC) * 15, ty = y + 2 + (i / PC - top) * 7, sel = i == s;
        u8 bg = sel ? C_SEL : C_RAISE;
        fill(tx, ty, 15, 6, ' ', AT(bg, C_LAV));
        int inst = i >= NP;
        if (!inst && progs[i].fn == app_xzn) {
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 6; c++)
                    gput(tx + 4 + c, ty + 1 + r, 251, xzn_icon, 24, c, r); /* 250 is the background */
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
}
static int launcher_loop(void);
void app_launcher(void) {
    lp_ni = prog_scan(ipr, MAXIP);
    ui_under(lp_draw);
    int s = launcher_loop();
    ui_under(0); /* what starts now draws over the desktop, not over the overview */
    if (s < 0)
        return;
    if (s < NP)
        progs[s].fn();
    else {
        int r = prog_run(ipr[s - NP].name);
        if (r)
            dlg_msg(ipr[s - NP].title, vfs_errstr(r, cfg_lang));
    }
}
/* Returns the chosen entry or -1 */
static int launcher_loop(void) {
    for (;;) {
        int s = lp_s, all = NP + lp_ni, rows = (all + PC - 1) / PC;
        if (s >= all)
            s = all - 1;
        if (s / PC < lp_top)
            lp_top = s / PC;
        if (s / PC >= lp_top + PV)
            lp_top = s / PC - PV + 1; /* the view follows the selection */
        lp_s = s;
        ui_render();
        ui_show();
        key_t k;
        if (!ui_wait(&k))
            continue;
        if (k.key == K_ESC || k.key == K_F2)
            return -1;
        if (k.key == K_ENTER)
            return s;
        if (k.key == K_DEL && s >= NP) {
            char q[72];
            ksnprintf(q, 72, TR("%s und seine Daten löschen?", "Delete %s and its data?"),
                      ipr[s - NP].title);
            if (dlg_confirm(TR("Deinstallieren", "Uninstall"), q)) {
                int r = prog_uninstall(ipr[s - NP].name);
                if (r)
                    dlg_msg(TR("Deinstallieren", "Uninstall"), vfs_errstr(r, cfg_lang));
                lp_ni = prog_scan(ipr, MAXIP);
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
        lp_s = s;
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
    char f[48];
    fb_list(f, 48);
    ksnprintf(b, 60, TR("Bildschirme: %s", "Screens:  %s"), f);
    ctext(w, 1, 9, T, b);
}
static const app_t in_app = {in_draw, 0, 0, 0, 0};
void app_info(void) {
    win_open(TR("Systeminfo", "System info"), 18, 6, 50, 12, &in_app, 0);
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
