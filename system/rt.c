/* Runtime of the system programs in ring 3: memory, the window with its event loop, drawing,
 * dialogs, files, system services and a blocking network layer over the non-blocking TCP and DNS
 * calls of the kernel. Every call goes through int 48 and the checks of the kernel. */
#include "os.h"
__asm__(
    ".text\n.globl _start\n_start:\n  and $-16,%rsp\n  call main\n  xor %edi,%edi\n  xor %eax,%eax\n"
    "  int $48\n1: jmp 1b\n"
    "vx_sys:\n .globl vx_sys\n  mov %rdi,%rax\n  mov %rsi,%rdi\n  mov %rdx,%rsi\n  mov %rcx,%rdx\n"
    "  int $48\n  ret\n");
enum { /* system calls */
    S_EXIT = 0,
    S_WAIT = 2,
    S_KEY = 3,
    S_SIZE = 4,
    S_TITLE = 5,
    S_SBRK = 8,
    S_READ = 11,
    S_WRITE = 12,
    S_STAT = 13,
    S_MKDIR = 14,
    S_REMOVE = 15,
    S_RENAME = 16,
    S_PRESENT = 18,
    S_CTL = 23,
    S_IMAGE = 24,
    S_MENU = 25,
    S_READAT = 26,
    S_LIST = 27,
    S_DLG = 28,
    S_NET = 29,
    S_SVC = 30
};
#define VE_AGAIN (-13)
static __attribute__((noreturn)) void quit(void) {
    vx_sys(S_EXIT, 0, 0, 0);
    for (;;)
        ;
}

/* ---------- Memory: first fit, grows with sbrk, neighbours merge when freed ---------- */
typedef struct blk {
    u64 size, free;
    struct blk *next;
    u64 pad;
} blk;
static blk *head, *tail;
static int adj(blk *a, blk *b) {
    return (u8 *)(a + 1) + a->size == (u8 *)b;
}
static void merge_all(void) {
    for (blk *b = head; b && b->next;)
        if (b->free && b->next->free && adj(b, b->next)) {
            if (tail == b->next)
                tail = b;
            b->size += b->next->size + sizeof(blk);
            b->next = b->next->next;
        } else
            b = b->next;
}
static int grow(size_t n) {
    size_t g = n + 2 * sizeof(blk);
    if (g < (2u << 20))
        g = 2u << 20;
    g = (g + 4095) & ~(size_t)4095;
    long p = vx_sys(S_SBRK, (long)g, 0, 0);
    if (p == -1)
        return 0;
    blk *b = (blk *)p;
    b->size = g - sizeof(blk);
    b->free = 1;
    b->next = 0;
    if (tail)
        tail->next = b;
    else
        head = b;
    tail = b;
    merge_all();
    return 1;
}
void *kmalloc(size_t n) {
    if (n > (240u << 20))
        return 0;
    n = (n + 15) & ~(size_t)15;
    for (int pass = 0; pass < 3; pass++) {
        for (blk *b = head; b; b = b->next)
            if (b->free && b->size >= n) {
                if (b->size >= n + sizeof(blk) + 64) {
                    blk *r = (blk *)((u8 *)(b + 1) + n);
                    r->size = b->size - n - sizeof(blk);
                    r->free = 1;
                    r->next = b->next;
                    b->next = r;
                    b->size = n;
                    if (tail == b)
                        tail = r;
                }
                b->free = 0;
                return b + 1;
            }
        if (pass == 0)
            merge_all();
        else if (pass == 1 && !grow(n))
            return 0;
    }
    return 0;
}
void kfree(void *p) {
    if (!p)
        return;
    blk *b = (blk *)p - 1;
    b->free = 1;
    while (b->next && b->next->free && adj(b, b->next)) { /* merge forward at once */
        if (tail == b->next)
            tail = b;
        b->size += b->next->size + sizeof(blk);
        b->next = b->next->next;
    }
}
void *k_calloc(size_t n, size_t s) {
    if (n && s > (1ull << 31) / n)
        return 0;
    void *p = kmalloc(n * s);
    if (p)
        memset(p, 0, n * s);
    return p;
}

