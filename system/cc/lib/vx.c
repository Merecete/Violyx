/* The system layer for Violyx: memory, files and time through the system calls, and a console in
 * the program's window for standard output and input. A program that only draws with the vx_
 * functions never sees the console. */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "violyx.h"
#include "sys.h"

/* ---------- console ---------- */
#define CL 1000 /* lines kept */
#define CC 200  /* characters per line */
static unsigned char *ch, *at; /* CL x CC */
static int top, cur, col, used, scroll; /* top: oldest line in the ring, cur: line written now */
static long last;
static int attr_out = VX_ATTR(VC_BLACK, VC_LAVENDER), attr_err = VX_ATTR(VC_BLACK, VC_HOT);
static int con_open(void) {
    if (ch)
        return 1;
    ch = malloc(CL * CC);
    at = malloc(CL * CC);
    if (!ch || !at)
        return 0;
    memset(ch, ' ', CL * CC);
    memset(at, attr_out, CL * CC);
    used = 1;
    return 1;
}
static int lines(void) { /* lines in use */
    return (cur - top + CL) % CL + 1;
}
static void newline(void) {
    cur = (cur + 1) % CL;
    if (cur == top)
        top = (top + 1) % CL;
    memset(ch + cur * CC, ' ', CC);
    memset(at + cur * CC, attr_out, CC);
    col = 0;
}
static void draw(int cursor) {
    int w = vx_cols(), h = vx_rows();
    vx_clear(VX_ATTR(VC_BLACK, VC_LAVENDER));
    int n = lines(), first = n - h - scroll;
    if (first < 0)
        first = 0;
    for (int y = 0; y < h && first + y < n; y++) {
        int l = (top + first + y) % CL;
        for (int x = 0; x < w && x < CC; x++)
            if (ch[l * CC + x] != ' ' || at[l * CC + x] != attr_out)
                vx_put(x, y, (char)ch[l * CC + x], at[l * CC + x]);
        if (cursor && !scroll && l == cur && col < w)
            vx_put(col, y, '_', VX_ATTR(VC_BLACK, VC_GLOW));
    }
    if (scroll) {
        const char *m = vx_english() ? " PgDn: further down " : " Bild runter: weiter ";
        vx_text(w - (int)strlen(m), h - 1, m, VX_ATTR(VC_SEL, VC_GLOW));
    }
    vx_show();
    last = vx_ms();
}
static void put_c(unsigned char c, int a) {
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\r') {
        col = 0;
        return;
    }
    if (c == '\b') {
        if (col)
            col--;
        return;
    }
    if (c == '\t') {
        do
            put_c(' ', a);
        while (col % 8);
        return;
    }
    if (col >= CC || col >= vx_cols())
        newline();
    ch[cur * CC + col] = c;
    at[cur * CC + col] = (unsigned char)a;
    col++;
}
long __os_write(int fd, const char *b, long n) {
    if (!con_open())
        return -1;
    for (long i = 0; i < n; i++)
        put_c((unsigned char)b[i], fd == 2 ? attr_err : attr_out);
    scroll = 0;
    if (vx_ms() - last >= 40) /* not more often than 25 times a second */
        draw(0);
    return n;
}
/* Scrolling back while the program waits */
static int scroll_key(int k) {
    int h = vx_rows(), max = lines() - h;
    if (max < 0)
        max = 0;
    if (k == VK_PGUP)
        scroll += h - 1;
    else if (k == VK_PGDN)
        scroll -= h - 1;
    else if (k == VK_HOME)
        scroll = max;
    else if (k == VK_END)
        scroll = 0;
    else
        return 0;
    if (scroll > max)
        scroll = max;
    if (scroll < 0)
        scroll = 0;
    return 1;
}
static char inl[512];
static int inn, inp, ineof;
int __os_getchar(void) {
    if (inp < inn)
        return (unsigned char)inl[inp++];
    if (ineof || !con_open())
        return -1;
    inn = inp = 0;
    int start = col;
    for (;;) {
        draw(1);
        vx_wait(-1);
        int k;
        while ((k = vx_key())) {
            if (scroll_key(k))
                continue;
            scroll = 0;
            if (k == VK_ENTER) {
                inl[inn++] = '\n';
                put_c('\n', attr_out);
                draw(0);
                return (unsigned char)inl[inp++];
            }
            if (k == VK_BS) {
                if (inn) {
                    inn--;
                    if (col > start)
                        col--;
                    ch[cur * CC + col] = ' ';
                }
            } else if (k == 4 && !inn) { /* Ctrl+D: end of input */
                ineof = 1;
                return -1;
            } else if (k >= 32 && k < 256 && inn < (int)sizeof inl - 2) {
                inl[inn++] = (char)k;
                put_c((unsigned char)k, VX_ATTR(VC_BLACK, VC_GLOW));
            }
        }
    }
}
void __os_fini(int code) {
    if (!used)
        return;
    const char *m = vx_english() ? "[End. Esc closes the window]" : "[Ende. Esc schließt das Fenster]";
    if (col)
        put_c('\n', attr_out);
    if (code)
        for (const char *p = vx_english() ? "Exit code " : "Rückgabewert "; *p; p++)
            put_c((unsigned char)*p, attr_err);
    if (code) {
        char d[12];
        int k = 0, v = code < 0 ? -code : code;
        do
            d[k++] = (char)('0' + v % 10);
        while ((v /= 10) && k < 11);
        if (code < 0)
            put_c('-', attr_err);
        while (k)
            put_c((unsigned char)d[--k], attr_err);
        put_c('\n', attr_out);
    }
    for (const char *p = m; *p; p++)
        put_c((unsigned char)*p, VX_ATTR(VC_BLACK, VC_DIM));
    scroll = 0;
    for (;;) {
        draw(0);
        vx_wait(-1);
        int k;
        while ((k = vx_key()))
            if (!scroll_key(k)) {
                if (k == VK_ESC || k == VK_ENTER)
                    return;
            } else
                break;
    }
}

