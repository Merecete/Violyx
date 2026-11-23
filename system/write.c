/* Write: a text editor, also for source code; a program in ring 3
 *
 * For code: line numbers, colours for C and for languages with # comments (Python, shell,
 * Makefile), tabs shown as tab stops, indentation kept on Enter, Tab and Shift+Tab move the
 * selected lines. Selection with Shift, Ctrl+C/X/V, undo with Ctrl+Z, find and replace, go to
 * a line. Files with Windows line ends (CR LF) are saved the way they came. */
#include "common.h"

#define ED_CAP (1024 * 1024)
#define TABW   4
#define NUNDO  400
enum { L_TEXT, L_C, L_HASH, L_MD };
typedef struct {
    int pos, nd, ni; /* at pos: nd characters went, ni came */
    char *d, *i;
    int cur, grp;    /* the cursor before, and whether typing may extend it */
} op_t;
typedef struct {
    char *b;
    int len, cur, anc, sel; /* cursor, other end of the selection */
    int top, col, dirty, crlf, lang, nums, want, help;
    char file[VFS_PATH];
    char find[80], msg[60];
    op_t u[NUNDO];
    int nu, nr; /* undo steps, of which the last nr are undone (redo) */
} ed_t;
static char *clip;
static int nclip;

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
static int nlines(ed_t *e) {
    return lineno(e, e->len) + 1;
}
static int linepos(ed_t *e, int n) {
    int p = 0;
    while (n > 0 && p < e->len)
        if (e->b[p++] == '\n')
            n--;
    return p;
}
/* the column on the screen, with tabs to the next tab stop */
static int vcol(ed_t *e, int p) {
    int c = 0;
    for (int i = ls(e, p); i < p; i++)
        c = e->b[i] == '\t' ? (c / TABW + 1) * TABW : c + 1;
    return c;
}
static int at_vcol(ed_t *e, int s, int want) {
    int c = 0, p = s;
    while (p < e->len && e->b[p] != '\n') {
        int n = e->b[p] == '\t' ? (c / TABW + 1) * TABW : c + 1;
        if (n > want)
            break;
        c = n;
        p++;
    }
    return p;
}
static int sel_a(ed_t *e) {
    return e->anc < e->cur ? e->anc : e->cur;
}
static int sel_b(ed_t *e) {
    return e->anc < e->cur ? e->cur : e->anc;
}
static int has_sel(ed_t *e) {
    return e->sel && e->anc != e->cur;
}
static int ends(const char *s, const char *x) {
    int a = strlen(s), b = strlen(x);
    if (a < b)
        return 0;
    for (int i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != x[i])
            return 0;
    }
    return 1;
}
static int lang_of(const char *f) {
    static const char *const c[] = {".c", ".h", ".cpp", ".hpp", ".cc", ".java", ".js", ".rs", ".go", ".cs", ".css", ".ld", 0};
    static const char *const h[] = {".py", ".sh", ".mk", ".toml", ".yaml", ".yml", ".conf", ".cfg", ".ini", "makefile", ".txt.sh", 0};
    for (int i = 0; c[i]; i++)
        if (ends(f, c[i]))
            return L_C;
    for (int i = 0; h[i]; i++)
        if (ends(f, h[i]))
            return L_HASH;
    if (ends(f, ".md"))
        return L_MD;
    return L_TEXT;
}

