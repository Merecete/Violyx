/* Calculator: a program in ring 3 */
#include "common.h"
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
void prog_main(void) {
    app_calc();
}
