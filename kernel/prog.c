/* Installable programs: packages (.vpk), ELF loader, address spaces, system calls. Everything from
 * a package counts as hostile. The check needs no kernel and can be tested on the host. Programs
 * run in ring 3 in the address space slice PML4[200]. The kernel stays out of their reach, because
 * only this slice carries the user bit. Each program is a coroutine: it runs until it waits, its
 * time slice ends or it makes a mistake. A mistake ends only this program. */
#ifdef HOST
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "os.h"
#endif
#define UBASE   0x640000000000ull
#define ULOAD   (UBASE + 0x400000ull)
#define UHEAP   (UBASE + 0x40000000ull)
#define USTACK  (UBASE + 0x7FF00000ull)
#define USTKSZ  0x100000ull
#define UEND    (USTACK + USTKSZ)
#define MAXCODE (64ull << 20)
#define MAXHEAP (256ull << 20)
#define VPK_MAX (32ull << 20)
#define VPK_HDR 256
typedef struct __attribute__((packed)) {
    char magic[4];
    u32 elf;
    u16 cols, rows;
    char name[24], title[24], version[12], author[32], icon[3][7];
} vpk_h;
static int nulstr(const char *s, int n, int minlen) {
    int i = 0;
    for (; i < n && s[i]; i++)
        if ((u8)s[i] < 32 || s[i] == 127)
            return 0;
    return i < n && i >= minlen;
}
static int vpk_check(const u8 *b, u64 n) {
    if (n < VPK_HDR + 64 || n > VPK_MAX)
        return 0;
    const vpk_h *h = (const vpk_h *)b;
    if (memcmp(h->magic, "VPK1", 4))
        return 0;
    if ((u64)h->elf + VPK_HDR != n)
        return 0;
    if (h->cols < 20 || h->cols > 76 || h->rows < 5 || h->rows > 20)
        return 0;
    if (!nulstr(h->name, 24, 1) || !nulstr(h->title, 24, 1) || !nulstr(h->version, 12, 1) ||
        !nulstr(h->author, 32, 0))
        return 0;
    for (int i = 0; h->name[i]; i++) {
        char c = h->name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return 0;
    }
    for (int i = 0; i < 3; i++)
        if (!nulstr(h->icon[i], 7, 0))
            return 0;
    return 1;
}
typedef struct {
    u64 off, vaddr, filesz, memsz;
    u32 flags;
} seg_t;
static u64 r64(const u8 *p) {
    u64 v = 0;
    for (int i = 7; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}
static u32 r32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static u32 r16(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8;
}
/* Returns the entry point and segments, or 0 if the ELF is rejected */
static int elf_check(const u8 *b, u64 n, u64 *entry, seg_t *s, int *ns) {
    if (n < 64 ||
        memcmp(b,
               "\x7f"
               "ELF\x02\x01\x01",
               7) ||
        r16(b + 16) != 2 || r16(b + 18) != 62)
        return 0;
    u64 ph = r64(b + 32);
    u32 pes = r16(b + 54), pn = r16(b + 56);
    if (pes != 56 || !pn || pn > 16 || ph > n || (u64)pn * 56 > n - ph)
        return 0;
    u64 e = r64(b + 24);
    int c = 0, xok = 0;
    u64 last = 0, total = 0;
    for (u32 i = 0; i < pn; i++) {
        const u8 *p = b + ph + (u64)i * 56;
        if (r32(p) != 1)
            continue;
        u32 fl = r32(p + 4);
        u64 of = r64(p + 8), va = r64(p + 16), fs = r64(p + 32), ms = r64(p + 40);
        if (!ms)
            continue;
        if (c >= 8 || fs > ms || ms > MAXCODE || va < ULOAD || va >= UHEAP || va + ms > UHEAP ||
            (va & 4095) || of > n || fs > n - of || (fl & 3) == 3 || (fl & ~7u))
            return 0;
        if (va < last)
            return 0;
        last = (va + ms + 4095) & ~4095ull;
        total += (ms + 4095) & ~4095ull;
        if (total > MAXCODE)
            return 0;
        if ((fl & 1) && e >= va && e < va + ms)
            xok = 1;
        s[c].off = of;
        s[c].vaddr = va;
        s[c].filesz = fs;
        s[c].memsz = ms;
        s[c].flags = fl;
        c++;
    }
    if (!c || !xok)
        return 0;
    *entry = e;
    *ns = c;
    return 1;
}
#ifndef HOST
/* ---------- Pages ---------- */
#define CMAX   240
#define RMAX   100
#define PTE_P  1ull
#define PTE_W  2ull
#define PTE_U  4ull
#define PTE_NX (1ull << 63)
#define PA     0x000FFFFFFFFFF000ull
#define SLOT   200
static u64 pool_next, pool_end;
static void *pg_free_list;
static void *pg_alloc(void) {
    void *p;
    if (pg_free_list) {
        p = pg_free_list;
        pg_free_list = *(void **)p;
    } else {
        if (pool_next >= pool_end) {
            if (pool_end)
                return 0;
            u64 f = heap_free() / 2;
            if (f > (256ull << 20))
                f = 256ull << 20;
            if (f < (16ull << 20))
                return 0;
            u8 *m = kmalloc(f);
            if (!m)
                return 0;
            pool_next = ((u64)m + 4095) & ~4095ull;
            pool_end = ((u64)m + f) & ~4095ull;
        }
        p = (void *)pool_next;
        pool_next += 4096;
    }
    memset(p, 0, 4096);
    return p;
}
static void pg_put(void *p) {
    *(void **)p = pg_free_list;
    pg_free_list = p;
}
#define V(e) ((u64 *)(((e) & PA) + hhdm_off()))
static u64 *walk(u64 root, u64 va, int make) {
    u64 e = root, *t;
    int sh[3] = {30, 21, 12};
    t = V(e);
    for (int l = 0; l < 2; l++) {
        u64 *q = &t[(va >> sh[l]) & 511];
        if (!(*q & PTE_P)) {
            if (!make)
                return 0;
            void *n = pg_alloc();
            if (!n)
                return 0;
            *q = phys(n) | PTE_P | PTE_W | PTE_U;
        }
        t = V(*q);
    }
    return &t[(va >> 12) & 511];
}
static void *map_page(u64 root, u64 va, u64 fl) {
    u64 *e = walk(root, va, 1);
    if (!e)
        return 0;
    if (*e & PTE_P)
        return V(*e);
    void *p = pg_alloc();
    if (!p)
        return 0;
    *e = phys(p) | PTE_P | PTE_U | fl;
    return p;
}
static void as_free(u64 root) {
    u64 *a = V(root);
    for (int i = 0; i < 512; i++)
        if (a[i] & PTE_P) {
            u64 *b = V(a[i]);
            for (int j = 0; j < 512; j++)
                if (b[j] & PTE_P) {
                    u64 *c = V(b[j]);
                    for (int k = 0; k < 512; k++)
                        if (c[k] & PTE_P)
                            pg_put(V(c[k]));
                    pg_put(c);
                }
            pg_put(b);
        }
    pg_put(a);
}
/* ---------- Program state ---------- */
enum {
    P_RUN,
    P_WAIT,
    P_DONE,
    P_DEAD
};
#define NIMG   32
#define IMGMAX (24u << 20)
#define NGRANT 8
typedef struct {
    u64 root, brk, wake;
    regs r;
    u8 fxraw[528];
    u8 *fx;
    int state, kh, kt, dirty;
    u32 keys[32];
    u32 *cells;
    int cols, rows, sc, sr;
    char name[24], base[VFS_PATH], msg[70];
    win_t *w;
    int bg, esc, f1;
    struct {
        u8 *px;
        int w, h;
        u64 cap; /* size of the memory block, it can be larger than w * h */
        u8 gid;
    } im[NIMG]; /* pictures for pixel cells, slot 0 is the one of vx_image */
    u64 imbytes;
    int sys; /* built-in system program: whole file system, system services */
    int lim; /* built-in program that reads foreign data (web pages, pictures): only the folders
                of a normal program; L_NET internet without asking, L_INST may install */
    u64 hash;                     /* of the package: the internet permission belongs to it */
    int cl_n;                     /* refused requests to close, for the forced end */
    u64 cl_t;
    int net;                      /* 1 may use the network, -1 refused for this run */
    int bi;                       /* index in the built-in table or -1 */
    int lw, lh;                   /* window size at the last drawing */
    int ccur, ccx, ccy;           /* text cursor */
    char arg[VFS_PATH];           /* file or address it was started with */
    char grant[NGRANT][VFS_PATH]; /* files the user chose for it in a dialog */
    int ng;
} proc_t;
#define L_NET  1
#define L_INST 2
#define L_LIM  4 /* marks a limited built-in program */
#define L_RO   8 /* may only read */
#define NBG    4
static proc_t *bgp[NBG];
static proc_t *cur;
static u64 kctx, slice_end;
static u8 ustack[262144] __attribute__((aligned(16)));
u64 proc_stack_top(void) {
    return ((u64)ustack + sizeof ustack) & ~15ull;
}
void proc_enter(regs *, u64 *);
void proc_back(u64);
extern u64 *kpml4(void);
static void cr3_flush(void) {
    u64 c;
    __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3" : "=r"(c));
}
static void leave_(regs *r) {
    proc_t *p = cur;
    p->r = *r;
    __asm__ volatile("fxsave64 (%0)" ::"r"(p->fx) : "memory");
    proc_back(kctx);
}
static void proc_run(proc_t *p) {
    if (cur)
        return;
    __asm__("cli");
    cur = p;
    kpml4()[SLOT] = p->root | PTE_P | PTE_W | PTE_U;
    cr3_flush();
    __asm__ volatile("fxrstor64 (%0)" ::"r"(p->fx));
    regs *f = (regs *)(ustack + sizeof ustack - sizeof(regs) - 16);
    *f = p->r;
    slice_end = ticks + 2;
    proc_enter(f, &kctx);
    kpml4()[SLOT] = 0;
    cr3_flush();
    cur = 0;
    __asm__("sti");
}
void proc_tick(regs *r) {
    if (cur && ticks >= slice_end)
        leave_(r);
}
void proc_fault(regs *r) {
    proc_t *p = cur;
    p->state = P_DEAD;
    ksnprintf(p->msg, 70,
              TR("Programmfehler %d bei %lx: beendet.", "Program error %d at %lx: stopped."),
              (int)r->no, r->rip);
    leave_(r);
}
/* Checks that a range belongs to the program. After that the kernel may touch it directly. */
static int uok(proc_t *p, u64 a, u64 n, int wr) {
    if (a < UBASE || a >= UEND || n > UEND - a)
        return 0;
    if (!n)
        return 1;
    for (u64 v = a & ~4095ull; v < a + n; v += 4096) {
        u64 *e = walk(p->root, v, 0);
        if (!e || !(*e & PTE_P) || (wr && !(*e & PTE_W)))
            return 0;
    }
    return 1;
}
static int ustr(proc_t *p, u64 a, char *o, int max) {
    for (int i = 0; i < max; i++) {
        if (!uok(p, a + i, 1, 0))
            return 0;
        o[i] = *(char *)(a + i);
        if (!o[i])
            return 1;
    }
    return 0;
}
static int prefix(const char *p, const char *d) {
    size_t l = strlen(d);
    return strlen(p) >= l && !memcmp(p, d, l) && (!p[l] || p[l] == '/');
}
static int upath(proc_t *p, u64 a, char *o) {
    static char t[VFS_PATH];
    if (!ustr(p, a, t, VFS_PATH))
        return 0;
    if (vfs_norm(p->base, t, o, VFS_PATH))
        return 0;
    if (p->sys)
        return 1;
    for (int i = 0; i < p->ng; i++)
        if (!strcmp(o, p->grant[i]))
            return 1;
    return prefix(o, p->base) || prefix(o, "/home") || prefix(o, "/downloads") ||
           prefix(o, "/drives");
}
/* Folders a normal program may fill, but not delete or move: its own folder and the shared ones */
static int keep(proc_t *p, const char *o) {
    return !p->sys && (!strcmp(o, p->base) || !strcmp(o, "/home") || !strcmp(o, "/downloads"));
}
static void grant(proc_t *p, const char *path) {
    if (p->sys)
        return;
    for (int i = 0; i < p->ng; i++)
        if (!strcmp(p->grant[i], path))
            return;
    if (p->ng == NGRANT) {
        memmove(p->grant[0], p->grant[1], (NGRANT - 1) * VFS_PATH);
        p->ng--;
    }
    strlcpy_(p->grant[p->ng++], path, VFS_PATH);
}
typedef struct {
    u8 *o;
    u64 n, max;
} lc_t;
static int lcb(const vfs_ent *e, void *x) {
    lc_t *c = x;
    u64 l = strlen(e->name);
    if (c->n + l + 2 > c->max)
        return 1;
    memcpy(c->o + c->n, e->name, l);
    c->n += l;
    if (e->dir)
        c->o[c->n++] = '/';
    c->o[c->n++] = 0;
    return 0;
}
static void pkey_add(proc_t *p, u32 v) {
    int n = (p->kt + 1) % 32;
    if (n != p->kh) {
        p->keys[p->kt] = v;
        p->kt = n;
    }
}
static long sys_service(proc_t *p, u64 op, u64 b, u64 c);
static int lcb2(const vfs_ent *e, void *x) {
    lc_t *c = x;
    u64 l = strlen(e->name);
    if (c->n + l + 10 > c->max)
        return 1;
    memcpy(c->o + c->n, &e->size, 8);
    c->o[c->n + 8] = (u8)e->dir;
    memcpy(c->o + c->n + 9, e->name, l + 1);
    c->n += l + 10;
    return 0;
}
/* Copies a {buffer, length} pair from the program and checks it */
static int ubuf(proc_t *p, u64 a, u8 **b, u64 *n) {
    u64 q[2];
    if (!uok(p, a, sizeof q, 0))
        return 0;
    memcpy(q, (void *)a, sizeof q);
    if (!q[1] || q[1] > (1u << 20) || !uok(p, q[0], q[1], 1))
        return 0;
    *b = (u8 *)q[0];
    *n = q[1];
    return 1;
}
/* ---------- System dialogs for programs ---------- */
#define DLGMAX 1280 /* longest input: addresses in the browser */
static u64 dlg_ticks; /* time programs spent in dialogs of the system */
static long sys_dialog(proc_t *p, u64 kind, u64 a) {
    static char t[64], m[256], io[DLGMAX];
    u64 q[5]; /* title, text, buffer, size, flags */
    if (!p->w || !uok(p, a, sizeof q, 0))
        return VE_INVAL;
    memcpy(q, (void *)a, sizeof q);
    t[0] = m[0] = 0;
    if ((q[0] && !ustr(p, q[0], t, 64)) || (q[1] && !ustr(p, q[1], m, 256)))
        return VE_INVAL;
    long v = VE_INVAL;
    u64 t0 = ticks;
    __asm__("sti");
    switch (kind) {
        case 0:
            dlg_msg(t, m);
            v = 0;
            break;
        case 1:
            v = dlg_confirm(t, m);
            break;
        case 2:
        case 3: {
            u64 max = q[3];
            if (max < 2 || max > DLGMAX || (kind == 3 && max > VFS_PATH) || !uok(p, q[2], max, 1))
                break;
            io[0] = 0;
            if (kind == 2) {
                memcpy(io, (void *)q[2], max);
                io[max - 1] = 0;
                dlg_fresh = (int)(q[4] & 1);
                v = dlg_input(t, m, io, (int)max);
            } else {
                v = dlg_pick(t, io, (int)max);
                if (v)
                    grant(p, io);
            }
            if (v)
                memcpy((void *)q[2], io, strlen(io) + 1);
            break;
        }
        case 4:
            v = dlg_savechoice();
            break;
    }
    __asm__("cli");
    dlg_ticks += ticks - t0;
    return v;
}
/* ---------- Network: TCP and DNS stay in the kernel, everything above runs in the program
 * ---------- */
static proc_t *net_owner;
static u64 netok[16]; /* packages the user allowed the internet in this session */
static int netokn;
static int net_allowed(proc_t *p) {
    if (p->sys || (p->lim & L_NET) || p->net > 0)
        return 1;
    if (p->net < 0)
        return 0;
    for (int i = 0; i < netokn; i++)
        if (netok[i] == p->hash) { /* the same package, not only the same name */
            p->net = 1;
            return 1;
        }
    char m[120];
    ksnprintf(
        m, 120,
        TR("\"%s\" möchte ins Internet. Erlauben?", "\"%s\" wants to use the internet. Allow?"),
        p->w ? p->w->title : p->name);
    u64 t0 = ticks;
    __asm__("sti");
    int y = dlg_confirm(TR("Internetzugang", "Internet access"), m);
    __asm__("cli");
    dlg_ticks += ticks - t0;
    p->net = y ? 1 : -1;
    if (y && netokn < 16)
        netok[netokn++] = p->hash;
    return y;
}
static int net_mine(proc_t *p) {
    if (net_owner && net_owner != p)
        return 0;
    net_owner = p;
    return 1;
}
#define VE_BUSY  (-12)
#define VE_AGAIN (-13)
static long sys_net(proc_t *p, u64 op, u64 b, u64 c) {
    static char t[256];
    switch (op) {
        case 0: { /* status text, returns 1 when the network is ready */
            if (!uok(p, b, c, 1) || !c)
                return VE_INVAL;
            const char *s = net_status();
            u64 l = strlen(s);
            if (l >= c)
                l = c - 1;
            memcpy((void *)b, s, l);
            ((char *)b)[l] = 0;
            return net_ready();
        }
        case 1: /* start a name lookup */
            if (!ustr(p, b, t, 256))
                return VE_INVAL;
            if (!net_allowed(p))
                return VE_DENIED;
            if (!net_mine(p))
                return VE_BUSY;
            return dns_start(t);
        case 2: { /* result of the lookup: 0 still running, 1 done (IPv4 in *c), <0 failed */
            if (net_owner != p || !uok(p, c, 4, 1))
                return VE_INVAL;
            u32 ip = 0;
            int r = dns_poll(&ip);
            if (r == 1)
                *(u32 *)c = ip;
            return r;
        }
        case 3: /* connect: b = IPv4, c = port; returns at once */
            if (!net_allowed(p))
                return VE_DENIED;
            if (!net_mine(p))
                return VE_BUSY;
            return tcp_start((u32)b, (u16)c);
        case 4: /* state: 0 connecting, 1 connected, <0 failed */
            if (net_owner != p)
                return VE_INVAL;
            return tcp_state();
        case 5: /* send: bytes taken (0 = no room just now) */
            if (net_owner != p || !uok(p, b, c, 0))
                return VE_INVAL;
            return tcp_send_nb((const void *)b, c > 65536 ? 65536 : c);
        case 6: /* receive: bytes, 0 = closed, VE_AGAIN = nothing yet */
            if (net_owner != p || !uok(p, b, c, 1))
                return VE_INVAL;
            {
                int r = tcp_recv_nb((void *)b, c > 65536 ? 65536 : c);
                return r == -2 ? VE_AGAIN : r;
            }
        case 7: /* close and give the network free for others */
            if (net_owner == p) {
                tcp_end();
                dns_cancel();
                net_owner = 0;
            }
            return 0;
        case 8: { /* ping b = IPv4, c = sequence; returns ms | ttl << 16, 1 = no answer */
            if (!net_allowed(p))
                return VE_DENIED;
            int ms = 0, ttl = 0;
            __asm__("sti");
            int r = net_ping((u32)b, (u16)c, &ms, &ttl);
            __asm__("cli");
            if (r < 0)
                return r == -2 ? VE_AGAIN : VE_IO;
            return r ? 1 : (long)ms | (long)ttl << 16;
        }
        case 9: { /* information line b into the buffer c (80 bytes): addresses of this computer,
                     only for the system programs */
            if (!p->sys)
                return VE_DENIED;
            if (!uok(p, c, 80, 1))
                return VE_INVAL;
            net_info((char *)c, 80, (int)b);
            return 0;
        }
    }
    return VE_INVAL;
}
/* ---------- Window control for programs ---------- */
static long win_control(proc_t *p, u64 op, u64 b, u64 c) {
    win_t *w = p->w;
    static char t[64];
    switch (op) {
        case 9: /* footer in the frame */
            if (!b) {
                w->foot[0] = 0;
                return 0;
            }
            if (!ustr(p, b, t, 64))
                return VE_INVAL;
            for (char *q = t; *q; q++)
                if ((u8)*q < 32)
                    *q = ' ';
            strlcpy_(w->foot, t, 64);
            return 0;
        case 10: /* text cursor at b, c; b < 0 hides it */
            p->ccur = (long)b >= 0;
            p->ccx = (int)b;
            p->ccy = (int)c;
            return 0;
        case 11: /* full screen without frame, menu bar and status line */
            w->full = b != 0;
            ui_render(); /* takes the new size at once */
            return 0;
        case 12: /* new size, centred */
            if (b < 10 || c < 3 || b > 1000 || c > 1000)
                return VE_INVAL;
            if (w->zoomed)
                win_zoom(w);
            win_resize(w, (int)b, (int)c);
            return 0;
        case 13: /* zoom: b = 1 toggles, 0 only asks */
            if (b)
                win_zoom(w);
            return w->zoomed;
        case 14: /* is the window in front? */
            return win_top() == w;
        case 15:
            win_raise(w);
            return 0;
        case 16: /* 1 keeps the violet colours, 0 follows the colour scheme */
            w->fixed = b != 0;
            return 0;
        case 17: /* no longer in the background */
            for (int i = 0; i < NBG; i++)
                if (bgp[i] == p)
                    bgp[i] = 0;
            p->bg = 0;
            return 0;
        case 18: /* screen colour of palette entry b as 0xRRGGBB */
            return (long)(ui_rgb((int)(b & 15)) & 0xFFFFFF);
        case 20: /* size of the screen in cells */
            return (long)W << 16 | H;
        case 19: /* name of the program menu */
            if (!ustr(p, b, t, 20))
                return VE_INVAL;
            strlcpy_(w->aname, t, 20);
            return 0;
    }
    return VE_INVAL;
}
void proc_syscall(regs *r) {
    proc_t *p = cur;
    if (!p)
        return;
    u64 a = r->rdi, b = r->rsi, c = r->rdx;
    static char pa[VFS_PATH], pb[VFS_PATH];
    long v = VE_DENIED;
    if ((p->lim & L_RO) && (r->rax == 12 || (r->rax >= 14 && r->rax <= 16))) {
        r->rax = (u64)v; /* the viewer writes nothing */
        return;
    }
    switch (r->rax) {
        case 0:
            p->state = P_DONE;
            leave_(r);
            break;
        case 1: {
            int cw_ = b ? (int)b : p->cols, ch_ = b ? (int)c : p->rows;
            if (cw_ < 1 || cw_ > CMAX || ch_ < 1 || ch_ > RMAX) {
                v = VE_INVAL;
                break;
            }
            u64 n = (u64)cw_ * ch_ * 2;
            if (!uok(p, a, n, 0)) {
                v = VE_INVAL;
                break;
            }
            for (int y = 0; y < ch_; y++)
                for (int x = 0; x < cw_; x++) {
                    u16 q = ((u16 *)a)[y * cw_ + x];
                    p->cells[y * CMAX + x] =
                        (u32)(q & 255) | (u32)((q >> 8) & 15) << 8 | (u32)(q >> 12) << 16;
                }
            p->sc = cw_;
            p->sr = ch_;
            prog_dirty = 1;
            v = 0;
            break;
        }
        case 18: {
            int cw_ = (int)b, ch_ = (int)c;
            if (cw_ < 1 || cw_ > CMAX || ch_ < 1 || ch_ > RMAX) {
                v = VE_INVAL;
                break;
            }
            u64 n = (u64)cw_ * ch_ * 4;
            if (!uok(p, a, n, 0)) {
                v = VE_INVAL;
                break;
            }
            for (int y = 0; y < ch_; y++)
                memcpy(p->cells + y * CMAX, (u32 *)a + y * cw_, (u64)cw_ * 4);
            p->sc = cw_;
            p->sr = ch_;
            prog_dirty = 1;
            v = 0;
            break;
        }
        case 2:
            if (p->kh != p->kt) {
                v = 0;
                break;
            }
            r->rax = 0;
            p->state = P_WAIT;
            p->wake = (long)a < 0 ? ~0ull : ticks + (a + 9) / 10;
            leave_(r);
            break;
        case 3:
            if (p->kh == p->kt)
                v = -1;
            else {
                v = p->keys[p->kh];
                p->kh = (p->kh + 1) % 32;
            }
            break;
        case 4: {
            int cw_ = cw(p->w), ch_ = ch(p->w);
            if (cw_ > CMAX)
                cw_ = CMAX;
            if (ch_ > RMAX)
                ch_ = RMAX;
            v = (long)cw_ << 16 | ch_;
            break;
        }
        case 26: { /* part of a file: b points to {buffer, offset, length} */
            u64 q[3];
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!uok(p, b, sizeof q, 0)) {
                v = VE_INVAL;
                break;
            }
            memcpy(q, (void *)b, sizeof q);
            if (q[2] > (512u << 10))
                q[2] = 512u << 10; /* small parts keep the rest of the system running */
            if (!uok(p, q[0], q[2], 1)) {
                v = VE_INVAL;
                break;
            }
            __asm__("sti");
            v = fsx_read_at(pa, (void *)q[0], q[1], q[2]);
            __asm__("cli");
            break;
        }
        case 25: { /* program menu in the menu bar: "item|item|item" */
            static char t[200];
            if (!p->w)
                break;
            if (ustr(p, a, t, 200)) {
                t[119] = 0;
                for (char *q = t; *q; q++)
                    if ((u8)*q < 32 || (u8)*q > 126)
                        *q = '?';
                strlcpy_(p->w->amenu, t, 120);              /* programs keep the old limit */
                if ((p->sys || p->lim) && ustr(p, a, p->w->amenu, 200)) /* built-in: UTF-8 */
                    for (char *q = p->w->amenu; *q; q++)
                        if ((u8)*q < 32)
                            *q = '?';
                if (!p->w->aname[0])
                    strlcpy_(p->w->aname, p->w->title, 20);
                v = 0;
            } else
                v = VE_INVAL;
            break;
        }
        case 5: {
            static char t[40];
            if (ustr(p, a, t, 40)) {
                win_title(p->w, t);
                v = 0;
            } else
                v = VE_INVAL;
            break;
        }
        case 22:
            if (!p->w)
                break;
            if (!a) {
                v = snd_stream(0, 0);
                break;
            }
            if (b > 65536 || !uok(p, a, b * 4, 0)) {
                v = VE_INVAL;
                break;
            }
            v = snd_stream((const short *)a, (u32)b);
            break;
        case 23:
            if (!p->w)
                break;
            v = 0;
            switch (a) {
                case 0:
                    snd_stream_flush();
                    break;
                case 1:
                    v = (long)snd_stream_pos();
                    break;
                case 2:
                    for (int i = 0; i < NBG; i++)
                        if (!bgp[i] || bgp[i] == p) {
                            bgp[i] = p;
                            p->bg = 1;
                            break;
                        }
                    break;
                case 3:
                    p->esc = (int)b;
                    break;
                case 8:
                    p->f1 = (int)b;
                    break;
                case 4:
                    if ((long)b >= 0 && (b > 1000 || c > 1000))
                        v = VE_INVAL;
                    else
                        win_place(p->w, -1, -1, (int)b, (int)c);
                    break;
                case 5: /* back to full size: first the normal size of the package, so
                           F5 later returns to a usable window and not to the mini size */
                    if (p->w->zoomed)
                        break;
                    win_resize(p->w, p->cols, p->rows);
                    win_zoom(p->w);
                    break;
                case 6:
                    snd_stream_gain((int)b, (int)c);
                    break;
                case 7:
                    snd_stream_pause((int)b);
                    break;
                default:
                    v = win_control(p, a, b, c);
            }
            break;
        case 24: { /* Picture for pixel cells: width divisible by 4, height by 8, 256 colours per
                      pixel. Bits 16-20 of c choose one of 32 slots. */
            int iw = (int)b, ih = (int)(c & 0xFFFF), sl = (int)((c >> 16) & 31);
            if (!p->w || iw < 4 || ih < 8 || iw > 960 || ih > 800 || (iw & 3) || (ih & 7) ||
                !uok(p, a, (u64)iw * ih, 0)) {
                v = VE_INVAL;
                break;
            }
            u64 need = (u64)iw * ih, have = p->im[sl].px ? p->im[sl].cap : 0;
            if (have < need) {
                if (p->imbytes - have + need > IMGMAX) {
                    v = VE_NOSPC;
                    break;
                }
                p->imbytes -= have;
                kfree(p->im[sl].px);
                p->im[sl].px = kmalloc(need);
                p->im[sl].w = p->im[sl].h = 0;
                p->im[sl].cap = 0;
                if (!p->im[sl].px) {
                    v = VE_NOSPC;
                    break;
                }
                p->im[sl].cap = need;
                p->imbytes += need;
            }
            memcpy(p->im[sl].px, (void *)a, (u64)iw * ih);
            p->im[sl].w = iw;
            p->im[sl].h = ih;
            p->im[sl].gid = gfx_newid();
            p->dirty = 1;
            prog_dirty = 1;
            v = 0;
            break;
        }
        case 27: { /* folder listing with sizes: u64 size, u8 folder, name, 0 */
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!uok(p, b, c, 1)) {
                v = VE_INVAL;
                break;
            }
            lc_t l = {(u8 *)b, 0, c};
            __asm__("sti");
            v = fsx_list(pa, lcb2, &l);
            __asm__("cli");
            if (!v)
                v = (long)l.n;
            break;
        }
        case 28:
            v = sys_dialog(p, a, b);
            break;
        case 29:
            v = sys_net(p, a, b, c);
            break;
        case 30:
            v = sys_service(p, a, b, c);
            break;
        case 21:
            snd_tone((int)a, (int)b, (int)(c & 255), (int)((c >> 8) & 15),
                     (int)((c >> 16) & 0xFFFF));
            v = 0;
            break;
        case 6:
            v = fs_now();
            break;
        case 7:
            v = (long)(ticks * 10);
            break;
        case 8: {
            u64 inc = (a + 4095) & ~4095ull;
            if (inc > MAXHEAP || p->brk - UHEAP + inc > MAXHEAP) {
                v = -1;
                break;
            }
            v = (long)p->brk;
            u64 e = p->brk + inc, o = p->brk;
            for (; o < e; o += 4096)
                if (!map_page(p->root, o, PTE_W | PTE_NX)) {
                    v = -1;
                    break;
                }
            p->brk = o;
            break;
        }
        case 9: /* held keys only for the window in front and only while no menu or dialog of
                   the system is open: otherwise a program could follow what is typed elsewhere */
            v = ui_keys_free && p->w && win_top() == p->w ? key_down((u8)a) : 0;
            break;
        case 10:
            v = cfg_lang;
            break;
        case 11:
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!uok(p, b, c, 1)) {
                v = VE_INVAL;
                break;
            }
            __asm__("sti");
            v = fsx_read(pa, (void *)b, c);
            __asm__("cli");
            break;
        case 12:
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!uok(p, b, c, 0)) {
                v = VE_INVAL;
                break;
            }
            __asm__("sti");
            v = fsx_write(pa, (void *)b, c, fs_now());
            __asm__("cli");
            break;
        case 13: {
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!uok(p, b, 16, 1)) {
                v = VE_INVAL;
                break;
            }
            vfs_ent e;
            __asm__("sti");
            v = fsx_stat(pa, &e);
            __asm__("cli");
            if (!v) {
                ((u64 *)b)[0] = e.size;
                ((u64 *)b)[1] = (u64)e.dir;
            }
            break;
        }
        case 14:
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            __asm__("sti");
            v = fsx_mkdir(pa, fs_now());
            __asm__("cli");
            break;
        case 15:
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!strcmp(pa, p->base) || keep(p, pa)) {
                v = VE_DENIED;
                break;
            }
            __asm__("sti");
            v = fsx_remove(pa);
            __asm__("cli");
            break;
        case 16:
            if (!upath(p, a, pa) || !upath(p, b, pb)) {
                v = VE_DENIED;
                break;
            }
            if (!strcmp(pa, p->base) || keep(p, pa) || keep(p, pb)) {
                v = VE_DENIED;
                break;
            }
            __asm__("sti");
            v = fsx_rename(pa, pb);
            __asm__("cli");
            break;
        case 17: {
            if (!upath(p, a, pa)) {
                v = VE_DENIED;
                break;
            }
            if (!uok(p, b, c, 1)) {
                v = VE_INVAL;
                break;
            }
            lc_t l = {(u8 *)b, 0, c};
            __asm__("sti");
            v = fsx_list(pa, lcb, &l);
            __asm__("cli");
            if (!v)
                v = (long)l.n;
            break;
        }
        default:
            v = VE_INVAL;
    }
    r->rax = (u64)v;
}
int prog_dirty; /* a program has drawn: the screen should be drawn again */
/* ---------- Window ---------- */
static void pw_draw(win_t *w) {
    proc_t *p = w->st;
    if (cw(w) != p->lw || ch(w) != p->lh) { /* new size: wake the program so it draws again */
        p->lw = cw(w);
        p->lh = ch(w);
        if (p->state == P_WAIT)
            p->wake = 0;
    }
    cfill(w, AT(C_VOID, C_LAV));
    int of = ui_fixed;
    int ox = (cw(w) - p->sc) / 2, oy = (ch(w) - p->sr) / 2;
    if (ox < 0)
        ox = 0;
    if (oy < 0)
        oy = 0;
    for (int y = 0; y < p->sr && y < ch(w); y++)
        for (int x = 0; x < p->sc && x < cw(w); x++) {
            u32 c = p->cells[y * CMAX + x];
            if (c >> 31) {
                int ix = (c >> 8) & 255, iy = (c >> 16) & 127, sl = (c >> 23) & (NIMG - 1);
                ui_fixed = w->fixed | (c & 1);
                if (p->im[sl].px && ix < p->im[sl].w / 4 && iy < p->im[sl].h / 8)
                    cputg(w, ox + x, oy + y, p->im[sl].gid, p->im[sl].px, p->im[sl].w, ix, iy);
                else
                    cput8(w, ox + x, oy + y, ' ', 0, 0);
                continue;
            }
            ui_fixed = w->fixed | ((c >> 24) & 1); /* bit 24: this cell keeps the violet colours */
            cput8(w, ox + x, oy + y, (u8)c, (u8)(c >> 8), (u8)(c >> 16));
        }
    ui_fixed = of;
    if (p->ccur && p->ccx >= 0 && p->ccy >= 0 && p->ccx < cw(w) && p->ccy < ch(w)) {
        w->cur = 1;
        w->cx = ox + p->ccx;
        w->cy = oy + p->ccy;
    }
    if (p->state == P_DEAD) {
        ctext(w, 1, 0, AT(C_VOID, C_HOT), p->msg);
        ctext(w, 1, 1, AT(C_VOID, C_LAV),
              TR("Esc schließt das Fenster.", "Esc closes the window."));
    }
}
static void pw_step(win_t *w) {
    proc_t *p = w->st;
    if (p->state == P_RUN || (p->state == P_WAIT && (p->kh != p->kt || ticks >= p->wake))) {
        p->state = P_RUN;
        proc_run(p);
    }
    if (p->state == P_DONE)
        win_close(w);
}
static void pw_key(win_t *w, key_t k) {
    pkey_add(w->st, (u32)k.key | (u32)k.mods << 16);
}
static int runnable(proc_t *p) {
    return p->state == P_RUN || (p->state == P_WAIT && (p->kh != p->kt || ticks >= p->wake));
}
/* Closing: a program that asked for it (mode 2) gets Esc and runs at once until it has ended or
 * waits for the user again. So it can ask about unsaved changes, also when switching off. */
