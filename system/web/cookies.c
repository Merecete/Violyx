#include "web.h"
/* Cookies: session cookies stay in memory, persistent ones go to /system/browser/cookies. Programs
 * cannot reach /system. Images neither send nor store cookies. */
#define MAXCK  200
#define PERDOM 30
#define CKFILE "/system/browser/cookies"
typedef struct {
    char name[48], val[320], dom[80], path[64];
    u32 exp, seq;
    u8 used, host, sec, pad;
} ck_t;
static ck_t t[MAXCK];
static u32 seq;
static int loaded, dirty;
static int lc(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static int ieqn(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++)
        if (lc((u8)a[i]) != lc((u8)b[i]))
            return 0;
    return 1;
}
static int term(const char *s, int n) {
    for (int i = 0; i < n; i++)
        if (!s[i])
            return 1;
    return 0;
}
static void load_(void) {
    if (loaded)
        return;
    loaded = 1;
    if (!fs_ok())
        return;
    static u32 hd[2];
    vfs_ent e;
    if (fsx_stat(CKFILE, &e) || e.size != 8 + sizeof t)
        return;
    u8 *b = kmalloc(e.size);
    if (!b)
        return;
    if (fsx_read(CKFILE, b, e.size) == (int64_t)e.size) {
        memcpy(hd, b, 8);
        if (hd[0] == 0x314B4356) {
            memcpy(t, b + 8, sizeof t);
            seq = hd[1];
            u32 now = fs_now();
            for (int i = 0; i < MAXCK; i++) {
                ck_t *c = &t[i];
                if (c->used && (!term(c->name, 48) || !term(c->val, 320) || !term(c->dom, 80) ||
                                !term(c->path, 64) || !c->exp || c->exp <= now))
                    c->used = 0;
            }
        }
    }
    kfree(b);
}
void cookie_save(void) {
    if (!dirty || !fs_ok())
        return;
    dirty = 0;
    u32 sz = 8 + sizeof t;
    u8 *b = kmalloc(sz);
    if (!b)
        return;
    u32 hd[2] = {0x314B4356, seq};
    memcpy(b, hd, 8);
    memset(b + 8, 0, sizeof t);
    int o = 0;
    for (int i = 0; i < MAXCK; i++)
        if (t[i].used && t[i].exp)
            memcpy(b + 8 + (o++) * sizeof(ck_t), &t[i], sizeof(ck_t));
    fsx_mkdir("/system/browser", fs_now());
    fsx_write(CKFILE, b, sz, fs_now());
    kfree(b);
}
void cookie_clear(void) {
    load_();
    memset(t, 0, sizeof t);
    dirty = 1;
    cookie_save();
}
int cookie_count(void) {
    load_();
    int n = 0;
    for (int i = 0; i < MAXCK; i++)
        n += t[i].used;
    return n;
}
static int days(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1,
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
/* "Wed, 21 Oct 2026 07:28:00 GMT", also with dashes */
static long pdate(const char *s, int n) {
    static const char mo[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    int d = -1, m = -1, y = -1, hh = 0, mi = 0, se = 0;
    for (int i = 0; i < n;) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            int v = 0, k = 0;
            while (i + k < n && s[i + k] >= '0' && s[i + k] <= '9' && k < 5) {
                v = v * 10 + s[i + k] - '0';
                k++;
            }
            if (i + k < n && s[i + k] == ':' && i + k + 5 < n) {
                hh = v;
                mi = (s[i + k + 1] - '0') * 10 + s[i + k + 2] - '0';
                se = (s[i + k + 4] - '0') * 10 + s[i + k + 5] - '0';
                i += k + 6;
                continue;
            }
            if (k >= 3)
                y = v;
            else if (d < 0)
                d = v;
            else
                y = v < 70 ? 2000 + v : 1900 + v;
            i += k;
        } else if (lc(c) >= 'a' && lc(c) <= 'z' && m < 0 && i + 3 <= n) {
            for (int q = 0; q < 12; q++)
                if (ieqn(s + i, mo + q * 3, 3))
                    m = q + 1;
            i += (m < 0) ? 1 : 3;
        } else
            i++;
    }
    if (d < 1 || d > 31 || m < 1 || y < 1970 || y > 2200)
        return -1;
    long v = (long)days(y, m, d) * 86400 + hh * 3600 + mi * 60 + se;
    return v > 0xFFFFFFF0L ? 0xFFFFFFF0L : v;
}
static int smatch(const char *host, const char *dom) {
    int h = strlen(host), d = strlen(dom);
    if (d > h || !ieqn(host + h - d, dom, d))
        return 0;
    return h == d || host[h - d - 1] == '.';
}
/* No cookies for whole suffixes such as .com or .co.uk */
static int suffix(const char *d) {
    int dots = 0;
    const char *l = 0, *s = 0;
    for (const char *p = d; *p; p++)
        if (*p == '.') {
            dots++;
            s = l;
            l = p;
        }
    if (!dots)
        return 1;
    if (dots == 1 && strlen(l + 1) == 2) {
        static const char *const sl[] = {"co",  "com", "org", "net", "ac",
                                         "gov", "edu", "ne",  "or",  0};
        for (int i = 0; sl[i]; i++)
            if ((int)strlen(sl[i]) == (int)(l - d) && ieqn(d, sl[i], l - d))
                return 1;
    }
    (void)s;
    return 0;
}
void cookie_set(const char *host, const char *path, const char *line, int len, int tls) {
    load_();
    const char *e = line + len, *semi = line;
    while (semi < e && *semi != ';')
        semi++;
    const char *eq = line;
    while (eq < semi && *eq != '=')
        eq++;
    if (eq >= semi || eq == line)
        return;
    int nl = (int)(eq - line);
    const char *v = eq + 1;
    int vl = (int)(semi - v);
    while (nl && line[nl - 1] == ' ')
        nl--;
    while (vl && *v == ' ') {
        v++;
        vl--;
    }
    while (vl && v[vl - 1] == ' ')
        vl--;
    if (nl < 1 || nl >= 48 || vl >= 320)
        return;
    for (int i = 0; i < nl; i++)
        if ((u8)line[i] < 33 || (u8)line[i] > 126)
            return;
    for (int i = 0; i < vl; i++)
        if ((u8)v[i] < 32 || (u8)v[i] > 126)
            return;
    static ck_t n;
    memset(&n, 0, sizeof n);
    memcpy(n.name, line, nl);
    memcpy(n.val, v, vl);
    strlcpy_(n.dom, host, 80);
    n.host = 1;
    {
        int pl = 0;
        const char *q = path;
        const char *last = path;
        for (; *q && *q != '?'; q++)
            if (*q == '/')
                last = q;
        pl = (int)(last - path);
        if (pl < 1 || pl >= 64) {
            n.path[0] = '/';
            n.path[1] = 0;
        } else {
            memcpy(n.path, path, pl);
            n.path[pl] = 0;
        }
    }
    long ma = -1, ex = -1;
    int hasma = 0;
    for (const char *p = semi; p < e;) {
        p++;
        while (p < e && *p == ' ')
            p++;
        const char *s = p;
        while (s < e && *s != ';')
            s++;
        const char *q = p;
        while (q < s && *q != '=')
            q++;
        int kl = (int)(q - p);
        const char *av = q < s ? q + 1 : s;
        int al = (int)(s - av);
        while (kl && p[kl - 1] == ' ')
            kl--;
        if (kl == 6 && ieqn(p, "domain", 6)) {
            if (al && *av == '.') {
                av++;
                al--;
            }
            if (al >= 80 || al < 1)
                return;
            char d[80];
            memcpy(d, av, al);
            d[al] = 0;
            for (int i = 0; i < al; i++)
                d[i] = (char)lc(d[i]);
            if (!smatch(host, d) || suffix(d))
                return;
            strlcpy_(n.dom, d, 80);
            n.host = 0;
        } else if (kl == 4 && ieqn(p, "path", 4)) {
            if (al >= 1 && al < 64 && *av == '/') {
                memcpy(n.path, av, al);
                n.path[al] = 0;
            }
        } else if (kl == 6 && ieqn(p, "secure", 6))
            n.sec = 1;
        else if (kl == 7 && ieqn(p, "max-age", 7)) {
            long m = 0;
            int neg = al && *av == '-';
            for (int i = neg; i < al && av[i] >= '0' && av[i] <= '9' && m < 400000000L; i++)
                m = m * 10 + av[i] - '0';
            ma = neg ? 0 : m;
            hasma = 1;
        } else if (kl == 7 && ieqn(p, "expires", 7))
            ex = pdate(av, al);
        p = s;
    }
    if (n.sec && !tls)
        return;
    u32 now = fs_now();
    int kill = 0;
    if (hasma) {
        if (ma <= 0)
            kill = 1;
        else
            n.exp = now + (u32)ma;
    } else if (ex >= 0) {
        if ((u32)ex <= now)
            kill = 1;
        else
            n.exp = (u32)ex;
    }
    int cnt = 0, slot = -1, fr = -1;
    u32 old = ~0u;
    int oldi = 0, domold = -1;
    u32 dq = ~0u;
    for (int i = 0; i < MAXCK; i++) {
        ck_t *c = &t[i];
        if (!c->used) {
            if (fr < 0)
                fr = i;
            continue;
        }
        if (c->seq < old) {
            old = c->seq;
            oldi = i;
        }
        if (!strcmp(c->dom, n.dom)) {
            cnt++;
            if (c->seq < dq) {
                dq = c->seq;
                domold = i;
            }
            if (!strcmp(c->name, n.name) && !strcmp(c->path, n.path))
                slot = i;
        }
    }
    if (kill) {
        if (slot >= 0) {
            if (t[slot].exp)
                dirty = 1;
            t[slot].used = 0;
        }
        return;
    }
    if (slot < 0)
        slot = cnt >= PERDOM ? domold : fr >= 0 ? fr : oldi;
    if (t[slot].used && t[slot].exp)
        dirty = 1;
    n.used = 1;
    n.seq = ++seq;
    t[slot] = n;
    if (n.exp)
        dirty = 1;
}
int cookie_get(const char *host, const char *path, int tls, char *out, int max) {
    load_();
    u32 now = fs_now();
    int o = 0;
    for (int i = 0; i < MAXCK; i++) {
        ck_t *c = &t[i];
        if (!c->used)
            continue;
        if (c->exp && c->exp <= now) {
            c->used = 0;
            dirty = 1;
            continue;
        }
        if (c->sec && !tls)
            continue;
        if (c->host ? !ieqn(host, c->dom, strlen(c->dom) + 1) : !smatch(host, c->dom))
            continue;
        int pl = strlen(c->path);
        if (pl > 1 &&
            (strncmp(path, c->path, pl) || (path[pl] && path[pl] != '/' && path[pl] != '?')))
            continue;
        int n = strlen(c->name) + strlen(c->val) + 3;
        if (o + n + 1 >= max)
            break;
        o += ksnprintf(out + o, max - o, "%s%s=%s", o ? "; " : "", c->name, c->val);
    }
    return o;
}
