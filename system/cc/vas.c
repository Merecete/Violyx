/* vas: the assembler of Violyx, see vas.h. Two passes over all files: the first one finds the size
 * of every instruction and so the place of every label, the second one writes the bytes with the
 * symbols resolved. The size of an instruction never depends on the value of a symbol (symbols
 * always take 32 bits), so both passes agree. */
#include "vas.h"
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef int64_t i64;
typedef uint64_t u64;

/* ---------- small helpers ---------- */
typedef struct {
    u8 *p;
    size_t n, cap;
} buf;
static int bput(buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->n + n)
            c *= 2;
        u8 *q = realloc(b->p, c);
        if (!q)
            return -1;
        b->p = q;
        b->cap = c;
    }
    if (d)
        memcpy(b->p + b->n, d, n);
    else
        memset(b->p + b->n, 0, n);
    b->n += n;
    return 0;
}
static int is_id(int c) {
    c &= 255; /* UTF-8 bytes belong to names too (chibicc allows Unicode identifiers) */
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.' || c == '$' || c == '@';
}
static int is_sp(int c) {
    return c == ' ' || c == '\t' || c == '\r';
}

/* ---------- symbols ---------- */
enum { S_TEXT, S_DATA, S_BSS, NSEC };
typedef struct sym {
    char *name;
    int file;    /* -1: global */
    int sec;     /* -1 undefined */
    u64 val;     /* offset in its section */
    u64 csize;   /* .comm */
    int calign;
    int globl;   /* this (file) declared it .globl */
    int loc;     /* declared .local in its file */
    struct sym *next;
} sym;
#define HSZ 8192
typedef struct {
    sym *h[HSZ];
    int file, pass, line, sec, listing;
    const char *fname;
    buf s[NSEC];
    u64 bss;
    u64 base[NSEC];
    char *err;
    buf list;
    int nlab[10], deflab[10]; /* numeric labels 0:-9: of this file */
} as_t;
static unsigned hash(const char *s, int f) {
    unsigned h = 2166136261u ^ (unsigned)f;
    while (*s)
        h = (h ^ (u8)*s++) * 16777619u;
    return h % HSZ;
}
static sym *sfind(as_t *a, const char *n, int f) {
    for (sym *s = a->h[hash(n, f)]; s; s = s->next)
        if (s->file == f && !strcmp(s->name, n))
            return s;
    return 0;
}
static sym *sadd(as_t *a, const char *n, int f) {
    sym *s = sfind(a, n, f);
    if (s)
        return s;
    s = calloc(1, sizeof *s);
    if (!s)
        return 0;
    size_t l = strlen(n);
    s->name = malloc(l + 1);
    if (!s->name)
        return 0;
    memcpy(s->name, n, l + 1);
    s->file = f;
    s->sec = -1;
    unsigned h = hash(n, f);
    s->next = a->h[h];
    a->h[h] = s;
    return s;
}
/* A name as seen from the current file: its own one first, then the global one */
static sym *slook(as_t *a, const char *n) {
    sym *s = sfind(a, n, a->file);
    if (s && s->sec >= 0)
        return s;
    sym *g = sfind(a, n, -1);
    if (g)
        return g;
    return s;
}

static void fail(as_t *a, const char *m, const char *x) {
    if (a->err[0])
        return;
    /* "file:line: message x" */
    char *o = a->err;
    size_t k = 0, max = 255;
    for (const char *p = a->fname; *p && k < max; p++)
        o[k++] = *p;
    if (k < max)
        o[k++] = ':';
    char d[12];
    int dn = 0, l = a->line;
    do
        d[dn++] = (char)('0' + l % 10);
    while ((l /= 10) && dn < 11);
    while (dn && k < max)
        o[k++] = d[--dn];
    for (const char *p = ": "; *p && k < max; p++)
        o[k++] = *p;
    for (const char *p = m; *p && k < max; p++)
        o[k++] = *p;
    if (x) {
        if (k < max)
            o[k++] = ' ';
        for (const char *p = x; *p && k < max; p++)
            o[k++] = *p;
    }
    o[k] = 0;
}

/* ---------- expressions: a number, or a symbol plus or minus a number ---------- */
typedef struct {
    i64 v;
    sym *s;
    int has; /* something was read */
} expr;
static const char *skipsp(const char *p) {
    while (is_sp(*p))
        p++;
    return p;
}
static const char *number(const char *p, i64 *v, int *ok) {
    u64 x = 0;
    *ok = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        for (;; p++) {
            int c = *p, d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                break;
            x = x * 16 + (u64)d;
            *ok = 1;
        }
    } else
        for (; *p >= '0' && *p <= '9'; p++) {
            x = x * 10 + (u64)(*p - '0');
            *ok = 1;
        }
    *v = (i64)x;
    return p;
}
/* name of a numeric label: "1b" / "1f" */
static void numlab(as_t *a, int d, int fwd, char *o) {
    int k = a->nlab[d] + (fwd ? 1 : 0);
    o[0] = '#';
    o[1] = (char)('0' + d);
    o[2] = '#';
    int i = 3;
    char t[12];
    int tn = 0;
    do
        t[tn++] = (char)('0' + k % 10);
    while ((k /= 10) && tn < 11);
    while (tn)
        o[i++] = t[--tn];
    o[i] = 0;
}
static const char *parse_expr(as_t *a, const char *p, expr *e) {
    e->v = 0;
    e->s = 0;
    e->has = 0;
    int sign = 1;
    p = skipsp(p);
    for (;;) {
        p = skipsp(p);
        if (*p == '-') {
            sign = -sign;
            p++;
            continue;
        }
        if (*p == '+') {
            p++;
            continue;
        }
        if (*p >= '0' && *p <= '9') {
            /* 1b / 1f */
            if (p[1] == 'b' || p[1] == 'f') {
                if (!is_id(p[2])) {
                    char n[24];
                    numlab(a, p[0] - '0', p[1] == 'f', n);
                    if (e->s)
                        fail(a, "two symbols in one expression", 0);
                    e->s = sadd(a, n, a->file);
                    e->has = 1;
                    p += 2;
                    goto next;
                }
            }
            i64 v;
            int ok;
            p = number(p, &v, &ok);
            e->v += sign * v;
            e->has = 1;
        } else if (is_id(*p) && *p != '$') {
            char n[256];
            int k = 0;
            while (is_id(*p) && k < 255) {
                if (*p == '@') { /* @PLT and the like: the plain symbol is meant */
                    if (!strncmp(p, "@PLT", 4))
                        p += 4;
                    else {
                        fail(a, "unsupported relocation", p);
                        return p;
                    }
                    break;
                }
                n[k++] = *p++;
            }
            n[k] = 0;
            if (sign < 0)
                fail(a, "negative symbol", n);
            if (e->s)
                fail(a, "two symbols in one expression", n);
            sym *s = slook(a, n);
            if (!s)
                s = sadd(a, n, a->file);
            e->s = s;
            e->has = 1;
        } else
            break;
    next:
        sign = 1;
        p = skipsp(p);
        if (*p != '+' && *p != '-')
            break;
    }
    return p;
}
static u64 eval(as_t *a, expr *e) {
    if (!e->s)
        return (u64)e->v;
    sym *s = e->s;
    if (s->sec < 0 || s->sec >= NSEC) {
        /* a symbol of the file may be global after all */
        if (s->file >= 0) {
            sym *g = sfind(a, s->name, -1);
            if (g && g->sec >= 0 && g->sec < NSEC)
                s = g;
        }
    }
    if (s->sec < 0 || s->sec >= NSEC) {
        if (a->pass == 2 && !a->listing)
            fail(a, "undefined symbol", s->name);
        return 0;
    }
    if (a->listing)
        return 0;
    return a->base[s->sec] + s->val + (u64)e->v;
}