/* ---------- changes, all through one place, so that they can be undone ---------- */
static char *dup_(const char *s, int n) {
    char *d = kmalloc(n + 1);
    if (d && n)
        memcpy(d, s, n);
    return d;
}
static void op_free(op_t *o) {
    kfree(o->d);
    kfree(o->i);
    o->d = o->i = 0;
}
/* replaces n characters at p with the ni characters of s; returns 0 when they do not fit */
static int edit(ed_t *e, int p, int n, const char *s, int ni, int grp) {
    if (e->len - n + ni > ED_CAP) {
        strlcpy_(e->msg, TR("Datei voll (max. 1 MB)", "File full (1 MB max)"), 60);
        return 0;
    }
    while (e->nr) /* a new change: what was undone is gone */
        op_free(&e->u[e->nu - e->nr]), e->nr--, e->nu--;
    op_t *last = e->nu ? &e->u[e->nu - 1] : 0;
    if (grp && last && last->grp && !n && !last->nd && last->pos + last->ni == p && last->ni < 200) {
        char *m = kmalloc(last->ni + ni + 1); /* typing goes on: one step */
        if (m) {
            memcpy(m, last->i, last->ni);
            memcpy(m + last->ni, s, ni);
            kfree(last->i);
            last->i = m;
            last->ni += ni;
            goto apply;
        }
    }
    if (e->nu == NUNDO) {
        op_free(&e->u[0]);
        memmove(e->u, e->u + 1, sizeof(op_t) * (NUNDO - 1));
        e->nu--;
    }
    op_t *o = &e->u[e->nu++];
    o->pos = p;
    o->nd = n;
    o->ni = ni;
    o->d = dup_(e->b + p, n);
    o->i = dup_(s, ni);
    o->cur = e->cur;
    o->grp = grp;
apply:
    memmove(e->b + p + ni, e->b + p + n, e->len - p - n);
    memcpy(e->b + p, s, ni);
    e->len += ni - n;
    e->dirty = 1;
    return 1;
}
static void del_sel(ed_t *e) {
    if (!has_sel(e))
        return;
    int a = sel_a(e), b = sel_b(e);
    if (edit(e, a, b - a, "", 0, 0))
        e->cur = a;
    e->sel = 0;
}
static void put_text(ed_t *e, const char *s, int n, int grp) {
    if (has_sel(e)) {
        del_sel(e);
        grp = 0;
    }
    e->sel = 0;
    if (edit(e, e->cur, 0, s, n, grp))
        e->cur += n;
}
static void undo(ed_t *e, int redo) {
    if (redo ? !e->nr : e->nu - e->nr <= 0) {
        strlcpy_(e->msg, redo ? TR("Nichts wiederherzustellen", "Nothing to redo") : TR("Nichts rückgängig zu machen", "Nothing to undo"), 60);
        return;
    }
    op_t *o = &e->u[e->nu - e->nr - (redo ? 0 : 1)];
    int dn = redo ? o->nd : o->ni, in = redo ? o->ni : o->nd;
    const char *s = redo ? o->i : o->d;
    if (e->len - dn + in > ED_CAP)
        return;
    memmove(e->b + o->pos + in, e->b + o->pos + dn, e->len - o->pos - dn);
    memcpy(e->b + o->pos, s, in);
    e->len += in - dn;
    e->cur = redo ? o->pos + o->ni : o->cur;
    if (e->cur > e->len)
        e->cur = e->len;
    e->nr += redo ? -1 : 1;
    e->sel = 0;
    e->dirty = 1;
    o->grp = 0;
}