/* ---------- Time ---------- */
u32 rtc_unix(const rtc_t *t) {
    int y = t->year, m = t->mon;
    y -= m <= 2;
    int era = y / 400, yoe = y - era * 400,
        doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->day - 1,
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (u32)(era * 146097 + doe - 719468) * 86400u + t->h * 3600u + t->m * 60u + t->s;
}
void rtc_from_unix(u32 s, rtc_t *t) {
    u32 d = s / 86400, r = s % 86400;
    t->h = r / 3600;
    t->m = (r / 60) % 60;
    t->s = r % 60;
    u32 z = d + 719468, era = z / 146097, doe = z - era * 146097,
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365,
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153,
        m = mp < 10 ? mp + 3 : mp - 9;
    t->day = doy - (153 * mp + 2) / 5 + 1;
    t->mon = m;
    t->year = yoe + era * 400 + (m <= 2);
}
void rtc_read(rtc_t *t) {
    rtc_from_unix(fs_now(), t);
}

/* ---------- Keys ---------- */
int key_poll(key_t *k) {
    long v = vx_sys(S_KEY, 0, 0, 0);
    if (v < 0)
        return 0;
    k->key = (u16)(v & 0xFFFF);
    k->mods = (u8)(v >> 16);
    return 1;
}
static void present(void);
key_t key_wait(void) {
    key_t k;
    present();
    while (!key_poll(&k))
        vx_sys(S_WAIT, -1, 0, 0);
    return k;
}
int ui_wait(key_t *k) {
    *k = key_wait();
    return 1;
}

/* ---------- Cells and drawing ---------- */
#define CMAX 240
#define RMAX 100
static u32 cells[CMAX * RMAX];
static int CW = 1, CH = 1;
int ui_fixed, ui_quitting;
static win_t win;
static void dims(void) {
    long v = vx_sys(S_SIZE, 0, 0, 0);
    CW = (int)(v >> 16);
    CH = (int)(v & 0xFFFF);
    if (CW < 1)
        CW = 1;
    if (CH < 1)
        CH = 1;
    win.w = CW + 2;
    win.h = CH + 2;
}
int ui_cols(void) {
    return CW;
}
int ui_rows(void) {
    return CH;
}
/* UTF-8 of the German letters to the code page of the font */
static int u8dec(const char **s) {
    const u8 *p = (const u8 *)*s;
    u8 c = *p++;
    int r = c;
    if (c == 0xC3 && *p) {
        u8 d = *p++;
        switch (d) {
            case 0xA4:
                r = 0x84;
                break;
            case 0xB6:
                r = 0x94;
                break;
            case 0xBC:
                r = 0x81;
                break;
            case 0x84:
                r = 0x8E;
                break;
            case 0x96:
                r = 0x99;
                break;
            case 0x9C:
                r = 0x9A;
                break;
            case 0x9F:
                r = 0xE1;
                break;
            default:
                r = '?';
        }
    } else if (c == 0xC2 && *p) {
        u8 d = *p++;
        r = d == 0xA7 ? 0x15 : d == 0xB0 ? 0xF8 : '?';
    }
    *s = (const char *)p;
    return r;
}
void put8(int x, int y, u8 c, u8 fg, u8 bg) {
    if (x >= 0 && y >= 0 && x < CW && y < CH)
        cells[y * CW + x] = (u32)c | (u32)fg << 8 | (u32)bg << 16 | (u32)(ui_fixed & 1) << 24;
}
void put(int x, int y, u8 c, u8 a) {
    put8(x, y, c, a & 15, a >> 4);
}
void text(int x, int y, u8 a, const char *s) {
    while (*s)
        put(x++, y, (u8)u8dec(&s), a);
}
void fill(int x, int y, int w, int h, u8 c, u8 a) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put(x + i, y + j, c, a);
}
int cw(win_t *w) {
    return CW;
}
int ch(win_t *w) {
    return CH;
}
void cput(win_t *w, int x, int y, u8 c, u8 a) {
    put(x, y, c, a);
}
void ctext(win_t *w, int x, int y, u8 a, const char *s) {
    text(x, y, a, s);
}
void cfill(win_t *w, u8 a) {
    fill(0, 0, CW, CH, ' ', a);
}
void cput8(win_t *w, int x, int y, u8 c, u8 fg, u8 bg) {
    put8(x, y, c, fg, bg);
}
/* Pictures: a program may have many (the browser one per image), the kernel holds 31 at a time.
 * cputg loads a picture into a free or the longest unused slot when it is needed. */