/* ---------- registers and operands ---------- */
enum { R_GPR = 1, R_XMM, R_ST, R_RIP, R_SEG };
typedef struct {
    const char *n;
    u8 cls, num, size, flg; /* flg 1: needs REX (spl..dil), 2: high byte (ah..bh) */
} regd;
static const regd regs[] = {
    {"rax", 1, 0, 8, 0},   {"rcx", 1, 1, 8, 0},   {"rdx", 1, 2, 8, 0},   {"rbx", 1, 3, 8, 0},
    {"rsp", 1, 4, 8, 0},   {"rbp", 1, 5, 8, 0},   {"rsi", 1, 6, 8, 0},   {"rdi", 1, 7, 8, 0},
    {"r8", 1, 8, 8, 0},    {"r9", 1, 9, 8, 0},    {"r10", 1, 10, 8, 0},  {"r11", 1, 11, 8, 0},
    {"r12", 1, 12, 8, 0},  {"r13", 1, 13, 8, 0},  {"r14", 1, 14, 8, 0},  {"r15", 1, 15, 8, 0},
    {"eax", 1, 0, 4, 0},   {"ecx", 1, 1, 4, 0},   {"edx", 1, 2, 4, 0},   {"ebx", 1, 3, 4, 0},
    {"esp", 1, 4, 4, 0},   {"ebp", 1, 5, 4, 0},   {"esi", 1, 6, 4, 0},   {"edi", 1, 7, 4, 0},
    {"r8d", 1, 8, 4, 0},   {"r9d", 1, 9, 4, 0},   {"r10d", 1, 10, 4, 0}, {"r11d", 1, 11, 4, 0},
    {"r12d", 1, 12, 4, 0}, {"r13d", 1, 13, 4, 0}, {"r14d", 1, 14, 4, 0}, {"r15d", 1, 15, 4, 0},
    {"ax", 1, 0, 2, 0},    {"cx", 1, 1, 2, 0},    {"dx", 1, 2, 2, 0},    {"bx", 1, 3, 2, 0},
    {"sp", 1, 4, 2, 0},    {"bp", 1, 5, 2, 0},    {"si", 1, 6, 2, 0},    {"di", 1, 7, 2, 0},
    {"r8w", 1, 8, 2, 0},   {"r9w", 1, 9, 2, 0},   {"r10w", 1, 10, 2, 0}, {"r11w", 1, 11, 2, 0},
    {"r12w", 1, 12, 2, 0}, {"r13w", 1, 13, 2, 0}, {"r14w", 1, 14, 2, 0}, {"r15w", 1, 15, 2, 0},
    {"al", 1, 0, 1, 0},    {"cl", 1, 1, 1, 0},    {"dl", 1, 2, 1, 0},    {"bl", 1, 3, 1, 0},
    {"spl", 1, 4, 1, 1},   {"bpl", 1, 5, 1, 1},   {"sil", 1, 6, 1, 1},   {"dil", 1, 7, 1, 1},
    {"ah", 1, 4, 1, 2},    {"ch", 1, 5, 1, 2},    {"dh", 1, 6, 1, 2},    {"bh", 1, 7, 1, 2},
    {"r8b", 1, 8, 1, 0},   {"r9b", 1, 9, 1, 0},   {"r10b", 1, 10, 1, 0}, {"r11b", 1, 11, 1, 0},
    {"r12b", 1, 12, 1, 0}, {"r13b", 1, 13, 1, 0}, {"r14b", 1, 14, 1, 0}, {"r15b", 1, 15, 1, 0},
    {"r8l", 1, 8, 1, 0},   {"r9l", 1, 9, 1, 0},   {"r10l", 1, 10, 1, 0}, {"r11l", 1, 11, 1, 0},
    {"r12l", 1, 12, 1, 0}, {"r13l", 1, 13, 1, 0}, {"r14l", 1, 14, 1, 0}, {"r15l", 1, 15, 1, 0},
    {"rip", 4, 0, 8, 0},   {"fs", 5, 4, 2, 0},    {"gs", 5, 5, 2, 0},    {"st", 3, 0, 10, 0},
};
enum { O_NONE, O_REG, O_IMM, O_MEM, O_EXP };
typedef struct {
    int k;
    int cls, num, size, flg; /* O_REG */
    expr e;                  /* O_IMM, O_EXP, displacement of O_MEM */
    int base, idx, scale;    /* O_MEM: -1 none, base -2: rip */
    int ind;                 /* '*' */
    int seg;
} opnd;
static const char *parse_reg(as_t *a, const char *p, opnd *o) {
    char n[8];
    int k = 0;
    p++; /* % */
    while ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) {
        if (k < 7)
            n[k++] = *p;
        p++;
    }
    n[k] = 0;
    o->k = O_REG;
    if (!strncmp(n, "xmm", 3) && k > 3) {
        int v = 0;
        for (int i = 3; i < k; i++)
            v = v * 10 + n[i] - '0';
        if (v > 15)
            fail(a, "bad register", n);
        o->cls = R_XMM;
        o->num = v;
        o->size = 16;
        return p;
    }
    for (size_t i = 0; i < sizeof regs / sizeof *regs; i++)
        if (!strcmp(regs[i].n, n)) {
            o->cls = regs[i].cls;
            o->num = regs[i].num;
            o->size = regs[i].size;
            o->flg = regs[i].flg;
            if (o->cls == R_ST) { /* %st or %st(i) */
                p = skipsp(p);
                if (*p == '(') {
                    p = skipsp(p + 1);
                    o->num = *p - '0';
                    while (*p && *p != ')')
                        p++;
                    if (*p)
                        p++;
                }
            }
            return p;
        }
    fail(a, "unknown register", n);
    return p;
}
static const char *parse_opnd(as_t *a, const char *p, opnd *o) {
    memset(o, 0, sizeof *o);
    o->base = o->idx = -1;
    o->scale = 1;
    p = skipsp(p);
    if (*p == '*') {
        o->ind = 1;
        p = skipsp(p + 1);
    }
    if (*p == '$') {
        o->k = O_IMM;
        return parse_expr(a, p + 1, &o->e);
    }
    if (*p == '%') {
        const char *q = parse_reg(a, p, o);
        if (o->cls == R_SEG && *q == ':') { /* %fs:... */
            o->seg = o->num;
            p = q + 1;
            o->cls = 0;
        } else
            return q;
    }
    p = parse_expr(a, p, &o->e);
    p = skipsp(p);
    if (*p != '(') {
        o->k = O_EXP;
        if (o->seg)
            o->k = O_MEM;
        return p;
    }
    o->k = O_MEM;
    p = skipsp(p + 1);
    if (*p == '%') {
        opnd r;
        memset(&r, 0, sizeof r);
        p = parse_reg(a, p, &r);
        if (r.cls == R_RIP)
            o->base = -2;
        else if (r.cls == R_GPR && r.size == 8)
            o->base = r.num;
        else
            fail(a, "bad base register", 0);
        p = skipsp(p);
    }
    if (*p == ',') {
        p = skipsp(p + 1);
        if (*p == '%') {
            opnd r;
            memset(&r, 0, sizeof r);
            p = parse_reg(a, p, &r);
            if (r.cls != R_GPR || r.size != 8 || r.num == 4)
                fail(a, "bad index register", 0);
            o->idx = r.num;
            p = skipsp(p);
        }
        if (*p == ',') {
            i64 v;
            int ok;
            p = number(skipsp(p + 1), &v, &ok);
            if (v != 1 && v != 2 && v != 4 && v != 8)
                fail(a, "bad scale", 0);
            o->scale = (int)v;
            p = skipsp(p);
        }
    }
    if (*p != ')')
        fail(a, "expected )", 0);
    else
        p++;
    return p;
}

