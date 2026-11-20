#include "os.h"
/* USB 2.0 and 1.1: EHCI, UHCI and OHCI, with keyboards and hubs. For PCs without an xHCI and for
 * ports that are not wired to it. Everything is polled: control transfers wait for their end,
 * keyboard reports are looked at in the timer tick. All structures lie below 4 GB, as UHCI, OHCI
 * and many EHCI only take 32-bit addresses.
 *
 * An EHCI only talks to high speed devices itself. Low and full speed devices (most keyboards)
 * go over to a companion UHCI or OHCI on old chipsets, or sit behind the hub that newer EHCI
 * have built in, and are then reached through its transaction translator (split transactions). */

#define MAXH2 8
#define MAXD2 40
#define MAXK2 4
enum { T_EHCI = 1, T_UHCI, T_OHCI };
enum { SP_LOW, SP_FULL, SP_HIGH };

typedef struct {
    int type, nports, ncc, ok;
    pci_t pci;
    u64 io;           /* EHCI: operational registers, OHCI: registers, UHCI: I/O port */
    u8 *buf, *setup;  /* data and setup packet of control transfers */
    u32 *fl;          /* frame list (EHCI, UHCI) or HCCA (OHCI) */
    u32 *cq;          /* control: EHCI QH, UHCI QH, OHCI ED */
    u32 *ct;          /* control: transfer descriptors */
    u32 *iq;          /* UHCI: the QH all frames point to */
    u8 used[128];     /* device addresses */
    int root[16];     /* the device at each root port, -1: none */
    u8 tried[16];
} h2_t;
typedef struct {
    int on, h, addr, speed, mps0, parent, pport, root, tth, ttp, kind, nports, depth, kb, cls;
    int kid[16];
    u8 tried[16];
    u16 vid, pid;
    char ifs[40];
} d2_t;
typedef struct {
    int on, d, ep, len, tog, err;
    hidkb_t hk;
    u8 *rep;
    u32 *q, *t[2];
    int ti;
} k2_t;
static h2_t hs[MAXH2];
static d2_t ds[MAXD2];
static k2_t ks[MAXK2];
static int nh;
int usb2_keyboards;

static u64 irq_off(void) {
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
    return f;
}
static void irq_on(u64 f) {
    if (f & 0x200)
        __asm__ volatile("sti");
}
static u32 pa(const void *p) {
    return (u32)phys(p);
}
#define R32(h, o) (*(volatile u32 *)((h)->io + (o)))
#define V32(p)    (*(volatile u32 *)(p))

