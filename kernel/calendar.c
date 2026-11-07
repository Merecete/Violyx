/* Calendar: month view with one note per day, saved in /system/calendar. The data logic has no
 * screen code and can be tested on the host. The file is checked strictly. */
#ifdef HOST
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#else
#include "os.h"
#endif
#define CN_MAX 1000
#define CN_LEN 60
typedef struct {
    int date;
    char t[CN_LEN + 1];
} cnote; /* date = YYYYMMDD */
typedef struct {
    int n;
    cnote a[CN_MAX];
} cstore;
static int cal_leap(int y) {
    return (y % 4 == 0 && y % 100) || y % 400 == 0;
}
static int cal_days(int y, int m) {
    static const u8 d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return d[m - 1] + (m == 2 && cal_leap(y));
}
/* 0 = Monday ... 6 = Sunday */
static int cal_wday(int y, int m, int d) {
    static const u8 t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d + 6) % 7;
}
static int cal_find(const cstore *s, int date) {
    for (int i = 0; i < s->n; i++)
        if (s->a[i].date == date)
            return i;
    return -1;
}
static void cal_del(cstore *s, int date) {
    int i = cal_find(s, date);
    if (i < 0)
        return;
    memmove(&s->a[i], &s->a[i + 1], (size_t)(s->n - i - 1) * sizeof(cnote));
    s->n--;
}
static int cal_set(cstore *s, int date, const char *t) {
    int i = cal_find(s, date);
    if (i < 0) {
        if (s->n >= CN_MAX)
            return -1;
        i = s->n;
        while (i > 0 && s->a[i - 1].date > date) {
            memcpy(&s->a[i], &s->a[i - 1], sizeof(cnote));
            i--;
        }
        s->n++;
        s->a[i].date = date;
    }
    int k = 0;
    for (; k < CN_LEN && t[k]; k++)
        s->a[i].t[k] = t[k];
    s->a[i].t[k] = 0;
    return 0;
}
static int cal_ok(int y, int m, int d) {
    return y >= 1980 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= cal_days(y, m);
}
/* Lines: YYYY-MM-DD text. Anything invalid is skipped */
static void cal_parse(cstore *s, const char *b, long n) {
    s->n = 0;
    long p = 0;
    while (p < n) {
        long e = p;
        while (e < n && b[e] != '\n')
            e++;
        long l = e - p;
        const char *q = b + p;
        if (l > 11 && l <= 11 + CN_LEN && q[4] == '-' && q[7] == '-' && q[10] == ' ') {
            int dg = 1;
            for (int i = 0; i < 10; i++)
                if (i != 4 && i != 7 && (q[i] < '0' || q[i] > '9'))
                    dg = 0;
            int y = (q[0] - '0') * 1000 + (q[1] - '0') * 100 + (q[2] - '0') * 10 + (q[3] - '0'),
                m = (q[5] - '0') * 10 + (q[6] - '0'), d = (q[8] - '0') * 10 + (q[9] - '0');
            if (dg && cal_ok(y, m, d)) {
                char t[CN_LEN + 1];
                int k = 0, good = 1;
                for (long i = 11; i < l; i++) {
                    if (q[i] == 0 || q[i] == '\r') {
                        good = 0;
                        break;
                    }
                    t[k++] = q[i];
                }
                t[k] = 0;
                if (good && k)
                    cal_set(s, y * 10000 + m * 100 + d, t);
            }
        }
        p = e + 1;
    }
}
static long cal_dump(const cstore *s, char *b, long max) {
    long o = 0;
    for (int i = 0; i < s->n; i++) {
        const cnote *c = &s->a[i];
        int y = c->date / 10000, m = c->date / 100 % 100, d = c->date % 100;
        long l = (long)strlen(c->t);
        if (o + l + 12 > max)
            break;
        b[o++] = '0' + y / 1000 % 10;
        b[o++] = '0' + y / 100 % 10;
        b[o++] = '0' + y / 10 % 10;
        b[o++] = '0' + y % 10;
        b[o++] = '-';
        b[o++] = '0' + m / 10;
        b[o++] = '0' + m % 10;
        b[o++] = '-';
        b[o++] = '0' + d / 10;
        b[o++] = '0' + d % 10;
        b[o++] = ' ';
        memcpy(b + o, c->t, (size_t)l);
        o += l;
        b[o++] = '\n';
    }
    return o;
}
#ifndef HOST
#define T_    AT(C_PANEL, C_ORCH)
#define CFILE "/system/calendar"
#define CBUF  ((CN_LEN + 12) * CN_MAX)
typedef struct {
    cstore s;
    int y, m, d, warned;
    char *io;
} cal_t;
static win_t *cal_w;
static int cdate(cal_t *c) {
    return c->y * 10000 + c->m * 100 + c->d;
}
static const char *mname(int m) {
    static const char *de[12] = {"Januar",    "Februar", "März",     "April",
                                 "Mai",       "Juni",    "Juli",     "August",
                                 "September", "Oktober", "November", "Dezember"},
                      *en[12] = {"January",   "February", "March",    "April",
                                 "May",       "June",     "July",     "August",
                                 "September", "October",  "November", "December"};
    return cfg_lang ? en[m - 1] : de[m - 1];
}
static void ctoday(cal_t *c) {
    rtc_t r;
    rtc_read(&r);
    c->y = r.year;
    c->m = r.mon;
    c->d = r.day;
    if (!cal_ok(c->y, c->m, c->d)) {
        c->y = 2026;
        c->m = 1;
        c->d = 1;
    }
}
static void cmove(cal_t *c, int dd) {
    c->d += dd;
    while (c->d < 1) {
        if (c->m == 1) {
            if (c->y <= 1980) {
                c->d = 1;
                return;
            }
            c->m = 12;
            c->y--;
        } else
            c->m--;
        c->d += cal_days(c->y, c->m);
    }
    while (c->d > cal_days(c->y, c->m)) {
        if (c->m == 12 && c->y >= 2099) {
            c->d = 31;
            return;
        }
        c->d -= cal_days(c->y, c->m);
        if (c->m == 12) {
            c->m = 1;
            c->y++;
        } else
            c->m++;
    }
}
static void cmonth(cal_t *c, int dm) {
    int t = c->y * 12 + c->m - 1 + dm;
    int y = t / 12, m = t % 12 + 1;
    if (y < 1980 || y > 2099)
        return;
    c->y = y;
    c->m = m;
    int l = cal_days(y, m);
    if (c->d > l)
        c->d = l;
}
static void csave(cal_t *c) {
    long n = cal_dump(&c->s, c->io, CBUF);
    int r = fsx_write(CFILE, c->io, (u64)n, fs_now());
    if (r && !c->warned) {
        c->warned = 1;
        dlg_msg(TR("Kalender", "Calendar"),
                TR("Notizen bleiben nur bis zum Neustart.", "Notes are kept only until restart."));
    }
}
static void rawtext(win_t *w, int x, int y, u8 a, const char *s, int max) {
    for (int i = 0; s[i] && i < max; i++)
        cput(w, x + i, y, (u8)s[i], a);
}
static void cal_draw(win_t *w) {
    cal_t *c = w->st;
    win_title(w, TR("Kalender", "Calendar"));
    cfill(w, T_);
    char s[64];
    ksnprintf(s, 64, "%s %d", mname(c->m), c->y);
    ctext(w, (35 - (int)strlen(s)) / 2 + 1, 0, AT(C_PANEL, C_GLOW), s);
    static const char *wd[2] = {"Mo   Di   Mi   Do   Fr   Sa   So",
                                "Mo   Tu   We   Th   Fr   Sa   Su"};
    ctext(w, 2, 1, AT(C_PANEL, C_MID), wd[cfg_lang]);
    rtc_t r;
    rtc_read(&r);
    int first = cal_wday(c->y, c->m, 1), len = cal_days(c->y, c->m);
    for (int d = 1; d <= len; d++) {
        int p = first + d - 1, x = 1 + (p % 7) * 5, y = 2 + (p / 7) * 2;
        int date = c->y * 10000 + c->m * 100 + d, sel = d == c->d,
            today = r.year == c->y && r.mon == c->m && r.day == d, has = cal_find(&c->s, date) >= 0;
        u8 a = sel           ? AT(C_SEL, C_GLOW)
               : today       ? AT(C_PANEL, C_HOT)
               : (p % 7 > 4) ? AT(C_PANEL, C_MID)
                             : T_;
        char n[6];
        ksnprintf(n, 6, "%2d", d);
        if (sel)
            for (int i = 0; i < 4; i++)
                cput(w, x + i, y, ' ', a);
        ctext(w, x + 1, y, a, n);
        if (has)
            cput(w, x + 3, y, 0x04, sel ? a : AT(C_PANEL, C_NEON));
    }
    ctext(w, 38, 0, AT(C_PANEL, C_GLOW), TR("Nächste Notizen", "Upcoming notes"));
    int today = r.year * 10000 + r.mon * 100 + r.day, row = 1;
    for (int i = 0; i < c->s.n && row < 13; i++) {
        cnote *q = &c->s.a[i];
        if (q->date < today)
            continue;
        char b[8];
        ksnprintf(b, 8, "%02d.%02d.", q->date % 100, q->date / 100 % 100);
        if (cfg_lang)
            ksnprintf(b, 8, "%02d-%02d", q->date / 100 % 100, q->date % 100);
        ctext(w, 38, row, AT(C_PANEL, C_MID), b);
        rawtext(w, 45, row, T_, q->t, cw(w) - 46);
        row++;
    }
    if (row == 1)
        ctext(w, 38, 1, AT(C_PANEL, C_MID), TR("Keine", "None"));
    ksnprintf(s, 64, cfg_lang ? "%d-%02d-%02d" : "%02d.%02d.%d", cfg_lang ? c->y : c->d, c->m,
              cfg_lang ? c->d : c->y);
    int yy = ch(w) - 2;
    ctext(w, 1, yy, AT(C_PANEL, C_NEON), s);
    int i = cal_find(&c->s, cdate(c));
    if (i >= 0)
        rawtext(w, 12, yy, T_, c->s.a[i].t, cw(w) - 13);
    else
        ctext(w, 12, yy, AT(C_PANEL, C_MID), TR("Keine Notiz", "No note"));
    ctext(w, 1, ch(w) - 1, AT(C_PANEL, C_MID),
          TR("Pfeile Tag  Bild Monat  H Heute  Enter Notiz  Entf Löschen",
             "Arrows Day  PgUp/PgDn Month  T Today  Enter Note  Del Delete"));
}
static void cal_key(win_t *w, key_t k) {
    cal_t *c = w->st;
    if (k.key == K_LEFT)
        cmove(c, -1);
    else if (k.key == K_RIGHT)
        cmove(c, 1);
    else if (k.key == K_UP)
        cmove(c, -7);
    else if (k.key == K_DOWN)
        cmove(c, 7);
    else if (k.key == K_PGUP)
        cmonth(c, -1);
    else if (k.key == K_PGDN)
        cmonth(c, 1);
    else if (k.key == 't' || k.key == 'T' || k.key == 'h' || k.key == 'H')
        ctoday(c);
    else if (k.key == K_ENTER) {
        char b[CN_LEN + 1];
        b[0] = 0;
        int i = cal_find(&c->s, cdate(c));
        if (i >= 0)
            strlcpy_(b, c->s.a[i].t, CN_LEN + 1);
        if (dlg_input(TR("Notiz", "Note"), TR("Text für diesen Tag:", "Text for this day:"), b,
                      CN_LEN + 1)) {
            if (cal_set(&c->s, cdate(c), b))
                dlg_msg(TR("Kalender", "Calendar"), TR("Kalender ist voll.", "Calendar is full."));
            else
                csave(c);
        }
    } else if (k.key == K_DEL) {
        if (cal_find(&c->s, cdate(c)) >= 0 &&
            dlg_confirm(TR("Notiz löschen", "Delete note"),
                        TR("Notiz für diesen Tag löschen?", "Delete the note for this day?"))) {
            cal_del(&c->s, cdate(c));
            csave(c);
        }
    }
}
static void cal_free(win_t *w) {
    cal_t *c = w->st;
    kfree(c->io);
    kfree(c);
    cal_w = 0;
}
static const app_t cal_app = {cal_draw, cal_key, 0, 0, cal_free, 0};
void app_calendar(void) {
    if (cal_w) {
        win_raise(cal_w);
        return;
    }
    cal_t *c = kmalloc(sizeof *c);
    char *io = kmalloc(CBUF);
    if (!c || !io) {
        kfree(c);
        kfree(io);
        return;
    }
    memset(c, 0, sizeof *c);
    c->io = io;
    ctoday(c);
    if (fs_ok()) {
        int64_t n = fsx_read(CFILE, io, CBUF);
        if (n > 0)
            cal_parse(&c->s, io, (long)n);
    }
    cal_w = win_open(TR("Kalender", "Calendar"), 7, 2, 66, 19, &cal_app, c);
    if (!cal_w) {
        kfree(io);
        kfree(c);
    }
}
#endif