static struct {
    const u8 *px;
    int w, h, slot, dirty;
} gi[256];
static struct {
    int id;
    u32 used;
} gs[32];
static u32 gframe;
static u8 gid;
u8 gfx_newid(void) {
    if (++gid == 0)
        gid = 1;
    gi[gid].px = 0;
    return gid;
}
int gfx_upload(u8 id, const u8 *px, int w, int h) {
    if (!id || (w & 3) || (h & 7) || w < 4 || h < 8)
        return VE_INVAL;
    gi[id].px = px;
    gi[id].w = w;
    gi[id].h = h;
    gi[id].dirty = 1;
    return 0;
}
static int gslot(u8 id) {
    if (!gi[id].px)
        return -1;
    int s = gi[id].slot;
    if (s > 0 && gs[s].id == id && !gi[id].dirty) {
        gs[s].used = gframe;
        return s;
    }
    if (!(s > 0 && gs[s].id == id)) {
        s = 1;
        for (int i = 1; i < 32; i++) {
            if (!gs[i].id) {
                s = i;
                break;
            }
            if (gs[i].used < gs[s].used)
                s = i;
        }
    }
    if (vx_sys(S_IMAGE, (long)gi[id].px, gi[id].w, (long)gi[id].h | (long)s << 16))
        return -1;
    gs[s].id = id;
    gs[s].used = gframe;
    gi[id].slot = s;
    gi[id].dirty = 0;
    return s;
}
void cputg(win_t *w, int x, int y, u8 id, const u8 *px, int stride, int ix, int iy) {
    if (x < 0 || y < 0 || x >= CW || y >= CH)
        return;
    int s = gslot(id);
    if (s < 0) {
        put8(x, y, ' ', 0, 0);
        return;
    }
    cells[y * CW + x] = 0x80000000u | (u32)(ui_fixed & 1) | (u32)(ix & 255) << 8 |
                        (u32)(iy & 127) << 16 | (u32)s << 23;
}
u32 ui_rgb(int i) { /* the colour scheme can change, so the copy is renewed now and then */
    static u32 c[16];
    static long at = -1000;
    if (i < 0 || i > 15)
        return (u32)vx_sys(S_CTL, 18, i, 0);
    long now = vx_sys(7, 0, 0, 0);
    if (now - at > 300) {
        for (int k = 0; k < 16; k++)
            c[k] = (u32)vx_sys(S_CTL, 18, k, 0);
        at = now;
    }
    return c[i];
}