/* ---------- EHCI ---------- */
#define EPORT(h, n) R32(h, 0x44 + 4 * (n))
#define E_W1C       0x2Au /* change bits of PORTSC, cleared by writing 1 */
static int e_wait(h2_t *h, u32 bit, int on) {
    for (int i = 0; i < 20000; i++) {
        if (!!(R32(h, 4) & bit) == on)
            return 1;
        udelay(50);
    }
    return 0;
}
/* one control transfer on the single QH of the asynchronous list */
static int e_ctrl(h2_t *h, d2_t *d, int len, int in) {
    u32 *q = h->cq, *t0 = h->ct, *t1 = t0 + 16, *t2 = t0 + 32;
    memset(t0, 0, 192);
    memset(q, 0, 96);
    t0[0] = len ? pa(t1) : pa(t2);
    t0[1] = 1;
    t0[2] = (8u << 16) | (3 << 10) | (2 << 8) | 0x80;
    t0[3] = pa(h->setup);
    if (len) {
        t1[0] = pa(t2);
        t1[1] = 1;
        t1[2] = (1u << 31) | ((u32)len << 16) | (3 << 10) | ((in ? 1u : 0u) << 8) | 0x80;
        t1[3] = pa(h->buf);
        t1[4] = (pa(h->buf) + 4096) & ~0xFFFu;
    }
    t2[0] = 1;
    t2[1] = 1;
    t2[2] = (1u << 31) | (1 << 15) | (3 << 10) | ((len && in) ? 0u : 1u) << 8 | 0x80;
    int hs_ = d->speed == SP_HIGH, eps = hs_ ? 2 : d->speed == SP_LOW ? 1 : 0;
    q[0] = pa(q) | 2;
    q[1] = (u32)d->addr | ((u32)eps << 12) | (1 << 14) | (1 << 15) | ((u32)d->mps0 << 16) | (hs_ ? 0 : 1u << 27);
    q[2] = (1u << 30) | (hs_ ? 0 : ((u32)d->tth << 16) | ((u32)d->ttp << 23));
    q[4] = pa(t0);
    q[5] = 1;
    R32(h, 0x18) = pa(q);
    R32(h, 0) |= 1 << 5;
    e_wait(h, 1 << 15, 1);
    int r = -1;
    for (int i = 0; i < 100000; i++) { /* up to 1 s */
        u32 a = V32(&t0[2]), b = len ? V32(&t1[2]) : 0, c = V32(&t2[2]);
        if ((a | b | c) & 0x40) { /* halted: a stall or too many errors */
            r = (a | b | c) & 0x38 ? -1 : -2;
            break;
        }
        if (!(c & 0x80)) {
            r = len ? len - (int)((b >> 16) & 0x7FFF) : 0;
            break;
        }
        udelay(10);
    }
    R32(h, 0) &= ~(1u << 5);
    e_wait(h, 1 << 15, 0);
    return r;
}
static void e_karm(k2_t *k) {
    u32 *t = k->t[0];
    t[0] = 1;
    t[1] = 1;
    t[3] = pa(k->rep);
    t[2] = ((u32)k->len << 16) | (3 << 10) | (1 << 8) | 0x80;
    V32(&k->q[4]) = pa(t);
}
static void e_kinit(h2_t *h, d2_t *d, k2_t *k, int mps) {
    (void)h;
    u32 *q = k->q;
    memset(q, 0, 96);
    memset(k->t[0], 0, 64);
    int hs_ = d->speed == SP_HIGH, eps = hs_ ? 2 : d->speed == SP_LOW ? 1 : 0;
    q[0] = 1;
    q[1] = (u32)d->addr | ((u32)k->ep << 8) | ((u32)eps << 12) | ((u32)mps << 16);
    /* every frame: start in micro frame 0, the complete splits in 2 to 4 */
    q[2] = (1u << 30) | 1 | (hs_ ? 0 : (0x1Cu << 8) | ((u32)d->tth << 16) | ((u32)d->ttp << 23));
    q[5] = 1;
    e_karm(k);
}
/* returns the bytes of a finished report, -1 still waiting, -2 error */
static int e_kdone(k2_t *k) {
    u32 t = V32(&k->t[0][2]);
    if (t & 0x80)
        return -1;
    if (t & 0x40) {
        V32(&k->q[6]) = 0; /* clear the halt in the overlay */
        return -2;
    }
    return k->len - (int)((t >> 16) & 0x7FFF);
}
static void e_init(h2_t *h) {
    u64 cap = pci_bar(&h->pci, 0);
    if (!cap)
        return;
    mmio_map(cap, 0x1000);
    u32 hcs = *(volatile u32 *)(cap + 4), hcc = *(volatile u32 *)(cap + 8);
    u8 cl = *(volatile u8 *)cap;
    if (hcs == 0xFFFFFFFFu || !cl)
        return;
    h->io = cap + cl;
    h->nports = hcs & 15;
    h->ncc = (hcs >> 12) & 15;
    /* hand-over from the firmware (USB legacy support in the PCI configuration) */
    for (u32 e = (hcc >> 8) & 255, n = 0; e >= 0x40 && n < 16; n++) {
        u32 v = pci_rd(&h->pci, (u8)e);
        if ((v & 255) == 1) {
            pci_wr(&h->pci, (u8)e, v | (1u << 24));
            for (int i = 0; i < 100000 && (pci_rd(&h->pci, (u8)e) & (1u << 16)); i++)
                udelay(10);
            pci_wr(&h->pci, (u8)e, (pci_rd(&h->pci, (u8)e) & ~(1u << 16)) | (1u << 24));
            pci_wr(&h->pci, (u8)(e + 4), 0xE0000000u); /* SMIs off, events cleared */
        }
        e = (v >> 8) & 255;
    }
    R32(h, 0) &= ~1u;
    e_wait(h, 1 << 12, 1);
    R32(h, 0) = 2;
    for (int i = 0; i < 20000 && (R32(h, 0) & 2); i++)
        udelay(50);
    if (R32(h, 0) & 2)
        return;
    if (hcc & 1)
        R32(h, 0x10) = 0; /* the upper 32 bits of all addresses */
    R32(h, 8) = 0;
    for (int i = 0; i < 1024; i++)
        h->fl[i] = 1;
    R32(h, 0x14) = pa(h->fl);
    R32(h, 0x18) = pa(h->cq);
    R32(h, 0) = (8u << 16) | (1 << 4) | 1; /* run, with the periodic list */
    e_wait(h, 1 << 12, 0);
    R32(h, 0x40) = 1; /* all ports to the EHCI; low and full speed are given away below */
    udelay(5000);
    if (hcs & 16)
        for (int n = 0; n < h->nports; n++)
            EPORT(h, n) = (EPORT(h, n) & ~E_W1C) | (1 << 12);
    udelay(20000);
    h->ok = 1;
}
/* reset of a root port: the speed, or -1 when the device goes to a companion */
static int e_reset(h2_t *h, int n) {
    u32 s = EPORT(h, n);
    if (((s >> 10) & 3) == 1 && h->ncc) { /* low speed: K state on the lines */
        EPORT(h, n) = (s & ~E_W1C) | (1 << 13);
        return -1;
    }
    EPORT(h, n) = (s & ~(E_W1C | 4u)) | (1 << 8);
    udelay(50000);
    EPORT(h, n) = EPORT(h, n) & ~(E_W1C | (1u << 8));
    for (int i = 0; i < 200 && (EPORT(h, n) & (1 << 8)); i++)
        udelay(100);
    udelay(5000);
    s = EPORT(h, n);
    if (s & 4)
        return SP_HIGH;
    if (h->ncc && (s & 1)) /* full speed: to the companion */
        EPORT(h, n) = (s & ~E_W1C) | (1 << 13);
    return -1;
}