/* ---------- encoding ---------- */
typedef struct {
    u8 pfx[4];
    int npfx;
    int w;     /* REX.W */
    int rex;   /* REX needed anyway (spl..dil) */
    int hi8;   /* ah..bh used: no REX allowed */
    u8 op[3];
    int nop;
    int reg;   /* ModRM reg field, -1: no ModRM */
    opnd *rm;  /* r/m operand (register or memory) */
    int rmnum; /* r/m as register number when rm == 0 and reg >= 0 */
    int isz;   /* immediate size */
    expr imm;
    int rel;   /* rel32 to imm expression */
    int rel8;
    int oreg;  /* register in the low bits of the opcode (+r), -1 none */
} enc;
static void enc_init(enc *e) {
    memset(e, 0, sizeof *e);
    e->reg = -1;
    e->rmnum = -1;
    e->oreg = -1;
}
static void use_reg(enc *e, opnd *o) {
    if (o && o->k == O_REG && o->cls == R_GPR && o->size == 1) {
        if (o->flg & 1)
            e->rex = 1;
        if (o->flg & 2)
            e->hi8 = 1;
    }
}
static void emit(as_t *a, enc *e) {
    u8 b[32];
    int n = 0;
    for (int i = 0; i < e->npfx; i++)
        b[n++] = e->pfx[i];
    int R = e->reg >= 0 ? (e->reg >> 3) & 1 : 0, X = 0, B = 0;
    opnd *m = e->rm;
    if (m && m->k == O_REG)
        B = (m->num >> 3) & 1;
    else if (m && m->k == O_MEM) {
        if (m->base >= 0)
            B = (m->base >> 3) & 1;
        if (m->idx >= 0)
            X = (m->idx >> 3) & 1;
    } else if (e->rmnum >= 0)
        B = (e->rmnum >> 3) & 1;
    if (e->oreg >= 0)
        B = (e->oreg >> 3) & 1;
    if (e->w || R || X || B || e->rex) {
        if (e->hi8)
            fail(a, "ah/bh/ch/dh cannot be used here", 0);
        b[n++] = (u8)(0x40 | e->w << 3 | R << 2 | X << 1 | B);
    }
    for (int i = 0; i < e->nop; i++)
        b[n++] = e->op[i];
    if (e->oreg >= 0)
        b[n - 1] = (u8)(b[n - 1] + (e->oreg & 7));
    int ripat = -1;
    expr disp = {0, 0, 0};
    if (e->reg >= 0) {
        int r = e->reg & 7;
        if (m && m->k == O_REG)
            b[n++] = (u8)(0xC0 | r << 3 | (m->num & 7));
        else if (!m)
            b[n++] = (u8)(0xC0 | r << 3 | (e->rmnum & 7));
        else { /* memory */
            disp = m->e;
            int sym_ = disp.s != 0;
            i64 dv = disp.v;
            if (m->base == -2) { /* rip */
                b[n++] = (u8)(0x00 | r << 3 | 5);
                ripat = n;
                n += 4;
            } else if (m->base < 0) { /* absolute or index only */
                b[n++] = (u8)(0x00 | r << 3 | 4);
                int ss = m->scale == 8 ? 3 : m->scale == 4 ? 2 : m->scale == 2 ? 1 : 0;
                b[n++] = (u8)(ss << 6 | (m->idx >= 0 ? (m->idx & 7) : 4) << 3 | 5);
                u64 v = eval(a, &disp);
                if (a->pass == 2 && !a->listing && (i64)v != (int32_t)v)
                    fail(a, "address does not fit 32 bits", 0);
                memcpy(b + n, &v, 4);
                n += 4;
            } else {
                int base = m->base & 7, mod;
                if (sym_)
                    mod = 2;
                else if (dv == 0 && base != 5)
                    mod = 0;
                else if (dv >= -128 && dv <= 127)
                    mod = 1;
                else
                    mod = 2;
                if (m->idx >= 0 || base == 4) {
                    int ss = m->scale == 8 ? 3 : m->scale == 4 ? 2 : m->scale == 2 ? 1 : 0;
                    b[n++] = (u8)(mod << 6 | r << 3 | 4);
                    b[n++] = (u8)(ss << 6 | (m->idx >= 0 ? (m->idx & 7) : 4) << 3 | base);
                } else
                    b[n++] = (u8)(mod << 6 | r << 3 | base);
                if (mod == 1)
                    b[n++] = (u8)dv;
                else if (mod == 2) {
                    u64 v = sym_ ? eval(a, &disp) : (u64)dv;
                    if (sym_ && a->pass == 2 && !a->listing && (i64)v != (int32_t)v)
                        fail(a, "address does not fit 32 bits", 0);
                    memcpy(b + n, &v, 4);
                    n += 4;
                }
            }
        }
    }
    int immat = n;
    if (e->rel8)
        n += 1;
    else if (e->rel)
        n += 4;
    else if (e->isz) {
        u64 v = eval(a, &e->imm);
        memcpy(b + n, &v, (size_t)e->isz);
        n += e->isz;
    }
    u64 here = a->base[a->sec] + a->s[a->sec].n, end = here + (u64)n;
    if (ripat >= 0) {
        i64 d = a->listing ? (disp.s ? 0 : disp.v) : (i64)(eval(a, &disp) - end);
        if (a->pass == 2 && !a->listing && d != (int32_t)d)
            fail(a, "target too far", 0);
        int32_t d32 = (int32_t)d;
        memcpy(b + ripat, &d32, 4);
    }
    if (e->rel || e->rel8) {
        i64 d = a->listing ? 0 : (i64)(eval(a, &e->imm) - end);
        if (e->rel8) {
            if (a->pass == 2 && (d < -128 || d > 127))
                fail(a, "short jump too far", 0);
            b[immat] = (u8)d;
        } else {
            int32_t d32 = (int32_t)d;
            memcpy(b + immat, &d32, 4);
        }
    }
    if (a->sec == S_BSS) {
        fail(a, "code in .bss", 0);
        return;
    }
    bput(&a->s[a->sec], b, (size_t)n);
}

/* ---------- instructions ---------- */
static int suffix_size(char c) {
    return c == 'b' ? 1 : c == 'w' ? 2 : c == 'l' ? 4 : c == 'q' ? 8 : 0;
}
static int opsize(opnd *o, int n, int suf) {
    for (int i = 0; i < n; i++)
        if (o[i].k == O_REG && o[i].cls == R_GPR)
            return o[i].size;
    return suf;
}
static void size_pfx(enc *e, int sz) {
    if (sz == 2)
        e->pfx[e->npfx++] = 0x66;
    if (sz == 8)
        e->w = 1;
}
static int fits8(expr *x) {
    return !x->s && x->v >= -128 && x->v <= 127;
}
static int fits32(expr *x) {
    return !x->s && x->v >= INT32_MIN && x->v <= INT32_MAX;
}
static const char *const ccs[] = {"o",  "no", "b", "ae", "e",  "ne", "be", "a",  "s",  "ns",
                                   "p",  "np", "l", "ge", "le", "g",  "c",  "nae", "nb", "nc",
                                   "z",  "nz", "na", "nbe", "pe", "po", "nge", "nl", "ng", "nle"};
static const u8 ccv[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                         2, 2, 3, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15};
static int cond(const char *s) {
    for (size_t i = 0; i < sizeof ccs / sizeof *ccs; i++)
        if (!strcmp(ccs[i], s))
            return ccv[i];
    return -1;
}
/* ModRM with a register operand (reg field) and an r/m operand */
static void rm_reg(enc *e, int reg, opnd *rm) {
    e->reg = reg;
    e->rm = rm;
}
static int is_r(opnd *o) {
    return o->k == O_REG && o->cls == R_GPR;
}
static int is_x(opnd *o) {
    return o->k == O_REG && o->cls == R_XMM;
}
static int is_m(opnd *o) {
    return o->k == O_MEM;
}
static int is_rm(opnd *o) {
    return is_r(o) || is_m(o);
}
#define OP1(x) (e.op[0] = (x), e.nop = 1)
#define OP2(x, y) (e.op[0] = (x), e.op[1] = (y), e.nop = 2)
#define OP3(x, y, z) (e.op[0] = (x), e.op[1] = (y), e.op[2] = (z), e.nop = 3)

static void bad(as_t *a, const char *m) {
    fail(a, "unsupported instruction or operands:", m);
}
static void sse(as_t *a, enc *e, u8 pfx, u8 op, opnd *src, opnd *dst) {
    if (pfx)
        e->pfx[e->npfx++] = pfx;
    e->op[0] = 0x0F;
    e->op[1] = op;
    e->nop = 2;
    rm_reg(e, dst->num, src);
    emit(a, e);
}