/* ---------- The window ---------- */
static char stitle[40], sfoot[64];
static int scur = -2, scx, scy, sfixed = -1;
static void present(void) {
    vx_sys(S_PRESENT, (long)cells, CW, CH);
    if (strcmp(stitle, win.title)) {
        strlcpy_(stitle, win.title, 40);
        vx_sys(S_TITLE, (long)stitle, 0, 0);
    }
    if (strcmp(sfoot, win.foot)) {
        strlcpy_(sfoot, win.foot, 64);
        vx_sys(S_CTL, 9, sfoot[0] ? (long)sfoot : 0, 0);
    }
    int c = win.cur ? 1 : 0;
    if (c != scur || (c && (win.cx != scx || win.cy != scy))) {
        scur = c;
        scx = win.cx;
        scy = win.cy;
        vx_sys(S_CTL, 10, c ? win.cx : -1, win.cy);
    }
}
static void redraw(void) {
    dims();
    if (win.fixed != sfixed) {
        sfixed = win.fixed;
        vx_sys(S_CTL, 16, sfixed, 0);
    }
    int of = ui_fixed;
    ui_fixed = win.fixed;
    fill(0, 0, CW, CH, ' ', AT(C_PANEL, C_LAV));
    ui_fixed = of;
    win.cur = 0;
    gframe++;
    if (win.app && win.app->draw)
        win.app->draw(&win);
    present();
}
void ui_render(void) {
    present();
    vx_sys(S_WAIT, 0, 0, 0); /* let the desktop draw it */
}
void ui_show(void) {
    ui_render();
}
win_t *win_open(const char *t, int x, int y, int w, int h, const app_t *a, void *st) {
    dims();
    win.app = a;
    win.st = st;
    strlcpy_(win.title, t, 40);
    if (w - 2 != CW || h - 2 != CH) {
        vx_sys(S_CTL, 12, w - 2, h - 2);
        dims();
    }
    return &win;
}
void win_close(win_t *w) {
    if (w->app && w->app->can_close && !w->app->can_close(w))
        return;
    if (w->app && w->app->destroy)
        w->app->destroy(w);
    quit();
}
void win_raise(win_t *w) {
    vx_sys(S_CTL, 15, 0, 0);
}
void win_zoom(win_t *w) {
    w->zoomed = (int)vx_sys(S_CTL, 13, 1, 0);
    dims();
}
void win_title(win_t *w, const char *t) {
    strlcpy_(w->title, t, 40);
}
void win_menu(win_t *w, const char *name, const char *items) {
    if (!w)
        return;
    vx_sys(S_CTL, 19, (long)name, 0);
    vx_sys(S_MENU, (long)items, 0, 0);
}
void win_full(win_t *w, int on) {
    vx_sys(S_CTL, 11, on, 0);
    dims();
}
int win_front(void) {
    return (int)vx_sys(S_CTL, 14, 0, 0);
}
void ui_screen(int *w, int *h) {
    long v = vx_sys(S_CTL, 20, 0, 0);
    *w = (int)(v >> 16);
    *h = (int)(v & 0xFFFF);
}
void ui_background(int on) {
    vx_sys(S_CTL, on ? 2 : 17, 0, 0);
}
__attribute__((weak)) void prog_reopen(void) {
}
/* Events: keys, picture steps (25 a second while in front) and the once a second tick */
static void run(void) {
    long nf = 0;
    u32 sec = fs_now();
    int lc = -1, lr = -1, dirty = 1;
    for (;;) {
        dims();
        if (CW != lc || CH != lr) {
            lc = CW;
            lr = CH;
            dirty = 1;
        }
        if (dirty) {
            redraw();
            dirty = 0;
        }
        const app_t *a = win.app;
        long now = vx_sys(7, 0, 0, 0), to = -1;
        int front = a->frame ? win_front() : 0;
        if (a->frame)
            to = front ? (nf > now ? nf - now : 0) : 200;
        if (a->tick) {
            long t = 1000 - now % 1000;
            if (to < 0 || t < to)
                to = t;
        }
        vx_sys(S_WAIT, to, 0, 0);
        key_t k;
        while (key_poll(&k)) {
            if (k.key == K_ESC) {
                ui_quitting = (k.mods & 8) != 0; /* the computer is being switched off */
                win_close(&win);                 /* returns when the program stays open */
                ui_quitting = 0;
            } else if (k.key == K_OPEN)
                prog_reopen();
            else if (a->key)
                a->key(&win, k);
            dirty = 1;
        }
        now = vx_sys(7, 0, 0, 0);
        if (a->frame && now >= nf && win_front()) {
            nf = now + 40;
            a->frame(&win);
            dirty = 1;
        }
        if (a->tick && fs_now() != sec) {
            sec = fs_now();
            a->tick(&win);
            dirty = 1;
        }
    }
}
int main(void) {
    vx_sys(S_CTL, 3, 2, 0); /* Esc and closing come to the program */
    dims();
    prog_main();
    if (!win.app)
        quit();
    run();
    return 0;
}
void panic(const char *m) {
    dlg_msg(TR("Fehler", "Error"), m);
    quit();
}