/* ---------- UHCI ---------- */
#define UPORT(h, n) ((u16)((h)->io + 0x10 + 2 * (n)))
static int u_ctrl(h2_t *h, d2_t *d, int len, int in) {
    u32 *t = h->ct;
    int n = 0, ls = d->speed == SP_LOW, mps = d->mps0 ? d->mps0 : 8, tog = 1;
    u32 cs = (3u << 27) | ((u32)ls << 26) | (1u << 23);
    u32 a = (u32)d->addr << 8;
#define TD(i) (t + 8 * (i))
    memset(t, 0, 132 * 32);
    TD(0)[1] = cs;
    TD(0)[2] = 0x2D | a | (7u << 21);
    TD(0)[3] = pa(h->setup);
    n = 1;
    for (int off = 0; off < len && n < 130; off += mps, n++) {
        int k = len - off < mps ? len - off : mps;
        TD(n)[1] = cs | (in ? 1u << 29 : 0); /* short packet detect */
        TD(n)[2] = (in ? 0x69u : 0xE1u) | a | ((u32)tog << 19) | ((u32)(k - 1) << 21);
        TD(n)[3] = pa(h->buf + off);
        tog ^= 1;
    }
    int st = n;
    TD(st)[1] = cs | (1u << 24);
    TD(st)[2] = ((len && in) ? 0xE1u : 0x69u) | a | (1u << 19) | (0x7FFu << 21);
    for (int i = 0; i < st; i++)
        TD(i)[0] = pa(TD(i + 1)) | 4;
    TD(st)[0] = 1;
    V32(&h->cq[1]) = pa(TD(0));
    int r = -1;
    for (int i = 0; i < 100000; i++) {
        int done = 1, err = 0;
        for (int j = 0; j <= st; j++) {
            u32 c = V32(&TD(j)[1]);
            if (c & (1u << 23)) {
                done = 0;
                break;
            }
            if (c & 0x760000u) {
                err = c & (1u << 22) && !(c & 0x360000u) ? -2 : -1;
                break;
            }
            /* a short packet stops the queue at that packet: go on with the status stage */
            if (j > 0 && j < st && in && ((c + 1) & 0x7FF) < (((V32(&TD(j)[2]) >> 21) + 1) & 0x7FF) &&
                V32(&h->cq[1]) == pa(TD(j))) {
                for (int x = j + 1; x < st; x++) /* the packets that are not needed: 0 bytes */
                    TD(x)[1] = (TD(x)[1] & ~((1u << 23) | 0x7FFu)) | 0x7FF;
                V32(&h->cq[1]) = pa(TD(st));
                done = 0;
                break;
            }
        }
        if (err) {
            r = err;
            break;
        }
        if (done) {
            r = 0;
            for (int j = 1; j < st; j++)
                r += (int)((V32(&TD(j)[1]) + 1) & 0x7FF);
            break;
        }
        udelay(10);
    }
    V32(&h->cq[1]) = 1;
#undef TD
    return r > len ? len : r;
}
static void u_karm(k2_t *k) {
    d2_t *d = &ds[k->d];
    u32 *t = k->t[0];
    t[0] = 1;
    t[3] = pa(k->rep);
    t[2] = 0x69 | ((u32)d->addr << 8) | ((u32)k->ep << 15) | ((u32)k->tog << 19) | ((u32)(k->len - 1) << 21);
    t[1] = (3u << 27) | ((u32)(d->speed == SP_LOW) << 26) | (1u << 23);
    V32(&k->q[1]) = pa(t);
}
static int u_kdone(k2_t *k) {
    u32 c = V32(&k->t[0][1]);
    if (c & (1u << 23))
        return -1;
    if (c & 0x760000u) {
        k->tog = 0;
        return -2;
    }
    k->tog ^= 1;
    return (int)((c + 1) & 0x7FF);
}
static void u_init(h2_t *h) {
    u32 b = pci_rd(&h->pci, 0x20);
    if (!(b & 1))
        return;
    h->io = b & 0xFFE0;
    pci_wr(&h->pci, 4, pci_rd(&h->pci, 4) | 5); /* I/O and bus master */
    pci_wr(&h->pci, 0xC0, 0x8F00);              /* legacy support (keyboard emulation) off */
    u16 io = (u16)h->io;
    outw(io, 4); /* global reset */
    udelay(50000);
    outw(io, 0);
    udelay(10000);
    outw(io, 2);
    for (int i = 0; i < 1000 && (inw(io) & 2); i++)
        udelay(10);
    outw((u16)(io + 4), 0);
    outw((u16)(io + 2), 0x3F);
    for (int i = 0; i < 1024; i++)
        h->fl[i] = pa(h->iq) | 2;
    h->iq[0] = pa(h->cq) | 2;
    h->iq[1] = 1;
    h->cq[0] = 1;
    h->cq[1] = 1;
    outw((u16)(io + 6), 0);
    outl((u16)(io + 8), pa(h->fl));
    outb((u16)(io + 0xC), 64);
    outw(io, 0x80 | 0x40 | 1); /* run, 64-byte packets, configured */
    for (h->nports = 0; h->nports < 8; h->nports++) {
        u16 s = inw(UPORT(h, h->nports));
        if (s == 0xFFFF || !(s & 0x80))
            break;
    }
    h->ok = 1;
}
#define U_KEEP 0x1244 /* enable, resume, reset, suspend: the bits that are not cleared by 1 */
static int u_reset(h2_t *h, int n) {
    u16 p = UPORT(h, n), s = inw(p);
    outw(p, (u16)((s & U_KEEP) | 0x200));
    udelay(50000);
    outw(p, (u16)(inw(p) & U_KEEP & ~0x200));
    udelay(100);
    for (int i = 0; i < 10; i++) {
        s = inw(p);
        if (!(s & 1))
            return -1;
        if (s & 0x0A) {
            outw(p, (u16)((s & U_KEEP) | 0x0A));
            continue;
        }
        if (s & 4)
            return s & 0x100 ? SP_LOW : SP_FULL;
        outw(p, (u16)((s & U_KEEP) | 4));
        udelay(10000);
    }
    return -1;
}