static void instr(as_t *a, const char *mn, opnd *o, int n) {
    enc e;
    enc_init(&e);
    int L = (int)strlen(mn);
    char last = L ? mn[L - 1] : 0;
    /* prefixes written as their own word */
    if (!strcmp(mn, "lock") || !strcmp(mn, "rep") || !strcmp(mn, "repe") || !strcmp(mn, "repz") ||
        !strcmp(mn, "repne") || !strcmp(mn, "repnz")) {
        u8 p = mn[0] == 'l' ? 0xF0 : (mn[3] == 'n') ? 0xF2 : 0xF3;
        bput(&a->s[a->sec], &p, 1);
        return;
    }
    for (int i = 0; i < n; i++) {
        use_reg(&e, &o[i]);
        if (o[i].seg) {
            if (o[i].seg == 4 && is_m(&o[i]) && !o[i].e.s && o[i].base == -1) {
                /* only mov %fs:N, %reg */
            }
        }
    }
    /* simple ones without operands */
    struct {
        const char *n;
        u8 b[3];
        int len;
    } simple[] = {{"ret", {0xC3}, 1},         {"retq", {0xC3}, 1},      {"leave", {0xC9}, 1},
                  {"leaveq", {0xC9}, 1},      {"cdq", {0x99}, 1},       {"cltd", {0x99}, 1},
                  {"cqo", {0x48, 0x99}, 2},   {"cqto", {0x48, 0x99}, 2}, {"cltq", {0x48, 0x98}, 2},
                  {"cdqe", {0x48, 0x98}, 2},  {"cwtl", {0x98}, 1},      {"nop", {0x90}, 1},
                  {"hlt", {0xF4}, 1},         {"ud2", {0x0F, 0x0B}, 2}, {"syscall", {0x0F, 0x05}, 2},
                  {"pause", {0xF3, 0x90}, 2}, {"stosb", {0xAA}, 1},     {"stosq", {0x48, 0xAB}, 2},
                  {"stosl", {0xAB}, 1},       {"movsb", {0xA4}, 1},     {"movsq", {0x48, 0xA5}, 2},
                  {"movsl", {0xA5}, 1},       {"cmpsb", {0xA6}, 1},     {"scasb", {0xAE}, 1},
                  {"faddp", {0xDE, 0xC1}, 2}, {"fmulp", {0xDE, 0xC9}, 2}, {"fsubrp", {0xDE, 0xE9}, 2},
                  {"fsubp", {0xDE, 0xE1}, 2}, {"fdivrp", {0xDE, 0xF9}, 2}, {"fdivp", {0xDE, 0xF1}, 2},
                  {"fchs", {0xD9, 0xE0}, 2},  {"fabs", {0xD9, 0xE1}, 2}, {"fldz", {0xD9, 0xEE}, 2},
                  {"fld1", {0xD9, 0xE8}, 2},  {"fcomip", {0xDF, 0xF1}, 2}, {"fucomip", {0xDF, 0xE9}, 2},
                  {"fcomp", {0xD8, 0xD9}, 2}, {"fxch", {0xD9, 0xC9}, 2}, {"fsqrt", {0xD9, 0xFA}, 2},
                  {"fninit", {0xDB, 0xE3}, 2}, {"fwait", {0x9B}, 1},    {"data16", {0x66}, 1},
                  {"rex64", {0x48}, 1},       {"fprem", {0xD9, 0xF8}, 2}, {"frndint", {0xD9, 0xFC}, 2},
                  {"fsin", {0xD9, 0xFE}, 2},  {"fcos", {0xD9, 0xFF}, 2}, {"fpatan", {0xD9, 0xF3}, 2},
                  {"fyl2x", {0xD9, 0xF1}, 2}, {"f2xm1", {0xD9, 0xF0}, 2}, {"fscale", {0xD9, 0xFD}, 2},
                  {"fldl2e", {0xD9, 0xEA}, 2}, {"fldln2", {0xD9, 0xED}, 2}, {"fptan", {0xD9, 0xF2}, 2},
                  {"fstp", {0xDD, 0xD9}, 2}};
    if (n == 0) {
        if (!strcmp(mn, "data16") || !strcmp(mn, "rex64")) {
            fail(a, "unsupported instruction (thread-local storage)", mn);
            return;
        }
        for (size_t i = 0; i < sizeof simple / sizeof *simple; i++)
            if (!strcmp(simple[i].n, mn)) {
                bput(&a->s[a->sec], simple[i].b, (size_t)simple[i].len);
                return;
            }
    }
    /* x87 with %st(i) */
    if (n >= 1 && o[0].k == O_REG && o[0].cls == R_ST) {
        int i = o[0].num & 7;
        u8 b[2] = {0, 0};
        if (!strcmp(mn, "fstp"))
            b[0] = 0xDD, b[1] = (u8)(0xD8 + i);
        else if (!strcmp(mn, "fld"))
            b[0] = 0xD9, b[1] = (u8)(0xC0 + i);
        else if (!strcmp(mn, "fxch"))
            b[0] = 0xD9, b[1] = (u8)(0xC8 + i);
        else if (!strcmp(mn, "fcomip") || !strcmp(mn, "fcomip"))
            b[0] = 0xDF, b[1] = (u8)(0xF0 + i);
        else if (!strcmp(mn, "fucomip"))
            b[0] = 0xDF, b[1] = (u8)(0xE8 + i);
        else if (!strcmp(mn, "fucomi"))
            b[0] = 0xDB, b[1] = (u8)(0xE8 + i);
        else if (n == 2 && o[1].k == O_REG && o[1].cls == R_ST && (o[1].num & 7) == 0 &&
                 (!strcmp(mn, "fadd") || !strcmp(mn, "fsub") || !strcmp(mn, "fmul") ||
                  !strcmp(mn, "fdiv"))) /* fxxx %st(i), %st */
            b[0] = 0xD8, b[1] = (u8)((mn[1] == 'a'   ? 0xC0
                                      : mn[1] == 's' ? 0xE0
                                      : mn[1] == 'm' ? 0xC8
                                                     : 0xF0) +
                                     i);
        else if (!strcmp(mn, "faddp"))
            b[0] = 0xDE, b[1] = (u8)(0xC0 + (n > 1 ? o[1].num & 7 : i));
        else if (!strcmp(mn, "fmulp"))
            b[0] = 0xDE, b[1] = (u8)(0xC8 + (n > 1 ? o[1].num & 7 : i));
        else {
            bad(a, mn);
            return;
        }
        bput(&a->s[a->sec], b, 2);
        return;
    }
    /* x87 with memory */
    struct {
        const char *n;
        u8 op, r;
    } fm[] = {{"fldt", 0xDB, 5},   {"fstpt", 0xDB, 7},  {"flds", 0xD9, 0},   {"fldl", 0xDD, 0},
              {"fstps", 0xD9, 3},  {"fstpl", 0xDD, 3},  {"fsts", 0xD9, 2},   {"fstl", 0xDD, 2},
              {"fildl", 0xDB, 0},  {"fildll", 0xDF, 5}, {"fildq", 0xDF, 5},  {"filds", 0xDF, 0},
              {"fistps", 0xDF, 3}, {"fistpl", 0xDB, 3}, {"fistpq", 0xDF, 7}, {"fistpll", 0xDF, 7},
              {"fnstcw", 0xD9, 7}, {"fldcw", 0xD9, 5},  {"fisttpl", 0xDB, 1}, {"fisttpq", 0xDD, 1},
              {"fadds", 0xD8, 0},  {"faddl", 0xDC, 0},  {"fmuls", 0xD8, 1},  {"fmull", 0xDC, 1}};
    if (n == 1 && is_m(&o[0]))
        for (size_t i = 0; i < sizeof fm / sizeof *fm; i++)
            if (!strcmp(fm[i].n, mn)) {
                OP1(fm[i].op);
                rm_reg(&e, fm[i].r, &o[0]);
                emit(a, &e);
                return;
            }
    /* SSE */
    struct {
        const char *n;
        u8 pfx, op;
    } ss[] = {{"addsd", 0xF2, 0x58},    {"subsd", 0xF2, 0x5C},   {"mulsd", 0xF2, 0x59},
              {"divsd", 0xF2, 0x5E},    {"addss", 0xF3, 0x58},   {"subss", 0xF3, 0x5C},
              {"mulss", 0xF3, 0x59},    {"divss", 0xF3, 0x5E},   {"sqrtsd", 0xF2, 0x51},
              {"sqrtss", 0xF3, 0x51},   {"minsd", 0xF2, 0x5D},   {"maxsd", 0xF2, 0x5F},
              {"minss", 0xF3, 0x5D},    {"maxss", 0xF3, 0x5F},   {"ucomisd", 0x66, 0x2E},
              {"ucomiss", 0, 0x2E},     {"comisd", 0x66, 0x2F},  {"comiss", 0, 0x2F},
              {"xorpd", 0x66, 0x57},    {"xorps", 0, 0x57},      {"andpd", 0x66, 0x54},
              {"andps", 0, 0x54},       {"orpd", 0x66, 0x56},    {"orps", 0, 0x56},
              {"andnpd", 0x66, 0x55},   {"cvtss2sd", 0xF3, 0x5A}, {"cvtsd2ss", 0xF2, 0x5A},
              {"pxor", 0x66, 0xEF},     {"movapd", 0x66, 0x28},  {"movaps", 0, 0x28},
              {"unpcklps", 0, 0x14},    {"unpcklpd", 0x66, 0x14}};
    if (n == 2 && is_x(&o[1]) && (is_x(&o[0]) || is_m(&o[0])))
        for (size_t i = 0; i < sizeof ss / sizeof *ss; i++)
            if (!strcmp(ss[i].n, mn)) {
                sse(a, &e, ss[i].pfx, ss[i].op, &o[0], &o[1]);
                return;
            }
    if ((!strcmp(mn, "movsd") || !strcmp(mn, "movss")) && n == 2) {
        u8 p = mn[4] == 'd' ? 0xF2 : 0xF3;
        if (is_x(&o[1]) && (is_x(&o[0]) || is_m(&o[0])))
            sse(a, &e, p, 0x10, &o[0], &o[1]);
        else if (is_x(&o[0]) && is_m(&o[1]))
            sse(a, &e, p, 0x11, &o[1], &o[0]);
        else
            bad(a, mn);
        return;
    }
    if ((!strcmp(mn, "movq") || !strcmp(mn, "movd")) && n == 2 && (is_x(&o[0]) || is_x(&o[1]))) {
        int q = mn[3] == 'q';
        if (is_x(&o[1]) && is_r(&o[0])) { /* gpr -> xmm */
            e.pfx[e.npfx++] = 0x66;
            e.w = q;
            OP2(0x0F, 0x6E);
            rm_reg(&e, o[1].num, &o[0]);
        } else if (is_x(&o[0]) && is_r(&o[1])) { /* xmm -> gpr */
            e.pfx[e.npfx++] = 0x66;
            e.w = q;
            OP2(0x0F, 0x7E);
            rm_reg(&e, o[0].num, &o[1]);
        } else if (q && is_x(&o[1])) { /* mem/xmm -> xmm */
            e.pfx[e.npfx++] = 0xF3;
            OP2(0x0F, 0x7E);
            rm_reg(&e, o[1].num, &o[0]);
        } else if (q && is_x(&o[0]) && is_m(&o[1])) {
            e.pfx[e.npfx++] = 0x66;
            OP2(0x0F, 0xD6);
            rm_reg(&e, o[0].num, &o[1]);
        } else if (!q && is_x(&o[1]) && is_m(&o[0])) {
            e.pfx[e.npfx++] = 0x66;
            OP2(0x0F, 0x6E);
            rm_reg(&e, o[1].num, &o[0]);
        } else if (!q && is_x(&o[0]) && is_m(&o[1])) {
            e.pfx[e.npfx++] = 0x66;
            OP2(0x0F, 0x7E);
            rm_reg(&e, o[0].num, &o[1]);
        } else {
            bad(a, mn);
            return;
        }
        emit(a, &e);
        return;
    }
    /* cvtsi2sd[lq] / cvtsi2ss[lq], cvttsd2si[lq] / cvttss2si[lq], cvtsd2si / cvtss2si */
    if (!strncmp(mn, "cvtsi2s", 7) && n == 2 && is_x(&o[1])) {
        u8 p = mn[7] == 'd' ? 0xF2 : 0xF3;
        int sz = mn[8] ? suffix_size(mn[8]) : opsize(o, 1, 4);
        e.pfx[e.npfx++] = p;
        e.w = sz == 8;
        OP2(0x0F, 0x2A);
        rm_reg(&e, o[1].num, &o[0]);
        emit(a, &e);
        return;
    }
    if ((!strncmp(mn, "cvtts", 5) || !strncmp(mn, "cvts", 4)) && n == 2 && is_r(&o[1]) &&
        (strstr(mn, "2si"))) {
        int t = mn[3] == 't';
        const char *q = mn + (t ? 5 : 4); /* "d2si.." or "s2si.." */
        u8 p = q[0] == 'd' ? 0xF2 : 0xF3;
        e.pfx[e.npfx++] = p;
        e.w = o[1].size == 8;
        OP2(0x0F, t ? 0x2C : 0x2D);
        rm_reg(&e, o[1].num, &o[0]);
        emit(a, &e);
        return;
    }
    /* jumps and calls */
    if (!strcmp(mn, "jmp") || !strcmp(mn, "call") || !strcmp(mn, "callq") || !strcmp(mn, "jmpq")) {
        int call = mn[0] == 'c';
        if (n == 1 && o[0].ind) {
            OP1(0xFF);
            if (o[0].k == O_REG)
                rm_reg(&e, call ? 2 : 4, &o[0]);
            else
                rm_reg(&e, call ? 2 : 4, &o[0]);
            emit(a, &e);
            return;
        }
        if (n == 1 && o[0].k == O_EXP) {
            OP1(call ? 0xE8 : 0xE9);
            e.rel = 1;
            e.imm = o[0].e;
            emit(a, &e);
            return;
        }
        bad(a, mn);
        return;
    }
    if (mn[0] == 'j' && n == 1 && o[0].k == O_EXP) {
        int c = cond(mn + 1);
        if (c >= 0) {
            OP2(0x0F, (u8)(0x80 + c));
            e.rel = 1;
            e.imm = o[0].e;
            emit(a, &e);
            return;
        }
    }
    if (!strncmp(mn, "set", 3) && n == 1 && is_rm(&o[0])) {
        int c = cond(mn + 3);
        if (c >= 0) {
            OP2(0x0F, (u8)(0x90 + c));
            rm_reg(&e, 0, &o[0]);
            emit(a, &e);
            return;
        }
    }
    if (!strncmp(mn, "cmov", 4) && n == 2 && is_r(&o[1])) {
        char t[8];
        strncpy(t, mn + 4, 7);
        t[7] = 0;
        int c = cond(t);
        int sz = o[1].size;
        if (c < 0 && strlen(t) > 1) { /* cmovel etc. */
            t[strlen(t) - 1] = 0;
            c = cond(t);
        }
        if (c >= 0) {
            size_pfx(&e, sz);
            OP2(0x0F, (u8)(0x40 + c));
            rm_reg(&e, o[1].num, &o[0]);
            emit(a, &e);
            return;
        }
    }
    if (!strcmp(mn, "int") && n == 1 && o[0].k == O_IMM) {
        u8 b[2] = {0xCD, (u8)o[0].e.v};
        bput(&a->s[a->sec], b, 2);
        return;
    }
    if ((!strcmp(mn, "push") || !strcmp(mn, "pushq") || !strcmp(mn, "pop") || !strcmp(mn, "popq")) &&
        n == 1) {
        int push = mn[1] == 'u';
        if (is_r(&o[0]) && o[0].size == 8) {
            OP1(push ? 0x50 : 0x58);
            e.oreg = o[0].num;
            emit(a, &e);
            return;
        }
        if (push && o[0].k == O_IMM) {
            if (fits8(&o[0].e)) {
                OP1(0x6A);
                e.isz = 1;
            } else {
                OP1(0x68);
                e.isz = 4;
            }
            e.imm = o[0].e;
            emit(a, &e);
            return;
        }
        if (is_m(&o[0])) {
            OP1(push ? 0xFF : 0x8F);
            rm_reg(&e, push ? 6 : 0, &o[0]);
            emit(a, &e);
            return;
        }
        bad(a, mn);
        return;
    }
    /* movsx / movzx families: movsbl movsbw movsbq movswl movswq movslq movsxd movzbl movzbw
     * movzbq movzwl movzwq movzb movzx movsx */
    if (n == 2 && is_r(&o[1]) && (!strncmp(mn, "movs", 4) || !strncmp(mn, "movz", 4)) &&
        strcmp(mn, "movsd") && strcmp(mn, "movss") && strcmp(mn, "movsb") && strcmp(mn, "movsq") &&
        strcmp(mn, "movsl")) {
        int z = mn[3] == 'z';
        const char *t = mn + 4;
        int from = 0, to = o[1].size;
        if (!strcmp(t, "xd") || !strcmp(t, "lq"))
            from = 4;
        else if (t[0] == 'x' || t[0] == 0) /* movzx / movsx: size from the source */
            from = is_r(&o[0]) ? o[0].size : 0;
        else {
            from = suffix_size(t[0]);
            if (t[1])
                to = suffix_size(t[1]);
        }
        if (!from || (is_r(&o[0]) && o[0].size != from)) {
            bad(a, mn);
            return;
        }
        if (to != o[1].size) {
            bad(a, mn);
            return;
        }
        size_pfx(&e, to);
        if (from == 4) {
            if (z) { /* movzlq = mov 32 bit */
                bad(a, mn);
                return;
            }
            OP1(0x63);
        } else
            OP2(0x0F, (u8)((z ? 0xB6 : 0xBE) + (from == 2)));
        rm_reg(&e, o[1].num, &o[0]);
        emit(a, &e);
        return;
    }
    /* the rest: integer instructions with an optional size suffix */
    char base_[16];
    strncpy(base_, mn, 15);
    base_[15] = 0;
    int suf = 0;
    static const char *const known[] = {
        "mov", "add", "or",  "adc", "sbb", "and", "sub", "xor",  "cmp",  "test", "lea", "inc",
        "dec", "neg", "not", "mul", "imul", "div", "idiv", "shl", "sal", "shr",  "sar", "rol",
        "ror", "xchg", "cmpxchg", "bswap", "movabs", "xadd", "bt",  "bsr", "bsf", "movnti"};
    int found = 0;
    for (size_t i = 0; i < sizeof known / sizeof *known && !found; i++)
        if (!strcmp(base_, known[i]))
            found = 1;
    if (!found && L > 1 && suffix_size(last)) {
        base_[L - 1] = 0;
        for (size_t i = 0; i < sizeof known / sizeof *known && !found; i++)
            if (!strcmp(base_, known[i]))
                found = 1;
        if (found)
            suf = suffix_size(last);
        else
            strncpy(base_, mn, 15);
    }
    if (!found) {
        bad(a, mn);
        return;
    }
    int sz = opsize(o, n, suf);
    if (!strcmp(base_, "lea") || !strcmp(base_, "bswap"))
        sz = opsize(o + n - 1, 1, suf);
    if (!sz) {
        fail(a, "size unknown, a suffix (b, w, l, q) is needed:", mn);
        return;
    }
    static const char *const alu[] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
    for (int k = 0; k < 8; k++)
        if (!strcmp(base_, alu[k])) {
            if (n != 2)
                break;
            size_pfx(&e, sz);
            if (o[0].k == O_IMM && !o[0].e.s && sz < 8) /* $0xfffffff0 on 32 bits is -16 */
                o[0].e.v = sz == 4 ? (int32_t)o[0].e.v : sz == 2 ? (int16_t)o[0].e.v : (int8_t)o[0].e.v;
            if (o[0].k == O_IMM && is_r(&o[1]) && o[1].num == 0 &&
                (sz == 1 || !fits8(&o[0].e))) { /* short form with al/ax/eax/rax */
                OP1((u8)(k * 8 + (sz == 1 ? 4 : 5)));
                e.isz = sz == 1 ? 1 : sz == 2 ? 2 : 4;
                e.imm = o[0].e;
                if (sz == 8 && !o[0].e.s && !fits32(&o[0].e)) {
                    fail(a, "immediate does not fit 32 bits", 0);
                    return;
                }
            } else if (o[0].k == O_IMM && is_rm(&o[1])) {
                if (sz == 1) {
                    OP1(0x80);
                    e.isz = 1;
                } else if (fits8(&o[0].e)) {
                    OP1(0x83);
                    e.isz = 1;
                } else {
                    OP1(0x81);
                    e.isz = sz == 2 ? 2 : 4;
                }
                if (sz == 8 && !o[0].e.s && !fits32(&o[0].e)) {
                    fail(a, "immediate does not fit 32 bits", 0);
                    return;
                }
                e.imm = o[0].e;
                rm_reg(&e, k, &o[1]);
            } else if (is_r(&o[0]) && is_rm(&o[1])) {
                OP1((u8)(k * 8 + (sz == 1 ? 0 : 1)));
                rm_reg(&e, o[0].num, &o[1]);
            } else if (is_m(&o[0]) && is_r(&o[1])) {
                OP1((u8)(k * 8 + (sz == 1 ? 2 : 3)));
                rm_reg(&e, o[1].num, &o[0]);
            } else
                break;
            emit(a, &e);
            return;
        }
    if (!strcmp(base_, "test") && n == 2) {
        size_pfx(&e, sz);
        if (o[0].k == O_IMM && is_r(&o[1]) && o[1].num == 0) {
            OP1(sz == 1 ? 0xA8 : 0xA9);
            e.isz = sz == 1 ? 1 : sz == 2 ? 2 : 4;
            e.imm = o[0].e;
        } else if (o[0].k == O_IMM && is_rm(&o[1])) {
            OP1(sz == 1 ? 0xF6 : 0xF7);
            e.isz = sz == 1 ? 1 : sz == 2 ? 2 : 4;
            e.imm = o[0].e;
            rm_reg(&e, 0, &o[1]);
        } else if (is_r(&o[0]) && is_rm(&o[1])) {
            OP1(sz == 1 ? 0x84 : 0x85);
            rm_reg(&e, o[0].num, &o[1]);
        } else if (is_m(&o[0]) && is_r(&o[1])) {
            OP1(sz == 1 ? 0x84 : 0x85);
            rm_reg(&e, o[1].num, &o[0]);
        } else {
            bad(a, mn);
            return;
        }
        emit(a, &e);
        return;
    }
    if ((!strcmp(base_, "mov") || !strcmp(base_, "movabs")) && n == 2) {
        if (o[0].seg || o[1].seg) {
            fail(a, "unsupported instruction (thread-local storage)", mn);
            return;
        }
        size_pfx(&e, sz);
        if (o[0].k == O_IMM && is_r(&o[1])) {
            if (sz == 8) {
                if (!o[0].e.s && fits32(&o[0].e) && base_[3] != 'a') {
                    OP1(0xC7);
                    e.isz = 4;
                    rm_reg(&e, 0, &o[1]);
                } else {
                    OP1(0xB8);
                    e.oreg = o[1].num;
                    e.isz = 8;
                }
            } else {
                OP1(sz == 1 ? 0xB0 : 0xB8);
                e.oreg = o[1].num;
                e.isz = sz;
            }
            e.imm = o[0].e;
        } else if (o[0].k == O_IMM && is_m(&o[1])) {
            OP1(sz == 1 ? 0xC6 : 0xC7);
            e.isz = sz == 1 ? 1 : sz == 2 ? 2 : 4;
            e.imm = o[0].e;
            rm_reg(&e, 0, &o[1]);
        } else if (is_r(&o[0]) && is_rm(&o[1])) {
            OP1(sz == 1 ? 0x88 : 0x89);
            rm_reg(&e, o[0].num, &o[1]);
        } else if (is_m(&o[0]) && is_r(&o[1])) {
            OP1(sz == 1 ? 0x8A : 0x8B);
            rm_reg(&e, o[1].num, &o[0]);
        } else {
            bad(a, mn);
            return;
        }
        emit(a, &e);
        return;
    }
    if (!strcmp(base_, "lea") && n == 2 && is_m(&o[0]) && is_r(&o[1])) {
        size_pfx(&e, o[1].size);
        OP1(0x8D);
        rm_reg(&e, o[1].num, &o[0]);
        emit(a, &e);
        return;
    }
    if (n == 1 && is_rm(&o[0])) {
        static const char *const un[] = {"inc", "dec", "not", "neg", "mul", "imul", "div", "idiv"};
        static const u8 unr[] = {0, 1, 2, 3, 4, 5, 6, 7};
        for (int k = 0; k < 8; k++)
            if (!strcmp(base_, un[k])) {
                size_pfx(&e, sz);
                OP1(k < 2 ? (sz == 1 ? 0xFE : 0xFF) : (sz == 1 ? 0xF6 : 0xF7));
                rm_reg(&e, unr[k], &o[0]);
                emit(a, &e);
                return;
            }
    }
    if (!strcmp(base_, "imul") && n >= 2 && is_r(&o[n - 1])) {
        size_pfx(&e, sz);
        if (n == 2 && is_rm(&o[0])) {
            OP2(0x0F, 0xAF);
            rm_reg(&e, o[1].num, &o[0]);
        } else if (n == 3 && o[0].k == O_IMM && is_rm(&o[1])) {
            if (fits8(&o[0].e)) {
                OP1(0x6B);
                e.isz = 1;
            } else {
                OP1(0x69);
                e.isz = sz == 2 ? 2 : 4;
            }
            e.imm = o[0].e;
            rm_reg(&e, o[2].num, &o[1]);
        } else if (n == 2 && o[0].k == O_IMM) { /* imul $n, %reg */
            if (fits8(&o[0].e)) {
                OP1(0x6B);
                e.isz = 1;
            } else {
                OP1(0x69);
                e.isz = sz == 2 ? 2 : 4;
            }
            e.imm = o[0].e;
            rm_reg(&e, o[1].num, &o[1]);
        } else {
            bad(a, mn);
            return;
        }
        emit(a, &e);
        return;
    }
    static const char *const sh[] = {"rol", "ror", "", "", "shl", "shr", "sal", "sar"};
    for (int k = 0; k < 8; k++)
        if (sh[k][0] && !strcmp(base_, sh[k])) {
            int r = k == 6 ? 4 : k;
            opnd *t = &o[n - 1];
            if (!is_rm(t))
                break;
            sz = is_r(t) ? t->size : suf; /* the count in %cl says nothing about the size */
            if (!sz) {
                fail(a, "size unknown, a suffix (b, w, l, q) is needed:", mn);
                return;
            }
            size_pfx(&e, sz);
            if (n == 1) {
                OP1(sz == 1 ? 0xD0 : 0xD1);
            } else if (o[0].k == O_IMM) {
                OP1(sz == 1 ? 0xC0 : 0xC1);
                e.isz = 1;
                e.imm = o[0].e;
            } else if (is_r(&o[0]) && o[0].num == 1 && o[0].size == 1) {
                OP1(sz == 1 ? 0xD2 : 0xD3);
            } else
                break;
            rm_reg(&e, r, t);
            emit(a, &e);
            return;
        }
    if ((!strcmp(base_, "xchg") || !strcmp(base_, "cmpxchg") || !strcmp(base_, "xadd")) && n == 2 &&
        is_r(&o[0]) && is_rm(&o[1])) {
        size_pfx(&e, sz);
        if (base_[0] == 'x' && base_[1] == 'c')
            OP1(sz == 1 ? 0x86 : 0x87);
        else if (base_[0] == 'x')
            OP2(0x0F, sz == 1 ? 0xC0 : 0xC1);
        else
            OP2(0x0F, sz == 1 ? 0xB0 : 0xB1);
        rm_reg(&e, o[0].num, &o[1]);
        emit(a, &e);
        return;
    }
    if ((!strcmp(base_, "bsr") || !strcmp(base_, "bsf")) && n == 2 && is_r(&o[1])) {
        size_pfx(&e, sz);
        OP2(0x0F, base_[2] == 'r' ? 0xBD : 0xBC);
        rm_reg(&e, o[1].num, &o[0]);
        emit(a, &e);
        return;
    }
    if (!strcmp(base_, "bswap") && n == 1 && is_r(&o[0]) && sz >= 4) {
        size_pfx(&e, sz);
        OP2(0x0F, 0xC8);
        e.oreg = o[0].num;
        emit(a, &e);
        return;
    }
    bad(a, mn);
}