/* ---------- Dialogs ---------- */
int dlg_fresh;
static long dlg(int kind, const char *t, const char *m, char *b, int max) {
    present();
    u64 q[5] = {(u64)t, (u64)m, (u64)b, (u64)max, (u64)dlg_fresh};
    dlg_fresh = 0;
    return vx_sys(S_DLG, kind, (long)q, 0);
}
void dlg_msg(const char *t, const char *s) {
    dlg(0, t, s, 0, 0);
}
int dlg_confirm(const char *t, const char *s) {
    return dlg(1, t, s, 0, 0) > 0;
}
int dlg_input(const char *t, const char *p, char *b, int max) {
    return dlg(2, t, p, b, max) > 0;
}
int dlg_pick(const char *t, char *o, int max) {
    return dlg(3, t, 0, o, max) > 0;
}
int dlg_savechoice(void) {
    long r = dlg(4, 0, 0, 0, 0);
    return r < 0 ? 0 : (int)r;
}
/* A box with a double frame inside the program's own window */
void dlg_box(const char *t, int w, int h, int *x, int *y) {
    *x = (CW - w) / 2;
    *y = (CH - h) / 2;
    u8 a = AT(C_RAISE, C_NEON);
    fill(*x, *y, w, h, ' ', AT(C_RAISE, C_LAV));
    for (int i = 1; i < w - 1; i++) {
        put(*x + i, *y, 0xCD, a);
        put(*x + i, *y + h - 1, 0xCD, a);
    }
    for (int i = 1; i < h - 1; i++) {
        put(*x, *y + i, 0xBA, a);
        put(*x + w - 1, *y + i, 0xBA, a);
    }
    put(*x, *y, 0xC9, a);
    put(*x + w - 1, *y, 0xBB, a);
    put(*x, *y + h - 1, 0xC8, a);
    put(*x + w - 1, *y + h - 1, 0xBC, a);
    char b[44];
    ksnprintf(b, 44, " %s ", t);
    text(*x + (w - (int)strlen(b)) / 2, *y, AT(C_RAISE, C_GLOW), b);
}