/* ---------- OHCI ---------- */
#define OPORT(h, n) R32(h, 0x54 + 4 * (n))
static int o_ctrl(h2_t *h, d2_t *d, int len, int in) {
    u32 *e = h->cq, *t0 = h->ct, *t1 = t0 + 8, *t2 = t0 + 16, *t3 = t0 + 24;
    memset(t0, 0, 128);
    t0[0] = (7u << 21) | (2u << 24) | (0xFu << 28); /* SETUP, DATA0 */
    t0[1] = pa(h->setup);
    t0[3] = pa(h->setup) + 7;
    t0[2] = len ? pa(t1) : pa(t2);
    if (len) {
        t1[0] = (1u << 18) | ((in ? 2u : 1u) << 19) | (7u << 21) | (3u << 24) | (0xFu << 28);
        t1[1] = pa(h->buf);
        t1[3] = pa(h->buf) + (u32)len - 1;
        t1[2] = pa(t2);
    }
    t2[0] = (((len && in) ? 1u : 2u) << 19) | (7u << 21) | (3u << 24) | (0xFu << 28);
    t2[2] = pa(t3);
    e[0] = (u32)d->addr | ((u32)(d->speed == SP_LOW) << 13) | ((u32)d->mps0 << 16);
    e[1] = pa(t3);
    e[2] = pa(t0);
    R32(h, 8) = 2; /* control list filled */
    int r = -1;
    for (int i = 0; i < 100000; i++) {
        u32 c2 = V32(&t2[0]) >> 28;
        if (V32(&e[2]) & 1) { /* halted */
            u32 c0 = V32(&t0[0]) >> 28, c1 = len ? V32(&t1[0]) >> 28 : 0;
            r = (c0 == 4 || c1 == 4 || c2 == 4) ? -2 : -1;
            break;
        }
        if (c2 != 0xF) {
            if (c2) {
                r = c2 == 4 ? -2 : -1;
                break;
            }
            u32 cbp = V32(&t1[1]);
            r = !len ? 0 : cbp ? (int)(cbp - pa(h->buf)) : len;
            break;
        }
        udelay(10);
    }
    e[0] |= 1 << 14; /* skip, until the next transfer */
    e[2] = pa(t3);
    e[0] &= ~(1u << 14);
    return r;
}
static void o_karm(k2_t *k) {
    u32 *t = k->t[k->ti], *dm = k->t[k->ti ^ 1];
    memset(dm, 0, 16);
    t[0] = (1u << 18) | (2u << 19) | (7u << 21) | (0xFu << 28);
    t[1] = pa(k->rep);
    t[3] = pa(k->rep) + (u32)k->len - 1;
    t[2] = pa(dm);
    V32(&k->q[1]) = pa(dm);
}
static void o_kinit(d2_t *d, k2_t *k, int mps) {
    u32 *e = k->q;
    memset(e, 0, 16);
    k->ti = 0;
    e[0] = (u32)d->addr | ((u32)k->ep << 7) | ((u32)(d->speed == SP_LOW) << 13) | ((u32)mps << 16);
    e[2] = pa(k->t[0]);
    o_karm(k);
}
static int o_kdone(k2_t *k) {
    u32 *t = k->t[k->ti], c = V32(&t[0]) >> 28;
    if (c == 0xF)
        return -1;
    int r = c ? -2 : V32(&t[1]) ? (int)(V32(&t[1]) - pa(k->rep)) : k->len;
    k->ti ^= 1;
    return r;
}
static void o_init(h2_t *h) {
    u64 b = pci_bar(&h->pci, 0);
    if (!b)
        return;
    mmio_map(b, 0x1000);
    h->io = b;
    if (R32(h, 0) == 0xFFFFFFFFu)
        return;
    if (R32(h, 4) & (1 << 8)) { /* the firmware (SMM) has it: ask for it */
        R32(h, 8) = 1 << 3;
        for (int i = 0; i < 100000 && (R32(h, 4) & (1 << 8)); i++)
            udelay(10);
    }
    u32 fi = R32(h, 0x34) & 0x3FFF;
    if (fi < 0x2000)
        fi = 0x2EDF;
    R32(h, 0x14) = 0xFFFFFFFFu;
    R32(h, 8) = 1; /* reset */
    for (int i = 0; i < 1000 && (R32(h, 8) & 1); i++)
        udelay(10);
    R32(h, 0x34) = ((R32(h, 0x34) & 0x80000000u) ^ 0x80000000u) | ((6 * (fi - 210) / 7) << 16) | fi;
    R32(h, 0x40) = fi * 9 / 10;
    R32(h, 0x44) = 0x628;
    R32(h, 0x18) = pa(h->fl);
    R32(h, 0x20) = pa(h->cq);
    R32(h, 0x28) = 0;
    R32(h, 0xC) = 0xFFFFFFFFu;
    h->cq[0] = 1 << 14;
    R32(h, 4) = (2u << 6) | (1 << 4) | (1 << 2) | 3; /* operational, control and periodic lists */
    u32 da = R32(h, 0x48);
    h->nports = da & 255;
    if (h->nports > 15)
        h->nports = 15;
    if (!(da & (1 << 9))) { /* ports are switched: power on */
        R32(h, 0x50) = 1 << 16;
        for (int n = 0; n < h->nports; n++)
            OPORT(h, n) = 1 << 8;
    }
    udelay(((da >> 24) * 2 + 20) * 1000);
    h->ok = 1;
}
static int o_reset(h2_t *h, int n) {
    OPORT(h, n) = 1 << 4;
    for (int i = 0; i < 100 && !(OPORT(h, n) & (1 << 20)); i++)
        udelay(1000);
    OPORT(h, n) = 1 << 20;
    udelay(10000);
    u32 s = OPORT(h, n);
    if (!(s & 1))
        return -1;
    if (!(s & 2)) {
        OPORT(h, n) = 2;
        udelay(2000);
        s = OPORT(h, n);
    }
    return s & 2 ? (s & (1 << 9) ? SP_LOW : SP_FULL) : -1;
}