/* ---------- colours ---------- */
#define A_TEXT AT(C_PANEL, C_ORCH)
#define A_KEY  AT(C_PANEL, C_NEON)
#define A_TYPE AT(C_PANEL, C_LAV)
#define A_COM  AT(C_PANEL, C_MID)
#define A_STR  AT(C_PANEL, C_HOT)
#define A_NUM  AT(C_PANEL, C_GLOW)
#define A_PRE  AT(C_PANEL, C_VIOLET)
#define A_GUT  AT(C_PANEL, C_DIM)
#define A_SEL  AT(C_SEL, C_GLOW)
static int word_in(const char *w, int n, const char *const *l) {
    for (int i = 0; l[i]; i++)
        if ((int)strlen(l[i]) == n && !memcmp(l[i], w, n))
            return 1;
    return 0;
}
static const char *const ckey[] = {"if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto", "sizeof", "typedef", "struct", "union", "enum", "static", "const", "extern", "volatile", "inline", "register", "class", "public", "private", "new", "delete", "fn", "let", "mut", "impl", "use", "func", "var", "function", "import", "package", "true", "false", "NULL", "nullptr", 0};
static const char *const ctype[] = {"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "auto", 0};
static const char *const hkey[] = {"if", "elif", "else", "fi", "for", "while", "do", "done", "then", "case", "esac", "in", "def", "class", "return", "import", "from", "as", "with", "try", "except", "finally", "raise", "pass", "break", "continue", "lambda", "and", "or", "not", "is", "None", "True", "False", "yield", "global", "export", "local", "function", "echo", "ifeq", "ifneq", "endif", "include", 0};
static int isw(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
/* colours of one line into a; *blk: inside a C block comment, carried from line to line */
static void colour_line(ed_t *e, int s, int n, u8 *a, int *blk) {
    const char *b = e->b + s;
    for (int i = 0; i < n; i++)
        a[i] = A_TEXT;
    if (e->lang == L_TEXT)
        return;
    if (e->lang == L_MD) {
        if (n && b[0] == '#')
            for (int i = 0; i < n; i++)
                a[i] = A_KEY;
        return;
    }
    int i = 0, first = 1;
    while (i < n) {
        char c = b[i];
        if (*blk) {
            a[i] = A_COM;
            if (c == '*' && i + 1 < n && b[i + 1] == '/') {
                a[i + 1] = A_COM;
                *blk = 0;
                i += 2;
            } else
                i++;
            continue;
        }
        if (e->lang == L_C && c == '/' && i + 1 < n && b[i + 1] == '/') {
            for (; i < n; i++)
                a[i] = A_COM;
            break;
        }
        if (e->lang == L_C && c == '/' && i + 1 < n && b[i + 1] == '*') {
            *blk = 1;
            a[i++] = A_COM;
            a[i++] = A_COM;
            continue;
        }
        if (c == '#' && (e->lang == L_HASH || first)) {
            u8 at = e->lang == L_HASH ? A_COM : A_PRE; /* a comment, or #include and #define */
            for (; i < n; i++) {
                if (at == A_PRE && b[i] == '/' && i + 1 < n && (b[i + 1] == '/' || b[i + 1] == '*'))
                    break;
                a[i] = at;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            a[i++] = A_STR;
            while (i < n && b[i] != c) {
                if (b[i] == '\\' && i + 1 < n)
                    a[i++] = A_STR;
                a[i++] = A_STR;
            }
            if (i < n)
                a[i++] = A_STR;
            first = 0;
            continue;
        }
        if (c >= '0' && c <= '9' && (i == 0 || !isw(b[i - 1]))) {
            while (i < n && (isw(b[i]) || b[i] == '.'))
                a[i++] = A_NUM;
            first = 0;
            continue;
        }
        if (isw(c)) {
            int j = i;
            while (j < n && isw(b[j]))
                j++;
            u8 at = A_TEXT;
            if (e->lang == L_C)
                at = word_in(b + i, j - i, ckey) ? A_KEY : word_in(b + i, j - i, ctype) ? A_TYPE : A_TEXT;
            else if (word_in(b + i, j - i, hkey))
                at = A_KEY;
            for (; i < j; i++)
                a[i] = at;
            first = 0;
            continue;
        }
        if (c != ' ' && c != '\t')
            first = 0;
        i++;
    }
}
/* is a C block comment open at p? */
static int blk_at(ed_t *e, int p) {
    int blk = 0;
    if (e->lang != L_C)
        return 0;
    for (int i = 0; i < p; i++) {
        char c = e->b[i];
        if (blk) {
            if (c == '*' && i + 1 < p && e->b[i + 1] == '/')
                blk = 0, i++;
        } else if (c == '/' && i + 1 < p && e->b[i + 1] == '*')
            blk = 1, i++;
        else if (c == '/' && i + 1 < p && e->b[i + 1] == '/')
            while (i < p && e->b[i] != '\n')
                i++;
        else if (c == '"' || c == '\'') {
            for (i++; i < p && e->b[i] != c && e->b[i] != '\n'; i++)
                if (e->b[i] == '\\')
                    i++;
        }
    }
    return blk;
}

static const char *const keys_de =
    "Pfeile, Pos1/Ende, Bild auf/ab: bewegen\n"
    "Strg+Pfeil: ein Wort weiter\n"
    "Umschalt dazu: markieren, Strg+A alles\n"
    "Strg+C/X/V: kopieren, ausschneiden, einfügen\n"
    "Strg+Z/Y: rückgängig, wiederherstellen\n"
    "Tab/Umschalt+Tab: Zeilen ein-/ausrücken\n"
    "Strg+F suchen, Strg+G weitersuchen\n"
    "Strg+R ersetzen, Strg+L gehe zu Zeile\n"
    "Strg+S speichern, Strg+O öffnen, Strg+N neu\n"
    "Menü: Speichern unter, Zeilennummern";
static const char *const keys_en =
    "Arrows, Home/End, Page Up/Down: move\n"
    "Ctrl+arrow: one word on\n"
    "with Shift: select, Ctrl+A all\n"
    "Ctrl+C/X/V: copy, cut, paste\n"
    "Ctrl+Z/Y: undo, redo\n"
    "Tab/Shift+Tab: indent lines in/out\n"
    "Ctrl+F find, Ctrl+G find again\n"
    "Ctrl+R replace, Ctrl+L go to line\n"
    "Ctrl+S save, Ctrl+O open, Ctrl+N new\n"
    "Menu: Save as, line numbers";
static void ed_name(win_t *w, ed_t *e) {
    char t[40];
    ksnprintf(t, 40, "%s%s", e->dirty ? "* " : "", e->file[0] ? base(e->file) : TR("Unbenannt", "Untitled"));
    win_title(w, t);
}
static int gutter(ed_t *e) {
    if (!e->nums)
        return 0;
    int n = nlines(e), d = 1;
    while (n >= 10)
        n /= 10, d++;
    return (d < 3 ? 3 : d) + 1;
}
static void ed_draw(win_t *w) {
    ed_t *e = w->st;
    int H_ = ch(w), gw = gutter(e), W_ = cw(w) - gw;
    if (W_ < 4)
        W_ = 4;
    int cl = lineno(e, e->cur), cc = vcol(e, e->cur);
    if (cl < e->top)
        e->top = cl;
    if (cl >= e->top + H_)
        e->top = cl - H_ + 1;
    if (cc < e->col)
        e->col = cc;
    if (cc >= e->col + W_)
        e->col = cc - W_ + 1;
    cfill(w, A_TEXT);
    int p = linepos(e, e->top), blk = blk_at(e, p), a = sel_a(e), b = sel_b(e), hs = has_sel(e);
    static u8 at[4096];
    for (int y = 0; y < H_ && p <= e->len; y++) {
        int end = le(e, p), n = end - p;
        if (gw) {
            char g[12];
            int v = e->top + y + 1, d = gw - 1;
            g[d] = ' ';
            g[d + 1] = 0;
            for (int i = d - 1; i >= 0; i--, v /= 10)
                g[i] = v || i == d - 1 ? (char)('0' + v % 10) : ' ';
            ctext(w, 0, y, A_GUT, g);
        }
        int cn = n > 4096 ? 4096 : n; /* very long lines: the rest without colours */
        colour_line(e, p, cn, at, &blk);
        int x = 0;
        for (int i = 0; i < n; i++) {
            char c = e->b[p + i];
            u8 col = hs && p + i >= a && p + i < b ? A_SEL : i < cn ? at[i] : A_TEXT;
            int nx = c == '\t' ? (x / TABW + 1) * TABW : x + 1;
            for (int k = x; k < nx; k++)
                if (k >= e->col && k < e->col + W_)
                    cput(w, gw + k - e->col, y, c == '\t' ? ' ' : (u8)c, col);
            x = nx;
            if (x >= e->col + W_)
                break;
        }
        if (hs && end >= a && end < b && x >= e->col && x < e->col + W_) /* the line end selected */
            cput(w, gw + x - e->col, y, ' ', A_SEL);
        if (end >= e->len)
            break;
        p = end + 1;
    }
    w->cx = gw + cc - e->col;
    w->cy = cl - e->top;
    w->cur = 1;
    if (e->help) { /* the keys, over the text */
        const char *t = TR(keys_de, keys_en);
        int y = 1, bw = cw(w) - 4;
        for (int i = 0; i < 12 && y < H_; i++)
            for (int x = 1; x < cw(w) - 1; x++)
                cput(w, x, i, ' ', AT(C_RAISE, C_LAV));
        ctext(w, 2, 0, AT(C_RAISE, C_HOT), TR("Tasten (Taste: zurück)", "Keys (any key: back)"));
        while (*t && y < H_) {
            char l[100];
            int n = 0;
            while (*t && *t != '\n' && n < 99 && n < bw)
                l[n++] = *t++;
            l[n] = 0;
            while (*t && *t != '\n')
                t++;
            if (*t)
                t++;
            ctext(w, 2, y++, AT(C_RAISE, C_LAV), l);
        }
        w->cur = 0;
    }
    static const char *const ln[] = {"Text", "C", "Script", "Markdown"};
    if (e->msg[0])
        ksnprintf(w->foot, 64, " %s ", e->msg);
    else if (cw(w) >= 56)
        ksnprintf(w->foot, 64, TR(" Z %d/%d  S %d  %s   F1 Tasten ", " Ln %d/%d  Col %d  %s   F1 keys "), cl + 1, nlines(e), cc + 1, ln[e->lang]);
    else
        ksnprintf(w->foot, 64, TR(" Z %d  S %d ", " Ln %d  Col %d "), cl + 1, cc + 1);
    ed_name(w, e);
}

/* ---------- files ---------- */
static int ed_save_to(ed_t *e, const char *f) {
    char *o = e->b;
    int n = e->len;
    if (e->crlf) { /* back to CR LF */
        int lf = 0;
        for (int i = 0; i < e->len; i++)
            lf += e->b[i] == '\n';
        o = kmalloc(e->len + lf + 1);
        if (!o) {
            dlg_msg(TR("Speichern", "Save"), TR("Nicht genug Speicher.", "Not enough memory."));
            return 0;
        }
        n = 0;
        for (int i = 0; i < e->len; i++) {
            if (e->b[i] == '\n')
                o[n++] = '\r';
            o[n++] = e->b[i];
        }
    }
    int r = fsx_write(f, o, n, fs_now());
    if (o != e->b)
        kfree(o);
    if (r) {
        dlg_msg(TR("Speichern", "Save"), vfs_errstr(r, cfg_lang));
        return 0;
    }
    e->dirty = 0;
    strlcpy_(e->msg, TR("Gespeichert", "Saved"), 60);
    return 1;
}
static void ed_save(ed_t *e, int ask) {
    if (!e->file[0] || ask) {
        char n[VFS_PATH], p[VFS_PATH];
        if (e->file[0])
            strlcpy_(n, e->file, VFS_PATH);
        else
            ksnprintf(n, VFS_PATH, "%s/", fsx_home());
        if (!dlg_input(TR("Speichern unter", "Save as"), TR("Pfad und Dateiname:", "Path and file name:"), n, VFS_PATH - 1))
            return;
        if (vfs_norm(fsx_home(), n, p, VFS_PATH)) {
            dlg_msg(TR("Speichern", "Save"), vfs_errstr(VE_INVAL, cfg_lang));
            return;
        }
        vfs_ent st;
        if (strcmp(p, e->file) && !fsx_stat(p, &st) &&
            !dlg_confirm(TR("Speichern unter", "Save as"), TR("Die Datei gibt es schon. Ersetzen?", "The file exists. Replace it?")))
            return;
        if (!ed_save_to(e, p))
            return;
        strlcpy_(e->file, p, VFS_PATH);
        e->lang = lang_of(p);
        return;
    }
    ed_save_to(e, e->file);
}

/* ---------- find, replace, go to ---------- */
static int find_from(ed_t *e, int from) {
    int n = strlen(e->find);
    if (!n)
        return -1;
    for (int k = 0; k < 2; k++) { /* to the end, then from the start */
        int s = k ? 0 : from, t = k ? from : e->len;
        for (int i = s; i + n <= e->len && i < t + (k ? n : 0); i++) {
            int j = 0;
            while (j < n) {
                char x = e->b[i + j], y = e->find[j];
                if (x >= 'A' && x <= 'Z')
                    x += 32;
                if (y >= 'A' && y <= 'Z')
                    y += 32;
                if (x != y)
                    break;
                j++;
            }
            if (j == n)
                return i;
        }
    }
    return -1;
}
static void find_next(ed_t *e) {
    int i = find_from(e, has_sel(e) ? sel_b(e) : e->cur);
    if (i < 0) {
        strlcpy_(e->msg, TR("Nicht gefunden", "Not found"), 60);
        return;
    }
    e->anc = i;
    e->cur = i + strlen(e->find);
    e->sel = 1;
}
static void replace_all(ed_t *e) {
    char with[80] = "";
    if (!dlg_input(TR("Ersetzen", "Replace"), TR("Suchen nach:", "Find:"), e->find, 79) || !e->find[0])
        return;
    if (!dlg_input(TR("Ersetzen", "Replace"), TR("Ersetzen durch (leer: löschen):", "Replace with (empty: delete):"), with, 79) && with[0])
        return;
    int n = strlen(e->find), m = strlen(with), c = 0;
    for (int i = 0; i + n <= e->len;) {
        if (!memcmp(e->b + i, e->find, n)) {
            if (!edit(e, i, n, with, m, 0))
                break;
            i += m;
            c++;
        } else
            i++;
    }
    e->sel = 0;
    if (e->cur > e->len)
        e->cur = e->len;
    ksnprintf(e->msg, 60, TR("%d Mal ersetzt", "Replaced %d times"), c);
}
static void goto_line(ed_t *e) {
    char t[16] = "";
    if (!dlg_input(TR("Gehe zu Zeile", "Go to line"), TR("Zeilennummer:", "Line number:"), t, 15))
        return;
    int n = 0;
    for (char *q = t; *q >= '0' && *q <= '9'; q++)
        n = n * 10 + *q - '0';
    if (n < 1)
        n = 1;
    e->cur = linepos(e, n - 1);
    e->sel = 0;
}

/* ---------- keys ---------- */
static void ed_vert(ed_t *e, int d) {
    int l = lineno(e, e->cur) + d;
    if (l < 0)
        l = 0;
    int p = linepos(e, l);
    e->cur = at_vcol(e, p, e->want);
}
/* Tab or Shift+Tab on the selected lines: in or out by 4 */
static void shift_lines(ed_t *e, int out) {
    int a = ls(e, sel_a(e)), b = sel_b(e);
    if (b > a && b == ls(e, b))
        b--; /* a selection that ends at a line start leaves that line alone */
    int p = a;
    while (p <= b && p <= e->len) {
        int end = le(e, p);
        if (out) {
            int k = 0;
            while (k < TABW && p + k < end && e->b[p + k] == ' ')
                k++;
            if (!k && p < end && e->b[p] == '\t')
                k = 1;
            if (k && edit(e, p, k, "", 0, 0))
                b -= k, end -= k;
        } else if (end > p && edit(e, p, 0, "    ", TABW, 0))
            b += TABW, end += TABW;
        p = end + 1;
    }
    e->anc = a;
    e->cur = b < e->len ? le(e, b) : e->len;
    e->sel = 1;
}
static void enter(ed_t *e) {
    char ind[81] = "\n";
    int s = ls(e, e->cur), n = 1;
    while (s < e->cur && (e->b[s] == ' ' || e->b[s] == '\t') && n < 80)
        ind[n++] = e->b[s++];
    if (e->lang == L_C && e->cur > 0 && e->b[e->cur - 1] == '{' && n + TABW < 80) /* after { one more */
        for (int i = 0; i < TABW; i++)
            ind[n++] = ' ';
    put_text(e, ind, n, 0);
}
static void copy_sel(ed_t *e) {
    if (!has_sel(e))
        return;
    kfree(clip);
    nclip = sel_b(e) - sel_a(e);
    clip = dup_(e->b + sel_a(e), nclip);
    if (!clip)
        nclip = 0;
}
static void ed_key(win_t *w, key_t k) {
    ed_t *e = w->st;
    int c = k.mods & M_CTRL, sh = k.mods & M_SHIFT, keepw = 0;
    e->msg[0] = 0;
    if (e->help) {
        e->help = 0;
        return;
    }
    if (k.key >= K_MENU && k.key < K_MENU + 8) { /* the menu: same as the keys */
        static const u16 mk[] = {'o', 's', 'S', 'f', 'r', 'l', 'N', 0};
        int i = k.key - K_MENU;
        if (mk[i] == 'S') {
            ed_save(e, 1);
            return;
        }
        if (mk[i] == 'N') {
            e->nums = !e->nums;
            return;
        }
        k.key = mk[i];
        k.mods = M_CTRL;
        c = 1;
        sh = 0;
    }
    int move = 0, old = e->cur;
    if (c) {
        switch (k.key) {
            case 's':
            case 'S':
                ed_save(e, sh || k.key == 'S');
                return;
            case 'o': {
                char n[VFS_PATH];
                if (dlg_pick(TR("Öffnen", "Open"), n, VFS_PATH))
                    app_write(n);
                return;
            }
            case 'n':
                app_write(0);
                return;
            case 'a':
                e->anc = 0;
                e->cur = e->len;
                e->sel = 1;
                return;
            case 'c':
                copy_sel(e);
                return;
            case 'x':
                copy_sel(e);
                del_sel(e);
                return;
            case 'v':
                if (clip)
                    put_text(e, clip, nclip, 0);
                return;
            case 'z':
                undo(e, 0);
                return;
            case 'y':
                undo(e, 1);
                return;
            case 'f':
                if (has_sel(e) && sel_b(e) - sel_a(e) < 79 && lineno(e, sel_b(e)) == lineno(e, sel_a(e))) {
                    memcpy(e->find, e->b + sel_a(e), sel_b(e) - sel_a(e));
                    e->find[sel_b(e) - sel_a(e)] = 0;
                }
                if (dlg_input(TR("Suchen", "Find"), TR("Suchen nach:", "Find:"), e->find, 79)) {
                    e->sel = 0;
                    find_next(e);
                }
                return;
            case 'g':
                find_next(e);
                return;
            case 'r':
                replace_all(e);
                return;
            case 'l':
                goto_line(e);
                return;
            case K_HOME:
                e->cur = 0;
                move = 1;
                break;
            case K_END:
                e->cur = e->len;
                move = 1;
                break;
            case K_LEFT: /* a word to the left */
                while (e->cur && !isw(e->b[e->cur - 1]))
                    e->cur--;
                while (e->cur && isw(e->b[e->cur - 1]))
                    e->cur--;
                move = 1;
                break;
            case K_RIGHT:
                while (e->cur < e->len && !isw(e->b[e->cur]))
                    e->cur++;
                while (e->cur < e->len && isw(e->b[e->cur]))
                    e->cur++;
                move = 1;
                break;
            default:
                return;
        }
    } else
        switch (k.key) {
            case K_F1:
                e->help = 1;
                return;
            case K_F3:
                find_next(e);
                return;
            case K_LEFT:
                if (has_sel(e) && !sh)
                    e->cur = sel_a(e);
                else if (e->cur)
                    e->cur--;
                move = 1;
                break;
            case K_RIGHT:
                if (has_sel(e) && !sh)
                    e->cur = sel_b(e);
                else if (e->cur < e->len)
                    e->cur++;
                move = 1;
                break;
            case K_UP:
                ed_vert(e, -1);
                move = keepw = 1;
                break;
            case K_DOWN:
                ed_vert(e, 1);
                move = keepw = 1;
                break;
            case K_PGUP:
                ed_vert(e, -(ch(w) - 1));
                move = keepw = 1;
                break;
            case K_PGDN:
                ed_vert(e, ch(w) - 1);
                move = keepw = 1;
                break;
            case K_HOME: { /* first the start of the text in the line, then the line start */
                int s = ls(e, e->cur), t = s;
                while (t < e->len && (e->b[t] == ' ' || e->b[t] == '\t'))
                    t++;
                e->cur = e->cur == t ? s : t;
                move = 1;
                break;
            }
            case K_END:
                e->cur = le(e, e->cur);
                move = 1;
                break;
            case K_ENTER:
                enter(e);
                break;
            case K_TAB:
                if (has_sel(e) || sh)
                    shift_lines(e, sh);
                else {
                    int n = TABW - vcol(e, e->cur) % TABW;
                    put_text(e, "    ", n, 1);
                }
                e->want = vcol(e, e->cur);
                return;
            case K_BS:
                if (has_sel(e))
                    del_sel(e);
                else if (e->cur) {
                    int n = 1, s = ls(e, e->cur), v = vcol(e, e->cur);
                    /* in the indentation: back to the last tab stop */
                    int only = 1;
                    for (int i = s; i < e->cur; i++)
                        only &= e->b[i] == ' ';
                    if (only && v && !(v % TABW))
                        n = TABW;
                    if (edit(e, e->cur - n, n, "", 0, 0))
                        e->cur -= n;
                }
                break;
            case K_DEL:
                if (has_sel(e))
                    del_sel(e);
                else if (e->cur < e->len)
                    edit(e, e->cur, 1, "", 0, 0);
                break;
            default:
                if (k.key >= 32 && k.key < 256 && !(k.mods & M_ALT)) {
                    char ch_ = (char)k.key;
                    put_text(e, &ch_, 1, ch_ != ' ');
                }
        }
    if (move) {
        if (sh) {
            if (!e->sel) {
                e->sel = 1;
                e->anc = old;
            }
        } else
            e->sel = 0;
    } else
        e->sel = e->sel && has_sel(e);
    if (!keepw)
        e->want = vcol(e, e->cur);
}
static int ed_cc(win_t *w) {
    ed_t *e = w->st;
    if (e->help) { /* Esc only closes the key overlay */
        e->help = 0;
        return 0;
    }
    if (!e->dirty)
        return 1;
    int c = dlg_savechoice();
    if (c == 2)
        return 1;
    if (c == 1) {
        ed_save(e, 0);
        return !e->dirty;
    }
    return 0;
}
static void ed_free(win_t *w) {
    ed_t *e = w->st;
    for (int i = 0; i < e->nu; i++)
        op_free(&e->u[i]);
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
            dlg_msg(TR("Öffnen", "Open"), TR("Datei ist zu groß für Schreiben (max. 1 MB).", "File is too large for Write (1 MB max)."));
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
        int o = 0, cr = 0;
        for (int i = 0; i < n; i++) {
            if (b[i] == '\r' && i + 1 < n && b[i + 1] == '\n') {
                cr++;
                continue;
            }
            b[o++] = b[i];
        }
        e->crlf = cr > 0; /* saved again with CR LF */
        e->len = o;
        strlcpy_(e->file, f, VFS_PATH);
        e->lang = lang_of(f);
    }
    e->nums = e->lang == L_C || e->lang == L_HASH;
    win_t *mw = win_open(TR("Unbenannt", "Untitled"), 4, 2, 72, 20, &ed_app, e);
    win_menu(mw, TR("Schreiben", "Write"),
             TR("Öffnen (Strg+O)|Speichern (Strg+S)|Speichern unter|Suchen (Strg+F)|Ersetzen (Strg+R)|"
                "Gehe zu Zeile (Strg+L)|Zeilennummern an/aus|*Info",
                "Open (Ctrl+O)|Save (Ctrl+S)|Save as|Find (Ctrl+F)|Replace (Ctrl+R)|Go to line (Ctrl+L)|"
                "Line numbers on/off|*Info"));
    if (!mw) {
        kfree(b);
        kfree(e);
        return;
    }
    vx_sys(23, 8, 1, 0); /* F1 shows the keys of Write */
    vx_sys(23, 21, 1, 0); /* Shift+Arrows select, Ctrl+Left/Right jump by words */
}
void prog_main(void) {
    static char a[VFS_PATH];
    ed_open(sys_arg(a, VFS_PATH) ? a : 0);
}