/* ---------- the rest of the system ---------- */
void __os_exit(int c) {
    (void)c;
    vx_exit();
}
void *__os_sbrk(long n) {
    long p = vx_sys(8, n, 0, 0);
    return p == -1 ? 0 : (void *)p;
}
long __os_fsize(const char *p) {
    vx_stat_t s;
    if (vx_stat(p, &s) < 0 || s.dir)
        return -1;
    return (long)s.size;
}
long __os_fread(const char *p, void *b, long n) {
    long k = 0;
    while (k < n) { /* in parts of 512 KB */
        long r = vx_read_at(p, (char *)b + k, (unsigned long)k, (size_t)(n - k > 524288 ? 524288 : n - k));
        if (r <= 0)
            break;
        k += r;
    }
    return k;
}
long __os_fwrite(const char *p, const void *b, long n) {
    return vx_write(p, b, (size_t)n) < 0 ? -1 : 0;
}
int __os_remove(const char *p) {
    return vx_remove(p) < 0 ? -1 : 0;
}
int __os_rename(const char *a, const char *b) {
    return vx_rename(a, b) < 0 ? -1 : 0;
}
long __os_time(void) {
    return (long)vx_time();
}
long __os_ms(void) {
    return vx_ms();
}
void __os_init(void) {
}
/* argc and argv from the text the program was started with (Commands: "run prog.vpk words") */
static char argbuf[512];
static char *argvv[32];
int __os_args(char ***argv) {
    int n = 0;
    argvv[n++] = "program";
    unsigned long q[2] = {(unsigned long)argbuf, sizeof argbuf};
    long r = vx_sys(30, 1, (long)q, 0);
    if (r > 0) {
        argbuf[sizeof argbuf - 1] = 0;
        char *p = argbuf;
        while (*p && n < 31) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            if (*p == '"') { /* "words with spaces" */
                argvv[n++] = ++p;
                while (*p && *p != '"')
                    p++;
            } else {
                argvv[n++] = p;
                while (*p && *p != ' ')
                    p++;
            }
            if (*p)
                *p++ = 0;
        }
    }
    argvv[n] = 0;
    *argv = argvv;
    return n;
}