/* ---------- directives ---------- */
static int setsec(as_t *a, const char *n) {
    if (!strncmp(n, ".text", 5))
        a->sec = S_TEXT;
    else if (!strncmp(n, ".data", 5) || !strncmp(n, ".rodata", 7))
        a->sec = S_DATA;
    else if (!strncmp(n, ".bss", 4))
        a->sec = S_BSS;
    else if (!strncmp(n, ".tdata", 6) || !strncmp(n, ".tbss", 5)) {
        fail(a, "thread-local variables are not supported", 0);
        return -1;
    } else if (!strncmp(n, ".note", 5) || !strncmp(n, ".comment", 8))
        a->sec = S_DATA; /* harmless */
    else {
        fail(a, "unknown section", n);
        return -1;
    }
    return 0;
}
static void data(as_t *a, const void *d, size_t n) {
    if (a->sec == S_BSS) {
        /* only zeros are allowed there */
        const u8 *p = d;
        for (size_t i = 0; p && i < n; i++)
            if (p[i])
                fail(a, "data in .bss", 0);
        a->bss += n;
        return;
    }
    bput(&a->s[a->sec], d, n);
}
static u64 pos(as_t *a) {
    return a->sec == S_BSS ? a->bss : a->s[a->sec].n;
}
static const char *string_lit(as_t *a, const char *p, int zero) {
    p = skipsp(p);
    if (*p != '"') {
        fail(a, "expected a string", 0);
        return p;
    }
    p++;
    while (*p && *p != '"') {
        u8 c = (u8)*p++;
        if (c == '\\') {
            c = (u8)*p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case 'a': c = 7; break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                int v = c - '0';
                for (int i = 0; i < 2 && *p >= '0' && *p <= '7'; i++)
                    v = v * 8 + (*p++ - '0');
                c = (u8)v;
                break;
            }
            case 'x': {
                int v = 0;
                while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
                       (*p >= 'A' && *p <= 'F')) {
                    int d = *p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10;
                    v = v * 16 + d;
                    p++;
                }
                c = (u8)v;
                break;
            }
            default: break;
            }
        }
        data(a, &c, 1);
    }
    if (*p == '"')
        p++;
    if (zero) {
        u8 z = 0;
        data(a, &z, 1);
    }
    return p;
}
static void directive(as_t *a, const char *d, const char *p) {
    char name[256];
    if (!strcmp(d, ".text") || !strcmp(d, ".data") || !strcmp(d, ".bss")) {
        setsec(a, d);
        return;
    }
    if (!strcmp(d, ".section")) {
        p = skipsp(p);
        int k = 0;
        while (*p && *p != ',' && !is_sp(*p) && k < 255)
            name[k++] = *p++;
        name[k] = 0;
        setsec(a, name);
        return;
    }
    if (!strcmp(d, ".file") || !strcmp(d, ".loc") || !strcmp(d, ".type") || !strcmp(d, ".size") ||
        !strcmp(d, ".ident") || !strcmp(d, ".local") || !strcmp(d, ".hidden") ||
        !strcmp(d, ".globl") || !strcmp(d, ".global") || !strcmp(d, ".weak") ||
        !strcmp(d, ".cfi_startproc") || !strcmp(d, ".cfi_endproc") || !strcmp(d, ".intel_syntax"))
        return; /* .globl was taken care of before the passes */
    if (!strcmp(d, ".align") || !strcmp(d, ".p2align") || !strcmp(d, ".balign")) {
        i64 v;
        int ok;
        number(skipsp(p), &v, &ok);
        if (d[1] == 'p')
            v = 1ll << (v & 31);
        if (v < 1 || v > 4096 || (v & (v - 1))) {
            fail(a, "bad alignment", 0);
            return;
        }
        while (pos(a) % (u64)v) {
            u8 f = a->sec == S_TEXT ? 0x90 : 0;
            data(a, &f, 1);
        }
        return;
    }
    if (!strcmp(d, ".zero") || !strcmp(d, ".skip") || !strcmp(d, ".space")) {
        i64 v;
        int ok;
        number(skipsp(p), &v, &ok);
        if (v < 0 || v > (1 << 28)) {
            fail(a, "bad size", 0);
            return;
        }
        if (a->sec == S_BSS)
            a->bss += (u64)v;
        else
            bput(&a->s[a->sec], 0, (size_t)v);
        return;
    }
    int sz = !strcmp(d, ".byte")                                              ? 1
             : !strcmp(d, ".short") || !strcmp(d, ".value") || !strcmp(d, ".word") ? 2
             : !strcmp(d, ".long") || !strcmp(d, ".int")                       ? 4
             : !strcmp(d, ".quad")                                            ? 8
                                                                              : 0;
    if (sz) {
        for (;;) {
            expr e;
            p = parse_expr(a, p, &e);
            if (!e.has) {
                fail(a, "expected a value", 0);
                return;
            }
            u64 v = eval(a, &e);
            if (e.s && sz != 8)
                fail(a, "a symbol needs 8 bytes (.quad)", 0);
            data(a, &v, (size_t)sz);
            p = skipsp(p);
            if (*p != ',')
                break;
            p++;
        }
        return;
    }
    if (!strcmp(d, ".string") || !strcmp(d, ".asciz") || !strcmp(d, ".ascii")) {
        for (;;) {
            p = string_lit(a, p, d[3] != 'c' || d[5]);
            p = skipsp(p);
            if (*p != ',')
                break;
            p++;
        }
        return;
    }
    if (!strcmp(d, ".comm") || !strcmp(d, ".lcomm")) {
        p = skipsp(p);
        int k = 0;
        while (is_id(*p) && k < 255)
            name[k++] = *p++;
        name[k] = 0;
        p = skipsp(p);
        i64 size = 0, al = 1;
        int ok;
        if (*p == ',')
            p = number(skipsp(p + 1), &size, &ok);
        p = skipsp(p);
        if (*p == ',')
            number(skipsp(p + 1), &al, &ok);
        if (a->pass == 1) {
            sym *l = sfind(a, name, a->file);
            int g = d[1] == 'c' && !(l && l->loc);
            sym *s = sadd(a, name, g ? -1 : a->file);
            if (s && (u64)size > s->csize)
                s->csize = (u64)size;
            if (s && al > s->calign)
                s->calign = (int)al;
        }
        return;
    }
    fail(a, "unknown directive", d);
}