/* ---------- Files ---------- */
int fsx_stat(const char *p, vfs_ent *e) {
    u64 s[2];
    long r = vx_sys(S_STAT, (long)p, (long)s, 0);
    if (r)
        return (int)r;
    if (e) {
        memset(e, 0, sizeof *e);
        e->size = s[0];
        e->dir = (int)s[1];
        const char *b = strrchr(p, '/');
        strlcpy_(e->name, b ? b + 1 : p, sizeof e->name);
    }
    return 0;
}
int64_t fsx_read(const char *p, void *b, u64 n) {
    return vx_sys(S_READ, (long)p, (long)b, (long)n);
}
int64_t fsx_read_at(const char *p, void *b, u64 off, u64 n) {
    u64 got = 0;
    while (got < n) { /* the kernel hands out at most 512 KB per call */
        u64 q[3] = {(u64)b + got, off + got, n - got};
        long r = vx_sys(S_READAT, (long)p, (long)q, 0);
        if (r < 0)
            return got ? (int64_t)got : r;
        if (!r)
            break;
        got += (u64)r;
    }
    return (int64_t)got;
}
int fsx_write(const char *p, const void *b, u64 n, u32 now) {
    return (int)vx_sys(S_WRITE, (long)p, (long)b, (long)n);
}
int fsx_mkdir(const char *p, u32 now) {
    return (int)vx_sys(S_MKDIR, (long)p, 0, 0);
}
int fsx_remove(const char *p) {
    return (int)vx_sys(S_REMOVE, (long)p, 0, 0);
}
int fsx_rename(const char *a, const char *b) {
    return (int)vx_sys(S_RENAME, (long)a, (long)b, 0);
}
#define LBUF (1u << 20)
int fsx_list(const char *p, int (*cb)(const vfs_ent *, void *), void *arg) {
    u8 *b = kmalloc(LBUF);
    if (!b)
        return VE_NOSPC;
    long n = vx_sys(S_LIST, (long)p, (long)b, LBUF);
    if (n < 0) {
        kfree(b);
        return (int)n;
    }
    static vfs_ent e;
    for (long o = 0; o + 10 <= n;) {
        memset(&e, 0, sizeof e);
        memcpy(&e.size, b + o, 8);
        e.dir = b[o + 8];
        strlcpy_(e.name, (char *)b + o + 9, sizeof e.name);
        o += 10 + (long)strlen((char *)b + o + 9);
        if (cb(&e, arg))
            break;
    }
    kfree(b);
    return 0;
}
typedef struct {
    fs_item *it;
    int n, cap;
} loadc;
static int loadcb(const vfs_ent *e, void *x) {
    loadc *l = x;
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        fs_item *t = kmalloc((size_t)nc * sizeof *t);
        if (!t)
            return 1;
        if (l->it)
            memcpy(t, l->it, (size_t)l->n * sizeof *t);
        kfree(l->it);
        l->it = t;
        l->cap = nc;
    }
    fs_item *i = &l->it[l->n++];
    strlcpy_(i->name, e->name, 251);
    i->dir = e->dir;
    i->size = e->size;
    return 0;
}
static int cmpi(const fs_item *a, const fs_item *b) {
    if (a->dir != b->dir)
        return b->dir - a->dir;
    return strcmp(a->name, b->name);
}
int fs_load(const char *p, fs_item **o) {
    loadc l = {0, 0, 0};
    int r = fsx_list(p, loadcb, &l);
    *o = l.it;
    if (r)
        return r;
    for (int i = 1; i < l.n; i++) { /* insertion sort: folders first, then by name */
        fs_item t = l.it[i];
        int j = i - 1;
        while (j >= 0 && cmpi(&l.it[j], &t) > 0) {
            l.it[j + 1] = l.it[j];
            j--;
        }
        l.it[j + 1] = t;
    }
    return l.n;
}
static long svc(long op, long b, long c) {
    return vx_sys(S_SVC, op, b, c);
}
static const char *stext(int which) {
    static char t[4][VFS_PATH];
    u64 q[2] = {(u64)t[which & 3], VFS_PATH};
    if (svc(14, which, (long)q) < 0)
        t[which & 3][0] = 0;
    return t[which & 3];
}
int fs_ok(void) {
    return svc(13, 0, 0) > 0;
}
const char *fs_status(void) {
    return stext(1);
}
const char *fsx_home(void) {
    return stext(0);
}
u32 fsx_gen_now(void) {
    return (u32)svc(13, 1, 0);
}
int fsx_rmtree(const char *p) {
    return (int)svc(11, (long)p, 0);
}
int fsx_copy(const char *a, const char *b) {
    return (int)svc(10, (long)a, (long)b);
}
int fsx_quiet(const char *p) {
    return svc(13, 2, (long)p) > 0;
}
int fsx_first(char *o, int n) {
    const char *t = stext(2);
    strlcpy_(o, t, (size_t)n);
    return t[0] != 0;
}
void fsx_info(const char *d, char *o, int n) {
    u64 q[2] = {(u64)o, (u64)n};
    if (svc(12, (long)d, (long)q) < 0)
        o[0] = 0;
}
int fsx_eject(const char *d) {
    return (int)svc(8, (long)d, 0);
}
int fsx_protected(const char *p) {
    return svc(17, (long)p, 0) > 0;
}

/* ---------- System services ---------- */
int sys_arg(char *o, int n) {
    u64 q[2] = {(u64)o, (u64)n};
    long r = svc(1, (long)q, 0);
    if (r < 0)
        o[0] = 0;
    return o[0] != 0;
}
int sys_report(int kind, char *o, int n) {
    u64 q[2] = {(u64)o, (u64)n};
    return (int)svc(2, kind, (long)q);
}
int prog_open(const char *name, const char *arg) {
    return (int)svc(0, (long)name, (long)arg);
}
int prog_install(const char *p) {
    return (int)svc(4, (long)p, 0);
}
int prog_run(const char *n) {
    return (int)svc(5, (long)n, 0);
}
int prog_run_file(const char *p, const char *base) {
    return (int)svc(6, (long)p, 0);
}
int prog_uninstall(const char *n) {
    return (int)svc(7, (long)n, 0);
}
void drive_format(const char *n) {
    present();
    svc(9, (long)n, 0);
}
int wall_open_file(const char *p) {
    present();
    return (int)svc(16, (long)p, 0);
}
int wall_use(const u8 *c) {
    static u8 b[4004];
    memcpy(b, "VWP1", 4);
    memcpy(b + 4, c, 4000);
    return (int)svc(15, (long)b, 0);
}
void reboot(void) {
    svc(3, 0, 0);
}