/* ---------- the same for all three ---------- */
static int ctl(d2_t *d, u8 rt, u8 rq, u16 val, u16 idx, u16 len, void *data) {
    h2_t *h = &hs[d->h];
    if (len > 1024)
        len = 1024;
    u8 *s = h->setup;
    s[0] = rt;
    s[1] = rq;
    s[2] = (u8)val;
    s[3] = (u8)(val >> 8);
    s[4] = (u8)idx;
    s[5] = (u8)(idx >> 8);
    s[6] = (u8)len;
    s[7] = (u8)(len >> 8);
    int in = rt & 0x80;
    if (!in && len)
        memcpy(h->buf, data, len);
    int r = h->type == T_EHCI ? e_ctrl(h, d, len, in) : h->type == T_UHCI ? u_ctrl(h, d, len, in) : o_ctrl(h, d, len, in);
    if (r > 0 && in && data)
        memcpy(data, h->buf, (size_t)r);
    return r;
}
/* the keyboards of a controller in its periodic schedule, after a change */
static void schedule(int hi) {
    h2_t *h = &hs[hi];
    k2_t *l[MAXK2];
    int n = 0;
    for (int i = 0; i < MAXK2; i++)
        if (ks[i].on && ds[ks[i].d].h == hi)
            l[n++] = &ks[i];
    if (h->type == T_EHCI) {
        for (int i = 0; i < n; i++)
            V32(&l[i]->q[0]) = i + 1 < n ? pa(l[i + 1]->q) | 2 : 1;
        u32 f = n ? pa(l[0]->q) | 2 : 1;
        for (int i = 0; i < 1024; i++)
            V32(&h->fl[i]) = f;
    } else if (h->type == T_UHCI) {
        for (int i = 0; i < n; i++)
            V32(&l[i]->q[0]) = (i + 1 < n ? pa(l[i + 1]->q) : pa(h->cq)) | 2;
        V32(&h->iq[0]) = (n ? pa(l[0]->q) : pa(h->cq)) | 2;
    } else {
        for (int i = 0; i < n; i++)
            V32(&l[i]->q[3]) = i + 1 < n ? pa(l[i + 1]->q) : 0;
        for (int i = 0; i < 32; i++)
            V32(&h->fl[i]) = n ? pa(l[0]->q) : 0;
    }
    udelay(3000); /* the controller is done with what was taken out */
}
static void kb_arm(k2_t *k) {
    int t = hs[ds[k->d].h].type;
    if (t == T_EHCI)
        e_karm(k);
    else if (t == T_UHCI)
        u_karm(k);
    else
        o_karm(k);
}
static void detach(int di) {
    d2_t *d = &ds[di];
    if (!d->on)
        return;
    for (int p = 1; p < 16; p++)
        if (d->kid[p] >= 0) {
            detach(d->kid[p]);
            d->kid[p] = -1;
        }
    if (d->kind == 1 && d->kb >= 0) {
        ks[d->kb].on = 0;
        schedule(d->h);
        usb2_keyboards--;
        hidkb_stop();
    }
    hs[d->h].used[d->addr] = 0;
    d->on = 0;
}
static u8 tmp[1024];
static int attach(int hi, int parent, int pport, int root, int sp, int tth, int ttp, int depth) {
    h2_t *h = &hs[hi];
    int di = -1;
    for (int i = 0; i < MAXD2 && di < 0; i++)
        if (!ds[i].on)
            di = i;
    if (di < 0)
        return -1;
    d2_t *d = &ds[di];
    memset(d, 0, sizeof *d);
    for (int p = 0; p < 16; p++)
        d->kid[p] = -1;
    d->on = 1;
    d->h = hi;
    d->speed = sp;
    d->mps0 = sp == SP_HIGH ? 64 : 8;
    d->parent = parent;
    d->pport = pport;
    d->root = root;
    d->tth = tth;
    d->ttp = ttp;
    d->depth = depth;
    d->kb = -1;
    d->cls = -1;
    udelay(10000);
    u8 *b = tmp;
    int r = ctl(d, 0x80, 6, 0x100, 0, 8, b);
    if (r < 8) {
        udelay(100000);
        r = ctl(d, 0x80, 6, 0x100, 0, 8, b);
    }
    if (r < 8) {
        d->on = 0;
        return -1;
    }
    if (b[7] == 8 || b[7] == 16 || b[7] == 32 || b[7] == 64)
        d->mps0 = b[7];
    int a = 1;
    while (a < 128 && h->used[a])
        a++;
    if (a == 128 || ctl(d, 0, 5, (u16)a, 0, 0, 0) < 0) {
        d->on = 0;
        return -1;
    }
    h->used[a] = 1;
    d->addr = a;
    udelay(10000);
    if (ctl(d, 0x80, 6, 0x100, 0, 18, b) >= 12) {
        d->vid = (u16)(b[8] | b[9] << 8);
        d->pid = (u16)(b[10] | b[11] << 8);
        d->cls = b[4];
    }
    int dcls = d->cls;
    if (ctl(d, 0x80, 6, 0x200, 0, 9, b) < 9)
        return di;
    int tl = b[2] | b[3] << 8;
    if (tl > 1024)
        tl = 1024;
    tl = ctl(d, 0x80, 6, 0x200, 0, (u16)tl, b);
    if (tl < 9)
        return di;
    int cfgv = b[5], ifn = -1, ep = 0, eps = 8, cur = 0, boot = 0, hub = dcls == 9, n = 0;
    for (int o = 0; o + 1 < tl && b[o] >= 2 && o + b[o] <= tl; o += b[o]) {
        if (b[o + 1] == 4 && o + 8 < tl) {
            if (!b[o + 3] && n < 34)
                n += ksnprintf(d->ifs + n, (int)sizeof d->ifs - n, "%s%d/%d/%d", n ? " " : "", b[o + 5], b[o + 6], b[o + 7]);
            hub |= b[o + 5] == 9;
            cur = b[o + 5] == 3 && b[o + 7] == 1 && !b[o + 3] && ifn < 0;
            if (cur) {
                ifn = b[o + 2];
                boot = b[o + 6] == 1;
            }
        } else if (b[o + 1] == 5 && cur && (b[o + 2] & 0x80) && (b[o + 3] & 3) == 3 && !ep) {
            ep = b[o + 2] & 15;
            eps = (b[o + 4] | b[o + 5] << 8) & 0x7FF;
        }
    }
    if (hub) {
        if (ctl(d, 0, 9, (u16)cfgv, 0, 0, 0) < 0 || ctl(d, 0xA0, 6, 0x2900, 0, 8, b) < 3)
            return di;
        d->nports = b[2] > 15 ? 15 : b[2];
        int pg = b[5] * 2;
        for (int p = 1; p <= d->nports; p++)
            ctl(d, 0x23, 3, 8, (u16)p, 0, 0); /* port power */
        udelay((u32)(pg < 100 ? 100 : pg) * 1000);
        d->kind = 3;
        return di;
    }
    if (ifn < 0 || !ep)
        return di;
    int ki = -1;
    for (int i = 0; i < MAXK2 && ki < 0; i++)
        if (!ks[i].on)
            ki = i;
    if (ki < 0 || ctl(d, 0, 9, (u16)cfgv, 0, 0, 0) < 0)
        return di;
    /* as on the xHCI: the report descriptor, boot protocol, no repeat by the keyboard */
    memset(b, 0, sizeof tmp);
    int hn = ctl(d, 0x81, 6, 0x2200, (u16)ifn, 1024, b);
    k2_t *k = &ks[ki];
    hidkb_layout(&k->hk, b, hn > 0 ? hn : 0);
    memset(k->hk.last, 0, 8);
    if (boot)
        ctl(d, 0x21, 0x0B, 0, (u16)ifn, 0, 0);
    ctl(d, 0x21, 0x0A, 0, (u16)ifn, 0, 0);
    k->d = di;
    k->ep = ep;
    k->len = eps < 8 ? 8 : eps > 64 ? 64 : eps;
    k->tog = 0;
    k->err = 0;
    if (h->type == T_EHCI)
        e_kinit(h, d, k, eps);
    else if (h->type == T_UHCI) {
        memset(k->q, 0, 16);
        u_karm(k);
    } else
        o_kinit(d, k, eps);
    k->on = 1;
    d->kb = ki;
    d->kind = 1;
    usb2_keyboards++;
    schedule(hi);
    return di;
}
static void hub_poll(int di) {
    d2_t *d = &ds[di];
    u8 b[4];
    for (int p = 1; p <= d->nports && d->on; p++) {
        if (ctl(d, 0xA3, 0, 0, (u16)p, 4, b) < 4)
            continue;
        u32 st = b[0] | b[1] << 8, ch = b[2] | b[3] << 8;
        if (ch & 1)
            ctl(d, 0x23, 1, 16, (u16)p, 0, 0);
        if (((ch & 1) || !(st & 1)) && d->kid[p] >= 0) {
            detach(d->kid[p]);
            d->kid[p] = -1;
        }
        if ((ch & 1) || !(st & 1))
            d->tried[p] = 0;
        if (!(st & 1))
            continue;
        if (d->tried[p]) {
            if (d->kid[p] >= 0 && ds[d->kid[p]].kind == 3)
                hub_poll(d->kid[p]);
            continue;
        }
        d->tried[p] = 1;
        if (d->depth >= 5)
            continue;
        ctl(d, 0x23, 3, 4, (u16)p, 0, 0); /* reset */
        int ok = 0;
        for (int i = 0; i < 50 && !ok; i++) {
            udelay(10000);
            if (ctl(d, 0xA3, 0, 0, (u16)p, 4, b) == 4 && ((b[2] | b[3] << 8) & 0x10))
                ok = 1;
        }
        ctl(d, 0x23, 1, 20, (u16)p, 0, 0);
        if (!ok)
            continue;
        udelay(10000);
        if (ctl(d, 0xA3, 0, 0, (u16)p, 4, b) < 4 || !(b[0] & 2))
            continue;
        st = b[0] | b[1] << 8;
        int sp = st & 0x200 ? SP_LOW : st & 0x400 ? SP_HIGH : SP_FULL;
        int tth = d->tth, ttp = d->ttp;
        if (d->speed == SP_HIGH && sp != SP_HIGH) { /* this hub translates */
            tth = d->addr;
            ttp = p;
        }
        d->kid[p] = attach(d->h, di, p, d->root, sp, tth, ttp, d->depth + 1);
    }
}
/* the root ports of a controller: plugged in, pulled out */
static void root_poll(int hi) {
    h2_t *h = &hs[hi];
    for (int n = 0; n < h->nports && n < 16; n++) {
        int con, chg;
        if (h->type == T_EHCI) {
            u32 s = EPORT(h, n);
            if (s == 0xFFFFFFFFu)
                return;
            con = s & 1;
            chg = s & 2;
            if (chg)
                EPORT(h, n) = (s & ~E_W1C) | 2;
            if (s & (1 << 13)) { /* a companion has it */
                h->tried[n] = 1;
                continue;
            }
        } else if (h->type == T_UHCI) {
            u16 s = inw(UPORT(h, n));
            con = s & 1;
            chg = s & 2;
            if (chg)
                outw(UPORT(h, n), (u16)((s & U_KEEP) | 2));
        } else {
            u32 s = OPORT(h, n);
            con = s & 1;
            chg = s & (1 << 16);
            if (chg)
                OPORT(h, n) = 1 << 16;
        }
        if ((chg || !con) && h->root[n] >= 0) {
            detach(h->root[n]);
            h->root[n] = -1;
        }
        if (chg || !con)
            h->tried[n] = 0;
        if (!con)
            continue;
        if (h->tried[n]) {
            if (h->root[n] >= 0 && ds[h->root[n]].kind == 3)
                hub_poll(h->root[n]);
            continue;
        }
        h->tried[n] = 1;
        int sp = h->type == T_EHCI ? e_reset(h, n) : h->type == T_UHCI ? u_reset(h, n) : o_reset(h, n);
        if (sp >= 0)
            h->root[n] = attach(hi, -1, n + 1, n + 1, sp, 0, 0, 0);
    }
}