/* ---------- one line ---------- */
static void label(as_t *a, const char *n) {
    char t[24];
    if (n[0] >= '0' && n[0] <= '9' && !n[1]) { /* numeric label */
        int d = n[0] - '0';
        a->nlab[d]++;
        numlab(a, d, 0, t);
        n = t;
    }
    int f = a->file;
    sym *g = sfind(a, n, -1);
    if (g && g->globl == a->file + 1) /* declared .globl in this file */
        f = -1;
    if (a->pass == 1) {
        sym *s = sadd(a, n, f);
        if (!s) {
            fail(a, "out of memory", 0);
            return;
        }
        if (s->sec >= 0 && s->sec < NSEC) {
            fail(a, "symbol defined twice:", n);
            return;
        }
        s->sec = a->sec;
        s->val = pos(a);
    } else {
        sym *s = sfind(a, n, f);
        if (s && s->val != pos(a) && !a->err[0])
            fail(a, "internal: label moved", n);
    }
}
static void line(as_t *a, char *l) {
    /* comments */
    int q = 0;
    for (char *p = l; *p; p++) {
        if (*p == '"' && (p == l || p[-1] != '\\'))
            q = !q;
        if (!q && *p == '#') {
            *p = 0;
            break;
        }
    }
    char *p = (char *)skipsp(l);
    while (*p) {
        /* labels */
        char *s = p;
        while (is_id(*s))
            s++;
        if (s > p && *s == ':') {
            *s = 0;
            label(a, p);
            p = (char *)skipsp(s + 1);
            continue;
        }
        break;
    }
    if (!*p)
        return;
    char mn[32];
    int k = 0;
    while (*p && !is_sp(*p) && k < 31)
        mn[k++] = *p++;
    mn[k] = 0;
    p = (char *)skipsp(p);
    if (mn[0] == '.') {
        directive(a, mn, p);
        return;
    }
    /* "rep stosb", "lock cmpxchg ..." */
    if (!strcmp(mn, "rep") || !strcmp(mn, "lock") || !strcmp(mn, "repne") || !strcmp(mn, "repe") ||
        !strcmp(mn, "repz") || !strcmp(mn, "repnz")) {
        instr(a, mn, 0, 0);
        if (*p)
            line(a, p);
        return;
    }
    opnd o[3];
    int n = 0;
    while (*p && n < 3) {
        p = (char *)parse_opnd(a, p, &o[n++]);
        p = (char *)skipsp(p);
        if (*p == ',')
            p++;
        else
            break;
    }
    if (*skipsp(p)) {
        fail(a, "unexpected text", p);
        return;
    }
    instr(a, mn, o, n);
}
static void run_file(as_t *a, const vas_file *f) {
    const char *t = f->text;
    a->fname = f->name;
    a->line = 0;
    a->sec = S_TEXT;
    memset(a->nlab, 0, sizeof a->nlab);
    static char l[4096];
    while (*t && !a->err[0]) {
        a->line++;
        size_t start_text = a->s[a->sec].n;
        int start_sec = a->sec;
        int n = 0;
        while (*t && *t != '\n') {
            if (n < (int)sizeof l - 1)
                l[n++] = *t;
            t++;
        }
        if (*t)
            t++;
        l[n] = 0;
        /* several statements in one line, split at ; outside of strings */
        char *s = l;
        int q = 0;
        for (char *p = l;; p++) {
            if (*p == '"' && (p == l || p[-1] != '\\'))
                q = !q;
            if (*p == 0 || (*p == ';' && !q)) {
                int end = !*p;
                *p = 0;
                line(a, s);
                if (end)
                    break;
                s = p + 1;
            }
        }
        if (a->listing && a->pass == 2 && start_sec == a->sec && a->sec != S_BSS) {
            buf *b = &a->s[a->sec];
            if (b->n > start_text) {
                char h[16];
                for (const char *p = f->name; *p; p++)
                    bput(&a->list, p, 1);
                int ln = a->line, dn = 0;
                char d[12];
                do
                    d[dn++] = (char)('0' + ln % 10);
                while ((ln /= 10) && dn < 11);
                bput(&a->list, ":", 1);
                while (dn)
                    bput(&a->list, &d[--dn], 1);
                bput(&a->list, ":", 1);
                for (size_t i = start_text; i < b->n; i++) {
                    static const char hx[] = "0123456789abcdef";
                    h[0] = ' ';
                    h[1] = hx[b->p[i] >> 4];
                    h[2] = hx[b->p[i] & 15];
                    bput(&a->list, h, 3);
                }
                bput(&a->list, "\n", 1);
            }
        }
    }
}
/* .globl names: a pass before the others, so labels know they are global */
static void scan_globl(as_t *a, const vas_file *f, int fi) {
    const char *t = f->text;
    while (*t) {
        const char *s = t;
        while (*t && *t != '\n')
            t++;
        const char *p = skipsp(s);
        if (!strncmp(p, ".local", 6) && is_sp(p[6])) {
            p = skipsp(p + 6);
            char n[256];
            int k = 0;
            while (is_id(*p) && k < 255)
                n[k++] = *p++;
            n[k] = 0;
            sym *l = sadd(a, n, fi);
            if (l)
                l->loc = 1;
        }
        if ((!strncmp(p, ".globl", 6) && is_sp(p[6])) || (!strncmp(p, ".global", 7) && is_sp(p[7]))) {
            p = skipsp(p + (p[6] == 'l' ? 7 : 6));
            char n[256];
            int k = 0;
            while (is_id(*p) && k < 255)
                n[k++] = *p++;
            n[k] = 0;
            sym *g = sadd(a, n, -1);
            if (g && !g->globl)
                g->globl = fi + 1;
        }
        if (*t)
            t++;
    }
}