/* ---------- Network: waiting happens here, so the rest of the system keeps running ---------- */
int net_aborted, net_bg;
static long net(long op, long b, long c) {
    return vx_sys(S_NET, op, b, c);
}
static int esc(void) {
    if (net_bg) {
        if (key_down(1)) {
            net_aborted = 1;
            return 1;
        }
        return 0;
    }
    key_t k;
    while (key_poll(&k))
        if (k.key == K_ESC) {
            net_aborted = 1;
            return 1;
        }
    return 0;
}
static int nwait(void) {
    vx_sys(S_WAIT, 10, 0, 0);
    return esc();
}
const char *net_status(void) {
    static char b[64];
    net(0, (long)b, 64);
    return b;
}
int net_ready(void) {
    char b[64];
    return net(0, (long)b, 64) == 1;
}
void net_info(char *o, int n, int line) {
    char b[80];
    b[0] = 0;
    net(9, line, (long)b);
    strlcpy_(o, b, (size_t)n);
}
int net_resolve(const char *name, u32 *ip, char *txt) {
    if (!net_ready())
        return -3;
    long r = net(1, (long)name, 0);
    if (r == -9)
        return -4; /* not allowed */
    if (r < 0)
        return -1;
    long end = vx_sys(7, 0, 0, 0) + 6000;
    while (!(r = net(2, 0, (long)ip))) {
        if (vx_sys(7, 0, 0, 0) > end)
            return -1;
        if (nwait())
            return -2;
    }
    if (r != 1)
        return -1;
    u8 *a = (u8 *)ip;
    ksnprintf(txt, 16, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
    return 0;
}
int net_ping(u32 ip, u16 seq, int *ms, int *ttl) {
    present();
    long r = net(8, ip, seq);
    if (r == VE_AGAIN)
        return -2;
    if (r < 0)
        return -1;
    if (r == 1)
        return 1;
    *ms = (int)(r & 0xFFFF);
    *ttl = (int)(r >> 16);
    return 0;
}
int tcp_open(u32 ip, u16 port) {
    if (net(3, ip, port) < 0)
        return -1;
    ui_background(1); /* the transfer goes on when another window is in front */
    long end = vx_sys(7, 0, 0, 0) + 15000, s;
    while (!(s = net(4, 0, 0))) {
        if (vx_sys(7, 0, 0, 0) > end)
            return -1;
        if (nwait())
            return -2;
    }
    return s == 1 ? 0 : -1;
}
int tcp_tx(const void *b, size_t n) {
    long end = vx_sys(7, 0, 0, 0) + 30000;
    for (;;) {
        long r = net(5, (long)b, (long)n);
        if (r)
            return (int)r;
        if (vx_sys(7, 0, 0, 0) > end || nwait())
            return -1;
    }
}
int tcp_tx_all(const void *b, int n) {
    int o = 0;
    while (o < n) {
        int r = tcp_tx((const u8 *)b + o, (size_t)(n - o));
        if (r < 0)
            return -1;
        o += r;
    }
    return o;
}
int tcp_rx(void *b, size_t n) {
    long end = vx_sys(7, 0, 0, 0) + 30000;
    for (;;) {
        long r = net(6, (long)b, (long)n);
        if (r != VE_AGAIN)
            return (int)r;
        if (vx_sys(7, 0, 0, 0) > end || nwait())
            return -1;
    }
}
void tcp_end(void) {
    net(7, 0, 0);
    ui_background(0);
}