/* A program that keeps refusing (three times within ten seconds) can be ended anyway: no program
 * may stay on the screen against the will of the user, also not in full screen. */
static int refused(proc_t *p) {
    if (ticks - p->cl_t > 1000)
        p->cl_n = 0;
    p->cl_t = ticks;
    if (++p->cl_n < 3)
        return 0;
    p->cl_n = 0;
    char m[120];
    ksnprintf(m, 120,
              TR("\"%s\" lässt sich nicht schließen. Sofort beenden? Nicht Gespeichertes geht "
                 "verloren.",
                 "\"%s\" does not close. End it now? Anything not saved is lost."),
              p->w ? p->w->title : p->name);
    return dlg_confirm(TR("Programm beenden", "End program"), m);
}
static int pw_close(win_t *w) {
    proc_t *p = w->st;
    if (p->state > P_WAIT)
        return 1;
    if (p->esc == 2) {
        if (cur)
            return 0;
        pkey_add(p, K_ESC | (ui_quitting ? 8u << 16 : 0)); /* 8: switching off */
        u64 end = ticks + 300;
        while (p->state <= P_WAIT && ticks < end) {
            if (p->state == P_WAIT && p->kh == p->kt)
                break; /* it has taken Esc and waits again: it stays open */
            if (runnable(p)) {
                p->state = P_RUN;
                u64 d0 = dlg_ticks;
                proc_run(p);
                end += dlg_ticks - d0; /* time in a dialog does not count, busy time does */
            } else
                idle();
        }
        return p->state > P_WAIT || refused(p);
    }
    if (p->esc && !ui_quitting) {
        pkey_add(p, K_ESC);
        return refused(p);
    }
    return 1;
}
void prog_bg(void) {
    if (cur)
        return;
    for (int i = 0; i < NBG; i++) {
        proc_t *p = bgp[i];
        if (p && runnable(p)) {
            p->state = P_RUN;
            proc_run(p);
        }
    }
}
static proc_t *single_p[24];
static void pw_free(win_t *w) {
    proc_t *p = w->st;
    for (int i = 0; i < NIMG; i++)
        kfree(p->im[i].px);
    if (net_owner == p) {
        tcp_end();
        dns_cancel();
        net_owner = 0;
    }
    if (p->bi >= 0 && single_p[p->bi] == p)
        single_p[p->bi] = 0;
    if (p->bg) {
        snd_stream_flush();
        snd_stream_pause(0);
        snd_stream_gain(256, 256);
        for (int i = 0; i < NBG; i++)
            if (bgp[i] == p)
                bgp[i] = 0;
    }
    as_free(p->root);
    kfree(p->cells);
    kfree(p);
}
static const app_t pw_app = {pw_draw, pw_key, pw_step, pw_close, pw_free, pw_step};
int prog_wants(win_t *w, int key) {
    if (w->app != &pw_app)
        return 0;
    proc_t *p = w->st;
    return key == K_F1 && p->f1 && p->state <= P_WAIT;
}
static proc_t *lastp;
static int load(const u8 *b, u64 n, const char *base) {
    const vpk_h *h = (const vpk_h *)b;
    const u8 *el = b + VPK_HDR;
    u64 en = h->elf, entry;
    seg_t s[8];
    int ns;
    if (!elf_check(el, en, &entry, s, &ns))
        return VE_FORMAT;
    proc_t *p = kmalloc(sizeof *p);
    if (!p)
        return VE_NOSPC;
    memset(p, 0, sizeof *p);
    p->hash = 0xcbf29ce484222325ull; /* FNV-1a over the whole package */
    for (u64 i = 0; i < n; i++)
        p->hash = (p->hash ^ b[i]) * 0x100000001b3ull;
    p->cells = kmalloc(CMAX * RMAX * 4);
    void *rt = pg_alloc();
    if (!p->cells || !rt) {
        if (rt)
            pg_put(rt);
        kfree(p->cells);
        kfree(p);
        return VE_NOSPC;
    }
    p->root = phys(rt);
    p->cols = h->cols;
    p->rows = h->rows;
    for (int i = 0; i < CMAX * RMAX; i++)
        p->cells[i] = (u32)' ' | (u32)C_LAV << 8 | (u32)C_VOID << 16;
    p->sc = h->cols;
    p->sr = h->rows;
    for (int i = 0; i < ns; i++) {
        seg_t *q = &s[i];
        u64 fl = ((q->flags & 2) ? PTE_W : 0) | ((q->flags & 1) ? 0 : PTE_NX);
        for (u64 o = 0; o < q->memsz; o += 4096) {
            u8 *pg = map_page(p->root, q->vaddr + o, fl);
            if (!pg)
                goto oom;
            if (o < q->filesz) {
                u64 k = q->filesz - o;
                if (k > 4096)
                    k = 4096;
                memcpy(pg, el + q->off + o, k);
            }
        }
    }
    for (u64 o = 0; o < USTKSZ; o += 4096)
        if (!map_page(p->root, USTACK + o, PTE_W | PTE_NX))
            goto oom;
    p->brk = UHEAP;
    p->fx = (u8 *)(((u64)p->fxraw + 15) & ~15ull);
    memset(p->fx, 0, 512);
    p->fx[0] = 0x7F;
    p->fx[1] = 3;
    p->fx[24] = 0x80;
    p->fx[25] = 0x1F;
    p->r.rip = entry;
    p->r.rsp = UEND - 16;
    p->r.cs = 0x23;
    p->r.ss = 0x1B;
    p->r.rflags = 0x202;
    strlcpy_(p->name, h->name, 24);
    strlcpy_(p->base, base, VFS_PATH);
    p->bi = -1;
    p->state = P_RUN;
    lastp = p;
    p->w = win_open(h->title, (80 - h->cols - 2) / 2, 1 + (21 - h->rows) / 2, h->cols + 2,
                    h->rows + 2, &pw_app, p);
    if (!p->w)
        goto oom;
    p->w->fixed = 1; /* programs keep their own (violet) colours */
    return 0;
oom:
    as_free(p->root);
    kfree(p->cells);
    kfree(p);
    return VE_NOSPC;
}
/* Reads and checks a package */
static u8 *pkg_read(const char *path, u64 *n, int *err) {
    vfs_ent e;
    int r = fsx_stat(path, &e);
    if (r) {
        *err = r;
        return 0;
    }
    if (e.dir || e.size > VPK_MAX || e.size < VPK_HDR + 64) {
        *err = VE_FORMAT;
        return 0;
    }
    u8 *b = kmalloc(e.size + 1);
    if (!b) {
        *err = VE_NOSPC;
        return 0;
    }
    int64_t l = fsx_read(path, b, e.size);
    if (l < 0) {
        *err = (int)l;
        kfree(b);
        return 0;
    }
    u64 en;
    seg_t s[8];
    int ns;
    if (!vpk_check(b, (u64)l) || !elf_check(b + VPK_HDR, (u64)l - VPK_HDR, &en, s, &ns)) {
        kfree(b);
        *err = VE_FORMAT;
        return 0;
    }
    *n = (u64)l;
    return b;
}
int prog_run_file(const char *path, const char *base) {
    u64 n;
    int e = 0;
    u8 *b = pkg_read(path, &n, &e);
    if (!b)
        return e;
    e = load(b, n, base ? base : fsx_home());
    kfree(b);
    return e;
}
static int safe_name(const char *s) {
    if (!*s || strlen(s) > 23)
        return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_'))
            return 0;
    return 1;
}
/* Built-in programs: packages like the ones from the SDK, only already inside the kernel. The
 * system programs among them may use the whole file system and the system services; everything
 * else about them is as protected as any other program. */