/* ---------- ELF ---------- */
static void w16(u8 *p, u64 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}
static void w32(u8 *p, u64 v) {
    for (int i = 0; i < 4; i++)
        p[i] = (u8)(v >> (8 * i));
}
static void w64(u8 *p, u64 v) {
    for (int i = 0; i < 8; i++)
        p[i] = (u8)(v >> (8 * i));
}

int vas_build(const vas_file *files, int nf, const vas_opts *op, vas_result *r) {
    memset(r, 0, sizeof *r);
    as_t *a = calloc(1, sizeof *a);
    if (!a) {
        strcpy(r->err, "out of memory");
        return -1;
    }
    a->err = r->err;
    a->listing = op->listing;
    for (int i = 0; i < nf; i++)
        scan_globl(a, &files[i], i);
    for (a->pass = 1; a->pass <= 2 && !a->err[0]; a->pass++) {
        for (int s = 0; s < NSEC; s++)
            a->s[s].n = 0;
        a->bss = 0;
        for (int i = 0; i < nf && !a->err[0]; i++) {
            a->file = i;
            run_file(a, &files[i]);
        }
        if (a->pass == 1 && !a->err[0]) {
            /* common symbols that nobody defined go to .bss */
            for (int h = 0; h < HSZ; h++)
                for (sym *s = a->h[h]; s; s = s->next)
                    if (s->csize && s->sec < 0) {
                        u64 al = (u64)(s->calign > 0 ? s->calign : 1);
                        a->bss = (a->bss + al - 1) / al * al;
                        s->sec = S_BSS;
                        s->val = a->bss;
                        a->bss += s->csize;
                    }
            r->bss_size = a->bss;
            u64 t = a->s[S_TEXT].n, d = a->s[S_DATA].n;
            a->base[S_TEXT] = op->base;
            a->base[S_DATA] = (op->base + t + 4095) & ~4095ull;
            a->base[S_BSS] = (a->base[S_DATA] + d + 4095) & ~4095ull; /* up to .align 4096 */
            /* the global copy of a symbol is the one the files mean: link the file's .globl names */
        }
    }
    if (a->err[0]) {
        r->list = (char *)a->list.p;
        r->list_len = a->list.n;
        return -1;
    }
    r->text_size = a->s[S_TEXT].n;
    r->data_size = a->s[S_DATA].n;
    if (op->listing) {
        bput(&a->list, "", 1);
        r->list = (char *)a->list.p;
        r->list_len = a->list.n - 1;
        return 0;
    }
    expr en = {0, 0, 0};
    a->file = -1;
    en.s = sfind(a, op->entry ? op->entry : "_start", -1);
    if (!en.s || en.s->sec != S_TEXT) {
        strcpy(r->err, "no entry point (_start)");
        return -1;
    }
    u64 entry = eval(a, &en);
    /* the file: header, 2 program headers, text at 0x1000, data at its page offset */
    u64 toff = 4096, tlen = a->s[S_TEXT].n;
    u64 doff = (toff + tlen + 4095) & ~4095ull, dlen = a->s[S_DATA].n;
    u64 msz = (a->base[S_BSS] - a->base[S_DATA]) + r->bss_size; /* with the common symbols of pass 1 */
    size_t total = (size_t)(doff + dlen);
    u8 *f = calloc(1, total);
    if (!f) {
        strcpy(r->err, "out of memory");
        return -1;
    }
    memcpy(f, "\x7f" "ELF\x02\x01\x01", 7);
    w16(f + 16, 2);
    w16(f + 18, 62);
    w32(f + 20, 1);
    w64(f + 24, entry);
    w64(f + 32, 64);
    w16(f + 52, 64);
    w16(f + 54, 56);
    w16(f + 56, 2);
    u8 *ph = f + 64;
    w32(ph, 1);
    w32(ph + 4, 5); /* R X */
    w64(ph + 8, toff);
    w64(ph + 16, a->base[S_TEXT]);
    w64(ph + 24, a->base[S_TEXT]);
    w64(ph + 32, tlen);
    w64(ph + 40, tlen ? tlen : 1);
    w64(ph + 48, 4096);
    ph += 56;
    w32(ph, 1);
    w32(ph + 4, 6); /* R W */
    w64(ph + 8, doff);
    w64(ph + 16, a->base[S_DATA]);
    w64(ph + 24, a->base[S_DATA]);
    w64(ph + 32, dlen);
    w64(ph + 40, msz ? msz : 1);
    w64(ph + 48, 4096);
    memcpy(f + toff, a->s[S_TEXT].p, tlen);
    if (dlen)
        memcpy(f + doff, a->s[S_DATA].p, dlen);
    r->elf = f;
    r->elf_len = total;
    return 0;
}