void usb2_add(const pci_t *c) {
    if (nh >= MAXH2)
        return;
    h2_t *h = &hs[nh];
    memset(h, 0, sizeof *h);
    h->pci = *c;
    h->type = c->pif == 0x20 ? T_EHCI : c->pif == 0x10 ? T_OHCI : T_UHCI;
    nh++;
}
void usb2_start(void) {
    if (!nh)
        return;
    for (int i = 0; i < MAXK2; i++) { /* the keyboards' structures, once */
        u8 *m = dma32_alloc(1024);
        if (!m) {
            nh = 0;
            return;
        }
        ks[i].q = (u32 *)m;
        ks[i].t[0] = (u32 *)(m + 128);
        ks[i].t[1] = (u32 *)(m + 192);
        ks[i].rep = m + 256;
    }
    u64 f = irq_off();
    /* EHCI first: it gives low and full speed devices to its companions */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nh; i++) {
            h2_t *h = &hs[i];
            if ((h->type == T_EHCI) != !pass)
                continue;
            u8 *m = dma32_alloc(16384);
            if (!m)
                continue;
            h->fl = (u32 *)m;           /* 4 KB: frame list, or HCCA */
            h->buf = m + 4096;          /* 1 KB */
            h->setup = m + 5120;
            h->cq = (u32 *)(m + 5248);  /* 96 bytes */
            h->iq = (u32 *)(m + 5376);
            h->ct = (u32 *)(m + 8192);  /* UHCI: up to 132 descriptors of 32 bytes */
            for (int n = 0; n < 16; n++)
                h->root[n] = -1;
            pci_d0(&h->pci);
            pci_enable(&h->pci);
            if (h->type == T_EHCI)
                e_init(h);
            else if (h->type == T_UHCI)
                u_init(h);
            else
                o_init(h);
            if (h->ok)
                root_poll(i);
        }
        if (!pass)
            udelay(100000); /* the companions see what the EHCI gave away */
    }
    irq_on(f);
}
void usb2_hotplug(void) {
    static u64 next;
    if (!nh || usb_hold || ticks < next)
        return;
    next = ticks + 50;
    u64 f = irq_off();
    for (int i = 0; i < MAXK2; i++)
        if (ks[i].on && ks[i].err) { /* a keyboard that stopped after an error */
            ks[i].err = 0;
            kb_arm(&ks[i]);
        }
    for (int i = 0; i < nh; i++)
        if (hs[i].ok)
            root_poll(i);
    irq_on(f);
}
void usb2_poll(void) {
    for (int i = 0; i < MAXK2; i++) {
        k2_t *k = &ks[i];
        if (!k->on || k->err)
            continue;
        int t = hs[ds[k->d].h].type;
        int r = t == T_EHCI ? e_kdone(k) : t == T_UHCI ? u_kdone(k) : o_kdone(k);
        if (r == -1)
            continue;
        if (r == -2) {
            k->err = 1; /* armed again in usb2_hotplug */
            if (t == T_OHCI)
                V32(&k->q[2]) &= ~1u; /* the halt */
            continue;
        }
        if (r > 0)
            hidkb_input(&k->hk, k->rep, r);
        kb_arm(k);
    }
}
int usb2_count(void) {
    return nh;
}
static const char *const tn[] = {"", "USB 2.0 (EHCI)", "USB 1.1 (UHCI)", "USB 1.1 (OHCI)"};
static void dev_lines(int hi, int parent, int depth, void (*cb)(const char *, void *), void *a) {
    char l[80];
    for (int i = 0; i < MAXD2; i++) {
        d2_t *d = &ds[i];
        if (!d->on || d->h != hi || d->parent != parent)
            continue;
        static const char *const sp[] = {"Low Speed", "Full Speed", "High Speed"};
        const char *what = d->kind == 1 ? TR("Tastatur", "keyboard") : d->kind == 3 ? "Hub" : TR("nicht unterstuetzt", "not supported");
        static const char ind[] = "            ";
        ksnprintf(l, 80, "%s%d  %04x:%04x  %s, %s  %s", ind + 10 - 2 * (depth > 5 ? 5 : depth), d->pport, d->vid, d->pid, what, sp[d->speed], d->ifs);
        cb(l, a);
        if (d->kind == 3)
            dev_lines(hi, i, depth + 1, cb, a);
    }
}
void usb2_report(void (*cb)(const char *, void *), void *a) {
    char l[80];
    u64 f = irq_off();
    for (int i = 0; i < nh; i++) {
        h2_t *h = &hs[i];
        if (!h->ok) {
            ksnprintf(l, 80, TR("%s: startet nicht", "%s: does not start"), tn[h->type]);
            cb(l, a);
            continue;
        }
        ksnprintf(l, 80, TR("%s: %d Ports", "%s: %d ports"), tn[h->type], h->nports);
        cb(l, a);
        dev_lines(i, -1, 0, cb, a);
    }
    irq_on(f);
}