#include "apps.h"
#define K_OPEN 0x1BE /* a running single program got a new file or address (svc 1 reads it) */
static const struct {
    const char *name;
    const u8 *d;
    u64 n;
    u8 sys, single, lim;
} builtin[] = {{"snake", pkg_snake, sizeof pkg_snake, 0, 0, 0},
               {"bricks", pkg_bricks, sizeof pkg_bricks, 0, 0, 0},
               {"novastrike", pkg_novastrike, sizeof pkg_novastrike, 0, 0, 0},
               {"xzn", pkg_xzn, sizeof pkg_xzn, 0, 0, 0},
               {"pong", pkg_pong, sizeof pkg_pong, 0, 0, 0},
               {"invaders", pkg_invaders, sizeof pkg_invaders, 0, 0, 0},
               {"chomp", pkg_chomp, sizeof pkg_chomp, 0, 0, 0},
               {"mines", pkg_mines, sizeof pkg_mines, 0, 0, 0},
               {"calc", pkg_calc, sizeof pkg_calc, 1, 0, 0},
               {"calendar", pkg_calendar, sizeof pkg_calendar, 1, 1, 0},
               {"clock", pkg_clock, sizeof pkg_clock, 1, 1, 0},
               {"write", pkg_write, sizeof pkg_write, 1, 0, 0},
               {"view", pkg_view, sizeof pkg_view, 0, 0, L_LIM | L_RO},
               {"paint", pkg_paint, sizeof pkg_paint, 1, 1, 0},
               {"files", pkg_files, sizeof pkg_files, 1, 0, 0},
               {"commands", pkg_commands, sizeof pkg_commands, 1, 0, 0},
               {"web", pkg_web, sizeof pkg_web, 0, 1, L_LIM | L_NET | L_INST}};
#define NBUILT ((int)(sizeof builtin / sizeof builtin[0]))
static int is_builtin(const char *n) {
    for (int i = 0; i < NBUILT; i++)
        if (!strcmp(builtin[i].name, n))
            return 1;
    return 0;
}
static int run_builtin(int i, const char *arg) {
    if (builtin[i].single && single_p[i] && single_p[i]->w) {
        proc_t *o = single_p[i];
        win_raise(o->w);
        if (arg && *arg) {
            strlcpy_(o->arg, arg, VFS_PATH);
            pkey_add(o, K_OPEN);
        }
        return 0;
    }
    char d[VFS_PATH];
    if (builtin[i].sys)
        strlcpy_(d, "/system", VFS_PATH);
    else if (builtin[i].lim & L_NET) { /* the browser: bookmarks, history and cookies */
        strlcpy_(d, "/system/browser", VFS_PATH);
        if (fs_ok())
            fsx_mkdir(d, fs_now());
    } else if (builtin[i].lim) { /* the viewer: the folder of the picture, for the next one */
        strlcpy_(d, fsx_home(), VFS_PATH);
        if (arg && arg[0] == '/') {
            strlcpy_(d, arg, VFS_PATH);
            char *s = strrchr(d, '/');
            s[s == d] = 0;
        }
    } else {
        ksnprintf(d, VFS_PATH, "/programs/%s/data", builtin[i].name);
        if (fs_ok()) {
            char a[VFS_PATH];
            ksnprintf(a, VFS_PATH, "/programs/%s", builtin[i].name);
            fsx_mkdir("/programs", fs_now());
            fsx_mkdir(a, fs_now());
            fsx_mkdir(d, fs_now());
        }
    }
    int r = vpk_check(builtin[i].d, builtin[i].n) ? load(builtin[i].d, builtin[i].n, d) : VE_FORMAT;
    if (r)
        return r;
    proc_t *p = lastp;
    p->bi = i;
    p->sys = builtin[i].sys;
    p->lim = builtin[i].lim;
    if (arg) {
        strlcpy_(p->arg, arg, VFS_PATH);
        if (p->lim && !(p->lim & L_NET) && arg[0] == '/')
            grant(p, arg); /* the picture itself, also when it lies in / */
    }
    if (builtin[i].single)
        single_p[i] = p;
    if (!cur) { /* first steps at once: the window shows its real title and content */
        proc_run(p);
        if (p->state == P_DONE)
            win_close(p->w);
    }
    return 0;
}
/* Starts a built-in program by name, with a file or address or without (0) */
int prog_open(const char *name, const char *arg) {
    for (int i = 0; i < NBUILT; i++)
        if (!strcmp(builtin[i].name, name)) {
            int r = run_builtin(i, arg);
            if (r)
                dlg_msg(name, vfs_errstr(r, cfg_lang));
            return r;
        }
    return VE_NOENT;
}
void app_snake(void) {
    prog_open("snake", 0);
}
void app_bricks(void) {
    prog_open("bricks", 0);
}
void app_novastrike(void) {
    prog_open("novastrike", 0);
}
void app_xzn(void) {
    prog_open("xzn", 0);
}
void app_pong(void) {
    prog_open("pong", 0);
}
void app_invaders(void) {
    prog_open("invaders", 0);
}
void app_chomp(void) {
    prog_open("chomp", 0);
}
void app_mines(void) {
    prog_open("mines", 0);
}
void app_calc(void) {
    prog_open("calc", 0);
}
void app_calendar(void) {
    prog_open("calendar", 0);
}
void app_clock(void) {
    prog_open("clock", 0);
}
void app_write(const char *file) {
    prog_open("write", file);
}
void app_view(const char *file) {
    prog_open("view", file);
}
void app_paint(void) {
    prog_open("paint", 0);
}
void app_files(void) {
    prog_open("files", 0);
}
void app_shell(void) {
    prog_open("commands", 0);
}
void app_web(void) {
    prog_open("web", 0);
}
void web_app_open(const char *url) {
    prog_open("web", url);
}
int prog_run(const char *name) {
    if (!safe_name(name))
        return VE_INVAL;
    if (is_builtin(name)) /* the built-in programs start by name too */
        return prog_open(name, 0);
    char a[VFS_PATH], d[VFS_PATH];
    ksnprintf(a, VFS_PATH, "/programs/%s/app.vpk", name);
    ksnprintf(d, VFS_PATH, "/programs/%s/data", name);
    fsx_mkdir(d, fs_now());
    return prog_run_file(a, d);
}
int prog_install(const char *path) {
    if (!fs_ok())
        return VE_NOFS;
    u64 n;
    int e = 0;
    u8 *b = pkg_read(path, &n, &e);
    if (!b)
        return e;
    const vpk_h *h = (const vpk_h *)b;
    char q[72], t[VFS_PATH], a[VFS_PATH];
    ksnprintf(q, 72, TR("%s %s installieren?", "Install %s %s?"), h->title, h->version);
    ksnprintf(t, VFS_PATH, "/programs/%s", h->name);
    ksnprintf(a, VFS_PATH, "%s/app.vpk", t);
    vfs_ent v;
    const char *ti = fsx_stat(t, &v) ? TR("Programm installieren", "Install program")
                                     : TR("Programm ersetzen", "Replace program");
    if (!dlg_confirm(ti, q)) {
        kfree(b);
        return VE_DENIED;
    }
    fsx_mkdir("/programs", fs_now());
    fsx_mkdir(t, fs_now());
    e = fsx_write(a, b, n, fs_now());
    kfree(b);
    return e;
}
int prog_uninstall(const char *name) {
    if (!safe_name(name))
        return VE_INVAL;
    char t[VFS_PATH];
    ksnprintf(t, VFS_PATH, "/programs/%s", name);
    return fsx_rmtree(t);
}
int prog_scan(prog_info *o, int max) {
    if (!fs_ok())
        return 0;
    fs_item *it;
    int n = fs_load("/programs", &it), c = 0;
    if (n < 0) {
        kfree(it);
        return 0;
    }
    static u8 h[VPK_HDR];
    for (int i = 0; i < n && c < max; i++) {
        if (!it[i].dir)
            continue;
        char a[VFS_PATH];
        ksnprintf(a, VFS_PATH, "/programs/%s/app.vpk", it[i].name);
        if (fsx_read(a, h, VPK_HDR) != VPK_HDR)
            continue;
        vpk_h *v = (vpk_h *)h;
        if (memcmp(v->magic, "VPK1", 4) || !nulstr(v->name, 24, 1) || !nulstr(v->title, 24, 1) ||
            strcmp(v->name, it[i].name) || is_builtin(v->name))
            continue;
        int ok = 1;
        for (int k = 0; k < 3; k++)
            ok &= nulstr(v->icon[k], 7, 0);
        if (!ok)
            continue;
        memcpy(o[c].name, v->name, 24);
        memcpy(o[c].title, v->title, 24);
        memcpy(o[c].icon, v->icon, 21);
        c++;
    }
    kfree(it);
    return c;
}
/* ---------- System services for programs ---------- */
typedef struct {
    u8 *b;
    u64 n, max;
} rep_t;
static void rep_line(const char *l, void *x) {
    rep_t *r = x;
    u64 k = strlen(l);
    if (r->n + k + 2 > r->max)
        return;
    memcpy(r->b + r->n, l, k);
    r->n += k;
    r->b[r->n++] = '\n';
    r->b[r->n] = 0;
}
static void pci_report(int all, void (*cb)(const char *, void *), void *a) {
    static pci_t d[256];
    int n = pci_scan(d, 256), shown = 0;
    static const char *cn[] = {"?",           "Massenspeicher",
                               "Netzwerk",    "Grafik",
                               "Multimedia",  "Speicher",
                               "Bruecke",     "Schnittstelle",
                               "System",      "Eingabe",
                               "Dock",        "CPU",
                               "Seriell/USB", "Funk"},
                      *ce[] = {"?",      "Storage", "Network",    "Display", "Multimedia",
                               "Memory", "Bridge",  "Comm",       "System",  "Input",
                               "Dock",   "CPU",     "Serial/USB", "Wireless"};
    char l[80];
    for (int i = 0; i < n; i++) {
        if (!all && (d[i].cls == 6 || d[i].cls == 8 || d[i].cls == 5 || d[i].cls == 0x11 ||
                     d[i].cls == 0x13))
            continue;
        shown++;
        const char *mk = d[i].ven == 0x10DE   ? "NVIDIA"
                         : d[i].ven == 0x1002 ? "AMD"
                         : d[i].ven == 0x8086 ? "Intel"
                         : d[i].ven == 0x10EC ? "Realtek"
                         : d[i].ven == 0x1022 ? "AMD"
                                              : "";
        ksnprintf(l, 80, "%02x:%02x.%d  %04x:%04x  %s %s%s", d[i].bus, d[i].dev, d[i].fn, d[i].ven,
                  d[i].did, d[i].cls < 14 ? (cfg_lang ? ce : cn)[d[i].cls] : "?", mk,
                  d[i].cls == 4 && d[i].sub == 3 ? " (HD Audio)" : "");
        cb(l, a);
    }
    ksnprintf(l, 80,
              TR("%d von %d Geräten, Brücken und Systemteile ausgeblendet (geraete alle)",
                 "%d of %d devices, bridges and system parts hidden (devices all)"),
              shown, n);
    cb(l, a);
}
static int sys_path(proc_t *p, u64 a, char *o) {
    return a && upath(p, a, o);
}
static long ustrout(proc_t *p, u64 a, const char *s) {
    u8 *b;
    u64 n;
    if (!ubuf(p, a, &b, &n))
        return VE_INVAL;
    u64 l = strlen(s);
    if (l >= n)
        l = n - 1;
    memcpy(b, s, l);
    b[l] = 0;
    return (long)l;
}
static long sys_service(proc_t *p, u64 op, u64 b, u64 c) {
    static char pa[VFS_PATH], pb[VFS_PATH], t[80];
    long v = VE_DENIED;
    /* open, argument, home folder and the simple states are there for every program */
    if (!p->sys && op != 0 && op != 1 && !(op == 14 && b == 0) && !(op == 13 && b <= 1) &&
        !(op == 4 && (p->lim & L_INST))) /* the browser installs, the kernel asks first */
        return VE_DENIED;
    __asm__("sti");
    switch (op) {
        case 0: { /* open a built-in program, c = file or address or 0 */
            char nm[24];
            if (!ustr(p, b, nm, 24)) {
                v = VE_INVAL;
                break;
            }
            pa[0] = 0;
            if (c) {
                if (!ustr(p, c, pb, VFS_PATH)) {
                    v = VE_INVAL;
                    break;
                }
                /* only the browser takes an address, everything else is a file and goes through
                 * the access check: "x://../../system/..." is a path too */
                if (!strcmp(nm, "web")) {
                    strlcpy_(pa, pb, VFS_PATH);
                    /* the browser may go online without asking: for another program it would
                     * be a way out with data, so that program needs the permission itself */
                    int ok = p->sys || (p->lim & L_NET) || net_allowed(p);
                    __asm__("sti");
                    if (!ok)
                        break;
                } else if (!upath(p, c, pa)) {
                    v = VE_DENIED;
                    break;
                }
            }
            if (!p->sys && !strcmp(nm, "files")) /* not a way around the access check */
                break;
            v = prog_open(nm, c ? pa : 0);
            break;
        }
        case 1:
            v = ustrout(p, b, p->arg);
            break;
        case 2: { /* reports as text lines */
            u8 *o;
            u64 n;
            if (!ubuf(p, c, &o, &n)) {
                v = VE_INVAL;
                break;
            }
            rep_t r = {o, 0, n - 1};
            o[0] = 0;
            switch (b) {
                case 0:
                    usb_report(rep_line, &r);
                    break;
                case 1:
                    snd_report(rep_line, &r);
                    break;
                case 2:
                    net_diag(rep_line, &r);
                    break;
                case 3:
                case 4:
                    pci_report(b == 4, rep_line, &r);
                    break;
                case 5:
                    ksnprintf(t, 80,
                              TR("RAM %ld KB, Heap frei %ld KB", "RAM %ld KB, heap free %ld KB"),
                              mem_total_kb, heap_free() / 1024);
                    rep_line(t, &r);
                    break;
                case 6:
                    if (!fs_ok())
                        rep_line(fs_status(), &r);
                    else {
                        u64 tt, f;
                        u32 nn;
                        vfs_usage(&tt, &f, &nn);
                        ksnprintf(t, 80,
                                  TR("%ld MB frei von %ld MB, %d Dateien",
                                     "%ld MB free of %ld MB, %d files"),
                                  f >> 20, tt >> 20, nn);
                        rep_line(t, &r);
                    }
                    break;
                default:
                    v = VE_INVAL;
                    goto out;
            }
            v = (long)r.n;
            break;
        }
        case 3:
            __asm__("cli");
            reboot();
            break;
        case 4:
            v = sys_path(p, b, pa) ? prog_install(pa) : VE_INVAL;
            break;
        case 5:
            v = ustr(p, b, t, 24) ? prog_run(t) : VE_INVAL;
            break;
        case 6:
            v = sys_path(p, b, pa) ? prog_run_file(pa, 0) : VE_INVAL;
            break;
        case 7:
            v = ustr(p, b, t, 24) ? prog_uninstall(t) : VE_INVAL;
            break;
        case 8:
            v = ustr(p, b, t, 16) ? fsx_eject(t) : VE_INVAL;
            break;
        case 9:
            if (ustr(p, b, t, 16)) {
                drive_format(t);
                v = 0;
            } else
                v = VE_INVAL;
            break;
        case 10:
            v = sys_path(p, b, pa) && sys_path(p, c, pb) ? fsx_copy(pa, pb) : VE_INVAL;
            break;
        case 11:
            v = sys_path(p, b, pa) ? fsx_rmtree(pa) : VE_INVAL;
            break;
        case 12: {
            char d[60];
            if (!ustr(p, b, t, 16)) {
                v = VE_INVAL;
                break;
            }
            fsx_info(t, d, 60);
            v = ustrout(p, c, d);
            break;
        }
        case 13: /* states: 0 system disk ready, 1 drive generation, 2 folder reachable without
                    asking, 3 drive can be formatted */
            if (b == 0)
                v = fs_ok();
            else if (b == 1)
                v = (long)fsx_gen;
            else if (b == 2)
                v = sys_path(p, c, pa) ? fsx_quiet(pa) : 0;
            else if (b == 3)
                v = ustr(p, c, t, 16) ? fsx_can_format(t) : VE_INVAL;
            else
                v = VE_INVAL;
            break;
        case 14: /* texts: 0 home folder, 1 state of the system disk, 2 first drive */
            if (b == 0)
                v = ustrout(p, c, fsx_home());
            else if (b == 1)
                v = ustrout(p, c, fs_status());
            else if (b == 2) {
                if (!fsx_first(pa, VFS_PATH))
                    pa[0] = 0;
                v = ustrout(p, c, pa);
            } else
                v = VE_INVAL;
            break;
        case 15: /* new background from a picture, b = "VWP1" and 80x50 colour bytes */
            if (!uok(p, b, 4004, 0) || memcmp((void *)b, "VWP1", 4))
                v = VE_INVAL;
            else
                v = wall_use((const u8 *)b + 4);
            break;
        case 16:
            v = sys_path(p, b, pa) ? wall_open_file(pa) : VE_INVAL;
            break;
        case 17: /* is a path write protected? */
            v = sys_path(p, b, pa) ? fsx_protected(pa) : VE_INVAL;
            break;
        default:
            v = VE_INVAL;
    }
out:
    __asm__("cli");
    return v;
}
#endif
